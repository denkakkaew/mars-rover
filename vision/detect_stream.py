#!/usr/bin/env python3
"""Live rock detection on the host, from the XIAO's Wi-Fi stream — step 2.0e (host side).

The board stays a camera. It serves MJPEG exactly as it does for capture (`xiaostream`,
step 2.0b.2), and everything else happens here, where a laptop CPU is roughly a hundred
times faster than the ESP32-S3 and a model can be swapped without reflashing anything.

    python vision/detect_stream.py --url http://192.168.1.39
    python vision/detect_stream.py --url http://192.168.1.39 --threshold 0.7
    python vision/detect_stream.py --url http://192.168.1.39 --no-console
    python vision/detect_stream.py --bench 200          # measure, no window
    python vision/detect_stream.py --source ./some/frames/   # replay stills, no board

WHAT IT EXPECTS. An Edge Impulse **TensorFlow Lite** export of a FOMO model, dropped in
vision/model/ as a .tflite file. Not the C++ or Arduino library — those are for the board.
The .tflite is the same trained model in a different wrapper, so no retraining is involved.

WHAT FOMO ACTUALLY OUTPUTS, because it is not a box. The model divides its input into a
grid -- 20x20 cells for a 160 px input -- and gives every cell a probability per class,
with channel 0 meaning "background". There are no box coordinates anywhere in the output.
What this script does is take the cells that are confidently some rock, join the ones that
touch into a single blob, and report each blob's centre. A rock that fills many cells and
a rock that fills one both come back as one point, which is what the mission needs: the
console wants to know which rock and roughly where, not its outline.

THE CENTRE CROP MATTERS. The model was trained with "fit shortest axis", so a 640x480
frame reaches it as the middle 480x480 scaled down. The outer 80 px columns are visible in
the video and invisible to the detector. Coordinates are mapped back to full-frame pixels
before anything is drawn, so the overlay lines up with the video.

ONLY CONFIDENT ROCKS COUNT. A detection whose best cell is under --min-confidence (70% by
default) is neither drawn nor reported. --threshold still decides which cells join a blob;
--min-confidence decides whether the blob as a whole is believed.

THE CONSOLE IS TOLD. Every confident detection is POSTed to the operator console's
detection API (console/scripts/detection_api.gd, default http://127.0.0.1:8765), which
adds the element to its session log unless that name is already there. The posts go from
a background thread and each element is re-sent at most every --report-every seconds, so
a console that is closed or slow costs the video nothing.

Keys in the window: q or Esc quits, s saves the current frame with its overlay, space
pauses.
"""

from __future__ import annotations

import argparse
import json
import queue
import sys
import threading
import time
import urllib.request
from collections import deque
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
MODEL_DIR = HERE / "model"
MANIFEST = HERE / "dataset" / "manifest.json"
SHOTS = HERE / "detections"

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

# FOMO's channel 0 is "no rock here". It is a real class in the output even though it was
# never drawn as a box -- the empty-floor frames taught it.
BACKGROUND_CHANNEL = 0

WINDOW = "rock detection"

DEFAULT_CONSOLE = "http://127.0.0.1:8765"


# --------------------------------------------------------------------------- model


class Fomo:
    """An Edge Impulse FOMO model, quantised or not, behind one predict() call."""

    def __init__(self, path: Path, labels: list[str]):
        from ai_edge_litert.interpreter import Interpreter

        self.interp = Interpreter(model_path=str(path))
        self.interp.allocate_tensors()
        self.inp = self.interp.get_input_details()[0]
        self.out = self.interp.get_output_details()[0]

        _, self.height, self.width, channels = self.inp["shape"]
        if channels != 3:
            sys.exit("this model wants %d input channels; only RGB is handled here"
                     % channels)

        self.labels = labels
        out_classes = int(self.out["shape"][-1])
        # The exported model carries no label names, so a mismatch here means the labels
        # came from the wrong training run -- worth catching now rather than seeing the
        # wrong element announced later.
        if len(labels) + 1 != out_classes:
            print("warning: model has %d output channels (background + %d classes) but "
                  "%d label(s) were supplied: %s"
                  % (out_classes, out_classes - 1, len(labels), ", ".join(labels)))
            while len(self.labels) < out_classes - 1:
                self.labels.append("class-%d" % (len(self.labels) + 1))

    def predict(self, rgb: np.ndarray) -> np.ndarray:
        """rgb is HxWx3 uint8 at the model's input size. Returns grid x grid x classes."""
        if self.inp["dtype"] == np.int8 or self.inp["dtype"] == np.uint8:
            scale, zero = self.inp["quantization"]
            # Edge Impulse's image block feeds pixels as 0..1, so the quantisation is
            # applied to that range, not to 0..255.
            x = (rgb.astype(np.float32) / 255.0) / scale + zero
            x = np.clip(np.round(x), -128 if self.inp["dtype"] == np.int8 else 0,
                        127 if self.inp["dtype"] == np.int8 else 255)
            x = x.astype(self.inp["dtype"])
        else:
            x = (rgb.astype(np.float32) / 255.0)

        self.interp.set_tensor(self.inp["index"], x[np.newaxis, ...])
        self.interp.invoke()
        y = self.interp.get_tensor(self.out["index"])[0]

        if self.out["dtype"] in (np.int8, np.uint8):
            scale, zero = self.out["quantization"]
            y = (y.astype(np.float32) - zero) * scale
        return y


# --------------------------------------------------------------------------- detections


def cells_to_blobs(grid: np.ndarray, threshold: float) -> list[dict]:
    """Group confident, touching cells of the same class into one detection each.

    FOMO fires on every cell the rock covers, so a near rock lights up a patch. Reporting
    those as separate detections would announce the same rock several times; joining them
    and taking the confidence-weighted centre gives one point per rock, which is what the
    console is going to act on.
    """
    best_class = grid.argmax(axis=-1)
    best_score = grid.max(axis=-1)
    hits = (best_class != BACKGROUND_CHANNEL) & (best_score >= threshold)
    if not hits.any():
        return []

    rows, cols = grid.shape[0], grid.shape[1]
    seen = np.zeros((rows, cols), bool)
    blobs = []

    for r in range(rows):
        for c in range(cols):
            if not hits[r, c] or seen[r, c]:
                continue
            klass = best_class[r, c]
            stack = [(r, c)]
            seen[r, c] = True
            members = []
            while stack:
                rr, cc = stack.pop()
                members.append((rr, cc, float(best_score[rr, cc])))
                for dr in (-1, 0, 1):
                    for dc in (-1, 0, 1):
                        nr, nc = rr + dr, cc + dc
                        if 0 <= nr < rows and 0 <= nc < cols and not seen[nr, nc] \
                                and hits[nr, nc] and best_class[nr, nc] == klass:
                            seen[nr, nc] = True
                            stack.append((nr, nc))

            weight = sum(m[2] for m in members)
            cy = sum((m[0] + 0.5) * m[2] for m in members) / weight
            cx = sum((m[1] + 0.5) * m[2] for m in members) / weight
            blobs.append({
                "class": int(klass),
                "value": max(m[2] for m in members),
                "cx_cells": cx, "cy_cells": cy,
                "cells": len(members),
                "r0": min(m[0] for m in members), "r1": max(m[0] for m in members),
                "c0": min(m[1] for m in members), "c1": max(m[1] for m in members),
            })
    return blobs


def to_frame_coords(blob: dict, grid_rows: int, grid_cols: int,
                    crop_x: int, crop_y: int, crop_side: int) -> dict:
    """Grid cells -> pixels in the original frame, undoing the centre crop."""
    px = crop_side / grid_cols
    py = crop_side / grid_rows
    return {
        **blob,
        "cx": int(crop_x + blob["cx_cells"] * px),
        "cy": int(crop_y + blob["cy_cells"] * py),
        "x0": int(crop_x + blob["c0"] * px),
        "y0": int(crop_y + blob["r0"] * py),
        "x1": int(crop_x + (blob["c1"] + 1) * px),
        "y1": int(crop_y + (blob["r1"] + 1) * py),
    }


# --------------------------------------------------------------------------- frames


def centre_crop(frame: np.ndarray, size: int) -> tuple[np.ndarray, int, int, int]:
    """The same "fit shortest axis" the training images went through."""
    h, w = frame.shape[:2]
    side = min(h, w)
    x0 = (w - side) // 2
    y0 = (h - side) // 2
    square = frame[y0:y0 + side, x0:x0 + side]
    return cv2.resize(square, (size, size), interpolation=cv2.INTER_AREA), x0, y0, side


class StreamSource:
    """Frames from the board's MJPEG endpoint, with /snap as the fallback."""

    def __init__(self, base_url: str, stream_port: int = 81, timeout: float = 5.0):
        import requests

        self.requests = requests
        self.base = base_url.rstrip("/")
        host = self.base.split("//", 1)[-1].split("/")[0].split(":")[0]
        self.stream_url = "http://%s:%d/stream" % (host, stream_port)
        self.snap_url = self.base + "/snap"
        self.timeout = timeout
        self.resp = None
        self.buf = b""
        self._open_stream()

    def _open_stream(self) -> None:
        try:
            self.resp = self.requests.get(self.stream_url, stream=True,
                                          timeout=self.timeout)
            self.resp.raise_for_status()
            print("stream: %s" % self.stream_url)
        except Exception as e:  # noqa: BLE001 - any failure falls back the same way
            print("stream unavailable (%s); falling back to %s" % (e, self.snap_url))
            self.resp = None

    def read(self):
        if self.resp is None:
            return self._read_snap()
        # MJPEG is just JPEGs separated by boundaries; scanning for the SOI/EOI markers is
        # more forgiving than parsing the multipart headers, and the board is the only
        # thing on the other end.
        try:
            for chunk in self.resp.iter_content(chunk_size=4096):
                self.buf += chunk
                start = self.buf.find(b"\xff\xd8")
                end = self.buf.find(b"\xff\xd9", start + 2)
                if start != -1 and end != -1:
                    jpg = self.buf[start:end + 2]
                    self.buf = self.buf[end + 2:]
                    img = cv2.imdecode(np.frombuffer(jpg, np.uint8), cv2.IMREAD_COLOR)
                    if img is not None:
                        return img
                if len(self.buf) > 4 * 1024 * 1024:
                    self.buf = b""  # desynchronised; start over rather than grow forever
        except Exception as e:  # noqa: BLE001
            print("stream dropped (%s); switching to /snap" % e)
            self.resp = None
        return self._read_snap()

    def _read_snap(self):
        try:
            r = self.requests.get(self.snap_url, timeout=self.timeout)
            r.raise_for_status()
            return cv2.imdecode(np.frombuffer(r.content, np.uint8), cv2.IMREAD_COLOR)
        except Exception:  # noqa: BLE001
            return None

    def close(self):
        if self.resp is not None:
            self.resp.close()


class FolderSource:
    """Stills from disk, so the pipeline can be checked with no board on the network."""

    def __init__(self, path: Path, loop: bool = True):
        self.files = sorted(f for f in Path(path).rglob("*")
                            if f.suffix.lower() in IMAGE_SUFFIXES)
        if not self.files:
            sys.exit("no images under %s" % path)
        print("replaying %d frame(s) from %s" % (len(self.files), path))
        self.i = 0
        self.loop = loop

    def read(self):
        if self.i >= len(self.files):
            if not self.loop:
                return None
            self.i = 0
        img = cv2.imread(str(self.files[self.i]))
        self.i += 1
        return img

    def close(self):
        pass


# --------------------------------------------------------------------------- console


class ConsoleReporter:
    """Tells the operator console about detected elements, off the video thread.

    The console de-duplicates by name, so this only has to keep the chatter down: one
    rock in view is a detection on every frame, and there is no reason to send thirty
    identical posts a second. Each element is sent again after `every` seconds, which
    also covers a console that was started after the detector.
    """

    def __init__(self, base_url: str, every: float = 2.0, timeout: float = 0.5):
        self.url = base_url.rstrip("/") + "/detection"
        self.every = every
        self.timeout = timeout
        self.last_sent: dict[str, float] = {}
        self.q: queue.Queue = queue.Queue(maxsize=16)
        self.warned = False
        threading.Thread(target=self._run, daemon=True).start()
        print("console: reporting to %s" % self.url)

    def report(self, element: str, label: str, confidence: float) -> None:
        now = time.monotonic()
        if now - self.last_sent.get(element, -1e9) < self.every:
            return
        self.last_sent[element] = now
        try:
            self.q.put_nowait({"element": element, "label": label,
                               "confidence": round(confidence, 3)})
        except queue.Full:
            pass  # the console is not keeping up; dropping beats stalling the video

    def _run(self) -> None:
        while True:
            body = self.q.get()
            req = urllib.request.Request(self.url, data=json.dumps(body).encode(),
                                         headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=self.timeout) as r:
                    reply = json.loads(r.read() or b"{}")
                if reply.get("added"):
                    print("console: added %s" % body["element"])
                self.warned = False
            except Exception as e:  # noqa: BLE001 - a missing console is not fatal
                if not self.warned:
                    print("console unreachable (%s); still detecting" % e)
                    self.warned = True


# --------------------------------------------------------------------------- labels


def load_labels(explicit: str | None) -> tuple[list[str], dict]:
    """Class names in the model's output order, and the label -> element mapping.

    The element a rock stands for lives in the manifest, never in the model, for the same
    reason console/data/compositions.json is console-side: the arena can be re-dressed
    without retraining anything.
    """
    if explicit:
        labels = [s.strip() for s in explicit.split(",") if s.strip()]
    else:
        labels = []
        train = HERE / "dataset" / "labelled" / "train"
        if train.is_dir():
            labels = sorted(d.name for d in train.iterdir()
                            if d.is_dir() and not d.name.startswith("_")
                            and d.name != "upload")
        if not labels:
            sys.exit("could not work out the class names -- pass --labels a,b")

    elements = {}
    if MANIFEST.exists():
        m = json.loads(MANIFEST.read_text(encoding="utf-8"))
        elements = {k: v.get("element", "") for k, v in m.get("labels", {}).items()}
    return labels, elements


def find_model(explicit: str | None) -> Path:
    if explicit:
        p = Path(explicit)
        if not p.exists():
            sys.exit("no such model: %s" % p)
        return p
    found = sorted(MODEL_DIR.glob("*.tflite")) if MODEL_DIR.is_dir() else []
    if not found:
        sys.exit("no .tflite in %s\n"
                 "  Edge Impulse -> Deployment -> TensorFlow Lite -> Build,\n"
                 "  then put the .tflite from the zip there." % MODEL_DIR)
    if len(found) > 1:
        print("several models in %s; using %s" % (MODEL_DIR, found[-1].name))
    return found[-1]


# --------------------------------------------------------------------------- drawing


def draw(frame, dets, labels, elements, fps, infer_ms, threshold):
    for d in dets:
        name = labels[d["class"] - 1] if 0 < d["class"] <= len(labels) else "?"
        element = elements.get(name, "")
        colour = (0, 255, 0)

        cv2.rectangle(frame, (d["x0"], d["y0"]), (d["x1"], d["y1"]), colour, 2)
        cv2.drawMarker(frame, (d["cx"], d["cy"]), colour, cv2.MARKER_CROSS, 22, 2)
        caption = "%s %.0f%%" % (element.upper() if element else name.upper(),
                                 d["value"] * 100)
        y = max(d["y0"] - 8, 18)
        cv2.putText(frame, caption, (d["x0"], y), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                    (0, 0, 0), 4, cv2.LINE_AA)
        cv2.putText(frame, caption, (d["x0"], y), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                    colour, 2, cv2.LINE_AA)

    if dets:
        names = sorted({labels[d["class"] - 1] for d in dets
                        if 0 < d["class"] <= len(labels)})
        banner = ", ".join((elements.get(n) or n).upper() for n in names) + " DETECTED"
        colour = (0, 255, 0)
    else:
        banner = "no rock in view"
        colour = (170, 170, 170)
    cv2.putText(frame, banner, (12, 34), cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                (0, 0, 0), 5, cv2.LINE_AA)
    cv2.putText(frame, banner, (12, 34), cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                colour, 2, cv2.LINE_AA)

    h = frame.shape[0]
    cv2.putText(frame, "%.1f fps | %.1f ms inference | min confidence %.0f%%"
                % (fps, infer_ms, threshold * 100), (12, h - 14),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 4, cv2.LINE_AA)
    cv2.putText(frame, "%.1f fps | %.1f ms inference | min confidence %.0f%%"
                % (fps, infer_ms, threshold * 100), (12, h - 14),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    return frame


# --------------------------------------------------------------------------- main


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--url", help="board base URL, e.g. http://192.168.1.39")
    p.add_argument("--stream-port", type=int, default=81)
    p.add_argument("--source", help="a folder of stills instead of the board")
    p.add_argument("--model", help="path to the .tflite (default: newest in vision/model)")
    p.add_argument("--labels", help="comma-separated class names in the model's order "
                                    "(default: the training folder names)")
    p.add_argument("--threshold", type=float, default=0.5,
                   help="confidence a cell needs to count (default 0.5)")
    p.add_argument("--min-confidence", type=float, default=0.7,
                   help="confidence a detection needs to be shown or reported "
                        "(default 0.7)")
    p.add_argument("--console", default=DEFAULT_CONSOLE,
                   help="operator console detection API (default %s)" % DEFAULT_CONSOLE)
    p.add_argument("--no-console", action="store_true",
                   help="detect and display only; tell the console nothing")
    p.add_argument("--report-every", type=float, default=2.0,
                   help="seconds before the same element is re-sent (default 2)")
    p.add_argument("--bench", type=int, metavar="N",
                   help="time N frames with no window, print the numbers, exit")
    args = p.parse_args(argv)

    if not args.url and not args.source:
        p.error("one of --url or --source is required")

    labels, elements = load_labels(args.labels)
    model_path = find_model(args.model)
    model = Fomo(model_path, labels)
    print("model  : %s, %dx%d input, classes: %s"
          % (model_path.name, model.width, model.height, ", ".join(model.labels)))
    for name in model.labels:
        print("         %-14s -> %s" % (name, elements.get(name) or "(no element set)"))

    source = FolderSource(Path(args.source)) if args.source \
        else StreamSource(args.url, args.stream_port)
    reporter = None if args.no_console or args.bench \
        else ConsoleReporter(args.console, args.report_every)

    recent = deque(maxlen=30)
    infer_recent = deque(maxlen=30)
    frames = 0
    paused = False
    last = None
    t_start = time.perf_counter()

    try:
        while True:
            if not paused or last is None:
                frame = source.read()
                if frame is None:
                    print("no frame; is the board reachable?")
                    time.sleep(0.5)
                    continue
                last = frame
            frame = last.copy()

            square, cx0, cy0, side = centre_crop(frame, model.width)
            rgb = cv2.cvtColor(square, cv2.COLOR_BGR2RGB)

            t0 = time.perf_counter()
            grid = model.predict(rgb)
            infer_ms = (time.perf_counter() - t0) * 1000.0
            infer_recent.append(infer_ms)

            blobs = cells_to_blobs(grid, args.threshold)
            dets = [to_frame_coords(b, grid.shape[0], grid.shape[1], cx0, cy0, side)
                    for b in blobs if b["value"] >= args.min_confidence]

            if reporter is not None:
                for d in dets:
                    if 0 < d["class"] <= len(model.labels):
                        name = model.labels[d["class"] - 1]
                        reporter.report(elements.get(name) or name, name, d["value"])

            recent.append(time.perf_counter())
            fps = (len(recent) - 1) / (recent[-1] - recent[0]) if len(recent) > 1 else 0.0
            frames += 1

            if args.bench:
                if frames >= args.bench:
                    total = time.perf_counter() - t_start
                    print("\n%d frames in %.1f s" % (frames, total))
                    print("  end to end : %.1f fps" % (frames / total))
                    print("  inference  : %.1f ms mean, %.1f ms worst"
                          % (float(np.mean(infer_recent)), float(np.max(infer_recent))))
                    return 0
                continue

            draw(frame, dets, model.labels, elements, fps,
                 float(np.mean(infer_recent)), args.min_confidence)
            cv2.imshow(WINDOW, frame)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord(" "):
                paused = not paused
            if key == ord("s"):
                SHOTS.mkdir(parents=True, exist_ok=True)
                out = SHOTS / time.strftime("detect_%Y%m%d-%H%M%S.jpg")
                cv2.imwrite(str(out), frame)
                print("saved %s" % out)
    except KeyboardInterrupt:
        pass
    finally:
        source.close()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Capture and label rock images for the detection POC — IMPLEMENTATION_PLAN step 2.0c.

Two ways to shoot, and the choice matters:

  --url http://<board-ip>   pull frames from the XIAO ESP32S3 Sense (step 2.0b.2 firmware)
  (default)                 the PC webcam

**Shoot the real corpus through the board.** A model trained on webcam frames and deployed
to the OV2640 faces a domain gap — different sensor, lens, colour response, field of view —
and that gap costs accuracy for no visible reason. The webcam path stays because it is
useful for shaking the pipeline out before the board is aimed.

Two output shapes, for the two things a dataset gets used for:

  --raw            full frames, one flat folder, no class label. This is what an
                   annotation tool wants: you draw the boxes afterwards, so the image
                   must keep its whole scene. Lands in vision/dataset/_raw/<session>/.
  --label <name>   square 320px crops sorted into per-class folders, which is what an
                   image *classifier* trains on directly and what Edge Impulse ingests.

`label` is the rock's identity as the detector sees it ("green-sharp"); the *element* it
stands for ("Helium") lives in the manifest, not in the folder name. That split mirrors
console/data/compositions.json: the rover reports an identifier and something console-side
turns it into a displayed element, so an arena can be re-dressed without retraining.

LIGHTING. Step 2.0's decision record names lighting as what replaced risk R2, and the
failure it describes is a detector that learned the room rather than the rock. So this
tool locks exposure and white balance before shooting, and reports what the camera
actually accepted rather than what it was asked for. On the board that lock is real (the
firmware's /lock freezes AGC, AEC and AWB); on a webcam the driver often refuses, and you
are told when it does. Aim a neutral grey or white card at the lens under the light you
will shoot in, then lock, then shoot.

Usage:
    python vision/capture.py --url http://192.168.1.39 --raw --lock
    python vision/capture.py --url http://192.168.1.39 --label green-sharp --element Helium
    python vision/capture.py --label green-sharp --element Helium      # webcam
    python vision/capture.py --label red-round --element Iron --import ./phone-photos
    python vision/capture.py --status
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
DATASET = HERE / "dataset"
MANIFEST = DATASET / "manifest.json"

# Saved square, at a size that outlives the choice of input resolution. FOMO and the small
# classifiers typically train at 96 or 160 px; saving at 320 means changing that later is a
# retrain, not a recapture. Raw frames are NOT cropped to this — see --raw.
SAVE_SIZE = 320

MIN_PER_CLASS = 50
GOOD_PER_CLASS = 150

# Burst mode saves only frames differing from the last saved one by at least this mean
# absolute difference (0-255 scale). Holding a rock still under a 30 fps camera otherwise
# yields 200 near-identical images, which inflates the count without teaching anything.
BURST_NOVELTY = 6.0

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


# --------------------------------------------------------------------------- manifest


def load_manifest() -> dict:
    if MANIFEST.exists():
        return json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {
        "_comment": (
            "Label -> element mapping and capture provenance for the step 2.0c POC. "
            "The element names here are the POC's stand-in for "
            "console/data/compositions.json; they are presets, not measurements."
        ),
        "labels": {},
        "sessions": [],
    }


def save_manifest(manifest: dict) -> None:
    DATASET.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def class_counts() -> dict:
    """Per-class image counts.

    Folders whose name starts with '_' are not classes: '_raw' holds unlabelled frames
    awaiting annotation, and '_bringup' holds whatever tools/cam_grab.py decoded off the
    serial line. Counting either as a class would quietly corrupt the readiness report.
    """
    if not DATASET.exists():
        return {}
    return {
        d.name: sum(1 for f in d.iterdir() if f.suffix.lower() in IMAGE_SUFFIXES)
        for d in sorted(DATASET.iterdir())
        if d.is_dir() and not d.name.startswith("_")
    }


# ---------------------------------------------------------------------- frame sources


class WebcamSource:
    """The PC webcam, via OpenCV."""

    kind = "webcam"

    def __init__(self, index: int, width: int, height: int):
        # CAP_DSHOW on Windows: the default MSMF backend refuses most property writes, so
        # the exposure lock below silently does nothing under it.
        backend = cv2.CAP_DSHOW if sys.platform == "win32" else cv2.CAP_ANY
        self.cap = cv2.VideoCapture(index, backend)
        if not self.cap.isOpened():
            raise SystemExit(
                "Could not open camera %d. Close anything else using it (Teams, the "
                "Camera app), or try --camera 1." % index)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.description = "webcam %d" % index
        self.report = {}
        self.ok = False
        self.msg = "not locked yet"

    def lock(self):
        """Attempt to pin exposure and white balance; report what actually took.

        Property semantics differ per backend and per driver, and a write that is ignored
        still returns success. So every value is written and then read back, and the
        caller is told the truth rather than the intention.
        """
        requested = {
            # 0.25 means "manual" on V4L2 and on most DSHOW drivers; 0.75 is "auto".
            "CAP_PROP_AUTO_EXPOSURE": (cv2.CAP_PROP_AUTO_EXPOSURE, 0.25),
            "CAP_PROP_AUTO_WB": (cv2.CAP_PROP_AUTO_WB, 0.0),
            "CAP_PROP_AUTOFOCUS": (cv2.CAP_PROP_AUTOFOCUS, 0.0),
        }
        report = {}
        for name, (prop, value) in requested.items():
            self.cap.set(prop, value)
            report[name] = {"requested": value, "readback": self.cap.get(prop)}
        for name, prop in (
            ("CAP_PROP_EXPOSURE", cv2.CAP_PROP_EXPOSURE),
            ("CAP_PROP_WB_TEMPERATURE", cv2.CAP_PROP_WB_TEMPERATURE),
            ("CAP_PROP_GAIN", cv2.CAP_PROP_GAIN),
        ):
            report[name] = {"readback": self.cap.get(prop)}

        # DSHOW returns -1 for a property it does not support.
        problems = [n.replace("CAP_PROP_", "").lower()
                    for n in ("CAP_PROP_AUTO_EXPOSURE", "CAP_PROP_AUTO_WB")
                    if (report[n]["readback"] or -1) < 0]
        self.report = report
        self.ok = not problems
        self.msg = ("driver refused: " + ", ".join(problems)) if problems \
            else "exposure and white balance locked"
        return self.ok, self.msg

    def read(self):
        ok, frame = self.cap.read()
        return frame if ok else None

    def release(self):
        self.cap.release()


class BoardSource:
    """The XIAO ESP32S3 Sense running the step 2.0b.2 firmware.

    Frames come off /stream as MJPEG rather than by polling /snap: the board sends them
    back to back, so the live view is smooth enough to aim and focus with, and a burst
    capture is limited by the sensor rather than by a request round-trip.
    """

    kind = "board"

    # Control is on :80 and the stream on :81, because an MJPEG handler never returns and
    # would otherwise starve every control endpoint. The firmware header explains it.
    STREAM_PORT = 81

    def __init__(self, base_url: str, timeout: float = 10.0):
        self.base = base_url.rstrip("/")
        parts = urllib.parse.urlsplit(self.base)
        self.stream_url = urllib.parse.urlunsplit(
            (parts.scheme or "http", "%s:%d" % (parts.hostname, self.STREAM_PORT),
             "/stream", "", ""))
        self.timeout = timeout
        self.description = "%s (stream :%d)" % (self.base, self.STREAM_PORT)
        self.report = {}
        self.ok = False
        self.msg = "not locked yet"
        self.stream = None
        self._open_stream()

    def _open_stream(self):
        try:
            self.stream = urllib.request.urlopen(self.stream_url, timeout=self.timeout)
        except Exception as exc:  # noqa: BLE001
            raise SystemExit(
                "Could not open %s: %s\n"
                "Check the board is powered and on the same network, and that the IP is "
                "the one it printed on serial. Try %s/status in a browser first."
                % (self.stream_url, exc, self.base))

    def lock(self):
        """Ask the firmware to freeze AGC, AEC and AWB where they currently sit."""
        try:
            urllib.request.urlopen(self.base + "/lock", timeout=self.timeout).read()
            status = json.loads(
                urllib.request.urlopen(self.base + "/status", timeout=self.timeout).read())
        except Exception as exc:  # noqa: BLE001
            self.ok, self.msg = False, "lock request failed: %s" % exc
            return self.ok, self.msg

        self.report = status
        # Trust the board's own read-back, not the fact that the request returned 200.
        self.ok = (status.get("awb") == 0 and status.get("aec") == 0
                   and status.get("agc") == 0)
        self.msg = ("sensor locked (AGC/AEC/AWB off)" if self.ok
                    else "board reports still auto: %s" % status)
        return self.ok, self.msg

    def read(self):
        """Read one JPEG out of the multipart stream.

        The firmware always sends Content-Length, so this parses headers rather than
        scanning for JPEG markers — cheaper, and it cannot resynchronise onto a false
        0xFFD8 inside compressed data.
        """
        try:
            length = None
            while True:
                line = self.stream.readline()
                if not line:
                    return None
                line = line.strip()
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":", 1)[1])
                elif line == b"" and length is not None:
                    break
            payload = self.stream.read(length)
        except Exception:  # noqa: BLE001 - a dropped stream should not kill the session
            return None

        frame = cv2.imdecode(np.frombuffer(payload, np.uint8), cv2.IMREAD_COLOR)
        return frame

    def release(self):
        if self.stream is not None:
            try:
                self.stream.close()
            except Exception:  # noqa: BLE001
                pass


# ------------------------------------------------------------------------------ frame


def square_crop(frame, size: int = SAVE_SIZE):
    h, w = frame.shape[:2]
    side = min(h, w)
    y0 = (h - side) // 2
    x0 = (w - side) // 2
    return cv2.resize(frame[y0:y0 + side, x0:x0 + side], (size, size),
                      interpolation=cv2.INTER_AREA)


def guide_box(frame):
    """Pixel rect of the region square_crop keeps, for the on-screen overlay."""
    h, w = frame.shape[:2]
    side = min(h, w)
    return (w - side) // 2, (h - side) // 2, side, side


def next_index(label_dir: Path) -> int:
    return sum(1 for f in label_dir.glob("*") if f.suffix.lower() in IMAGE_SUFFIXES)


def novelty(previous, current) -> float:
    if previous is None:
        return 255.0
    return float(np.mean(cv2.absdiff(previous, current)))


# ---------------------------------------------------------------------------- overlay


def draw_overlay(view, *, title, saved, total, session, lock_msg, lock_ok, burst, flash,
                 raw, source_desc):
    h, w = view.shape[:2]

    # In raw mode the whole frame is kept, so a crop guide would be a lie.
    if not raw:
        gx, gy, gw, gh = guide_box(view)
        cv2.rectangle(view, (gx, gy), (gx + gw, gy + gh), (0, 255, 255), 2)

    panel_h = 112
    panel = view[0:panel_h, 0:w].copy()
    cv2.rectangle(panel, (0, 0), (w, panel_h), (0, 0, 0), -1)
    cv2.addWeighted(panel, 0.65, view[0:panel_h, 0:w], 0.35, 0, view[0:panel_h, 0:w])

    def put(text, y, colour=(255, 255, 255), scale=0.6):
        cv2.putText(view, text, (12, y), cv2.FONT_HERSHEY_SIMPLEX, scale, colour, 1,
                    cv2.LINE_AA)

    put(title, 26, (0, 255, 255), 0.75)
    put("this session %d   total %d   %s   %dx%d"
        % (saved, total, "BURST" if burst else "single", w, h), 50)
    put(lock_msg, 70, (120, 255, 120) if lock_ok else (120, 160, 255), 0.5)
    put("source: %s" % source_desc, 88, (200, 200, 200), 0.45)
    put("SPACE save   B burst   L lock   Q quit   [%s]" % session, 106,
        (200, 200, 200), 0.45)

    if flash > 0:
        cv2.rectangle(view, (0, 0), (w - 1, h - 1), (255, 255, 255), 8)

    if not raw:
        bar_w = int(w * min(1.0, total / float(GOOD_PER_CLASS)))
        if total >= GOOD_PER_CLASS:
            colour = (120, 255, 120)
        elif total >= MIN_PER_CLASS:
            colour = (0, 200, 255)
        else:
            colour = (80, 80, 255)
        cv2.rectangle(view, (0, h - 6), (bar_w, h), colour, -1)


# ------------------------------------------------------------------------- subcommands


def record_session(label, element, session, saved, note, *, lock_ok, lock_report,
                   source_kind, source_desc, raw):
    manifest = load_manifest()
    if label and not raw:
        entry = manifest["labels"].setdefault(label, {})
        if element:
            entry["element"] = element
        entry.setdefault("element", "UNSET")
    manifest["sessions"].append({
        "started": session,
        "label": label if not raw else "(raw, unlabelled)",
        "saved": saved,
        "note": note,
        "lock_ok": lock_ok,
        # Which camera shot it. This is the provenance that makes a later accuracy
        # difference explainable rather than mysterious.
        "source": {"kind": source_kind, "detail": source_desc},
        "camera": lock_report,
    })
    save_manifest(manifest)


def do_status() -> int:
    manifest = load_manifest()
    counts = class_counts()

    raw_dir = DATASET / "_raw"
    raw_n = sum(1 for f in raw_dir.rglob("*") if f.suffix.lower() in IMAGE_SUFFIXES) \
        if raw_dir.exists() else 0

    if not counts and not raw_n:
        print("No dataset yet. Start one with:\n"
              "  python vision/capture.py --url http://<board-ip> --raw --lock")
        return 0

    if counts:
        print("%-20s %-16s %7s  readiness" % ("label", "element", "images"))
        print("-" * 62)
        for label, n in counts.items():
            element = manifest["labels"].get(label, {}).get("element", "?")
            if n >= GOOD_PER_CLASS:
                verdict = "good"
            elif n >= MIN_PER_CLASS:
                verdict = "trainable, thin (want %d)" % GOOD_PER_CLASS
            else:
                verdict = "too few (want %d+)" % MIN_PER_CLASS
            print("%-20s %-16s %7d  %s" % (label, element, n, verdict))
        print("-" * 62)
        print("%-20s %-16s %7d  across %d classes"
              % ("", "", sum(counts.values()), len(counts)))

    if raw_n:
        print("\n%d raw frame(s) in %s awaiting annotation" % (raw_n, raw_dir))

    if counts and len(counts) < 2:
        print("\nA classifier needs at least two classes. Capture a second rock -- and "
              "consider a 'background' class of arena with no rock in shot, which is "
              "what stops the detector naming a rock in every frame.")
    if manifest["sessions"]:
        print("\nsessions:")
        for s in manifest["sessions"][-8:]:
            src = s.get("source", {}).get("kind", "?")
            print("  %s  %-18s %4d imgs  lock=%-3s  %-7s %s"
                  % (s["started"], s["label"], s["saved"],
                     "yes" if s["lock_ok"] else "NO", src, s["note"]))
    return 0


def do_import(label, element, source: Path, note) -> int:
    if not source.exists():
        raise SystemExit("--import path does not exist: %s" % source)
    files = sorted(f for f in source.rglob("*") if f.suffix.lower() in IMAGE_SUFFIXES)
    if not files:
        raise SystemExit("No images under %s" % source)

    label_dir = DATASET / label
    label_dir.mkdir(parents=True, exist_ok=True)
    session = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    start = next_index(label_dir)

    saved = 0
    for f in files:
        img = cv2.imread(str(f))
        if img is None:
            print("  skipped (unreadable): %s" % f.name)
            continue
        out = label_dir / ("%s_%s_%04d.jpg" % (label, session, start + saved))
        cv2.imwrite(str(out), square_crop(img), [cv2.IMWRITE_JPEG_QUALITY, 92])
        saved += 1

    record_session(label, element, session, saved, note or ("imported from %s" % source),
                   lock_ok=False, lock_report={}, source_kind="import",
                   source_desc=str(source), raw=False)
    print("Imported %d image(s) into %s" % (saved, label_dir))
    print("Note: imported photos carry whatever exposure and white balance the phone "
          "chose. Mixing them with locked captures teaches the model that colour is "
          "unreliable -- keep them as a separate evaluation set if you can.")
    return 0


def do_capture(args) -> int:
    raw = args.raw
    manifest = load_manifest()
    label = args.label or ""
    element = ""

    if not raw:
        element = args.element or manifest["labels"].get(label, {}).get("element", "")
        if not element:
            raise SystemExit(
                "Label '%s' has no element yet. Pass it once:\n"
                "  python vision/capture.py --label %s --element Helium" % (label, label))

    session = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    if raw:
        out_dir = DATASET / "_raw" / session
        title = "RAW capture (full frames, annotate later)"
        stem = "raw"
    else:
        out_dir = DATASET / label
        title = "%s  ->  %s" % (label, element)
        stem = label
    out_dir.mkdir(parents=True, exist_ok=True)
    index = next_index(out_dir)

    if args.url:
        source = BoardSource(args.url)
    else:
        source = WebcamSource(args.camera, args.width, args.height)

    lock_ok, lock_msg = (False, "not locked - press L")
    if args.lock:
        lock_ok, lock_msg = source.lock()
        if not lock_ok:
            print("WARNING: %s\nFrames will drift as the light changes, which is exactly "
                  "the failure step 2.0 recorded as the new R2. Fine for a pipeline "
                  "shakedown; do not trust it for the 2.3 accuracy measurement."
                  % lock_msg)

    if args.shots:
        print("Capturing %d frame(s) from %s, headless." % (args.shots, source.description))
    else:
        print("Capturing from %s. SPACE saves, B toggles burst, L locks, Q quits."
              % source.description)
    if raw:
        print("Raw mode: whole frames, no crop, into %s" % out_dir)
    else:
        print("Turn the rock between shots -- a model trained on one pose recognises "
              "one pose.")

    saved = 0
    burst = False
    flash = 0
    last_saved = None
    window = "capture: %s" % (stem)

    # Headless bulk capture. Bulk shooting is not an interactive job -- it is "point the
    # board at the scene, take N frames a second apart, come back later" -- and a GUI
    # window that must stay focused makes that harder, not easier. The novelty gate still
    # applies, so N frames of a motionless scene will not quietly become N copies.
    if args.shots:
        try:
            skipped = 0
            while saved < args.shots:
                frame = source.read()
                if frame is None:
                    print("No frame from source; stopping.")
                    break
                keep = frame if raw else square_crop(frame)
                if novelty(last_saved, keep) < BURST_NOVELTY:
                    skipped += 1
                    if skipped % 50 == 0:
                        print("  ... %d frames too similar to the last saved one; move "
                              "the rock or the camera" % skipped)
                    continue
                out = out_dir / ("%s_%s_%04d.jpg" % (stem, session, index + saved))
                cv2.imwrite(str(out), keep, [cv2.IMWRITE_JPEG_QUALITY, 92])
                last_saved = keep
                saved += 1
                print("  [%d/%d] %s" % (saved, args.shots, out.name))
                if args.interval > 0:
                    time.sleep(args.interval)
        finally:
            source.release()
        if saved:
            record_session(label, element, session, saved, args.session or "",
                           lock_ok=lock_ok, lock_report=source.report,
                           source_kind=source.kind, source_desc=source.description,
                           raw=raw)
        print("Saved %d image(s) to %s" % (saved, out_dir))
        return 0

    try:
        while True:
            frame = source.read()
            if frame is None:
                print("No frame from source; stopping.")
                break

            keep = frame if raw else square_crop(frame)
            take = burst and novelty(last_saved, keep) >= BURST_NOVELTY

            view = frame.copy()
            draw_overlay(view, title=title, saved=saved, total=index + saved,
                         session=args.session or "unnoted", lock_msg=lock_msg,
                         lock_ok=lock_ok, burst=burst, flash=flash, raw=raw,
                         source_desc=source.description)
            cv2.imshow(window, view)
            flash = max(0, flash - 1)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):
                break
            if key == ord(" "):
                take = True
            elif key == ord("b"):
                burst = not burst
                last_saved = None
            elif key == ord("l"):
                lock_ok, lock_msg = source.lock()

            if take:
                out = out_dir / ("%s_%s_%04d.jpg" % (stem, session, index + saved))
                cv2.imwrite(str(out), keep, [cv2.IMWRITE_JPEG_QUALITY, 92])
                last_saved = keep
                saved += 1
                flash = 3
    finally:
        source.release()
        cv2.destroyAllWindows()

    if saved:
        record_session(label, element, session, saved, args.session or "",
                       lock_ok=lock_ok, lock_report=source.report,
                       source_kind=source.kind, source_desc=source.description, raw=raw)
    print("Saved %d image(s) to %s" % (saved, out_dir))
    if raw and saved:
        print("Next: annotate them, then train. They are full frames on purpose -- an "
              "annotation tool needs the whole scene, not a centre crop.")
    return 0


# ------------------------------------------------------------------------------- main


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Capture and label rock images for the step 2.0c detection POC.")
    p.add_argument("--url",
                   help="board base URL, e.g. http://192.168.1.39 (step 2.0b.2 "
                        "firmware). Omit to use the PC webcam.")
    p.add_argument("--raw", action="store_true",
                   help="save whole frames, unlabelled, for an annotation tool")
    p.add_argument("--lock", action="store_true",
                   help="lock exposure/white balance before capturing")
    p.add_argument("--label", help="folder-safe rock identity, e.g. green-sharp")
    p.add_argument("--element", default="",
                   help="element this rock stands for, e.g. Helium. Stored in the "
                        "manifest; needed once per label.")
    p.add_argument("--session", default="",
                   help="lighting note for this sitting, e.g. 'blinds closed, lamp only'")
    p.add_argument("--import", dest="import_dir", metavar="DIR",
                   help="ingest existing photos from a folder instead of capturing")
    p.add_argument("--status", action="store_true",
                   help="print per-class counts and stop")
    p.add_argument("--shots", type=int, default=0,
                   help="headless: capture this many frames and exit, no window")
    p.add_argument("--interval", type=float, default=0.5,
                   help="seconds between headless shots (default 0.5)")
    p.add_argument("--camera", type=int, default=0, help="webcam index (default 0)")
    p.add_argument("--width", type=int, default=1280)
    p.add_argument("--height", type=int, default=720)
    args = p.parse_args(argv)

    if args.status:
        return do_status()
    if args.import_dir:
        if not args.label:
            p.error("--import needs --label")
        manifest = load_manifest()
        element = args.element or manifest["labels"].get(args.label, {}).get("element", "")
        if not element:
            p.error("label '%s' has no element yet; pass --element once" % args.label)
        return do_import(args.label, element, Path(args.import_dir).expanduser(),
                         args.session)
    if not args.raw and not args.label:
        p.error("give either --raw (unlabelled frames for annotation) or "
                "--label <name> (sorted into a class folder)")
    return do_capture(args)


if __name__ == "__main__":
    raise SystemExit(main())

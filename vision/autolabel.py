#!/usr/bin/env python3
"""Draw the boxes for you — IMPLEMENTATION_PLAN step 2.0d.

Hand-annotating 300 frames is the step that stalls a vision POC, so this does it
automatically and asks you to *check* rather than to draw.

It works because of how the corpus was shot (step 2.0c): one rock per session, on the
arena floor, through the board, with the sensor locked. That gives two things a general
annotation tool does not have —

  1. the session already says which rock is in every frame, so the class needs no clicking;
  2. a session of the *empty floor* was shot under the same lock, so the floor's colours
     are known.

So the rock is found by contrast, not by a fixed colour rule: a 2-D histogram of Lab
chroma (a, b) is built over the rock session and another over the background session, and
the log-ratio of the two says, per colour, "this is far more common when the rock is in
shot than when it is not". Back-projecting that ratio onto a frame lights the rock up and
leaves the floor dark. No hue thresholds to tune per rock, and it adapts to the board's
colour cast for free, because both histograms carry the same cast.

What it cannot do is know a yellow waste bin at the edge of the arena from a yellow rock.
Those come out **flagged**, and the review sheets draw them in red. Check the red ones.

    python vision/autolabel.py --list
    python vision/autolabel.py --bg 20260926-112022 \
        --rock 20260926-110447=green-sharp --rock 20260926-111622=red-round
    python vision/autolabel.py --bg 20260926-112022 --out test \
        --rock 20260926-112602=green-sharp

Output lands in vision/dataset/labelled/<split>/:

    <label>/*.jpg                  the frames, copied
    <label>/*.txt                  YOLO boxes: class cx cy w h, normalised
    bounding_boxes.labels          Edge Impulse ingestion, pixel top-left + size
    autolabel-report.json          every decision, including the rejects
    _review/<label>-NN.jpg         contact sheets: green box = confident, red = check me

The empty-floor session needs no boxes — upload those frames to Edge Impulse as they are.
A detector learns "no rock here" from unlabelled background images, which is what stops it
naming a rock in an empty frame.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
DATASET = HERE / "dataset"
RAW = DATASET / "_raw"
OUT_ROOT = DATASET / "labelled"
MANIFEST = DATASET / "manifest.json"

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

# Lab chroma is quantised this coarsely on purpose. Finer bins start modelling JPEG noise
# and the histogram stops generalising to a frame it has not seen.
BINS = 32

# Back-projection threshold, in log-odds. 1.5 means "about 4x more likely to belong to the
# rock than to the floor". Lower finds more of a dull rock and more of the room with it.
SCORE_MIN = 1.5

# Blob size bounds as a fraction of the frame. The lower bound throws away JPEG speckle;
# the upper one throws away a whole wall that happened to match.
AREA_MIN_FRAC = 0.0010
AREA_MAX_FRAC = 0.2500

# A box is flagged rather than trusted when the runner-up blob is this close in size --
# two plausible rocks means the wrong one may have been picked.
RIVAL_FRAC = 0.5

# Blobs whose centre sits above this fraction of the frame are not considered at all: that
# band is the room behind the arena, not the floor the rock sits on.
HIGH_IN_FRAME = 0.30

REVIEW_COLS, REVIEW_ROWS = 5, 4
REVIEW_TILE = (256, 192)


# --------------------------------------------------------------------------- sessions


def session_frames(name: str) -> list[Path]:
    d = RAW / name
    if not d.is_dir():
        sys.exit("no such session: %s\n  try: python vision/autolabel.py --list" % d)
    return sorted(f for f in d.iterdir() if f.suffix.lower() in IMAGE_SUFFIXES)


def session_notes() -> dict:
    """Session folder -> the --session note typed at capture time, from the manifest."""
    if not MANIFEST.exists():
        return {}
    m = json.loads(MANIFEST.read_text(encoding="utf-8"))
    return {s["started"]: s.get("note", "") for s in m.get("sessions", [])}


def do_list() -> int:
    notes = session_notes()
    if not RAW.is_dir():
        sys.exit("no raw sessions yet -- capture some first (vision/capture.py --raw)")
    print("%-18s %6s  %s" % ("session", "frames", "note typed at capture"))
    for d in sorted(RAW.iterdir()):
        if not d.is_dir():
            continue
        n = sum(1 for f in d.iterdir() if f.suffix.lower() in IMAGE_SUFFIXES)
        if n:
            print("%-18s %6d  %s" % (d.name, n, notes.get(d.name, "-")))
    return 0


# --------------------------------------------------------------------------- colour model


def chroma_hist(files: list[Path], stride: int = 1) -> np.ndarray:
    """Normalised 2-D histogram of Lab (a, b) over a set of frames."""
    h = np.zeros((BINS, BINS), np.float64)
    for f in files[::stride]:
        img = cv2.imread(str(f))
        if img is None:
            continue
        lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
        ab = lab[:, :, 1:3].reshape(-1, 2)
        counts, _, _ = np.histogram2d(
            ab[:, 0], ab[:, 1], bins=BINS, range=[[0, 256], [0, 256]])
        h += counts
    total = h.sum()
    if total == 0:
        sys.exit("no readable frames in that session")
    return h / total


def log_ratio(fg: np.ndarray, bg: np.ndarray) -> np.ndarray:
    # The epsilon keeps a colour that never occurs in one of the two sets from producing an
    # infinite score; it caps the evidence a single unseen bin can carry.
    return np.log((fg + 1e-6) / (bg + 1e-6))


def set_score_min(value: float) -> None:
    global SCORE_MIN
    SCORE_MIN = value


def score_map(img: np.ndarray, ratio: np.ndarray) -> np.ndarray:
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    idx = lab[:, :, 1:3].astype(np.int32) * BINS // 256
    return ratio[idx[:, :, 0], idx[:, :, 1]]


# --------------------------------------------------------------------------- box finding


def find_box(img: np.ndarray, ratio: np.ndarray) -> dict:
    """Locate the rock. Returns a verdict dict; 'box' is None when nothing qualified."""
    h, w = img.shape[:2]
    mask = (score_map(img, ratio) > SCORE_MIN).astype(np.uint8)

    # Open first to drop speckle, then close so a specular highlight across the rock's top
    # does not split it into two blobs.
    k = np.ones((5, 5), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))

    n, _, stats, centroids = cv2.connectedComponentsWithStats(mask)
    frame_area = float(h * w)
    cands = []
    for i in range(1, n):
        x, y, bw, bh, area = stats[i]
        if not (AREA_MIN_FRAC * frame_area <= area <= AREA_MAX_FRAC * frame_area):
            continue
        cy = centroids[i][1]
        # The arena floor is the lower part of the view. A match up in the room -- a yellow
        # cabinet, a window's glare -- is not a rock, and a wrong box teaches worse than a
        # missing frame does, so those are discarded here rather than ranked and flagged.
        if cy < HIGH_IN_FRAME * h:
            continue
        # Rank by size, but bias downward: of two equally sized blobs on the floor the
        # lower one is the nearer, and the better bet for a rock.
        cands.append({"box": (int(x), int(y), int(bw), int(bh)),
                      "area": int(area), "cy": float(cy),
                      "rank": area * (1.0 + cy / h)})
    if not cands:
        return {"box": None,
                "reason": "nothing on the floor matched the rock's colour"}

    cands.sort(key=lambda c: -c["rank"])
    best = cands[0]
    x, y, bw, bh = best["box"]

    flags = []
    if len(cands) > 1 and cands[1]["area"] > RIVAL_FRAC * best["area"]:
        flags.append("a second blob is nearly as large")
    if x <= 1 or y <= 1 or x + bw >= w - 1 or y + bh >= h - 1:
        flags.append("touches the frame edge -- the rock may be cut off")
    if max(bw, bh) > 4 * max(1, min(bw, bh)):
        flags.append("very elongated for a rock")

    return {"box": best["box"], "area": best["area"], "flags": flags,
            "rivals": len(cands) - 1}


# --------------------------------------------------------------------------- review sheets


def review_sheets(rows: list[dict], stem: str, out_dir: Path) -> int:
    """Contact sheets with the boxes drawn: green confident, red flagged.

    Flagged frames come first, so the ones needing a decision are on sheet 01 and you can
    stop looking when the red runs out.
    """
    ordered = ([r for r in rows if r["box"] and r["flags"]]
               + [r for r in rows if r["box"] and not r["flags"]])
    if not ordered:
        return 0
    out_dir.mkdir(parents=True, exist_ok=True)
    per = REVIEW_COLS * REVIEW_ROWS
    sheets = 0
    for s in range((len(ordered) + per - 1) // per):
        tiles = []
        for r in ordered[s * per:(s + 1) * per]:
            img = cv2.imread(r["path"])
            x, y, bw, bh = r["box"]
            colour = (0, 0, 255) if r["flags"] else (0, 220, 0)
            cv2.rectangle(img, (x, y), (x + bw, y + bh), colour, 3)
            cv2.putText(img, Path(r["path"]).stem[-4:], (6, 26),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, colour, 2)
            tiles.append(cv2.resize(img, REVIEW_TILE))
        blank = np.zeros((REVIEW_TILE[1], REVIEW_TILE[0], 3), np.uint8)
        while len(tiles) < per:
            tiles.append(blank)
        grid = np.vstack([np.hstack(tiles[r * REVIEW_COLS:(r + 1) * REVIEW_COLS])
                          for r in range(REVIEW_ROWS)])
        cv2.imwrite(str(out_dir / ("%s-%02d.jpg" % (stem, s + 1))), grid)
        sheets += 1
    return sheets


# --------------------------------------------------------------------------- the pass


def label_session(session: str, label: str, ratio: np.ndarray, out_dir: Path,
                  class_id: int) -> dict:
    frames = session_frames(session)
    dest = out_dir / label
    dest.mkdir(parents=True, exist_ok=True)

    rows, boxes, kept, flagged, rejected = [], {}, 0, 0, 0
    for f in frames:
        img = cv2.imread(str(f))
        if img is None:
            rejected += 1
            continue
        h, w = img.shape[:2]
        v = find_box(img, ratio)
        row = {"path": str(f), "frame": f.name, "box": v["box"],
               "flags": v.get("flags", []), "reason": v.get("reason", "")}
        rows.append(row)
        if v["box"] is None:
            rejected += 1
            continue

        x, y, bw, bh = v["box"]
        shutil.copy2(f, dest / f.name)
        # YOLO: class, centre and size, all normalised. One rock per frame by construction.
        (dest / (f.stem + ".txt")).write_text(
            "%d %.6f %.6f %.6f %.6f\n"
            % (class_id, (x + bw / 2) / w, (y + bh / 2) / h, bw / w, bh / h),
            encoding="utf-8")
        boxes[f.name] = [{"label": label, "x": x, "y": y, "width": bw, "height": bh}]
        kept += 1
        if v["flags"]:
            flagged += 1

    # Sheets are named for the label *and* the session, so a second session of the same
    # rock does not overwrite the first one's sheets.
    sheets = review_sheets(rows, "%s-%s" % (label, session), out_dir / "_review")
    print("  %-14s %3d labelled, %d flagged for review, %d rejected  (%d sheet(s))"
          % (label, kept, flagged, rejected, sheets))
    return {"session": session, "label": label, "class_id": class_id,
            "kept": kept, "flagged": flagged, "rejected": rejected,
            "frames": rows, "boxes": boxes}


def build_upload(out_dir: Path, bg_frames: list[Path], boxes: dict) -> int:
    """One flat folder to drag into Edge Impulse.

    The uploader wants the images and bounding_boxes.labels side by side in a single
    directory, which the per-class folders above are not. The empty-floor frames are copied
    in **with no entry in the labels file**: an image with no boxes is how a detector is
    taught that some views contain no rock at all, and skipping that is what produces a
    model that confidently names a rock in every frame.
    """
    up = out_dir / "upload"
    if up.exists():
        shutil.rmtree(up)
    up.mkdir(parents=True)
    for label_dir in sorted(d for d in out_dir.iterdir() if d.is_dir() and d.name != "upload"):
        if label_dir.name.startswith("_"):
            continue
        for f in label_dir.glob("*.jpg"):
            shutil.copy2(f, up / f.name)
    for f in bg_frames:
        shutil.copy2(f, up / f.name)
    (up / "bounding_boxes.labels").write_text(
        json.dumps({"version": 1, "type": "bounding-box-labels",
                    "boundingBoxes": boxes}, indent=1) + "\n", encoding="utf-8")
    return len(list(up.glob("*.jpg")))


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--list", action="store_true",
                   help="show the raw sessions on disk and stop")
    p.add_argument("--bg", metavar="SESSION",
                   help="the empty-floor session -- the colour model's negative half")
    p.add_argument("--rock", action="append", default=[], metavar="SESSION=LABEL",
                   help="a rock session and the class name for it; repeatable")
    p.add_argument("--out", default="train", metavar="SPLIT",
                   help="output split under dataset/labelled (default: train). Use a "
                        "different one for held-out frames -- they must never train.")
    p.add_argument("--negatives", metavar="SESSION",
                   help="empty-floor session to copy into the upload folder as box-free "
                        "negatives (default: --bg). A held-out split needs its OWN empty "
                        "floor here, or its no-rock score is measured on frames the model "
                        "trained against.")
    p.add_argument("--bg-frames", type=int, default=0, metavar="N",
                   help="copy this many empty-floor frames into the upload folder as "
                        "box-free negatives (0 = all of them)")
    p.add_argument("--score-min", type=float, default=SCORE_MIN,
                   help="back-projection threshold in log-odds (default %.1f); lower it "
                        "if a dull rock is being missed" % SCORE_MIN)
    args = p.parse_args(argv)

    if args.list:
        return do_list()
    if not args.bg or not args.rock:
        p.error("--bg and at least one --rock are required (or --list)")

    set_score_min(args.score_min)

    pairs = []
    for spec in args.rock:
        if "=" not in spec:
            p.error("--rock wants SESSION=LABEL, got %r" % spec)
        session, label = spec.split("=", 1)
        pairs.append((session.strip(), label.strip()))

    bg_frames = session_frames(args.bg)
    print("background model: %s (%d frames)" % (args.bg, len(bg_frames)))
    # Every 4th frame is plenty: consecutive frames half a second apart are near-duplicates,
    # and the histogram only needs the colour *range* of the floor.
    bg_hist = chroma_hist(bg_frames, stride=4)

    out_dir = OUT_ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    # Class ids are per *label*, not per session: two sessions of the same rock (a
    # re-shoot, or a partial run kept) must land on the same class, and they merge in one
    # folder because the frame names carry their session's timestamp.
    class_ids: dict[str, int] = {}
    for _, label in pairs:
        class_ids.setdefault(label, len(class_ids))

    all_boxes, report = {}, []
    for session, label in pairs:
        class_id = class_ids[label]
        frames = session_frames(session)
        fg_hist = chroma_hist(frames, stride=4)
        ratio = log_ratio(fg_hist, bg_hist)
        r = label_session(session, label, ratio, out_dir, class_id)
        all_boxes.update(r.pop("boxes"))
        report.append(r)

    (out_dir / "bounding_boxes.labels").write_text(
        json.dumps({"version": 1, "type": "bounding-box-labels",
                    "boundingBoxes": all_boxes}, indent=1) + "\n", encoding="utf-8")
    (out_dir / "autolabel-report.json").write_text(
        json.dumps({"background": args.bg, "score_min": SCORE_MIN,
                    "classes": report}, indent=1) + "\n", encoding="utf-8")

    neg_frames = session_frames(args.negatives) if args.negatives else bg_frames
    bg_frames = neg_frames
    n_bg = len(bg_frames) if args.bg_frames == 0 else min(args.bg_frames, len(bg_frames))
    # Evenly spaced rather than the first N, so a truncated set still spans the whole
    # session -- both shade levels, not just whichever came first.
    step = max(1, len(bg_frames) // n_bg) if n_bg else 1
    chosen_bg = bg_frames[::step][:n_bg]
    n_up = build_upload(out_dir, chosen_bg, all_boxes)
    print("\nupload folder: %s  (%d images, %d of them empty floor)"
          % (out_dir / "upload", n_up, len(chosen_bg)))

    total = sum(r["kept"] for r in report)
    flagged = sum(r["flagged"] for r in report)
    print("\n%d boxes in %s" % (total, out_dir))
    if flagged:
        print("check the red boxes on the first sheet(s) in %s"
              % (out_dir / "_review"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

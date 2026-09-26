# Rock recognition POC

A proof of concept for the identification half of the mission: show the system a rock, and
have it answer with an element.

It began as a desktop-only exercise while the camera board was in shipping. **The XIAO
ESP32S3 Sense arrived on 2026-09-18 and passed bring-up**, so the centre of gravity has
moved: the corpus is now shot through the board's own OV2640 over Wi-Fi, and the webcam
path is kept only for shaking the pipeline out.

**To actually build a model, follow [TRAINING.md](TRAINING.md)** — the whole procedure end
to end, from flashing the camera firmware to a live detection window, with the 2026-09-26
run's real numbers kept in it as a reference. This file is the why; that one is the how.

**It works.** A FOMO model trained on 238 auto-labelled frames scores 20/20 on each rock and
raises no false alarms on the held-out empty floor, at about 1 ms per frame on the host. The
honest caveats are in TRAINING.md §5.2 and §6.5 — the localisation-aware score is lower, and
why.

This is [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) **step 2.0c**, a dry run of the
replacement steps 2.2 (rock finishing), 2.3 (discrimination accuracy) and 2.6 (the on-board
detector). It is not a substitute for any of them: those three still have to run under real
arena light, against the real finished rocks, with the camera on the rover. What 2.0c buys
is that they start from a pipeline known to work end to end and a dataset that already
exists.

## Where this departs from the plan, deliberately

Step 2.0 chose **colour-and-size recognition** — blob detection, hue thresholds, no
training. This POC instead trains a **small CNN**, at the user's direction on 2026-09-16.

The reasoning for allowing it: the board that was actually bought is a **XIAO ESP32S3
Sense**, not the ESP32-CAM that 2.0 assumed. The S3 has 8 MB of PSRAM and vector
instructions, so TFLite Micro and Edge Impulse FOMO run on it properly, where on a plain
ESP32-CAM they are marginal. A learned classifier also picks up **shape and texture**,
which is the half of "the green *sharp* rock" that hue thresholds cannot see.

What it costs, recorded honestly, because 2.0's record is the thing that must not drift:

- **Data, instead of thresholds.** Colour-and-size needs five photos and a calibration
  card. A CNN needs 50–200 images per rock, and re-dressing the arena means recapturing
  and retraining rather than editing two numbers.
- **2.6's runtime-settable thresholds go away.** The replacement step says the detector's
  thresholds are settable at runtime "so re-dressing the arena needs no reflash". A model
  is a flash, not a setting. That promise has to be rewritten or dropped.
- **Lighting is still the binding risk, and is now less inspectable.** Under thresholds,
  a wrong answer is traceable to a hue number. Under a CNN it is traceable to nothing
  without a confusion matrix, so 2.3's ≥ 95%-over-100-detections measurement matters
  *more* here, not less.
- **Inference moved to the host on 2026-09-26**, which is a second departure and a larger
  one: 2.0 says detection runs *on the detection board* and the rover reports a colour code
  in a `tag` frame. It now runs on the operator PC, and the rover sends nothing. The reason
  is a measured 2713 ms per frame on the board against ~1 ms on an i7. The on-board sketch
  is built and kept, so this is reversible.
- **If this works, step 2.0's decision block needs amending** — candidate C is still the
  choice, but "colour-and-size on an ESP32-CAM" is no longer an accurate description of
  it, the power and airtime lines in the decision were costed against the other board, and
  the detection-board-reports-a-tag architecture is no longer what is built.

The fallback trigger from 2.0 is unchanged and still applies: **below 95% correct class
over 100 detections spanning the lighting extremes, switch to ArUco fiducials.**

## Setup

```powershell
python -m pip install -r vision/requirements.txt
```

## Capturing from the board — the path that matters

The XIAO ESP32S3 Sense arrived 2026-09-18 and is at **192.168.1.39** running the step
2.0b.2 firmware (`-e xiaostream`). Shoot the real corpus through it, not through the
laptop webcam: a model trained on webcam frames and deployed to the OV2640 faces a
**domain gap** — different sensor, lens, colour response and field of view — and that gap
costs accuracy for no visible reason.

Live view in a browser, to aim and focus:

    http://192.168.1.39/           status page with the stream embedded
    http://192.168.1.39:81/stream  the raw MJPEG, if you want it full-window

**Lock the sensor before shooting anything you intend to train on.** Fill the frame with a
neutral grey or white card under the light you will shoot in, then hit
`http://192.168.1.39/lock`. Left on auto, the OV2640 re-balances every time the scene
changes, so a green rock filling the frame drags the whole image magenta and the next shot
of the same rock is a different colour. `/status` reports `awb/aec/agc` so you can confirm
it took rather than trusting that the request returned.

Bulk capture into a folder, headless, for annotation afterwards:

```powershell
python vision/capture.py --url http://192.168.1.39 --raw --lock --shots 100 --interval 0.5 `
    --session "desk lamp, blinds shut"
```

`--raw` saves **whole VGA frames, uncropped**, into `vision/dataset/_raw/<session>/`. That
is deliberate: an annotation tool needs the entire scene to draw boxes on, and a centre
crop throws away the context that makes a box meaningful. Frames that are too similar to
the previous one are skipped, so a motionless scene does not become 100 copies — move the
rock or the camera between shots.

Interactive instead, with a window and the keyboard:

```powershell
python vision/capture.py --url http://192.168.1.39 --raw --lock
```

`SPACE` saves, `B` toggles burst, `L` re-locks, `Q` quits.

## Capturing from the webcam — `capture.py`

Still useful for shaking the pipeline out before the board is aimed. The element is stored
once per label and remembered after that:

```powershell
python vision/capture.py --label green-sharp --element Helium
python vision/capture.py --label red-round   --element Iron
python vision/capture.py --label background  --element None
```

In the window: **SPACE** saves the frame inside the yellow guide box, **B** toggles burst
capture, **L** re-attempts the camera lock, **Q** quits. Burst only saves frames that
differ enough from the last one saved, so sweeping the rock through poses fills the class
quickly while holding it still does not.

Record the lighting each sitting, because step 2.3 has to measure across the extremes:

```powershell
python vision/capture.py --label green-sharp --session "blinds open, midday"
python vision/capture.py --label green-sharp --session "lamp only, blinds shut"
```

Phone photos instead of, or alongside, the webcam:

```powershell
python vision/capture.py --label green-sharp --element Helium --import .\phone-photos
```

Progress at any time:

```powershell
python vision/capture.py --status
```

## What to actually capture

- **50 images per rock is the floor, 150 is a reasonable target.** The status readout
  says which side of those you are on.
- **Turn the rock between shots.** A model trained on one pose recognises one pose. Vary
  rotation, tilt, distance and where in the frame it sits.
- **Capture a `background` class** — arena, sand, empty table, your hand. Without it a
  classifier has no way to say "no rock here", and will confidently name a rock in every
  frame including the empty ones. This is the single most commonly skipped step and the
  one that most often makes a POC look broken.
- **Capture under more than one lighting condition**, and note each with `--session`. A
  model trained under one lamp fails the moment the arena lights differ, which is exactly
  the risk that replaced R2.
- **Keep phone photos separate if you can.** They carry the phone's own auto-exposure and
  white balance, so mixing them into the training set teaches the model that colour is
  unreliable. They make a good *evaluation* set for that same reason.

## Dataset layout

```
vision/dataset/
  manifest.json                    label -> element, plus per-session lighting and camera state
  _raw/<session>/                  full frames as shot -- what --raw produces, and what
                                   autolabel.py reads. One session is one rock.
  labelled/<split>/<label>/        autolabel.py's output: frames plus a YOLO .txt each
  labelled/<split>/_review/        contact sheets -- green box confident, red check me
  labelled/<split>/upload/         flat, with bounding_boxes.labels: what Edge Impulse takes
  green-sharp/                     320x320 crops from --label, for a classifier
```

Two shapes, for two jobs. `--raw` keeps whole frames because a detector's boxes need the
whole scene; `--label` writes 320 × 320 centre crops, which is what an image *classifier*
trains on. 320 is larger than any likely training size (96 or 160 px), so changing the input
resolution later is a retrain and not a recapture.

Since [autolabel.py](autolabel.py) arrived, **`--raw` is the path that matters** — the crops
are left in for the classifier option.

`manifest.json` holds the **label → element** mapping rather than the folder names doing
it. That mirrors [console/data/compositions.json](../console/data/compositions.json): the
rover reports an identifier, and something console-side turns it into a displayed
element, so the arena can be re-dressed without touching the detector. The element names
in it are presets, not measurements — the same caveat that file carries.

## Is the dataset in git?

**No, by default.** [.gitignore](.gitignore) excludes the JPEGs and keeps
`manifest.json`, so the label → element mapping and the capture provenance are versioned
but the images are not. The reasoning is that a POC generates and discards a lot of
frames, and churning binaries through git history is unpleasant.

That is a reversible call and arguably the wrong one — the dataset is the expensive part
and the only thing here that cannot be regenerated. Delete the two lines at the top of
`.gitignore` to version it.

## Annotating — done for you

**Nobody draws boxes here.** [autolabel.py](autolabel.py) finds the rock and writes the
boxes, and you check its work instead of doing it. That is possible because of how the
corpus is shot: one rock per session on the arena floor, plus a session of the *empty*
floor under the same lock. A colour model built by contrasting the two lights the rock up
and leaves the floor dark; the session already says which rock is in frame, so the class
needs no clicking either.

The full procedure is [TRAINING.md](TRAINING.md) §3. What was true before still holds about
the model itself:

- **Boxes make it an object detector, not a classifier**, which is what the mission needs —
  the console wants to know *where* the rock is, not only *what*, because bearing is the
  thing candidate C was picked for.
- **FOMO gives centroids, not tight boxes.** Boxes are the input format; do not expect box
  dimensions back at inference time. Centroids are enough for bearing.
- **Objects should not overlap much.** FOMO struggles when two rocks touch.
- **The `background` frames go up with no boxes at all.** Frames with no boxes are how the
  model learns what "no rock" looks like, and skipping them is the most common reason a
  detector fires constantly.
- **Keep the lighting spread**, and keep the *distance* spread — see TRAINING.md §5.2 for
  what the second one cost when it was missed.

## Where inference runs — the host, decided 2026-09-26

The board serves its camera stream; the operator PC runs the model. The same model on the
same frames takes about **1 ms** on an i7 against a **2713 ms** estimate on the ESP32-S3,
and changing a model is dropping in a file rather than a rebuild and a reflash.

It also fits the architecture rather than fighting it: the console is a PC and already owns
the tag → element lookup, precisely so the arena can be re-dressed without reflashing.

```powershell
python vision/detect_stream.py --url http://192.168.1.39
```

The on-board detector exists — `firmware/src/xiao_detect.cpp`, `[env:xiaodetect]` — and is
**parked, not abandoned**: it builds and reaches inference, and the final demo may still want
a rover that identifies rocks with no PC in the loop. TRAINING.md §7 has it, including the
three bugs that cost a day.

**One consequence to record, not to let slide:** with detection host-side the rover never
sends a `tag` frame, so step 2.0's "detection runs on the detection board" is no longer what
is built. That belongs in step 2.0's amendment review with the other drifts listed above.

## What is built

| | |
|---|---|
| [capture.py](capture.py) | shoot through the board (or a webcam), sensor locked, provenance recorded |
| [autolabel.py](autolabel.py) | find the rock, write the boxes, build the Edge Impulse upload folders |
| [detect_stream.py](detect_stream.py) | pull the stream, run the model, put the element name on screen |
| [TRAINING.md](TRAINING.md) | the whole procedure, with the 2026-09-26 run's real numbers |

Not built: the console integration. The detector runs in its own window beside the console
rather than inside it, and how the result reaches the analysis panel is the open question —
see TRAINING.md §6.6.

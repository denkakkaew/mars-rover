# Training the rock detector, end to end

Everything from an empty folder to a model that names a rock: shooting the corpus through the
XIAO, labelling it without drawing a single box, training FOMO in Edge Impulse, and running
the result live.

**Inference runs on the host** (section 6), decided 2026-09-26: the board serves its camera
stream and the operator PC does the thinking, which is about a thousand times faster and
needs no reflash to change a model. The on-board route is built, and kept in section 7.

This is the procedure actually followed on **2026-09-26**, with that run's real numbers kept
in place as a reference for what "working" looks like. It belongs to
[IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) steps 2.0c (corpus and POC), 2.0d
(auto-labelling) and 2.0e (live inference); see [README.md](README.md) for why this POC departs from step 2.0's recorded
colour-and-size decision.

**Budget about two hours** the first time, most of it shooting and waiting on training.

---

## 0. What you need

| | |
|---|---|
| Board | XIAO ESP32S3 Sense, camera module seated, antenna fitted |
| Firmware | `[env:xiaostream]` — see step 1 |
| Network | the 2.4 GHz SSID in `firmware/include/secrets.h` |
| PC | Python with `opencv-python` and `numpy` (`tools/requirements.txt`) |
| Account | a free Edge Impulse account at studio.edgeimpulse.com |
| Rocks | the real finished rocks, and the real arena floor |

**Shoot through the board, on the arena floor.** A model trained on phone photos or webcam
frames and deployed to the OV2640 faces a domain gap — different sensor, lens, colour
response, field of view — and a model trained against a white sheet of paper learns the paper.
Both cost accuracy for no visible reason. The whole labelling method in step 3 depends on
this too.

---

## 1. Flash the camera firmware and find the board

This is the **only** firmware the host-side approach needs, and the only time the board gets
reflashed. Everything after it is Python.

```powershell
cd firmware
python -m platformio run -e xiaostream -t upload -t monitor
```

The monitor prints the IP once it joins. On 2026-09-26 it was **192.168.1.39**; use whatever
yours prints. If the port will not appear at all: hold **BOOT**, tap **RESET**, release BOOT.

The board serves two ports, and that split is deliberate — an MJPEG handler never returns, so
a single-threaded server with a stream open starves every control endpoint:

| Port | Endpoint | |
|---|---|---|
| 81 | `/stream` | MJPEG, what you watch and what `capture.py` pulls from |
| 80 | `/snap` | one JPEG |
| 80 | `/status` | JSON: IP, RSSI, heap, PSRAM, framesize, and the AGC/AEC/AWB state |
| 80 | `/lock` | freeze exposure, gain and white balance |
| 80 | `/control` | sensor settings |

---

## 2. Shoot the corpus

### 2.1 Set the scene, once

Put the camera **where it will live on the rover** — same height, same forward-and-down
angle — and use the **real arena floor**. Fix the arena lighting and do not touch it again
until section 2.5.

Open `http://<board-ip>:81/stream` in a browser and check the framing: rock in view, in
focus, right way up. Fix the aim now, not after 400 frames.

### 2.2 Lock the sensor

```powershell
curl http://192.168.1.39/lock
curl http://192.168.1.39/status
```

`/status` must come back with `"agc":0, "aec":0, "awb":0`. **This is not optional.** With
auto-exposure running, every frame is taken under a slightly different virtual light, and the
model spends its capacity learning that instead of learning the rock. Step 2.0 named lighting
stability as what replaced risk R2, and this is the mechanism.

Aim the lens at a neutral grey or white card under the working light, *then* lock. A strong
colour cast after locking is harmless as long as it is the same in every session — the
2026-09-26 corpus has a heavy green cast throughout and trained fine.

### 2.3 One session per rock

`--session` is a note recorded in the manifest; frames land in a timestamped folder under
`vision/dataset/_raw/`.

```powershell
cd ..
python vision/capture.py --url http://192.168.1.39 --raw --lock `
    --session "green-sharp" --shots 100 --interval 0.5
```

**One rock in frame, nothing else.** While the 100 shots run (50 seconds), keep moving it:

- slide it around the frame — centre, each corner, near, far
- turn it a little between moves so every face is seen
- tip it onto a different side a few times
- vary distance, so it is sometimes small in frame and sometimes large

**Decide the working range before you shoot, and make every session span it.** How close does
the rover get before it announces? If it stops about 30 cm away, shoot from 20 cm to 1 m —
in the training sessions *and* the test ones, for *every* rock. FOMO answers per grid cell
(20 × 20 at 160 px), so a rock covering one cell and the same rock covering twenty are
different things to it: it does not generalise across scale the way it generalises across
position. This is the single mistake that cost the 2026-09-26 run its gate — see §5.2.

Repeat per rock, one session each, changing `--session`.

### 2.4 One session of the empty floor

```powershell
python vision/capture.py --url http://192.168.1.39 --raw --lock `
    --session "background" --shots 100 --interval 0.5
```

Nothing in frame but floor. Move the camera between shots, include your hand, include
whatever else will be in the arena. Two shade levels in one session is good — it widens what
the model accepts as "floor".

**This session does two jobs**, which is why it is worth 100 frames rather than 20:

1. it teaches the detector that some views contain no rock at all — skip it and you get a
   model that confidently names a rock in every single frame, empty ones included;
2. it is the negative half of the colour model that labels everything in section 3.

### 2.5 Now change the light, and shoot the test set

Move the lamp, open or close the blinds — make it visibly different. **Lock again.** Then a
short round of every rock plus the floor:

```powershell
curl http://192.168.1.39/lock
python vision/capture.py --url http://192.168.1.39 --raw --lock --session "test-green-sharp"  --shots 20 --interval 0.5
python vision/capture.py --url http://192.168.1.39 --raw --lock --session "test-red-round"    --shots 20 --interval 0.5
python vision/capture.py --url http://192.168.1.39 --raw --lock --session "test-background"   --shots 20 --interval 0.5
```

**These never train.** They are the only honest measurement you will get, and the one step
2.0's 95% gate is settled against. A model scored on the light it was trained under tells you
nothing about the arena on demo day.

### 2.6 Target counts

| Session | Frames |
|---|---|
| each rock | 100 |
| `background` | 100 |
| each `test-<rock>` | 20 |
| `test-background` | 20 |

Check what landed:

```powershell
python vision/autolabel.py --list
```

---

## 3. Label it — automatically

```powershell
python vision/autolabel.py --list
```

```
session            frames  note typed at capture
20260926-110345        49  -
20260926-110447       100  green-sharp
20260926-111622       100  red-round
20260926-112022       100  background
20260926-112602        20  test-green-sharp
20260926-115901        20  test-red-round
20260926-120043        20  test-background
```

### 3.1 How it works, in one paragraph

Hand-annotating 300 frames is the step that stalls a vision POC, so
[autolabel.py](autolabel.py) does it and asks you to *check* rather than to draw. It builds a
2-D histogram of Lab chroma over a rock's session and another over the empty-floor session,
and takes the log-ratio: per colour, "how much more common is this when the rock is in shot
than when it is not". Back-projecting that onto a frame lights the rock and leaves the floor
dark; the largest blob on the floor becomes the box. Nothing is tuned per rock, and the
board's colour cast cancels out because both histograms carry it. Blobs up in the top 30% of
the frame are discarded — that band is the room behind the arena, not the floor, and a box on
a yellow waste bin teaches worse than a dropped frame does.

**Which rock is in the picture never has to be clicked**, because one session is one rock.
That is the whole reason section 2.3 insists on shooting them separately.

### 3.2 Training split

Repeat `--rock SESSION=LABEL` per session. Two sessions of the same rock are fine — same
label, they merge.

```powershell
python vision/autolabel.py --bg 20260926-112022 `
    --rock 20260926-110447=green-sharp `
    --rock 20260926-110345=green-sharp `
    --rock 20260926-111622=red-round
```

```
green-sharp     99 labelled, 4 flagged for review, 1 rejected
green-sharp     44 labelled, 7 flagged for review, 5 rejected
red-round       95 labelled, 2 flagged for review, 5 rejected
upload folder: vision/dataset/labelled/train/upload  (338 images, 100 of them empty floor)
```

### 3.3 Test split

`--bg` is the colour model; `--negatives` is which empty-floor frames get copied in as
box-free negatives. **A held-out split needs its own**, or its false-alarm rate is measured on
frames the model trained against. Keep `--bg` pointed at the big training background — it is
only colour statistics, and a 20-frame histogram is too thin to label well.

```powershell
python vision/autolabel.py --bg 20260926-112022 --negatives 20260926-120043 --out test `
    --rock 20260926-112602=green-sharp `
    --rock 20260926-115901=red-round
```

### 3.4 Check the flagged ones

Open the contact sheets in `vision/dataset/labelled/<split>/_review/`. **Green box = confident,
red = check me**, and flagged frames are placed first so you can stop looking when the red runs
out.

A red box that is not on the rock: note the 4-digit number in its corner and delete both that
frame's `.jpg` and its `.txt` from `vision/dataset/labelled/<split>/<label>/`, then re-run the
command to rebuild `upload/`. On the 2026-09-26 corpus every flagged box turned out correct,
so nothing was deleted.

`--score-min` (default 1.5, in log-odds) is the one knob: lower it if a dull rock is being
missed, raise it if the floor keeps lighting up.

### 3.5 What you end up with

```
vision/dataset/labelled/
  train/
    green-sharp/  red-round/        the frames, plus YOLO .txt per frame
    _review/                        contact sheets
    upload/                         ← flat, this is what Edge Impulse ingests
    bounding_boxes.labels
    autolabel-report.json           every decision, including the rejects
  test/ …
```

2026-09-26: **train 338 images (238 boxed, 100 empty floor); test 60 (40 boxed, 20 empty)**.

---

## 4. Train FOMO in Edge Impulse

### 4.1 The project

New project → when asked what you are building, pick **Object detection**.

If the learning-block list later offers only *Classification*, *Transfer Learning (Images)*,
*Regression* and the anomaly blocks, the project is in classification mode. Fix it at
**Dashboard → Project info → Labeling method → Bounding boxes (object detection)**. If that
will not change because data is already in, make a fresh project — re-uploading 400 small
images takes under a minute. **This is not a paid feature**; object detection is on the free
plan.

### 4.2 Upload

**Data acquisition → Upload data**, twice:

| Folder | Category |
|---|---|
| `vision/dataset/labelled/train/upload/` | Training |
| `vision/dataset/labelled/test/upload/` | Testing |

- **Image label format: Edge Impulse (`bounding_boxes.labels`)** — that is the file
  `autolabel.py` writes.
- Select the images **and** `bounding_boxes.labels` in the same batch. Sent separately, the
  images arrive unlabelled.
- The empty-floor frames have no entry in that file **on purpose**. An image with no boxes is
  how a detector learns "no rock here".

### 4.3 Impulse

**Create impulse** → **Add a processing block → Image** → **Add a learning block → Object
Detection (Images)** → Save.

Colour depth is not set here. Go to **Image** in the sidebar → **Colour depth: RGB** → Save
parameters → **Generate features**. Keep RGB: the rocks are told apart largely by colour, and
grayscale throws that away.

The feature explorer is an early read, not a verdict — separated clumps per class means
training will go easily.

### 4.4 Train

**Object detection** in the sidebar:

| Field | Value |
|---|---|
| Image size (set back on Create impulse) | **160 × 160**, or 96 × 96 for speed — see §4.6 |
| Training cycles | 60 |
| Learning rate | 0.001 |
| Validation set size | 20% |
| Model | **FOMO (Faster Objects, More Objects) MobileNetV2 0.35** |

FOMO is the one that fits an ESP32-S3. If the list offers YOLOv5 or similar, those are for
larger hardware — do not take them. **`Visual Anomaly Detection - FOMO-AD` is a different
block** despite the name: it flags "this looks unusual" without knowing what it is looking at,
so it cannot tell one rock from another.

### 4.5 What FOMO gives back

**Centroids, not boxes.** Boxes are the input format; the output is a class, a centre point
and a confidence. That is enough to announce "Helium detected" and to steer toward the rock,
which is what the mission needs. It also means overlapping or touching rocks are its weak
spot — FOMO can merge two into one.

The model divides the image into a grid — 160×160 gives 20×20 cells, 96×96 gives 12×12 — and
answers per cell. A rock that is small in frame lands in one cell, which still detects but
localises coarsely.

### 4.6 Reference results, 2026-09-26, 160 × 160

Validation F1 **98.0%**:

| | background | green-sharp | red-round |
|---|---|---|---|
| background | 100% | 0% | 0% |
| green-sharp | 3.0% | 97.0% | 0% |
| red-round | 5.9% | 0% | 94.1% |

Precision (non-background) 1.00, recall 0.96. On-device estimate: **2713 ms**, 305.5 K peak
RAM, 81.3 K flash.

Two things to read out of that table:

- **The rocks are never confused with each other** — 0% both ways. Every error is a rock
  called `background`: a miss, not a wrong name. That is the right failure mode here, since
  the console never announces the wrong element; it occasionally says nothing and you drive
  closer.
- **2713 ms is too slow** for a live view — 0.37 fps. Two things bring it down and they
  stack: **96 × 96** (roughly 2.7× fewer pixels) and **ESP-NN**, the ESP32-S3's vector
  instructions, which the Studio estimate generally does not include. Measure on the board
  rather than trusting either figure.

Validation numbers are carved out of the training sessions under the same light, so they are
optimistic by construction. Section 5 is the honest one.

---

## 5. Model testing — the number that counts

**Model testing** in the sidebar → **Classify all**. This runs the held-out set from §2.5,
shot under different light, which the model has never seen.

**The gate, from step 2.0, unchanged: below 95% correct class across the lighting extremes,
the fallback is ArUco fiducials** — printed markers on the rocks. That trigger is a
measurement, not an opinion, and this page is where it is taken.

Reading the result:

| What you see | What it means | What to do |
|---|---|---|
| Test score close to validation | The model learned the rock | Deploy it |
| Test much lower than validation | It leaned on the lighting | More sessions under more lighting — not more epochs |
| **One class fails while another passes under the same light** | **Not lighting. Check scale first — §5.2** | Match the distance range between splits |
| Rocks confused with each other | They are genuinely too alike to the sensor | Change one rock's colour, or fall back to ArUco |
| Rocks called `background` | Misses. Usually too small in frame, or a size the training set never covered | Shoot the full working range, or move to 160 × 160 |
| `background` called a rock | Not enough empty-floor data, or it is seeing the room | More floor frames, including the arena's edges |

Record the score, the per-class rows and the inferencing time. Step 2.0c's review gate asks
for exactly this: an honest accuracy figure against a held-out set captured under *different*
light.

### 5.1 Getting the numbers out

The Studio page may show only a headline percentage. The full breakdown is in
**Dashboard → download block output → model evaluation metrics (JSON)**, which carries both
splits — `validation` (optimistic, carved from the training sessions) and `test` (the honest
one). Read the `test` branch: `confusion_matrix`, `class_names`, and `non_background`.

### 5.2 Reference result, 2026-09-26 — the gate was missed, and why

| Class | Correct | Precision | Recall |
|---|---|---|---|
| green-sharp | 12 / 20 | 0.60 | 0.60 |
| red-round | **20 / 20** | 1.00 | 1.00 |
| non-background overall | | 0.80 | 0.80 |

**F1 0.80 against a 95% gate**, where validation had said 98%.

The obvious reading — the changed lighting broke it — is wrong, and the evidence is
red-round: a perfect score under that *same* light. The actual cause was rock size in frame,
measured out of `autolabel-report.json`:

| Session | Rock area, median % of frame |
|---|---|
| green-sharp, training | 1.83 |
| green-sharp, test | **6.68** (up to 24) |
| red-round, training | 1.16 |
| red-round, test | 1.08 ✅ |

The green rock was shot **3.6× closer** in its test session than in training, so the model
was asked about a size it had never seen. Red-round's distances matched and it scored 100%.
The eight false green-sharp detections fit the same cause: an oversized blob fragmenting
across cells.

**This was accepted rather than fixed.** The two objects are stand-ins, the floor is not the
glass arena, and the point of the exercise was to prove the pipeline — capture, auto-label,
train, measure — which it did. The fix is written down in §2.3 and applies to the real corpus:
6–8 rocks, in the glass box, every session spanning the same working distance range.

**Step 2.0's fallback trigger is not tripped by this run.** It is a measurement "across the
lighting extremes" against the real rocks in the real arena, and this was neither.

---

## 6. Running the model — on the host

**This is the chosen approach, decided 2026-09-26.** The board stays a camera; inference
runs on the operator PC. Section 7 keeps the on-board route, which is built but parked.

The reason is arithmetic. The same model, the same frames:

| | XIAO ESP32-S3 | i7 laptop |
|---|---|---|
| Per frame | 2713 ms (Studio estimate) | **~1 ms measured** |
| Practical rate | under 1 fps | limited by the camera, not the CPU |
| Model choice | FOMO MobileNet 0.35, and little else fits | anything — real boxes, higher input, more classes |
| Changing the model | rebuild and reflash | drop in a file, rerun |

It also sits comfortably in the existing architecture rather than against it: the console is
a PC, and it already owns the tag → element lookup precisely so the arena can be re-dressed
without reflashing. Detection moving to the same side of the link is consistent with that.

What it costs is recorded in §7.4.

### 6.1 Export the model

Edge Impulse → **Deployment** → **TensorFlow Lite** → **Build**, and put the `.tflite` from
the zip into `vision/model/`.

If what you have is a **C++ library** export instead, it contains the same file as long as
**EON Compiler was off** when it was built — look for `tflite-model/*.tflite` inside the zip
and copy it out. An EON-compiled export has no `.tflite` in it at all; by then the model has
been turned into C++ source.

`vision/model/` is gitignored for size, so keep the zip somewhere else. The Studio project is
not backed up by this repo either.

### 6.2 Install the runtime, once

```powershell
python -m pip install -r tools/requirements.txt
```

The interpreter is **`ai-edge-litert`** — TensorFlow Lite on its own, without TensorFlow.
That choice is deliberate: `tflite-runtime` has no Windows wheel at all, and the full
`tensorflow` wheel on this machine is built against numpy 1.x and will not import beside the
installed numpy 2.5. `ai-edge-litert` has a Windows/Python 3.12 wheel and runs alongside
numpy 2.x and OpenCV 5 with nothing downgraded.

### 6.3 Run it

```powershell
# once, if the board is not already on the camera firmware
python -m platformio run -e xiaostream -t upload -t monitor

curl.exe -s http://192.168.1.39/lock
python vision/detect_stream.py --url http://192.168.1.39
```

A window opens with the live view, a marker on each rock, and **HELIUM DETECTED** across the
top. The element name comes from `dataset/manifest.json`, never from the model — re-dressing
the arena needs no retraining.

**Lock the sensor first, every session**, for the reason in §2.2: the corpus was shot with
AGC/AEC/AWB frozen, and inference should see the colours it was trained on.

| Switch | |
|---|---|
| `--bench 200` | no window; prints end-to-end fps and mean inference time |
| `--source <folder>` | replay stills instead of the board — checks the pipeline with nothing on the network |
| `--threshold 0.7` | confidence a grid cell needs; raise it if the floor triggers |
| `--labels a,b` | class names in the model's output order, if the training folders are gone |
| `--model <path>` | a specific `.tflite` instead of the newest in `vision/model/` |

Keys in the window: `q` quit, `space` pause, `s` save the frame with its overlay to
`vision/detections/`.

### 6.4 What the script does with FOMO's output

Worth knowing, because it is not a box. The model returns a 20×20 grid with a probability per
class per cell, channel 0 meaning background. [detect_stream.py](detect_stream.py) takes the
cells that clear the threshold, joins touching cells of the same class into one blob, and
reports each blob's confidence-weighted centre. A rock covering twelve cells and a rock
covering one both come back as a single point — which is what the console acts on.

It also undoes the centre crop. "Fit shortest axis" means the model sees the middle 480×480 of
a 640×480 frame, so the outer 80 px columns are in the video and invisible to the detector;
coordinates are mapped back to full-frame pixels before anything is drawn.

### 6.5 Reference result, 2026-09-26 — host side

Against the held-out test frames from §2.5:

| Set | Frames | Result |
|---|---|---|
| test green-sharp | 20 | 20/20 green-sharp |
| test red-round | 20 | 20/20 red-round |
| test background | 20 | 0 false alarms |

**~1 ms per frame.**

Read that next to §5.2's 80%, carefully. This measurement asks *"is the right rock detected
somewhere in the frame"*, which is what "Helium detected" needs. Edge Impulse's F1
additionally requires the centroid to land **on** the rock, and scores a right-class,
wrong-place hit as both a miss and a false alarm. Both numbers are honest; they answer
different questions, and the localisation one is the harder and the more demanding. The scale
mismatch in §5.2 is still real, and still gets fixed by the discipline in §2.3.

### 6.6 Still to do

- Fold the detector into the console, so the operator sees one screen rather than a Python
  window beside it.
- Decide how the result reaches the console's analysis panel. With detection host-side the
  rover never sends a `tag` frame, which is a **departure from step 2.0's recorded design**
  ("detection runs on the detection board") and belongs in that step's amendment review.
- Keep the element lookup console-side in
  [console/data/compositions.json](../console/data/compositions.json).

---

## 7. The on-board route — built, parked

Everything here works up to the point noted, and is kept because the final demo may want the
rover to identify rocks with no PC in the loop. **It is not the current approach.**

### 7.1 Export and install

Deployment → **C++ library** or **Arduino library**, EON Compiler **on**, quantisation
**int8**. Unzip into `firmware/lib/EiRockModel/`, replacing the folder wholesale, and restore
**`library.json`** — the one hand-written file in there, and the only part of it this repo
owns. That manifest does four things the export cannot:

| | |
|---|---|
| `srcFilter` | compiles the nested SDK tree, excluding CMSIS (ARM) and every porting layer but Espressif's |
| `EI_PORTING_ESPRESSIF=1` | selects that porting layer |
| `EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1` **and** `..._ESP_NN_S3=1` | the ESP32-S3 vector kernels. **Both are needed.** With only the first, the S3 assembly files compile to empty objects — every body in them is behind the second guard — and the link fails on a wall of undefined `*_esp32s3` symbols, which looks like a missing file and is not |
| `-O3` | the SDK is unusably slow at `-Os` |

### 7.2 Build and flash

```powershell
cd firmware
python -m platformio run -e xiaodetect -t upload -t monitor
```

The first build compiles the whole TensorFlow Lite Micro tree — several minutes.
`http://<ip>/` then serves the live view with the detections drawn over it by the browser,
plus `/detect` as JSON and a `/threshold?val=` knob.

### 7.3 Three things that bit, and their fixes

All three are in [src/xiao_detect.cpp](../firmware/src/xiao_detect.cpp), and not one of them
announced itself honestly:

- **The tensor arena must go to PSRAM.** It is around 300 KB; internal RAM is 320 KB in
  total, shared with Wi-Fi, lwIP, the camera's DMA descriptors and two HTTP servers. Taking
  it from there does not fail cleanly — it starves those, and the board panics in
  `ll_cam_memcpy` or in the Wi-Fi stack's `esf_buf_alloc`, neither of which mentions
  inference. The sketch overrides `ei_malloc`/`ei_calloc`/`ei_free` to send big blocks to
  PSRAM and small ones to internal RAM.
- **`heap_caps_aligned_alloc` must be freed with `heap_caps_aligned_free`.** On this IDF it
  returns a pointer *inside* its block, with the offset recorded ahead of it.
  `heap_caps_free` assumes a block start, frees the wrong thing, and leaves the free list
  pointing at nonsense. Nothing fails at that moment; the *next* allocation panics inside
  `tlsf`, which reads as an ESP-IDF bug rather than as a mismatched free here. The
  deprecation warning on `heap_caps_aligned_free` is about the name, not about it being
  wrong — suppress it with a `#pragma`, do not substitute the other function.
- **The stream starves the detector.** The MJPEG handler runs above the detector's task
  priority, so it re-takes the camera mutex before the detector is ever scheduled: 23 of the
  first 24 attempts failed and not one inference ran. Two changes fix it — hold the camera
  only long enough to `memcpy` the JPEG out, decode and infer with it free, and sleep briefly
  between stream frames.

Because each of those was silent, the sketch now counts its failures and reports them at
`/detect`: `fail_mutex`, `fail_fb`, `fail_decode`, `fail_resize`, `fail_infer`. A detector
quietly doing nothing must not look the same as one watching an empty floor.

### 7.4 Where it stands

Builds clean (919 KB flash, 17% static RAM) and reaches inference. **The heap fix has not
been flashed and verified on hardware** — that is where the host-side switch was made. If
this route is picked up again, that is the next step, and the number to take is the measured
inference time with ESP-NN, because Studio's estimate is made without it.

The cost of this route, stated plainly: every model change is a rebuild and a reflash, and
debugging is microcontroller debugging. What it buys is a rover that identifies rocks with no
PC in the loop.

### 7.5 What retraining costs, either way

Re-shoot (§2), re-label (§3), retrain (§4), re-export. Host-side that is where it ends;
on-board it is a reflash as well. A model is a flash, not a setting — which is why **step
2.6's promise of runtime-settable detector thresholds does not survive the CNN choice**, and
why the arena lighting wants settling before the corpus is shot rather than after.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Serial port never appears | Board not in bootloader | Hold BOOT, tap RESET, release BOOT |
| `psramFound()` false | `memory_type` wrong | `board_build.arduino.memory_type = qio_opi`; `-DBOARD_HAS_PSRAM` is not the fix |
| `/lock` times out | A stream is open on a single-threaded server | Use `xiaostream` — ports 80 and 81 are separate servers for this reason |
| `/status` shows `agc:1` after `/lock` | The lock did not take | Call it again and re-check; do not shoot until it reads 0 |
| `capture.py` saves 0 frames | Wrong IP, or stream not up | Open `http://<ip>:81/stream` in a browser first |
| Phone photos silently skipped | HEIC is not in `IMAGE_SUFFIXES`, and OpenCV cannot decode it anyway | iPhone: Settings → Camera → Formats → **Most Compatible**. Better: shoot through the board |
| `autolabel.py` rejects most frames | Rock colour too close to the floor's | Lower `--score-min`; if that fails the rock is genuinely not separable by colour |
| Boxes on the room, not the rock | Distractors low in frame | Re-shoot with the arena's edges out of view, or crop |
| Learning block list has no object detection | Project is in classification mode | Dashboard → Project info → Labeling method |
| `no .tflite in vision/model` | The export was a C++/Arduino library, or EON was on | Export **TensorFlow Lite**, or pull `tflite-model/*.tflite` out of a non-EON C++ zip — §6.1 |
| `ModuleNotFoundError: ai_edge_litert` | Runtime not installed | `python -m pip install -r tools/requirements.txt` |
| `numpy.core._multiarray_umath failed to import` | The installed `tensorflow` wheel is built against numpy 1.x | Ignore it — nothing here uses TensorFlow; `ai-edge-litert` is the interpreter |
| `detect_stream.py` shows no window, no error | No frames arriving | Open `http://<ip>:81/stream` in a browser; try `--source <folder>` to prove the model path separately |
| Wrong element announced | Class order or the manifest | `--labels a,b` sets the model's output order; the element comes from `dataset/manifest.json` |
| Colours look nothing like training | Sensor not locked | `curl.exe -s http://<ip>/lock`, then check `/status` reads `agc/aec/awb` 0 |
| Link fails on undefined `*_esp32s3` symbols | `EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN_S3` not defined, so the S3 assembly compiled to empty objects | Both ESP-NN flags in `library.json` — see §7.1 |
| `run_classifier` returns an allocation error | ~300 KB arena did not fit internal RAM | The sketch's `ei_malloc` override handles it; check `/status` `psram_allocs` |
| Panic inside `tlsf`/`multi_heap` on an allocation | A mismatched free corrupted the heap earlier | `heap_caps_aligned_alloc` must be freed with `heap_caps_aligned_free`, never `heap_caps_free` — see the note on `ei_free` in the sketch |
| `heap_caps_aligned_free` deprecation stops the build | Deprecated name in this IDF | Suppress it locally with a `#pragma GCC diagnostic` — do **not** swap in `heap_caps_free` |
| `/detect` shows `fail_mutex` climbing | The stream handler is starving the detector | Hold the camera only for the JPEG copy, and yield between stream frames |
| Detections drawn in the wrong place | Centre-crop mapping | The board maps model space to full-frame pixels; check `kCropX` against the frame size |
| Uploaded images arrive unlabelled | `bounding_boxes.labels` not sent with them | Re-upload, selecting the labels file in the same batch |

---

## Where things live

| | |
|---|---|
| [capture.py](capture.py) | shoot through the board or a webcam; locks the sensor and records what it accepted |
| [autolabel.py](autolabel.py) | finds the rock and writes the boxes; builds the Edge Impulse upload folders |
| [detect_stream.py](detect_stream.py) | **the live detector** — pulls the board's stream, runs the model, draws the element name |
| `model/*.tflite` | the trained model the host runs (gitignored) |
| [../firmware/src/xiao_detect.cpp](../firmware/src/xiao_detect.cpp) | the parked on-board detector, §7 |
| `../firmware/lib/EiRockModel/` | the Edge Impulse C++ export for that, generated and gitignored apart from `library.json` |
| `dataset/_raw/<session>/` | untouched frames as shot |
| `dataset/manifest.json` | label → element, and the capture provenance of every session |
| `dataset/labelled/<split>/upload/` | what gets uploaded |
| [README.md](README.md) | why this POC exists and where it departs from step 2.0 |

Three firmware-side notes that outlive this document: the sensor lock is not optional, the
XIAO's PSRAM needs `memory_type = qio_opi`, and an MJPEG handler never returns — which is why
`xiaostream` runs two servers on two ports.

The dataset is **gitignored by default** — see [.gitignore](.gitignore), where two lines at the
top reverse that. It is a decision about repo size against reproducibility, and it has not been
made yet.

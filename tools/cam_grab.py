#!/usr/bin/env python3
"""Decode base64 JPEG frames dumped over the serial line by a camera bring-up sketch.

Both camera sketches -- firmware/src/cam_check.cpp (ESP32-CAM) and
firmware/src/xiao_cam.cpp (XIAO ESP32S3 Sense) -- answer the `d` command by printing a
frame as base64 between two markers:

    ---BEGIN JPEG 7412---
    /9j/4AAQSkZJRgABAQAAAQABAAD...
    ---END JPEG---

This turns that back into a file you can look at. That is the whole point of the dump: a
JPEG *length* proves the sensor is clocking pixels, but only an actual image proves the
picture is a picture -- in focus, correctly exposed, the right way up, and not a field of
purple because the ribbon is in backwards.

It needs no Wi-Fi, no credentials and no arena, which is why bring-up uses it instead of
waiting for a stream.

Usage:
    python tools/cam_grab.py --port COM7                 # watch, save every frame dumped
    python tools/cam_grab.py --port COM7 --send d        # ask for one frame, save, exit
    python tools/cam_grab.py --port COM7 --count 5 --send d
    python tools/cam_grab.py --from-file capture.log     # decode a saved monitor log

Frames land in vision/dataset/_bringup/ by default -- deliberately inside the POC's
dataset folder but under a name no label will collide with, so bring-up shots are near
the corpus without polluting a class.

Needs pyserial (in tools/requirements.txt) unless --from-file is used.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import re
import sys
from pathlib import Path

BEGIN = re.compile(r"^---BEGIN JPEG (\d+)---\s*$")
END = "---END JPEG---"

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "vision" / "dataset" / "_bringup"


def save(payload: bytes, declared: int, out_dir: Path, index: int) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = out_dir / ("frame_%s_%03d.jpg" % (stamp, index))
    path.write_bytes(payload)

    # The sketch prints the length it believes it sent. A mismatch means characters were
    # lost in transit, which on a real UART usually means the monitor could not keep up --
    # worth saying out loud, because a truncated JPEG opens as a half-grey image and looks
    # like a camera fault rather than a serial one.
    note = ""
    if declared and len(payload) != declared:
        note = "  MISMATCH: sketch declared %d bytes, decoded %d -- serial dropped data" % (
            declared, len(payload))

    # JPEG starts FFD8 and ends FFD9. Cheap integrity check that costs nothing.
    if not payload.startswith(b"\xff\xd8"):
        note += "  NOT A JPEG: missing SOI marker"
    elif not payload.rstrip().endswith(b"\xff\xd9"):
        note += "  TRUNCATED: missing EOI marker"

    print("saved %s  (%d bytes)%s" % (path, len(payload), note))
    return path


def decode_stream(lines, out_dir: Path, limit: int, echo: bool) -> int:
    """Consume an iterable of text lines, saving each framed JPEG. Returns count saved."""
    collecting = False
    declared = 0
    chunks: list[str] = []
    saved = 0

    for line in lines:
        line = line.rstrip("\r\n")

        match = BEGIN.match(line)
        if match:
            collecting = True
            declared = int(match.group(1))
            chunks = []
            continue

        if collecting and line.strip() == END:
            collecting = False
            try:
                payload = base64.b64decode("".join(chunks), validate=False)
            except Exception as exc:  # noqa: BLE001 - report and keep watching
                print("decode failed: %s" % exc)
                continue
            saved += 1
            save(payload, declared, out_dir, saved)
            if limit and saved >= limit:
                return saved
            continue

        if collecting:
            chunks.append(line.strip())
        elif echo and line:
            print(line)

    return saved


def serial_lines(port: str, baud: int, send: str, timeout: float):
    try:
        import serial  # pyserial
    except ImportError:
        raise SystemExit(
            "pyserial is not installed. Either:\n"
            "  python -m pip install -r tools/requirements.txt\n"
            "or capture a monitor log and use --from-file instead:\n"
            "  python -m platformio device monitor > capture.log")

    with serial.Serial(port, baud, timeout=timeout) as ser:
        if send:
            # The sketch may still be printing its identity block; give it a moment so the
            # request is not swallowed by the boot banner.
            import time
            time.sleep(0.5)
            ser.reset_input_buffer()
            ser.write(send.encode("ascii"))
            ser.flush()
        while True:
            raw = ser.readline()
            if not raw:
                continue
            yield raw.decode("utf-8", errors="replace")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Decode base64 JPEG frames from a camera bring-up sketch.")
    p.add_argument("--port", help="serial port, e.g. COM7")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--from-file", dest="from_file",
                   help="decode a saved serial log instead of opening a port")
    p.add_argument("--send", default="",
                   help="character to send first, e.g. 'd' to request one frame")
    p.add_argument("--count", type=int, default=0,
                   help="stop after this many frames (0 = keep watching)")
    p.add_argument("--out", type=Path, default=DEFAULT_OUT,
                   help="output directory (default: vision/dataset/_bringup)")
    p.add_argument("--quiet", action="store_true",
                   help="do not echo the board's other serial output")
    args = p.parse_args(argv)

    if args.from_file:
        with open(args.from_file, "r", encoding="utf-8", errors="replace") as fh:
            n = decode_stream(fh, args.out, args.count, echo=not args.quiet)
        print("decoded %d frame(s) from %s" % (n, args.from_file))
        return 0 if n else 1

    if not args.port:
        p.error("one of --port or --from-file is required")

    print("watching %s at %d baud. Press 'd' in a serial monitor, or pass --send d."
          % (args.port, args.baud))
    try:
        n = decode_stream(serial_lines(args.port, args.baud, args.send, timeout=2.0),
                          args.out, args.count, echo=not args.quiet)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0
    print("decoded %d frame(s)" % n)
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())

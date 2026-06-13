#!/usr/bin/env python3
"""Pull a live screenshot from the EVtivity panel over USB serial.

The firmware (EVtivityPanel.ino) renders the active LVGL screen to a PSRAM buffer
on receiving 'S' and streams it back as base64 RGB565, one row per "D:" line,
framed by "[shot] BEGIN W H FMT" / "[shot] END". Top-layer modals are not included.

Usage:
    python3 screenshot.py [-p PORT] [-o OUT.png]
"""
import argparse
import base64
import sys
import time

import numpy as np
import serial
from PIL import Image

DEFAULT_PORT = "/dev/cu.usbmodem5ABA0007191"


def capture(port: str, out: str, timeout: float) -> None:
    # App Serial is UART0 via the CH343 bridge. Hold DTR and RTS both HIGH before
    # opening: on this board's auto-reset circuit that keeps EN high, so the open
    # does not reboot the ESP32 and the on-screen state is preserved. (Both LOW,
    # pyserial's default-ish, pulses EN and resets the panel.)
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 1000000
    ser.timeout = 0.3
    ser.dtr = True
    ser.rts = True
    ser.open()
    try:
        time.sleep(0.3)
        ser.reset_input_buffer()

        w = h = 0
        b64row = 0           # expected base64 chars per row
        rows: list[str] = []
        receiving = False
        corrupt = False
        last_send = 0.0
        deadline = time.time() + timeout
        while time.time() < deadline:
            # Resend the trigger every 1.5s until the stream starts (covers a boot race).
            if not receiving and time.time() - last_send > 1.5:
                ser.write(b"S")
                ser.flush()
                last_send = time.time()

            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "ignore").strip()
            if line.startswith("[shot] BEGIN"):
                parts = line.split()
                w, h = int(parts[2]), int(parts[3])
                b64row = (w * 2 + 2) // 3 * 4
                rows, corrupt, receiving = [], False, True
            elif receiving and line.startswith("D:"):
                body = line[2:]
                if len(body) != b64row:
                    corrupt = True  # a dropped/split byte; whole frame is retried
                rows.append(body)
            elif line.startswith("[shot] END"):
                if not receiving:
                    continue
                if corrupt or len(rows) != h:
                    print(f"frame corrupt ({len(rows)}/{h} rows), retrying", file=sys.stderr)
                    receiving = False
                    last_send = 0.0  # trigger an immediate resend
                    continue
                break
            elif line.startswith("[shot]"):
                print(line, file=sys.stderr)
                return
        else:
            print(f"timeout after {timeout}s ({len(rows)} rows)", file=sys.stderr)
            sys.exit(1)
        rows = [base64.b64decode(r) for r in rows]
    finally:
        ser.close()

    if not w or len(rows) != h:
        print(f"incomplete: {len(rows)}/{h} rows", file=sys.stderr)
        sys.exit(1)

    # RGB565 little-endian -> RGB888
    px = np.frombuffer(b"".join(rows), dtype="<u2").reshape(h, w)
    r = ((px >> 11) & 0x1F).astype(np.uint16)
    g = ((px >> 5) & 0x3F).astype(np.uint16)
    b = (px & 0x1F).astype(np.uint16)
    rgb = np.empty((h, w, 3), dtype=np.uint8)
    rgb[..., 0] = (r * 255 + 15) // 31
    rgb[..., 1] = (g * 255 + 31) // 63
    rgb[..., 2] = (b * 255 + 15) // 31
    Image.fromarray(rgb, "RGB").save(out)
    print(f"wrote {out} ({w}x{h})")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default=DEFAULT_PORT)
    ap.add_argument("-o", "--out", default="panel.png")
    ap.add_argument("-t", "--timeout", type=float, default=20.0)
    args = ap.parse_args()
    capture(args.port, args.out, args.timeout)


if __name__ == "__main__":
    main()

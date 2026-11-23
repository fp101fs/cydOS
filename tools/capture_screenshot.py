#!/usr/bin/env python3
"""Capture pixel-perfect 240x320 PNG screenshots from cydOS over USB serial."""

import os
import select
import struct
import sys
import termios
import time
import zlib


def write_png(path: str, width: int, height: int, rgb: bytearray) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        start = y * stride
        raw.extend(rgb[start : start + stride])

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def open_serial(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def drain(fd: int, duration: float) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            os.read(fd, 4096)


def read_exact(fd: int, n: int, timeout: float = 15.0) -> bytes:
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while len(buf) < n:
        rem = deadline - time.monotonic()
        if rem <= 0:
            raise TimeoutError(f"Timed out reading {n} bytes (got {len(buf)})")
        r, _, _ = select.select([fd], [], [], min(rem, 0.25))
        if r:
            chunk = os.read(fd, n - len(buf))
            if chunk:
                buf.extend(chunk)
    return bytes(buf)


def snap_one(fd: int, out_path: str) -> None:
    os.write(fd, b"S")

    ring = bytearray()
    scan_deadline = time.monotonic() + 10.0
    while True:
        rem = scan_deadline - time.monotonic()
        if rem <= 0:
            raise TimeoutError(f"Did not receive SCRN header for {out_path}")
        r, _, _ = select.select([fd], [], [], min(rem, 0.2))
        if r:
            b = os.read(fd, 1)
            if b:
                ring.extend(b)
                if len(ring) > 4:
                    del ring[:-4]
                if bytes(ring) == b"SCRN":
                    break

    w, h = struct.unpack("<HH", read_exact(fd, 4))
    rgb = bytearray(w * h * 3)
    strips = 0

    while True:
        tag = read_exact(fd, 4, timeout=10.0)
        if tag == b"DONE":
            break
        if tag != b"STRP":
            raise RuntimeError(f"Unexpected tag: {tag!r}")
        x1, y1, sw, sh = struct.unpack("<HHHH", read_exact(fd, 8))
        pix = read_exact(fd, sw * sh * 2, timeout=10.0)
        idx = 0
        for sy in range(sh):
            row_off = ((y1 + sy) * w + x1) * 3
            for sx in range(sw):
                v = (pix[idx] << 8) | pix[idx + 1]
                idx += 2
                r5 = (v >> 11) & 0x1F
                g6 = (v >> 5) & 0x3F
                b5 = v & 0x1F
                off = row_off + sx * 3
                rgb[off] = (r5 << 3) | (r5 >> 2)
                rgb[off + 1] = (g6 << 2) | (g6 >> 4)
                rgb[off + 2] = (b5 << 3) | (b5 >> 2)
        strips += 1

    write_png(out_path, w, h, rgb)
    print(f"Saved {w}x{h} ({strips} strips) -> {out_path}", flush=True)


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-140"
    # Usage: capture_screenshot.py <port> [cmd:out_path ...]
    # Or:    capture_screenshot.py <port> <out_path>
    specs = sys.argv[2:] if len(sys.argv) > 2 else ["cyd_homescreen.png"]

    fd = open_serial(port)
    try:
        drain(fd, 3.0)
        for spec in specs:
            if ":" in spec:
                cmd, out_path = spec.split(":", 1)
                if cmd:
                    os.write(fd, cmd.encode("ascii"))
                    drain(fd, 1.5)
                snap_one(fd, out_path)
            else:
                snap_one(fd, spec)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()

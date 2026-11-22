#!/usr/bin/env python3
"""Verify Pixel CYD 4 MB Launcher-compatible partition tables and flash headroom."""
import hashlib
import pathlib
import struct
import sys

FLASH_SIZE = 0x400000
SECTOR_SIZE = 0x1000
APP_ALIGN = 0x10000
EXPECTED = [
    ("nvs", 0x01, 0x02, 0x9000, 0x5000),
    ("otadata", 0x01, 0x00, 0xE000, 0x2000),
    ("factory", 0x00, 0x00, 0x10000, 0x200000),
]


def verify_env(root: pathlib.Path, env: str) -> None:
    part_path = root / ".pio" / "build" / env / "partitions.bin"
    fw_path = root / ".pio" / "build" / env / "firmware.bin"
    if not part_path.exists() or not fw_path.exists():
        raise AssertionError(f"{env}: build artifacts missing ({part_path}, {fw_path})")

    data = part_path.read_bytes()
    entries = []
    md5_ok = False
    for off in range(0, len(data), 32):
        rec = data[off : off + 32]
        magic = rec[0:2]
        if magic == b"\xaa\x50":
            ptype, psub, poffs, psize, plabel, pflags = struct.unpack("<BBII16sI", rec[2:32])
            label = plabel.split(b"\x00", 1)[0].decode("ascii", "replace")
            entries.append((label, ptype, psub, poffs, psize))
        elif magic == b"\xeb\xeb":
            md5_ok = hashlib.md5(data[:off]).digest() == rec[16:32]
            break

    assert md5_ok, f"{env}: partition table MD5 mismatch"
    assert entries == EXPECTED, f"{env}: unexpected entries {entries}"

    _, _, _, app_off, app_size = entries[2]
    assert app_off % APP_ALIGN == 0, f"{env}: app offset not 64 KB aligned"
    assert (app_off + app_size) % APP_ALIGN == 0, f"{env}: app end not 64 KB aligned"

    fw_size = fw_path.stat().st_size
    assert fw_size <= app_size, f"{env}: firmware ({fw_size} B) exceeds app slot ({app_size} B)"

    headroom = app_size - fw_size
    dynamic_pool = FLASH_SIZE - (app_off + app_size)
    print(
        f"[OK] {env}: fw={fw_size:,} B / {app_size:,} B "
        f"({fw_size / app_size * 100:.1f}%, {headroom / 1024:.1f} KB headroom), "
        f"dynamic_pool=0x{dynamic_pool:06x} ({dynamic_pool / (1024 * 1024):.4f} MB)"
    )


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    envs = sys.argv[1:] or ["cyd2usb", "cyd"]
    for env in envs:
        verify_env(root, env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

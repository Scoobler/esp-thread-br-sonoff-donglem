#!/usr/bin/env python3
"""Create a non-destructive USB upgrade image for the validated Dongle-M partition layout.

The image is deliberately *relative to flash offset 0x20000*. Never write it
at 0x0: that would corrupt the bootloader and erase NVS!
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

APP0 = 0x20000
APP1 = 0x320000
WEB = 0x620000
EXPECTED = {
    "nvs": (0x9000, 0x6000),
    "otadata": (0xF000, 0x2000),
    "phy_init": (0x11000, 0x1000),
    "ota_0": (APP0, 0x300000),
    "ota_1": (APP1, 0x300000),
    "web_storage": (WEB, 0x80000),
}
MAGIC = 0x50AA


def check_partition_table(path: Path) -> None:
    table = path.read_bytes()
    found = {}
    for pos in range(0, len(table) - 31, 32):
        magic, ptype, subtype, offset, size, name, flags = struct.unpack_from("<HBBII16sI", table, pos)
        if magic in (0xFFFF, 0xEBEB):  # end of entries or ESP-IDF MD5 trailer
            break
        if magic != MAGIC:
            raise ValueError(f"Unexpected partition-table entry at {pos:#x}: {magic:#x}")
        label = name.split(b"\x00", 1)[0].decode("ascii")
        found[label] = (offset, size)
    if found != EXPECTED:
        raise ValueError(f"Partition layout changed: {found!r}; expected {EXPECTED!r}. Refusing unsafe upgrade.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    metadata = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    flash_files = {int(key, 0): build / value for key, value in metadata["flash_files"].items()}
    partition_table = flash_files.get(0x8000)
    if partition_table is None:
        raise ValueError("No partition table at 0x8000 in ESP-IDF flash metadata")
    check_partition_table(partition_table)

    app_file = flash_files.get(APP0)
    web_file = flash_files.get(WEB)
    if app_file is None or web_file is None:
        raise ValueError("Missing application at 0x20000 or web storage at 0x620000")
    app = app_file.read_bytes()
    web = web_file.read_bytes()
    if not 0 < len(app) <= EXPECTED["ota_0"][1] or not 0 < len(web) <= EXPECTED["web_storage"][1]:
        raise ValueError("Firmware exceeds an application or Web UI partition")

    # Two identical application slots: safe for either currently selected OTA slot.
    # Everything from APP0 onwards belongs to app slots or web storage in this layout.
    # No NVS / OTA data / PHY calibration sectors are in this image.
    output = bytearray(b"\xff" * (WEB - APP0 + len(web)))
    for address, payload in ((APP0, app), (APP1, app), (WEB, web)):
        offset = address - APP0
        output[offset:offset + len(payload)] = payload

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    sha = hashlib.sha256(output).hexdigest()
    args.output.with_name(args.output.name + ".sha256").write_text(
        f"{sha}  {args.output.name}\n", encoding="utf-8"
    )
    print(f"Upgrade image: {args.output} ({len(output)} bytes), FLASH AT 0x{APP0:x} ONLY")
    print(f"SHA-256: {sha}")


if __name__ == "__main__":
    main()

"""Safety tests for the settings-preserving USB upgrade image packager."""
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "upgrade", ROOT / "tools" / "release" / "create_upgrade_image.py"
)
UPGRADE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPGRADE)


class UpgradeImageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        entries = []
        for label, (offset, size) in UPGRADE.EXPECTED.items():
            ptype = 0 if label.startswith("ota_") else 1
            subtype = 0 if label == "otadata" else 2
            entries.append(struct.pack("<HBBII16sI", 0x50AA, ptype, subtype,
                                       offset, size, label.encode().ljust(16, b"\x00"), 0))
        # ESP-IDF appends an MD5 checksum descriptor after partition entries.
        self.table = self.build / "partition-table.bin"
        self.table.write_bytes(b"".join(entries) + b"\xeb\xeb" + b"\x00" * 30)
        (self.build / "app.bin").write_bytes(b"\xe9" + b"APPTEST")
        (self.build / "web.bin").write_bytes(b"WEBTEST")
        (self.build / "flasher_args.json").write_text(json.dumps({
            "flash_files": {"0x8000": "partition-table.bin",
                            "0x20000": "app.bin", "0x620000": "web.bin"}
        }))

    def test_known_layout_and_md5_trailer_are_accepted(self):
        UPGRADE.check_partition_table(self.table)

    def test_layout_mismatch_rejected(self):
        data = bytearray(self.table.read_bytes())
        # Corrupt first partition address 0x9000.
        struct.pack_into("<I", data, 4, 0xA000)
        self.table.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Partition layout changed"):
            UPGRADE.check_partition_table(self.table)

    def test_upgrade_image_offsets_and_preservation_boundary(self):
        import subprocess
        import sys
        output = self.root / "upgrade.bin"
        subprocess.run([sys.executable, str(ROOT / "tools/release/create_upgrade_image.py"),
                        str(self.build), "--output", str(output)], check=True)
        data = output.read_bytes()
        self.assertEqual(data[:8], b"\xe9APPTEST")
        self.assertEqual(data[UPGRADE.APP1 - UPGRADE.APP0:
                              UPGRADE.APP1 - UPGRADE.APP0 + 8], b"\xe9APPTEST")
        self.assertEqual(data[UPGRADE.WEB - UPGRADE.APP0:
                              UPGRADE.WEB - UPGRADE.APP0 + 7], b"WEBTEST")
        self.assertEqual(data[0x100:0x110], b"\xff" * 16)
        self.assertTrue(output.with_name("upgrade.bin.sha256").is_file())


if __name__ == "__main__":
    unittest.main()

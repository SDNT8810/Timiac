"""Verify mode selection and fail-closed image validation without serial access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("timiac_run", Path(__file__).resolve().parents[1] / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class MasterFlashTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        hashes = {}
        for role in ("master", "master_release"):
            images = []
            for offset, name in (("0x10000", f"esp_{role}.bin"), ("0x290000", f"{role}_littlefs.bin")):
                content = name.encode()
                (self.root / name).write_bytes(content)
                hashes[name] = hashlib.sha256(content).hexdigest()
                images.append({"offset": offset, "file": name})
            (self.root / f"{role}_flash.json").write_text(json.dumps({"chip": "esp32", "images": images}))
        (self.root / "SHA256.json").write_text(json.dumps(hashes))
        for name, value in (("RELEASE_DIR", self.root), ("CHECKSUMS_FILE", self.root / "SHA256.json")):
            patcher = patch.object(runner, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    @patch.object(runner, "ensure_esptool")
    @patch.object(runner.subprocess, "run")
    def test_modes_select_matching_firmware_and_filesystem(self, execute, _ensure):
        for release, role in ((False, "master"), (True, "master_release")):
            runner.flash_master("COM5", 460800, release)
            args = execute.call_args.args[0]
            self.assertIn(str(self.root / f"esp_{role}.bin"), args)
            self.assertIn(str(self.root / f"{role}_littlefs.bin"), args)
            self.assertIn("COM5", args)
            self.assertIn("0x290000", args)

    @patch.object(runner.subprocess, "run")
    def test_corrupt_or_missing_release_never_flashes(self, execute):
        image = self.root / "master_release_littlefs.bin"
        image.write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            runner.flash_master("COM5", 460800, True)
        image.unlink()
        with self.assertRaises(FileNotFoundError):
            runner.flash_master("COM5", 460800, True)
        execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()

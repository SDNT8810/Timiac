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

    def test_full_release_rejects_checksum_valid_old_machine_image(self):
        from tools.machine_config import load
        _, identity = load()
        (self.root / "machine.json").write_text(json.dumps({"identity": identity}))
        hashes = {}
        names = {"firmware.bin", "esp_board1.bin", "esp_board2.bin"}
        for role in ("master", "master_release"):
            entries = [("0x1000", f"{role}_bootloader.bin"), ("0x8000", f"{role}_partitions.bin"),
                       ("0xe000", f"{role}_boot_app0.bin"), ("0x10000", f"esp_{role}.bin"),
                       ("0x290000", f"{role}_littlefs.bin")]
            names.update(name for _, name in entries)
            (self.root / f"{role}_flash.json").write_text(json.dumps({
                "chip": "esp32", "images": [{"offset": offset, "file": name} for offset, name in entries]}))
        for name in names:
            data = f"test {name} {identity}".encode()
            (self.root / name).write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        (self.root / "SHA256.json").write_text(json.dumps(hashes))
        self.assertEqual(runner.verify_release(), identity)
        stale = b"old bridge with a valid checksum"
        (self.root / "esp_board2.bin").write_bytes(stale)
        hashes["esp_board2.bin"] = hashlib.sha256(stale).hexdigest()
        (self.root / "SHA256.json").write_text(json.dumps(hashes))
        with self.assertRaisesRegex(ValueError, "does not contain"):
            runner.verify_release()

    def test_card_contains_only_firmware_and_needs_no_gcode_directory(self):
        with patch.object(runner, "validate_sd_root", return_value=self.root), \
             patch.object(runner, "verified_release_file", return_value=self.root / "source.bin") as selected, \
             patch.object(runner, "verified_copy") as copied:
            runner.prepare_octopus_card(self.root, False)
            selected.assert_called_once_with("firmware.bin")
            copied.assert_called_once_with(self.root / "source.bin", self.root / "firmware.bin")

    def test_all_eight_commands_route_to_the_correct_image(self):
        for mode in ("dev", "release"):
            for role in ("esp1", "esp2", "master", "octopus"):
                with self.subTest(mode=mode, role=role), \
                     patch.object(runner.sys, "argv", ["run.py", f"{role}_{mode}"] + (["--sd", "I:/"] if role == "octopus" else ["--port", "COM5"])), \
                     patch.object(runner, "verify_release"), \
                     patch.object(runner, "flash_master") as master, \
                     patch.object(runner, "flash_esp8266") as bridge, \
                     patch.object(runner, "prepare_octopus_card") as octopus:
                    self.assertEqual(runner.main(), 0)
                    if role == "master":
                        master.assert_called_once_with("COM5", 460800, release=mode == "release")
                    elif role == "octopus":
                        octopus.assert_called_once_with(Path("I:/"), False)
                    else:
                        bridge.assert_called_once_with(f"esp_board{role[-1]}.bin", "COM5", 460800)

    def test_sensorless_option_is_rejected(self):
        with patch.object(runner.sys, "argv", ["run.py", "octopus_dev", "--sensorless"]), patch.object(runner.sys, "stderr"):
            with self.assertRaises(SystemExit) as error:
                runner.parse_args()
            self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()

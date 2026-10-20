import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build


class BuildTargetsTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.release = self.root / 'firmware/two-motor'
        self.release.mkdir(parents=True)
        (self.release / 'esp_master.bin').write_bytes(b'existing development master')
        self.old_dev_hash = hashlib.sha256((self.release / 'esp_master.bin').read_bytes()).hexdigest()
        (self.release / 'SHA256.json').write_text(json.dumps({'esp_master.bin': self.old_dev_hash}))
        self.marlin = self.root / 'marlin'
        image = self.marlin / '.pio/build/STM32F446ZE_btt/firmware.bin'
        image.parent.mkdir(parents=True); image.write_bytes(b'new Octopus')
        for role in ('master', 'master_release', 'board1', 'board2'):
            folder = self.root / 'ESP_Timiac/.pio/build' / ('timiac_' + role)
            folder.mkdir(parents=True)
            (folder / 'firmware.bin').write_bytes(('new ' + role).encode())
            if role.startswith('master'):
                images = [{'offset': '0x10000', 'file': 'esp_master.bin'}]
                for offset, suffix in (('0x1000', 'bootloader'), ('0x8000', 'partitions'), ('0xe000', 'boot_app0')):
                    name = 'master_' + suffix + '.bin'
                    (folder / name).write_bytes(suffix.encode())
                    images.append({'offset': offset, 'file': name})
                (folder / 'master_flash.json').write_text(json.dumps({'images': images}))
                (folder / 'littlefs.bin').write_bytes(('dashboard ' + role).encode())

    def run_target(self, target):
        with patch.object(build, 'ROOT', self.root), patch.object(build, 'generate'), \
             patch('tools.generate_presets.generate'), \
             patch.object(build, 'prepare_marlin', return_value=self.marlin) as marlin, \
             patch.object(build.subprocess, 'run') as run, patch('sys.argv', ['build.py', target]):
            build.main()
        return marlin, [call.args[0] for call in run.call_args_list]

    def test_cloud_builds_every_install_component_without_rebuilding_development_master(self):
        marlin, commands = self.run_target('cloud')
        marlin.assert_called_once()
        esp = next(command for command in commands if str(self.root / 'ESP_Timiac') in command and '-t' not in command)
        environments = [esp[n + 1] for n, value in enumerate(esp) if value == '-e']
        self.assertEqual(environments, ['timiac_master_release', 'timiac_board1', 'timiac_board2'])
        for name in ('firmware.bin', 'esp_master_release.bin', 'esp_board1.bin', 'esp_board2.bin', 'master_release_littlefs.bin'):
            self.assertTrue((self.release / name).is_file(), name)
        checksums = json.loads((self.release / 'SHA256.json').read_text())
        self.assertEqual(checksums['esp_master.bin'], self.old_dev_hash)
        self.assertEqual((self.release / 'esp_master.bin').read_bytes(), b'existing development master')
        for name, digest in checksums.items():
            self.assertEqual(hashlib.sha256((self.release / name).read_bytes()).hexdigest(), digest)
        self.assertFalse((self.release / 'master_littlefs.bin').exists())

    def test_all_still_builds_and_exports_the_development_master(self):
        marlin, commands = self.run_target('all')
        marlin.assert_called_once()
        esp = next(command for command in commands if str(self.root / 'ESP_Timiac') in command and '-t' not in command)
        self.assertNotIn('-e', esp)  # Existing PlatformIO default environments.
        self.assertEqual((self.release / 'esp_master.bin').read_bytes(), b'new master')
        self.assertTrue((self.release / 'master_littlefs.bin').is_file())

    def test_individual_development_target_does_not_rebuild_other_boards(self):
        marlin, commands = self.run_target('master_dev')
        marlin.assert_not_called()
        self.assertTrue(all(command[command.index('-e') + 1] == 'timiac_master' for command in commands))
        self.assertFalse((self.release / 'firmware.bin').exists())
        self.assertFalse((self.release / 'esp_board1.bin').exists())


if __name__ == '__main__':
    unittest.main()

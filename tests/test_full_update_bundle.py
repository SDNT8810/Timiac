import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import publish_full_update as publisher


class FullUpdateBundleTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.output = Path(directory.name)
        for target, kind, name in publisher.IMAGES:
            role = 'octopus' if kind == 'octopus' else 'master-filesystem' if kind == 'filesystem' else target
            (self.output / name).write_bytes(('TIMIAC_OTA_ROLE:' + role + ' TIMIAC2-123456789abc 2026-10-20.1').encode())
        self.rehash()
        self.patch_output = patch.object(publisher, 'OUTPUT', self.output)
        self.patch_output.start(); self.addCleanup(self.patch_output.stop)

    def rehash(self):
        hashes = {name: hashlib.sha256((self.output / name).read_bytes()).hexdigest() for _, _, name in publisher.IMAGES}
        (self.output / 'SHA256.json').write_text(json.dumps(hashes))

    def test_check_only_validates_six_components_without_upload_or_device_request(self):
        with patch('sys.argv', ['publish_full_update.py', '--check-only']), \
             patch.object(publisher, 'call') as call, contextlib.redirect_stdout(io.StringIO()) as output:
            publisher.main()
        call.assert_not_called()
        self.assertIn('Verified complete bundle: 6 components', output.getvalue())

    def test_changed_bytes_cannot_pass_the_cached_checksum(self):
        (self.output / 'esp_master_release.bin').write_bytes(b'changed after compilation')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            publisher.checked_images()

    def test_even_valid_hashes_cannot_hide_wrong_role_or_mixed_release(self):
        path = self.output / 'esp_board1.bin'
        original = path.read_bytes()
        path.write_bytes(original.replace(b'ROLE:esp1', b'ROLE:esp2')); self.rehash()
        with self.assertRaisesRegex(ValueError, 'Wrong firmware role'):
            publisher.checked_images()
        path.write_bytes(original.replace(b'2026-10-20.1', b'2026-10-19.4')); self.rehash()
        with self.assertRaisesRegex(ValueError, 'same release'):
            publisher.checked_images()

    def test_mixed_hardware_and_missing_files_are_rejected_before_any_upload(self):
        path = self.output / 'esp_board2.bin'
        path.write_bytes(path.read_bytes().replace(b'123456789abc', b'abcdef123456')); self.rehash()
        with self.assertRaisesRegex(ValueError, 'same release and hardware identity'):
            publisher.checked_images()
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            publisher.checked_images()

    def test_publishing_checks_entire_bundle_before_the_first_network_call(self):
        (self.output / 'master_release_littlefs.bin').write_bytes(b'invalid final component')
        args = ['publish_full_update.py', '--gateway', 'https://control.timiac.it', '--update-id', 'pending', '--source-sha', 'a' * 40]
        with patch('sys.argv', args), patch.object(publisher, 'call') as call:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                publisher.main()
        call.assert_not_called()


    def test_single_component_publishes_only_the_exact_selected_target_and_kind(self):
        for target, kind, _ in publisher.IMAGES:
            with self.subTest(target=target, kind=kind):
                args = ['publish_full_update.py', '--gateway', 'https://control.timiac.it', '--update-id', 'pending',
                        '--source-sha', 'a' * 40, '--target', target, '--kind', kind]
                def reply(args, endpoint, data, headers=None):
                    return {'sha256': hashlib.sha256(data).hexdigest()} if endpoint == 'images' else {'state': 'installing'}
                with patch('sys.argv', args), patch.object(publisher, 'call', side_effect=reply) as call, contextlib.redirect_stdout(io.StringIO()):
                    publisher.main()
                self.assertEqual(call.call_count, 2)
                image = call.call_args_list[0]
                self.assertEqual(image.args[1], 'images')
                self.assertEqual(image.args[3], {'X-Timiac-Target': target, 'X-Timiac-Kind': kind})
                self.assertEqual(call.call_args_list[1].args[1], 'complete')

    def test_component_requests_reject_invalid_or_partial_selection_before_network(self):
        for selection in (['--target', 'esp1'], ['--kind', 'app'], ['--target', 'master', '--kind', 'octopus']):
            with patch('sys.argv', ['publish_full_update.py', *selection]), patch.object(publisher, 'call') as call, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit): publisher.main()
                call.assert_not_called()

    def test_component_still_checks_complete_cached_bundle_before_uploading(self):
        (self.output / 'esp_board2.bin').write_bytes(b'corrupted unselected cached image')
        args = ['publish_full_update.py', '--gateway', 'https://control.timiac.it', '--update-id', 'pending',
                '--source-sha', 'a' * 40, '--target', 'esp1', '--kind', 'app']
        with patch('sys.argv', args), patch.object(publisher, 'call') as call:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'): publisher.main()
        call.assert_not_called()


if __name__ == '__main__':
    unittest.main()

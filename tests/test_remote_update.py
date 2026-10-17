import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import remote_update


class RemoteUpdateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'firmware/two-motor').mkdir(parents=True)
        (self.root / 'firmware/two-motor/esp_board2.bin').write_bytes(b'test image')
        self.gateway = remote_update.Gateway('https://control.timiac.it')

    def test_plain_http_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'HTTPS'):
            remote_update.Gateway('http://control.timiac.it')

    def test_requests_identify_the_updater_to_cloudflare(self):
        with patch.object(self.gateway.client, 'open', return_value=io.BytesIO(b'{"ok":true}')) as opened:
            self.assertEqual(self.gateway.call('/healthz'), {'ok': True})
        request = opened.call_args.args[0]
        self.assertEqual(request.get_header('User-agent'), 'Timiac-Updater/1.0')
        self.assertEqual(request.get_header('Origin'), 'https://control.timiac.it')

    def test_correct_board_is_uploaded_and_installed_once(self):
        replies = [{'id': 'image'}, {'id': 'job'}, {'online': True, 'update': {'id': 'job', 'state': 'complete'}}]
        with patch.object(remote_update, 'ROOT', self.root), patch.object(self.gateway, 'call', side_effect=replies) as call, contextlib.redirect_stdout(io.StringIO()):
            self.gateway.install('clock-01', 'esp2')
        self.assertEqual(call.call_args_list[0].args, ('/api/devices/clock-01/firmware', b'test image', {'X-Timiac-Target': 'esp2', 'X-Timiac-Kind': 'app'}))
        self.assertEqual(call.call_args_list[1].args, ('/api/devices/clock-01/updates', {'imageId': 'image'}))
        self.assertEqual(call.call_count, 3)

    def test_a_failed_or_expired_job_is_not_automatically_retried(self):
        for state in ('failed', 'expired', 'cancelled'):
            replies = [{'id': 'image'}, {'id': 'job'}, {'online': True, 'update': {'id': 'job', 'state': state, 'error': 'board unavailable'}}]
            with patch.object(remote_update, 'ROOT', self.root), patch.object(self.gateway, 'call', side_effect=replies) as call, contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(RuntimeError, 'board unavailable'):
                    self.gateway.install('clock-01', 'esp2')
            self.assertEqual(call.call_count, 3)

    def test_all_updates_master_then_bridges_then_octopuses_and_dashboard(self):
        with patch('sys.argv', ['remote_update.py', '--target', 'all']), patch.object(remote_update, 'verify_release'), patch.object(remote_update.getpass, 'getpass', return_value='private'), patch.object(remote_update, 'Gateway') as factory, contextlib.redirect_stdout(io.StringIO()):
            factory.return_value.call.side_effect = [{'username': 'Andrea'}, {'deviceId': 'clock-01'}, {}]
            remote_update.main()
            self.assertEqual([call.args for call in factory.return_value.install.call_args_list], [('clock-01', target) for target in ['master', 'esp1', 'esp2', 'octopus1', 'octopus2', 'dashboard']])

    def test_corrupt_package_is_rejected_before_login_or_any_upload(self):
        with patch('sys.argv', ['remote_update.py', '--target', 'all']), patch.object(remote_update, 'verify_release', side_effect=ValueError('Checksum mismatch')), patch.object(remote_update, 'Gateway') as factory:
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                remote_update.main()
            factory.assert_not_called()

    def test_a_lost_status_connection_retries_only_the_read(self):
        replies = [{'id': 'image'}, {'id': 'job'}, OSError('temporary connection loss'),
                   {'online': True, 'update': {'id': 'job', 'state': 'complete'}}]
        with patch.object(remote_update, 'ROOT', self.root), patch.object(self.gateway, 'call', side_effect=replies) as call, patch.object(remote_update.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            self.gateway.install('clock-01', 'esp2')
        self.assertEqual([entry.args[0] for entry in call.call_args_list], [
            '/api/devices/clock-01/firmware', '/api/devices/clock-01/updates',
            '/api/devices/clock-01/status', '/api/devices/clock-01/status'])


if __name__ == '__main__':
    unittest.main()

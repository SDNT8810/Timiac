import contextlib
import hashlib
import hmac
import http.client
import http.server
import io
import json
import threading
import unittest
import urllib.error
import urllib.request
from unittest.mock import patch
import local_update
import run


class LocalUpdateTests(unittest.TestCase):
    def prepared(self):
        data = b'verified firmware'
        return ('esp1', data, dict(target='esp1', kind='app', size=len(data),
            sha256=hashlib.sha256(data).hexdigest(), revision='2026-10-19.2',
            machine='TIMIAC2-123456789abc', crc32=0))

    def test_manifest_canonicalization_and_every_signed_field(self):
        manifest = dict(session='boot', nonce='nonce', id='job', target='esp1', kind='app',
            size=1000, sha256='a'*64, crc32=123, revision='2026-10-19.2',
            machine='TIMIAC2-123456789abc', port=12000, imageId='image')
        canonical = 'timiac-local-v1|boot|nonce|job|esp1|app|1000|' + 'a'*64 + '|123|2026-10-19.2|TIMIAC2-123456789abc|12000|image'
        signed = local_update.sign_manifest(manifest, b'pairing')
        self.assertEqual(signed, hmac.new(b'pairing', canonical.encode(), hashlib.sha256).hexdigest())
        for name, value in manifest.items():
            changed = {**manifest, name: value + 1 if isinstance(value, int) else value + 'x'}
            self.assertNotEqual(local_update.sign_manifest(changed, b'pairing'), signed, name)

    def test_only_private_master_addresses_are_accepted(self):
        for host in ('8.8.8.8', '127.0.0.1', '0.0.0.0', '224.1.1.1', 'clock.example'):
            with self.assertRaises(ValueError):
                local_update.LocalGateway(host)
        self.assertEqual(local_update.LocalGateway('192.168.4.1').host, '192.168.4.1')

    def test_windows_connection_abort_retries_get_and_recovers(self):
        gateway = local_update.LocalGateway()
        response = io.BytesIO(b'{"localSupported":true,"nonce":"nonce","software":"2026-10-19.4"}')
        aborted = urllib.error.URLError(ConnectionAbortedError(10053, 'connection aborted by host software'))
        with patch.object(gateway.client, 'open', side_effect=[aborted, response]) as opened, \
             patch.object(local_update.time, 'sleep') as sleep, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(gateway.info()['software'], '2026-10-19.4')
        self.assertEqual(opened.call_count, 2)
        self.assertTrue(all(call.args[0].get_method() == 'GET' for call in opened.call_args_list))
        self.assertTrue(all(call.kwargs['timeout'] == 5 for call in opened.call_args_list))
        sleep.assert_called_once_with(0.5)

    def test_persistent_connection_abort_is_bounded_and_explains_browser_python_check(self):
        gateway = local_update.LocalGateway()
        error = urllib.error.URLError(ConnectionAbortedError(10053, 'connection aborted'))
        with patch.object(gateway.client, 'open', side_effect=error) as opened, \
             patch.object(local_update.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, '10053.*browser.*Python executable') as failure:
                gateway.info()
        self.assertEqual(opened.call_count, 3)
        self.assertEqual(failure.exception.status, 503)
        self.assertIn(local_update.sys.executable, str(failure.exception))

    def test_incomplete_reads_and_busy_get_are_retried_but_refusal_is_not(self):
        gateway = local_update.LocalGateway()
        busy = urllib.error.HTTPError(gateway.url, 503, 'busy', {}, io.BytesIO(b'{"error":"Controller busy"}'))
        response = io.BytesIO(b'{"localSupported":true,"nonce":"nonce"}')
        with patch.object(gateway.client, 'open', side_effect=[http.client.RemoteDisconnected(), busy, response]) as opened, \
             patch.object(local_update.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(gateway.info()['localSupported'])
        self.assertEqual(opened.call_count, 3)
        refused = urllib.error.HTTPError(gateway.url, 403, 'forbidden', {}, io.BytesIO(b'{"error":"Request refused"}'))
        with patch.object(gateway.client, 'open', side_effect=refused) as opened:
            with self.assertRaisesRegex(RuntimeError, 'Request refused') as failure:
                gateway.info()
        self.assertEqual(opened.call_count, 1)
        self.assertEqual(failure.exception.status, 403)

    def test_transport_and_busy_post_failures_never_replay_a_command(self):
        gateway = local_update.LocalGateway()
        for error in (urllib.error.URLError(ConnectionAbortedError(10053, 'connection aborted')),
                      urllib.error.HTTPError(gateway.url, 503, 'busy', {}, io.BytesIO(b'{"error":"Controller busy"}'))):
            with patch.object(gateway.client, 'open', side_effect=error) as opened, patch.object(local_update.time, 'sleep') as sleep:
                with self.assertRaises(RuntimeError):
                    gateway.call('/api/firmware/local', {'id': 'already submitted'})
            self.assertEqual(opened.call_count, 1)
            self.assertEqual(opened.call_args.args[0].get_method(), 'POST')
            sleep.assert_not_called()

    def test_connection_check_sends_only_one_get_without_loading_images_or_stopping(self):
        response = io.BytesIO(b'{"localSupported":true,"nonce":"nonce","software":"2026-10-19.4"}')
        with patch('sys.argv', ['local_update.py', '--check']), \
             patch.object(urllib.request.OpenerDirector, 'open', return_value=response) as opened, \
             patch.object(local_update, 'prepare_images') as images, patch.object(local_update, 'ImageServer') as server, \
             patch.object(local_update.LocalGateway, 'pause') as pause, contextlib.redirect_stdout(io.StringIO()):
            local_update.main()
        opened.assert_called_once()
        self.assertEqual(opened.call_args.args[0].get_method(), 'GET')
        self.assertEqual(opened.call_args.args[0].full_url, 'http://192.168.4.1/api/firmware/local')
        images.assert_not_called(); server.assert_not_called(); pause.assert_not_called()

    def test_real_socket_closed_without_reply_recovers_before_any_post(self):
        methods = []
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                methods.append(self.command)
                if len(methods) == 1:
                    self.close_connection = True
                    return
                data = b'{"localSupported":true,"nonce":"nonce"}'
                self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers()
                self.wfile.write(data)
            def log_message(self, *_): pass
        with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            gateway = local_update.LocalGateway()
            gateway.url = 'http://127.0.0.1:' + str(server.server_port)
            try:
                with patch.object(local_update.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
                    self.assertTrue(gateway.info()['localSupported'])
            finally:
                server.shutdown(); thread.join()
        self.assertEqual(methods, ['GET', 'GET'])

    def test_real_local_server_serves_exact_image_and_rejects_unrelated_paths_or_clients(self):
        with local_update.ImageServer(('127.0.0.1', 0), '127.0.0.1') as server:
            server.images['/firmware/token'] = b'\x00firmware\xff'
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            url = 'http://127.0.0.1:' + str(server.server_port)
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            try:
                with opener.open(url + '/firmware/token') as response:
                    self.assertEqual(response.read(), b'\x00firmware\xff')
                    self.assertEqual(response.headers['Content-Length'], '10')
                try: opener.open(url + '/../../run.py')
                except urllib.error.HTTPError as error: error.close()
                else: self.fail('Unrelated file path was served')
                server.master_ip = '192.168.4.1'
                try: opener.open(url + '/firmware/token')
                except urllib.error.HTTPError as error: error.close()
                else: self.fail('Non-master client was served')
            finally:
                server.shutdown(); thread.join()

    def test_correct_receipt_completes_after_one_start_without_cloud_login(self):
        prepared = self.prepared()
        gateway = local_update.LocalGateway()
        info = dict(localSupported=True, session='boot', nonce='nonce', machine=prepared[2]['machine'])
        class Server:
            images, server_port = {}, 12000
        server = Server()
        starts = []
        def call(path, body=None):
            if body is None:
                if not starts: return info
                return {**info, 'ota': dict(id=starts[0]['id'], sha256=prepared[2]['sha256'], state='complete')}
            self.assertEqual(path, '/api/firmware/local')
            self.assertEqual(server.images['/firmware/' + body['imageId']], prepared[1])
            self.assertEqual(body['auth'], local_update.sign_manifest(body, b'pairing'))
            starts.append(body); return {}
        with patch.object(gateway, 'call', side_effect=call), contextlib.redirect_stdout(io.StringIO()):
            gateway.install(prepared, server, b'pairing')
        self.assertEqual(len(starts), 1)
        self.assertEqual(server.images, {})

    def test_lost_start_reply_is_reconciled_without_replaying_the_flash(self):
        prepared = self.prepared(); gateway = local_update.LocalGateway()
        initial = dict(localSupported=True, session='boot', nonce='nonce', machine=prepared[2]['machine'])
        class Server:
            images, server_port = {}, 12000
        saved = []
        def call(path, body=None):
            if body is not None: saved.append(body); raise OSError('reply lost')
            if not saved: return initial
            return {**initial, 'ota': dict(id=saved[0]['id'], sha256=prepared[2]['sha256'], state='complete')}
        with patch.object(gateway, 'call', side_effect=call), contextlib.redirect_stdout(io.StringIO()):
            gateway.install(prepared, Server(), b'pairing')
        self.assertEqual(len(saved), 1)

    def test_reboot_without_receipt_is_not_success_or_a_reason_to_resubmit(self):
        prepared = self.prepared(); gateway = local_update.LocalGateway()
        initial = dict(localSupported=True, session='boot', nonce='nonce', machine=prepared[2]['machine'])
        class Server:
            images, server_port = {}, 12000
        with patch.object(gateway, 'call', side_effect=[initial, {}, {**initial, 'session': 'rebooted'}]) as call, contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'without confirming'):
                gateway.install(prepared, Server(), b'pairing')
        self.assertEqual(len([c for c in call.call_args_list if len(c.args) == 2]), 1)

    def test_corrupt_bundle_is_rejected_before_stop_or_any_network_request(self):
        with patch.object(local_update, 'verify_release', side_effect=ValueError('Checksum mismatch')), patch.object(local_update, 'LocalGateway') as factory:
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                local_update.install_targets('192.168.4.1', ['master', 'esp1'])
            factory.assert_not_called()

    def test_a_master_downgrade_cannot_remove_local_recovery(self):
        data = b'\xe9TIMIAC_OTA_ROLE:master\x00TIMIAC2-123456789abc\x002026-10-19.2\x00'
        with patch.object(local_update, 'verify_release'), patch.object(local_update, 'verified_release_file') as image:
            image.return_value.read_bytes.return_value = data
            with self.assertRaisesRegex(ValueError, 'removes local-update recovery'):
                local_update.prepare_images(['master'])
            image.return_value.read_bytes.return_value = data + b'TIMIAC_LOCAL_OTA_V1\x00'
            self.assertEqual(local_update.prepare_images(['master'])[0][2]['target'], 'master')

    def test_run_host_routes_only_to_local_and_usb_remains_unchanged(self):
        with patch('sys.argv', ['run.py', 'master_release', '--host', '192.168.4.1']), patch.object(run, 'verify_release'), patch.object(local_update, 'install_targets') as install, patch.object(run, 'flash_master') as usb:
            self.assertEqual(run.main(), 0)
            install.assert_called_once_with('192.168.4.1', ['master', 'dashboard'], 'release')
            usb.assert_not_called()
        with patch('sys.argv', ['run.py', 'master_release', '--port', 'COM5']), patch.object(run, 'verify_release'), patch.object(local_update, 'install_targets') as install, patch.object(run, 'flash_master') as usb:
            self.assertEqual(run.main(), 0)
            usb.assert_called_once_with('COM5', 460800, release=True)
            install.assert_not_called()

    def test_full_sequence_stops_on_first_failure_and_always_closes_the_server(self):
        steps = ['master', 'esp1', 'esp2', 'octopus1', 'octopus2', 'dashboard']
        prepared = [(step, b'image', {'machine': 'TIMIAC2-123456789abc'}) for step in steps]
        for fail in (False, True):
            with patch.object(local_update, 'prepare_images', return_value=prepared), patch.object(local_update, 'pairing_key', return_value=b'key'), \
                 patch.object(local_update, 'LocalGateway') as factory, patch.object(local_update, 'ImageServer') as servers, \
                 patch.object(local_update, 'route_address', return_value='192.168.4.2'), patch.object(local_update.threading, 'Thread') as threads, contextlib.redirect_stdout(io.StringIO()):
                gateway = factory.return_value; gateway.host = '192.168.4.1'
                gateway.info.return_value = {'machine': 'TIMIAC2-123456789abc', 'ota': {'active': False}}
                server = servers.return_value.__enter__.return_value
                if fail:
                    gateway.install.side_effect = [None, RuntimeError('ESP1 failed')]
                    with self.assertRaisesRegex(RuntimeError, 'ESP1 failed'):
                        local_update.install_targets('192.168.4.1', steps)
                else:
                    local_update.install_targets('192.168.4.1', steps)
                self.assertEqual([call.args[0][0] for call in gateway.install.call_args_list], steps[:2] if fail else steps)
                gateway.pause.assert_called_once()
                server.shutdown.assert_called_once()
                threads.return_value.join.assert_called_once()


if __name__ == '__main__': unittest.main()

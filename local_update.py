"""Update Timiac over its hotspot or router LAN, without a cloud connection."""
from __future__ import annotations
import argparse
import hashlib
import hmac
import http.server
import ipaddress
import json
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
import zlib
from run import ROOT, MASTER_CLOUD_CONFIG, MASTER_LOCAL_CONFIG, verify_release, verified_release_file

IMAGES = {
    'master': ('master', 'app', 'esp_master_release.bin'),
    'esp1': ('esp1', 'app', 'esp_board1.bin'),
    'esp2': ('esp2', 'app', 'esp_board2.bin'),
    'octopus1': ('octopus1', 'octopus', 'firmware.bin'),
    'octopus2': ('octopus2', 'octopus', 'firmware.bin'),
    'dashboard': ('master', 'filesystem', 'master_release_littlefs.bin'),
}

def pairing_key():
    for path in (MASTER_LOCAL_CONFIG, MASTER_CLOUD_CONFIG):
        if path.is_file():
            match = re.search(r'^\s*#define\s+TIMIAC_CLOUD_DEVICE_KEY\s+"([^"\r\n]+)"', path.read_text(encoding='utf-8'), re.M)
            if match:
                return match[1].encode()
    raise ValueError('Local pairing key is missing from the firmware configuration')

def prepare_images(steps, mode='release'):
    # Validate the entire selection before any network request, Stop or flash.
    verify_release()
    result = []
    for step in steps:
        target, kind, filename = IMAGES[step]
        if mode == 'dev' and step in ('master', 'dashboard'):
            filename = 'esp_master.bin' if step == 'master' else 'master_littlefs.bin'
        data = verified_release_file(filename).read_bytes()
        revision = re.search(rb'20\d\d-\d\d-\d\d\.\d{1,3}(?!\d)', data)
        machine = re.search(rb'TIMIAC2-[0-9a-f]{12}', data)
        role = 'master-filesystem' if kind == 'filesystem' else 'octopus' if kind == 'octopus' else target
        if not revision or not machine or ('TIMIAC_OTA_ROLE:' + role).encode() not in data:
            raise ValueError('Wrong role or missing identity/revision in ' + filename)
        if step == 'master' and b'TIMIAC_LOCAL_OTA_V1' not in data:
            raise ValueError('This master image removes local-update recovery; choose a release with local-update support')
        result.append((step, data, dict(target=target, kind=kind, size=len(data),
            revision=revision[0].decode(), machine=machine[0].decode(), sha256=hashlib.sha256(data).hexdigest(),
            crc32=zlib.crc32(data) if kind == 'octopus' else 0)))
    return result

def sign_manifest(manifest, key):
    names = ('session', 'nonce', 'id', 'target', 'kind', 'size', 'sha256', 'crc32', 'revision', 'machine', 'port', 'imageId')
    message = 'timiac-local-v1|' + '|'.join(str(manifest[name]) for name in names)
    return hmac.new(key, message.encode(), hashlib.sha256).hexdigest()

class ImageServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, master_ip):
        self.images, self.master_ip = {}, master_ip
        super().__init__(address, ImageHandler)

class ImageHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data = self.server.images.get(self.path)
        if self.client_address[0] != self.server.master_ip or data is None:
            self.send_error(404); return
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.connection.settimeout(30)
            self.wfile.write(data)
        except (OSError, TimeoutError):
            pass  # Device checks length/hash; never replay an interrupted flash.

    def log_message(self, *_):
        pass

def route_address(master):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((master, 80))
        return route.getsockname()[0]

class LocalGateway:
    def __init__(self, host='192.168.4.1'):
        address = ipaddress.IPv4Address(host)
        if not address.is_private or address.is_loopback or address.is_unspecified or address.is_multicast:
            raise ValueError('Use the master hotspot/LAN IPv4 address, for example 192.168.4.1')
        self.host, self.url = str(address), 'http://' + str(address)
        self.client = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def call(self, path, body=None):
        request = urllib.request.Request(self.url + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Content-Type': 'application/json', 'X-Timiac': '1', 'User-Agent': 'Timiac-Local-Updater/1.0'})
        try:
            with self.client.open(request, timeout=15) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            try:
                message = json.load(error).get('error', str(error))
            except (ValueError, AttributeError):
                message = str(error)
            if error.code == 404 and path == '/api/firmware/local':
                message = 'Install local-update support on the master by USB once before using hotspot updates'
            failure = RuntimeError(message); failure.status = error.code
            raise failure from None

    def info(self):
        info = self.call('/api/firmware/local')
        if not info.get('localSupported') or not info.get('nonce'):
            raise ValueError('Install local-update support on the master by USB once')
        return info

    def pause(self):
        self.call('/api/stop', {})
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            state = self.call('/api/status')
            if not state.get('stopPending') and not state.get('wifi', {}).get('connecting') and state.get('operation', {}).get('state') not in ('preparing', 'running') and state.get('program', {}).get('state') not in ('running', 'paused', 'uploading'):
                return
            time.sleep(0.5)
        raise RuntimeError('The bridges have not acknowledged Stop; no firmware was installed')

    def install(self, prepared, server, key, timeout=1260):
        step, data, metadata = prepared
        info = self.info()
        if metadata['machine'] != info.get('machine'):
            raise ValueError('Hardware identity changed; install a matched USB/SD package')
        manifest = dict(metadata, id=str(uuid.uuid4()), imageId=str(uuid.uuid4()),
            session=info['session'], nonce=info['nonce'], port=server.server_port)
        manifest['auth'] = sign_manifest(manifest, key)
        path = '/firmware/' + manifest['imageId']
        server.images[path] = data
        print(f"{step}: starting local Wi-Fi update. Keep the PC connected and powered.", flush=True)
        try:
            # One start request only. A lost reply is reconciled by exact receipt.
            try:
                self.call('/api/firmware/local', manifest)
            except (OSError, RuntimeError) as error:
                if isinstance(error, RuntimeError) and getattr(error, 'status', 0) < 500:
                    raise
                print('Start reply unavailable; checking the existing job without resubmitting.', flush=True)
            started = time.monotonic()
            deadline, previous, accepted = started + timeout, '', False
            while time.monotonic() < deadline:
                try:
                    current = self.info()
                except (OSError, RuntimeError):
                    time.sleep(2); continue
                ota = current.get('ota') or {}
                if ota.get('id') == manifest['id'] and ota.get('sha256') == metadata['sha256']:
                    accepted = True
                    if ota.get('state') == 'complete':
                        if step == 'master' and current.get('software') != metadata['revision']:
                            raise RuntimeError('Master restarted with an unexpected revision')
                        print(step + ': complete. Clock remains paused.', flush=True)
                        return
                    if ota.get('state') == 'failed':
                        raise RuntimeError(step + ': ' + (ota.get('error') or 'Firmware update failed'))
                    label = str(ota.get('phase') or ota.get('state') or 'starting')
                    if ota.get('size'):
                        label += ' ' + str(min(100, round(100 * ota.get('done', 0) / ota['size']))) + '%'
                else:
                    # A reboot without our receipt is never treated as success.
                    if current.get('session') != info['session']:
                        raise RuntimeError('Controller restarted without confirming this update; inspect its diagnostic log')
                    if not accepted and time.monotonic() - started > 30:
                        raise RuntimeError('Master did not confirm accepting this update; it was not resubmitted')
                    label = 'waiting for this update to be accepted'
                if label != previous:
                    print(step + ': ' + label, flush=True); previous = label
                time.sleep(2)
            raise RuntimeError(step + ': confirmation timed out. Inspect local diagnostics before retrying')
        finally:
            server.images.pop(path, None)

def install_targets(host, steps, mode='release'):
    prepared, key = prepare_images(steps, mode), pairing_key()
    gateway = LocalGateway(host)
    info = gateway.info()
    if (info.get('ota') or {}).get('active'):
        raise RuntimeError('Another firmware update is active; wait for its result')
    if any(metadata['machine'] != info.get('machine') for _, _, metadata in prepared):
        raise ValueError('Hardware identity changed; install a matched USB/SD package')
    interface = route_address(gateway.host)
    # The request may address the station IP through the hotspot. The return
    # download still originates from the master's AP IP on that interface.
    downloader = '192.168.4.1' if ipaddress.IPv4Address(interface) in ipaddress.IPv4Network('192.168.4.0/24') else gateway.host
    with ImageServer((interface, 0), downloader) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        print('Serving firmware on the PC local interface. Allow Python through Windows Firewall if prompted.', flush=True)
        try:
            gateway.pause()
            for image in prepared:
                gateway.install(image, server, key)
        finally:
            server.shutdown(); thread.join(timeout=5)
    print('Finished. Use Maintenance > Calibrate arms, then Show time.', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.4.1', help='master hotspot or router LAN IPv4 address')
    parser.add_argument('--target', choices=['all', *IMAGES], default='all')
    parser.add_argument('--build', action='store_true', help='build all firmware before verifying/installing the bundle')
    args = parser.parse_args()
    if args.build:
        subprocess.run([sys.executable, str(ROOT / 'build.py'), 'all'], check=True)
    steps = list(IMAGES) if args.target == 'all' else ['master', 'dashboard'] if args.target == 'master' else [args.target]
    install_targets(args.host, steps)

if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))

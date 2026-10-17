"""Install verified Timiac firmware through the signed-in cloud gateway."""
from __future__ import annotations
import argparse
import getpass
import http.cookiejar
import json
import os
import time
import urllib.error
import urllib.request
from pathlib import Path
from run import verify_release

ROOT = Path(__file__).resolve().parent
IMAGES = {
    "master": ("master", "app", "esp_master_release.bin"),
    "esp1": ("esp1", "app", "esp_board1.bin"),
    "esp2": ("esp2", "app", "esp_board2.bin"),
    "octopus1": ("octopus1", "octopus", "firmware.bin"),
    "octopus2": ("octopus2", "octopus", "firmware.bin"),
    "dashboard": ("master", "filesystem", "master_release_littlefs.bin"),
}

class Gateway:
    def __init__(self, url):
        if not url.startswith("https://"):
            raise ValueError("Use the HTTPS hardware gateway URL")
        self.url = url.rstrip("/")
        self.client = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def call(self, path, body=None, headers=None):
        binary = isinstance(body, bytes)
        data = body if binary else json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data, headers={
            "User-Agent": "Timiac-Updater/1.0", "Origin": self.url,
            "Content-Type": "application/octet-stream" if binary else "application/json",
            **(headers or {}),
        })
        try:
            with self.client.open(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            try:
                message = json.load(error).get("error", str(error))
            except (ValueError, AttributeError):
                message = str(error)
            failure = RuntimeError(message)
            failure.status = error.code
            raise failure from None

    def install(self, device, step, timeout=1260):
        target, kind, filename = IMAGES[step]
        print(f"Uploading {filename} for {target} ({kind})...", flush=True)
        base = "/api/devices/" + device
        image = self.call(base + "/firmware", (ROOT / "firmware/two-motor" / filename).read_bytes(),
                          {"X-Timiac-Target": target, "X-Timiac-Kind": kind})
        job = self.call(base + "/updates", {"imageId": image["id"]})
        deadline, previous = time.monotonic() + timeout, ""
        while time.monotonic() < deadline:
            try:
                remote = self.call(base + "/status")
            except (OSError, RuntimeError) as error:
                if isinstance(error, RuntimeError) and getattr(error, 'status', 0) < 500:
                    raise
                label = 'waiting for the status connection; the existing job is not resubmitted'
                if label != previous:
                    print(f"{target}: {label}", flush=True)
                    previous = label
                time.sleep(3)
                continue
            ota, receipt = (remote.get("status") or {}).get("ota") or {}, remote.get("update") or {}
            if receipt.get("id") == job["id"]:
                if receipt.get("state") == "complete":
                    print(f"{target}: complete. Clock remains paused.", flush=True)
                    return
                if receipt.get("state") in ("failed", "expired", "cancelled"):
                    raise RuntimeError(f"{target}: {receipt.get('error') or receipt['state']}")
            label = (ota.get("phase") or ota.get("state", "queued")) if ota.get("id") == job["id"] else "queued"
            if ota.get("id") == job["id"] and ota.get("size"):
                label += f" {min(100, round(ota.get('done', 0) / ota['size'] * 100))}%"
            if not remote.get("online"):
                label = "waiting for the master to reconnect"
            if label != previous:
                print(f"{target}: {label}", flush=True)
                previous = label
            time.sleep(3)
        raise RuntimeError(f"{target}: confirmation timed out; check dashboard status before trying again")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=["all", *IMAGES])
    parser.add_argument("--url", default="https://control.timiac.it")
    parser.add_argument("--username", default="Andrea")
    parser.add_argument("--password-env", help="read the dashboard password from this environment variable instead of prompting")
    args = parser.parse_args()
    verify_release()
    gateway = Gateway(args.url)
    password = os.environ.get(args.password_env, "") if args.password_env else getpass.getpass("Dashboard password: ")
    if not password:
        raise ValueError("Dashboard password is required")
    gateway.call("/api/login", {"username": args.username, "password": password})
    config = gateway.call("/api/config")
    steps = list(IMAGES) if args.target == "all" else ["master", "dashboard"] if args.target == "master" else [args.target]
    try:
        for step in steps:
            gateway.install(config["deviceId"], step)
        print("Finished. Use Maintenance > Calibrate arms, then Show time when ready.")
    finally:
        try:
            gateway.call("/api/logout", {})
        except (RuntimeError, OSError):
            pass  # A lost sign-out connection must not hide the flashing result.

if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError) as error:
        raise SystemExit(str(error))

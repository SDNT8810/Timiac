"""Archive and verify the complete workspace before the controller rebuild."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    destination = ROOT / "BackUp" / datetime.now().strftime("before-rebuild-%Y%m%d-%H%M%S")
    destination.mkdir(parents=True, exist_ok=False)
    files = sorted(p for p in ROOT.rglob("*") if p.is_file() and "BackUp" not in p.relative_to(ROOT).parts)
    manifest = {}
    archive = destination / "Timiac.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as output:
        for index, path in enumerate(files):
            relative = path.relative_to(ROOT).as_posix()
            digest = hashlib.sha256()
            with path.open("rb") as source, output.open(relative, "w", force_zip64=True) as target:
                while chunk := source.read(1024 * 1024):
                    digest.update(chunk)
                    target.write(chunk)
            manifest[relative] = digest.hexdigest()
            if index % 1000 == 0:
                print(f"Archived {index}/{len(files)} files", flush=True)
    (destination / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    with zipfile.ZipFile(archive) as source:
        for name, expected in manifest.items():
            digest = hashlib.sha256()
            with source.open(name) as entry:
                while chunk := entry.read(1024 * 1024):
                    digest.update(chunk)
            if digest.hexdigest() != expected:
                raise RuntimeError(f"Backup verification failed: {name}")
    (destination / "VERIFIED.txt").write_text(f"Verified all {len(files)} files against SHA-256.\n", encoding="utf-8")
    print(f"Verified full backup: {archive} ({archive.stat().st_size:,} bytes)", flush=True)


if __name__ == "__main__":
    main()

"""Build a separate filesystem containing only the user dashboard."""
from pathlib import Path
import shutil

Import("env")
project = Path(env.subst("$PROJECT_DIR"))
assets = project / ".pio" / "release-data"
if assets.exists():
    # Recreate only this known staging directory; never package old debug assets.
    if assets.resolve().parent != (project / ".pio").resolve():
        raise ValueError("Unsafe release asset staging path")
    shutil.rmtree(assets)
assets.mkdir(parents=True, exist_ok=True)
for source in (project.parent / "WebApp" / "release").iterdir():
    if source.is_file():
        shutil.copy2(source, assets / source.name)
for name in ("hardware.js", "clock-panel.js", "system-panel.js"):
    shutil.copy2(project.parent / "WebApp/public" / name, assets / name)
env.Replace(PROJECT_DATA_DIR=str(assets))

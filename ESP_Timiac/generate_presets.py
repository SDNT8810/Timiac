"""Refresh the embedded presets before compiling either master mode."""
from pathlib import Path
import runpy
Import("env")
runpy.run_path(str(Path(env.subst("$PROJECT_DIR")).parent / "tools/generate_presets.py"), run_name="__main__")

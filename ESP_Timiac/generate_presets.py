"""Refresh the embedded presets before compiling either master mode."""
from pathlib import Path
import runpy
import json
import re
Import("env")
runpy.run_path(str(Path(env.subst("$PROJECT_DIR")).parent / "tools/generate_presets.py"), run_name="__main__")
root = Path(env.subst("$PROJECT_DIR")).parent
revision = re.search(r'kSoftwareRevision\[\] = "([^"]+)"', (root / 'ESP_Timiac/include/config.h').read_text()).group(1)
identity = re.search(r'machineId\[\] = "([^"]+)"', (root / 'shared/machine_generated.h').read_text()).group(1)
(root / 'WebApp/public/firmware-version.json').write_text(json.dumps({
    'marker': 'TIMIAC_OTA_ROLE:master-filesystem', 'software': revision, 'machine': identity
}) + '\n', encoding='utf-8')

"""Guard the ESP32 animation call-chain stack budget in the actual linked ELF.

Xtensa's `entry a1, N` instruction gives each function's fixed stack frame.
Account for the nested final clock-pose conversion, including float formatting,
and reserve 2 KB for library descendants, task setup and register spills. This
is a regression budget, not a complete analysis of every possible runtime path.
"""
from pathlib import Path
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RESERVE_BYTES = 2048


def animation_stack_report(disassembly, release, stack_bytes):
    functions = {}
    bodies = {}
    parts = re.split(r"\n([0-9a-f]+) <([^\n]+)>:\n", disassembly)
    for index in range(1, len(parts), 3):
        name, body = parts[index + 1:index + 3]
        bodies[name] = body
        first = body.splitlines()[0]
        frame = re.search(r"\bentry\s+a1,\s+(0x[0-9a-f]+|\d+)", first)
        if frame:
            functions[name] = int(frame.group(1), 0)

    def frame_for(prefix):
        matches = [(name, size) for name, size in functions.items()
                   if name == prefix or (prefix.endswith("(") and name.startswith(prefix))]
        if len(matches) != 1:
            raise ValueError(f"Review animation stack guard: expected one linked function for {prefix}, found {len(matches)}")
        name, size = matches[0]
        return {"function": name, "bytes": size}

    base = ["loopTask(void*)", "loop()"]
    builders = ["(anonymous namespace)::writePresetTrajectory(",
                "timiac::presetTrajectoryFrame(", "timiac::presetStep(",
                "timiac::userAction(", "snprintf", "_svfprintf_r", "_dtoa_r"]
    paths = {"queued_animation": base + ["(anonymous namespace)::programTick()"] + builders}
    if release:
        paths["automatic_hour"] = base + ["(anonymous namespace)::releaseClockTick()",
                                          "(anonymous namespace)::startBuiltinPreset("] + builders
    # Inlining folds receiveCloudAction/startCloudAction into cloudTick. Only
    # enabled, paired builds link this call to the animation builder.
    cloud = "(anonymous namespace)::cloudTick()"
    if "startBuiltinPreset(" in bodies.get(cloud, ""):
        paths["remote_animation"] = base + [cloud, "(anonymous namespace)::startBuiltinPreset("] + builders
    report = {"loop_stack_bytes": stack_bytes, "reserve_bytes": RESERVE_BYTES, "paths": {}}
    for name, path in paths.items():
        frames = [frame_for(prefix) for prefix in path]
        total = sum(frame["bytes"] for frame in frames)
        report["paths"][name] = {"frames": frames, "accounted_bytes": total,
                                 "remaining_bytes": stack_bytes - total}
        if total + RESERVE_BYTES > stack_bytes:
            raise ValueError(f"{name}: {total} bytes of nested frames + {RESERVE_BYTES} bytes reserve "
                             f"exceed the {stack_bytes}-byte Arduino loop stack")
    return report


def check_master_stack(elf, objdump, release, run_env=None):
    config = (ROOT / "ESP_Timiac/include/config.h").read_text(encoding="utf-8")
    size = re.search(r"constexpr unsigned kMasterLoopStackBytes = (\d+);", config)
    if not size:
        raise ValueError("Missing explicit master loop stack size")
    main = (ROOT / "ESP_Timiac/src/main.cpp").read_text(encoding="utf-8")
    if "SET_LOOP_TASK_STACK_SIZE(kMasterLoopStackBytes);" not in main:
        raise ValueError("Master must use the stack size checked by its build guard")
    disassembly = subprocess.check_output([str(objdump), "-d", "-C", str(elf)], text=True, env=run_env)
    report = animation_stack_report(disassembly, release, int(size.group(1)))
    (Path(elf).parent / "master_stack.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for name, path in report["paths"].items():
        print(f"Master stack {name}: {path['accounted_bytes']} bytes of frames; "
              f"{path['remaining_bytes']} bytes remaining (minimum reserve {RESERVE_BYTES})", flush=True)
    return report

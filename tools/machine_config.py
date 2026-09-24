"""Adapt the pinned Lumiac configuration using one Timiac hardware contract."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "Marlin/profiles/lumiac-reference"


def baseline():
    source = json.loads((BASELINE / "source.json").read_text())
    configs = {}
    for name in ("Configuration.h", "Configuration_adv.h"):
        data = (BASELINE / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != source["sha256"][name]:
            raise ValueError(f"Pinned Lumiac configuration changed: {name}")
        configs[name] = data.decode("utf-8")
    return source, configs


def load():
    machine = json.loads((ROOT / "machine.json").read_text())
    source, _ = baseline()
    if machine["marlin_configuration_base"] != source["id"]:
        raise ValueError("Machine and pinned Lumiac baseline do not match")
    axes = machine["axes"]
    if "".join(a["name"] for a in axes) != "XYZABC" or "".join(a["marlin"] for a in axes) != "XYZIJK":
        raise ValueError("The physical axis contract must be XYZABC / XYZIJK")
    if [a["socket"] for a in axes] != [0, 1, 2, 3, 4, 5]:
        raise ValueError("Expected Lumiac's Octopus sockets 0,1,2,3,4,5")
    pins = [a[k] for a in axes for k in ("step", "dir", "enable", "uart", "endstop")]
    if len(set(pins)) != len(pins):
        raise ValueError("Two active signals share an MCU pin")
    if machine["driver_mode"] != "spreadCycle" or machine["driver"] != "TMC2209":
        raise ValueError("This controller requires TMC2209 UART in spreadCycle")
    if machine["microsteps"] not in (2, 8, 16, 32, 64) or machine["full_steps_per_mm"] <= 0:
        raise ValueError("Invalid microsteps or mechanical calibration")
    # Scale the pulse count whenever microsteps change; never change physical travel.
    machine["steps_per_mm"] = machine["full_steps_per_mm"] * machine["microsteps"]
    contract = {"machine": machine, "baseline_sha256": source["sha256"]}
    identity = hashlib.sha256(json.dumps(contract, sort_keys=True).encode()).hexdigest()[:12]
    return machine, "TIMIAC2-" + identity


def definitions(machine, identity):
    def array(value):
        return "{ " + ", ".join([str(value)] * 6) + " }"
    config = {
        "STRING_CONFIG_H_AUTHOR": '"Timiac generation 2"', "CUSTOM_MACHINE_NAME": json.dumps(identity),
        "MOTHERBOARD": machine["motherboard"], "SERIAL_PORT": "-1", "SERIAL_PORT_2": "3",
        "BAUDRATE": "115200", "BAUDRATE_2": "115200", "EXTRUDERS": "0",
        "TEMP_SENSOR_0": "0", "TEMP_SENSOR_BED": "0", "E0_DRIVER_TYPE": None,
        "ENDSTOPPULLUPS": "", "ENDSTOPPULLDOWNS": None,
        "VALIDATE_HOMING_ENDSTOPS": "", "DETECT_BROKEN_ENDSTOP": "",
        "DEFAULT_AXIS_STEPS_PER_UNIT": array(machine["steps_per_mm"]),
        "DEFAULT_MAX_FEEDRATE": array(f'{machine["max_feed_mm_min"] / 60:.6f}'),
        "DEFAULT_MAX_ACCELERATION": array(machine["max_acceleration_mm_s2"]),
        "DEFAULT_ACCELERATION": str(machine["acceleration_mm_s2"]),
        "DEFAULT_TRAVEL_ACCELERATION": str(machine["acceleration_mm_s2"]),
        "DEFAULT_RETRACT_ACCELERATION": str(machine["acceleration_mm_s2"]),
        "JUNCTION_DEVIATION_MM": str(machine["junction_deviation_mm"]),
        "S_CURVE_ACCELERATION": "", "ENDSTOP_INTERRUPTS_FEATURE": "",
        "HOMING_FEEDRATE_MM_M": array(machine["homing_feed_mm_min"]),
        "X_BED_SIZE": str(machine["travel_mm"]), "Y_BED_SIZE": str(machine["travel_mm"]),
        "Z_HOMING_HEIGHT": "0", "NO_MOTION_BEFORE_HOMING": "", "HOME_AFTER_DEACTIVATE": "",
        "EEPROM_SETTINGS": None, "SDSUPPORT": None,
        "Z_MIN_PROBE_USES_Z_MIN_ENDSTOP_PIN": None,
        "USE_UMIN_PLUG": None, "USE_VMIN_PLUG": None,
        "U_MIN_PIN": None, "V_MIN_PIN": None,
        "SPIDER_MICROSTEPS": str(machine["microsteps"]),
        "SPIDER_AXIS_STEPS_PER_UNIT": str(machine["steps_per_mm"]),
    }
    advanced = {
        "DEFAULT_STEPPER_TIMEOUT_SEC": "600", "AXIS_RELATIVE_MODES": array("false"),
        "HOMING_BUMP_MM": array(2), "HOMING_BUMP_DIVISOR": array(2),
        "STEALTHCHOP_XY": None, "STEALTHCHOP_Z": None, "STEALTHCHOP_I": None,
        "STEALTHCHOP_J": None, "STEALTHCHOP_K": None, "HYBRID_THRESHOLD": None,
        "CHOPPER_TIMING": machine["chopper_timing"], "INTERPOLATE": "true",
        "EMERGENCY_PARSER": "", "TMC_DEBUG": "", "MONITOR_DRIVER_STATUS": "",
        "MAX_CMD_SIZE": "192", "NO_SD_AUTOSTART": "", "EVENT_GCODE_SD_ABORT": None,
        "TMC_USE_SW_SPI": None, "CASE_LIGHT_ENABLE": None,
        "SD_ABORT_ON_ENDSTOP_HIT": None, "SPIDER_SD_FILE_CODES": None, "SPIDER_SD_FILE_COMPAT": None,
        "SPIDER_RUN_CURRENT_MA": str(machine["current_ma"]),
        "SPIDER_HOME_CURRENT_MA": str(machine["current_ma"]),
        "ENDSTOPS_ALWAYS_ON_DEFAULT": "", "SERIAL_OVERRUN_PROTECTION": "",
    }
    for axis in machine["axes"]:
        key = axis["marlin"]
        config.update({
            f"{key}_DRIVER_TYPE": machine["driver"], f"{key}_ENABLE_ON": "0",
            f"USE_{key}MIN_PLUG": "", f"USE_{key}MAX_PLUG": None,
            f"{key}_MIN_ENDSTOP_INVERTING": "false", f"INVERT_{key}_DIR": "false",
            f"{key}_HOME_DIR": "-1", f"{key}_MIN_POS": "0",
        })
        if key not in "XY":
            config[f"{key}_MAX_POS"] = str(machine["travel_mm"])
        advanced.update({
            f"{key}_CURRENT": str(machine["current_ma"]),
            f"{key}_CURRENT_HOME": f"{key}_CURRENT",
            f"{key}_MICROSTEPS": str(machine["microsteps"]), f"{key}_RSENSE": str(machine["rsense"]),
            f"{key}_SLAVE_ADDRESS": str(machine["uart_address"]), f"DISABLE_IDLE_{key}": "",
        })
    for n, letter in enumerate("ABC", 4):
        config[f"AXIS{n}_NAME"] = repr(letter)
        config[f"AXIS{n}_ROTATES"] = None
    # Lumiac puts SPI socket pins in Configuration.h. Timiac's complete UART
    # mapping belongs in the Octopus board header before pins_postprocess.
    for axis in "IJK":
        for signal in ("STEP", "DIR", "ENABLE", "CS", "MIN"):
            config[f"{axis}_{signal}_PIN"] = None
    return {"Configuration.h": config, "Configuration_adv.h": advanced}


def replace_definitions(text, values):
    for key, value in values.items():
        pattern = rf"^[ \t]*(?://[ \t]*)?#define[ \t]+{re.escape(key)}\b[^\n]*"
        replacement = f"// TIMIAC: {key} disabled" if value is None else f"#define {key} {value}"
        text, count = re.subn(pattern, lambda _: replacement, text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError(f"Expected exactly one template definition of {key}, got {count}")
    return text


def generate():
    machine, identity = load()
    header = ["#pragma once", "// Generated by tools/machine_config.py from machine.json.", "namespace timiac {",
              f'constexpr char machineId[] = "{identity}";',
              f'constexpr unsigned protocolVersion = {machine["protocol"]};',
              'constexpr char axes[] = "XYZABC";', 'constexpr unsigned axisCount = 6;',
              'constexpr unsigned char motorSockets[] = {' + ', '.join(str(a["socket"]) for a in machine["axes"]) + '};',
              f'constexpr float travelMm = {machine["travel_mm"]};',
              f'constexpr float maxFeed = {machine["max_feed_mm_min"]};',
              f'constexpr unsigned driverCurrent = {machine["current_ma"]};',
              f'constexpr unsigned driverMicrosteps = {machine["microsteps"]};', "}"]
    (ROOT / "shared/machine_generated.h").write_text("\n".join(header) + "\n")
    public = {"identity": identity, **machine}
    for directory in ("WebApp/public", "firmware/two-motor"):
        (ROOT / directory).mkdir(parents=True, exist_ok=True)
        (ROOT / directory / "machine.json").write_text(json.dumps(public, indent=2) + "\n")
    # The browser must use the same expected settings as the bridge parser.
    browser = {"identity": identity, "axes": "XYZABC",
               "sockets": [a["socket"] for a in machine["axes"]],
               "currentMa": machine["current_ma"], "microsteps": machine["microsteps"]}
    (ROOT / "WebApp/public/machine-generated.js").write_text(
        "// Generated by tools/machine_config.py.\nexport const machine = " + json.dumps(browser) + ";\n")
    # Kept as an inspectable build artifact; never used as another input.
    (ROOT / "Marlin/profiles/twelve-motor/profile.json").write_text(
        json.dumps(definitions(machine, identity), indent=2) + "\n")
    return machine, identity


if __name__ == "__main__":
    print(generate()[1])

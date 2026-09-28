"""Keep the active Octopus profile on the same six G-code letters as the ESPs."""
import json
from pathlib import Path
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.machine_config import baseline, load, definitions, replace_definitions


class AxisProfileTests(unittest.TestCase):
    def test_extra_axes_have_explicit_linear_abc_names(self):
        config = json.loads((ROOT / "Marlin/profiles/twelve-motor/profile.json").read_text())["Configuration.h"]
        for number, letter, driver in zip((4, 5, 6), "ABC", "IJK"):
            with self.subTest(axis=letter):
                self.assertEqual(config[f"AXIS{number}_NAME"], repr(letter))
                self.assertIsNone(config[f"AXIS{number}_ROTATES"])
                self.assertEqual(config[f"{driver}_DRIVER_TYPE"], "TMC2209")

    def test_all_six_sockets_match_lumiac_with_uart_and_original_endstops(self):
        machine = json.loads((ROOT / "machine.json").read_text())
        self.assertEqual([a["socket"] for a in machine["axes"]], [0, 1, 2, 3, 4, 5])
        expected = [
            ("PF13", "PF12", "PF14", "PC4", "PG6"),
            ("PG0", "PG1", "PF15", "PD11", "PG9"),
            ("PF11", "PG3", "PG5", "PC6", "PG10"),
            ("PG4", "PC1", "PA0", "PC7", "PG11"),
            ("PF9", "PF10", "PG2", "PF2", "PG12"),
            ("PC13", "PF0", "PF1", "PE4", "PG13"),
        ]
        for axis, pins in zip(machine["axes"], expected):
            with self.subTest(axis=axis["name"]):
                self.assertEqual(tuple(axis[k] for k in ("step", "dir", "enable", "uart", "endstop")), pins)

    def test_all_six_drivers_use_quiet_mode_and_restored_current(self):
        config = json.loads((ROOT / "Marlin/profiles/twelve-motor/profile.json").read_text())["Configuration_adv.h"]
        for group in ("XY", "Z", "I", "J", "K"):
            self.assertEqual(config[f"STEALTHCHOP_{group}"], "")
        self.assertEqual(config["HOLD_MULTIPLIER"], "0.5")
        self.assertEqual(config["CHOPPER_TIMING"], "CHOPPER_DEFAULT_24V")
        self.assertIsNone(config["SENSORLESS_HOMING"])
        self.assertEqual(config["HYBRID_THRESHOLD"], "")
        for axis in "XYZIJK":
            self.assertEqual(config[f"{axis}_HYBRID_THRESHOLD"], "1")
            self.assertEqual(config[f"{axis}_CURRENT"], "800")
            self.assertEqual(config[f"{axis}_CURRENT_HOME"], f"{axis}_CURRENT")
            self.assertEqual(config[f"{axis}_RSENSE"], "0.11")
            self.assertEqual(config[f"{axis}_MICROSTEPS"], "64")
            self.assertEqual(config[f"{axis}_SLAVE_ADDRESS"], "0")

    def test_run_and_home_use_800_ma_with_24v_timing(self):
        machine, _ = load()
        self.assertEqual(machine["current_ma"], 800)
        settings = definitions(machine, "test")["Configuration_adv.h"]
        self.assertEqual(settings["SPIDER_RUN_CURRENT_MA"], "800")
        self.assertEqual(settings["SPIDER_HOME_CURRENT_MA"], "800")
        self.assertEqual(settings["CHOPPER_TIMING"], "CHOPPER_DEFAULT_24V")
        self.assertEqual(settings["INTERPOLATE"], "true")
        self.assertEqual(machine["homing_strategy"], "concurrent-all")
        self.assertLessEqual(machine["steps_per_mm"] * machine["max_feed_mm_min"] / 60, 100000)

    def test_microstep_change_preserves_measured_travel(self):
        machine, _ = load()
        self.assertEqual(machine["microsteps"], 64)
        self.assertEqual(machine["steps_per_mm"], 11520)
        self.assertEqual(machine["max_feed_mm_min"], 500)
        self.assertEqual(machine["steps_per_mm"] / machine["microsteps"], 720 / 4)

    def test_only_microswitch_homing_and_no_sd_runtime(self):
        machine, identity = load()
        settings = definitions(machine, identity)
        self.assertEqual(machine["homing_mode"], "microswitch")
        self.assertIsNone(settings["Configuration.h"]["SDSUPPORT"])
        advanced = settings["Configuration_adv.h"]
        self.assertIsNone(advanced["SENSORLESS_HOMING"])
        self.assertEqual(advanced["NO_SD_AUTOSTART"], "")
        for axis in "XYZIJK":
            self.assertIsNone(advanced[f"{axis}_STALL_SENSITIVITY"])

    def test_pinned_lumiac_baseline_is_adapted_without_spi_or_pin_conflicts(self):
        machine, identity = load()
        source, configs = baseline()  # Verifies the two original file hashes.
        self.assertEqual(source["id"], "lumiac-b250656")
        settings = definitions(machine, identity)
        for name, values in settings.items():
            adapted = replace_definitions(configs[name], values)
            self.assertNotIn("#define TMC_USE_SW_SPI", adapted)
            self.assertNotIn("#define I_STEP_PIN", adapted)
            self.assertNotIn("#define CASE_LIGHT_ENABLE", adapted)
        self.assertEqual(settings["Configuration.h"]["DEFAULT_ACCELERATION"], "25")
        self.assertEqual(settings["Configuration.h"]["HOMING_FEEDRATE_MM_M"], "{ 120, 120, 120, 120, 120, 120 }")
        self.assertEqual(settings["Configuration.h"]["JUNCTION_DEVIATION_MM"], "0.003")
        self.assertEqual(settings["Configuration.h"]["S_CURVE_ACCELERATION"], "")
        self.assertEqual(settings["Configuration_adv.h"]["ENDSTOPS_ALWAYS_ON_DEFAULT"], "")


if __name__ == "__main__":
    unittest.main()

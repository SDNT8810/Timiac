"""Keep the active Octopus profile on the same six G-code letters as the ESPs."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AxisProfileTests(unittest.TestCase):
    def test_extra_axes_have_explicit_linear_abc_names(self):
        config = json.loads((ROOT / "Marlin/profiles/twelve-motor/profile.json").read_text())["Configuration.h"]
        for number, letter, driver in zip((4, 5, 6), "ABC", "IJK"):
            with self.subTest(axis=letter):
                self.assertEqual(config[f"AXIS{number}_NAME"], repr(letter))
                self.assertIsNone(config[f"AXIS{number}_ROTATES"])
                self.assertEqual(config[f"{driver}_DRIVER_TYPE"], "TMC2209")

    def test_z_uses_the_motor3_socket_instead_of_motor2(self):
        profile = json.loads((ROOT / "Marlin/profiles/twelve-motor/profile.json").read_text())
        self.assertEqual(profile["post_pins"], {
            "Z_STEP_PIN": "PG4", "Z_DIR_PIN": "PC1", "Z_ENABLE_PIN": "PA0",
            "Z_CS_PIN": "PC7", "Z_SERIAL_TX_PIN": "PC7",
        })


if __name__ == "__main__":
    unittest.main()

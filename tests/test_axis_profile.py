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


if __name__ == "__main__":
    unittest.main()

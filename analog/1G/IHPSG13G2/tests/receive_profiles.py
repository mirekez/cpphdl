"""Higher drive must strengthen the negative stimulus, not relax positive limits."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('derived_receive_cases', Path(__file__).with_name('receive_precision.py'))
profiles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profiles)


class Profiles(unittest.TestCase):
    def test_parallel_profile_retains_every_positive_load_and_checks_former_overload(self):
        original = dict(profiles.STANDARD_CASES)
        selected = profiles.cases('parallel')
        for name, value in original.items():
            if name != 'overload':
                self.assertEqual(selected[name], value)
        self.assertEqual(selected['large_load'], (4, 40, 10))
        self.assertEqual(selected['overload'], (4, 400, 10))
        self.assertEqual(profiles.cases('standard'), original)
        self.assertEqual(profiles.STANDARD_CASES, original)

    def test_unknown_profile_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unknown receive load profile'):
            profiles.cases('unchecked')


if __name__ == '__main__':
    unittest.main()

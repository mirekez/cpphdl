"""Conversion accuracy, held charge and decision stability remain separate checks."""
import copy
import math
import unittest
from programmable_receive_conversion import contract
from programmable_conversion_compare import require_same_profile


def fixture():
    origin, period = 16e-6, 4e-9
    header = ['time_s', 'PLUS', 'MINUS', 'GAIN_INPUT', 'GAIN_OUTPUT', 'VDD', 'VLOGIC', 'GAIN_BIAS',
              'HELD0', 'TRACK0', 'CORE_DECISION0']
    rows = []
    for tick in range(5001):
        ns = tick * .02
        rows.append([origin + ns * 1e-9, 1.74, 1.56, 1.65, 1.65 if ns < 52 else 1.75,
                     3.299, 1.2, 1.65, 1.65, 3.3 if 2 < ns < 14 else 0, 1.2])
    controls = [dict(clock=clock, fault=0, valid=int(clock == 13), slice=0, data=128,
                     normalized_valid=int(clock == 21), normalized_slice=0, normalized=128,
                     clipped=0, convert=int(4 <= clock <= 11)) for clock in range(-1, 25)]
    metadata = dict(slices=1, launches=1, clocks=25, period_s=period, clock_origin_s=origin, gain_code=4)
    return header, rows, controls, metadata


class Contract(unittest.TestCase):
    def test_same_codes_do_not_allow_different_receiver_classes(self):
        profile = dict(slices=16, gain=4, storage_pf=1.5, bursts=2,
                       fixture_source='wideband_receiver.cpp', gain_compensation_F=.25e-12, corner='mos_tt', temperature_C=27)
        require_same_profile(profile, dict(profile))
        for key, value in (('fixture_source', 'buffered_receiver.cpp'), ('storage_pf', 3),
                           ('gain_compensation_F', 1e-12), ('corner', 'mos_ss'), ('temperature_C', 125)):
            altered = dict(profile, **{key: value})
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'profiles'):
                require_same_profile(profile, altered)

    def test_incomplete_receiver_identity_is_rejected(self):
        profile = dict(slices=16, gain=4, storage_pf=1.5, bursts=2,
                       fixture_source='wideband_receiver.cpp', gain_compensation_F=.25e-12, corner='mos_tt', temperature_C=27)
        for key in tuple(profile):
            altered = dict(profile)
            altered.pop(key)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'profiles'):
                require_same_profile(profile, altered)

    def test_complete_conversion(self):
        result = contract(*fixture())
        self.assertTrue(all(result['checks'].values()))
        self.assertEqual(result['maximum_code_error'], 0)

    def test_code_error_cannot_be_hidden_by_correct_normalization(self):
        header, rows, controls, metadata = fixture()
        controls[14]['data'] = 133
        value = (133 - 128) * 424 / 256
        controls[22]['normalized'] = 128 + math.floor(value + .5)
        result = contract(header, rows, controls, metadata)
        self.assertTrue(result['checks']['normalization'])
        self.assertFalse(result['checks']['conversion_accuracy'])

    def test_hold_kickback_fails_even_when_code_is_correct(self):
        header, rows, controls, metadata = fixture()
        for index, row in enumerate(rows):
            if 15e-9 <= row[0] - metadata['clock_origin_s'] <= 49e-9:
                row[8] += .011 if index & 1 else -.011
        result = contract(header, rows, controls, metadata)
        self.assertTrue(result['checks']['conversion_accuracy'])
        self.assertTrue(result['checks']['sampler_acquisition'])
        self.assertFalse(result['checks']['hold'])

    def test_every_state_in_the_decision_aperture_is_checked(self):
        for repeat_time in (False, True):
            header, rows, controls, metadata = fixture()
            index = 1100
            if repeat_time:
                rows.insert(index + 1, list(rows[index]))
                index += 1
            rows[index][10] = .6
            with self.subTest(equal_time=repeat_time):
                self.assertFalse(contract(header, rows, controls, metadata)['checks']['core_decisions_delivered'])

    def test_missing_or_misidentified_conversion(self):
        for fault in ('missing', 'identity', 'fault'):
            header, rows, controls, metadata = fixture()
            if fault == 'missing':
                controls[14]['valid'] = 0
            elif fault == 'identity':
                controls[14]['slice'] = 1
            else:
                controls[8]['fault'] = 1
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                contract(header, rows, controls, metadata)

    def test_rails_headroom_and_excitation_are_not_code_checks(self):
        for column, value, check in ((5, 2.9, 'io_supply'), (6, .9, 'core_supply'),
                                     (4, 2.2, 'gain_output_headroom'), (4, 1.65, 'dynamic_excitation')):
            header, rows, controls, metadata = fixture()
            for row in rows:
                row[column] = value
            with self.subTest(check=check):
                self.assertFalse(contract(header, rows, controls, metadata)['checks'][check])


if __name__ == '__main__':
    unittest.main()

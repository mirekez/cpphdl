#!/usr/bin/env python3
"""Guard DC stimulus conversion and reject incomplete or ambiguous solver output."""
from pathlib import Path
import tempfile
import unittest

from receive_upper_current_dc import dc_deck, pwl_value, read_raw, screen


def receiver_deck():
    sources = '\n'.join(f'V{i} n{i} 0 PWL(0 0 1e-8 3.3)' for i in range(17))
    probes = '\n'.join(f'let observation{i} = v(n{i})' for i in range(39))
    return (sources + '\nRload n0 n1 2000\n.ic v(n0)=0\n.save v(n0)\n'
            'Vsim_timestep_grid cpphdl_sim_timestep_grid 0 PULSE(0 0 0 1p 1p 1n 2n)\n'
            '.control\n' + probes + '\ntran 1n 20u\n.endc\n.end\n')


RAW = ('Title: DC guard\nFlags: real\nNo. Variables: 2\nNo. Points: 1\n'
       'Variables:\n\t0\tv(out)\tvoltage\n\t1\tv(vdd)\tvoltage\n'
       'Values:\n0\t1.81\n\t3.3\n')


class DCGuards(unittest.TestCase):
    def test_pwl_interpolation_and_endpoint_hold(self):
        for time, expected in ((-1, 1), (0, 1), (1, 3), (2, 5), (99, 5)):
            with self.subTest(time=time):
                self.assertEqual(pwl_value('0 1 2 5', time), expected)

    def test_pwl_rejects_ambiguous_or_nonfinite_stimulus(self):
        for encoded, time in (('0 1 0 2', 0), ('1 1 0 2', 0), ('0 1 2', 0),
                              ('0 nan 1 2', 0), ('0 1 1 inf', 0), ('0 1 1 2', float('nan'))):
            with self.subTest(encoded=encoded, time=time), self.assertRaises(ValueError):
                pwl_value(encoded, time)

    def test_snapshot_preserves_laws_and_removes_initial_voltage(self):
        deck, probes, stimuli = dc_deck(receiver_deck(), 16.022e-6)
        self.assertEqual(len(probes), 39)
        self.assertEqual(len(stimuli), 17)
        self.assertTrue(all(value == 3.3 for value in stimuli.values()))
        self.assertIn('Rload n0 n1 2000\n', deck)
        self.assertIn('Vsim_timestep_grid cpphdl_sim_timestep_grid 0 DC 0\n', deck)
        for transient in ('.ic ', '.save ', 'tran ', 'PWL(', 'PULSE('):
            self.assertNotIn(transient, deck)
        self.assertIn('\nop\nwrite dc.raw all\n', deck)

    def test_changed_boundaries_and_unconverted_sources_rejected(self):
        original = receiver_deck()
        changed = [original.replace('V16 n16', 'V15 n16'),
                   original.replace('let observation38', 'let observation37'),
                   original.replace('.ic v(n0)=0\n', ''),
                   original.replace('V0 n0 0 PWL(0 0 1e-8 3.3)', 'V0 n0 0 SIN(0 1 1e6)'),
                   original.replace('Rload', 'Bunknown foo 0 V=time\nRload'),
                   original.replace('.control\n', '.control\n.control\n')]
        for deck in changed:
            with self.subTest(deck=deck), self.assertRaises(ValueError):
                dc_deck(deck, 16.022e-6)

    def test_complete_raw_and_corrupt_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'dc.raw'
            path.write_text(RAW)
            self.assertEqual(read_raw(path), {'v(out)': 1.81, 'v(vdd)': 3.3})
            variants = [RAW.rsplit('\n\t3.3', 1)[0], RAW.replace('Points: 1', 'Points: 2'),
                        RAW.replace('Flags: real', 'Flags: complex'),
                        RAW.replace('\t1\tv(vdd)', '\t2\tv(vdd)'),
                        RAW.replace('v(vdd)', 'v(out)'), RAW.replace('1.81', 'nan'),
                        RAW.replace('0\t1.81', '1\t1.81'), RAW + '\t7\n']
            for text in variants:
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    read_raw(path)

    def test_reference_and_divider_fail_independently(self):
        supply = 3.3
        target = supply * 243 / 443
        probes = {f'observation{i}': f'v(n{i})' for i in range(39)}
        for reference, divider, expected in ((0, 0, (True, True)), (.016, 0, (False, True)),
                                              (0, .003, (True, False))):
            values = {expression: 0 for expression in probes.values()}
            values.update({'v(n0)': target + reference, 'v(n1)': supply, 'v(n2)': target + divider})
            result = screen(values, probes)
            self.assertEqual(tuple(result['checks'].values()), expected)


if __name__ == '__main__':
    unittest.main()

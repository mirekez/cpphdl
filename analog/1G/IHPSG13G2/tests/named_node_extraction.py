#!/usr/bin/env python3
"""Reject changes hidden by internal-node naming or zero-ground correction."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import inventory
from extraction_capacitance import conserve
from named_node_extraction import check_relabel, label_plan, normalize_zero_ground


REFERENCE = '''.subckt ExtractedAnalog D G S VSS
X0 D G S VSS sg13_lv_nmos w=1u l=.13u
C0 G D 3f
C1 D VSS 2f
.ends
'''
DISTRIBUTED = REFERENCE.replace('X0 D G S VSS', 'X0 D gg S VSS').replace(
    'C0 G D 3f', 'C0 gg D 3f\nR0 G gg 10\nCdup gg VSS 3f')
PORTS = ['D', 'G', 'S', 'VSS']


class NamedExtraction(unittest.TestCase):
    def test_resistor_body_identity_is_retained(self):
        ext = ('scale 1000 1 .5\nnode "a_10_20#" 2 3 10 20 pres\n'
               'node "a_30_40#" 1 2 30 40 ndif\n'
               'device rsubckt rppd 10 20 11 21 w=100 l=200 '
               '"VSS" "a_10_20#" 0 0 "A" 100 3,4 "B" 100 3,4\n')
        aliases, commands = label_plan(ext)
        self.assertEqual(aliases, {'a_30_40#': 'RC_INTERNAL_0'})
        self.assertEqual(commands[-1], 'label RC_INTERNAL_0 center ndif')
        for changed in (ext.replace('rsubckt rppd', 'rsubckt unknown'),
                        ext.replace('rppd 10 20', 'rppd 11 20'),
                        ext.replace('"VSS" "a_10_20#"', '"VSS" "other_body"'),
                        ext + ext.splitlines()[1] + '\n',
                        ext + ext.splitlines()[-1] + '\n'):
            with self.assertRaises(ValueError):
                label_plan(changed)

    def test_units_and_layer_guard(self):
        ext = 'scale 1000 1 .5\nnode "a_298_337#" 3 87.6 298 337 p\n'
        aliases, commands = label_plan(ext)
        self.assertEqual(aliases, {'a_298_337#': 'RC_INTERNAL_0'})
        self.assertEqual(commands, ['box values 1.49um 1.685um 1.495um 1.69um',
                                    'label RC_INTERNAL_0 center p'])
        for changed in (ext.replace('337 p', '337 unknown'), ext + 'RC_INTERNAL_0',
                        ext.replace('1 .5', '1 0'), ext + ext.splitlines()[1] + '\n'):
            with self.assertRaises(ValueError):
                label_plan(changed)

    def test_labels_preserve_topology_dimensions_and_charge(self):
        original = REFERENCE.replace(' G ', ' a_1_2# ')
        named = original.replace('a_1_2#', 'RC_INTERNAL_0')
        # External ports remain fixed; the example labels an internal gate.
        original = original.replace('D a_1_2# S VSS\n', 'D G S VSS\n', 1)
        named = named.replace('D RC_INTERNAL_0 S VSS\n', 'D G S VSS\n', 1)
        check_relabel(original, named, PORTS, {'a_1_2#': 'RC_INTERNAL_0'})
        for changed in (named.replace('w=1u', 'w=2u'), named.replace('3f', '4f'),
                        named.replace('X0 D RC_INTERNAL_0', 'X0 S RC_INTERNAL_0')):
            with self.assertRaises(ValueError):
                check_relabel(original, changed, PORTS, {'a_1_2#': 'RC_INTERNAL_0'})

    def test_only_identified_zero_ground_duplication_removed(self):
        a, b = inventory(REFERENCE, PORTS), inventory(DISTRIBUTED, PORTS)
        text, audit = normalize_zero_ground(a, b, DISTRIBUTED, 'VSS')
        self.assertEqual(set(audit), {'G'})
        self.assertIn('Cdup gg VSS 0', text)
        self.assertIn('C0 gg D 3f', text)
        self.assertIn('C1 D VSS 2f', text)
        corrected, _ = conserve(a, inventory(text, PORTS), text, 'VSS')
        self.assertEqual(inventory(corrected, PORTS)['resistors'], b['resistors'])
        self.assertEqual(inventory(corrected, PORTS)['geometry'], b['geometry'])

    def test_unexplained_ground_change_and_shorted_nets_rejected(self):
        a = inventory(REFERENCE, PORTS)
        for text in (DISTRIBUTED.replace('Cdup gg VSS 3f', 'Cdup gg VSS 4f'),
                     DISTRIBUTED.replace('.ends', 'Rshort G D 1\n.ends')):
            with self.assertRaises(ValueError):
                normalize_zero_ground(a, inventory(text, PORTS), text, 'VSS')


if __name__ == '__main__':
    unittest.main()

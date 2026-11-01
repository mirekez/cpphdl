#!/usr/bin/env python3
"""Guard geometric identity and charge conservation for parallel R3 bodies."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from resistor_array_extraction import adapt
from extract_analog import inventory


PORTS = ['A', 'B', 'SUB', 'GATE']
REFERENCE = '''.subckt ExtractedAnalog A B SUB GATE
X0 A B SUB rppd w=0.5u l=20u
X1 A B SUB rppd w=0.5u l=20u
Ca A SUB 6f
Cb B SUB 6f
Cc body0# GATE .2f
Cd body1# GATE .7f
.ends
'''
DISTRIBUTED = REFERENCE.replace('X0 A B SUB', 'X7 a1 b1 s1').replace(
    'X1 A B SUB', 'X3 a0 b0 s0').replace('.ends', '''Ra0 A a0 1
Ra1 A a1 2
Rb0 B b0 3
Rb1 B b1 4
Rs0 SUB s0 5
Rs1 SUB s1 6
.ends''')
EXT = '''scale 1000 1 0.5
node "body0#" 100 4000 0 0 pres
node "body1#" 100 6000 10 0 pres
device rsubckt rppd 0 0 1 1 w=100 l=4000 "SUB" "body0#" 0 0 "A" 100 0,0 "B" 100 0,0
device rsubckt rppd 10 0 11 1 w=100 l=4000 "SUB" "body1#" 0 0 "A" 100 0,0 "B" 100 0,0
'''
RES_EXT = '''scale 1000 1 0.5
device rsubckt rppd 10 0 11 1 "s1" "body1#" 0 0 "b1" 100 0,0 "a1" 100 0,0
device rsubckt rppd 0 0 1 1 "s0" "body0#" 0 0 "b0" 100 0,0 "a0" 100 0,0
'''


class ResistorArray(unittest.TestCase):
    def convert(self, reference=REFERENCE, distributed=DISTRIBUTED, ext=EXT, res_ext=RES_EXT):
        return adapt(reference, distributed, ext, res_ext, PORTS, 'SUB')

    def test_geometric_mapping_ignores_instance_indices_and_record_order(self):
        text, result = self.convert()
        self.assertEqual(result['bodies']['X7']['body'], 'body1#')
        self.assertEqual(result['bodies']['X3']['body'], 'body0#')
        self.assertAlmostEqual(result['removed_duplicate_body_substrate_F'] / 1e-15, 10.)
        self.assertEqual(result['connections']['unattached_endpoints'], 0)
        net = inventory(text, PORTS)
        self.assertEqual(net['models'], {'rppd': 4})
        for coupling in inventory(REFERENCE, PORTS)['capacitors'][2:]:
            self.assertIn(coupling, net['capacitors'])
        reversed_ext = '\n'.join([RES_EXT.splitlines()[0], *RES_EXT.splitlines()[:0:-1]]) + '\n'
        self.assertEqual(self.convert(res_ext=reversed_ext), (text, result))

    def test_missing_or_duplicate_identity_rejected(self):
        for altered in (RES_EXT.rsplit('device', 1)[0],
                        RES_EXT + RES_EXT.splitlines()[-1] + '\n',
                        RES_EXT.replace('10 0 11 1', '20 0 21 1'),
                        RES_EXT.replace('"body1#"', '"body0#"')):
            with self.subTest(ext=altered), self.assertRaises(ValueError):
                self.convert(res_ext=altered)

    def test_geometry_inventory_and_connectivity_rejected(self):
        for altered in (DISTRIBUTED.replace('l=20u', 'l=21u', 1),
                        DISTRIBUTED.replace('X7 a1 b1 s1', 'X7 a0 b0 s0'),
                        DISTRIBUTED.replace('Rs1 SUB s1 6', 'Rs1 GATE s1 6')):
            with self.subTest(netlist=altered), self.assertRaises(ValueError):
                self.convert(distributed=altered)

    def test_insufficient_substrate_charge_rejected(self):
        with self.assertRaisesRegex(ValueError, 'body capacitance exceeds'):
            self.convert(distributed=DISTRIBUTED.replace('SUB 6f', 'SUB 4f'))

    def test_logical_short_rejected(self):
        with self.assertRaisesRegex(ValueError, 'shorts distinct'):
            self.convert(distributed=DISTRIBUTED.replace('.ends', 'Rshort A B 1\n.ends'))


if __name__ == '__main__':
    unittest.main()

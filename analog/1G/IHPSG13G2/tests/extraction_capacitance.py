#!/usr/bin/env python3
"""Reject lost coupling, unexplained charge and accidental shorts in RC output."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extraction_capacitance import conserve
from extract_analog import inventory


class ConservationTests(unittest.TestCase):
    def setUp(self):
        # Two wires: 2 fF coupling, 1/3 fF substrate loads. The faulty tool
        # duplicates coupling into 3/5 fF substrate loads split over each wire.
        self.reference = dict(capacitors=[('A', 'B', 2e-15), ('A', 'VSS', 1e-15), ('B', 'VSS', 3e-15)])
        self.raw = dict(resistors=[('A', 'a1', 10.), ('B', 'b1', 20.)],
                        capacitors=[('A', 'B', 2e-15), ('A', 'VSS', 1e-15),
                                    ('a1', 'VSS', 2e-15), ('B', 'VSS', 2e-15), ('b1', 'VSS', 3e-15)])
        self.text = ('R0 A a1 10\nR1 B b1 20\nC0 A B 2f\nC1 A VSS 1f\n'
                     'C2 a1 VSS 2f\nC3 B VSS 2f\nC4 b1 VSS 3f\n')

    def test_conserve_charge_and_keep_resistors(self):
        text, audit = conserve(self.reference, self.raw, self.text, 'VSS')
        self.assertEqual(audit['corrected_nodes'], 2)
        self.assertAlmostEqual(audit['corrected_total_F'] / 1e-15, 6)
        self.assertIn('R0 A a1 10\nR1 B b1 20\nC0 A B 2f\n', text)
        # With equal voltages, coupling stores no charge. The substrate load
        # must be 4 fF, not the erroneous 8 fF returned by raw full RC.
        substrate = sum(float(line.split()[3]) for line in text.splitlines()[3:])
        self.assertAlmostEqual(substrate / 1e-15, 4)

    def test_already_correct_is_unchanged(self):
        raw = dict(resistors=self.raw['resistors'], capacitors=self.reference['capacitors'])
        text = 'C0 A B 2f\nC1 A VSS 1f\nC2 B VSS 3f\n'
        _, audit = conserve(self.reference, raw, text, 'VSS')
        self.assertEqual(audit['corrected_nodes'], 0)

    def test_intranet_charge_is_retained_without_inventing_collapsed_load(self):
        raw = copy.deepcopy(self.raw)
        raw['capacitors'].append(('A', 'a1', 4e-15))
        text, audit = conserve(self.reference, raw, self.text + 'C5 A a1 4f\n', 'VSS')
        self.assertIn('C5 A a1 4f\n', text)
        self.assertEqual(audit['retained_intranet_capacitors'], [
            dict(a='A', b='a1', capacitance_F=4e-15, logical_net='A')])
        self.assertAlmostEqual(audit['corrected_total_F'] / 1e-15, 6)
        # A real short between formerly distinct nets must still fail; this
        # allowance cannot be used to conceal lost inter-net capacitance.
        raw['resistors'].append(('a1', 'b1', 10.))
        with self.assertRaisesRegex(ValueError, 'shorts'):
            conserve(self.reference, raw, self.text, 'VSS')

    def test_lost_coupling_rejected(self):
        for replacement in (0., 1e-15, 3e-15):
            raw = copy.deepcopy(self.raw)
            raw['capacitors'][0] = ('A', 'B', replacement)
            with self.subTest(value=replacement), self.assertRaisesRegex(ValueError, 'coupling'):
                conserve(self.reference, raw, self.text, 'VSS')

    def test_unexplained_ground_load_rejected(self):
        raw = copy.deepcopy(self.raw)
        raw['capacitors'][1] = ('A', 'VSS', 1.1e-15)
        with self.assertRaisesRegex(ValueError, 'unexplained'):
            conserve(self.reference, raw, self.text, 'VSS')

    def test_shorted_original_nets_rejected(self):
        raw = copy.deepcopy(self.raw)
        raw['resistors'].append(('a1', 'b1', 1.))
        with self.assertRaisesRegex(ValueError, 'shorts'):
            conserve(self.reference, raw, self.text, 'VSS')

    def test_unconnected_split_node_rejected(self):
        raw = copy.deepcopy(self.raw)
        raw['resistors'].pop()
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            conserve(self.reference, raw, self.text, 'VSS')

    def test_changed_text_order_rejected(self):
        with self.assertRaisesRegex(ValueError, 'ordering'):
            conserve(self.reference, self.raw, self.text.replace('C0 A B', 'C0 A VSS'), 'VSS')


class InventoryTests(unittest.TestCase):
    def test_hv_fingers_keep_their_process_geometry(self):
        text = ('.subckt ExtractedAnalog VDD VSS A Y\n'
                'X0 Y A VSS VSS sg13_hv_nmos w=10u l=0.45u\n'
                'X1 Y A VDD VDD sg13_hv_pmos w=10u l=0.45u\n.ends\n')
        ports = ['VDD', 'VSS', 'A', 'Y']
        result = inventory(text, ports)
        self.assertEqual(result['models'], dict(sg13_hv_nmos=1, sg13_hv_pmos=1))
        for broken in (text.replace('w=10u', 'w=20u', 1),
                       text.replace('l=0.45u', 'l=0.13u', 1),
                       text.replace('sg13_hv_nmos', 'unreviewed_hv_nmos')):
            with self.subTest(netlist=broken), self.assertRaises(ValueError):
                inventory(broken, ports)

    def test_wrapped_ports_and_actual_passive_families(self):
        text = ('.subckt ExtractedAnalog VDD VSS\n+ A B\n'
                'X0 A B VSS rppd w=2u l=76.89u\n'
                'X1 A B VSS rhigh w=2u\n+ l=0.23m\n'
                'X2 VSS A dantenna w=0.78u l=0.78u\n.ends\n')
        result = inventory(text, ['VDD', 'VSS', 'A', 'B'])
        self.assertEqual(result['models'], dict(rppd=1, rhigh=1, dantenna=1))
        self.assertAlmostEqual(result['geometry'][1]['length_m'], 230e-6)
        # Device resistors are not wire edges: collapsing them would short
        # the intended ladder during capacitance conservation.
        self.assertEqual(result['resistors'], [])
        for broken in (text.replace('A B VSS rppd', 'A B rppd'),
                       text.replace('rhigh', 'unbound_resistor'),
                       text.replace('l=0.23m', 'l=0.23m w=2u'), '+ A B\n' + text):
            with self.assertRaises(ValueError):
                inventory(broken, ['VDD', 'VSS', 'A', 'B'])

    def test_actual_finger_dimensions_and_port_order(self):
        text = ('.subckt ExtractedAnalog D G S B\n'
                'X0 D G S B sg13_lv_nmos w=10u l=0.13u\nC0 D S 0\n.ends\n')
        self.assertEqual(inventory(text, ['D', 'G', 'S', 'B'])['models'], {'sg13_lv_nmos': 1})
        for old, new in [('w=10u', 'w=40u'), ('l=0.13u', 'l=0.10u'),
                         ('C0 D S 0', 'C0 D S -1f'), ('D G S B\n', 'G D S B\n')]:
            with self.subTest(new=new), self.assertRaises(ValueError):
                inventory(text.replace(old, new), ['D', 'G', 'S', 'B'])


if __name__ == '__main__':
    unittest.main()

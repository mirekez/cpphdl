"""Unused hierarchy-port promotion must not mask a missing electrical connection."""
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from embedded_sram_ports import restore_unused_outputs, output_labels
from sram_lvs_audit import audit

SPICE = '''.SUBCKT INV A Y VDD VSS
MN Y A VSS VSS sg13_lv_nmos L=0.13u W=0.3u
MP Y A VDD VDD sg13_lv_pmos L=0.13u W=0.6u
.ENDS
.SUBCKT SRAM A A_DOUT<0> VDD VSS
XINV A A_DOUT<0> VDD VSS INV
.ENDS
.SUBCKT CHIP A VDD VSS
XBANK0 A UNUSED0 VDD VSS SRAM
XBANK1 A UNUSED1 VDD VSS SRAM
.ENDS
'''


class Evidence:
    def __init__(self, left, right):
        self.layout, self.reference = left, right
        self.matches = db.NetlistCrossReference()

    def netlist(self):
        return self.layout

    def xref(self):
        return self.matches


class UnusedOutputPorts(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        reference = self.root / 'reference.cir'
        reference.write_text(SPICE)
        layout = self.root / 'layout.cir'
        layout.write_text(SPICE.replace('SRAM A A_DOUT<0> VDD VSS', 'SRAM A VDD VSS')
                          .replace(' A UNUSED0 VDD VSS SRAM', ' A VDD VSS SRAM')
                          .replace(' A UNUSED1 VDD VSS SRAM', ' A VDD VSS SRAM'))
        left, right = db.Netlist(), db.Netlist()
        left.read(str(layout), db.NetlistSpiceReader())
        right.read(str(reference), db.NetlistSpiceReader())
        self.evidence = Evidence(left, right)

    def restore(self, labels=None):
        return restore_unused_outputs(self.evidence, 'SRAM', {'A_DOUT<0>'} if labels is None else labels, 2)

    def test_promoted_interface_passes_complete_independent_audit(self):
        result = self.restore()
        self.assertEqual(result['private_parent_stubs'], 2)
        self.assertFalse(result['qualified'])
        self.assertTrue(db.NetlistComparer().compare(self.evidence.layout, self.evidence.reference,
                                                   self.evidence.matches))
        self.assertEqual(audit(self.evidence, 'CHIP', {'A', 'VDD', 'VSS'})['status'], 'pass')

    def test_used_reference_output_rejected_without_mutation(self):
        chip = self.evidence.reference.circuit_by_name('CHIP')
        bank = next(chip.each_subcircuit())
        bank.connect_pin(bank.circuit_ref().pin_by_name('A_DOUT<0>').id(), chip.net_by_name('A'))
        with self.assertRaisesRegex(ValueError, 'external connection'):
            self.restore()
        self.assertIsNone(self.evidence.layout.circuit_by_name('SRAM').pin_by_name('A_DOUT<0>'))

    def test_absent_physical_label_rejected(self):
        with self.assertRaisesRegex(ValueError, 'physically labeled'):
            self.restore(set())

    def test_missing_internal_driver_rejected(self):
        sram = self.evidence.layout.circuit_by_name('SRAM')
        child = next(sram.each_subcircuit())
        child.connect_pin(child.circuit_ref().pin_by_name('Y').id(), sram.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'internal driver'):
            self.restore()

    def test_shared_reference_stub_rejected(self):
        chip = self.evidence.reference.circuit_by_name('CHIP')
        banks = list(chip.each_subcircuit())
        pin = banks[0].circuit_ref().pin_by_name('A_DOUT<0>').id()
        banks[1].connect_pin(pin, banks[0].net_for_pin(pin))
        with self.assertRaisesRegex(ValueError, 'external connection'):
            self.restore()

    def test_internal_device_defect_survives_promotion(self):
        inv = self.evidence.layout.circuit_by_name('INV')
        next(inv.each_device()).connect_terminal('G', inv.net_by_name('VSS'))
        self.restore()
        self.assertFalse(db.NetlistComparer().compare(self.evidence.layout, self.evidence.reference,
                                                    self.evidence.matches))
        with self.assertRaises(ValueError):
            audit(self.evidence, 'CHIP', {'A', 'VDD', 'VSS'})

    def test_exact_physical_output_label_set(self):
        layout = db.Layout()
        cell = layout.create_cell('SRAM')
        layer = layout.layer(10, 25)
        for port in 'AB':
            for bit in range(16):
                cell.shapes(layer).insert(db.Text(f'{port}_DOUT<{bit}>', db.Trans(bit, 0)))
        path = self.root / 'labels.gds'
        layout.write(str(path))
        self.assertEqual(len(output_labels(path, 'SRAM')), 32)
        cell.shapes(layer).insert(db.Text('A_DOUT<0>', db.Trans()))
        layout.write(str(path))
        with self.assertRaisesRegex(ValueError, 'interface differs'):
            output_labels(path, 'SRAM')


if __name__ == '__main__':
    unittest.main()

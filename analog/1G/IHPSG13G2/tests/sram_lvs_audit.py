"""Reject stale LVS matches whose circuit endpoints or parameters were changed."""
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sram_lvs_audit import audit

SPICE = '''* Independent graph-audit fixture
.SUBCKT INV A Y VDD VSS
MN Y A VSS VSS sg13_lv_nmos L=0.13u W=0.3u
MP Y A VDD VDD sg13_lv_pmos L=0.13u W=0.6u
.ENDS
.SUBCKT TOP A Y VDD VSS
X1 A MID VDD VSS INV
X2 MID DRIVER VDD VSS INV
RW DRIVER Y 0.264 res_metal2 L=0.6u W=0.2u
.ENDS
'''
PORTS = {'A', 'Y', 'VDD', 'VSS'}


class Evidence:
    def __init__(self, path):
        self.layout, self.reference = db.Netlist(), db.Netlist()
        for nl in [self.layout, self.reference]:
            nl.read(str(path), db.NetlistSpiceReader())
        self.matches = db.NetlistCrossReference()
        if not db.NetlistComparer().compare(self.layout, self.reference, self.matches):
            raise ValueError('fixture failed initial comparison')

    def netlist(self):
        return self.layout

    def xref(self):
        return self.matches


class AuditGuards(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'fixture.cir'
        self.path.write_text(SPICE)
        self.evidence = Evidence(self.path)

    def check(self):
        return audit(self.evidence, 'TOP', PORTS)

    def test_complete_hierarchy(self):
        result = self.check()
        self.assertEqual(result['status'], 'pass')
        self.assertEqual(result['counts']['devices'], 3)
        self.assertEqual(result['counts']['subcircuits'], 2)
        self.assertFalse(result['full_chip_qualified'])

    def test_gate_changed_after_match(self):
        c = self.evidence.layout.circuit_by_name('INV')
        next(c.each_device()).connect_terminal('G', c.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'fixed terminal'):
            self.check()

    def replace_layout_snapshot(self):
        self.original_layout = self.evidence.layout
        self.evidence.layout = self.original_layout.dup()

    def test_equivalent_snapshot(self):
        self.replace_layout_snapshot()
        self.assertEqual(self.check()['status'], 'pass')

    def test_changed_snapshot_gate_rejected(self):
        self.replace_layout_snapshot()
        c = self.evidence.layout.circuit_by_name('INV')
        next(c.each_device()).connect_terminal('G', c.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'fixed terminal'):
            self.check()

    def test_changed_snapshot_child_rejected(self):
        self.replace_layout_snapshot()
        c = self.evidence.layout.circuit_by_name('TOP')
        s = next(c.each_subcircuit())
        pin = s.circuit_ref().pin_by_name('A')
        s.connect_pin(pin.id(), c.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'subcircuit pin endpoint'):
            self.check()

    def test_changed_snapshot_port_rejected(self):
        self.replace_layout_snapshot()
        c = self.evidence.layout.circuit_by_name('TOP')
        c.rename_pin(c.pin_by_name('A').id(), 'WRONG')
        with self.assertRaisesRegex(ValueError, 'top port names'):
            self.check()

    def test_size_changed_after_match(self):
        d = next(self.evidence.layout.circuit_by_name('INV').each_device())
        d.set_parameter('W', d.parameter('W') * 2)
        with self.assertRaisesRegex(ValueError, 'parameter'):
            self.check()

    def test_hierarchy_endpoint_changed_after_match(self):
        c = self.evidence.layout.circuit_by_name('TOP')
        s = next(c.each_subcircuit())
        pin = s.circuit_ref().pin_by_name('A')
        s.connect_pin(pin.id(), c.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'subcircuit pin endpoint'):
            self.check()

    def test_extra_net_is_not_ignored(self):
        self.evidence.layout.circuit_by_name('INV').create_net('EXTRA')
        with self.assertRaisesRegex(ValueError, 'incomplete net'):
            self.check()

    def test_top_pin_name_changed(self):
        c = self.evidence.layout.circuit_by_name('TOP')
        c.rename_pin(c.pin_by_name('A').id(), 'WRONG')
        with self.assertRaisesRegex(ValueError, 'top port names'):
            self.check()

    def test_source_drain_symmetry(self):
        d = next(self.evidence.layout.circuit_by_name('INV').each_device())
        source, drain = d.net_for_terminal('S'), d.net_for_terminal('D')
        d.connect_terminal('S', drain)
        d.connect_terminal('D', source)
        self.assertEqual(self.check()['status'], 'pass')

    def test_body_changed_after_match(self):
        c = self.evidence.layout.circuit_by_name('INV')
        next(c.each_device()).connect_terminal('B', c.net_by_name('Y'))
        with self.assertRaisesRegex(ValueError, 'fixed terminal'):
            self.check()

    def test_resistor_changed_after_match(self):
        d = next(self.evidence.layout.circuit_by_name('TOP').each_device())
        d.set_parameter('R', d.parameter('R') * 2)
        with self.assertRaisesRegex(ValueError, 'parameter'):
            self.check()

    def test_lost_resistance_on_both_sides_rejected(self):
        for nl in [self.evidence.layout, self.evidence.reference]:
            next(nl.circuit_by_name('TOP').each_device()).set_parameter('R', 0)
        with self.assertRaisesRegex(ValueError, 'positive extracted metal resistance'):
            self.check()

    def test_negative_extracted_resistance_rejected(self):
        next(self.evidence.layout.circuit_by_name('TOP').each_device()).set_parameter('R', -1)
        with self.assertRaisesRegex(ValueError, 'positive extracted metal resistance'):
            self.check()

    def test_resistor_symmetry(self):
        d = next(self.evidence.layout.circuit_by_name('TOP').each_device())
        a, b = d.net_for_terminal('A'), d.net_for_terminal('B')
        d.connect_terminal('A', b)
        d.connect_terminal('B', a)
        self.assertEqual(self.check()['status'], 'pass')

    def test_top_pin_endpoint_changed(self):
        c = self.evidence.layout.circuit_by_name('TOP')
        c.connect_pin(c.pin_by_name('Y').id(), c.net_by_name('VSS'))
        with self.assertRaisesRegex(ValueError, 'pin endpoint'):
            self.check()

    def test_colliding_net_names_rejected(self):
        c = self.evidence.layout.circuit_by_name('TOP')
        c.net_by_name('MID').name = 'DRIVER'
        with self.assertRaisesRegex(ValueError, 'ambiguous net'):
            self.check()

    def geometry_defined_resistor(self):
        d = next(self.evidence.reference.circuit_by_name('TOP').each_device())
        d.set_parameter('R', 0)
        dc = self.evidence.layout.device_class_by_name('RES_METAL2')
        dc.enable_parameter('R', False)
        dc.enable_parameter('L', True)
        dc.enable_parameter('W', True)

    def test_geometry_defined_resistance(self):
        self.geometry_defined_resistor()
        result = audit(self.evidence, 'TOP', PORTS,
                       metal_sheet_resistance={'res_metal2': .088})
        self.assertEqual(result['counts']['derived_metal_resistors'], 1)

    def test_geometry_defined_resistance_needs_model(self):
        self.geometry_defined_resistor()
        with self.assertRaisesRegex(ValueError, 'valid extraction model'):
            self.check()

    def test_wrong_sheet_resistance_rejected(self):
        self.geometry_defined_resistor()
        with self.assertRaisesRegex(ValueError, 'parameter'):
            audit(self.evidence, 'TOP', PORTS,
                  metal_sheet_resistance={'res_metal2': .09})


if __name__ == '__main__':
    unittest.main()

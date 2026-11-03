"""Embedded SRAM matching must preserve the surrounding circuit and its ports."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from embedded_sram_lvs import EmbeddedSramCorrespondence
from flow_control_sram_lvs import combine_hashes, copy_extraction_deck, physical_signature
from sram_lvs_audit import audit
import klayout.db as db

spec = importlib.util.spec_from_file_location('comparison_fixture', Path(__file__).with_name('sram_lvs_compare.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
SPICE = fixture.SPICE + '''
.SUBCKT CHIP INPUT OUTPUT VDD VSS
XBANK0 INPUT MIDDLE VDD VSS TOP
XBANK1 MIDDLE OUTPUT VDD VSS TOP
.ENDS
'''
PORTS = {'INPUT', 'OUTPUT', 'VDD', 'VSS'}


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / 'assembly.cir'
        path.write_text(SPICE)
        self.evidence = fixture.ComparableEvidence(path)
        self.engine = EmbeddedSramCorrespondence(self.evidence, 'TOP', 'CHIP')

    def run_assembly(self):
        return self.engine.run_assembly(fixture.PORTS, PORTS, {'res_metal2': .088})

    def test_complete_hierarchy_including_parent(self):
        result = self.run_assembly()
        self.assertEqual(result['status'], 'assembly_graph_correspondence_pass')
        self.assertEqual(result['assembly_audit']['counts']['circuits'], 3)
        self.assertEqual(result['assembly_audit']['counts']['subcircuits'], 4)
        self.assertFalse(result['full_chip_qualified'])

    def test_changed_bank_connection_is_rejected(self):
        circuit = self.evidence.layout.circuit_by_name('CHIP')
        child = next(circuit.each_subcircuit())
        child.connect_pin(child.circuit_ref().pin_by_name('A').id(), circuit.net_by_name('VSS'))
        with self.assertRaises(ValueError):
            self.run_assembly()

    def test_wrong_parent_interface_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'interface differs'):
            self.engine.run_assembly(fixture.PORTS, {'INPUT', 'WRONG', 'VDD', 'VSS'}, {'res_metal2': .088})

    def test_changed_sram_device_is_rejected(self):
        circuit = self.evidence.layout.circuit_by_name('INV')
        next(circuit.each_device()).connect_terminal('G', circuit.net_by_name('VSS'))
        with self.assertRaises(ValueError):
            self.run_assembly()

    def test_unrelated_parent_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'does not instantiate'):
            EmbeddedSramCorrespondence(self.evidence, 'TOP', 'INV')


class ExtractionPackaging(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.fixture = Path(directory.name) / 'fixture'
        self.output = Path(directory.name) / 'output'
        self.deck = self.fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
        self.deck.mkdir(parents=True)
        self.alignment = Path(__file__).resolve().parents[1] / 'sram_lvs_alignment.rb'
        (self.deck / 'sg13g2.lvs').write_text(self.alignment.read_text())

    def test_runner_version_file_survives_isolation(self):
        versions = b'klayout: 0.30.12\n'
        (self.fixture / 'pdk/versions.txt').write_bytes(versions)
        deck = copy_extraction_deck(self.fixture, self.output)
        # The PDK runner searches relative to this exact hierarchy root.
        self.assertEqual((deck.parents[4] / 'versions.txt').read_bytes(), versions)
        self.assertIn('visit_sram.call(root)', (deck / 'sg13g2.lvs').read_text())

    def test_missing_version_fails_before_creating_partial_deck(self):
        with self.assertRaisesRegex(ValueError, 'versions.txt'):
            copy_extraction_deck(self.fixture, self.output)
        self.assertFalse(self.output.exists())


class GeometryGuards(unittest.TestCase):
    def setUp(self):
        self.layout = db.Layout()
        self.top = self.layout.create_cell('TOP')
        self.child = self.layout.create_cell('CHILD')
        self.layer = self.layout.layer(8, 0)
        self.child.shapes(self.layer).insert(db.Box(0, 0, 20, 20))
        self.top.insert(db.CellInstArray(self.child.cell_index(), db.Trans(),
                                        db.Vector(50, 0), db.Vector(0, 50), 2, 2))
        self.before = physical_signature(self.layout)

    def test_names_and_internal_labels_can_change(self):
        self.child.name = 'REFERENCE_NAME'
        self.child.shapes(self.layout.layer(8, 25)).insert(db.Text('LOCAL', db.Trans()))
        # Adding a new text-only layer also changes the layer inventory. Use
        # the same physical layer inventory when checking label removal.
        before = physical_signature(self.layout)
        self.child.shapes(self.layout.layer(8, 25)).clear()
        self.assertEqual(before, physical_signature(self.layout))

    def test_changed_conductor_is_rejected(self):
        self.child.shapes(self.layer).clear()
        self.child.shapes(self.layer).insert(db.Box(0, 0, 21, 20))
        self.assertNotEqual(self.before, physical_signature(self.layout))

    def test_changed_array_placement_is_rejected(self):
        instance = next(self.top.each_inst())
        instance.trans = db.Trans(1, 0)
        self.assertNotEqual(self.before, physical_signature(self.layout))

    def test_changed_array_count_is_rejected(self):
        instance = next(self.top.each_inst())
        instance.na = 3
        self.assertNotEqual(self.before, physical_signature(self.layout))

    def test_conflicting_receipts_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'conflicting assembly provenance'):
            combine_hashes({'layout': 'first'}, {'layout': 'second'})


class AntennaDiodeGuards(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / 'diodes.cir'
        path.write_text(SPICE.replace('XBANK0 INPUT',
            'D0 VSS INPUT dantenna\nD1 INPUT VDD dpantenna\nXBANK0 INPUT'))
        self.evidence = fixture.ComparableEvidence(path)
        for netlist in (self.evidence.layout, self.evidence.reference):
            for device_class in netlist.each_device_class():
                if device_class.name.lower() in ('dantenna', 'dpantenna'):
                    device_class.add_parameter(db.DeviceParameterDefinition('m', 'multiplier', 1, True))
            for device in netlist.circuit_by_name('CHIP').each_device():
                device.set_parameter('A', .6084)
                device.set_parameter('P', 3.12)
                device.set_parameter('m', 1)

    def check(self, enabled=True):
        return audit(self.evidence, 'CHIP', PORTS, metal_sheet_resistance={'res_metal2': .088},
                     allow_antenna_diodes=enabled)

    def diode(self):
        return next(self.evidence.layout.circuit_by_name('CHIP').each_device())

    def test_complete_assembly_with_both_diode_models(self):
        engine = EmbeddedSramCorrespondence(self.evidence, 'TOP', 'CHIP')
        result = engine.run_assembly(fixture.PORTS, PORTS, {'res_metal2': .088})
        self.assertEqual(result['assembly_audit']['counts']['antenna_diodes'], 2)

    def test_standalone_sram_does_not_accept_diodes(self):
        with self.assertRaisesRegex(ValueError, 'unsupported SRAM model'):
            self.check(False)

    def test_reversed_polarity_is_rejected(self):
        device = self.diode()
        anode, cathode = device.net_for_terminal('A'), device.net_for_terminal('C')
        device.connect_terminal('A', cathode)
        device.connect_terminal('C', anode)
        with self.assertRaisesRegex(ValueError, 'fixed terminal'):
            self.check()

    def test_geometry_outside_foundry_tolerance_is_rejected(self):
        for parameter in ('A', 'P'):
            with self.subTest(parameter=parameter):
                device = self.diode()
                original = device.parameter(parameter)
                device.set_parameter(parameter, original * 1.02)
                with self.assertRaisesRegex(ValueError, 'parameter differs'):
                    self.check()
                device.set_parameter(parameter, original)

    def test_geometry_within_foundry_tolerance(self):
        self.diode().set_parameter('A', .6084 * 1.005)
        self.assertEqual(self.check()['status'], 'pass')

    def test_multiplicity_must_match(self):
        self.diode().set_parameter('m', 2)
        with self.assertRaisesRegex(ValueError, 'parameter differs'):
            self.check()

    def test_missing_geometry_is_rejected(self):
        self.diode().set_parameter('A', 0)
        with self.assertRaisesRegex(ValueError, 'positive antenna diode'):
            self.check()


if __name__ == '__main__':
    unittest.main()

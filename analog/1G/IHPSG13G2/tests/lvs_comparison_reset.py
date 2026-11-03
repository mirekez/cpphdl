"""Rebuild comparison evidence without modifying native extracted circuits."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lvs_comparison_reset import reset_comparison
from sram_lvs_compare import Correspondence

spec = importlib.util.spec_from_file_location('audit_fixture', Path(__file__).with_name('sram_lvs_audit.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class NativeComparisonReset(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        spice = self.root / 'fixture.cir'
        spice.write_text(fixture.SPICE)
        self.database = db.LayoutVsSchematic('TOP', .001)
        self.database.extract_netlist()
        self.layout = self.database.netlist()
        self.layout.read(str(spice), db.NetlistSpiceReader())
        self.reference = db.Netlist()
        self.reference.read(str(spice), db.NetlistSpiceReader())
        self.database.reference = self.reference
        self.assertTrue(self.database.compare(db.NetlistComparer()))
        self.source, self.output = self.root / 'source.lvsdb', self.root / 'reset.lvsdb'
        self.database.write(str(self.source), True)

    def reload(self):
        result = reset_comparison(self.source, self.output)
        original = self.source.read_bytes()
        prefix = original[:original.index(b'\nZ(\n') + 1]
        self.assertEqual(self.output.read_bytes(), prefix + b'Z(\n)\n')
        self.assertEqual(result['retained_sha256'], hashlib.sha256(prefix).hexdigest())
        self.assertEqual(result['source_sha256'], hashlib.sha256(original).hexdigest())
        self.assertFalse(result['qualified'])
        loaded = db.LayoutVsSchematic()
        loaded.read(str(self.output))
        self.assertEqual(list(loaded.xref().each_circuit_pair()), [])
        return loaded

    def test_recomparison_and_independent_audit(self):
        loaded = self.reload()
        engine = Correspondence(loaded, 'TOP')
        self.assertTrue(engine.compare([]))
        self.assertEqual(engine.run(fixture.PORTS, {'res_metal2': .088})['status'],
                         'graph_correspondence_pass')

    def test_malformed_provisional_record_is_not_needed(self):
        data = self.source.read_bytes()
        self.source.write_bytes(data[:data.index(b'\nZ(\n') + 1] + b'Z(\n X(TOP TOP X\n  Z(\n   N(23462)\n  )\n )\n)\n')
        with self.assertRaises(RuntimeError):
            db.LayoutVsSchematic().read(str(self.source))
        engine = Correspondence(self.reload(), 'TOP')
        self.assertTrue(engine.compare([]))

    def test_circuit_defect_survives_reset_and_fails(self):
        child = self.layout.circuit_by_name('INV')
        next(child.each_device()).connect_terminal('G', child.net_by_name('VSS'))
        # Intentionally retain the stale successful comparison from setUp.
        self.database.write(str(self.source), True)
        engine = Correspondence(self.reload(), 'TOP')
        self.assertFalse(engine.compare([]))
        with self.assertRaises(ValueError):
            engine.run(fixture.PORTS, {'res_metal2': .088})

    def test_parameter_defect_survives_reset_and_fails(self):
        next(self.layout.circuit_by_name('TOP').each_device()).set_parameter('R', 0)
        self.database.write(str(self.source), True)
        engine = Correspondence(self.reload(), 'TOP')
        engine.compare([])
        with self.assertRaises(ValueError):
            engine.run(fixture.PORTS, {'res_metal2': .088})

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            reset_comparison(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), b'keep')

    def test_missing_reference_is_rejected(self):
        self.source.write_bytes(b'#%lvsdb-klayout\nJ(\n)\nZ(\n)\n')
        with self.assertRaises(ValueError):
            reset_comparison(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_malformed_circuit_data_is_not_repaired(self):
        self.source.write_bytes(b'#%lvsdb-klayout\nJ(\n bad(\n)\nH(\n)\nZ(\n)\n')
        reset_comparison(self.source, self.output)
        with self.assertRaises(RuntimeError):
            db.LayoutVsSchematic().read(str(self.output))


if __name__ == '__main__':
    unittest.main()

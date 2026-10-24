"""Area reports must reject stale functionality, unknown cells and altered maps."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('checked_cell_area', Path(__file__).resolve().parents[1] / 'mapped_area.py')
area = importlib.util.module_from_spec(spec)
spec.loader.exec_module(area)


class Evidence(unittest.TestCase):
    def test_inventory_area_and_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source = work / 'source.cpp'
            source.write_text('int intended_circuit;\n')
            netlist = work / 'mapped.v'
            netlist.write_text('module Unit(A, Y);\n  sg13g2_inv_1 I0 (.A(A), .Y(n));\n'
                               '  sg13g2_buf_1 I1 (.A(n), .X(Y));\nendmodule\n')
            liberty = work / 'cells.lib'
            liberty.write_text('library(test) {\ncell(sg13g2_inv_1) { area: 2.5; }\n'
                               'cell("sg13g2_buf_1") { area: 4.0; }\n}\n')
            evidence = dict(status='pass', module='Unit', native_mapped_exact=True,
                mapped_sha256=area.digest(netlist), source_sha256={str(source): area.digest(source)},
                cells={'sg13g2_inv_1': 1, 'sg13g2_buf_1': 1})
            proof = work / 'hardware_checks.json'
            proof.write_text(json.dumps(evidence))
            result = area.estimate(work, liberty)
            self.assertEqual(result['cell_count'], 2)
            self.assertEqual(result['mapped_cell_area_um2'], 6.5)
            original_netlist = netlist.read_text()
            original_parser = area.library_areas
            def concurrent_producer(text):
                netlist.write_text(original_netlist + '\n')
                return original_parser(text)
            with patch.object(area, 'library_areas', concurrent_producer):
                with self.assertRaises(ValueError):
                    area.estimate(work, liberty)
            netlist.write_text(original_netlist)
            lef = work / 'cells.lef'
            lef.write_text('MACRO sg13g2_inv_1\nSIZE 1 BY 3 ;\nEND sg13g2_inv_1\n'
                           'MACRO sg13g2_buf_1\nSIZE 1 BY 4 ;\nEND sg13g2_buf_1\n')
            footprint = area.estimate(work, liberty, lef)
            self.assertEqual(footprint['mapped_placement_area_um2'], 7)
            self.assertEqual(footprint['liberty_lef_difference_um2'], .5)
            lef.write_text('MACRO sg13g2_inv_1\nSIZE 1 BY 3 ;\nEND sg13g2_inv_1\n')
            with self.assertRaises(ValueError):
                area.estimate(work, liberty, lef)
            for key, value in (('status', 'fail'), ('native_mapped_exact', False),
                               ('mapped_sha256', '0' * 64), ('cells', {'sg13g2_inv_1': 1})):
                proof.write_text(json.dumps(dict(evidence, **{key: value})))
                with self.assertRaises(ValueError):
                    area.estimate(work, liberty)
            proof.write_text(json.dumps(evidence))
            original = source.read_text()
            source.write_text('changed circuit')
            with self.assertRaises(ValueError):
                area.estimate(work, liberty)
            source.write_text(original)
            liberty.write_text('cell(sg13g2_inv_1) { area: 2.5; }')
            with self.assertRaises(ValueError):
                area.estimate(work, liberty)

    def test_invalid_area(self):
        for body in ('', 'area: 0;', 'area: -1;', 'area: 2; area: 3;'):
            with self.assertRaises(ValueError):
                area.library_areas('cell(gate) {' + body + '}')
        for dimensions in ('0 BY 3', '-1 BY 3', '1e999 BY 3'):
            with self.assertRaises(ValueError):
                area.lef_areas('MACRO gate\nSIZE ' + dimensions + ' ;\nEND gate\n')


if __name__ == '__main__':
    unittest.main()

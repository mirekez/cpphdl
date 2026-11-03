"""Preserve PDK placeholders without accepting missing physical masters."""
import argparse
from pathlib import Path
import sys
import struct
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from flow_control_stream import (MACRO, SRAM_LEGACY_PIN_PORTS, copy_sram_pin_labels,
                                 check_sram_placement_interface, library_empty_cells,
                                 validate_physical_cells)


class LegacyPinTests(unittest.TestCase):
    def setUp(self):
        self.layout = db.Layout()
        self.layout.read(str(args.pdk / ('ihp-sg13g2/libs.ref/sg13g2_sram/gds/' + MACRO + '.gds')))

    def shape_snapshot(self, include_texts):
        return {
            (cell.name, str(self.layout.get_info(layer))): sorted(
                str(shape) for shape in cell.shapes(layer).each()
                if include_texts or not shape.is_text())
            for cell in self.layout.each_cell() for layer in self.layout.layer_indexes()
            if any(include_texts or not shape.is_text() for shape in cell.shapes(layer).each())
        }

    def test_real_library_preserves_geometry_and_is_idempotent(self):
        geometry = self.shape_snapshot(False)
        pins = {name: sorted(str(shape) for shape in self.layout.cell(name).shapes(
            self.layout.layer(8, 2)).each()) for name in SRAM_LEGACY_PIN_PORTS}
        instances = {cell.name: sorted(str(inst) for inst in cell.each_inst())
                     for cell in self.layout.each_cell()}
        self.assertEqual(copy_sram_pin_labels(self.layout)['copied_labels'], 12)
        self.assertEqual(self.shape_snapshot(False), geometry)
        self.assertEqual({cell.name: sorted(str(inst) for inst in cell.each_inst())
                          for cell in self.layout.each_cell()}, instances)
        for name, original in pins.items():
            self.assertEqual(sorted(str(shape) for shape in self.layout.cell(name).shapes(
                self.layout.layer(8, 2)).each()), original)
        first = self.shape_snapshot(True)
        self.assertEqual(copy_sram_pin_labels(self.layout)['copied_labels'], 0)
        self.assertEqual(self.shape_snapshot(True), first)

    def test_invalid_later_cell_does_not_partially_mutate_library(self):
        cell = self.layout.cell('RSC_IHPSG13_FILLCAP8')
        cell.shapes(self.layout.layer(8, 2)).clear()
        before = self.shape_snapshot(True)
        with self.assertRaisesRegex(ValueError, 'unexpected legacy SRAM pin interface'):
            copy_sram_pin_labels(self.layout)
        self.assertEqual(self.shape_snapshot(True), before)

    def test_label_outside_conductor_is_rejected(self):
        cell = self.layout.cell('RSC_IHPSG13_CINVX2')
        shape = next(s for s in cell.shapes(self.layout.layer(8, 2)).each()
                     if s.is_text() and s.text.string == 'A')
        shape.text = db.Text('A', db.Trans(1000000, 1000000))
        with self.assertRaisesRegex(ValueError, 'pin misses Metal1'):
            copy_sram_pin_labels(self.layout)

    def test_conflicting_destination_is_rejected(self):
        cell = self.layout.cell('RSC_IHPSG13_CINVX2')
        pin = next(s.text for s in cell.shapes(self.layout.layer(8, 2)).each()
                   if s.is_text() and s.text.string == 'A')
        pin.string = 'Z'
        cell.shapes(self.layout.layer(8, 25)).insert(pin)
        with self.assertRaisesRegex(ValueError, 'conflicting SRAM pin label'):
            copy_sram_pin_labels(self.layout)

    def test_duplicate_destination_is_rejected(self):
        copy_sram_pin_labels(self.layout)
        cell = self.layout.cell('RSC_IHPSG13_CINVX2')
        shapes = cell.shapes(self.layout.layer(8, 25))
        shapes.insert(next(s.text for s in shapes.each() if s.is_text()))
        with self.assertRaisesRegex(ValueError, 'conflicting SRAM pin label'):
            copy_sram_pin_labels(self.layout)

    def test_missing_cell_is_rejected(self):
        self.layout.cell('RSC_IHPSG13_CINVX2').name = 'unexpected_replacement'
        with self.assertRaisesRegex(ValueError, 'missing legacy SRAM cell'):
            copy_sram_pin_labels(self.layout)


class PhysicalLibraryTests(unittest.TestCase):
    def test_replacement_must_preserve_macro_boundary_and_pin_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            original, replacement = (Path(directory) / name for name in ('original.gds', 'replacement.gds'))
            layout = db.Layout()
            root = layout.create_cell(MACRO)
            metal = layout.layer(8, 0)
            labels = layout.layer(8, 25)
            root.shapes(metal).insert(db.Box(0, 0, 100, 100))
            root.shapes(labels).insert(db.Text('A', db.Trans(20, 20)))
            layout.write(str(original))
            # An internal fill shape can change without moving the LEF interface.
            root.shapes(layout.layer(67, 0)).insert(db.Box(30, 30, 70, 70))
            layout.write(str(replacement))
            check_sram_placement_interface(original, replacement)
            for text in (db.Text('B', db.Trans(20, 20)), db.Text('A', db.Trans(21, 20))):
                root.shapes(labels).clear()
                root.shapes(labels).insert(text)
                layout.write(str(replacement))
                with self.assertRaisesRegex(ValueError, 'placement/pin interface'):
                    check_sram_placement_interface(original, replacement)
            root.shapes(labels).clear()
            root.shapes(labels).insert(db.Text('A', db.Trans(20, 20)))
            root.shapes(metal).insert(db.Box(0, 0, 101, 100))
            layout.write(str(replacement))
            with self.assertRaisesRegex(ValueError, 'placement/pin interface'):
                check_sram_placement_interface(original, replacement)

    def test_supplied_sram_placeholders(self):
        path = args.pdk / ('ihp-sg13g2/libs.ref/sg13g2_sram/gds/' + MACRO + '.gds')
        placeholders = library_empty_cells(path, MACRO)
        self.assertIn('RM_IHPSG13_2P_BITKIT_16x2_EDGE_TB', placeholders)
        library = db.Layout()
        library.read(str(path))
        validate_physical_cells(library, placeholders)
        # Losing real transistor geometry must still be rejected.
        victim = next(c for c in library.each_cell()
                      if not c.is_empty() and not list(c.each_inst()) and c.name != MACRO)
        victim.clear()
        with self.assertRaisesRegex(ValueError, 'missing physical GDS masters'):
            validate_physical_cells(library, placeholders)

    def test_unknown_empty_master(self):
        layout = db.Layout()
        layout.create_cell('missing_standard_cell')
        with self.assertRaisesRegex(ValueError, 'missing_standard_cell'):
            validate_physical_cells(layout, {'documented_helper'})

    def test_empty_top_is_not_a_placeholder(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'empty.gds'
            layout = db.Layout()
            layout.create_cell(MACRO)
            layout.write(str(path))
            with self.assertRaisesRegex(ValueError, 'missing physical library master'):
                library_empty_cells(path, MACRO)

    def test_undefined_child_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'missing.gds'
            layout = db.Layout()
            root = layout.create_cell(MACRO)
            root.shapes(layout.layer(8, 0)).insert(db.Box(0, 0, 100, 100))
            child = layout.create_cell('undefined_child')
            root.insert(db.CellInstArray(child.cell_index(), db.Trans()))
            layout.write(str(path))
            # Remove the child's GDS structure but retain the parent's SREF.
            data = path.read_bytes()
            output, structure = bytearray(), bytearray()
            offset, name = 0, None
            while offset < len(data):
                size, kind = struct.unpack_from('>HB', data, offset)
                record = data[offset:offset + size]
                offset += size
                if kind == 5:  # BGNSTR
                    structure = bytearray(record)
                elif kind == 6:  # STRNAME
                    name = record[4:].rstrip(b'\0').decode()
                    structure.extend(record)
                elif kind == 7:  # ENDSTR
                    structure.extend(record)
                    if name != 'undefined_child':
                        output.extend(structure)
                    structure, name = bytearray(), None
                elif structure:
                    structure.extend(record)
                else:
                    output.extend(record)
            path.write_bytes(output)
            with self.assertRaisesRegex(ValueError, 'undefined physical library masters'):
                library_empty_cells(path, MACRO)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--pdk', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])

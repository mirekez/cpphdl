#!/usr/bin/env python3
"""Test GDS-derived diffusion conservation and rejection of mismatched evidence."""
import argparse
import copy
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest
from input_charge import verify_files
from mos_junction_extraction import restore
from named_node_extraction import extract_named
from tool_process import configure_cleanup, memory_slot


class JunctionGuards(unittest.TestCase):
    def restore(self, text=None, geometry=None, coordinates=None):
        return restore(self.gds, self.geometry if geometry is None else geometry,
                       self.coordinates if coordinates is None else coordinates,
                       self.text if text is None else text)

    def test_independent_native_diffusion_totals(self):
        result = self.binding
        self.assertEqual(len(result['changes']), 7)
        self.assertEqual({c['component'] for c in result['changes']}, {'SamplerMOS5', 'SamplerMOS6'})
        for name, area in (('SamplerMOS5', 10.6), ('SamplerMOS6', 18.48)):
            c = result['components'][name]
            self.assertAlmostEqual(c['before_area_m2'] * 1e12, area / 2)
            self.assertAlmostEqual(c['after_area_m2'] * 1e12, area)
        for c in result['components'].values():
            self.assertAlmostEqual(c['after_area_m2'] * 1e12, c['gds_area_m2'] * 1e12)
            self.assertAlmostEqual(c['after_perimeter_m'] * 1e6, c['gds_perimeter_m'] * 1e6)

    def test_only_junction_parameters_change(self):
        before, after = self.text.splitlines(), self.corrected.splitlines()
        self.assertEqual(len(before), len(after))
        changed = {c['device'] for c in self.binding['changes']}
        for a, b in zip(before, after):
            f, g = a.split(), b.split()
            if f and f[0] in changed:
                self.assertEqual(f[:6], g[:6])
                old, new = (dict(item.split('=') for item in fields[6:]) for fields in (f, g))
                self.assertEqual({k: old[k] for k in ('w', 'l')}, {k: new[k] for k in ('w', 'l')})
                self.assertGreater(float(new['as']), 1e-50)
            else:
                self.assertEqual(a, b)

    def test_second_application_preserves_geometry(self):
        text, report = self.restore(text=self.corrected)
        self.assertEqual(text, self.corrected)
        self.assertEqual(report['changes'], [])

    def test_missing_duplicate_and_resized_mos_rejected(self):
        line = next(l for l in self.text.splitlines() if ' sg13_hv_nmos ' in l)
        for text in (self.text.replace(line + '\n', '', 1), self.text.replace(line, line + '\n' + line, 1),
                     self.text.replace(line, re.sub(r'\bl=[^ ]+', 'l=.44u', line), 1)):
            with self.subTest(text=text[:80]), self.assertRaises(ValueError):
                self.restore(text=text)

    def test_geometry_placement_and_channel_inventory_rejected(self):
        for field in ('origin', 'fingers'):
            geometry = copy.deepcopy(self.geometry)
            part = next(p for p in geometry['components'] if p['kind'] == 'mos')
            if field == 'origin':
                part['origin_um'][0] += .001
            else:
                part['geometry']['fingers'] += 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.restore(geometry=geometry)

    def test_missing_and_relocated_terminal_coordinates_rejected(self):
        line = next(l for l in self.text.splitlines() if ' sg13_hv_nmos ' in l)
        node = line.split()[1]
        encoded = self.coordinates.read_text()
        match = re.search(r'^rnode "' + re.escape(node) + r'".*$', encoded, re.M)
        self.assertIsNotNone(match)
        fields = match[0].split()
        # The parser records x,y at fields 4,5 (after name, R and C).
        self.assertEqual(fields[0], 'rnode')
        moved = fields[:]
        moved[4] = '999999999'
        variants = (encoded.replace(match[0] + '\n', '', 1),
                    encoded.replace(match[0], ' '.join(moved), 1),
                    encoded + match[0] + '\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'coordinates.ext'
            for text in variants:
                path.write_text(text)
                with self.subTest(text=text[-60:]), self.assertRaises(ValueError):
                    self.restore(coordinates=path)


def main(args):
    proof = json.loads(args.layout.read_text())
    if proof.get('status') != 'pass' or not proof.get('cases', {}).get('connected', {}).get('local_drc_pass'):
        raise ValueError('passing sampler layout required')
    verify_files(proof['source_sha256'])
    for case in proof['cases'].values():
        verify_files(case['artifact_sha256'])
    geometry = proof['cases']['connected']['geometry']
    gds = Path(proof['work']) / 'connected/sampler.gds'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    if args.extraction:
        extracted = json.loads(args.extraction.read_text())
    else:
        with memory_slot(['mos-junction-geometry'], exclusive=not args.concurrent):
            extracted = extract_named(gds, 'ReceiveSampler', ['VDD', 'VSS', 'IN', 'CLOCK', 'OUT'],
                args.pdk, args.magic, work / 'extraction', substrate='VSS')
    if extracted.get('status') != 'pass' or extracted['source_sha256'].get(str(gds)) != digest(gds):
        raise ValueError('extraction does not belong to verified layout')
    verify_files(extracted['source_sha256'])
    verify_files(extracted['artifact_sha256'])
    extraction = Path(extracted['work'])
    coordinates = extraction / 'named/ExtractedAnalog.res.ext'
    source = extraction / 'conserved.spice'
    if any(str(p) not in extracted['artifact_sha256'] for p in (coordinates, source)):
        raise ValueError('missing original junction artifact hashes')
    text = source.read_text()
    corrected, binding = restore(gds, geometry, coordinates, text)
    JunctionGuards.gds, JunctionGuards.geometry, JunctionGuards.coordinates = gds, geometry, coordinates
    JunctionGuards.text, JunctionGuards.corrected, JunctionGuards.binding = text, corrected, binding
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(JunctionGuards))
    output = work / 'junction-corrected.spice'
    output.write_text(corrected)
    paths = [SOURCE, args.layout.resolve(), source]
    if args.extraction:
        paths.append(args.extraction.resolve())
    binding.update(status='pass' if result.wasSuccessful() else 'fail', work=str(work),
        tests=result.testsRun, artifact_sha256={str(output): digest(output)}, electrical_qualified=False)
    binding['source_sha256'].update({str(p): digest(p) for p in paths})
    for path in (args.work / 'checks.json', work / 'checks.json'):
        path.write_text(json.dumps(binding, indent=2) + '\n')
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'work', 'pdk', 'magic'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--extraction', type=Path, help='reuse verified extraction instead of regenerating it')
    parser.add_argument('--concurrent', action='store_true', help='separately budgeted small extraction slot')
    main(parser.parse_args())

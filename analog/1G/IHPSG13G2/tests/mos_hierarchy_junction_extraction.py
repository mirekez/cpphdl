"""Guard hierarchical junction conservation using the actual HV storage replica."""
import argparse
import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest
from input_charge import verify_files
from mos_hierarchy_junction_extraction import restore_hierarchy
from named_node_extraction import extract_named
from tool_process import configure_cleanup, memory_slot


class HierarchyGuards(unittest.TestCase):
    def restore(self, macros=None, text=None, gds=None):
        with tempfile.TemporaryDirectory(dir=self.work) as directory:
            return restore_hierarchy(self.gds if gds is None else gds, self.macros if macros is None else macros,
                self.coordinates, self.text if text is None else text, Path(directory) / 'view')

    def test_independent_bank_diffusion_totals(self):
        self.assertEqual(self.binding['devices'], 2220)
        self.assertEqual(len(self.binding['changes']), 2200)
        self.assertEqual(sorted(self.binding['hierarchy_instances'].values()), [1, 220])
        self.assertGreater(self.binding['geometry_view_layers_equal'], 20)
        storage = [c for c in self.binding['components'].values()
                   if math.isclose(c['gds_area_m2'], 41e-12, rel_tol=1e-10, abs_tol=0)]
        self.assertEqual(len(storage), 220)
        for c in storage:
            self.assertAlmostEqual(c['before_area_m2'] * 1e12, 20.5)
            self.assertAlmostEqual(c['after_area_m2'] * 1e12, 41)
            self.assertAlmostEqual(c['after_perimeter_m'] * 1e6, 228.2)

    def test_only_junction_geometry_changes(self):
        before, after = self.text.splitlines(), self.corrected.splitlines()
        self.assertEqual(len(before), len(after))
        changed = {c['device'] for c in self.binding['changes']}
        for a, b in zip(before, after):
            f, g = a.split(), b.split()
            if f and f[0] in changed:
                self.assertEqual(f[:6], g[:6])
                old, new = (dict(item.split('=') for item in fields[6:]) for fields in (f, g))
                self.assertEqual([old[k] for k in ('w', 'l')], [new[k] for k in ('w', 'l')])
                self.assertGreater(float(new['as']), 1e-50)
                self.assertGreater(float(new['ad']), 1e-50)
            else:
                self.assertEqual(a, b)

    def test_idempotence(self):
        text, binding = self.restore(text=self.corrected)
        self.assertEqual(text, self.corrected)
        self.assertEqual(binding['changes'], [])

    def test_incorrect_instance_counts_rejected(self):
        for count in (0, True, 219, 221):
            macros = copy.deepcopy(self.macros)
            macros['ReplicaMOSStorageBank_mos']['instances'] = count
            with self.subTest(count=count), self.assertRaises(ValueError):
                self.restore(macros=macros)

    def test_incorrect_geometry_and_macro_selection_rejected(self):
        for field in ('cell', 'length'):
            macros = copy.deepcopy(self.macros)
            if field == 'cell':
                macros['AbsentMOS'] = macros.pop('ReplicaMOSStorageBank_mos')
            else:
                macros['ReplicaMOSStorageBank_mos']['geometry']['length_um'] = .49
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.restore(macros=macros)

    def test_missing_finger_rejected(self):
        line = next(l for l in self.text.splitlines() if ' sg13_hv_nmos ' in l and 'l=0.5u' in l)
        with self.assertRaises(ValueError):
            self.restore(text=self.text.replace(line + '\n', '', 1))

    def test_rotated_and_mirrored_macros_rejected(self):
        import klayout.db as db
        with tempfile.TemporaryDirectory(dir=self.work) as directory:
            for angle, mirrored in ((1, False), (0, True)):
                layout = db.Layout()
                layout.read(str(self.gds))
                instance, = [i for i in layout.top_cell().each_inst() if i.cell.name == 'ReplicaMOS']
                origin = instance.trans.disp
                instance.trans = db.Trans(angle, mirrored, origin.x, origin.y)
                path = Path(directory) / 'unsupported-placement.gds'
                layout.write(str(path))
                with self.subTest(angle=angle, mirrored=mirrored), self.assertRaises(ValueError):
                    self.restore(gds=path)


def main(args):
    proof = json.loads(args.layout.read_text())
    connected = proof.get('cases', {}).get('connected', {})
    if (proof.get('status') != 'pass' or connected.get('local_drc_violations') != 0 or
            not all(proof['cases'][name].get('missing_device_rejected')
                    for name in ('missing_storage', 'missing_replica'))):
        raise ValueError('verified replica geometry and missing-device rejection required')
    verify_files(proof['source_sha256'])
    for case in proof['cases'].values():
        verify_files(case['artifact_sha256'])
    macros = {}
    for part in connected['geometry']['components']:
        if part['kind'] == 'mos':
            macros[part['name']] = dict(geometry=part['geometry'], instances=1)
        elif part['geometry'].get('storage_model') == 'hv_gate':
            bank = part['geometry']['bank']
            macros[part['name'] + 'Bank_mos'] = dict(instances=bank['copies'], geometry=dict(
                model=bank['model'], total_width_um=bank['width_um'], length_um=bank['length_um'],
                fingers=bank['fingers'], finger_width_um=bank['width_um'] / bank['fingers']))
    gds = Path(proof['work']) / 'connected/replica.gds'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    if args.extraction:
        parent = json.loads(args.extraction.read_text())
        extraction = parent.get('extraction', parent)
    else:
        with memory_slot(['hierarchical-mos-junctions'], exclusive=not args.concurrent):
            extraction = extract_named(gds, 'TransmitTranslationBias', ['VDD', 'VSS', 'VLOGIC', 'BIAS'],
                args.pdk, args.magic, work / 'extraction', substrate='VSS')
    if extraction.get('status') != 'pass' or extraction['source_sha256'].get(str(gds)) != digest(gds):
        raise ValueError('extraction does not attest the original GDS')
    verify_files(extraction['source_sha256'])
    verify_files(extraction['artifact_sha256'])
    extracted = Path(extraction['work'])
    coordinates, source = extracted / 'named/ExtractedAnalog.res.ext', extracted / 'conserved.spice'
    if any(str(p) not in extraction['artifact_sha256'] for p in (coordinates, source)):
        raise ValueError('unattested original MOS coordinates or netlist')
    text = source.read_text()
    corrected, binding = restore_hierarchy(gds, macros, coordinates, text, work / 'view')
    for key, value in dict(gds=gds, macros=macros, coordinates=coordinates, text=text,
            corrected=corrected, binding=binding, work=work).items():
        setattr(HierarchyGuards, key, value)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(HierarchyGuards))
    output = work / 'junction-corrected.spice'
    output.write_text(corrected)
    hashes = binding['source_sha256']
    groups = [proof['source_sha256'], extraction['source_sha256'], extraction['artifact_sha256']]
    groups += [case['artifact_sha256'] for case in proof['cases'].values()]
    for group in groups:
        for path, value in group.items():
            if path in hashes and hashes[path] != value:
                raise ValueError('conflicting hierarchical junction evidence')
            hashes[path] = value
    paths = [SOURCE, args.layout.resolve(), source]
    if args.extraction:
        paths.append(args.extraction.resolve())
    hashes.update({str(p): digest(p) for p in paths})
    verify_files(hashes)
    binding.update(status='pass' if result.wasSuccessful() else 'fail', tests=result.testsRun,
        work=str(work), electrical_qualified=False)
    binding['artifact_sha256'][str(output)] = digest(output)
    for path in (work / 'checks.json', args.work / 'checks.json'):
        path.write_text(json.dumps(binding, indent=2) + '\n')
    if not result.wasSuccessful():
        raise SystemExit(1)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'work', 'pdk', 'magic'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--extraction', type=Path, help='reuse attested extraction instead of regenerating it')
    parser.add_argument('--concurrent', action='store_true', help='separately budgeted geometry/extraction slot')
    main(parser.parse_args())

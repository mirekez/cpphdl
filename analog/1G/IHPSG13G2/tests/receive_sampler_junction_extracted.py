#!/usr/bin/env python3
"""Run the sampler contract with GDS-conserved MOS junction geometry."""
import argparse
import json
from pathlib import Path
import runpy
import sys

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import digest, inventory
from input_charge import verify_files
from mos_junction_extraction import restore

# Load a private set of test globals; the queued baseline run is unchanged.
checker = runpy.run_path(str(SOURCE.with_name('receive_sampler_extracted.py')))
shared = checker['run'].__globals__
original_extract = shared['extract_named']


def junction_extractor(geometry):
    def extract(gds, top, ports, pdk, magic, work, substrate):
        work = Path(work)
        work.mkdir(parents=True, exist_ok=False)
        raw = original_extract(gds, top, ports, pdk, magic, work / 'raw-junctions', substrate)
        raw_work = Path(raw['work'])
        physical = (raw_work / 'conserved.spice').read_text()
        corrected, binding = restore(gds, geometry, raw_work / 'named/ExtractedAnalog.res.ext', physical)
        after = inventory(corrected, ports)
        # Junction geometry may change; terminals, widths, lengths, R and C do not.
        if after != raw['inventory']:
            raise ValueError('junction restoration changed wiring or device inventory')
        hashes = dict(raw['source_sha256'])
        for group in (raw['artifact_sha256'], binding['source_sha256']):
            for path, value in group.items():
                if path in hashes and hashes[path] != value:
                    raise ValueError('conflicting junction provenance')
                hashes[path] = value
        hashes.update({str(p): digest(p) for p in (SOURCE, raw_work / 'checks.json')})
        verify_files(hashes)
        output = work / 'conserved.spice'
        output.write_text(corrected)
        report = dict(status='pass', work=str(work.resolve()), source_sha256=hashes,
            artifact_sha256={str(output.resolve()): digest(output)}, inventory=after,
            raw_extraction=raw, junction_geometry=binding,
            scope='GDS-conserved HV MOS junctions with original devices and distributed RC; electrical qualification separate')
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        return report
    return extract


if __name__ == '__main__':
    checker['configure_cleanup']()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'magic', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, choices=(-40, 27, 125), default=27)
    parser.add_argument('--concurrent', action='store_true', help='separately budgeted 512 MiB simulator')
    args = parser.parse_args()
    proof = json.loads(args.proof.read_text())
    shared['extract_named'] = junction_extractor(proof['cases']['connected']['geometry'])
    checker['run'](args)

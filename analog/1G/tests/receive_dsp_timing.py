#!/usr/bin/env python3
"""Apply the 250 MHz cell-timing constraint to a functionally checked DSP map."""
import argparse
import json
from pathlib import Path
import re
import sys

from hardware import configure_cleanup, execute
from receive_sample_recovery import digest


def run(args):
    root = args.work.resolve()
    receipt = root / 'hardware_checks.json'
    evidence = json.loads(receipt.read_text())
    if evidence.get('status') != 'pass' or not evidence.get('native_mapped_exact'):
        raise ValueError('passing mapped functional evidence is required before timing')
    work = Path(evidence['work'])
    if digest(work / 'hardware_checks.json') != digest(receipt):
        raise ValueError('mapped work receipt and selected receipt differ')
    checker = Path(__file__).with_name('cell_timing.py').resolve()
    destination = root / 'cell_timing.json'
    destination.unlink(missing_ok=True)
    with (root / 'cell_timing_runner.log').open('w') as output:
        result = execute([sys.executable, str(checker), '--work', str(work), '--pdk', str(args.pdk.resolve()),
                          '--tools', str(args.tools.resolve()), '--module', evidence['module'],
                          '--period', '4', '--repair', '--require-functional'],
                         stdout=output, stderr=-2, timeout=300, memory_exclusive=True)
    report = json.loads((work / 'cell_timing.json').read_text())
    if report['module'] != evidence['module'] or report['period_ns'] != 4:
        raise ValueError('wrong module or timing constraint in receipt')
    area = evidence.get('mapped_cell_area_um2')
    if area is None:
        areas = re.findall(r"Chip area for module '\\" + re.escape(evidence['module']) + r"': ([\d.]+)",
                           (work / 'synthesis.log').read_text())
        if len(areas) != 1:
            raise ValueError('missing or ambiguous mapped cell area')
        area = float(areas[0])
    report.update(mapped_cell_area_um2=area,
                  area_report_sha256=digest(work / 'synthesis.log'),
                  timing_runner_sha256=digest(Path(__file__)), checker_sha256=digest(checker),
                  openroad_sha256=digest(args.tools.resolve() / 'bin/openroad'))
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(evidence['module'], report['status'], 'slow-corner setup slack', report['setup_slack_ns'],
          'ns at 4 ns; mapped cell area', report['mapped_cell_area_um2'], 'um^2')
    return result.returncode


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'pdk', 'tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))

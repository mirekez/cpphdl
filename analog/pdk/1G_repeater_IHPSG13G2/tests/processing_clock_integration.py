#!/usr/bin/env python3
"""Verify real macro wiring and reject an opened clock wire with transistor LVS."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import klayout.db as db

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
from checks import check_lvs
from integrate_processing_clock import PORTS, run as assemble
from package_development_layout import ROOT, digest
from tool_process import configure_cleanup, execute


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), full_chip_qualified=False,
                  drc_qualified=False, electrical_qualified=False)

    def save():
        content = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(content)
        (args.work / 'checks.json').write_text(content)

    save()
    try:
        assemble(argparse.Namespace(work=work, provenance=args.provenance))
        generated = json.loads((work / 'integration.json').read_text())
        report['source_sha256'] = dict(generated['source_sha256'])
        tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech/lvs'
        report['source_sha256'][str(Path(__file__))] = digest(Path(__file__))
        report['source_sha256'][str(HERE / 'checks.py')] = digest(HERE / 'checks.py')
        for path in tech.rglob('*'):
            if path.is_file() and path.suffix in ('.lvs', '.py', '.json'):
                report['source_sha256'][str(path)] = digest(path)
        program = args.tools.resolve() / 'root/usr/bin/klayout'
        report['source_sha256'][str(program)] = digest(program)
        report['artifact_sha256'] = dict(generated['artifact_sha256'])
        env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
        for name, broken in (('delivery', False), ('delivery_open', True)):
            report['stage'] = name
            save()
            with (work / (name + '_lvs.log')).open('w') as log:
                result = execute([sys.executable, str(tech / 'run_lvs.py'),
                    '--layout', str(work / (name + '.gds')), '--netlist', str(work / 'delivery.cir'),
                    '--topcell', 'ProcessingClockDelivery', '--run_mode', 'deep',
                    '--run_dir', str(work / (name + '_lvs'))], env=env, stdout=log, stderr=-2,
                    timeout=180, memory_exclusive=False)
            database = work / (name + '_lvs') / (name + '.lvsdb')
            # A missing database is a tool failure, never a successful negative test.
            if not database.is_file() or not database.stat().st_size:
                raise ValueError('LVS produced no comparison database')
            try:
                checked = check_lvs(database, 'ProcessingClockDelivery', PORTS, 398)
            except ValueError:
                if not broken:
                    raise
                comparison = db.LayoutVsSchematic()
                comparison.read(str(database))
                pairs = [p for p in comparison.xref().each_circuit_pair()
                         if p.first() is not None and p.first().name == 'ProcessingClockDelivery']
                if len(pairs) != 1 or pairs[0].second() is None:
                    raise ValueError('negative test lacks the complete top comparison')
                text = (work / (name + '_lvs.log')).read_text()
                if "Netlists don't match" not in text or 'LVS Total Run time' not in text:
                    raise ValueError('negative LVS comparison did not finish')
                report['opened_clock_wire_rejected'] = True
            else:
                if broken:
                    raise ValueError('LVS accepted an open clock connection')
                if result.returncode != 0 or checked['instantiated_devices'] != 398:
                    raise ValueError('LVS did not preserve the expected complete device inventory')
                report['lvs'] = checked
            report['artifact_sha256'][str(database)] = digest(database)
        for path, expected in report['source_sha256'].items():
            if digest(Path(path)) != expected:
                raise ValueError('verification input changed: ' + path)
        for path, expected in generated['artifact_sha256'].items():
            if digest(work / path) != expected:
                raise ValueError('verification geometry changed')
        report.update(status='pass', stage='complete', lvs_qualified=True,
                      inter_macro_nets=['VDD', 'VSS', 'RAW'], substrate='shared PSUB',
                      raw_route_um=generated['raw_route_um'], component_geometry_preserved=True)
        print('PASS actual clock/supply interconnect, 398-device LVS and rejected opened clock wire')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())

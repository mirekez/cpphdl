"""Strict LVS of SRAM leaves after the maintained stream label conversion."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
COMMON = ANALOG / 'pdk/1G_repeater_IHPSG13G2'
sys.path[:0] = [str(SOURCE.parents[1]), str(COMMON)]
from flow_control_stream import MACRO, SRAM_LEGACY_PIN_PORTS, copy_sram_pin_labels
from checks import check_lvs
from tool_process import configure_cleanup, execute


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    pdk = args.pdk.resolve() / 'ihp-sg13g2'
    tech = pdk / 'libs.tech/klayout/tech/lvs'
    gds = pdk / ('libs.ref/sg13g2_sram/gds/' + MACRO + '.gds')
    cdl = pdk / ('libs.ref/sg13g2_sram/cdl/' + MACRO + '.cdl')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    files = [SOURCE, SOURCE.parents[1] / 'flow_control_stream.py', COMMON / 'checks.py',
             COMMON / 'tool_process.py', args.tools.resolve() / 'bin/klayout',
             args.tools.resolve() / 'root/usr/bin/klayout',
             gds, cdl, *tech.rglob('*.py'), *tech.rglob('*.lvs')]
    hashes = {str(path): digest(path) for path in files}
    report = dict(status='running', work=str(work), source_sha256=hashes, cells={},
                  scope='Four isolated SRAM leaves; complete SRAM, DRC and chip qualification remain separate')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        library = db.Layout()
        library.read(str(gds))
        report['labels'] = copy_sram_pin_labels(library)
        for name, ports in SRAM_LEGACY_PIN_PORTS.items():
            cell_work = work / name
            cell_work.mkdir()
            fixture = db.Layout()
            fixture.dbu = library.dbu
            fixture.create_cell(name).copy_tree(library.cell(name))
            layout_path = cell_work / 'input.gds'
            fixture.write(str(layout_path))
            match = re.search(r'^\.SUBCKT ' + re.escape(name) + r'\s.*?^\.ENDS.*?$',
                              cdl.read_text(), re.I | re.M | re.S)
            if match is None:
                raise ValueError('missing SRAM schematic leaf: ' + name)
            # The original GDS labels include "!"; preserve that spelling at
            # the reference boundary without changing any device connection.
            reference = re.sub(r'\bVDD\b', 'VDD!', match[0], flags=re.I)
            reference = re.sub(r'\bVSS\b', 'VSS!', reference, flags=re.I)
            reference_path = cell_work / 'reference.cir'
            reference_path.write_text(reference + '\n')
            environment = dict(os.environ, OMP_NUM_THREADS='1', MALLOC_ARENA_MAX='2',
                               PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
            command = ['prlimit', '--as=1073741824', '--', sys.executable,
                       str(tech / 'run_lvs.py'), '--layout', str(layout_path),
                       '--netlist', str(reference_path), '--topcell', name,
                       '--top_lvl_pins', '--run_mode', 'flat', '--run_dir', str(cell_work / 'lvs')]
            with (cell_work / 'run.log').open('w') as log:
                execute(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                        timeout=120, check=True, memory_exclusive=False)
            evidence = cell_work / 'lvs/input.lvsdb'
            result = check_lvs(evidence, name, ports, minimum_devices=2)
            result['artifact_sha256'] = {str(path): digest(path) for path in
                (layout_path, reference_path, evidence, cell_work / 'run.log')}
            report['cells'][name] = result
            save()
        if any(digest(Path(path)) != expected for path, expected in hashes.items()):
            raise ValueError('SRAM leaf LVS inputs changed')
        report['status'] = 'pass'
        print('PASS four isolated SRAM leaves: strict ports and transistor LVS')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    run(parser.parse_args())

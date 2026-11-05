"""Build the connected electrical/digital netlist before physical mapping."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time

from electrical_connections import check, join
from tool_process import execute

SOURCE = Path(__file__).resolve().parent
ANALOG = SOURCE.parents[1]
REPO = ANALOG.parent
sys.path.insert(0, str(ANALOG / '1G/tests'))
from hardware import source_dependencies


def run(args, work, digital):
    work = work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report = dict(status='running', full_chip_qualified=False, stages=[],
                  scope='Elaborated circuit laws and digital-to-electrical connectivity; no mixed transient or physical signoff.')
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def stage(name, command, timeout=180):
        print('electrical/' + name, flush=True)
        start = time.monotonic()
        with (work / (name + '.log')).open('w') as log:
            result = execute(list(map(str, command)), cwd=work, env=os.environ, stdout=log,
                             stderr=-2, check=False, timeout=timeout)
        report['stages'].append(dict(name=name, seconds=round(time.monotonic() - start, 3), returncode=result.returncode))
        save()
        if result.returncode:
            raise RuntimeError('electrical/' + name + ' failed: ' + str(work / (name + '.log')))

    save()
    try:
        proof = json.loads((digital / 'checks.json').read_text())
        if proof.get('status') != 'pass':
            raise ValueError('digital integration must pass before electrical composition')
        for field in ('source_sha256', 'rtl_sha256'):
            if not proof.get(field) or any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != h
                                           for p, h in proof[field].items()):
                raise ValueError('stale digital integration proof: ' + field)
        rtl = [digital / 'rtl/Predef_pkg.sv', *sorted(p for p in (digital / 'rtl').glob('*.sv') if p.name != 'Predef_pkg.sv')]
        if len(rtl) < 2 or not all(p.is_file() for p in rtl):
            raise ValueError('missing converted digital hierarchy')
        hashes = source_dependencies([SOURCE / 'electrical_export.cpp'], REPO, args.cxx,
            ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
        verilator = shutil.which('verilator') or str(REPO / '.conda/bin/verilator')
        for path in [*rtl, Path(__file__), SOURCE / 'electrical_connections.py',
                     SOURCE / 'tests/electrical_connections.py', SOURCE / 'tool_process.py',
                     ANALOG / '1G/tests/hardware.py', Path(verilator).resolve(),
                     Path(verilator).resolve().with_name('verilator_bin')]:
            hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        report['source_sha256'] = hashes
        stage('build', [args.cxx, '-std=c++20', '-O1', '-I' + str(ANALOG / 'include'),
                        '-I' + str(ANALOG), '-I' + str(REPO / 'include'), SOURCE / 'electrical_export.cpp',
                        '-o', work / 'export'])
        stage('export', [work / 'export', work])
        data = json.loads((work / 'electrical.json').read_text())
        report['connections'] = check(data)
        wrapper, abstract = join(data, (digital / 'rtl/RepeaterDigital.sv').read_text())
        (work / 'RepeaterElectrical.sv').write_text(wrapper)
        (work / 'RepeaterAnalog.blackbox.v').write_text(abstract)
        stage('connection-checks', [sys.executable, SOURCE / 'tests/electrical_connections.py',
                                    work / 'electrical.json', digital / 'rtl/RepeaterDigital.sv'])
        report['connections'] = json.loads((work / 'connection-checks.log').read_text())
        stage('elaborate', [verilator, '--lint-only', '--top-module', 'RepeaterElectrical', '-Wno-fatal',
                            *rtl, work / 'RepeaterAnalog.blackbox.v', work / 'RepeaterElectrical.sv'])
        if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != value for p, value in hashes.items()):
            raise ValueError('electrical integration inputs changed during validation')
        report.update(status='pass', output_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
            [work / 'electrical.json', work / 'RepeaterAnalog.va', work / 'RepeaterElectrical.sv', work / 'RepeaterAnalog.blackbox.v']})
        return report
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    import argparse
    from tool_process import configure_cleanup
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--digital', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    configure_cleanup()
    run(args, args.work, args.digital.resolve())

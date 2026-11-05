#!/usr/bin/env python3
"""Connect the verified electrical hierarchy to a generated physical padframe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

from pad_connections import join
from tool_process import configure_cleanup, execute

SOURCE = Path(__file__).resolve().parent
ANALOG = SOURCE.parents[1]


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    electrical = args.electrical.resolve()
    report = dict(status='running', full_chip_qualified=False, stages=[])
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    def stage(name, command):
        print('pad-integration/' + name, flush=True)
        start = time.monotonic()
        with (work / (name + '.log')).open('w') as log:
            result = execute(list(map(str, command)), cwd=work, env=os.environ,
                             stdout=log, stderr=-2, timeout=180, check=False)
        report['stages'].append(dict(name=name, seconds=round(time.monotonic() - start, 3),
                                    returncode=result.returncode))
        save()
        if result.returncode:
            raise ValueError('failed stage: ' + name)
    save()
    try:
        proof = json.loads((electrical / 'checks.json').read_text())
        if proof.get('status') != 'pass':
            raise ValueError('passing electrical integration required')
        hashes = dict(proof['source_sha256'])
        hashes.update({str(electrical / n): h for n, h in proof['output_sha256'].items()})
        for path in (Path(__file__), SOURCE / 'pad_connections.py', SOURCE / 'tests/pad_connections.py',
                     SOURCE / 'tool_process.py', electrical / 'checks.json'):
            hashes[str(path)] = digest(path)
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('stale electrical integration')
        stage('padframe', [args.pdk.resolve() / '.cpphdl/venv/bin/python', SOURCE / 'padframe.py',
              '--pdk', args.pdk.resolve(), '--work', work / 'pads', '--common-ground',
              '--core-size', str(args.core_size)])
        pads = json.loads((work / 'pads/padframe.json').read_text())
        hashes.update(pads['source_sha256'])
        if digest(work / 'pads/repeater_padframe.gds') != pads['gds_sha256']:
            raise ValueError('padframe geometry changed')
        wrapper, abstract, connections = join(pads, (work / 'pads/padframe.cdl').read_text(),
                                               (electrical / 'RepeaterElectrical.sv').read_text())
        (work / 'RepeaterPadConnected.sv').write_text(wrapper)
        (work / 'RepeaterPadframe.blackbox.v').write_text(abstract)
        stage('connections', [sys.executable, SOURCE / 'tests/pad_connections.py',
                              work / 'pads/padframe.json', work / 'pads/padframe.cdl',
                              electrical / 'RepeaterElectrical.sv'])
        report['connections'] = json.loads((work / 'connections.log').read_text())
        rtl = [Path(p) for p in proof['source_sha256'] if p.endswith('.sv')]
        rtl.sort(key=lambda p: (p.name != 'Predef_pkg.sv', str(p)))
        stage('elaborate', [ANALOG.parent / '.conda/bin/verilator', '--lint-only', '-Wno-fatal',
                           '--top-module', 'RepeaterPadConnected', *rtl,
                           electrical / 'RepeaterAnalog.blackbox.v', electrical / 'RepeaterElectrical.sv',
                           work / 'RepeaterPadframe.blackbox.v', work / 'RepeaterPadConnected.sv'])
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('integration inputs changed')
        report.update(status='connected_unrouted', source_sha256=hashes,
                      artifact_sha256={str(work / n): digest(work / n) for n in (
                          'RepeaterPadConnected.sv', 'RepeaterPadframe.blackbox.v',
                          'pads/padframe.cdl', 'pads/padframe.json', 'pads/repeater_padframe.gds',
                          'connections.log')})
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('electrical', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--core-size', type=int, default=6000)
    run(parser.parse_args())

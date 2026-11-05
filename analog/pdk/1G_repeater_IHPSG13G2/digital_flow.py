"""Elaborate and check the actual two-PHY repeater digital partition."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time

SOURCE = Path(__file__).resolve().parent
ANALOG = SOURCE.parents[1]
REPO = ANALOG.parent
sys.path.insert(0, str(ANALOG / '1G/tests'))
from hardware import source_dependencies
from tool_process import execute

# Legacy one-clock children use ref_clk as their local primary name. The
# converter requires its primary timing budget to be at least every secondary
# budget. This conservative conversion budget does not change the physical
# reference: the fixture and ReferenceMillisecond use 125 MHz.
CLOCKS = ['--primary_clock', 'ref_clk', '250000000',
          '--secondary_clock', 'adc_clk', '250000000',
          '--secondary_clock', 'raw_clk', '136000000',
          '--secondary_clock', 'pcs_clk', '136000000',
          '--secondary_clock', 'tx_a_clk', '125000000',
          '--secondary_clock', 'tx_b_clk', '125000000']


def run(args, work):
    work = work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    entry = SOURCE / 'repeater_digital.cpp'
    fixture = SOURCE / 'tests/repeater_digital.cpp'
    verilator = shutil.which('verilator') or str(REPO / '.conda/bin/verilator')
    report = dict(status='running', top='RepeaterDigital', full_1000base_t_chip=False,
                  scope='Two complete digital receive hierarchies, negotiation, transmit and PAUSE forwarding; '
                        'startup containment and native/RTL agreement. Electrical closure and frame-level '
                        'whole-system qualification remain required.', stages=[])

    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def stage(name, command, timeout=900):
        print('digital/' + name, flush=True)
        start = time.monotonic()
        with (work / (name + '.log')).open('w') as log:
            result = execute(list(map(str, command)), cwd=work, env=os.environ, stdout=log,
                             stderr=-2, check=False, timeout=timeout)
        report['stages'].append(dict(name=name, seconds=round(time.monotonic() - start, 3),
                                     returncode=result.returncode))
        save()
        if result.returncode:
            raise RuntimeError('digital/' + name + ' failed: ' + str(work / (name + '.log')))

    save()
    try:
        hashes = source_dependencies([entry, fixture], REPO, args.cxx)
        for path in [Path(__file__), SOURCE / 'digital_connections.py',
                     SOURCE / 'tests/digital_connections.py', SOURCE / 'tool_process.py',
                     ANALOG / '1G/tests/hardware.py', args.cpphdl.resolve(),
                     Path(verilator).resolve(), Path(verilator).resolve().with_name('verilator_bin')]:
            hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        report['source_sha256'] = hashes
        stage('native-build', [args.cxx, '-std=c++17', '-O1', '-I' + str(REPO / 'include'),
                               fixture, '-o', work / 'native'])
        stage('native', [work / 'native'])
        stage('convert', [args.cpphdl.resolve(), *CLOCKS, '--generated-dir', work / 'rtl',
                          entry, '--', '-I' + str(REPO / 'include')])
        log = (work / 'convert.log').read_text()
        if any(message in log for message in ('MISSED CALL', 'Missing register strobe', "can't find wire")):
            raise ValueError('converter omitted required connectivity or state scheduling')
        rtl = [work / 'rtl/Predef_pkg.sv', *sorted(p for p in (work / 'rtl').glob('*.sv')
                                                  if p.name != 'Predef_pkg.sv')]
        stage('structure', [verilator, '--json-only', '--json-only-output', work / 'design.tree.json',
                            '--top-module', 'RepeaterDigital', '-Wno-fatal', *rtl])
        stage('structure-check', [sys.executable, SOURCE / 'tests/digital_connections.py', work / 'design.tree.json'])
        report['connectivity'] = json.loads((work / 'structure-check.log').read_text())
        stage('rtl-build', [verilator, '--cc', '--exe', '--build', '--no-skip-identical', '-j', '1',
                            '--output-split', '12000', '--output-groups', '0', '-Wno-fatal',
                            '--top-module', 'RepeaterDigital', '--Mdir', work / 'obj',
                            '-CFLAGS', '-DVERILATOR -std=c++17', '-MAKEFLAGS',
                            'CXX=' + args.cxx + ' LINK=' + args.cxx, *rtl, fixture], timeout=1800)
        stage('rtl', [work / 'obj/VRepeaterDigital'])
        native = (work / 'native.log').read_text().strip()
        result = (work / 'rtl.log').read_text().strip()
        if native != result or not re.fullmatch(
                r'PASS repeater digital startup: two PHYs, six clocks, no premature link or drive; \d+ digest', result):
            raise ValueError('whole digital hierarchy startup contract or native/RTL comparison failed')
        if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != value for p, value in hashes.items()):
            raise ValueError('integration inputs changed during checks')
        report.update(status='pass', result=result,
                      rtl_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in rtl})
        print(result, flush=True)
        return report
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()

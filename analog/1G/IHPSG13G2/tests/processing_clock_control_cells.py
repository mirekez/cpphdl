#!/usr/bin/env python3
"""Check both controller clock domains and replay its actual IHP cell netlists."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(ANALOG / '1G/tests'))
from hardware import configure_cleanup, execute
from receive_sample_recovery import archive, digest


def check_hashes(hashes):
    if not hashes or any(not Path(p).is_file() or digest(p) != h for p, h in hashes.items()):
        raise ValueError('missing or changed controller qualification inputs')


def run(args):
    proof = json.loads(args.proof.read_text())
    if (proof.get('status') != 'pass' or proof.get('qualified_curves') != 5 or
            proof.get('rejected_curves') != 3 or proof.get('load_step_cases') != 6 or
            proof.get('drift_recoveries') != 2 or proof.get('events') != 698640):
        raise ValueError('a passing complete native/RTL controller proof is required')
    hashes = dict(proof.get('source_sha256', {}))
    check_hashes(hashes)
    check_hashes(proof.get('rtl_sha256', {}))
    hashes.update(proof['rtl_sha256'])
    expected = proof['traces']['native']['raw_sha256']
    for flow in ('native', 'rtl'):
        trace = proof['traces'][flow]
        path = Path(trace['path'])
        if digest(path) != trace['archive_sha256']:
            raise ValueError('changed compressed controller trajectory')
        raw = hashlib.sha256()
        with gzip.open(path, 'rb') as stream:
            for block in iter(lambda: stream.read(1 << 20), b''):
                raw.update(block)
        actual = raw.hexdigest()
        if actual != expected or actual != trace['raw_sha256']:
            raise ValueError('native/RTL controller trajectories differ')
        hashes[str(path)] = digest(path)
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    tools = args.tools.resolve() / 'bin'
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    typical = library / 'lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    slow = library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'
    rtl = [Path(proof['work']) / 'rtl' / name for name in ('Predef_pkg.sv', 'ProcessingClockControl.sv')]
    fixture = ANALOG / '1G/tests/processing_clock_control.cpp'
    inputs = [SOURCE, args.proof.resolve(), fixture, typical, slow,
              library / 'lef/sg13g2_tech.lef', library / 'lef/sg13g2_stdcell.lef',
              *[tools / n for n in ('yosys', 'abc', 'sv2v', 'openroad')],
              args.verilator.resolve(), args.verilator.resolve().with_name('verilator_bin')]
    hashes.update({str(p): digest(p) for p in inputs})
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  reference_period_ns=8, acquisition_period_ns=1.25,
                  scope='cell-only 125 MHz reference / 800 MHz raw-oscillator budget and functional gate proof; '
                        'excludes routed timing, hold, CDC/metastability and oscillator PVT envelope')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(name, argv):
        with (work / (name + '.log')).open('w') as log:
            execute(list(map(str, argv)), stdout=log, stderr=-2, check=True,
                    timeout=600, memory_exclusive=False)

    save()
    try:
        if any(re.search(r'[\s{}"\\]', str(p)) for p in [work, library, tools, *rtl]):
            raise ValueError('unsupported tool path')
        with (work / 'design.v').open('w') as output:
            execute([str(tools / 'sv2v'), *map(str, rtl)], stdout=output,
                    check=True, timeout=60, memory_exclusive=False)
        synth = work / 'synth.ys'
        synth.write_text(f'''read_liberty -lib {typical}
read_verilog {work}/design.v
hierarchy -check -top ProcessingClockControl
synth -top ProcessingClockControl -flatten -noabc -noshare
dfflibmap -liberty {typical}
abc -exe {tools}/abc -liberty {typical}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {work}/mapped.v
write_json {work}/mapped.json
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {typical}
write_verilog -noattr {work}/cells.v
''')
        command('synthesis', [tools / 'yosys', '-Q', '-T', '-s', synth])
        cells = json.loads((work / 'mapped.json').read_text())['modules']['ProcessingClockControl']['cells']
        counts = Counter(c['type'] for c in cells.values())
        if not counts or len(cells) > 10000 or any(not n.startswith('sg13g2_') for n in counts):
            raise ValueError('unexpected or non-IHP controller mapping')
        timing = work / 'timing.tcl'
        timing.write_text(f'''read_lef {library}/lef/sg13g2_tech.lef
read_lef {library}/lef/sg13g2_stdcell.lef
read_liberty {slow}
read_verilog {work}/mapped.v
link_design ProcessingClockControl
create_clock -name reference -period 8 [get_ports ref_clk]
create_clock -name acquisition -period 1.25 [get_ports pcs_clk]
set_clock_uncertainty 0.1 [all_clocks]
# Only intra-domain cell delay is qualified here. Physical synchronizer
# placement, Gray-bus skew and recovery/removal require the routed CDC flow.
set_clock_groups -asynchronous -group reference -group acquisition
set_input_delay 0.4 -clock reference [get_ports {{reset enable_in}}]
set_input_transition 0.1 [all_inputs]
set_output_delay 0.4 -clock reference [get_ports {{coarse_n_out* fine_n_out* clear_out ready_ref_out fault_out measured_edges_out*}}]
set_output_delay 0.4 -clock acquisition [get_ports ready_pcs_out]
set_load 0.005 [all_outputs]
initialize_floorplan -die_area {{0 0 250 250}} -core_area {{20 20 230 230}} -site CoreSite
set_wire_rc -signal -layer Metal2
set_max_fanout 4 [current_design]
set_max_capacitance 0.01 [current_design]
set_max_transition 0.3 [current_design]
repair_design -pre_placement
foreach domain {{reference acquisition}} {{
    if {{[llength [find_timing_paths -path_group $domain]] == 0}} {{error "empty timing domain: $domain"}}
}}
report_checks -path_delay max -group_path_count 3 -format full_clock_expanded -digits 9
report_worst_slack -max -digits 9
write_verilog {work}/resized.v
exit
''')
        command('timing', [tools / 'openroad', '-no_init', '-exit', timing])
        text = (work / 'timing.log').read_text()
        slack = re.findall(r'worst slack max\s+([-+\d.eE]+)', text)
        if len(slack) != 1 or not math.isfinite(float(slack[0])) or '[ERROR' in text:
            raise ValueError('missing or invalid controller timing result')
        report['setup_slack_ns'] = float(slack[0])
        report['cells'] = dict(counts)
        report['traces'] = {}
        for flow in ('mapped', 'resized'):
            objects = work / (flow + '_obj')
            command(flow + '_build', [args.verilator.resolve(), '--cc', '--exe', '--build',
                    '--no-skip-identical', '-j', '1', '--top-module', 'ProcessingClockControl',
                    '-Wno-fatal', '--Mdir', objects, '-CFLAGS', '-DVERILATOR -std=c++17',
                    '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx,
                    work / (flow + '.v'), work / 'cells.v', fixture])
            trace = work / (flow + '.csv')
            command(flow, [objects / 'VProcessingClockControl', trace])
            if digest(trace) != expected:
                raise ValueError(flow + ' controller trajectory differs from the independent native proof')
            report['traces'][flow] = archive(trace)
        check_hashes(hashes)
        report['artifact_sha256'] = {str(p): digest(p) for p in
            (synth, timing, work / 'mapped.v', work / 'mapped.json', work / 'resized.v', work / 'cells.v', work / 'timing.log')}
        if report['setup_slack_ns'] < 0:
            raise ValueError('controller misses its cell-only timing budget')
        report['status'] = 'pass'
        print('PASS controller: both clock budgets and exact mapped/resized trajectories')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'work', 'pdk', 'tools', 'verilator'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    try:
        run(args)
    except BaseException as error:
        # Input rejection occurs before creating a fresh run directory. Do not
        # leave a previous pass at the current receipt path in that case.
        args.work.mkdir(parents=True, exist_ok=True)
        receipt = args.work / 'checks.json'
        previous = json.loads(receipt.read_text()) if receipt.exists() else {}
        if previous.get('status') != 'fail' or previous.get('error') != str(error):
            receipt.write_text(json.dumps(dict(status='fail', error=str(error)), indent=2) + '\n')
        raise

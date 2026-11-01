#!/usr/bin/env python3
"""Drive the routed clock gate from the controller's actual IHP output cells."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / 'VerilogA'), str(SOURCE.parents[1]), str(SOURCE.parent)]
from processing_clock_gate_process import process_deck
from processing_clock_interface import output_cells
from processing_clock_gate_pex import qualify, HEADER
from compact_trace import read_spice
from extract_analog import digest
from input_charge import verify_files
from hardware import source_dependencies
from tool_process import execute, configure_cleanup

CONDITIONS = {'mos_tt': (27, 1.2, 'cap_typ'), 'mos_ss': (125, 1.08, 'cap_wcs'),
              'mos_ff': (-40, 1.32, 'cap_bcs')}


def deck(model, original, models, condition, extracted, cells):
    temperature, supply, cap_corner = CONDITIONS[condition]
    bound, _ = process_deck(model, original, models, condition, temperature, supply)
    observations = re.findall(r'^let observation\d+ = v\((n\d+)\)$', bound, re.M)
    if len(observations) != 5:
        raise ValueError('changed native admission-gate probes')
    clock, enable, output, _, power = observations
    mapping = {clock: 'CLOCK', enable: 'COMMAND', output: 'OUT', power: 'VDD'}
    resistors = re.findall(r'^Rphysical\d+ (n\d+) (n\d+) ([0-9.eE+-]+)$', bound, re.M)
    if len(resistors) != 3 or {b for a, b, value in resistors} != {clock, enable, power}:
        raise ValueError('changed finite source connections')
    for a, b, value in resistors:
        if float(value) != (1 if b == power else 50):
            raise ValueError('changed native source impedance')
        mapping[a] = mapping[b] + '_SOURCE'
    sources = re.findall(r'^Vsource\d+ .+$', bound, re.M)
    if len(sources) != 3:
        raise ValueError('changed native waveform sources')
    def rename(line):
        return re.sub(r'\bn\d+\b', lambda m: mapping[m[0]], line)
    # Preserve the native stimulus and finite source impedances. The actual
    # routed output masters drive ENABLE; their wire load is in the GDS PEX.
    sources = [rename(line) for line in sources]
    wires = [f'Rsource{i} {mapping[a]} {mapping[b]} {value}' for i, (a, b, value) in enumerate(resistors)]
    tran = re.findall(r'^tran .+$', bound, re.M)
    if len(tran) != 1:
        raise ValueError('changed native transient coverage')
    cells = re.sub(r'\bADMIT\b', 'ENABLE', cells)
    return '\n'.join(['Extracted clock gate with physical controller output masters',
        f'.lib "{models}/cornerMOSlv.lib" {condition}', f'.lib "{models}/cornerCAP.lib" {cap_corner}',
        f'.temp {temperature}', f'.include "{extracted}"', *sources, *wires, cells,
        'Csupply VDD 0 1p IC=0', 'Cload OUT 0 200f IC=0',
        'Xgate VDD 0 CLOCK ENABLE OUT ExtractedAnalog',
        '.save v(CLOCK) v(ENABLE) v(OUT) v(VDD)',
        '.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 chgtol=1e-16',
        '.control', 'set num_threads=1', 'set noaskquit', 'set wr_singlescale',
        'set wr_vecnames', 'set numdgt=17', tran[0],
        'wrdata spice.dat v(CLOCK) v(ENABLE) v(OUT) v(VDD)', 'quit', '.endc', '.end', ''])


def run(args):
    extraction = args.extraction.resolve()
    receipt = json.loads((extraction / 'extraction.json').read_text())
    if receipt.get('status') != 'pass' or receipt.get('ports') != ['VDD', 'VSS', 'CLOCK', 'ENABLE', 'OUT']:
        raise ValueError('requires conserved clock-gate extraction')
    verify_files(receipt['source_sha256'])
    verify_files(receipt['artifact_sha256'])
    physical = args.physical.resolve()
    controller = json.loads((physical / 'physical_module.json').read_text())
    if (controller.get('status') != 'component_checks_pass' or controller.get('top') != 'ProcessingClockControl'
            or controller['constraints'].get('ready_output_load_pf') != .08):
        raise ValueError('requires qualified controller topology with 80 fF output budget')
    netlist = physical / 'postroute.v'
    if controller['artifact_sha256'].get('postroute.v') != digest(netlist):
        raise ValueError('controller topology changed')
    cdl = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
    recorded = {str(Path(p).resolve()): value for p, value in controller['input_sha256'].items()}
    if recorded.get(str(cdl)) != digest(cdl):
        raise ValueError('controller process geometry changed')
    cells, inventory = output_cells(netlist.read_text(), cdl.read_text())
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    hashes = source_dependencies([SOURCE.with_name('processing_clock_gate.cpp')], ANALOG.parent, '/usr/bin/c++',
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    inputs = [SOURCE, SOURCE.with_name('processing_clock_gate_pex.py'), SOURCE.with_name('processing_clock_gate.py'),
        SOURCE.parents[1] / 'processing_clock_gate_process.py', SOURCE.parents[1] / 'processing_clock_interface.py',
        SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'mos_geometry.py',
        SOURCE.parents[1] / 'input_charge.py', ANALOG / 'VerilogA/compact_trace.py',
        ANALOG / '1G/tests/hardware.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.bench.resolve(), args.ngspice.resolve(), extraction / 'extraction.json',
        physical / 'physical_module.json', netlist, cdl]
    inputs += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes.update({str(p): digest(p) for p in inputs})
    hashes.update(receipt['artifact_sha256'])
    report = dict(status='running', work=str(work), source_sha256=hashes, cells=inventory, cases={},
        output_load_F=200e-15, requested_conditions=args.conditions,
        scope='controller output-cell topology drives conserved extracted gate at 200 fF; excludes upstream/controller wire RC, field-solver calibration, aperture and full clock tree')
    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)
    save()
    try:
        export = work / 'export'
        execute([str(args.bench.resolve()), str(export), 'export'], check=True, timeout=30)
        for condition in args.conditions:
            case = work / condition
            case.mkdir()
            circuit = deck((export / 'design.va').read_text(), (export / 'bench.cir').read_text(), models,
                           condition, extraction / 'conserved.spice', cells)
            (case / 'bench.cir').write_text(circuit)
            environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent))
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, timeout=2400, check=True, memory_exclusive=False)
            metrics = qualify(read_spice(case / 'spice.dat', HEADER), CONDITIONS[condition][1])
            report['cases'][condition] = dict(metrics=metrics,
                deck_sha256=digest(case / 'bench.cir'), trace_sha256=digest(case / 'spice.dat'))
            save()
        verify_files(hashes)
        report['status'] = 'pass'
        print('PASS controller output cells and extracted gate: ' + ', '.join(args.conditions))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('extraction', 'physical', 'bench', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--conditions', choices=list(CONDITIONS), nargs='+', default=list(CONDITIONS))
    run(parser.parse_args())

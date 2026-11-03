#!/usr/bin/env python3
"""Screen a PDK receiver-clock buffer after the conserved extracted gate.

This is a circuit candidate: the explicit interconnect budget must be replaced
by placed/routed extraction before the connected clock can be qualified.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from processing_clock_slew import (HERE, ROOT, CONDITIONS, HEADER, clock_limit,
    thresholds, loaded_deck, measure, read_spice, digest, verify_files,
    configure_cleanup, execute)
from mos_geometry import size_mos_instances


def buffer(cdl, master):
    if master not in ('sg13g2_buf_8', 'sg13g2_buf_16'):
        raise ValueError('unsupported clock restoration buffer')
    matches = re.findall(r'^\.SUBCKT ' + master + r' ([^\n]+)\n(.*?)^\.ENDS[^\n]*$', cdl, re.M | re.S)
    if len(matches) != 1 or matches[0][0] != 'X A VDD VSS':
        raise ValueError('changed PDK buffer interface')
    devices = []
    for line in matches[0][1].splitlines():
        if not line or line.startswith('*'):
            continue
        if not re.fullmatch(r'M\w+ \S+ \S+ \S+ \S+ sg13_lv_[np]mos m=1 w=\S+ l=\S+ ng=\d+', line):
            raise ValueError('unsupported buffer transistor: ' + line)
        devices.append('X' + line[1:])
    bound, geometry = size_mos_instances('\n'.join(devices))
    if len(geometry) != 4:
        raise ValueError('expected four physical buffer transistors')
    return '.SUBCKT ' + master + ' X A VDD VSS\n' + bound + '.ENDS ' + master + '\n', geometry


def connect(circuit, definition, master):
    # A pi interconnect budget between the two physical components. The load
    # capacitor remains at OUT; its value includes the receiver root and wire.
    gate = 'Xgate VDD 0 CLOCK ENABLE OUT ExtractedAnalog'
    if circuit.splitlines().count(gate) != 1 or re.search(r'\b(?:GATE_OUT|BUFFER_IN)\b', circuit):
        raise ValueError('changed extracted clock-gate connection')
    return circuit.replace(gate, '\n'.join([
        'Xgate VDD 0 CLOCK ENABLE GATE_OUT ExtractedAnalog', definition.rstrip(),
        'Rclock_link GATE_OUT BUFFER_IN 30', 'Cclock_link_a GATE_OUT 0 10f IC=0',
        'Cclock_link_b BUFFER_IN 0 10f IC=0',
        'Xclock_buffer OUT BUFFER_IN VDD 0 ' + master]))


def run(args):
    reference = args.reference.resolve()
    proof = json.loads(reference.read_text())
    if proof.get('status') != 'pass' or set(proof.get('cases', {})) != set(CONDITIONS):
        raise ValueError('completed extracted clock-gate PVT reference required')
    if not args.conditions or len(set(args.conditions)) != len(args.conditions):
        raise ValueError('unique nonempty process conditions required')
    hashes = dict(proof['source_sha256'])
    verify_files(hashes)
    cdl = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
    if hashes.get(str(cdl)) != digest(cdl):
        raise ValueError('buffer library differs from the physical controller reference')
    definition, geometry = buffer(cdl.read_text(), args.master)
    files = [reference, args.constraints.resolve(), Path(__file__).resolve(),
             HERE / 'processing_clock_slew.py', HERE / 'mos_geometry.py', cdl]
    origin = Path(proof['work'])
    levels = {}
    for case in args.conditions:
        path = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' / (
            'sg13g2_stdcell_' + CONDITIONS[case][1] + '.lib')
        levels[case] = thresholds(path.read_text())
        files.append(path)
        deck = origin / case / 'bench.cir'
        if digest(deck) != proof['cases'][case]['deck_sha256']:
            raise ValueError('changed reference deck')
        files.append(deck)
    hashes.update({str(p): digest(p) for p in files})
    ngspice = args.ngspice.resolve()
    if hashes.get(str(ngspice)) != digest(ngspice):
        raise ValueError('simulator differs from retained clock reference')
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    report = dict(status='running', stage='waiting_for_simulation', pid=os.getpid(),
        source_sha256=hashes, cases={}, master=args.master, transistor_geometry=geometry,
        link_resistance_ohm=30, link_capacitance_F=20e-15,
        output_load_F=args.load_ff * 1e-15, conditions=args.conditions,
        physical_qualified=False, full_chip_qualified=False, scope=__doc__)

    def save():
        temporary = work / 'checks.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(work / 'checks.json')

    save()
    try:
        limit = clock_limit(args.constraints.read_text())
        for case in args.conditions:
            destination = work / case
            destination.mkdir()
            deck = loaded_deck((origin / case / 'bench.cir').read_text(), proof['output_load_F'], args.load_ff)
            (destination / 'bench.cir').write_text(connect(deck, definition, args.master))
            models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
            environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                               PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models))
            report['stage'] = case
            save()
            with (destination / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--core=0', '--', ngspice, '-b', 'bench.cir'],
                        cwd=destination, env=environment, stdout=log, stderr=-2,
                        timeout=2400, check=True, memory_exclusive=True)
            trace = destination / 'spice.dat'
            report['cases'][case] = measure(read_spice(trace, HEADER), CONDITIONS[case][0], levels[case], limit)
            report['cases'][case]['trace_sha256'] = digest(trace)
            save()
        verify_files(hashes)
        passed = all(v['passes'] for v in report['cases'].values())
        status = 'buffer_slew_screen_pass' if set(args.conditions) == set(CONDITIONS) else 'partial_buffer_screen_pass'
        report.update(status=status if passed else 'input_slew_fail', stage='complete')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*')
                                    if p.is_file() and p.name not in ('checks.json', 'checks.tmp')}
        save()
    print(report['status'])
    return passed


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'constraints', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--master', choices=['sg13g2_buf_8', 'sg13g2_buf_16'], default='sg13g2_buf_16')
    parser.add_argument('--load-ff', type=float, default=200)
    parser.add_argument('--conditions', choices=list(CONDITIONS), nargs='+', default=list(CONDITIONS))
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--ngspice', type=Path, default=ROOT / 'out/veriloga-toolchain/install/bin/ngspice')
    sys.exit(0 if run(parser.parse_args()) else 1)

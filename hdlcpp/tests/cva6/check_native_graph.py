#!/usr/bin/env python3
"""Regenerate the small original-SV CVA6 bus with the native CppHDL graph backend."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cva6-source', required=True, type=Path)
    parser.add_argument('--hdlcpp', required=True, type=Path)
    parser.add_argument('--cpphdl', required=True, type=Path)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--trace', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--reference', type=Path, help='existing original-SV Verilator replay executable')
    parser.add_argument('--cxxrtl-reference', type=Path, help='optional existing CXXRTL replay executable')
    parser.add_argument('--trials', type=int, default=5)
    parser.add_argument('--cpu', type=int, default=2)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    if output.is_relative_to(repo) or output.exists():
        parser.error('output must be a new directory outside the cpphdl checkout')
    if args.trials < 1:
        parser.error('trials must be positive')
    source = args.cva6_source.resolve(strict=True)
    fixtures = Path(__file__).with_name('context_replay')
    output.mkdir(parents=True)
    manifest = {'commands': [], 'backend': 'native-graph', 'status': 'building'}

    def run(command, label):
        command = list(map(str, command))
        manifest['commands'].append(command)
        (output / 'commands.json').write_text(json.dumps(manifest, indent=2) + '\n')
        with (output / (label + '.log')).open('w') as log:
            subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT, check=True)

    for name in ['XbarBench.sv', 'XbarDesign.sv', 'BusTrace.h', 'BusRun.cc']:
        shutil.copyfile(fixtures / name, output / name)
    harness = (source / 'corev_apu/tb/ariane_testharness.sv').read_text()
    mapping = re.search(r"assign addr_map\s*=\s*'\{.*?\n  \};", harness, re.DOTALL)
    if not mapping:
        raise RuntimeError('cannot find the original address map')
    (output / 'AddressMap.svh').write_text(mapping[0].replace('assign addr_map', 'assign rules') + '\n')
    wiring = []
    for port in range(2):
        wiring += [f'`AXI_ASSIGN_FROM_REQ(slave[{port}], slv_reqs[{port}])',
                   f'`AXI_ASSIGN_TO_RESP(slv_resps[{port}], slave[{port}])']
    for port in range(10):
        wiring += [f'`AXI_ASSIGN_TO_REQ(mst_reqs[{port}], master[{port}])',
                   f'`AXI_ASSIGN_FROM_RESP(master[{port}], mst_resps[{port}])']
    (output / 'BoundaryWiring.svh').write_text('\n'.join(wiring) + '\n')
    includes = [source / 'vendor/pulp-platform/axi/include', source / 'vendor/pulp-platform/common_cells/include',
                source / 'vendor/pulp-platform/axi/src', source / 'vendor/pulp-platform/common_cells/src',
                source / 'corev_apu/tb', output]
    source_inputs = {Path(__file__).resolve(), args.hdlcpp.resolve(), args.cpphdl.resolve(),
                     repo / 'include/cpphdl_graph.h', repo / 'tools/cpphdl-graph.py',
                     source / 'corev_apu/tb/ariane_testharness.sv'}
    for directory in includes:
        source_inputs.update(directory.rglob('*.sv'))
        source_inputs.update(directory.rglob('*.svh'))
    source_inputs.update([output / 'BusRun.cc', output / 'BusTrace.h'])
    source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(source_inputs)}
    run([args.hdlcpp.resolve(), '--native-graph', '--top', 'XbarBench', '--output', output / 'graph.cc',
         '--translate-off-format', 'pragma,translate_off,translate_on', '-DSYNTHESIS', '-DVERILATOR',
         *['-I' + str(path) for path in includes], output / 'XbarDesign.sv'], 'frontend')
    run([args.cpphdl.resolve(), '--native-graph', '--output', output / 'backend', '--cxx', args.cxx,
         '--runner', output / 'BusRun.cc', output / 'graph.cc', '--', '-DUSE_NATIVE_GRAPH', '-DNDEBUG'], 'backend')
    binaries = {'cpphdl-native-graph': output / 'backend/run'}
    if args.reference:
        binaries['verilator'] = args.reference.resolve(strict=True)
    if args.cxxrtl_reference:
        binaries['cxxrtl'] = args.cxxrtl_reference.resolve(strict=True)
    inputs = [*binaries.values(), args.trace.resolve(strict=True), output / 'graph.cc',
              repo / 'include/cpphdl_graph.h', args.hdlcpp.resolve(), args.cpphdl.resolve()]
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    records = []
    expected = None
    for trial in range(args.trials):
        order = list(binaries.items())
        if trial % 2:
            order.reverse()
        for name, binary in order:
            label = f'trial-{trial}-{name}'
            run(['taskset', '-c', args.cpu, binary, args.trace.resolve(), '1'], label)
            text = (output / (label + '.log')).read_text()
            match = re.search(r'seconds=([0-9.]+) cycles=(\d+) checked=(\d+) checksum=([0-9a-f]+)', text)
            if not match:
                raise RuntimeError('missing checked timing output')
            seconds, cycles, checked, checksum = match.groups()
            identity = (cycles, checked, checksum)
            if cycles != checked or (expected and identity != expected):
                raise RuntimeError('not an equivalent checked replay')
            expected = identity
            records.append(dict(name=name, trial=trial, seconds=float(seconds), cycles=int(cycles),
                                checked=int(checked), checksum=checksum))
    if hashes != {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}:
        raise RuntimeError('benchmark inputs changed')
    result = {'inputs': hashes, 'records': records,
              'source_inputs': source_hashes,
              'median_seconds': {name: statistics.median(record['seconds'] for record in records if record['name'] == name)
                                 for name in binaries}}
    if source_hashes != {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(source_inputs)}:
        raise RuntimeError('conversion or build sources changed')
    (output / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    manifest['status'] = 'complete'
    (output / 'commands.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(result['median_seconds'], indent=2))


if __name__ == '__main__':
    main()

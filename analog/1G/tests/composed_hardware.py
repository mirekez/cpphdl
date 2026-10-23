#!/usr/bin/env python3
"""Native/RTL/IHP checking for complete startup compositions without fixtures."""
import argparse
from collections import Counter
import filecmp
import hashlib
import json
import re
from pathlib import Path
import subprocess

from hardware import TestToolchain, configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
MODULES = {
    'receive_calibration': ('ReceiveGainCalibration', 'receive_calibration.cpp', ['ReceiveGainCalibration']),
    'pam5_driver': ('PAM5DriverControl', 'pam5_driver_control.cpp', ['PAM5DriverControl']),
    'normalizer': ('ReceiveSampleNormalizer', 'receive_normalizer.cpp', ['ReceiveSampleNormalizer']),
    'pma_aligned': ('AlignedPMAEndpoint', 'pma_aligned.cpp', ['ReceivePairAcquirer', 'PMAControl',
        'PCSSymbolMapper', 'PCSTransmitBits', 'PCSTransmitter', 'PCSReceiveSynchronizer',
        'PCSReceiveFramer', 'PipelinedPCSBranchMetrics', 'PCSTrellisDecoder', 'PCSReceiver',
        'PMAEndpoint', 'AlignedPMAEndpoint']),
    'pair_acquisition': ('ReceivePairAcquirer', 'pair_acquisition.cpp', ['ReceivePairAcquirer']),
    'adc_interleave': ('InterleavedSARController', 'adc_interleave.cpp', ['ADCSARController', 'InterleavedSARController']),
    'adc_receive': ('ADCReceiveController', 'adc_receive.cpp',
                    ['ADCSARController', 'InterleavedSARController', 'ReceiveSampleNormalizer', 'ADCReceiveController']),
    'pma_control': ('PMAControl', 'pma_control.cpp', ['PMAControl']),
    'pma_endpoint': ('PMAEndpoint', 'pma_endpoint.cpp', ['PMAControl', 'PCSSymbolMapper', 'PCSTransmitBits',
        'PCSTransmitter', 'PCSReceiveSynchronizer', 'PCSReceiveFramer', 'PipelinedPCSBranchMetrics',
        'PCSTrellisDecoder', 'PCSReceiver', 'PMAEndpoint']),
    'autoneg_link': ('Autonegotiation', 'autonegotiation.cpp', ['ReferenceMillisecond', 'FLPTransmitter',
        'FLPReceiver', 'MasterSlaveResolver', 'Autonegotiator', 'Autonegotiation'])}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args):
    top, implementation, children = MODULES[args.case]
    source = SOURCE.parent.parent
    repo = source.parents[1]
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    toolchain = TestToolchain(args.cxx, args.verilator.resolve(), work)
    (work / 'hardware_checks.json').unlink(missing_ok=True)
    harness = SOURCE.parent / ('pma_endpoint.cpp' if args.case == 'pma_aligned' else args.case + '.cpp')
    defines = ['-DALIGNED_PMA'] if args.case == 'pma_aligned' else []
    provenance = source_dependencies([source / implementation, harness], repo, args.cxx, defines=defines)
    provenance[str(SOURCE)] = digest(SOURCE)
    simulation_timeout = 4200 if args.case == 'autoneg_link' else 180

    def stage(name, command, timeout=1200, output=None):
        with (output or work / (name + '.log')).open('w') as log:
            execute(toolchain.command(command), env=toolchain.env,
                    stdout=log, stderr=subprocess.STDOUT, check=True, timeout=timeout)

    def simulate(binary, name):
        stage(name, [binary, work / (name + '_trace.csv')], simulation_timeout)
        if name != 'cpp' and not filecmp.cmp(work / 'cpp_trace.csv', work / (name + '_trace.csv'), shallow=False):
            raise ValueError('native/' + name + ' composed output trajectories differ')

    simulate(args.native.resolve(), 'cpp')
    frequency = '250000000' if args.case in ('adc_interleave', 'adc_receive', 'normalizer') else '125000000'
    stage('convert', [args.converter.resolve(), '--primary_clock', 'clk', frequency,
        '--generated-dir', work / 'rtl', source / implementation, '--', '-I' + str(repo / 'include')])
    rtl = [work / 'rtl/Predef_pkg.sv'] + [work / 'rtl' / (child + '.sv') for child in children]

    def build(name, files):
        directory = work / (name + '_obj')
        stage(name + '-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '--no-skip-identical',
            '--flatten', '--output-groups', '0', '--output-split', '10000', '--output-split-cfuncs', '500',
            '-j', '2', '--top-module', top, '-Wno-fatal', '--Mdir', directory,
            '-CFLAGS', ' '.join(['-DVERILATOR', '-std=c++17', *defines]), *files, harness], timeout=1800)
        return directory / ('V' + top)

    simulate(build('rtl', rtl), 'rtl')
    if args.tools is None:
        if any(digest(Path(path)) != value for path, value in provenance.items()):
            raise ValueError('composed hardware sources changed during validation')
        return
    binary = args.tools.resolve() / 'bin'
    liberty = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'
    stage('sv2v', [binary / 'sv2v', *rtl], timeout=180, output=work / 'design.v')

    def quote(path):
        if any(c in str(path) for c in ('"', '\\', '\n')):
            raise ValueError('unsupported synthesis path')
        return '"' + str(path) + '"'

    script = f'''read_liberty -lib {quote(liberty)}
read_verilog {quote(work / 'design.v')}
hierarchy -check -top {top}
synth -top {top} -flatten -noabc -noshare
dfflibmap -liberty {quote(liberty)}
abc -exe {quote(binary / 'abc')} -liberty {quote(liberty)}
hilomap -hicell sg13g2_tiehi L_HI -locell sg13g2_tielo L_LO
clean
check -assert
write_verilog -noattr -noexpr {quote(work / 'mapped.v')}
write_json {quote(work / 'mapped.json')}
design -reset
read_liberty -ignore_miss_func -ignore_miss_dir {quote(liberty)}
write_verilog -noattr {quote(work / 'cells.v')}
'''
    (work / 'synth.ys').write_text(script)
    stage('synthesis', [binary / 'yosys', '-Q', '-T', '-s', work / 'synth.ys'], timeout=1800)
    cells = Counter(cell['type'] for cell in json.loads((work / 'mapped.json').read_text())['modules'][top]['cells'].values())
    if not cells or any(not cell.startswith('sg13g2_') for cell in cells):
        raise ValueError('composed synthesis retained a non-IHP/blackbox cell')
    mapped_files = [work / 'mapped.v', work / 'cells.v']
    if args.case in ('adc_interleave', 'adc_receive'):
        # sv2v flattens unpacked ports, including ascending one-bit arrays.
        # Restore the public interface around the unchanged mapped logic so
        # the exact same independent harness addresses every physical slice.
        text = (work / 'mapped.v').read_text()
        text, count = re.subn(r'\bmodule\s+' + top + r'\s*\(', 'module ' + top + '_mapped(', text, count=1)
        if count != 1:
            raise ValueError('mapped controller top was not found')
        (work / 'mapped_flat.v').write_text(text)
        header = (work / 'rtl' / (top + '.sv')).read_text().split('module ' + top, 1)[1].split(');', 1)[0]
        connections = []
        ports = ['clk', 'reset', 'enable_in', 'sample_enable_in', 'comparisons_in', 'trial_out',
                 'acquire_out', 'convert_out', 'data_out', 'slice_out', 'valid_out', 'fault_out']
        if args.case == 'adc_receive':
            ports += ['offset_in', 'gain_in', 'normalized_out', 'normalized_valid_out',
                      'clipped_out', 'normalized_slice_out']
        for name in ports:
            expression = ('{' + ', '.join(name + '[' + str(i) + ']' for i in range(16)) + '}'
                          if name in ('trial_out', 'acquire_out', 'convert_out') else name)
            connections.append('.' + name + '(' + expression + ')')
        wrapper = 'module ' + top + header + ');\n' + top + '_mapped implementation(\n' + ',\n'.join(connections) + ');\nendmodule\n'
        (work / 'mapped_interface.sv').write_text(wrapper)
        mapped_files = [work / 'mapped_flat.v', work / 'mapped_interface.sv', work / 'cells.v']
    simulate(build('mapped', mapped_files), 'mapped')
    if any(digest(Path(path)) != value for path, value in provenance.items()):
        raise ValueError('composed hardware sources changed during validation')
    result = dict(status='pass', module=top, scope='functional equivalence; physical implementation is separate',
                  native_rtl_exact=True, native_mapped_exact=True, mapped_sha256=digest(work / 'mapped.v'),
                  source_sha256=provenance, fixture_sha256={}, cells=cells)
    if args.case == 'adc_receive':
        result['simulation_interface_sha256'] = digest(work / 'mapped_interface.sv')
    (work / 'hardware_checks.json').write_text(json.dumps(result, indent=2) + '\n')
    print((work / 'mapped.log').read_text(), end='')
    print('PASS exact native/RTL/IHP composed trajectories')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=MODULES, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    if args.tools is not None and args.pdk is None:
        parser.error('--tools requires --pdk')
    run(args)

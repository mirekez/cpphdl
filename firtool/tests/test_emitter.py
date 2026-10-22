#!/usr/bin/env python3
"""Execute support emitted by the real patch; optionally exercise its CIRCT pass."""
import argparse
import ast
import random
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run(args, **kwargs):
    result = subprocess.run(list(map(str, args)), text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed (exit {result.returncode}): {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def extract(base):
    run(['git', 'init', '-q', base])
    run(['git', '-C', base, 'apply', '--include=include/circt/Conversion/Exportcpphdl.h',
         '--include=lib/Conversion/Exportcpphdl/*', ROOT / 'firtool/firrtl_cpphdl.patch'])
    source = (base / 'lib/Conversion/Exportcpphdl/Exportcpphdl.cpp').read_text()
    body = source.split('static void emitcpphdlSupport(raw_ostream &os) {', 1)[1].split(
        '\n}\n', 1)[0]
    # Only the emitted C++ string literals are decoded, not an alternate copy
    # of the helper. The exporter pass below is tested against CIRCT itself.
    support = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', body))
    (base / 'cpphdl_support.h').write_text(support)
    return source


def support_checks(base, cxx):
    randomizer = random.Random(0x4d554c)
    checks = []
    total = 0
    for width in (1, 8, 31, 32, 63, 64, 65, 96, 127, 128, 129, 256, 512):
        mask = (1 << width) - 1
        cases = [(0, mask), (1, mask), (mask, mask)]
        if width > 64:
            cases += [(1 << 63, 2), (1 << (width-1), 1), ((1 << 64)-1, (1 << 64)-1)]
        cases += [(randomizer.getrandbits(width), randomizer.getrandbits(width)) for _ in range(80)]
        def literal(value):
            limbs = ','.join(str((value >> shift) & ((1 << 64)-1)) + 'ULL' for shift in range(0, width, 64))
            return f'firtool_cpphdl::constant<{width}>({{{limbs}}})'
        for left, right in cases:
            checks.append(f'if (firtool_cpphdl::multiply<{width}>({literal(left)}, {literal(right)}) != '
                          f'{literal(left * right & mask)}) return {width};')
            total += 1
    checks += [
        'if (firtool_cpphdl::multiply<128>(UINT64_MAX, UINT64_MAX) != firtool_cpphdl::constant<128>({1ULL, UINT64_MAX-1})) return 99;',
        'if (firtool_cpphdl::multiply<65>(cpphdl::logic<8>(255), cpphdl::logic<64>(UINT64_MAX)) != firtool_cpphdl::constant<65>({UINT64_MAX-254, 0ULL})) return 98;',
    ]
    source = base / 'support.cpp'
    source.write_text('#include "cpphdl_support.h"\nlong _system_clock=0;\nint main(){\n' + '\n'.join(checks) + '\n}\n')
    binary = base / 'support-check'
    run([cxx, '-std=c++17', '-O1', '-fno-strict-aliasing', '-I' + str(ROOT / 'include'), source, '-o', binary])
    run([binary])
    print(f'PASS: {total + 2} exact wide/narrow products against Python integer arithmetic')


def circt_checks(base, emitter, cxx):
    # Both signed and unsigned FIRRTL high-half products arrive as comb bitvectors.
    ir = '''module {
  hw.module @Multiply(in %lhs : i128, in %rhs : i128, out result : i128) {
    %r = comb.mul %lhs, %rhs : i128
    hw.output %r : i128
  }
}
'''
    input_file = base / 'multiply.mlir'
    input_file.write_text(ir)
    mono = run([emitter, input_file, '--single'])
    assert 'firtool_cpphdl::multiply<128>' in mono
    directory = base / 'split-mul'
    run([emitter, input_file, directory])
    assert 'firtool_cpphdl::multiply<128>' in (directory / 'Multiply.cpp').read_text()
    # Execute the exported module in both file layouts, including a variadic
    # comb.mul. This checks port bindings and expression emission, not just helpers.
    for operands in (2, 3):
        third_port = ', in %third : i128' if operands == 3 else ''
        third_operand = ', %third' if operands == 3 else ''
        input_file.write_text(f'''module {{
  hw.module @Multiply(in %lhs : i128, in %rhs : i128{third_port}, out result : i128) {{
    %r = comb.mul %lhs, %rhs{third_operand} : i128
    hw.output %r : i128
  }}
}}
''')
        mono = run([emitter, input_file, '--single'])
        (base / 'multiply-mono.h').write_text(mono)
        run([emitter, input_file, directory])
        cases = [(0, (1 << 128)-1, 3), ((1 << 64)-1, (1 << 64)-1, 5),
                 ((1 << 127)+7, 17, 9)]
        def literal(value):
            return f'firtool_cpphdl::constant<128>({{{value & ((1<<64)-1)}ULL,{value >> 64}ULL}})'
        checks = []
        for a, b, c in cases:
            expected = a * b * (c if operands == 3 else 1) & ((1 << 128)-1)
            checks.append(f'lhs={literal(a)}; rhs={literal(b)}; third={literal(c)}; ++_system_clock; '
                          f'if(dut.result()!={literal(expected)}) return 1;')
        for split in (False, True):
            header = 'Multiply.h' if split else 'multiply-mono.h'
            third_binding = 'dut.third=[&]{return third;};' if operands == 3 else ''
            source = base / 'multiply-check.cpp'
            source.write_text(f'#include "{header}"\nlong _system_clock=0;\nint main(){{\n'
                              'cpphdl::logic<128> lhs(0),rhs(0),third(0); Multiply dut;\n'
                              'dut.lhs=[&]{return lhs;}; dut.rhs=[&]{return rhs;};\n' +
                              third_binding + 'dut._assign();\n' + '\n'.join(checks) + '\n}\n')
            extra = [directory / 'Multiply.cpp'] if split else []
            run([cxx, '-std=c++17', '-fno-strict-aliasing', '-I' + str(ROOT / 'include'),
                 '-I' + str(directory), source, *extra, '-o', base / 'multiply-check'])
            run([base / 'multiply-check'])
    # Required parameters have no extern declaration defaults; instance bindings
    # must supply their concrete values, including values wider than 64 bits.
    def parameter_ir(second=123456789012345678901234567890):
        return f'''module {{
  hw.module.extern @External<CAPACITY: i128, LABEL: none>(in %in : i8, out out : i8) attributes {{verilogName = "VendorRam"}}
  hw.module @Top(in %a : i8, out result : i8) {{
    %x = hw.instance "first" @External<CAPACITY: i128 = 123456789012345678901234567890, LABEL: none = "ram\\22label">(in: %a: i8) -> (out: i8)
    %y = hw.instance "second" @External<CAPACITY: i128 = {second}, LABEL: none = "ram\\22label">(in: %x: i8) -> (out: i8)
    hw.output %y : i8
  }}
}}
'''
    input_file.write_text(parameter_ir())
    mono = run([emitter, input_file, '--single'])
    directory = base / 'split-extern'
    run([emitter, input_file, directory])
    for header in (mono, (directory / 'External.h').read_text()):
        assert 'cpphdl_external_name' in header and 'VendorRam' in header
        assert 'cpphdl_parameter_CAPACITY' in header and 'logic<128>' in header
        assert 'cpphdl_parameter_LABEL' in header
    # Compile and execute the generated class metadata, not only string checks.
    value = 123456789012345678901234567890
    source = base / 'metadata.cpp'
    source.write_text('#include "External.h"\n#include <cstring>\nlong _system_clock=0;\nint main(){\n'
                      f'if (External::cpphdl_parameter_CAPACITY != firtool_cpphdl::constant<128>({{{value & ((1<<64)-1)}ULL,{value >> 64}ULL}})) return 1;\n'
                      'if(std::strcmp(External::cpphdl_external_name,"VendorRam")) return 2;\n'
                      'if(std::strcmp(External::cpphdl_parameter_LABEL,"ram\\\"label")) return 3;\n}\n')
    run([cxx, '-std=c++17', '-I' + str(ROOT / 'include'), '-I' + str(directory), source, '-o', base / 'metadata'])
    run([base / 'metadata'])
    input_file.write_text(parameter_ir(7))
    for output in ('--single', base / 'conflict'):
        result = subprocess.run([str(emitter), str(input_file), str(output)], text=True, capture_output=True)
        assert result.returncode != 0 and 'bindings differ' in result.stderr, result.stdout + result.stderr
    input_file.write_text('module { hw.module.extern @Unbound<DEPTH: i32>() }\n')
    for output in ('--single', base / 'unbound'):
        result = subprocess.run([str(emitter), str(input_file), str(output)], text=True, capture_output=True)
        assert result.returncode != 0 and 'concrete integer or string value' in result.stderr, result.stdout + result.stderr
    input_file.write_text('module { hw.module.extern @Defaults<COUNT: i17 = -2, LABEL: none = "default">() }\n')
    mono = run([emitter, input_file, '--single'])
    assert 'cpphdl_external_name = "Defaults"' in mono
    assert 'constant<17>({131070ULL})' in mono
    assert 'cpphdl_parameter_LABEL = "default"' in mono
    print('PASS: executable CIRCT products, extern bindings/defaults and conflict/unbound rejection')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--emitter', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpphdl-emitter-') as temp:
        base = Path(temp)
        source = extract(base)
        assert source.count('emitExternalParameters(external,') == 2
        assert 'external parameter must have a concrete integer or string value' in source
        support_checks(base, args.cxx)
        if args.emitter:
            circt_checks(base, args.emitter.resolve(), args.cxx)


if __name__ == '__main__':
    main()

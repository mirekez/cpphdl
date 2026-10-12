#!/usr/bin/env python3
"""External host models must receive generic FIRRTL parameter metadata."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIR = '''circuit ParameterTest:
  extmodule ParameterReader:
    output out: UInt<32>
    defname = parameter_reader
    parameter DEFAULT = 7
    parameter NEGATIVE = -1
    parameter FORMAT = "reader=%d"
  module ParameterTest:
    output out: UInt<32>
    inst reader of ParameterReader
    out <= reader.out
'''
CHECK = '''#include "ParameterReader.h"
#include <string_view>
static_assert(std::string_view(ParameterReader::cpphdl_external_name) == "parameter_reader");
static_assert(uint64_t(ParameterReader::cpphdl_parameter_DEFAULT) == 7);
static_assert(uint64_t(ParameterReader::cpphdl_parameter_NEGATIVE) == 0xffffffffULL);
static_assert(std::string_view(ParameterReader::cpphdl_parameter_FORMAT) == "reader=%d");
int main() {}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firtool', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpphdl-external-parameters-') as tmp:
        work = Path(tmp)
        (work/'test.fir').write_text(FIR)
        subprocess.run([str(args.firtool.resolve()), '--split-cpphdl', '-o='+str(work/'generated'),
                        str(work/'test.fir')], check=True)
        (work/'check.cpp').write_text(CHECK)
        subprocess.run([args.cxx, '-std=c++23', '-I'+str(ROOT/'include'),
                        '-I'+str(work/'generated'), str(work/'check.cpp'), '-o', str(work/'check')], check=True)
        subprocess.run([str(work/'check')], check=True)
        print('External integer/string parameters and Verilog name PASSED')


if __name__ == '__main__':
    main()

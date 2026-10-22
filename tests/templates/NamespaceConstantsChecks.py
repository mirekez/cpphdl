#!/usr/bin/env python3
"""Namespace/HLS-policy constexprs fold; module parameters stay overridable."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

SOURCE = r'''
#include "cpphdl.h"
namespace constants {
inline constexpr uint32_t version=0x80000003u;
constexpr int32_t negative=-7;
constexpr uint64_t wide=0xff00000000000003ull;
}
struct Policy { static constexpr uint32_t bias=11; };
template<uint32_t OFFSET=5>
class NamespaceConstants:public cpphdl::Module {
public:
    _PORT(uint32_t) value_in;
    _PORT(uint32_t) word_out;
    _PORT(uint64_t) wide_out;
    _PORT(bool) signed_out;
    void _assign() {
        word_out=_ASSIGN((value_in()^constants::version)+OFFSET+Policy::bias);
        wide_out=_ASSIGN(uint64_t(value_in())+constants::wide);
        signed_out=_ASSIGN(int32_t(value_in())<constants::negative);
    }
    void _work(bool) {}
    void _strobe() {}
};
'''
TB = r'''
module Check;
  reg [31:0] value_in;
  wire [31:0] word_out;
  wire [63:0] wide_out;
  wire signed_out;
  NamespaceConstants #(.OFFSET(9)) dut(.clk(1'b0),.reset(1'b0),.*);
  initial begin
    for (integer i=0;i<4096;i=i+1) begin
      value_in=32'(i*7919)^((i%2)?32'h80000000:32'h0);
      if (i<4) value_in=32'hfffffff8+32'(i); // -8,-7,-6,-5.
      #1;
      if (word_out!==((value_in^32'h80000003)+32'd20)) $fatal(1,"namespace constant / overridable module parameter");
      if (wide_out!==(64'(value_in)+64'hff00000000000003)) $fatal(1,"64-bit namespace constant");
      if (signed_out!==($signed(value_in)<-32'sd7)) $fatal(1,"signed namespace constant");
    end
    $display("PASS namespace/policy constexpr folding and module parameter override");
    $finish;
  end
endmodule
'''

def boundary_checks(args, root, env):
    with tempfile.TemporaryDirectory(prefix='cpphdl-namespace-constants-') as temp:
        work=Path(temp);source=work/'source.cpp';source.write_text(SOURCE)
        def run(command):subprocess.run(list(map(str,command)),cwd=work,env=env,check=True)
        run([args.cpphdl.resolve(),'--generated-dir',work/'generated',source,'--','-std=c++17','-I'+str(root/'include')])
        (work/'tb.sv').write_text(TB)
        atomic=Path(subprocess.check_output([args.cxx,'-print-file-name=libatomic.so'],text=True).strip()).parent
        generated=work/'generated';packages=sorted(generated.glob('*_pkg.sv'))
        sources=packages+[s for s in sorted(generated.glob('*.sv')) if s not in packages]
        run([args.verilator,'--binary','--timing','-Wno-fatal','--top-module','Check','--Mdir',work/'obj',
             '-CFLAGS','-std=c++20','-MAKEFLAGS',f'CXX={args.cxx} LINK={args.cxx} AR=ar',
             '-LDFLAGS','-L'+str(atomic),*sources,work/'tb.sv'])
        run([work/'obj/VCheck'])

def fixture_checks(args, root, env):
    source = Path(__file__).with_name('NamespaceConstants.cc')
    with tempfile.TemporaryDirectory(prefix='cpphdl-namespace-constants-') as directory:
        work = Path(directory)
        def run(command):
            result = subprocess.run(list(map(str, command)), env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout)
            return result.stdout
        run([args.cxx, '-std=c++17', '-I' + str(root / 'include'), source, '-o', work / 'native'])
        print(run([work / 'native']))
        generated = work / 'generated'
        run([args.cpphdl, '--generated-dir', generated, source, '--', '-I' + str(root / 'include')])
        rtl = (generated / 'NamespaceConstants.sv').read_text()
        assert 'OFFSET' in rtl and 'BITS +' in rtl, 'module parameter expression was prematurely substituted'
        assert 'Constants::' not in rtl and 'ConstantPolicy_pkg' not in rtl
        files = sorted(generated.glob('*_pkg.sv')) + sorted(p for p in generated.glob('*.sv') if not p.name.endswith('_pkg.sv'))
        run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
             '--top-module', 'NamespaceConstants', '-GBITS=19', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++17 -DVERILATOR -I' + str(root / 'include'),
             '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx +
             ' LDFLAGS=-L' + str(Path(args.cxx).resolve().parent.parent / 'lib'), *files, source])
        print(run([work / 'obj/VNamespaceConstants']))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/g++')
    parser.add_argument('--verilator', default='verilator')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
    fixture_checks(args, root, env)
    boundary_checks(args, root, env)

if __name__ == '__main__':
    main()

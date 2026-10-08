"""Build a small combinational fixture in native, graph, and RTL flows."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


class FixtureChecks:
    def __init__(self, name, argv=None):
        parser = argparse.ArgumentParser()
        parser.add_argument('--cpphdl', required=True)
        parser.add_argument('--cxx', required=True)
        parser.add_argument('--work', type=Path, required=True)
        parser.add_argument('--verilator')
        parser.add_argument('--hdlcpp')
        parser.add_argument('--flow', choices=['cpp', 'graph', 'effects', 'verilator'])
        self.args = parser.parse_args(argv)
        self.name = name
        self.fixture = Path(__file__).resolve().parent
        self.include = self.fixture.parents[1] / 'include'
        self.args.work.mkdir(parents=True, exist_ok=True)
        self.work = Path(tempfile.mkdtemp(prefix=name + '-', dir=self.args.work))
        self.toolchain = TestToolchain(self.args.cxx, self.args.verilator, self.work)

    def run(self, command, label):
        result = subprocess.run(self.toolchain.command(command), cwd=self.work,
                                env=self.toolchain.env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        (self.work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {self.work}')
        return result.stdout

    def native(self, flags=()):
        self.run([self.args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
                  '-I' + str(self.include), *flags, self.fixture / (self.name + 'Run.cc'),
                  '-o', self.work / 'native'], 'native-build')
        print(self.run([self.work / 'native'], 'native-run'), end='')

    def graph(self, definitions=(), link_flags=()):
        output = self.work / 'graph'
        self.run([self.args.cpphdl, '--native-graph', '--top', 'cpphdl_top',
                  '--cxx', self.args.cxx, *['--frontend-flag=' + d for d in definitions],
                  '--runner', self.fixture / (self.name + 'Run.cc'), '--output', output,
                  self.fixture / (self.name + '.cc'), '--', '-I' + str(self.include),
                  '-DTEST_GRAPH', *definitions, *link_flags, '-fsanitize=address,undefined'], 'graph-build')
        print(self.run([output / 'run'], 'graph-run'), end='')

    def rtl(self):
        generated = self.work / 'rtl'
        self.run([self.args.cpphdl, '--generated-dir=' + str(generated),
                  self.fixture / (self.name + '.cc'), '--', '-std=c++23',
                  '-I' + str(self.include)], 'convert')
        self.run([self.args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                  '--top-module', self.name, '--Mdir', self.work / 'obj',
                  '-CFLAGS', '-std=c++23 -DTEST_RTL -I' + str(self.include),
                  generated / 'Predef_pkg.sv', generated / (self.name + '.sv'),
                  self.fixture / (self.name + 'Run.cc')], 'verilator-build')
        print(self.run([self.work / ('obj/V' + self.name)], 'rtl-run'), end='')

    def check(self):
        if self.args.verilator:
            self.rtl()
        else:
            self.native()
            self.graph()

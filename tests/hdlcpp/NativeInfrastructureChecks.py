"""Preserved SV RAM, tasks, scratch functions, external ports and reset deltas."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
for name in ('hdlcpp', 'cxx', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--verilator', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
tools = TestToolchain(args.cxx.absolute(), args.verilator.resolve() if args.verilator else None, work)

def run(name, command, expected_error=None):
    result = subprocess.run(tools.command(command), env=tools.env,
                            capture_output=True, text=True, timeout=180)
    diagnostic = result.stdout + result.stderr
    (work / (name + '.log')).write_text(diagnostic)
    if expected_error:
        assert result.returncode and expected_error in diagnostic, diagnostic
    elif result.returncode:
        raise RuntimeError(name + ':\n' + diagnostic)

def lower(name, text, extra=(), expected_error=None):
    source = work / (name + '.sv')
    output = work / (name + '.cpp')
    source.write_text(text)
    output.unlink(missing_ok=True)
    run(name + '-lower', [args.hdlcpp.resolve(), '--native-graph', '--top', name,
                         '--output', output, *extra, source], expected_error)
    return output

flags = [args.cxx, '-std=c++23', '-O1', '-I' + str(repo / 'include')]
graph = lower('Infrastructure', r'''
module Infrastructure(input clk, slow, reset, write,
    input [1:0] address, input [31:0] data,
    output reg [31:0] value, output [31:0] read_data, result,
    output full, output reg [7:0] async_value);
    reg [31:0] memory [0:3];
    reg [31:0] scratch, temporary;
    reg reset_n;
    wire is_full = address == 3;
    assign full = is_full;
    assign read_data = memory[address];
    function [31:0] transform(input [31:0] x);
        scratch = x + 7;
        transform = scratch ^ 19;
    endfunction
    assign result = transform(data);
    task set_value(input [31:0] x);
        value <= x + 3;
    endtask
    always @(posedge clk) begin
        reset_n <= !reset;
        temporary = data ^ 53;
        if (reset) begin
            value <= 0;
            for (int i=0; i<4; ++i) memory[i] <= 0;
        end else begin
            set_value(temporary);
            if (write) memory[address] <= data;
        end
    end
    always @(posedge slow or negedge reset_n)
        if (!reset_n) async_value <= 0;
        else async_value <= async_value + 1;
endmodule
''')
run('graph-build', [*flags, graph, '-o', work / 'emit'])
run('graph-emit', [work / 'emit', work / 'model.h'])
runner = work / 'runner.cpp'
runner.write_text(r'''
#ifdef VERILATOR
#include "VInfrastructure.h"
#else
#include "model.h"
#endif
#include <cassert>
#include <cstdint>
int main() {
#ifdef VERILATOR
    VInfrastructure model;
#else
    cpphdl_native::Model model;
#endif
    uint32_t memory[4]{},value=0; unsigned counter=0;
    bool oldClock=false,oldSlow=false,reset_n=false;
    for(unsigned i=0;i<1024;++i) {
        bool clock=(i/2)%2,slow=(i/13)%2,reset=i%93<8,write=(i/7)%2;
        uint32_t data=i*7919,address=(i/17)%4;
#ifdef VERILATOR
        model.clk=clock;model.slow=slow;model.reset=reset;model.write=write;model.data=data;model.address=address;
        model.eval();
#else
        model.clk[0]=clock;model.slow[0]=slow;model.reset[0]=reset;model.write[0]=write;model.data[0]=data;model.address[0]=address;
        model.step();
#endif
        if(slow&&!oldSlow)counter=reset_n?(counter+1)&255:0;
        if(clock&&!oldClock) {
            value=reset?0:(data^53)+3;
            if(reset)for(auto& word:memory)word=0;
            else if(write)memory[address]=data;
            if(reset&&reset_n)counter=0;
            reset_n=!reset;
        }
#ifdef VERILATOR
#define OUT(name) model.name
#else
#define OUT(name) model.name[0]
#endif
        assert(OUT(value)==value && OUT(read_data)==memory[address]);
        assert(OUT(result)==((data+7)^19) && OUT(full)==(address==3));
        assert(OUT(async_value)==counter);
        oldClock=clock;oldSlow=slow;
    }
}
''')
run('native-build', [*flags, runner, '-o', work / 'run'])
run('native-run', [work / 'run'])
if args.verilator:
    run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
        '--top-module', 'Infrastructure', '--Mdir', work / 'obj', '-CFLAGS', '-DVERILATOR -std=c++23',
        work / 'Infrastructure.sv', runner])
    run('rtl-run', [work / 'obj/VInfrastructure'])

external = lower('ExternalTop', '''module Box(input [7:0] a, output [7:0] b); endmodule
module ExternalTop(input [7:0] a, output [7:0] b);
Box box(.a(a), .b(b)); endmodule
''', ['--external-module', 'Box'])
metadata = Path(str(external) + '.externals.tsv').read_text()
assert '\tBox\t' in metadata and '__external_0_a' in metadata and '__external_0_b' in metadata
run('external-build', [*flags, external, '-o', work / 'external-emit'])
run('external-emit', [work / 'external-emit', work / 'external.h'])
external_runner = work / 'external-run.cpp'
external_runner.write_text('''#include "external.h"
#include <cassert>
int main() { cpphdl_native::Model m;
for(unsigned i=0;i<256;++i) { m.a[0]=i; m.__external_0_b[0]=i^53; m.eval();
assert(m.__external_0_a[0]==i && m.b[0]==(i^53)); } }
''')
run('external-run-build', [*flags, external_runner, '-o', work / 'external-run'])
run('external-run', [work / 'external-run'])

lower('Partial', '''module Partial(input clk, input [1:0] a, output [7:0] q);
reg [7:0] ram[4]; always @(posedge clk) ram[a][3:0] <= 4'b1;
assign q=ram[a]; endmodule''', expected_error='partial RAM row writes')
lower('Retained', '''module Retained(input [7:0] a, output [7:0] q);
function [7:0] f(input [7:0] x); reg [7:0] old; old=old+x; f=old; endfunction
assign q=f(a); endmodule''', expected_error='reads storage before defining')
lower('MultipleWriters', '''module MultipleWriters(input clk, other, input [1:0] a,
output [7:0] q); reg [7:0] ram[4];
always @(posedge clk) ram[a] <= 8'd1;
always @(posedge other) ram[a] <= 8'd2;
assign q=ram[a]; endmodule''', expected_error='multiple writing processes')
lower('PersistentInitializer', '''module PersistentInitializer(input [7:0] a, output [7:0] q);
function [7:0] f(input [7:0] x); reg [7:0] previous=1; previous=previous+x; f=previous; endfunction
assign q=f(a); endmodule''', expected_error='static function initializers')
lower('ObservableScratch', '''module ObservableScratch(input [7:0] a, output [7:0] q, observed);
reg [7:0] scratch;
function [7:0] f(input [7:0] x); scratch=x+1; f=scratch; endfunction
assign q=f(a); assign observed=scratch; endmodule''', expected_error='observable module storage')
lower('TaskOutput', '''module TaskOutput(input clk, input [7:0] a, output reg [7:0] q);
task copy(input [7:0] x, output [7:0] y); y=x; endtask
always @(posedge clk) copy(a,q); endmodule''', expected_error='task output/ref arguments')
blocking = lower('Blocking', '''module Blocking(input clk, input [7:0] a, output reg [7:0] q);
always @(posedge clk) q=a; endmodule''')
run('blocking-build', [*flags, blocking, '-o', work / 'blocking'])
run('blocking-emit', [work / 'blocking', work / 'blocking.h'], 'observable clocked blocking storage')
print('SV native infrastructure: RAM, tasks, scratch functions, reset deltas, externals and rejection checks PASS')

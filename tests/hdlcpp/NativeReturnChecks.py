"""Native function control-flow regression, independently checked in C++."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

p=argparse.ArgumentParser(description=__doc__)
for key in ('hdlcpp','cxx','work'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
temporary = tempfile.TemporaryDirectory(prefix='returns-', dir=work)
work = Path(temporary.name)
root=Path(__file__).resolve().parents[2]
tools = TestToolchain(a.cxx.absolute(), None, work)
def run(name,command,error=None):
    r=subprocess.run(tools.command(command), env=tools.env,
                     capture_output=True,text=True,timeout=180)
    log=r.stdout+r.stderr;(work/(name+'.log')).write_text(log)
    if error:
        if not r.returncode or error not in log:raise RuntimeError(name+': '+log)
    elif r.returncode:raise RuntimeError(name+': '+log)
source=work/'Returns.sv'
source.write_text(r'''
module Returns(input [7:0] x, input [1:0] mode,
               output [31:0] early, nested, searched, selected, implicit_value,
               output [95:0] wide);
    function automatic [31:0] early_f(input [7:0] a);
        reg [31:0] temp;
        if (a == 0) return 91;
        temp = a * 3;
        if (a[0]) return temp + 5;
        temp = temp ^ 53;
        return temp;
        temp = 999; // unreachable
    endfunction
    function automatic [31:0] nested_f(input [7:0] a);
        if (a[7]) begin
            if (a[0]) return early_f(a);
            else return early_f(a-1);
        end
        return early_f(a+1) + 17;
    endfunction
    function automatic [31:0] search_f(input [7:0] a);
        for (int i=0; i<8; ++i) begin
            if (a[i]) return i;
        end
        return 32'hffffffff;
    endfunction
    function automatic [31:0] case_f(input [1:0] m,input [7:0] a);
        case (m)
            0: return early_f(a);
            1: if (a[0]) return a+101;
            2: begin
                if (a[1]) return a+202;
                return a+203;
            end
            default: return a+303;
        endcase
        return a+404;
    endfunction
    function automatic [31:0] implicit_f(input [7:0] a);
        implicit_f = a + 7;
        if (a[0]) return a + 9;
        implicit_f = a + 11;
    endfunction
    function automatic [95:0] wide_f(input [7:0] a);
        if (a[0]) return {32'h12345678,32'h87654321,24'b0,a};
        return {32'hfedcba98,32'h01234567,24'b0,a};
    endfunction
    assign early=early_f(x),nested=nested_f(x),searched=search_f(x);
    assign selected=case_f(mode,x),implicit_value=implicit_f(x),wide=wide_f(x);
endmodule
''')
output=work/'converted.cpp'
run('lower',[a.hdlcpp,'--native-graph','--top','Returns','--output',output,source])
flags=[a.cxx,'-std=c++23','-O1','-I'+str(root/'include')]
run('emitter-build',[*flags,output,'-o',work/'emit'])
run('emit',[work/'emit',work/'model.h'])
runner=work/'runner.cpp'
runner.write_text(r'''
#include "model.h"
#include <cstdint>
#include <stdexcept>
#include <iostream>
uint32_t early(uint8_t a) {
    if(a==0)return 91;
    uint32_t temp=a*3;
    if(a&1)return temp+5;
    return temp^53;
}
int main() {
    cpphdl_native::Model model;
    for(unsigned repetition=0;repetition<2;++repetition)for(unsigned a=0;a<256;++a)for(unsigned mode=0;mode<4;++mode) {
        model.x[0]=a;model.mode[0]=mode;model.step();
        unsigned first=0;while(first<8&&!(a&(1<<first)))++first;
        uint32_t nested=(a&128)?early((a&1)?a:a-1):early(a+1)+17;
        uint32_t selected=mode==0?early(a):mode==1?((a&1)?a+101:a+404):mode==2?((a&2)?a+202:a+203):a+303;
        uint64_t low=(uint64_t((a&1)?0x87654321u:0x01234567u)<<32)|a;
        if(model.early[0]!=early(a)||model.nested[0]!=nested||model.searched[0]!=(first<8?first:0xffffffffu)||
           model.selected[0]!=selected||model.implicit_value[0]!=a+((a&1)?9:11)||model.wide[0]!=uint32_t(low)||model.wide[1]!=uint32_t(low>>32)||
           model.wide[2]!=((a&1)?0x12345678u:0xfedcba98u)) {
            std::cerr<<"x="<<a<<" mode="<<mode<<" early="<<model.early[0]<<"/"<<early(a)<<" nested="<<model.nested[0]<<"/"<<nested
                <<" searched="<<model.searched[0]<<" selected="<<model.selected[0]<<"/"<<selected<<" implicit="<<model.implicit_value[0]
                <<" wide="<<std::hex<<model.wide[0]<<","<<model.wide[1]<<" expected_low="<<low<<'\n';
            throw std::runtime_error("conditional return mismatch");
        }
    }
    std::cout<<"PASS native early returns: nested branches/calls, case fallthrough, loop search, implicit and 96-bit returns\n";
}
''')
run('runner-build',[*flags,runner,'-o',work/'run']);run('runner',[work/'run'])
# Do not silently turn a missing result into the new return accumulator's zero.
source.write_text('''module Undefined(input x,output [31:0] value);
function automatic [31:0] f(input a); if(a)return 7; endfunction
assign value=f(x); endmodule\n''')
run('undefined',[a.hdlcpp,'--native-graph','--top','Undefined','--output',work/'undefined.cpp',source],
    'function reads storage before defining its returned value')
# Reads on an already-returned path must not trigger the native bounds checker.
source.write_text('''module ReturnMemory(input clk, write, input [7:0] address,
                                      input [31:0] data, output [31:0] result);
reg [31:0] memory [0:3];
always @(posedge clk) if(write) memory[address] <= data;
function automatic [31:0] load(input [7:0] a);
    if(a == 0) return 99;
    return memory[a-1];
endfunction
function automatic [31:0] nested(input [7:0] a);
    if(a == 255) return 77;
    return load(a);
endfunction
assign result=nested(address);
endmodule\n''')
memory_output = work/'memory.cpp'
run('memory-lower',[a.hdlcpp,'--native-graph','--top','ReturnMemory','--output',memory_output,source])
run('memory-emitter-build',[*flags,memory_output,'-o',work/'emit'])
run('memory-emit',[work/'emit',work/'model.h'])
runner.write_text(r'''
#include "model.h"
#include <stdexcept>
int main() {
    cpphdl_native::Model model;
    for(unsigned i=0;i<4;++i) {
        model.address[0]=i;model.data[0]=1000+i;model.write[0]=1;
        model.clk[0]=0;model.step();model.clk[0]=1;model.step();
    }
    model.write[0]=0;
    for(unsigned repetition=0;repetition<4;++repetition) for(unsigned a: {0u,1u,2u,3u,4u,255u}) {
        model.address[0]=a;model.clk[0]=0;model.step();
        auto expected=a==0?99u:a==255?77u:999u+a;
        if(model.result[0]!=expected) throw std::runtime_error("early-return memory guard mismatch");
    }
}
''')
run('memory-runner-build',[*flags,runner,'-o',work/'run'])
run('memory-runner',[work/'run'])
print((work/'runner.log').read_text(),end='')

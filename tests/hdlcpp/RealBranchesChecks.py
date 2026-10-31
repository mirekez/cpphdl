"""Native real arithmetic and explicit preservation of both SYNTHESIS branches."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser()
for name in ('hdlcpp','cxx','work'):p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2]
Path(a.work).mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='real-',dir=a.work))
def run(cmd,error=None):
 r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=240)
 if error:
  assert r.returncode and error in r.stdout+r.stderr,r.stdout+r.stderr
 elif r.returncode:raise RuntimeError(str(cmd)+'\n'+r.stdout+r.stderr)
 return r
source=work/'input.sv';source.write_text('''module Test(input [63:0] av,bv,output [63:0] product,difference,ratio,
 output [31:0] narrow,output less);
`ifdef SYNTHESIS
 assign product=av^bv;assign difference=av;assign ratio=bv;assign narrow=av[31:0];assign less=av<bv;
`else
 real a,b,c,d,e;
 always_comb begin a=$bitstoreal(av);b=$bitstoreal(bv);c=a*b;d=a-b;e=a/b;end
 assign product=$realtobits(c);assign difference=$realtobits(d);assign ratio=$realtobits(e);
 assign narrow=$shortrealtobits(shortreal'(c));assign less=a<b;
`endif
endmodule
''')
base=[a.hdlcpp,'--native-graph','--preserve-synthesis','--top','Test']
run([*base,'--output',work/'converted.cc',source])
code=(work/'converted.cc').read_text();assert '#ifdef SYNTHESIS\n' in code and '\n#else\n' in code
assert all('"'+op+'"' in (work/'converted.cc.simulation.cc').read_text() for op in ['fp_mul','fp_sub','fp_div','fp_cast','fp_lt'])
flags=[a.cxx,'-std=c++23','-O1','-ffp-contract=off','-I'+str(root/'include')]
for name,defines in [('simulation',[]),('hardware',['-DSYNTHESIS'])]:
 run([*flags,*defines,work/'converted.cc','-o',work/(name+'-emit')]);run([work/(name+'-emit'),work/(name+'.h')])
(work/'check.cpp').write_text('''#define cpphdl_native sim
#include "simulation.h"
#undef cpphdl_native
#define cpphdl_native hw
#include "hardware.h"
#undef cpphdl_native
#include <bit>
#include <cassert>
#include <cmath>
int main(){sim::Model s;hw::Model h;
auto bits=[](double x){return std::bit_cast<uint64_t>(x);};
for(int i=-1000;i<1000;i++){double a=i*0.125,b=(i%37)*0.25+0.125;auto av=bits(a),bv=bits(b);
s.av={uint32_t(av),uint32_t(av>>32)};s.bv={uint32_t(bv),uint32_t(bv>>32)};h.av=s.av;h.bv=s.bv;s.eval();h.eval();
auto value=[](const auto& v){return uint64_t(v[0])|(uint64_t(v[1])<<32);};
assert(value(s.product)==bits(a*b));assert(value(s.difference)==bits(a-b));assert(value(s.ratio)==bits(a/b));
assert(s.narrow[0]==std::bit_cast<uint32_t>(float(a*b)));assert(s.less[0]==(a<b));assert(value(h.product)==(av^bv));}}
''')
run([*flags,work/'check.cpp','-o',work/'check']);run([work/'check'])
run([*base,'-DSYNTHESIS','--output',work/'bad-define.cc',source],error='omit explicit SYNTHESIS')
source.write_text('''module Test(input [31:0] a,
`ifdef SYNTHESIS
output [31:0] q
`else
output [63:0] q
`endif
);assign q=a;endmodule''')
run([*base,'--output',work/'bad-ports.cc',source],error='different interfaces')
assert not (work/'bad-ports.cc').exists()
print('PASS native real arithmetic, narrowing, comparisons, both retained branches, interface and define rejection')

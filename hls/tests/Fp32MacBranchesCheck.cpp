// Compare both branches of the same converted Verilog, and optionally the
// earlier replacement already checked against the retimed gate reference.
#define cpphdl_native hardware_native
#include "hardware.h"
#undef cpphdl_native
#define cpphdl_native simulation_native
#include "replacement.h"
#undef cpphdl_native
#ifdef LEGACY_REFERENCE
#define cpphdl_native legacy_native
#include "legacy.h"
#undef cpphdl_native
#endif
#include "../../lib/math/Fp32Bits.h"
#include <array>
#include <deque>
#include <optional>
#include <cstdio>
#include <stdexcept>
int main(){
 constexpr unsigned R=MAC_ROWS,C=MAC_COLS,N=R*C,W=2*N+R+C+4,L=MAC_LATENCY;
 hardware_native::Model hardware;simulation_native::Model simulation;
#ifdef LEGACY_REFERENCE
 legacy_native::Model legacy;
#endif
 using Result=std::array<uint32_t,N+1>;std::deque<std::optional<Result>> expected(L);
 constexpr uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x00800001,0x00800002,
  0x3f7fffff,0x3f7ffffe,0x3f800001,0x7f7fffff,0xff7fffff,0x3f800000,0xbf800000,
  0x7f800000,0xff800000,0x7fc00000,0x7f800001,0xffc00000,0x80800000};
 uint32_t rng=0x18475bad;auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
 unsigned completed=0;constexpr unsigned Clocks=(N==1?100000:8192);
 for(unsigned cycle=0;cycle<Clocks;cycle++){
  bool reset=cycle<3 || cycle%997==0,valid=cycle<Clocks-300 && cycle%7!=1,ready=cycle%211>=53 && cycle%11!=3;
  std::array<uint32_t,W> in;for(auto& x:in)x=cycle%3==0?edges[next()%20]:next();
  if(N==1 && cycle<8000){in[0]=in[3]=edges[cycle%20];in[1]=edges[(cycle/20)%20];in[2]=edges[(cycle/400)%20];}
  in[W-4]=cycle<8000?R:next()%(R+1);in[W-3]=cycle<8000?C:next()%(C+1);in[W-2]=cycle;in[W-1]=next()%3;
  auto drive=[&](auto& g){g.clk[0]=0;g.reset[0]=reset;g.command_valid_in[0]=valid;g.response_ready_in[0]=ready;
   for(unsigned i=0;i<W;i++){g.operation_in[i]=in[i];g.index_in[i]=next();g.value_in[i]=next();}g.step();};
  drive(hardware);drive(simulation);
#ifdef LEGACY_REFERENCE
  drive(legacy);
#endif
  bool outvalid=expected.back().has_value(),advance=!outvalid||ready;
  if(cycle>2 && !reset){
   auto check=[&](auto& g,const char* label){
    if(bool(g.command_ready_out[0])!=advance || bool(g.response_valid_out[0])!=outvalid || g.fault_out[0])throw std::runtime_error("branch handshake/latency");
    if(outvalid)for(unsigned i=0;i<N+1;i++)if(g.result_out[i]!=(*expected.back())[i]){
     std::fprintf(stderr,"%s cycle=%u word=%u got=%08x expected=%08x\n",label,cycle,i,g.result_out[i],(*expected.back())[i]);
     throw std::runtime_error("branch FP32/tag/mask mismatch");}
   };
   check(hardware,"SYNTHESIS");check(simulation,"real");
#ifdef LEGACY_REFERENCE
   check(legacy,"legacy");
#endif
   if(outvalid && ready)completed++;
  }
  if(reset)expected.assign(L,std::nullopt);
  else if(advance){expected.pop_back();std::optional<Result> out;
   if(valid){out.emplace();(*out)[N]=cycle;
    for(unsigned r=0;r<R;r++)for(unsigned c=0;c<C;c++){unsigned i=r*C+c;uint32_t initial=in[N+C+i];
     (*out)[i]=r<in[W-4] && c<in[W-3]?cpphdl_examples::math::fp32_add(initial,
      cpphdl_examples::math::fp32_multiply(in[W-1]==0?in[2*N+C+r]:in[i],in[N+c])):initial;}}
   expected.push_front(out);
  }
  auto edge=[&](auto& g){g.clk[0]=1;g.step();g.step();g.clk[0]=0;g.step();};
  edge(hardware);edge(simulation);
#ifdef LEGACY_REFERENCE
  edge(legacy);
#endif
 }
 if(completed<1000)throw std::runtime_error("insufficient branch transactions");
 std::printf("PASS MAC branches: %ux%u latency=%u, %u clocks, %u completed; FP32, masks, modes, reset, stalls, duplicate edges\n",R,C,L,Clocks,completed);
}

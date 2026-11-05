#include "model.h"
#include "cpphdl_graph_affinity.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>
using Model=cpphdl_native::Model;
struct Oracle {
 std::array<std::array<uint32_t,8>,24> state{};
 std::array<std::array<uint32_t,16>,24> ram{};
 void step(const Model& g) {
  auto old=state;auto mem=ram;
  for(unsigned m=0;m<24;++m) {
   uint32_t y=(old[m][0]+g.data[0])^mem[(m+1)%24][g.address[0]];
   for(unsigned k=0;k<96;++k)y=(y*1664525u)^((y>>3)+k+m+1013904223u);
   bool en=g.enable[0]&(1u<<m);
   if(en||g.reset[0])for(unsigned r=0;r<8;++r)state[m][r]=g.reset[0]?0:y+old[(m+1)%24][r];
   if(en)for(unsigned r=0;r<4;++r)ram[m][(g.address[0]+r)%16]=y+r;
  }
 }
 void check(const Model& g)const {
  for(unsigned m=0;m<24;++m)for(unsigned r=0;r<8;++r)assert(g.result[m*8+r]==state[m][r]);
 }
};
void inputs(Model& g,unsigned t,unsigned mode) {
 g.data[0]=t*1009;g.address[0]=(t/4)%16;g.reset[0]=(t%137)==0;
 g.enable[0]=mode==0?0:mode==1?1u<<((t/32)%24):mode==2?0xffffff:((0x155555u<<((t/32)%2))&0xffffff);
}
void check(Model& g,unsigned offset) {
 Oracle oracle;
 for(unsigned t=0;t<256;++t) {
  inputs(g,t+offset,(t/32)%4);oracle.step(g);g.step();oracle.check(g);
  if(t%31==0) {auto copy=g;g=copy;g.eval(false);oracle.check(g);}
 }
}
int main(int argc,char**argv) {
 cpphdl::graph_runtime::NativeAffinityScope affinity;
 if(argc==1) {
  Model seed;check(seed,0);Model initial,left=initial,right=initial;
  std::thread a([&]{check(left,256);}),b([&]{check(right,512);});a.join();b.join();
  puts("PASS local tasks: independent oracle, cyclic state/RAM snapshots, reset, copies, shared executor");return 0;
 }
 unsigned mode=std::strtoul(argv[1],nullptr,10);Model g;uint64_t digest=0,clocks=0;
 const auto cpu=std::clock();
 auto start=std::chrono::steady_clock::now();double seconds=0;
 do {
  for(unsigned t=0;t<1024;++t){inputs(g,t,mode);g.step();digest=digest*0x100000001b3ull+g.result[0];}
  clocks+=1024;seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 }while(seconds<2.0);
 printf("{\"clocks\":%llu,\"seconds\":%.9f,\"cpu_seconds\":%.9f,\"digest\":\"%016llx\",\"tasks\":%u,\"workers\":%u}\n",
 (unsigned long long)clocks,seconds,double(std::clock()-cpu)/CLOCKS_PER_SEC,
 (unsigned long long)digest,Model::__cpphdl_task_count,Model::__cpphdl_thread_count);
}

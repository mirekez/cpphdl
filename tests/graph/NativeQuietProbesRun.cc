#define cpphdl_native reference
#include "plain.h"
#undef cpphdl_native
#include "model.h"
#define cpphdl_native empty_probe
#include "empty.h"
#undef cpphdl_native
#define cpphdl_native always_probe
#include "always.h"
#undef cpphdl_native
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <memory>
#ifndef QUIET_PROBE_ROUNDS
#define QUIET_PROBE_ROUNDS 8
#endif
static void require(bool ok) { if(!ok) throw std::runtime_error("quiet probe mismatch"); }
static uint32_t hash(uint32_t v) {
    for(unsigned j=0;j<QUIET_PROBE_ROUNDS;++j) { v^=v<<13; v^=v>>17; v^=v<<5; }
    return v;
}
int main(int argc,char** argv) {
    empty_probe::Model empty; empty.eval(false);
    require(empty.__cpphdl_advance_quiet_event());
    always_probe::Model always; always.eval(false);
    require(!always.__cpphdl_advance_quiet_event());
    auto referenceModel=std::make_unique<reference::Model>();
    auto candidate=std::make_unique<cpphdl_native::Model>();
    auto& a=*referenceModel; auto& b=*candidate;
    // Isolate every predicate, including the high bits of each packed word.
    for(unsigned i=0;i<256;++i) {
        a.enabled.fill(0); b.enabled.fill(0);
        a.enabled[i/32]=b.enabled[i/32]=1u<<(i%32);
        a.seed[0]=b.seed[0]=i*7919u;
        a.tick[0]=b.tick[0]=i%2;
        b.eval(false);
        const bool quiet=b.__cpphdl_advance_quiet_event();
        require(quiet==!(hash(i*7919u+i+1)&1));
        a.step(); if(!quiet)b.step();else b.eval(false);
        require(a.counters==b.counters);
    }
    a.enabled.fill(0); b.enabled.fill(0);
    a.seed[0]=b.seed[0]=12345;
    b.eval(false); require(b.__cpphdl_advance_quiet_event());
    a.write[0]=b.write[0]=1; b.eval(false);
    require(!b.__cpphdl_advance_quiet_event());
    a.step();b.step();require(a.memory0==b.memory0 && b.memory0[0][0]==12345);
    a.write[0]=b.write[0]=0;
    // Invalid access or assertion must reject before any state/RAM commits;
    // output-only evaluation and subsequent recovery remain legal.
    for(unsigned kind=0;kind<3;++kind) {
        b.bad[0]=1; b.seed[0]=98765;
        b.write[0]=kind==0; b.read[0]=kind==1; b.fail[0]=kind==2;
        b.eval(false); require(!b.__cpphdl_advance_quiet_event());
        bool rejected=false;
        try{b.eval(true);}catch(const std::runtime_error&){rejected=true;}
        catch(const std::out_of_range&){rejected=true;}
        require(rejected && b.counters==a.counters && b.memory0==a.memory0);
        b.bad[0]=b.write[0]=b.read[0]=b.fail[0]=0;
        b.eval(false);require(b.__cpphdl_advance_quiet_event());
    }
    b.enabled.fill(~0u);
    const unsigned iterations=argc>1?std::stoul(argv[1]):64;
    const auto start=std::chrono::steady_clock::now();
    unsigned active=0;
    for(unsigned i=0;i<iterations;++i) {
        b.seed[0]=i*7919u; b.eval(false);
        active+=!b.__cpphdl_advance_quiet_event();
    }
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    require(active==iterations);
    std::printf("PASS quiet probes: all words, RAM, assertion, rollback; %u evaluations %.6f seconds %.3f/s\n",
                iterations,elapsed,iterations/elapsed);
}

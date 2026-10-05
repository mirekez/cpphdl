#pragma once
#include <array>
#include <cstdio>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VTiledMatVec.h"
#endif
long _system_clock = 0;

class TiledBench : public cpphdl::Module {
    using Q = integer_llm::Q;
#ifdef VERILATOR
    VTiledMatVec dut;
#else
    TiledMatVec dut;
#endif
    std::array<Q,255*LLM_TILE_DEPTH> weights{};
    std::array<Q,LLM_TILE_DEPTH> input{};
    std::array<Q,255> expected{};
    std::mt19937_64 rng{1973};
    bool load=false, command=false, ready=true, mem_ready=false, mem_valid=false, mem_error=false;
    bool pending=false, held=false, expect_fault=false, inject_error=false;
    uint32_t load_address=0, rows=0, base=0x1000, request=0;
    uint64_t load_data=0, mem_data=0;
    unsigned delay=0, outputs=0, request_wait=0;
    uint32_t held_address=0;
    bool previous_valid=false;
    uint64_t previous_data=0;
    unsigned previous_row=0;
#ifndef VERILATOR
    struct Responder : cpphdl::Module {
        cpphdl::hls::ExternalMemoryIf<> memory_in;
        void _assign() {
            memory_in.ready_out = _ASSIGN(false);
            memory_in.valid_out = _ASSIGN(false);
            memory_in.data_out = _ASSIGN(uint64_t(0));
            memory_in.error_out = _ASSIGN(false);
        }
    } responder;
#endif
public:
    unsigned clocks=0, reads=0, overlaps=0, matrices=0;
    TiledBench() {
#ifndef VERILATOR
        cpphdl::hls::bind_external_memory(weights.data(),sizeof(weights),0x1000);
        _assign();
#endif
        reset();
    }
#ifndef VERILATOR
    void _assign() {
        dut.load_in = _ASSIGN(load); dut.load_address_in = _ASSIGN(load_address); dut.load_data_in = _ASSIGN(load_data);
        dut.command_valid_in = _ASSIGN(command); dut.rows_in = _ASSIGN(rows); dut.base_in = _ASSIGN(base);
        dut.ready_in = _ASSIGN(ready);
        assignIf(dut,responder,dut.weights_out,responder.memory_in);
    }
#endif
    bool tick(bool reset=false) {
#ifndef VERILATOR
        if(reset) { dut._work(true); dut._strobe(); ++_system_clock; ++clocks; return false; }
#endif
        mem_ready = !pending && !mem_valid && request_wait==0 && rng()%4!=0;
        if(request_wait) --request_wait;
        if(pending && !mem_valid) {
            if(delay) --delay;
            else { mem_valid=true; mem_error=inject_error; mem_data=uint64_t(weights.at((request-0x1000)/8)); }
        }
#ifdef VERILATOR
        dut.clk=0; dut.reset=reset;
        dut.load_in=load; dut.load_address_in=load_address; dut.load_data_in=load_data;
        dut.command_valid_in=command; dut.rows_in=rows; dut.base_in=base; dut.ready_in=ready;
        dut.weights_out___05Fready_in=mem_ready; dut.weights_out___05Fvalid_in=mem_valid;
        dut.weights_out___05Fdata_in=mem_data; dut.weights_out___05Ferror_in=mem_error;
        dut.eval();
        bool accepted=command && dut.command_ready_out;
        bool valid=dut.valid_out, fault=dut.fault_out;
        uint64_t value=dut.data_out; unsigned row=dut.row_out;
        bool loading=dut.loading_out, computing=dut.computing_out;
        bool req=dut.weights_out___05Fvalid_out, response=mem_valid && dut.weights_out___05Fready_out;
        uint32_t address=dut.weights_out___05Faddr_out;
        if(!reset) {
            if(held && (!req || address!=held_address)) throw std::runtime_error("DDR request changed while stalled");
            held=req && !mem_ready; held_address=address;
            if(req && (dut.weights_out___05Fwrite_out || dut.weights_out___05Fsize_out!=8))
                throw std::runtime_error("bad loader request");
        }
#else
        bool accepted=command && dut.command_ready_out();
        bool valid=dut.valid_out(), fault=dut.fault_out();
        uint64_t value=dut.data_out(); unsigned row=dut.row_out();
        bool loading=dut.loading_out(), computing=dut.computing_out();
#endif
        if(!reset) {
            if(fault && !expect_fault) throw std::runtime_error("tiled compute fault");
            if(previous_valid && (!valid || previous_data!=value || previous_row!=row))
                throw std::runtime_error("output changed under backpressure");
            previous_valid=valid && !ready; previous_data=value; previous_row=row;
            if(valid) {
                if(outputs>=rows || row!=outputs || Q(value)!=expected.at(outputs)) {
                    std::fprintf(stderr,"matrix %u output %u row %u: got %016llx expected %016llx clock %u\n",
                        matrices,outputs,row,(unsigned long long)value,
                        (unsigned long long)(outputs<rows ? uint64_t(expected.at(outputs)) : 0),clocks);
                    throw std::runtime_error("tiled matvec arithmetic/row mismatch");
                }
                if(ready) ++outputs;
            }
            if(loading && computing) ++overlaps;
        }
#ifdef VERILATOR
        dut.clk=1; dut.eval();
        if(response) { pending=false; mem_valid=false; }
        if(!reset && req && mem_ready) {
            if(pending || address<0x1000 || address>=0x1000+sizeof(weights) || address%8)
                throw std::runtime_error("duplicate or out-of-range DDR request");
            pending=true; request=address; delay=1+rng()%19; ++reads;
        }
#else
        dut._work(reset); dut._strobe();
#endif
        ++_system_clock; ++clocks;
        return !reset && accepted;
    }
    void reset() {
        load=false; command=false; ready=true; outputs=0; pending=false; mem_valid=false; mem_error=false;
        held=false; previous_valid=false; inject_error=false; expect_fault=false;
        tick(true); tick(true);
    }
    void prepare(unsigned count, bool edges=false) {
        rows=count;
        for(auto& v:weights) v=Q(rng()%(4*integer_llm::one))-2*integer_llm::one;
        for(auto& v:input) v=Q(rng()%(4*integer_llm::one))-2*integer_llm::one;
        if(edges) {
            constexpr Q edge[]={INT64_MIN,INT64_MAX,-1,1,0,integer_llm::one,-integer_llm::one,17};
            for(unsigned i=0;i<weights.size();++i) weights[i]=edge[i%8];
            for(unsigned i=0;i<input.size();++i) input[i]=edge[(i+3)%8];
        }
        integer_llm::mmul(expected.data(),weights.data(),input.data(),rows,1,LLM_TILE_DEPTH);
        for(unsigned i=0;i<input.size();++i) { load=true; load_address=i; load_data=uint64_t(input[i]); tick(); }
        load=false; command=true;
        unsigned watchdog=0;
        while(!tick()) if(++watchdog>1000) throw std::runtime_error("command timeout");
        command=false;
        // The design must retain the accepted geometry, not these input pins.
        base=0x1000;
    }
    void matrix(unsigned count,bool edges=false,bool fresh=true) {
        if(fresh) reset(); else outputs=0;
        unsigned before=reads, overlap_before=overlaps;
        prepare(count,edges);
        unsigned watchdog=0;
        // Stall long enough to fill both banks and test ownership on wraparound.
        while(outputs!=rows && ++watchdog<100000) { ready=watchdog>400 && rng()%3!=0; tick(); }
        if(outputs!=rows) throw std::runtime_error("tiled matvec timeout");
        ready=true; for(unsigned i=0;i<30;++i) tick();
#ifdef VERILATOR
        if(reads-before!=count*LLM_TILE_DEPTH) throw std::runtime_error("wrong DDR read count");
#endif
        if(count>1 && overlaps==overlap_before) throw std::runtime_error("no loader/compute overlap");
        ++matrices;
    }
    void cancel() {
        reset(); prepare(4); for(unsigned i=0;i<15;++i) tick();
        reset(); for(unsigned i=0;i<100;++i) tick();
        if(outputs) throw std::runtime_error("reset leaked output");
    }
    void error() {
#ifdef VERILATOR
        reset(); inject_error=true; expect_fault=true; prepare(4);
        unsigned watchdog=0;
        while(!dut.fault_out && ++watchdog<1000) tick();
        if(!dut.fault_out) throw std::runtime_error("DDR error not propagated");
        for(unsigned i=0;i<50;++i) tick();
        if(outputs) throw std::runtime_error("DDR error produced valid result");
#endif
    }
};
int main() {
    TiledBench bench;
    bench.matrix(16,true);
    for(unsigned i=0;i<24;++i) bench.matrix(1+i%16);
    bench.matrix(255);
    bench.matrix(5,false,false);
    bench.cancel(); bench.error(); bench.matrix(3);
    std::printf("PASS: %u tiled matrices; %u DDR reads; %u overlapping loader/compute clocks; %u clocks\n",
        bench.matrices,bench.reads,bench.overlaps,bench.clocks);
}

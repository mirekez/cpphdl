#pragma once
#include <array>
#include <deque>
#include <cstdio>
#include <random>
#include <stdexcept>
#ifdef VERILATOR
#include "VStreamingMatVec.h"
#endif
long _system_clock = 0;

class StreamingBench : public cpphdl::Module {
    using Q = integer_llm::Q;
    struct Read { uint32_t address; unsigned due; bool error; };
#ifdef VERILATOR
    VStreamingMatVec dut;
#else
    StreamingMatVec dut;
    struct Controller : cpphdl::Module {
        cpphdl::hls::DramReadIf<512> memory_in;
        bool ready=false, valid=false, error=false;
        cpphdl::logic<512> data=0;
        void _assign() {
            memory_in.ready_out = _ASSIGN(ready);
            memory_in.valid_out = _ASSIGN(valid);
            memory_in.error_out = _ASSIGN(error);
            memory_in.data_out = _ASSIGN(data);
        }
    } controller;
#endif
    std::array<Q, ((255*LLM_TILE_DEPTH+7)/8)*8> weights{};
    std::array<Q, LLM_TILE_DEPTH> input{};
    std::array<Q,255> expected{};
    std::deque<Read> pending;
    std::mt19937_64 values{1973}, timing{91827};
    bool load=false, command=false, ready=true, expect_fault=false, inject_error=false, pause=false, stall_requests=false;
    uint32_t load_address=0, rows=0, base=0x1000;
    uint64_t load_data=0;
    unsigned clocks=0, outputs=0, reads=0, received=0, products=0, peak=0, latency=0;
    unsigned first_product=0, last_product=0, last_output=0, max_gap=0;
    bool held=false, held_request=false, fault_seen=false;
    uint64_t held_data=0;
    unsigned held_row=0, held_address=0;
    static void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
public:
    StreamingBench() { _assign(); reset(); }
    void _assign() {
#ifndef VERILATOR
        dut.load_in = _ASSIGN(load); dut.load_address_in = _ASSIGN(load_address); dut.load_data_in = _ASSIGN(load_data);
        dut.command_valid_in = _ASSIGN(command); dut.rows_in = _ASSIGN(rows); dut.base_in = _ASSIGN(base);
        dut.ready_in = _ASSIGN(ready);
        assignIf(dut,controller,dut.weights_out,controller.memory_in);
#endif
    }
    bool tick(bool reset=false) {
#ifndef VERILATOR
        if(reset) { dut._work(true); dut._strobe(); ++clocks; ++_system_clock; return false; }
#endif
        bool mem_ready = !stall_requests && pending.size()<LLM_READ_WINDOW && (latency || timing()%4!=0);
        bool mem_valid = !pause && !pending.empty() && pending.front().due<=clocks;
        bool mem_error = mem_valid && pending.front().error;
        cpphdl::logic<512> beat=0;
        if(mem_valid) for(int i=7;i>=0;--i)
            beat=(beat<<64)|uint64_t(weights.at((pending.front().address-0x1000)/8+i));
#ifdef VERILATOR
        dut.clk=0; dut.reset=reset;
        dut.load_in=load; dut.load_address_in=load_address; dut.load_data_in=load_data;
        dut.command_valid_in=command; dut.rows_in=rows; dut.base_in=base; dut.ready_in=ready;
        dut.weights_out___05Fready_in=mem_ready; dut.weights_out___05Fvalid_in=mem_valid;
        dut.weights_out___05Ferror_in=mem_error;
        for(unsigned i=0;i<16;++i) dut.weights_out___05Fdata_in[i]=uint32_t(beat>>(32*i));
        dut.eval();
        bool accepted=command && dut.command_ready_out, valid=dut.valid_out, fault=dut.fault_out;
        uint64_t data=dut.data_out; unsigned row=dut.row_out;
        bool req=dut.weights_out___05Fvalid_out, response=mem_valid && dut.weights_out___05Fready_out;
        uint32_t address=dut.weights_out___05Faddr_out;
        bool product=dut.product_accepted_out;
#else
        controller.ready=mem_ready; controller.valid=mem_valid; controller.error=mem_error; controller.data=beat;
        bool accepted=command && dut.command_ready_out(), valid=dut.valid_out(), fault=dut.fault_out();
        uint64_t data=dut.data_out(); unsigned row=dut.row_out();
        bool req=dut.weights_out.valid_in(), response=mem_valid && dut.weights_out.ready_in();
        uint32_t address=uint32_t(dut.weights_out.addr_in());
        bool product=dut.product_accepted_out();
#endif
        if(!reset) {
            fault_seen |= fault;
            check(!fault || expect_fault,"unexpected streaming fault");
            if(held_request) check(req && address==held_address,"unstable stalled DDR request");
            held_request=req && !mem_ready; held_address=address;
            if(held) check(valid && data==held_data && row==held_row,"unstable stalled row result");
            held=valid && !ready; held_data=data; held_row=row;
            if(valid) {
                if(outputs>=rows || row!=outputs || Q(data)!=expected.at(outputs)) {
                    std::fprintf(stderr,"row=%u expected_row=%u actual=%016llx expected=%016llx clock=%u\n",
                        row,outputs,(unsigned long long)data,
                        (unsigned long long)(outputs<rows ? uint64_t(expected.at(outputs)) : 0),clocks);
                    throw std::runtime_error("streaming row arithmetic/order mismatch");
                }
                if(ready) {
                    if(outputs) max_gap=std::max(max_gap,clocks-last_output);
                    last_output=clocks; ++outputs;
                }
            }
            if(product) { if(!products) first_product=clocks; last_product=clocks; ++products; }
            if(response) { pending.pop_front(); ++received; }
            if(req && mem_ready) {
                check(address==0x1000+64*reads,"DDR skipped or repeated a beat");
                check(address+64<=0x1000+sizeof(weights),"DDR request outside padded matrix");
                pending.push_back({address,clocks+(latency ? latency : 1+unsigned(timing()%61)),inject_error});
                ++reads; peak=std::max(peak,unsigned(pending.size()));
                check(peak<=LLM_READ_WINDOW,"DDR credit overflow");
            }
        }
#ifdef VERILATOR
        dut.clk=1; dut.eval(); dut.clk=0; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        ++clocks; ++_system_clock;
        return !reset && accepted;
    }
    void reset() {
        command=load=false; ready=true; pause=stall_requests=false; inject_error=expect_fault=fault_seen=false;
        held=held_request=false; pending.clear(); outputs=reads=received=products=peak=0;
        first_product=last_product=last_output=max_gap=0; base=0x1000;
        tick(true); tick(true);
    }
    void prepare(unsigned count,bool edges=false) {
        rows=count;
        for(auto& w:weights) w=Q(values()%(4*integer_llm::one))-2*integer_llm::one;
        for(auto& x:input) x=Q(values()%(4*integer_llm::one))-2*integer_llm::one;
        if(edges) {
            constexpr Q e[]={INT64_MIN,INT64_MAX,-1,1,0,integer_llm::one,-integer_llm::one,17};
            for(unsigned i=0;i<weights.size();++i) weights[i]=e[i%8];
            for(unsigned i=0;i<input.size();++i) input[i]=e[(i+3)%8];
        }
        integer_llm::mmul(expected.data(),weights.data(),input.data(),rows,1,LLM_TILE_DEPTH);
        for(unsigned i=0;i<input.size();++i) { load=true; load_address=i; load_data=uint64_t(input[i]); tick(); }
        load=false; command=true; check(tick(),"stream command not accepted"); command=false;
        // Accepted inputs must be retained throughout the transaction.
        rows=0x1234; base=0x87654321; tick(); rows=count; base=0x1000;
    }
    void matrix(unsigned count,bool edges=false,bool fresh=true) {
        if(fresh) reset(); else { outputs=reads=received=products=peak=0; }
        latency=0; prepare(count,edges);
        unsigned watchdog=0;
        while(outputs!=rows && ++watchdog<100000) { ready=watchdog>400 && watchdog%199>30 && timing()%3!=0; tick(); }
        check(outputs==rows,"stream matrix timeout");
        check(products==rows*LLM_TILE_DEPTH,"wrong product count");
        check(reads==(rows*LLM_TILE_DEPTH+7)/8 && received==reads && pending.empty(),"wrong DDR completion count");
        ready=true; for(unsigned i=0;i<30;++i) tick();
    }
    void benchmark(unsigned delay) {
        reset(); latency=delay; prepare(255);
        unsigned begin=clocks-1;
        while(outputs!=rows && clocks-begin<100000) tick();
        check(outputs==rows,"stream benchmark timeout");
        unsigned span=last_product-first_product+1;
        check(products==rows*LLM_TILE_DEPTH,"benchmark dropped product");
        // Once prefetched, neither row reduction nor DDR should create bubbles.
        if(LLM_READ_WINDOW>=8 && delay<=60) {
            check(span==products,"avoidable product pipeline bubble");
            check(max_gap==LLM_TILE_DEPTH,"row-boundary bubble");
            check(clocks-begin<=products+delay+32,"excessive fill/drain latency");
        }
        if(delay>1) check(peak>=2,"reads did not overlap");
        std::printf("BENCH streaming depth=%u latency=%u rows=%u cycles=%u reads=%u peak=%u products=%u span=%u max_row_gap=%u\n",
            unsigned(LLM_TILE_DEPTH),delay,rows,clocks-begin,reads,peak,products,span,max_gap);
    }
    void cancel() {
        reset(); latency=1; pause=true; prepare(16);
        for(unsigned i=0;i<30;++i) tick();
        check(!pending.empty(),"reset did not exercise pending DDR");
        reset(); for(unsigned i=0;i<100;++i) tick(); check(outputs==0,"reset leaked a row");
    }
    void error() {
        reset(); latency=1; inject_error=expect_fault=true; prepare(8);
        unsigned watchdog=0;
        while(!fault_seen && ++watchdog<1000) tick();
        check(fault_seen,"DDR error missing");
        for(unsigned i=0;i<50;++i) tick(); check(!outputs,"errored matrix produced a row");
        reset(); latency=10; inject_error=expect_fault=true; prepare(32); stall_requests=true;
        watchdog=0;
        while(!fault_seen && ++watchdog<1000) tick();
        check(fault_seen && held_request,"error did not exercise a stalled request");
        unsigned before=reads;
        for(unsigned i=0;i<5;++i) tick();
        stall_requests=false;
        for(unsigned i=0;i<20;++i) tick();
        check(reads==before+1 && !outputs,"fault did not finish exactly the held request");
        // A later read error must not withdraw a row already offered to us.
        reset(); latency=1; prepare(255); ready=false;
        watchdog=0;
        while(!held && ++watchdog<1000) tick();
        check(held,"error test did not hold a result");
        expect_fault=true; inject_error=true; latency=20;
        if(!pending.empty()) { pending.front().error=true; pending.front().due=clocks+20; }
        ready=true;
        // Let the prefetch queue progress, then hold the next visible result.
        for(unsigned i=0;i<10 && !fault_seen;++i) tick();
        ready=false; watchdog=0;
        while(!fault_seen && ++watchdog<1000) tick();
        check(fault_seen && held,"late read error did not preserve an offered row");
        for(unsigned i=0;i<10;++i) tick();
        ready=true; for(unsigned i=0;i<30;++i) tick();
    }
    void invalid() {
        for(unsigned test=0;test<4;++test) {
            reset(); expect_fault=true; rows=test==0 ? 0 : test==1 ? 256 : 1;
            base=test==2 ? 0x1008 : test==3 ? 0xffffffc0 : 0x1000;
            command=true; check(tick(),"invalid command not accepted for validation"); command=false;
            for(unsigned i=0;i<20;++i) tick();
            check(fault_seen && !reads && !outputs,"invalid geometry/address escaped validation");
        }
    }
};

int main() {
    try {
        StreamingBench bench;
        bench.matrix(16,true);
        for(unsigned i=0;i<24;++i) bench.matrix(1+i%16);
        bench.matrix(255); bench.matrix(5,false,false);
        bench.cancel(); bench.error(); bench.invalid(); bench.matrix(3);
        bench.benchmark(1); bench.benchmark(20); bench.benchmark(60); bench.benchmark(120);
        std::puts("PASS: streaming matrices, backpressure, reset and DDR error");
    } catch(const std::exception& e) { std::fprintf(stderr,"StreamingMatVec: %s\n",e.what()); return 1; }
}

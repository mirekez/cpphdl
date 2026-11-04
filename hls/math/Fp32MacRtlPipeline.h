#pragma once
#include "../../lib/math/Fp32Mac.h"

// The complete sequential replacement, shared by direct C++/native-graph
// simulation and RTL generation. LATENCY comes from the HLS retiming report.
template<unsigned LATENCY=2>
class Fp32MacRtlPipeline : public cpphdl::Module {
    static_assert(LATENCY>=2,"FP32 MAC requires LATENCY >= 2");
public:
    _PORT(bool) command_valid_in;
    _PORT(bool) response_ready_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_valid_out;
    _PORT(uint32_t) result_out;
    _PORT(uint32_t) fault_out;
    Fp32Mac mac;
    cpphdl::reg<cpphdl::u1> valid[LATENCY];
    cpphdl::reg<cpphdl::u32> delay[LATENCY>2?LATENCY-2:1];
    void _assign() {
        command_ready_out=_ASSIGN(!valid[LATENCY-1] || response_ready_in());
        response_valid_out=_ASSIGN(bool(valid[LATENCY-1]));
        fault_out=_ASSIGN(0u);
        mac.a_in=_ASSIGN(index_in());mac.b_in=_ASSIGN(value_in());mac.c_in=_ASSIGN(operation_in());
        mac.en_in=_ASSIGN(command_ready_out());mac._assign();
        if(LATENCY==2)result_out=_ASSIGN(mac.result_out());
        else result_out=_ASSIGN(uint32_t(delay[LATENCY-3]));
    }
    void _work(bool reset) {
        if(command_ready_out()) {
            valid[0]._next=command_valid_in();
            for(unsigned i=1;i<LATENCY;++i)valid[i]._next=bool(valid[i-1]);
            if(LATENCY>2) {
                delay[0]._next=mac.result_out();
                for(unsigned i=1;i<LATENCY-2;++i)delay[i]._next=uint32_t(delay[i-1]);
            }
        }
        if(reset) {
            for(unsigned i=0;i<LATENCY;++i)valid[i].clr();
            for(unsigned i=0;i<(LATENCY>2?LATENCY-2:1);++i)delay[i].clr();
        }
        mac._work(reset);
    }
    void _strobe() {
        for(unsigned i=0;i<LATENCY;++i)valid[i].strobe();
        for(unsigned i=0;i<(LATENCY>2?LATENCY-2:1);++i)delay[i].strobe();
        mac._strobe();
    }
};

// Word layout matches the HLS MacInput / MacResult packet ABI. All lane
// arithmetic is an instance of the same Fp32Mac/FpAdd C++RTL implementation.
template<unsigned Rows=4,unsigned Cols=4,unsigned LATENCY=2>
class Fp32MacRtlArray : public cpphdl::Module {
    static_assert(Rows>=1 && Rows<=64 && Cols>=1 && Cols<=64);
    static_assert(LATENCY>=2,"FP32 MAC array requires LATENCY >= 2");
public:
    static constexpr unsigned N=Rows*Cols,Words=2*N+Rows+Cols+4;
    _PORT(bool) command_valid_in;
    _PORT(bool) response_ready_in;
    _PORT(cpphdl::logic<(2*Rows*Cols+Rows+Cols+4)*32>) operation_in;
    _PORT(cpphdl::logic<(2*Rows*Cols+Rows+Cols+4)*32>) index_in;
    _PORT(cpphdl::logic<(2*Rows*Cols+Rows+Cols+4)*32>) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_valid_out;
    _PORT(cpphdl::logic<(Rows*Cols+1)*32>) result_out;
    _PORT(uint32_t) fault_out;
    Fp32MacRtlPipeline<LATENCY> lanes[Rows][Cols];
    cpphdl::reg<cpphdl::u32> tags[LATENCY];
    cpphdl::reg<cpphdl::logic<N>> masks[LATENCY];
    cpphdl::reg<cpphdl::logic<N*32>> initials[LATENCY];
    cpphdl::logic<N> mask_comb;
    cpphdl::logic<(N+1)*32> result_comb;
    cpphdl::logic<N>& mask_comb_func() {
        cpphdl::logic<Words*32> input=operation_in();mask_comb=0;
        for(unsigned i=0;i<Rows;++i)for(unsigned j=0;j<Cols;++j)
            mask_comb.bits(i*Cols+j,i*Cols+j)=i<uint32_t(input.bits((Words-4)*32+31,(Words-4)*32)) &&
                j<uint32_t(input.bits((Words-3)*32+31,(Words-3)*32));
        return mask_comb;
    }
    cpphdl::logic<(N+1)*32>& result_comb_func() {
        cpphdl::logic<N> mask=masks[LATENCY-1];
        cpphdl::logic<N*32> initial=initials[LATENCY-1];
        result_comb=0;
        result_comb.bits(N*32+31,N*32)=tags[LATENCY-1];
        for(unsigned i=0;i<Rows;++i)for(unsigned j=0;j<Cols;++j)
            result_comb.bits((i*Cols+j)*32+31,(i*Cols+j)*32)=
                mask.bits(i*Cols+j,i*Cols+j) ? lanes[i][j].result_out() :
                uint32_t(initial.bits((i*Cols+j)*32+31,(i*Cols+j)*32));
        return result_comb;
    }
    void _assign() {
        for(unsigned i=0;i<Rows;++i)for(unsigned j=0;j<Cols;++j) {
            lanes[i][j].command_valid_in=_ASSIGN(command_valid_in());
            lanes[i][j].response_ready_in=_ASSIGN(response_ready_in());
            lanes[i][j].operation_in=_ASSIGN_IJ(uint32_t(operation_in().bits((N+Cols+i*Cols+j)*32+31,(N+Cols+i*Cols+j)*32)));
            lanes[i][j].index_in=_ASSIGN_IJ(uint32_t(operation_in().bits((Words-1)*32+31,(Words-1)*32))==0 ?
                uint32_t(operation_in().bits((2*N+Cols+i)*32+31,(2*N+Cols+i)*32)) :
                uint32_t(operation_in().bits((i*Cols+j)*32+31,(i*Cols+j)*32)));
            lanes[i][j].value_in=_ASSIGN_J(uint32_t(operation_in().bits((N+j)*32+31,(N+j)*32)));
            lanes[i][j]._assign();
        }
        command_ready_out=_ASSIGN(lanes[0][0].command_ready_out());
        response_valid_out=_ASSIGN(lanes[0][0].response_valid_out());
        result_out=_ASSIGN_COMB(result_comb_func());fault_out=_ASSIGN(0u);
    }
    void _work(bool reset) {
        if(command_ready_out()) {
            tags[0]._next=uint32_t(operation_in().bits((Words-2)*32+31,(Words-2)*32));
            masks[0]._next=mask_comb_func();
            initials[0]._next=operation_in().bits((2*N+Cols)*32-1,(N+Cols)*32);
            for(unsigned i=1;i<LATENCY;++i) {
                tags[i]._next=uint32_t(tags[i-1]);masks[i]._next=cpphdl::logic<N>(masks[i-1]);initials[i]._next=cpphdl::logic<N*32>(initials[i-1]);
            }
        }
        if(reset)for(unsigned i=0;i<LATENCY;++i){tags[i].clr();masks[i].clr();initials[i].clr();}
        for(unsigned i=0;i<Rows;++i)for(unsigned j=0;j<Cols;++j)lanes[i][j]._work(reset);
    }
    void _strobe() {
        for(unsigned i=0;i<LATENCY;++i){tags[i].strobe();masks[i].strobe();initials[i].strobe();}
        for(unsigned i=0;i<Rows;++i)for(unsigned j=0;j<Cols;++j)lanes[i][j]._strobe();
    }
};

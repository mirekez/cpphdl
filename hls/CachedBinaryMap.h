#pragma once
#include "SynchronousRam.h"

namespace cpphdl::hls {
// Contiguous two-input map. The callable is an ordered ready/valid pipeline;
// arithmetic latency does not serialize memory transactions. Entire bounded
// inputs are cached before stores, so either input can alias the destination.
template<class Compute>
class CachedBinaryMap:public cpphdl::Module {
public:
    using Beat=cpphdl::array<16,cpphdl::u32>;
    Compute compute;
    static constexpr uint32_t CacheBytes=2*8192*4;
    _PORT(bool) command_valid_in;_PORT(bool) command_ready_out;
    _PORT(uint32_t) source_in;_PORT(uint32_t) second_in;_PORT(uint32_t) destination_in;
    _PORT(uint32_t) count_in;_PORT(uint32_t) operation_in;
    _PORT(bool) response_valid_out;_PORT(bool) response_ready_in;_PORT(uint32_t) fault_out;
    _PORT(uint32_t) m_address_out;_PORT(Beat) m_writedata_out;_PORT(cpphdl::logic<64>) m_byteenable_out;
    _PORT(bool) m_read_out;_PORT(bool) m_write_out;
    _PORT(bool) m_waitrequest_in;_PORT(Beat) m_readdata_in;
    _PORT(bool) m_readdatavalid_in;_PORT(bool) m_readdataerror_in;
    _PORT(uint32_t) profile_active_out;_PORT(uint32_t) profile_accept_out;_PORT(uint32_t) profile_complete_out;
private:
    SynchronousRam<16,512> first,second;
    cpphdl::reg<cpphdl::u<2>> state;
    cpphdl::reg<cpphdl::u32> source,other,destination,count,operation,error,requested,returned;
    cpphdl::reg<cpphdl::u32> read_index,read_tag,issued,completed,write_index,fill;
    cpphdl::reg<cpphdl::u1> held,read_valid;
    cpphdl::reg<Beat> output;
    uint32_t beats(){return (uint32_t(count)+15)/16;}
    bool valid_command(){return count_in()>0&&count_in()<=8192&&!((source_in()|second_in()|destination_in())&63)&&
        uint64_t(source_in())+((uint64_t(count_in())+15)/16)*64<=0x100000000ull&&
        uint64_t(second_in())+((uint64_t(count_in())+15)/16)*64<=0x100000000ull&&
        uint64_t(destination_in())+uint64_t(count_in())*4<=0x100000000ull;}
    bool load_return(){return state==1&&m_readdatavalid_in()&&(returned<requested||(m_read_out()&&!m_waitrequest_in()));}
    bool full(){return fill==16||(completed==count&&fill!=0);}
    bool fetch(){return state==2&&error==0&&read_index<count&&(!bool(read_valid)||compute.ready_out());}
public:
    void _assign(){
        command_ready_out=_ASSIGN(state==0);response_valid_out=_ASSIGN(state==3);fault_out=_ASSIGN(uint32_t(error));
        m_read_out=_ASSIGN(state==1&&(bool(held)||(error==0&&requested<2*beats()&&requested-returned<64)));
        m_write_out=_ASSIGN(state==2&&full()&&error==0);
        m_address_out=_ASSIGN(state==1?(requested<beats()?uint32_t(source)+uint32_t(requested)*64:uint32_t(other)+(uint32_t(requested)-beats())*64):uint32_t(destination)+uint32_t(write_index)*4);
        m_writedata_out=_ASSIGN_REG(output);m_byteenable_out=_ASSIGN(cpphdl::logic<64>(fill==16?~uint64_t(0):(uint64_t(1)<<((uint32_t(fill)*4)&63))-1));
        first.write_in=_ASSIGN(load_return()&&returned<beats()&&error==0&&!m_readdataerror_in());first.write_address_in=_ASSIGN(uint32_t(returned));first.data_in=_ASSIGN(m_readdata_in());
        second.write_in=_ASSIGN(load_return()&&returned>=beats()&&error==0&&!m_readdataerror_in());second.write_address_in=_ASSIGN(uint32_t(returned)-beats());second.data_in=_ASSIGN(m_readdata_in());
        first.read_in=_ASSIGN(fetch());second.read_in=_ASSIGN(fetch());first.read_address_in=_ASSIGN(uint32_t(read_index)/16);second.read_address_in=_ASSIGN(uint32_t(read_index)/16);first._assign();second._assign();
        compute.valid_in=_ASSIGN(state==2&&bool(read_valid)&&error==0);compute.operation_in=_ASSIGN(uint32_t(operation));
        compute.a_in=_ASSIGN(uint32_t(first.data_out()[uint32_t(read_tag)&15]));compute.b_in=_ASSIGN(uint32_t(second.data_out()[uint32_t(read_tag)&15]));
        compute.ready_in=_ASSIGN(state==2&&(error!=0||!full()||!m_waitrequest_in()));compute._assign();
        profile_active_out=_ASSIGN((state==1?1u:0u)|(state==2&&read_index<count?2u:0u)|(issued!=completed?4u:0u)|(state==2&&fill!=0?8u:0u));
        profile_accept_out=_ASSIGN((m_read_out()&&!m_waitrequest_in()?1u:0u)|(fetch()?2u:0u)|(compute.valid_in()&&compute.ready_out()?4u:0u)|(m_write_out()&&!m_waitrequest_in()?8u:0u));
        profile_complete_out=_ASSIGN((load_return()?1u:0u)|(compute.valid_in()&&compute.ready_out()?2u:0u)|(compute.valid_out()&&compute.ready_in()?4u:0u)|(m_write_out()&&!m_waitrequest_in()?8u:0u));
    }
    void _work(bool reset){
        bool drain,finish;uint32_t n,slot;
        first._work(reset);second._work(reset);compute._work(reset);
        if(reset){state.clr();source.clr();other.clr();destination.clr();count.clr();operation.clr();error.clr();requested.clr();returned.clr();read_index.clr();read_tag.clr();issued.clr();completed.clr();write_index.clr();fill.clr();held.clr();read_valid.clr();output.clr();}
        else {
            if(state==0&&command_valid_in()){
                source._next=source_in();other._next=second_in();destination._next=destination_in();count._next=count_in();operation._next=operation_in();
                state._next=valid_command()?1u:3u;error._next=valid_command()?0u:1u;requested._next=0;returned._next=0;read_index._next=0;issued._next=0;completed._next=0;write_index._next=0;fill._next=0;held._next=false;read_valid._next=false;
            }
            if(state==1){
                if(m_read_out()){held._next=m_waitrequest_in();if(!m_waitrequest_in())requested._next=uint32_t(requested)+1;}
                if(load_return()){returned._next=uint32_t(returned)+1;if(m_readdataerror_in())error._next=2;}
                n=uint32_t(returned)+(load_return()?1u:0u);
                if(error==0&&!m_readdataerror_in()&&n==2*beats())state._next=2;
                if(error!=0&&n==uint32_t(requested)+(m_read_out()&&!m_waitrequest_in()?1u:0u)&&!(m_read_out()&&m_waitrequest_in()))state._next=3;
            }
            if(compute.valid_in()&&compute.ready_out()){read_valid._next=false;issued._next=uint32_t(issued)+1;}
            if(fetch()){read_valid._next=true;read_tag._next=uint32_t(read_index);read_index._next=uint32_t(read_index)+1;}
            drain=m_write_out()&&!m_waitrequest_in();finish=compute.valid_out()&&compute.ready_in();
            if(drain){write_index._next=uint32_t(write_index)+uint32_t(fill);fill._next=0;if(uint32_t(write_index)+uint32_t(fill)==count)state._next=3;}
            if(finish){
                slot=drain?0u:uint32_t(fill);completed._next=uint32_t(completed)+1;
                if(error==0){output._next[slot]=compute.value_out();fill._next=slot+1;}
                if(compute.fault_out())error._next=3;
            }
            if(state==2&&error!=0&&completed==issued)state._next=3;
            if(state==3&&response_ready_in())state._next=0;
        }
    }
    void _strobe(){first._strobe();second._strobe();compute._strobe();state.strobe();source.strobe();other.strobe();destination.strobe();count.strobe();operation.strobe();error.strobe();requested.strobe();returned.strobe();read_index.strobe();read_tag.strobe();issued.strobe();completed.strobe();write_index.strobe();fill.strobe();held.strobe();read_valid.strobe();output.strobe();}
};
}

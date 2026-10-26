#pragma once
#include "cpphdl.h"

class ChildResetCounter:public cpphdl::Module {
    cpphdl::reg<cpphdl::u8> count;
public:
    _PORT(uint32_t) value_out;
    void _assign(){value_out=_ASSIGN(uint32_t(count));}
    void _work(bool reset){if(reset)count.clr();else count._next=uint32_t(count)+1;}
    void _strobe(){count.strobe();}
};

class ChildResetTop:public cpphdl::Module {
    cpphdl::reg<cpphdl::u<2>> phase;
public:
    ChildResetCounter shared,cleared,phased;
    _PORT(bool) clear_in;
    _PORT(uint32_t) shared_out;_PORT(uint32_t) cleared_out;_PORT(uint32_t) phased_out;
    void _assign(){
        shared._assign();cleared._assign();phased._assign();
        shared_out=_ASSIGN(shared.value_out());cleared_out=_ASSIGN(cleared.value_out());phased_out=_ASSIGN(phased.value_out());
    }
    // The alternate parameter spelling must bind to the physical reset port.
    void _work(bool clear_all){
        shared._work(clear_all);cleared._work(clear_all||clear_in());phased._work(clear_all||phase==2);
        if(clear_all)phase.clr();else phase._next=uint32_t(phase)+1;
    }
    void _strobe(){shared._strobe();cleared._strobe();phased._strobe();phase.strobe();}
};

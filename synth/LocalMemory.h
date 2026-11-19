#pragma once
#include "MemoryContinuation.h"

namespace cpphdl::synth {
// Compiler-owned local byte arena. Independent byte banks permit scalar and
// subobject accesses without read/modify/write of sibling bytes. Every read is
// synchronous; the registered reply holds its data under continuation stalls.
inline void connectLocalMemory(graph::Graph& g,const std::string& scope,unsigned bytes,
    unsigned dataBits,const std::map<std::string,graph::Value>& pins) {
    using namespace graph;
    if(dataBits<64 || bytes<16)throw std::runtime_error("invalid local memory geometry");
    const auto previousScope=g.currentScope;g.currentScope=scope;
    auto both=[&](Value a,Value b){return g.binary("and",a,b,1);};
    auto either=[&](Value a,Value b){return g.binary("or",a,b,1);};
    auto inv=[&](Value a){return g.unary("not",a);};
    auto reg=[&](const std::string& name,unsigned width){return g.wire(width,scope+"."+name,"state");};
    auto resize=[](Value value,unsigned width){value.resize(width,0);return value;};
    const auto reset=pins.at("reset"),running=inv(reset);
    auto pending=reg("reply_valid",1),written=reg("reply_write",1);
    auto replyReady=g.wire(1,scope+".reply_ready");
    auto ramReady=both(running,either(inv(pending),replyReady));
    auto prepared=reg("prepared_valid",1),preparedWrite=reg("prepared_write",1),preparedLow=reg("prepared_offset",3);
    auto ready=both(running,either(inv(prepared),ramReady));
    // The arbitration tree ends at this register. Address calculation and the
    // bank-address preparation then gets its own cycle instead of sharing the
    // full path from every source access site's request selection. This is an
    // two-entry queue with registered capacity. Do not let downstream reply
    // occupancy propagate back through every source region's request-ready:
    // that global feedback path is routing-dominated in full-model placement.
    // The spare entry still allows one access per clock in steady state.
    Value request;
    for(const auto& field:{"addr_in","size_in","write_in","data_in"}) {
        const auto& value=pins.at("local_out."+std::string(field));
        request.insert(request.end(),value.begin(),value.end());
    }
    auto requests=buildMemoryContinuation(g,scope+".requests",2,
        {{"reset",reset},{"command_valid_in",pins.at("local_out.valid_in")},
         {"request_ready_in",{1}},{"context_in",request},{"memory_valid_in",{1}},
         {"memory_data_in",{0}},{"memory_error_in",{0}},
         {"response_ready_in",ready}},false);
    const auto savedRequest=requests.at("context_out");
    const auto address=slice(savedRequest,0,32),size=slice(savedRequest,32,8),write=slice(savedRequest,40,1);
    auto prepare=both(ready,requests.at("response_valid_out"));
    auto accept=both(ramReady,prepared);
    g.states.push_back({prepared,g.mux(reset,Value{0},g.mux(prepare,Value{1},g.mux(ramReady,Value{0},prepared))),{1}});
    g.states.push_back({preparedWrite,g.mux(prepare,write,preparedWrite),{1}});
    // Access widths are checked during lowering. As in ordinary C++, local
    // pointers must designate live objects; out-of-bounds accesses are not a
    // runtime exception contract. Do not synthesize a byte-arena bounds checker
    // into every local request. Fields may straddle an eight-byte word.
    auto readEnable=both(accept,inv(preparedWrite));
    unsigned indexBits=1;while((uint64_t(1)<<indexBits)<(uint64_t(bytes)+7)/8)++indexBits;
    // Match the addressable hardware depth. A non-power-of-two declaration
    // requires the generic emitter to guard an out-of-range read with a data
    // mux before the register, which prevents Quartus block-RAM inference.
    // The additional addresses are outside the source arena's object contract.
    const unsigned depth=1u<<indexBits;
    Value data;
    const auto low=slice(address,0,3);
    g.states.push_back({preparedLow,g.mux(prepare,low,preparedLow),{1}});
    for(unsigned lane=0;lane<8;++lane) {
        const auto m=g.memories.size();g.memories.push_back({scope+".lane_"+std::to_string(lane),8,depth});
        auto offset=g.binary("sub",constant(lane,3),low,3);
        auto calculatedIndex=g.binary("add",slice(address,3,indexBits),
            resize(g.binary("lt",constant(lane,3),low,1),indexBits),indexBits);
        // Register the final bank address, byte enable and write data at
        // the hard RAM port. Registering only the source byte address leaves
        // the crossing-word increment in series with the RAM access delay.
        auto index=reg("bank_address_"+std::to_string(lane),indexBits);
        g.states.push_back({index,g.mux(prepare,calculatedIndex,index),{1}});
        auto read=g.add("memory_read",8,index,constant(m,64),constant(0,64));
        auto saved=reg("read_"+std::to_string(lane),8);
        g.states.push_back({saved,g.mux(readEnable,read,saved),{1}});
        g.memoryAccesses.push_back({m,index,readEnable,true});
        data.insert(data.end(),saved.begin(),saved.end());
        Value shift=offset;shift.insert(shift.begin(),3,0);
        auto value=g.binary("shr",slice(savedRequest,41,64),shift,64);
        auto byte=reg("bank_data_"+std::to_string(lane),8),enable=reg("bank_write_"+std::to_string(lane),1);
        g.states.push_back({byte,g.mux(prepare,slice(value,0,8),byte),{1}});
        g.states.push_back({enable,g.mux(prepare,both(write,g.binary("lt",resize(offset,8),size,1)),enable),{1}});
        g.memoryWrites.push_back({m,index,byte,both(accept,enable)});
    }
    auto offset=reg("read_offset",3);
    g.states.push_back({offset,g.mux(readEnable,preparedLow,offset),{1}});
    auto duplicate=data;data.insert(data.end(),duplicate.begin(),duplicate.end());
    Value shift=offset;shift.insert(shift.begin(),3,0);
    data=resize(g.binary("shr",data,shift,128),dataBits);
    auto state=[&](Value q,Value d){g.states.push_back({q,g.mux(reset,Value(q.size(),0),d),{1}});};
    state(pending,g.mux(accept,Value{1},g.mux(replyReady,Value{0},pending)));
    state(written,g.mux(accept,preparedWrite,written));
    g.connect(pins.at("local_out.ready_out"),requests.at("command_ready_out"));
    auto reply=g.mux(written,Value(dataBits,0),data);reply.push_back(0);
    // Capacity comes from registered occupancy, never downstream pop-ready.
    // This cuts the ready cycle through access queues and their continuation,
    // while the synchronous RAM port can still accept one access per clock.
    auto responses=buildMemoryContinuation(g,scope+".replies",2,
        {{"reset",reset},{"command_valid_in",both(running,pending)},
         {"request_ready_in",{1}},{"context_in",reply},{"memory_valid_in",{1}},
         {"memory_data_in",{0}},{"memory_error_in",{0}},
         {"response_ready_in",pins.at("local_out.ready_in")}},false);
    g.connect(replyReady,responses.at("command_ready_out"));
    g.connect(pins.at("local_out.valid_out"),responses.at("response_valid_out"));
    g.connect(pins.at("local_out.error_out"),slice(responses.at("context_out"),dataBits,1));
    g.connect(pins.at("local_out.data_out"),slice(responses.at("context_out"),0,dataBits));
    g.attributes.push_back({scope,"hls_local_memory_bytes",std::to_string(bytes)});
    g.currentScope=previousScope;
}
} // namespace cpphdl::synth

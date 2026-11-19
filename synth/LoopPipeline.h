#pragma once
#include "ReadPipeline.h"
#include "ScheduledGraph.h"
#include "LocalMemory.h"
#include "../hls/Combinational.h"

namespace cpphdl::synth {
// Runtime C++ loops become a network of retimable calculation/memory regions.
// Backedges carry live scalar values; each source region owns its arithmetic.
// One invocation token is active until return, preserving arbitrary external
// aliases. There is no phase register selecting a shared calculation engine.
inline std::map<std::string,graph::Value> buildLoopPipeline(graph::Graph& out,
    ScheduledDesign source,const std::string& scope,unsigned depth) {
    using namespace graph;
    std::set<int> reachable,entries{source.commandEntry};
    std::vector<int> pending{source.commandEntry};
    while(!pending.empty()) {
        int n=pending.back();pending.pop_back();if(n<0 || !reachable.insert(n).second)continue;
        const auto& b=source.blocks.at(n);
        if(b.suspend && b.yes>=0)entries.insert(b.yes);
        pending.push_back(b.yes);pending.push_back(b.no);
    }
    // Cut at shared joins as well, so a source call reachable from two loop
    // exits is one physical instance, not a duplicate in each predecessor.
    for(bool changed=true;changed;) {
        changed=false;std::map<int,int> owner;
        for(int entry:entries) {
            std::set<int> seen;pending={entry};
            while(!pending.empty()) {
                int n=pending.back();pending.pop_back();
                if(n<0 || (n!=entry && entries.count(n)) || !seen.insert(n).second)continue;
                if(owner.count(n) && owner.at(n)!=entry){entries.insert(n);changed=true;break;}
                owner[n]=entry;const auto& b=source.blocks.at(n);
                if(!b.suspend)pending.push_back(b.yes);
                pending.push_back(b.no);
            }
            if(changed)break;
        }
    }
    // A shared join introduces a boundary that did not exist in the source's
    // loop-backedge liveness pass. Values assigned after the loop (or in its
    // alternate branch) must cross that new boundary too. Model both kinds of
    // CFG edge explicitly while recomputing carried values.
    auto flow=source.blocks;
    for(size_t n=0;n<source.blocks.size();++n) {
        auto b=source.blocks[n];b.suspend=false;
        auto split=[&](int target) {
            if(target<0 || !entries.count(target))return target;
            hls::ScheduledBlock edge;edge.yes=target;edge.suspend=true;
            flow.push_back(std::move(edge));return int(flow.size()-1);
        };
        b.yes=split(b.yes);b.no=split(b.no);flow[n]=std::move(b);
    }
    auto carried=hls::liveAcrossClocks(flow,source.symbols);
    source.clocked.insert(carried.begin(),carried.end());
    ScheduledDesign cut=source;
    auto endpoint=[&](int target) {
        hls::ScheduledBlock b;b.statements={"phase = "+std::to_string(target+1)+";"};
        cut.blocks.push_back(std::move(b));return int(cut.blocks.size()-1);
    };
    for(size_t n=0;n<source.blocks.size();++n) {
        auto b=source.blocks[n];
        if(b.yes>=0 && (b.suspend || entries.count(b.yes)))b.yes=endpoint(b.yes);
        if(b.no>=0 && entries.count(b.no))b.no=endpoint(b.no);
        b.suspend=false;cut.blocks[n]=std::move(b);
    }
    auto symbols=source.symbols;
    for(const auto& p:{"operation_in","index_in","value_in"})
        symbols[p]={source.argumentBits,p,false};
    const auto live=hls::liveAtBlockEntries(source.blocks,symbols);
    struct Layout {
        std::map<std::string,unsigned> offsets;
        unsigned bits=0;
    };
    auto layout=[&](const std::set<std::string>& fields) {
        Layout result;
        for(const auto& field:fields) {
            result.offsets[field]=result.bits;result.bits+=symbols.at(field).width;
        }
        // The region's validity still carries a token when its context is empty.
        result.bits=std::max(1u,result.bits);return result;
    };
    std::map<int,Layout> inputs,outputs;
    std::map<int,std::set<int>> successors;
    for(int entry:entries) {
        std::set<int> seen;pending={entry};
        while(!pending.empty()) {
            int n=pending.back();pending.pop_back();if(!seen.insert(n).second)continue;
            for(int target:{source.blocks[n].yes,source.blocks[n].no})if(target>=0) {
                if(entries.count(target))successors[entry].insert(target);
                else pending.push_back(target);
            }
        }
    }
    auto carriedFields=source.clocked;
    for(const auto& p:{"operation_in","index_in","value_in"})carriedFields.insert(p);
    for(int entry:entries) {
        std::set<std::string> fields;
        for(const auto& field:live.at(entry))if(carriedFields.count(field))fields.insert(field);
        inputs[entry]=layout(fields);
    }
    for(int entry:entries) {
        std::set<std::string> fields;
        for(int target:successors[entry])for(const auto& [field,offset]:inputs.at(target).offsets)fields.insert(field);
        outputs[entry]=layout(fields);
    }
    auto both=[&](Value a,Value b){return out.binary("and",a,b,1);};
    auto either=[&](Value a,Value b){return out.binary("or",a,b,1);};
    auto inv=[&](Value a){return out.unary("not",a);};
    out.currentScope=scope;
    std::map<std::string,Value> pins;
    for(auto [name,width]:std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
        {"response_ready_in",1},{"operation_in",source.argumentBits},{"index_in",source.argumentBits},
        {"value_in",source.argumentBits},{"memory_out.ready_out",1},{"memory_out.valid_out",1},
        {"memory_out.data_out",source.externalDataBits},{"memory_out.error_out",1}})
        pins[name]=out.wire(width,scope+"."+name);
    if(source.blockRam)for(auto [name,width]:std::map<std::string,unsigned>{{"ready_out",1},{"valid_out",1},
        {"data_out",source.externalDataBits},{"error_out",1}})
        pins["local_out."+name]=out.wire(width,scope+".local_out."+name);
    // Only one source region can own the invocation token. Allocate carried
    // source scalars once, rather than storing a copy in every possible next
    // region. Values change only when a completed region transfers its token;
    // all calculations and memory continuations see the old values until then.
    std::map<std::string,Value> context;
    for(const auto& [entry,input]:inputs)for(const auto& [field,offset]:input.offsets)
        if(!context.count(field))context[field]=out.wire(symbols.at(field).width,scope+".context."+field,"state");
    struct ContextWrite {Value valid,data;};
    std::map<std::string,std::vector<ContextWrite>> contextWrites;
    const auto running=inv(pins.at("reset"));
    auto busy=out.wire(1,scope+".invocation_active","state");
    auto canStart=both(running,inv(busy));
    struct Region {int entry;Value occupied,frame,ready,terminal;std::map<std::string,Value> pins;};
    std::vector<Region> regions;
    for(int entry:entries) {
        const auto name=scope+".loop_"+std::to_string(entry);
        Region r;r.entry=entry;r.occupied=out.wire(1,name+".occupied","state");
        const auto& input=inputs.at(entry);
        r.frame=Value(input.bits,0);r.ready=both(running,inv(r.occupied));
        for(const auto& [field,offset]:input.offsets) {
            const auto& value=context.at(field);std::copy(value.begin(),value.end(),r.frame.begin()+offset);
        }
        auto design=cut;design.commandEntry=entry;
        Graph logic;auto ports=exportCombinationalRegion(logic,design,name);
        if(logic.states.size()!=source.clocked.size())throw std::runtime_error("unexpected persistent state in loop region");
        Value packet=ports.at("result_out"),destination=ports.at("destination_out");
        packet.insert(packet.end(),destination.begin(),destination.end());
        std::map<std::string,Value> next;
        std::set<std::string> changed;
        for(const auto& p:{"operation_in","index_in","value_in"}) {
            auto value=ports.at(p);next[p]=value;
            if(input.offsets.count(p))logic.ports.push_back({p,value,true});
            else logic.connect(value,Value(value.size(),0));
        }
        auto field=source.clocked.begin();
        for(size_t i=0;i<logic.states.size();++i) {
            const auto& s=logic.states[i];
            next[*field]=s.next;
            if(logic.resolved(s.next)!=logic.resolved(s.bits))changed.insert(*field);
            if(input.offsets.count(*field))logic.ports.push_back({"context_"+std::to_string(i),s.bits,true});
            else logic.connect(s.bits,Value(s.bits.size(),0));
            ++field;
        }
        // An unchanged scalar already resides in its allocated register. It
        // needs neither a writeback mux nor alignment copies in this region.
        std::set<std::string> written;
        for(const auto& [field,offset]:outputs.at(entry).offsets)if(changed.count(field))written.insert(field);
        outputs[entry]=layout(written);const auto& output=outputs.at(entry);
        for(const auto& [field,offset]:output.offsets) {
            const auto& value=next.at(field);packet.insert(packet.end(),value.begin(),value.end());
        }
        if(output.offsets.empty())packet.push_back(0);
        logic.states.clear();
        logic.ports.push_back({"result_out",packet,false});logic.ports.push_back({"fault_out",ports.at("fault_out"),false});
        for(auto& node:logic.nodes) {
            if(node.op=="wire" || node.op=="state")node.op="input";
            else node.scope=name+".hls_stage_0";
        }
        // A region cannot be visited again until its current result retires.
        // Each static access site therefore holds at most one context/reply;
        // shared owner queues retain their configured request window.
        r.pins=buildReadPipeline(out,std::move(logic),name,depth,source.argumentBits,source.externalDataBits,source.blockRam,true);
        out.connect(r.pins.at("reset"),pins.at("reset"));
        out.connect(r.pins.at("command_valid_in"),both(running,r.occupied));
        for(const auto& p:{"operation_in","index_in","value_in"}) {
            out.connect(r.pins.at(p),input.offsets.count(p)?slice(r.frame,input.offsets.at(p),source.argumentBits):Value(source.argumentBits,0));
        }
        size_t i=0;
        for(const auto& field:source.clocked) {
            auto width=source.symbols.at(field).width;
            if(input.offsets.count(field))out.connect(r.pins.at("context_"+std::to_string(i)),slice(r.frame,input.offsets.at(field),width));
            ++i;
        }
        if(r.pins.at("result_out").size()!=source.resultBits+32+output.bits)
            throw std::runtime_error("loop context layout mismatch");
        out.attributes.push_back({name,"hls_loop_context_bits",std::to_string(input.bits)});
        auto target=slice(r.pins.at("result_out"),source.resultBits,32);
        r.terminal=either(out.unary("any",r.pins.at("fault_out")),out.binary("eq",target,constant(0,32),1));
        regions.push_back(std::move(r));
    }
    out.currentScope=scope+".routing";
    for(auto& r:regions) {
        auto inputValid=r.entry==source.commandEntry?both(canStart,pins.at("command_valid_in")):Value{0};
        const auto& input=inputs.at(r.entry);
        for(const auto& from:regions) {
            if(!successors[from.entry].count(r.entry))continue;
            auto target=slice(from.pins.at("result_out"),source.resultBits,32);
            auto selected=both(inv(from.terminal),out.binary("eq",target,constant(r.entry+1,32),1));
            auto valid=both(from.pins.at("response_valid_out"),selected);
            inputValid=either(inputValid,valid);
        }
        auto push=both(inputValid,r.ready),pop=both(r.occupied,r.pins.at("command_ready_out"));
        out.states.push_back({r.occupied,out.mux(pins.at("reset"),Value{0},out.mux(push,Value{1},out.mux(pop,Value{0},r.occupied))),{1}});
        if(r.entry==source.commandEntry) {
            pins["command_ready_out"]=both(canStart,r.ready);
            // A new invocation initializes everything its entry can read,
            // matching the former entry frame even after cancellation/reset.
            auto start=both(pins.at("command_valid_in"),pins.at("command_ready_out"));
            for(const auto& [field,offset]:input.offsets)
                contextWrites[field].push_back({start,pins.count(field)?pins.at(field):Value(symbols.at(field).width,0)});
        }
    }
    Value result(source.resultBits,0),fault(32,0),valid{0};
    struct Returned {Value valid,data;};std::vector<Returned> returns;
    for(auto& r:regions) {
        auto target=slice(r.pins.at("result_out"),source.resultBits,32);
        std::vector<Value> readyTerms;
        for(const auto& to:regions)if(successors[r.entry].count(to.entry))
            readyTerms.push_back(both(to.ready,out.binary("eq",target,constant(to.entry+1,32),1)));
        if(readyTerms.empty())readyTerms.push_back({0});
        while(readyTerms.size()>1) {
            std::vector<Value> level;
            for(size_t i=0;i<readyTerms.size();i+=2)level.push_back(i+1==readyTerms.size()?readyTerms[i]:either(readyTerms[i],readyTerms[i+1]));
            readyTerms=std::move(level);
        }
        const auto ready=readyTerms.front();
        out.connect(r.pins.at("response_ready_in"),both(running,out.mux(r.terminal,pins.at("response_ready_in"),ready)));
        auto transfer=both(both(r.pins.at("response_valid_out"),r.pins.at("response_ready_in")),inv(r.terminal));
        for(const auto& [field,offset]:outputs.at(r.entry).offsets)
            contextWrites[field].push_back({transfer,slice(r.pins.at("result_out"),source.resultBits+32+offset,symbols.at(field).width)});
        auto returned=both(r.pins.at("response_valid_out"),r.terminal);
        auto data=slice(r.pins.at("result_out"),0,source.resultBits);
        data.insert(data.end(),r.pins.at("fault_out").begin(),r.pins.at("fault_out").end());
        returns.push_back({returned,data});
    }
    out.currentScope=scope+".context_updates";
    for(const auto& [field,value]:context) {
        auto writes=contextWrites[field];
        // Source-region transfers are mutually exclusive. Balance selection
        // across the actual writers of this field, never all region entries.
        while(writes.size()>1) {
            std::vector<ContextWrite> level;
            for(size_t i=0;i<writes.size();i+=2) {
                if(i+1==writes.size()){level.push_back(writes[i]);continue;}
                const auto& a=writes[i];const auto& b=writes[i+1];
                level.push_back({either(a.valid,b.valid),out.mux(b.valid,b.data,a.data)});
            }
            writes=std::move(level);
        }
        if(writes.empty())out.connect(value,Value(value.size(),0));
        else out.states.push_back({value,out.mux(writes[0].valid,writes[0].data,value),{1}});
    }
    while(returns.size()>1) {
        std::vector<Returned> level;
        for(size_t i=0;i<returns.size();i+=2) {
            if(i+1==returns.size()){level.push_back(returns[i]);continue;}
            const auto& left=returns[i];const auto& right=returns[i+1];
            level.push_back({either(left.valid,right.valid),out.mux(right.valid,right.data,left.data)});
        }
        returns=std::move(level);
    }
    valid=returns.front().valid;result=slice(returns.front().data,0,source.resultBits);
    fault=slice(returns.front().data,source.resultBits,32);
    // One source-region token owns all accesses until its replies complete.
    // Select its request through a balanced tree rather than a priority chain
    // spanning every source loop. Request valids are mutually exclusive: a
    // successor receives the token only after all predecessor effects finish.
    auto connectMemory=[&](const std::string& prefix) {
        out.currentScope=scope+"."+prefix+"_bus";
        struct Request {Value valid,payload;};
        std::vector<Request> requests;
        std::vector<Value> replies;
        for(unsigned i=0;i<regions.size();++i) {
            const auto& p=regions[i].pins;Value payload;
            for(const auto& field:{"addr_in","size_in","write_in","data_in"}) {
                const auto& value=p.at(prefix+"."+field);payload.insert(payload.end(),value.begin(),value.end());
            }
            requests.push_back({p.at(prefix+".valid_in"),payload});
            replies.push_back(p.at(prefix+".ready_in"));
        }
        while(requests.size()>1) {
            std::vector<Request> level;std::vector<Value> replyLevel;
            for(size_t i=0;i<requests.size();i+=2) {
                if(i+1==requests.size()){level.push_back(requests[i]);replyLevel.push_back(replies[i]);continue;}
                const auto& left=requests[i];const auto& right=requests[i+1];
                level.push_back({either(left.valid,right.valid),out.mux(right.valid,right.payload,left.payload)});
                replyLevel.push_back(either(replies[i],replies[i+1]));
            }
            requests=std::move(level);replies=std::move(replyLevel);
        }
        const auto& request=requests.front();
        pins[prefix+".valid_in"]=request.valid;pins[prefix+".ready_in"]=replies.front();
        unsigned offset=0;
        for(auto [field,width]:std::vector<std::pair<std::string,unsigned>>{{"addr_in",32},{"size_in",8},
            {"write_in",1},{"data_in",source.externalDataBits}}) {
            pins[prefix+"."+field]=slice(request.payload,offset,width);offset+=width;
        }
        for(unsigned i=0;i<regions.size();++i) {
            auto& p=regions[i].pins;
            // The active request already identifies its owner. Decoding an
            // index carried through the payload mux would put every region's
            // arbitration and the entire payload tree on this ready path.
            out.connect(p.at(prefix+".ready_out"),both(pins.at(prefix+".ready_out"),p.at(prefix+".valid_in")));
            // An inactive region has no outstanding reply context. Its source
            // successor cannot issue until the current region has acknowledged
            // every access, so broadcasting responses cannot misroute a reply.
            for(const auto& field:{"valid_out","data_out","error_out"})
                out.connect(p.at(prefix+"."+field),pins.at(prefix+"."+field));
        }
    };
    connectMemory("memory_out");
    if(source.blockRam)connectMemory("local_out");
    out.currentScope=scope;
    pins["result_out"]=result;pins["fault_out"]=fault;pins["response_valid_out"]=both(running,valid);
    auto enter=both(pins.at("command_valid_in"),pins.at("command_ready_out"));
    auto leave=both(pins.at("response_valid_out"),pins.at("response_ready_in"));
    out.states.push_back({busy,out.mux(pins.at("reset"),Value{0},out.mux(leave,Value{0},out.mux(enter,Value{1},busy))),{1}});
    out.attributes.push_back({scope,"hls_loop_regions",std::to_string(regions.size())});
    unsigned totalBits=0;
    for(const auto& [entry,input]:inputs)totalBits+=input.bits;
    out.attributes.push_back({scope,"hls_loop_total_context_bits",std::to_string(totalBits)});
    unsigned allocatedBits=0;for(const auto& [field,value]:context)allocatedBits+=value.size();
    out.attributes.push_back({scope,"hls_loop_allocated_context_bits",std::to_string(allocatedBits)});
    if(source.blockRam) {
        connectLocalMemory(out,scope+".local_memory",source.memoryBytes,source.externalDataBits,pins);
        for(auto p=pins.begin();p!=pins.end();)if(p->first.find("local_out.")==0)p=pins.erase(p);else ++p;
    }
    return pins;
}
} // namespace cpphdl::synth

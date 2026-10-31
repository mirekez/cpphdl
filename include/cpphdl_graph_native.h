#pragma once

#include "cpphdl_graph.h"
#include <numeric>
#include <filesystem>

namespace cpphdl::graph {
// Equivalent word gathers can have very different host costs. Combine sparse
// masks sharing a producer/shift, and compare that form with broadcasting
// repeated source bits. Neither form changes graph operations or state.
template<class Name> inline std::string nativeCoalescedAssembly(
        const Value& value, Name&& name, const std::string& original,bool bmi2=false) {
    if(value.size()>64)throw std::runtime_error("wide native gather");
    if(value.size()<3)return original;
    std::map<Bit,uint64_t> destinations;
    uint64_t constants=0;
    for(unsigned offset=0;offset<value.size();++offset) {
        auto bit=value[offset];
        if(bit<2)constants|=uint64_t(bit)<<offset;
        else destinations[bit]|=uint64_t(1)<<offset;
    }
    auto best=original;
    for(bool broadcast:{false,true}) {
        std::map<std::pair<size_t,int>,uint64_t> shifts;
        std::vector<std::string> terms;
        for(const auto& [bit,mask]:destinations) {
            const auto producer=Graph::owner(bit);
            const unsigned source=Graph::lane(bit);
            if(broadcast && (mask&(mask-1))) {
                auto term=name(producer);
                if(source)term="("+term+" >> "+std::to_string(source)+")";
                terms.push_back("(("+term+" & 1ull) * "+std::to_string(mask)+"ull)");
            } else {
                for(unsigned offset=0;offset<value.size();++offset)
                    if(mask&(uint64_t(1)<<offset))
                        shifts[{producer,int(offset)-int(source)}]|=uint64_t(1)<<source;
            }
        }
        for(const auto& [key,mask]:shifts) {
            auto term=name(key.first);
            if(mask!=UINT64_MAX)term="("+term+" & "+std::to_string(mask)+"ull)";
            if(key.second>0)term="("+term+" << "+std::to_string(key.second)+")";
            if(key.second<0)term="("+term+" >> "+std::to_string(-key.second)+")";
            terms.push_back(std::move(term));
        }
        if(constants||terms.empty())terms.push_back(std::to_string(constants)+"ull");
        std::string candidate="(";
        for(size_t i=0;i<terms.size();++i){if(i)candidate+=" | ";candidate+=terms[i];}
        candidate+=")";
        // Keep the existing expression on ties and never enlarge emitted text.
        // This is a code-size heuristic, not a claim about execution latency.
        if(candidate.size()<best.size())best=std::move(candidate);
    }
    if(bmi2) {
        // A monotone source/destination subsequence is exactly PDEP(PEXT()).
        // Separate repeated or reversed source bits into independent routes.
        struct Route {unsigned last;uint64_t source,destination;};
        std::map<size_t,std::vector<Route>> routes;
        for(unsigned offset=0;offset<value.size();++offset)if(value[offset]>1) {
            const auto bit=value[offset];const unsigned source=Graph::lane(bit);
            auto& group=routes[Graph::owner(bit)];size_t selected=group.size();
            for(size_t i=0;i<group.size();++i)
                if(group[i].last<source&&(selected==group.size()||group[selected].last<group[i].last))selected=i;
            if(selected==group.size())group.push_back({source,0,0});
            auto& route=group[selected];route.last=source;
            route.source|=uint64_t(1)<<source;route.destination|=uint64_t(1)<<offset;
        }
        std::string candidate="(";bool first=true;
        for(const auto& [producer,group]:routes)for(const auto& route:group) {
            if(!first)candidate+=" | ";first=false;
            candidate+="cpphdl::graph_runtime::gatherBits("+name(producer)+","+
                std::to_string(route.source)+"ull,"+std::to_string(route.destination)+"ull)";
        }
        if(constants||first){if(!first)candidate+=" | ";candidate+=std::to_string(constants)+"ull";}
        candidate+=")";
        if(candidate.size()<best.size())best=std::move(candidate);
    }
    return best;
}
inline std::string nativeStringLiteral(const std::string& value) {
    std::string result = "\"";
    for (unsigned char byte : value) {
        if (byte == '"' || byte == '\\') { result += '\\'; result += byte; }
        else if (byte >= 32 && byte < 127) result += byte;
        else {
            result += '\\';
            result += char('0' + ((byte >> 6) & 7));
            result += char('0' + ((byte >> 3) & 7));
            result += char('0' + (byte & 7));
        }
    }
    return result + '"';
}
// Native tasks own complete combinational cones. Pure shared logic can be
// duplicated to avoid cross-lane intermediate traffic. Each lane also owns
// its sinks and prepares register updates in an inactive state bank.
struct NativeParallelPlan {
    unsigned lanes = 1, stages = 1;
    uint64_t cost = 0;
    std::vector<std::vector<size_t>> tasks;
    std::vector<std::vector<size_t>> waits;
    std::vector<unsigned> ports, states, accesses, writes;
    std::vector<std::vector<unsigned>> portWords;
};

inline NativeParallelPlan nativeParallelPlan(Graph& graph, const std::vector<size_t>& order, unsigned lanes,
                                             bool hostOverlap = false, bool balanceLanes = false, bool scopeTasks = false) {
    const size_t count = graph.nodes.size();
    std::vector<size_t> values;
    std::vector<std::vector<size_t>> dependencies(count);
    std::vector<uint64_t> weight(count);
    uint64_t total = 0;
    for (auto index : order) {
        const auto& node = graph.nodes[index];
        if (node.op == "input" || node.op == "state" || node.validation()) continue;
        values.push_back(index); weight[index] = 1;
        std::set<size_t> deps;
        for (const auto* bits : {&node.left, &node.right, &node.select}) {
            Bit previous = 0;
            for (auto raw : *bits) {
                auto bit = graph.resolve(raw);
                if (bit < 2) { previous = 0; continue; }
                if (bit != previous + 1) ++weight[index];
                previous = bit;
                auto producer = Graph::owner(bit);
                if (graph.nodes[producer].op != "input" && graph.nodes[producer].op != "state") deps.insert(producer);
            }
        }
        dependencies[index].assign(deps.begin(), deps.end()); total += weight[index];
    }
    if (values.empty()) return {};
    // Each sink owns its entire pure combinational fan-in, ending at current
    // state, input ports and memory contents. Duplicated operations have private
    // node IDs/storage, so lanes never communicate intermediate values.
    struct Root { std::vector<Value*> fields; uint64_t cost = 0; unsigned lane = 0; uint64_t sinkCost = 0; };
    std::vector<Root> roots;
    std::vector<size_t> stateRoots, accessRoots, writeRoots;
    std::vector<std::vector<size_t>> portRoots(graph.ports.size());
    std::vector<std::vector<Value>> portSlices(graph.ports.size());
    for (size_t i=0;i<graph.ports.size();++i) {
        const auto& port=graph.ports[i];
        if(port.input)continue;
        // A packed vector port is not one indivisible computation. Separate
        // host words expose independent SIMD/MAC results without splitting
        // 64-bit scalar operations or changing the public port interface.
        auto& slices=portSlices[i];slices.resize((port.bits.size()+63)/64);
        for(size_t word=0;word<slices.size();++word) {
            slices[word]=slice(port.bits,word*64,std::min<size_t>(64,port.bits.size()-word*64));
            portRoots[i].push_back(roots.size());roots.push_back({{&slices[word]}});
        }
    }
    // Explicit-event graphs can have ordered updates to the same state word.
    // Keep all such sinks on one lane so their original priority is preserved.
    std::vector<size_t> group(graph.states.size()); std::iota(group.begin(), group.end(), 0);
    auto leader = [&](size_t i) { while (group[i] != i) { group[i] = group[group[i]]; i = group[i]; } return i; };
    std::map<size_t, size_t> writers;
    for (size_t i = 0; i < graph.states.size(); ++i) for (auto bit : graph.states[i].bits) {
        auto [it, fresh] = writers.emplace(Graph::owner(bit), i);
        if (!fresh) group[leader(i)] = leader(it->second);
    }
    std::map<size_t, size_t> groupRoot;
    for (size_t i = 0; i < graph.states.size(); ++i) {
        auto [it, fresh] = groupRoot.emplace(leader(i), roots.size());
        if (fresh) roots.push_back({});
        stateRoots.push_back(it->second);
        auto& state = graph.states[i]; auto& fields = roots[it->second].fields;
        fields.insert(fields.end(), {&state.next, &state.trigger, &state.reset, &state.resetValue});
    }
    for (auto& access : graph.memoryAccesses) {
        accessRoots.push_back(roots.size()); roots.push_back({{&access.address, &access.enabled}});
    }
    for (auto& write : graph.memoryWrites) {
        writeRoots.push_back(roots.size()); roots.push_back({{&write.address, &write.data, &write.enabled}});
    }
    std::vector<Value> hostValues;
    hostValues.reserve(std::count_if(order.begin(), order.end(),
        [&](size_t node) { return graph.nodes[node].hostEffect(); }));
    std::vector<size_t> hostRoots;
    for (auto index : order) if (graph.nodes[index].hostEffect()) {
        hostValues.push_back({(index + 1) * 64 + 2});
        hostRoots.push_back(roots.size());
        roots.push_back({{&hostValues.back()}});
    }
    std::vector<size_t> validationRoots;
    for (auto& node : graph.nodes) if (node.validation()) {
        validationRoots.push_back(roots.size()); roots.push_back({{&node.left}});
    }
    // Keep scoped state and RAM transactions together automatically. These
    // are scheduling hints, not simulation boundaries: each task still owns
    // its complete pure fan-in and reads the same committed snapshot.
    if (scopeTasks) {
        std::vector<std::string> scopes(roots.size());
        std::vector<bool> mixed(roots.size());
        auto assign = [&](size_t root, const std::string& scope) {
            if (scope.empty()) { mixed[root] = true; return; }
            if (scopes[root].empty()) scopes[root] = scope;
            else if (scopes[root] != scope) mixed[root] = true;
        };
        for (size_t i=0;i<graph.states.size();++i)
            for (auto bit:graph.states[i].bits) assign(stateRoots[i],graph.nodes[Graph::owner(bit)].scope);
        auto memoryScope = [&](size_t memory) {
            const auto& name=graph.memories[memory].name;
            auto slash=name.find_last_of("/.");
            return slash==std::string::npos?std::string():name.substr(0,slash);
        };
        for (size_t i=0;i<accessRoots.size();++i) assign(accessRoots[i],memoryScope(graph.memoryAccesses[i].memory));
        for (size_t i=0;i<writeRoots.size();++i) assign(writeRoots[i],memoryScope(graph.memoryWrites[i].memory));
        // Output/effect/validation roots stay separate. Caller pinning below
        // retains the original foreign-effect and error semantics.
        std::map<std::string,std::pair<size_t,unsigned>> groups;
        std::vector<Root> grouped;
        std::vector<size_t> remap(roots.size());
        for (size_t i=0;i<roots.size();++i) {
            size_t dest=grouped.size();
            if (!mixed[i]&&!scopes[i].empty()) {
                auto [it,fresh]=groups.emplace(scopes[i],std::make_pair(dest,0u));
                // A flattened frontend may put a large controller in one
                // scope. Bound grouping rather than making that scope an
                // indivisible serial task. An ordered multiwriter root is
                // never split by this limit.
                if(it->second.second==256)it->second={dest,0};
                dest=it->second.first;++it->second.second;
            }
            if (dest==grouped.size()) grouped.push_back({});
            auto& fields=grouped[dest].fields;
            fields.insert(fields.end(),roots[i].fields.begin(),roots[i].fields.end());
            remap[i]=dest;
        }
        for (auto* indices:{&stateRoots,&accessRoots,&writeRoots,&hostRoots,&validationRoots})
            for (auto& index:*indices) index=remap[index];
        for (auto& indices:portRoots) for (auto& index:indices) index=remap[index];
        roots=std::move(grouped);
    }
    // Do not retain one transitive dependency vector per sink. Wide RAM
    // interfaces and shared control cones can make that storage quadratic.
    // Visit once for costs, then reconstruct one cone at a time for placement.
    std::vector<size_t> seen(count, size_t(-1)), pending, cone;
    size_t epoch = 0;
    auto visitCone = [&](const Root& root, auto&& visit) {
        if (++epoch == size_t(-1)) { std::fill(seen.begin(), seen.end(), size_t(-1)); epoch = 0; }
        pending.clear();
        for (auto* field : root.fields) for (auto raw : *field) {
            auto bit = graph.resolve(raw);
            if (bit > 1 && weight[Graph::owner(bit)]) pending.push_back(Graph::owner(bit));
        }
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            if (seen[node] == epoch) continue;
            seen[node] = epoch; visit(node);
            pending.insert(pending.end(), dependencies[node].begin(), dependencies[node].end());
        }
    };
    size_t active = 0;
    for (auto& root : roots) {
        // Account for assembling the sink as well as its combinational cone.
        // Constant/direct-state sinks still do work and must not all land on 0.
        root.sinkCost = 1;
        for (auto* field : root.fields) root.sinkCost += (field->size() + 63) / 64;
        root.cost = root.sinkCost;
        bool nonempty = false;
        visitCone(root, [&](size_t node) { nonempty = true; root.cost += weight[node]; });
        active += nonempty;
    }
    lanes = std::min<size_t>(lanes, active);
    if (lanes < 2) { NativeParallelPlan p; p.tasks = {values}; p.waits.resize(1); return p; }
    std::vector<size_t> sorted(roots.size()); std::iota(sorted.begin(), sorted.end(), 0);
    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) { return roots[a].cost > roots[b].cost; });
    std::vector<std::vector<bool>> owned(lanes, std::vector<bool>(count));
    std::vector<uint64_t> load(lanes);
    std::set<size_t> callerRoots;
    callerRoots.insert(validationRoots.begin(), validationRoots.end());
    callerRoots.insert(hostRoots.begin(), hostRoots.end());
    // Foreign calls execute exactly once on the caller, including unused
    // results. Pin their consumers too: duplicating a host-dependent cone
    // would repeat the call or race its output buffers. Independent pure
    // cones can still run in workers against the same committed snapshot.
    if (!hostRoots.empty())
        for (size_t root = 0; root < roots.size(); ++root) {
            bool hostDependent = false;
            visitCone(roots[root], [&](size_t node) { hostDependent |= graph.nodes[node].hostEffect(); });
            if (hostDependent) callerRoots.insert(root);
        }
    if (hostOverlap) {
        for (const auto& port : portRoots) callerRoots.insert(port.begin(),port.end());
        callerRoots.insert(accessRoots.begin(), accessRoots.end());
        callerRoots.insert(writeRoots.begin(), writeRoots.end());
        // The caller owns every observable output and every validation that
        // can fail. Its host continuation is safe before the workers finish:
        // they only prepare inactive register storage, never observable state.
    }
    for (auto r : callerRoots) {
        load[0] += roots[r].sinkCost;
        visitCone(roots[r], [&](size_t node) {
            if (!owned[0][node]) { owned[0][node] = true; load[0] += weight[node]; }
        });
    }
    // Group large overlapping cones first. Balance the accumulated lane cost
    // and penalize extra work, rather than balancing unrelated depth slices.
    for (auto r : sorted) {
        if (callerRoots.count(r)) continue;
        auto& root = roots[r]; uint64_t bestScore = UINT64_MAX, bestAdded = 0;
        cone.clear(); visitCone(root, [&](size_t node) { cone.push_back(node); });
        const auto peak = *std::max_element(load.begin(), load.end());
        for (unsigned lane = hostOverlap ? 1 : 0; lane < lanes; ++lane) {
            uint64_t added = root.sinkCost; for (auto node : cone) if (!owned[lane][node]) added += weight[node];
            // Compact scheduling avoids duplication once another cone fixes
            // the critical path. Distributed scheduling balances each lane
            // independently, allowing large hosts to use the requested CPUs.
            const auto cost = balanceLanes ? load[lane] + added : std::max(peak, load[lane] + added);
            const auto score = cost * lanes * 4 + added;
            if (score < bestScore) { bestScore = score; bestAdded = added; root.lane = lane; }
        }
        load[root.lane] += bestAdded;
        for (auto node : cone) owned[root.lane][node] = true;
    }
    // Overlapping cones can leave lanes unused. Keep the caller at lane zero,
    // but do not create workers or barrier participants for empty lanes.
    std::vector<unsigned> laneMap(lanes);
    unsigned usedLanes = 0;
    for (unsigned lane = 0; lane < lanes; ++lane) {
        if (lane == 0 || load[lane] != 0) {
            laneMap[lane] = usedLanes;
            if (usedLanes != lane) {
                owned[usedLanes] = std::move(owned[lane]);
                load[usedLanes] = load[lane];
            }
            ++usedLanes;
        }
    }
    for (auto& root : roots) root.lane = laneMap[root.lane];
    lanes = usedLanes;
    owned.resize(lanes);
    load.resize(lanes);
    NativeParallelPlan plan; plan.lanes = lanes; plan.stages = 1; plan.tasks.resize(lanes); plan.waits.resize(lanes);
    plan.portWords.resize(graph.ports.size());
    for (size_t i=0;i<portRoots.size();++i) {
        plan.ports.push_back(portRoots[i].empty()?0:roots[portRoots[i][0]].lane);
        for(size_t word=0;word<(graph.ports[i].input?0:(graph.ports[i].bits.size()+31)/32);++word)
            plan.portWords[i].push_back(roots[portRoots[i][word/2]].lane);
    }
    for (auto root : stateRoots) plan.states.push_back(roots[root].lane);
    for (auto root : accessRoots) plan.accesses.push_back(roots[root].lane);
    for (auto root : writeRoots) plan.writes.push_back(roots[root].lane);
    // Preserve templates while each original operation is reused by its first
    // owner. Only operations needed by additional lanes are duplicated.
    const auto originals = graph.nodes;
    std::vector<bool> reused(count);
    for (unsigned lane = 0; lane < lanes; ++lane) {
        std::vector<size_t> copy(count, size_t(-1));
        auto remap = [&](Value& bits) {
            for (auto& bit : bits) {
                bit = graph.resolve(bit);
                if (bit < 2 || !weight[Graph::owner(bit)]) continue;
                auto index = copy[Graph::owner(bit)];
                if (index == size_t(-1)) throw std::runtime_error("missing native cone producer");
                bit = (index + 1) * 64 + 2 + Graph::lane(bit);
            }
        };
        for (auto node : values) if (owned[lane][node]) {
            auto cloned = originals[node]; remap(cloned.left); remap(cloned.right); remap(cloned.select);
            if (!reused[node]) {
                reused[node] = true; copy[node] = node; graph.nodes[node] = std::move(cloned);
            } else {
                copy[node] = graph.nodes.size(); graph.nodes.push_back(std::move(cloned));
            }
            plan.tasks[lane].push_back(copy[node]);
        }
        for (auto& root : roots) if (root.lane == lane) for (auto* field : root.fields) remap(*field);
    }
    for(size_t i=0;i<portSlices.size();++i)if(!graph.ports[i].input) {
        graph.ports[i].bits.clear();
        for(const auto& bits:portSlices[i])graph.ports[i].bits.insert(graph.ports[i].bits.end(),bits.begin(),bits.end());
    }
    plan.cost = *std::max_element(load.begin(), load.end());
    fprintf(stderr, "native graph cones: base weight %llu; lane weights", (unsigned long long)total);
    for(auto x:load) fprintf(stderr," %llu",(unsigned long long)x); fprintf(stderr,"\n");
    return plan;
}

inline void Graph::emit(const std::string& path, unsigned chunkSize, unsigned threads, bool hostOverlap,
                        bool balanceLanes, bool memoizeChunks, bool memoizeNextWords, bool memoizeDeltas,
                        unsigned splitChunks) {
        if (!replacements.empty()) throw std::runtime_error("native graph requires linked pipeline replacement RTL to be converted with hdlcpp");
        if (splitChunks && !chunkSize) throw std::runtime_error("split native compilation requires chunking");
        if (!threads || threads > 256) throw std::runtime_error("native graph threads must be between 1 and 256");
        if (threads > 1 && !chunkSize) chunkSize = 512;
        if (memoizeChunks && !chunkSize) throw std::runtime_error("native chunk memoization requires chunking");
        if (memoizeNextWords && !memoizeChunks) throw std::runtime_error("native next-word memoization requires chunk memoization");
        if (memoizeDeltas && !memoizeChunks) throw std::runtime_error("native delta memoization requires chunk memoization");
        auto option=[&](const char* name){return std::any_of(attributes.begin(),attributes.end(),[&](const ScopeAttribute& a){
            return a.scope.empty()&&a.name==name&&a.value=="1";
        });};
        const unsigned requestedWorkers=threads;
        unsigned requestedTasks=threads;
        for(const auto& a:attributes) if(a.scope.empty()&&a.name=="native.tasks") {
            size_t end=0;auto count=std::stoul(a.value,&end);
            if(end!=a.value.size()||count<threads||count>256)
                throw std::runtime_error("native.tasks must be between worker count and 256");
            requestedTasks=unsigned(count);
        }
        if(requestedTasks!=threads&&hostOverlap)
            throw std::runtime_error("native.tasks does not support host-overlap continuations");
        const bool scopeTasks=option("native.scope_tasks");
        if(requestedTasks>1&&!chunkSize)chunkSize=512;
        const bool parallelRoots=option("native.parallel_roots")&&threads>1;
        const bool coalesceBits=option("native.coalesce_bits");
        const bool bmi2Gathers=option("native.bmi2_gathers");
        const bool guardChunkCalls=option("native.guard_chunk_calls");
        if(guardChunkCalls&&!memoizeDeltas)
            throw std::runtime_error("native chunk call guards require delta memoization");
        bool cacheStateRoots=option("native.cache_state_roots")&&threads>1;
        if(cacheStateRoots&&!memoizeDeltas)
            throw std::runtime_error("state root caching requires delta memoization");
        if(parallelRoots&&!memoizeDeltas)throw std::runtime_error("parallel roots require delta memoization");
        // Internally owned registers publish changes at commit. Public input
        // arrays retain a small compatibility scan; native.scan_roots is the
        // reference/debug path for callers that directly modify register fields.
        const bool writeTracking=memoizeDeltas&&!option("native.scan_roots");
        validateClocks();
        const bool hostEffects = std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.hostEffect(); });
        const bool ownedMemories=option("native.owned_memories");
        const bool groupedTransactions=!option("native.ungrouped_transactions");
        const bool changedMemoryWrites=ownedMemories&&!option("native.unconditional_memory_writes");
        if(ownedMemories&&(!memoizeChunks||hostEffects))
            throw std::runtime_error("owned memory caching requires a pure memoized graph");
        if(cacheStateRoots&&(hostEffects||clockContract!=ClockContract::NamedEdges))
            throw std::runtime_error("state root caching requires a pure named-edge graph with internally owned state");
        if(writeTracking)cacheStateRoots=false;
        const bool quietEvents=std::any_of(attributes.begin(),attributes.end(),[](const ScopeAttribute& a){
            return a.scope.empty()&&a.name=="native.quiet_events"&&a.value=="1";
        });
        if(quietEvents) {
            if(hostEffects||clockContract!=ClockContract::ExplicitEvents||!clocks.empty())
                throw std::runtime_error("quiet-event observation requires an explicit-event graph without host effects");
            Value active{0};unsigned histories=0;
            for(const auto& state:states) {
                const auto& name=nodes[owner(state.bits.at(0))].name;
                if(name=="event history"||name=="async reset history") {
                    if(state.bits.size()!=1||state.next.size()!=1||number(resolved(state.trigger))!=1)
                        throw std::runtime_error("invalid quiet-event history");
                    ports.push_back({"__cpphdl_event_history_"+std::to_string(histories++),Value{state.next[0],state.bits[0]},false});
                }else active=binary("or",active,state.trigger,1);
            }
            for(const auto& write:memoryWrites)active=binary("or",active,write.enabled,1);
            for(const auto& access:memoryAccesses)if(access.transaction)active=binary("or",active,access.enabled,1);
            std::vector<Value> checks;
            for(const auto& node:nodes)if(node.validation())checks.push_back(node.left);
            for(const auto& check:checks)active=binary("or",active,check,1);
            ports.push_back({"__cpphdl_event_active",active,false});
        }
        if (memoizeChunks && hostEffects)
            throw std::runtime_error("native chunk memoization does not support host effects");
        if (clockContract == ClockContract::NamedEdges) {
            for (const auto& node : nodes) if (node.hostEffect())
                throw std::runtime_error("multiclock host effects are not implemented");
        }
        for (const auto& port : ports) {
            if (port.name.empty() || (!std::isalpha(static_cast<unsigned char>(port.name[0])) && port.name[0] != '_'))
                throw std::runtime_error("top port is not a C++ identifier");
            for (unsigned char character : port.name)
                if (!std::isalnum(character) && character != '_')
                    throw std::runtime_error("top port is not a C++ identifier");
        }
        optimize();
        // One simultaneous NBA commit followed by a combinational evaluation
        // suffices only when events cannot be created by that same commit.
        // Reset probes have a bounded delta loop; ordinary state-derived clocks
        // remain unsupported rather than silently losing clock events.
        std::set<size_t> eventCone;
        std::function<void(Bit)> checkEvent = [&](Bit raw) {
            auto bit = resolve(raw);
            if (bit < 2) return;
            auto index = owner(bit);
            if (!eventCone.insert(index).second) return;
            const auto& node = nodes[index];
            if (node.op == "state") throw std::runtime_error("state-derived clock/reset requires delta scheduling");
            for (auto* value : {&node.left,&node.right,&node.select}) for (auto input : *value) checkEvent(input);
        };
        for (const auto& state : states)
            if (nodes[owner(state.bits[0])].name == "event history")
                for (auto bit : state.next) checkEvent(bit);
        auto order = dependencyOrder();
        // Validate clocks, effects and combinational cycles before pruning so
        // unused state cannot hide an invalid model. Partition files retain
        // their original state; only the final executable loses dead registers.
        if (pruneUnusedState()) {
            compact();
            order = dependencyOrder();
        }
        for(const auto& state:states)if(nodes[owner(state.bits.at(0))].name.rfind("SV blocking storage:", 0) == 0)
            throw std::runtime_error("observable clocked blocking storage is not supported: "+nodes[owner(state.bits.at(0))].name);
        size_t cachedNextWords=0;
        if (memoizeNextWords) {
            // A register word can gather many scattered producer bits. Put
            // that assembly in the cached combinational schedule, rather
            // than reconstructing it in every commit sink. The identity
            // AND is introduced after optimization so it becomes a retained
            // chunk result; a later emit/optimization can remove it normally.
            for (auto& state:states) {
                Value next;
                for (size_t offset=0;offset<state.next.size();offset+=64) {
                    auto width=std::min<size_t>(64,state.next.size()-offset);
                    // add() canonicalizes identities immediately; retain
                    // this one explicitly until native chunks are emitted.
                    auto base=(nodes.size()+1)*64+2;
                    nodes.push_back({"and",unsigned(width),slice(state.next,offset,width),
                                     constant(mask(width),width),{},"native_next_word",currentScope});
                    for(size_t bit=0;bit<width;++bit)next.push_back(base+bit);
                    ++cachedNextWords;
                }
                state.next=std::move(next);
            }
            order=dependencyOrder();
        }
        auto scheduledNodes = order.size();
        NativeParallelPlan parallel;
        std::vector<std::vector<size_t>> taskNodes(1);
        if (requestedTasks > 1) {
            parallel = nativeParallelPlan(*this, order, requestedTasks, hostOverlap, balanceLanes, scopeTasks);
            threads = parallel.lanes;
            if (threads > 1) {
                taskNodes = parallel.tasks;
                scheduledNodes = std::count_if(order.begin(), order.end(), [&](size_t i) {
                    return nodes[i].op == "input" || nodes[i].op == "state";
                });
                for (const auto& task : taskNodes) scheduledNodes += task.size();
            }
        }
        if (threads == 1) for (auto index : order)
            if (nodes[index].op != "state" && nodes[index].op != "input") taskNodes[0].push_back(index);
        const unsigned workerCount=std::min(requestedWorkers,threads);
        const bool dynamicTasks=threads>workerCount;
        // A compacted one-lane plan has scalar state, not published banks.
        cacheStateRoots=cacheStateRoots&&threads>1;
        // Capture live state before the parallel schedule narrows order to
        // combinational tasks. Read-only initialized state has no writer task.
        std::vector<bool> storedState(nodes.size());
        for(auto index:order)if(nodes[index].op=="state")storedState[index]=true;
        for(const auto& state:states)for(auto bit:state.bits)storedState[owner(bit)]=true;
        if (threads > 1) {
            order.clear();
            for (const auto& task : taskNodes) order.insert(order.end(), task.begin(), task.end());
            fprintf(stderr, "native graph parallel: %u lanes, %u stages, %zu tasks\n",
                    threads, parallel.stages, parallel.tasks.size());
        }
        // Filter the validated dependency order, rather than introducing a
        // second scheduler. Run after lane cloning so roots refer to each
        // lane's private producers. Settling observes committed storage and
        // outputs; next-state, transactional RAM checks and effects are work roots.
        std::vector<bool> settleLive(nodes.size());
        auto requireSettling = [&](const Value& value) {
            for (auto bit : resolved(value)) if (bit > 1) settleLive[owner(bit)] = true;
        };
        for (const auto& port : ports) if (!port.input) requireSettling(port.bits);
        for (const auto& access : memoryAccesses) if (!access.transaction) {
            requireSettling(access.address);
            requireSettling(access.enabled);
        }
        for (auto position = order.rbegin(); position != order.rend(); ++position) {
            const auto& node = nodes[*position];
            if (!settleLive[*position] || node.op == "state" || node.op == "input") continue;
            for (const auto* value : {&node.left, &node.right, &node.select}) requireSettling(*value);
        }
        std::vector<int64_t> chunkOf(nodes.size(), -1);
        std::vector<std::vector<size_t>> taskChunks(taskNodes.size());
        std::vector<bool> settleChunks;
        if (chunkSize) {
            size_t chunk = 0;
            for (size_t task = 0; task < taskNodes.size(); ++task) {
                for (size_t i = 0; i < taskNodes[task].size(); ++i) {
                    if (i % chunkSize == 0) {
                        taskChunks[task].push_back(chunk++);
                        settleChunks.push_back(false);
                    }
                    chunkOf[taskNodes[task][i]] = chunk - 1;
                    if (settleLive[taskNodes[task][i]]) settleChunks.back() = true;
                }
            }
        }
        // Phase-specific chunks must not let output-only evaluation mark
        // skipped next-state work as cached. Fully live chunks can share keys.
        std::set<int64_t> workOnlyChunks;
        for (auto index : order) if (chunkOf[index] >= 0 && !settleLive[index])
            workOnlyChunks.insert(chunkOf[index]);
        // RAM contents can change even when an address is unchanged. Always
        // reevaluate chunks containing mutable loads, then propagate their
        // exact output changes through the ordinary cache dependencies.
        std::set<int64_t> mutableReadChunks;
        std::map<size_t,std::set<int64_t>> memoryReadChunks;
        if(memoizeChunks)for(const auto& task:taskNodes)for(auto index:task)
            if(nodes[index].op=="memory_read") {
                auto identity=number(nodes[index].right);
                if(identity && memories.at(*identity).contents.empty()) {
                    mutableReadChunks.insert(chunkOf[index]);
                    memoryReadChunks[*identity].insert(chunkOf[index]);
                }
            }
        // Earlier writes with identical address, enable, edge and width cannot
        // affect committed RAM: all operands are sampled before ordered commit.
        // The retained later write also validates that same enabled address.
        std::vector<bool> shadowedWrites(memoryWrites.size());
        if(changedMemoryWrites) {
            std::set<std::tuple<size_t,Value,Value,int,bool,size_t>> retained;
            for(size_t i=memoryWrites.size();i-- > 0;) {
                const auto& write=memoryWrites[i];
                shadowedWrites[i]=!retained.emplace(write.memory,resolved(write.address),resolved(write.enabled),
                                                   write.clock,write.falling,write.data.size()).second;
            }
        }
        struct WriteGroup { unsigned lane; Value enable; int clock; bool falling; std::vector<size_t> writes; };
        std::vector<WriteGroup> writeGroups;
        std::vector<size_t> writeGroup(memoryWrites.size(),size_t(-1));
        std::vector<size_t> lastGroup(threads,size_t(-1));
        if(threads>1)for(size_t i=0;i<memoryWrites.size();++i)if(!shadowedWrites[i]) {
            const auto& write=memoryWrites[i];auto lane=parallel.writes[i];
            auto enable=resolved(write.enabled);auto group=lastGroup[lane];
            if(!groupedTransactions||group==size_t(-1)||writeGroups[group].enable!=enable||
               writeGroups[group].clock!=write.clock||writeGroups[group].falling!=write.falling||
               writeGroups[group].writes.size()>=chunkSize) {
                group=writeGroups.size();lastGroup[lane]=group;
                writeGroups.push_back({lane,enable,write.clock,write.falling,{}});
            }
            writeGroup[i]=group;writeGroups[group].writes.push_back(i);
        }
        const bool requestCachedTransactions=memoizeDeltas&&threads>1&&!option("native.uncached_transactions");
        const size_t transactionChunkBase=settleChunks.size();
        size_t dirtyChunkCount=transactionChunkBase;
        std::vector<bool> cachedWriteGroups(writeGroups.size());
        std::vector<size_t> transactionChunks(writeGroups.size(),size_t(-1));
        // Simple word loads/stores are cheaper than a second dependency cache.
        // Cache fragmented bit assembly; the override is useful for regression
        // coverage and controlled comparisons of simple payloads.
        auto fragments=[&](const Value& raw) {
            auto value=resolved(raw);size_t count=0;
            for(size_t i=0;i<value.size();) {
                auto bit=value[i++];if(bit<2)continue;++count;
                if(i<value.size()&&value[i]==bit)while(i<value.size()&&value[i]==bit)++i;
                else while(i<value.size()&&value[i]>1&&owner(value[i])==owner(bit)&&value[i]==bit+1)bit=value[i++];
            }
            return count;
        };
        for(size_t group=0;group<writeGroups.size();++group) {
            size_t cost=0,words=0;
            for(auto i:writeGroups[group].writes) {
                cost+=fragments(memoryWrites[i].address)+fragments(memoryWrites[i].data);
                words+=1+(memoryWrites[i].data.size()+63)/64;
            }
            if(requestCachedTransactions&&(option("native.cache_simple_transactions")||cost>2*words)) {
                cachedWriteGroups[group]=true;transactionChunks[group]=dirtyChunkCount++;
            }
        }
        const bool cachedTransactions=dirtyChunkCount!=transactionChunkBase;
        const size_t cachedWriteGroupCount=dirtyChunkCount-transactionChunkBase;
        // A complete prepared write set is idempotent for internally owned
        // RAM. Cache its last active enable mask, not individual port commits:
        // changing priority/enables must replay all active writes in order.
        std::vector<std::vector<size_t>> memoryWritePorts(memories.size());
        std::vector<std::set<size_t>> memoryWriteGroups(memories.size());
        for(size_t i=0;i<memoryWrites.size();++i)if(!shadowedWrites[i]) {
            auto memory=memoryWrites[i].memory;
            memoryWritePorts[memory].push_back(i);
            if(threads>1)memoryWriteGroups[memory].insert(writeGroup[i]);
        }
        std::vector<bool> cachedMemoryCommits(memories.size()),commitPayloadGroups(writeGroups.size());
        // Opt-in: repeated-write microbenchmarks improve, but the complete
        // Llama graph regresses from the extra bookkeeping. Keep the faster
        // existing commit path as the default.
        if(ownedMemories&&cachedTransactions&&option("native.cache_memory_commits")&&!option("native.uncached_memory_commits"))
            for(size_t memory=0;memory<memories.size();++memory) {
                const auto& groups=memoryWriteGroups[memory];
                if(groups.empty()||(!option("native.cache_small_memory_commits")&&memoryWritePorts[memory].size()<=2*groups.size())||
                   !std::all_of(groups.begin(),groups.end(),[&](auto group){return cachedWriteGroups[group];}))continue;
                cachedMemoryCommits[memory]=true;
                for(auto group:groups)commitPayloadGroups[group]=true;
            }
        const size_t cachedMemoryCommitCount=std::count(cachedMemoryCommits.begin(),cachedMemoryCommits.end(),true);
        // Clear each payload notification after every dependent memory has
        // captured it. Keep these stores inside bounded commit helpers instead
        // of emitting thousands of stores into one compiler-hostile function.
        std::map<size_t,std::vector<size_t>> commitPayloadClearAt;
        std::vector<size_t> lastPayloadRead(writeGroups.size());
        for(size_t memory=0;memory<memories.size();++memory)if(cachedMemoryCommits[memory])
            for(auto group:memoryWriteGroups[memory])
                lastPayloadRead[group]=std::max(lastPayloadRead[group],memoryWritePorts[memory].front());
        for(size_t group=0;group<writeGroups.size();++group)if(commitPayloadGroups[group])
            commitPayloadClearAt[lastPayloadRead[group]].push_back(group);
        // Commit ownership follows backing storage, independently of the
        // combinational/preparation lanes. Each generated RAM is a distinct
        // vector. All writes to it stay ordered on one lane. Optional payload
        // notification clearing couples memories sharing a cached write group;
        // keep those connected components together as well.
        std::vector<size_t> commitParent(memories.size());
        for(size_t i=0;i<commitParent.size();++i)commitParent[i]=i;
        auto commitRoot=[&](size_t i) {
            while(commitParent[i]!=i) {commitParent[i]=commitParent[commitParent[i]];i=commitParent[i];}
            return i;
        };
        std::map<size_t,size_t> payloadMemory;
        for(size_t memory=0;memory<memories.size();++memory)if(cachedMemoryCommits[memory])
            for(auto group:memoryWriteGroups[memory]) {
                auto [at,inserted]=payloadMemory.emplace(group,memory);
                if(!inserted)commitParent[commitRoot(memory)]=commitRoot(at->second);
            }
        std::map<size_t,size_t> commitWeights;
        size_t commitWeight=0;
        for(size_t i=0;i<memoryWrites.size();++i)if(!shadowedWrites[i]) {
            auto cost=1+(memoryWrites[i].data.size()+63)/64;
            commitWeights[commitRoot(memoryWrites[i].memory)]+=cost;commitWeight+=cost;
        }
        const bool parallelMemoryCommits=threads>1&&commitWeights.size()>1&&
            !option("native.serial_memory_commits")&&
            (commitWeight>=16384||option("native.parallel_memory_commits"));
        const unsigned commitLanes=parallelMemoryCommits?std::min<size_t>(workerCount,commitWeights.size()):1;
        std::vector<unsigned> memoryCommitLane(memories.size());
        std::vector<std::vector<size_t>> commitWrites(commitLanes);
        if(parallelMemoryCommits) {
            std::vector<std::pair<size_t,size_t>> components;
            for(auto [root,weight]:commitWeights)components.emplace_back(weight,root);
            std::sort(components.rbegin(),components.rend());
            std::vector<size_t> loads(commitLanes);
            std::map<size_t,unsigned> placement;
            for(auto [weight,root]:components) {
                auto lane=std::min_element(loads.begin(),loads.end())-loads.begin();
                placement[root]=lane;loads[lane]+=weight;
            }
            for(size_t memory=0;memory<memories.size();++memory)
                if(!memoryWritePorts[memory].empty())memoryCommitLane[memory]=placement.at(commitRoot(memory));
        }
        for(size_t i=0;i<memoryWrites.size();++i)if(!shadowedWrites[i])
            commitWrites[memoryCommitLane[memoryWrites[i].memory]].push_back(i);
        const bool cachedStateTransactions=writeTracking&&threads>1&&!option("native.uncached_state_transactions")&&
            std::all_of(states.begin(),states.end(),[](const auto& state){return state.clock<0;});
        struct StateGroup {
            unsigned lane; std::vector<size_t> updates; std::set<size_t> words; size_t chunk;
            std::vector<Value> triggers,resets; bool alwaysActive=false;
        };
        std::vector<StateGroup> stateGroups;
        if(cachedStateTransactions) {
            // Updates sharing any word must remain in one transaction group.
            // Other groups commute because all operands read the current bank.
            std::vector<size_t> parent(states.size());
            for(size_t i=0;i<parent.size();++i)parent[i]=i;
            auto root=[&](size_t i) {while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
            std::map<size_t,size_t> wordWriter;
            for(size_t i=0;i<states.size();++i)for(auto bit:states[i].bits) {
                auto [at,inserted]=wordWriter.emplace(owner(bit),i);
                if(!inserted)parent[root(i)]=root(at->second);
            }
            std::map<size_t,std::vector<size_t>> components;
            for(size_t i=0;i<states.size();++i)components[root(i)].push_back(i);
            using StateCondition=std::pair<Value,Value>;
            std::map<std::pair<unsigned,std::vector<StateCondition>>,size_t> last;
            for(const auto& [identity,updates]:components) {
                auto lane=parallel.states[updates.front()];std::set<size_t> words;
                std::set<StateCondition> conditions;
                for(auto i:updates) {
                    if(parallel.states[i]!=lane)throw std::runtime_error("state transaction has multiple writer lanes");
                    for(auto bit:states[i].bits)words.insert(owner(bit));
                    conditions.emplace(resolved(states[i].trigger),resolved(states[i].reset));
                }
                // Keep always-active histories and independently enabled state
                // apart: one hot predicate must not invalidate unrelated words.
                auto key=std::make_pair(lane,std::vector<StateCondition>(conditions.begin(),conditions.end()));
                auto found=last.find(key);auto group=found==last.end()?size_t(-1):found->second;
                if(group==size_t(-1)||stateGroups[group].words.size()+words.size()>chunkSize) {
                    group=stateGroups.size();last[key]=group;
                    stateGroups.push_back({lane,{},{},dirtyChunkCount++});
                }
                auto& target=stateGroups[group];target.words.insert(words.begin(),words.end());
                target.updates.insert(target.updates.end(),updates.begin(),updates.end());
            }
            for(auto& group:stateGroups) {
                std::sort(group.updates.begin(),group.updates.end());
                std::set<Value> triggers,resets;
                for(auto i:group.updates) {
                    auto trigger=resolved(states[i].trigger);
                    if(auto value=number(trigger)){if(*value)group.alwaysActive=true;}
                    else triggers.insert(trigger);
                    if(!states[i].reset.empty()) {
                        auto reset=resolved(states[i].reset);
                        if(auto value=number(reset)){if(*value)group.alwaysActive=true;}
                        else resets.insert(reset);
                    }
                }
                group.triggers.assign(triggers.begin(),triggers.end());
                group.resets.assign(resets.begin(),resets.end());
            }
        }

        std::map<int64_t,std::map<size_t,uint64_t>> chunkInputs;
        const bool adaptiveDispatch = memoizeChunks && threads > 1 && !hostOverlap;
        std::map<int64_t,std::pair<size_t,size_t>> chunkWork;
        if (adaptiveDispatch) for (size_t task=0;task<taskNodes.size();++task)
            for (auto index:taskNodes[task]) {
                auto& work=chunkWork[chunkOf[index]];
                work.first=task;++work.second;
            }
        if (memoizeChunks) for (const auto& task:taskNodes) for (auto index:task)
            for (const auto* value:{&nodes[index].left,&nodes[index].right,&nodes[index].select})
                for(auto bit:resolved(*value)) if(bit>1) {
                    auto producer=owner(bit);
                    if(chunkOf[producer]!=chunkOf[index])chunkInputs[chunkOf[index]][producer]|=uint64_t(1)<<lane(bit);
                }
        // Transaction payloads consume the same producer-change notifications
        // as combinational chunks. Enable/edge tests remain live every commit;
        // cached address/data and validation results survive idle clocks.
        for(size_t group=0;group<writeGroups.size();++group)if(cachedWriteGroups[group])
            for(auto i:writeGroups[group].writes)
                for(const auto* value:{&memoryWrites[i].address,&memoryWrites[i].data})
                    for(auto bit:resolved(*value))if(bit>1)
                        chunkInputs[transactionChunks[group]][owner(bit)]|=uint64_t(1)<<lane(bit);
        for(const auto& group:stateGroups) {
            // Include committed words: after a bank switch, a changed word
            // forces one preparation to bring the other bank up to date.
            for(auto word:group.words)chunkInputs[group.chunk][word]|=mask(nodes[word].width);
            for(auto i:group.updates)
                for(const auto* value:{&states[i].next,&states[i].reset,&states[i].resetValue})
                    for(auto bit:resolved(*value))if(bit>1)
                        chunkInputs[group.chunk][owner(bit)]|=uint64_t(1)<<lane(bit);
        }
        std::map<size_t,std::map<size_t,uint64_t>> chunkConsumers;
        if (memoizeDeltas) for (const auto& [chunk,inputs]:chunkInputs)
            for (const auto& [producer,bits]:inputs) chunkConsumers[producer][size_t(chunk)] |= bits;
        // One shared routing table, rather than independent snapshots of the
        // same root in every consumer lane. Workers record future dirty bits;
        // publication executes with all lanes stopped after successful validation.
        std::map<size_t,std::pair<size_t,size_t>> rootRoutes;
        std::vector<std::pair<size_t,uint64_t>> rootConsumers;
        if(writeTracking)for(const auto& [producer,consumers]:chunkConsumers)
            if(nodes[producer].op=="state"||nodes[producer].op=="input") {
                auto first=rootConsumers.size();
                for(const auto& consumer:consumers)rootConsumers.push_back(consumer);
                rootRoutes[producer]={first,rootConsumers.size()};
            }
        if(rootConsumers.size()>UINT32_MAX)throw std::runtime_error("native root routing table too large");
        auto notifyRoot=[&](size_t producer,const std::string& delta) {
            auto found=rootRoutes.find(producer);
            if(found==rootRoutes.end())return std::string();
            auto [first,last]=found->second;
            return "__cpphdl_notify_root("+std::to_string(first)+","+std::to_string(last)+","+delta+");\n";
        };
        if(memoizeDeltas) {
            std::map<size_t,size_t> owners;
            for(size_t task=0;task<taskChunks.size();++task)
                for(auto chunk:taskChunks[task])owners[chunk]=task;
            for(size_t group=0;group<writeGroups.size();++group)if(cachedWriteGroups[group])
                owners[transactionChunks[group]]=writeGroups[group].lane;
            for(const auto& group:stateGroups)owners[group.chunk]=group.lane;
            for(const auto& [producer,consumers]:chunkConsumers) if(chunkOf[producer]>=0)
                for(const auto& [chunk,bits]:consumers)
                    if(owners.at(size_t(chunkOf[producer]))!=owners.at(chunk)||size_t(chunkOf[producer])>=chunk)
                        throw std::runtime_error("native delta cache requires ordered private lane cones");
        }
        std::ofstream header(path), part;
        if (!header) throw std::runtime_error("cannot write native model");
        std::ostream output(header.rdbuf());
        output << "#pragma once\n";
        if (threads > 1) output << "#include <cpphdl_graph_threads.h>\n";
        if(bmi2Gathers)output << "#include <cpphdl_graph_gather.h>\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_random"; }))
            output << "#include <cstdlib>\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_jtag_tick"; }))
            output << "extern \"C\" int jtag_tick(unsigned char*, unsigned char*, unsigned char*, unsigned char*, unsigned char);\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_debug_tick"; }))
            output << "extern \"C\" int debug_tick(unsigned char*, unsigned char, int*, int*, int*, unsigned char, unsigned char*, int, int);\n";
        output << "#include <array>\n#include <bit>\n#include <cmath>\n#include <cstdint>\n#include <stdexcept>\n#include <vector>\n"
                  "namespace cpphdl_native {\nstruct Model {\n";
        output << "static constexpr uint64_t __cpphdl_cached_next_words = " << cachedNextWords << ";\n";
        output << "static constexpr uint64_t __cpphdl_settle_chunks = " << std::count(settleChunks.begin(),settleChunks.end(),true) << ";\n";
        output << "static constexpr bool __cpphdl_delta_memoization = " << (memoizeDeltas?"true":"false") << ";\n";
        output << "static constexpr bool __cpphdl_write_tracking = " << (writeTracking?"true":"false") << ";\n";
        output << "static constexpr bool __cpphdl_owned_memories = " << (ownedMemories?"true":"false") << ";\n";
        auto invalidateMemoryChunk=[&](int64_t chunk) {
            if(!memoizeDeltas)output << "__cpphdl_cache_valid_" << chunk << " = __cpphdl_settle_cache_valid_" << chunk << " = false;\n";
            if(memoizeDeltas)output << "__cpphdl_dirty[" << chunk << "].pending[0] = __cpphdl_dirty[" << chunk << "].pending[1] = true;\n";
        };
        // Coalesce invalidations from all changed write ports of one RAM.
        // The transaction is already validated when these bits are recorded;
        // publication runs with workers stopped, before the next evaluation.
        const bool memoryChangeEvents=(changedMemoryWrites&&memoizeDeltas&&threads>1)||
            (parallelMemoryCommits&&ownedMemories);
        auto compareMemoryWrite=[&](size_t memory) {
            return changedMemoryWrites&&memoizeDeltas&&threads>1&&memoryReadChunks[memory].size()>1;
        };
        if(memoryChangeEvents) {
            std::vector<size_t> offsets{0},readers;
            for(size_t i=0;i<memories.size();++i) {
                for(auto chunk:memoryReadChunks[i])readers.push_back(chunk);
                offsets.push_back(readers.size());
            }
            output << "inline static constexpr std::array<size_t," << offsets.size() << "> __cpphdl_memory_offsets{{";
            for(auto offset:offsets)output << offset << ',';
            output << "}};\ninline static constexpr std::array<size_t," << readers.size() << "> __cpphdl_memory_readers{{";
            for(auto chunk:readers)output << chunk << ',';
            output << "}};\nstruct alignas(64) __cpphdl_MemoryChanges { std::array<uint64_t,"
                   << (memories.size()+63)/64 << "> words{}; };\n"
                   << "std::array<__cpphdl_MemoryChanges," << commitLanes << "> __cpphdl_changed_memories{};\n"
                      "void __cpphdl_note_memory_change(size_t memory, size_t lane) noexcept {\n"
                      "__cpphdl_changed_memories[lane].words[memory/64] |= uint64_t(1) << (memory%64);\n}\n"
                      "void __cpphdl_publish_memory_changes() noexcept {\n"
                      "for(size_t word=0;word<__cpphdl_changed_memories[0].words.size();++word) {\n"
                      "uint64_t changed=0;for(auto& lane:__cpphdl_changed_memories) {\n"
                      "changed |= lane.words[word];lane.words[word]=0;\n}\n"
                      "while(changed) {auto memory=word*64+__builtin_ctzll(changed);changed&=changed-1;\n";
            if(memoizeDeltas)output <<
                      "for(auto i=__cpphdl_memory_offsets[memory];i<__cpphdl_memory_offsets[memory+1];++i) {\n"
                      "auto& dirty=__cpphdl_dirty[__cpphdl_memory_readers[i]];dirty.pending[0]=dirty.pending[1]=true;\n"
                      "}\n";
            else {
                output << "switch(memory) {\n";
                for(const auto& [memory,readers]:memoryReadChunks) {
                    output << "case " << memory << ":\n";
                    for(auto chunk:readers)invalidateMemoryChunk(chunk);
                    output << "break;\n";
                }
                output << "}\n";
            }
            output << "}\n}\n}\n";
        }
        auto noteMemoryChange=[&](size_t memory) {
            if(memoryChangeEvents)output << "__cpphdl_note_memory_change(" << memory << ',' << memoryCommitLane[memory] << ");\n";
            else if(ownedMemories)for(auto chunk:memoryReadChunks[memory])invalidateMemoryChunk(chunk);
        };
        output << "static constexpr size_t __cpphdl_cached_memory_commits = " << cachedMemoryCommitCount << ";\n";
        output << "static constexpr unsigned __cpphdl_memory_commit_lanes = " << commitLanes << ";\n";
        if(parallelMemoryCommits&&!adaptiveDispatch)
            output << "cpphdl::graph_runtime::DispatchPolicy __cpphdl_memory_dispatch_policy = cpphdl::graph_runtime::nativeDispatchPolicy();\n";
        for(size_t memory=0;memory<memories.size();++memory)if(cachedMemoryCommits[memory])
            output << "bool __cpphdl_memory_dirty" << memory << "=true, __cpphdl_memory_apply" << memory << "=false;\n"
                   << "std::array<uint64_t," << (memoryWriteGroups[memory].size()+63)/64 << "> __cpphdl_memory_key" << memory << "{};\n";
        output << "void __cpphdl_invalidate_memories() {\n";
        for(size_t memory=0;memory<memories.size();++memory)if(cachedMemoryCommits[memory])
            output << "__cpphdl_memory_dirty" << memory << "=true;\n";
        for(auto chunk:mutableReadChunks)invalidateMemoryChunk(chunk);
        output << "}\nvoid __cpphdl_refresh_memory_chunks() {\n";
        if(!ownedMemories)output << "__cpphdl_invalidate_memories();\n";
        output << "}\n";
        std::string pendingResets="bool __cpphdl_pending_async_reset() const { return false";
        for(const auto& port:ports)if(!port.input && port.name.rfind("__cpphdl_async_reset_", 0) == 0) {
            if(port.bits.size()!=2)throw std::runtime_error("invalid asynchronous reset event probe");
            pendingResets+=" || (("+port.name+"[0] ^ ("+port.name+"[0] >> 1)) & 1u)";
        }
        output << pendingResets << "; }\n";
        std::string stepBody="void step() { for(unsigned delta=0;;++delta) { evaluate<true>(); evaluate<false>(); if(!__cpphdl_pending_async_reset()) return; if(delta==31) throw std::runtime_error(\"asynchronous reset did not settle\"); } }\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "word_slice"; }))
            output << "template<std::size_t Words> static uint64_t __cpphdl_slice(const std::array<uint64_t,Words>& words, "
                      "uint64_t offset, unsigned width, uint64_t extent) {\n"
                      "if(offset > extent - width) return 0;\n"
                      "const auto word = offset / 64;\nconst auto shift = unsigned(offset % 64);\n"
                      "auto value = words[word] >> shift;\n"
                      "if(shift && word + 1 < Words) value |= words[word + 1] << (64 - shift);\n"
                      "return width == 64 ? value : value & ((1ull << width) - 1);\n}\n";
        for (size_t i = 0; i < clocks.size(); ++i)
            output << "bool " << clocks[i].name << " = false;\nbool __cpphdl_previous_clock_" << i << " = false;\n";
        // Memory depth belongs to runtime storage, never to the bit graph or
        // the C++ stack. Reads observe committed rows throughout evaluation.
        for (size_t index = 0; index < memories.size(); ++index) {
            const auto& memory = memories[index];
            if (!memory.contents.empty()) {
                const auto words = (memory.width + 63) / 64;
                output << "inline static constexpr std::array<std::array<uint64_t," << words << ">,"
                       << memory.depth << "> memory" << index << " = {{\n";
                for (size_t row = 0; row < memory.depth; ++row) {
                    output << "{{";
                    for (unsigned word = 0; word < words; ++word)
                        output << (word ? "," : "") << memory.contents[row * words + word] << "ull";
                    output << "}},\n";
                }
                output << "}};\n";
                continue;
            }
            auto type = "std::vector<std::array<uint64_t," + std::to_string((memories[index].width + 63) / 64) + ">>";
            output << type << " memory" << index << " = " << type << '(' << memories[index].depth << "ull);\n";
        }
        std::map<size_t, std::string> names;
        for (const auto& port : ports) {
            output << "std::array<uint32_t," << (port.bits.size()+31)/32 << "> " << port.name << "{};\n";
            if (port.input) for (unsigned offset = 0; offset < port.bits.size(); offset += 64) {
                auto count = std::min<size_t>(64, port.bits.size() - offset);
                std::string expr = "uint64_t(" + port.name + "[" + std::to_string(offset/32) + "])";
                if (count > 32) expr += " | (uint64_t(" + port.name + "[" + std::to_string(offset/32+1) + "]) << 32)";
                names[owner(port.bits[offset])] = "(" + expr + ")";
            }
        }
        auto stateStorageWidth = [](unsigned width) {
            return width <= 8 ? 8u : width <= 16 ? 16u : width <= 32 ? 32u : 64u;
        };
        // Aliasing/packing leaves old state nodes outside the live schedule.
        // Streaming metadata prevents graph compaction, so filter storage.
        if (threads > 1) {
            // A state word has one writer. Lay out the two banks by owner and
            // keep lane boundaries on distinct cache lines. All lanes read the
            // current bank while writing the inactive bank; publication is one
            // index change after validation, with no per-register gather/copy.
            std::vector<unsigned> stateOwner(nodes.size());
            for (size_t i = 0; i < states.size(); ++i)
                for (auto bit : states[i].bits) stateOwner[owner(bit)] = parallel.states[i];
            output << "struct alignas(64) __cpphdl_StateBank {\n";
            for (unsigned lane = 0; lane < threads; ++lane) {
                bool first = true;
                // Store each register in its smallest native unsigned type.
                // Group by size to avoid padding between narrow registers.
                for (unsigned storageWidth : {64u, 32u, 16u, 8u})
                    for (size_t index = 0; index < nodes.size(); ++index)
                        if (nodes[index].op == "state" && storedState[index] && stateOwner[index] == lane) {
                            if (stateStorageWidth(nodes[index].width) != storageWidth) continue;
                            auto field = "state" + std::to_string(index);
                            names[index] = "uint64_t(__cpphdl_state[__cpphdl_current]." + field + ")";
                            output << (first ? "alignas(64) " : "") << "uint" << storageWidth << "_t " << field << " = "
                                   << number(nodes[index].left).value_or(0) << "ull;\n";
                            first = false;
                        }
            }
            output << "};\n__cpphdl_StateBank __cpphdl_state[2]{};\nbool __cpphdl_current = false;\n";
        } else for (unsigned storageWidth : {64u, 32u, 16u, 8u})
            for (size_t index = 0; index < nodes.size(); ++index)
                if (nodes[index].op == "state" && storedState[index] && stateStorageWidth(nodes[index].width) == storageWidth) {
                    auto field = "state" + std::to_string(index);
                    names[index] = "uint64_t(" + field + ")";
                    output << "uint" << storageWidth << "_t " << field << " = "
                           << number(nodes[index].left).value_or(0) << "ull;\n";
                }
        output << "bool __cpphdl_advance_quiet_event() {\n";
        if(quietEvents) {
            output << "if(__cpphdl_event_active[0]) return false;\n";
            for(const auto& port:ports)if(!port.input&&port.name.rfind("__cpphdl_event_history_", 0) == 0) {
                auto bit=resolve(port.bits.at(1));auto index=owner(bit);
                if(bit<2||lane(bit)||nodes[index].op!="state"||nodes[index].width!=1)
                    throw std::runtime_error("quiet-event history lost its state identity");
                // The caller advances histories only after all lanes joined.
                // Keep both banks coherent: a later cached commit may switch
                // banks without evaluating an unchanged history again.
                if(writeTracking)output << notifyRoot(index,"("+names.at(index)+" ^ ("+port.name+"[0] & 1u))");
                if(threads>1)
                    output << "__cpphdl_state[0].state" << index << " = __cpphdl_state[1].state" << index
                           << " = " << port.name << "[0] & 1u;\n";
                else output << "state" << index << " = " << port.name << "[0] & 1u;\n";
            }
            output << "return true;\n";
        }else output << "return false;\n";
        output << "}\n";
        // Only values that escape their defining chunk need model storage.
        // Keeping the rest as scalars lets the host compiler eliminate their
        // stores and allocate registers without exposing them to other calls.
        std::map<size_t, size_t> sharedSlots;
        if (chunkSize) {
            auto retain = [&](const Value& bits, int64_t consumer = -1) {
                for (auto bit : resolved(bits)) if (bit > 1) {
                    auto index = owner(bit);
                    if (chunkOf[index] >= 0 && chunkOf[index] != consumer)
                        sharedSlots.emplace(index, sharedSlots.size());
                }
            };
            for (auto index : order) if (chunkOf[index] >= 0)
                for (const auto* bits : {&nodes[index].left, &nodes[index].right, &nodes[index].select})
                    retain(*bits, chunkOf[index]);
            for (const auto& port : ports) if (!port.input) retain(port.bits);
            for (const auto& node : nodes) if (node.validation()) retain(node.left);
            for (const auto& state : states)
                for (const auto* bits : {&state.next, &state.trigger, &state.reset, &state.resetValue}) retain(*bits);
            for (const auto& access : memoryAccesses) { retain(access.address); retain(access.enabled); }
            for (const auto& write : memoryWrites)
                for (const auto* bits : {&write.address, &write.data, &write.enabled}) retain(*bits);
            size_t storage = sharedSlots.size();
            if (threads > 1) {
                // Different tasks never write the same cache line of shared
                // intermediates, even if they execute on different lanes.
                storage = 0;
                for (const auto& task : taskNodes) {
                    storage = (storage + 7) / 8 * 8;
                    for (auto node : task) if (sharedSlots.count(node)) sharedSlots[node] = storage++;
                }
            }
            output << (threads > 1 ? "alignas(64) " : "")
                   << "std::array<uint64_t," << storage << "> __cpphdl_values{};\n";
        }
        if (threads > 1)
            output << "static constexpr unsigned __cpphdl_thread_count = " << workerCount << ";\n"
                   << "static constexpr unsigned __cpphdl_task_count = " << threads << ";\n"
                   << "std::shared_ptr<cpphdl::graph_runtime::Threads> __cpphdl_threads = "
                      "std::make_shared<cpphdl::graph_runtime::Threads>("
                   << workerCount << ',' << taskNodes.size() << ");\n";
        if (adaptiveDispatch)
            output << "struct alignas(64) __cpphdl_Activity { uint64_t nodes = 0; };\n"
                   << "std::array<__cpphdl_Activity," << threads << "> __cpphdl_activity{};\n"
                      "bool __cpphdl_serial_dispatch = false;\n"
                      "std::array<bool,2> __cpphdl_serial_by_phase{};\n"
                      "cpphdl::graph_runtime::DispatchPolicy __cpphdl_dispatch_policy = cpphdl::graph_runtime::nativeDispatchPolicy();\n";
        auto valueName = [&](size_t index) {
            auto slot = sharedSlots.find(index);
            return slot != sharedSlots.end() ? "__cpphdl_values[" + std::to_string(slot->second) + "]" : "value" + std::to_string(index);
        };
        if (chunkSize && hostEffects) {
            const unsigned sizes[] = {8, 8, 32, 32, 32, 8, 8, 32, 32};
            for (auto index : order) if (nodes[index].op == "host_debug_tick")
                for (unsigned argument = 0; argument < 9; ++argument)
                    output << (sizes[argument] == 8 ? "unsigned char " : "int ")
                           << "debug" << index << '_' << argument << " = 0;\n";
        }
        auto declaration = [&](size_t index) {
            return sharedSlots.count(index) || !settleLive[index] ? "" : "const uint64_t ";
        };
        auto assembly = [&](Value value) {
            value = resolved(value);
            if (value.size() > 64) throw std::runtime_error("wide assembly");
            std::string result;
            uint64_t constants = 0;
            for (unsigned offset = 0; offset < value.size();) {
                auto bit = value[offset];
                if (bit < 2) { constants |= bit << offset; ++offset; continue; }
                auto index = owner(bit); auto first = lane(bit);
                unsigned count = 1;
                while (offset + count < value.size() && value[offset+count] == bit) ++count;
                if (count > 1) {
                    // Packed Boolean graphs frequently broadcast an enable
                    // or sign bit across a word. Keep that operation compact
                    // instead of emitting up to 64 shifted copies of it.
                    std::string term = names.count(index) ? names.at(index) : valueName(index);
                    if (first) term = "(" + term + " >> " + std::to_string(first) + ")";
                    term = "((" + term + " & 1ull) * " + std::to_string(mask(count)) + "ull)";
                    if (offset) term = "(" + term + " << " + std::to_string(offset) + ")";
                    if (!result.empty()) result += " | "; result += term; offset += count;
                    continue;
                }
                while (offset + count < value.size() && value[offset+count] == bit+count && first+count < nodes[index].width) ++count;
                std::string term = names.count(index) ? names.at(index) : valueName(index);
                if (first) term = "(" + term + " >> " + std::to_string(first) + ")";
                if (count < 64) term = "(" + term + " & " + std::to_string(mask(count)) + "ull)";
                if (offset) term = "(" + term + " << " + std::to_string(offset) + ")";
                if (!result.empty()) result += " | "; result += term; offset += count;
            }
            if (constants || result.empty()) { if (!result.empty()) result += " | "; result += std::to_string(constants) + "ull"; }
            auto expression="(" + result + ")";
            if(coalesceBits||bmi2Gathers)expression=nativeCoalescedAssembly(value,[&](size_t index) {
                return names.count(index)?names.at(index):valueName(index);
            },expression,bmi2Gathers);
            return expression;
        };
        auto clockEdge = [&](int clock, bool falling) {
            if (clock < 0) return std::string("true");
            auto previous = "__cpphdl_previous_clock_" + std::to_string(clock);
            auto current = clocks.at(clock).name;
            return "(" + (falling ? "!" + current + " && " + previous : current + " && !" + previous) + ")";
        };
        // Each intermediate has consumers only in its own independent lane.
        // Root snapshots are updated by the caller before dispatch. Workers
        // propagate exact changed-bit masks forward inside their lane; pending
        // invalidations survive output-only phases which skip some chunks.
        std::vector<unsigned> chunkLane;
        if(memoizeDeltas) {
            chunkLane.resize(dirtyChunkCount);
            for(size_t group=0;group<writeGroups.size();++group)if(cachedWriteGroups[group])
                chunkLane.at(transactionChunks[group])=writeGroups[group].lane;
            for(const auto& group:stateGroups)chunkLane.at(group.chunk)=group.lane;
            for(unsigned lane=0;lane<taskChunks.size();++lane)
                for(auto chunk:taskChunks[lane])chunkLane.at(chunk)=lane;
        }
        auto emitInvalidation = [&](size_t producer,const std::string& delta,int rootLane=-1) {
            auto consumers=chunkConsumers.find(producer);
            if (consumers==chunkConsumers.end()) return;
            output << "if(" << delta << ") {\n";
            for (const auto& [chunk,bits]:consumers->second) {
                if(rootLane>=0&&chunkLane.at(chunk)!=unsigned(rootLane))continue;
                if(bits!=mask(nodes[producer].width))output << "if(" << delta << " & " << bits << "ull) ";
                output << "__cpphdl_dirty[" << chunk << "].pending[0] = __cpphdl_dirty[" << chunk << "].pending[1] = true;\n";
            }
            output << "}\n";
        };
        if(memoizeDeltas) {
            const size_t count=dirtyChunkCount;
            output << "struct alignas(64) __cpphdl_Dirty { std::array<bool,2> pending{{true,true}}; };\n"
                   << "std::array<__cpphdl_Dirty," << count << "> __cpphdl_dirty{};\n";
            if(writeTracking) {
                output << "struct __cpphdl_RootConsumer { uint32_t chunk; uint64_t bits; };\n"
                       << "inline static constexpr std::array<__cpphdl_RootConsumer," << rootConsumers.size()
                       << "> __cpphdl_root_consumers{{\n";
                for(auto [chunk,bits]:rootConsumers)output << "{" << chunk << "," << bits << "ull},\n";
                output << "}};\nvoid __cpphdl_notify_root(uint32_t first,uint32_t last,uint64_t delta) noexcept {\n"
                          "if(!delta) return;\nfor(auto i=first;i<last;++i) {\n"
                          "const auto& consumer=__cpphdl_root_consumers[i];\n"
                          "if(delta & consumer.bits) __cpphdl_dirty[consumer.chunk].pending[0] = __cpphdl_dirty[consumer.chunk].pending[1] = true;\n"
                          "}\n}\nvoid __cpphdl_invalidate_state() noexcept {\n"
                          "for(auto& chunk:__cpphdl_dirty) chunk.pending[0]=chunk.pending[1]=true;\n";
                for(const auto& group:stateGroups)
                    output << "__cpphdl_pending_" << group.lane << ".state_bank_dirty" << group.chunk << "=true;\n";
                output << "}\n";
                std::map<size_t,size_t> inputs;
                for(const auto& [producer,route]:rootRoutes)if(nodes[producer].op=="input")inputs.emplace(producer,inputs.size());
                output << "static constexpr size_t __cpphdl_input_root_count = " << inputs.size() << ";\n"
                       << "std::array<uint64_t," << inputs.size() << "> __cpphdl_input_keys{};\n";
                auto emitInput=[&](size_t producer,size_t key) {
                    output << "const uint64_t input" << key << " = " << names.at(producer) << ";\n"
                           << "const uint64_t delta" << key << " = input" << key << " ^ __cpphdl_input_keys[" << key << "];\n"
                           << "__cpphdl_input_keys[" << key << "] = input" << key << ";\n";
                    emitInvalidation(producer,"delta"+std::to_string(key));
                };
                output << "void __cpphdl_mark_roots() {\n";
                for(auto [producer,key]:inputs)emitInput(producer,key);
                output << "}\n";
                // Explicit setters record host writes immediately. Raw array
                // writes remain supported by mark_roots at the next evaluation.
                for(const auto& port:ports)if(port.input) {
                    output << "void __cpphdl_set_" << port.name << "(size_t __cpphdl_word,uint32_t __cpphdl_value) {\n"
                           << port.name << ".at(__cpphdl_word)=__cpphdl_value;\n";
                    std::set<size_t> producers;
                    for(auto bit:resolved(port.bits))if(bit>1&&inputs.count(owner(bit)))producers.insert(owner(bit));
                    for(auto producer:producers)emitInput(producer,inputs.at(producer));
                    output << "}\n";
                }
                if(threads>1) {
                    auto words=(count+63)/64;
                    output << "void __cpphdl_notify_pending(std::array<uint64_t," << words
                           << ">& pending,uint32_t first,uint32_t last,uint64_t delta) noexcept {\n"
                              "if(!delta) return;\nfor(auto i=first;i<last;++i) {\n"
                              "const auto& consumer=__cpphdl_root_consumers[i];\n"
                              "if(delta & consumer.bits) pending[consumer.chunk/64] |= uint64_t(1) << (consumer.chunk%64);\n}\n}\n";
                    for(unsigned lane=0;lane<threads;++lane)
                        output << "alignas(64) std::array<uint64_t," << words << "> __cpphdl_pending_state_" << lane << "{};\n";
                    output << "void __cpphdl_publish_root_events() noexcept {\n";
                    output << "for(size_t word=0;word<" << words << ";++word) {\nuint64_t dirty=0";
                    for(unsigned lane=0;lane<threads;++lane)
                        output << " | __cpphdl_pending_state_" << lane << "[word]";
                    output << ";\n";
                    for(unsigned lane=0;lane<threads;++lane)output << "__cpphdl_pending_state_" << lane << "[word]=0;\n";
                    output << "while(dirty) {\nconst auto chunk=word*64+__builtin_ctzll(dirty);\n"
                              "__cpphdl_dirty[chunk].pending[0]=__cpphdl_dirty[chunk].pending[1]=true;\n"
                              "dirty &= dirty-1;\n}\n}\n}\nuint64_t __cpphdl_pending_root_work(bool commit=false) const {\nuint64_t work = " << inputs.size() << ";\nif(commit) {\n";
                    if(cachedStateTransactions) {
                        for(const auto& group:stateGroups)
                            output << "if(__cpphdl_dirty[" << group.chunk << "].pending[1]) work += " << group.words.size()+group.updates.size() << ";\n";
                    }else output << "work += " << states.size() << ";\n";
                    output << "}\nreturn work;\n}\n";
                }
            }else if(!parallelRoots||threads==1) {
                std::vector<size_t> roots;
                for(const auto& [producer,consumers]:chunkConsumers)
                    if(nodes[producer].op=="input"||nodes[producer].op=="state")roots.push_back(producer);
                output << "std::array<uint64_t," << roots.size() << "> __cpphdl_root_keys{};\n";
                std::vector<std::vector<size_t>> groups;
                auto appendGroups=[&](const std::vector<size_t>& selected) {
                    for(size_t first=0;first<selected.size();first+=128)
                        groups.emplace_back(selected.begin()+first,selected.begin()+std::min(selected.size(),first+128));
                };
                std::vector<size_t> inputs,states;
                for(size_t i=0;i<roots.size();++i)
                    (cacheStateRoots&&nodes[roots[i]].op=="state"?states:inputs).push_back(i);
                appendGroups(inputs);
                const auto inputGroups=groups.size();
                appendGroups(states);
                if(cacheStateRoots)output << "bool __cpphdl_root_states_seen = false, __cpphdl_root_state_bank = false;\n";
                for(size_t group=0;group<groups.size();++group) {
                    output << "[[gnu::noinline]] void __cpphdl_roots_" << group << "() {\n";
                    for(auto i:groups[group]) {
                        auto producer=roots[i];const auto source=names.at(producer);
                        output << "const uint64_t value" << i << " = " << source << ";\n"
                               << "const uint64_t delta" << i << " = __cpphdl_root_keys[" << i << "] ^ value" << i << ";\n"
                               << "__cpphdl_root_keys[" << i << "] = value" << i << ";\n";
                        emitInvalidation(producer,"delta"+std::to_string(i));
                    }
                    output << "}\n";
                }
                output << "void __cpphdl_mark_roots() {\n";
                for(size_t group=0;group<inputGroups;++group)output << "__cpphdl_roots_" << group << "();\n";
                if(cacheStateRoots)output << "if(__cpphdl_root_states_seen && __cpphdl_root_state_bank == __cpphdl_current) return;\n"
                    "__cpphdl_root_states_seen = true; __cpphdl_root_state_bank = __cpphdl_current;\n";
                for(size_t group=inputGroups;group<groups.size();++group)output << "__cpphdl_roots_" << group << "();\n";
                output << "}\n";
            }else {
            // Each lane owns its dirty flags and root keys. Shared roots are
            // observed independently, so workers never write another lane's
            // pending flags or race a shared key. Inputs/current state remain
            // stable until every lane has prepared its transaction.
            std::vector<std::vector<size_t>> roots(threads);
            for(const auto& [producer,consumers]:chunkConsumers) {
                if(nodes[producer].op!="input"&&nodes[producer].op!="state")continue;
                std::set<unsigned> lanes;
                for(const auto& [chunk,bits]:consumers)lanes.insert(chunkLane.at(chunk));
                for(auto lane:lanes)roots.at(lane).push_back(producer);
            }
            for(unsigned lane=0;lane<threads;++lane) {
                const auto suffix="lane"+std::to_string(lane);
                output << "alignas(64) std::array<uint64_t," << roots[lane].size()
                       << "> __cpphdl_root_keys_" << suffix << "{};\n";
                std::vector<size_t> inputs,states;
                for(size_t i=0;i<roots[lane].size();++i)
                    (cacheStateRoots&&nodes[roots[lane][i]].op=="state"?states:inputs).push_back(i);
                if(cacheStateRoots)output << "bool __cpphdl_roots_seen_" << suffix << " = false, __cpphdl_roots_bank_" << suffix << " = false;\n";
                auto emitRootGroups=[&](const std::vector<size_t>& selected,const char* kind) {
                size_t groups=(selected.size()+127)/128;
                for(size_t group=0;group<groups;++group) {
                    output << "[[gnu::noinline]] void __cpphdl_roots_" << suffix << '_' << kind << '_' << group << "() {\n";
                    if(adaptiveDispatch)output << "__cpphdl_activity[" << lane << "].nodes += " << std::min<size_t>(128,selected.size()-group*128) << ";\n";
                    for(size_t offset=group*128;offset<std::min(selected.size(),(group+1)*128);++offset) {
                        auto i=selected[offset];
                        auto producer=roots[lane][i];const auto source=names.at(producer);
                        output << "const uint64_t value" << i << " = " << source << ";\n"
                               << "const uint64_t delta" << i << " = __cpphdl_root_keys_" << suffix << '[' << i << "] ^ value" << i << ";\n"
                               << "__cpphdl_root_keys_" << suffix << '[' << i << "] = value" << i << ";\n";
                        emitInvalidation(producer,"delta"+std::to_string(i),int(lane));
                    }
                    output << "}\n";
                }
                return groups;
                };
                auto inputGroups=emitRootGroups(inputs,"input"),stateGroups=emitRootGroups(states,"state");
                output << "void __cpphdl_mark_roots_" << suffix << "() {\n";
                for(size_t group=0;group<inputGroups;++group)output << "__cpphdl_roots_" << suffix << "_input_" << group << "();\n";
                if(cacheStateRoots)output << "if(__cpphdl_roots_seen_" << suffix << " && __cpphdl_roots_bank_" << suffix << " == __cpphdl_current) return;\n"
                    << "__cpphdl_roots_seen_" << suffix << " = true; __cpphdl_roots_bank_" << suffix << " = __cpphdl_current;\n";
                for(size_t group=0;group<stateGroups;++group)output << "__cpphdl_roots_" << suffix << "_state_" << group << "();\n";
                output << "}\n";
            }
            size_t stateSinkWork=std::count(storedState.begin(),storedState.end(),true);
            for(const auto& state:states)stateSinkWork+=(state.bits.size()+63)/64;
            output << "uint64_t __cpphdl_pending_root_work(bool commit = false) const { return (commit ? " << stateSinkWork << " : 0)";
            for(unsigned lane=0;lane<threads;++lane) {
                size_t states=std::count_if(roots[lane].begin(),roots[lane].end(),[&](auto i){return nodes[i].op=="state";});
                if(cacheStateRoots)output << " + " << roots[lane].size()-states << " + ((!__cpphdl_roots_seen_lane" << lane
                    << " || __cpphdl_roots_bank_lane" << lane << " != __cpphdl_current) ? " << states << " : 0)";
                else output << " + " << roots[lane].size();
            }
            output << "; }\n";
            output << "void __cpphdl_mark_roots() {\n";
            for(unsigned lane=0;lane<threads;++lane)output << "__cpphdl_mark_roots_lane" << lane << "();\n";
            output << "}\n";
            }
        }
        auto deltaTracked = [&](size_t index) {return memoizeDeltas&&chunkConsumers.count(index);};
        auto rememberPrevious = [&](size_t index) {
            if(deltaTracked(index))output << "const uint64_t __cpphdl_before_" << index << " = " << valueName(index) << ";\n";
        };
        auto publishDelta = [&](size_t index) {
            if(deltaTracked(index)) {
                const auto delta="__cpphdl_delta_"+std::to_string(index);
                output << "const uint64_t " << delta << " = __cpphdl_before_" << index << " ^ " << valueName(index) << ";\n";
                emitInvalidation(index,delta);
            }
        };
        // Output settling does not need next-state logic. Make the two phases
        // compile-time distinct so the host compiler can remove that work,
        // rather than testing a runtime flag after computing both schedules.
        unsigned chunks = 0;
        unsigned parts = 0;
        auto closeChunk = [&] {
            output << "}\n";
            if (splitChunks)
                for (const auto* mode : {"false", "true"})
                    output << "template void Model::__cpphdl_chunk_" << chunks - 1 << '<' << mode << ">();\n";
        };
        auto closePart = [&] {
            if (!part.is_open()) return;
            output << "}\n";
            output.flush();
            output.rdbuf(header.rdbuf());
            part.close();
        };
        auto emitNode = [&](size_t index) {
            const auto& node = nodes[index];
            rememberPrevious(index);
            if (node.op == "word_slice") {
                if (!node.width || node.width > 64 || node.left.size() < node.width ||
                    node.right.empty() || node.right.size() > 64 || !node.select.empty())
                    throw std::runtime_error("invalid native word slice");
                output << "const std::array<uint64_t," << (node.left.size() + 63) / 64 << "> slice" << index << " = {";
                for (unsigned offset = 0; offset < node.left.size(); offset += 64)
                    output << (offset ? "," : "")
                           << assembly(slice(node.left, offset, std::min<size_t>(64, node.left.size() - offset)));
                output << "};\n" << declaration(index) << valueName(index) << " = __cpphdl_slice(slice" << index
                       << ',' << assembly(node.right) << ',' << node.width << ',' << node.left.size() << ");\n";
                publishDelta(index);
                return;
            }
            if (node.op == "memory_read") {
                auto memory = number(node.right), word = number(node.select);
                if (!memory || *memory >= memories.size() || !word || *word >= (memories[*memory].width + 63) / 64 ||
                    node.left.empty() || node.left.size() > 64 || node.width != std::min<uint64_t>(64, memories[*memory].width - *word * 64))
                    throw std::runtime_error("invalid graph memory read");
                auto address = assembly(node.left);
                output << declaration(index) << valueName(index) << " = " << address << " < " << memories[*memory].depth
                       << "ull ? memory" << *memory << '[' << address << "][" << *word << "] : 0ull;\n";
                publishDelta(index);
                return;
            }
            if (node.op == "host_debug_tick") {
                if (node.width != 32 || node.right.size() != 192 || node.select.size() != 1)
                    throw std::runtime_error("invalid debug_tick effect layout");
                const unsigned sizes[] = {8, 8, 32, 32, 32, 8, 8, 32, 32};
                const bool pointers[] = {true, false, true, true, true, false, true, false, false};
                auto prefix = "debug" + std::to_string(index);
                unsigned offset = 0;
                // Initialize pointer locals from current storage: a callback
                // may intentionally leave outputs unchanged on a stalled tick.
                for (unsigned argument = 0; argument < 9; ++argument) {
                    auto type = sizes[argument] == 8 ? "unsigned char" : "int";
                    if (!chunkSize && settleLive[index]) output << type << ' ';
                    output << prefix << '_' << argument << " = static_cast<"
                           << type << ">(" << assembly(slice(node.right, offset, sizes[argument])) << ");\n";
                    offset += sizes[argument];
                }
                output << (sharedSlots.count(index) || !settleLive[index] ? "" : "uint64_t ") << valueName(index) << " = 0;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n"
                       << valueName(index) << " = uint32_t(::debug_tick(";
                for (unsigned argument = 0; argument < 9; ++argument) {
                    if (argument) output << ", ";
                    if (pointers[argument]) output << '&';
                    output << prefix << '_' << argument;
                }
                output << "));\n} }\n";
                return;
            }
            if (node.op == "host_debug_output") {
                auto argument = number(node.select);
                if (node.left.size() != 1 || node.left.front() < 2 || !argument ||
                    (*argument != 0 && *argument != 2 && *argument != 3 && *argument != 4 && *argument != 6) ||
                    nodes[owner(node.left.front())].op != "host_debug_tick" ||
                    node.width != (*argument == 0 || *argument == 6 ? 8u : 32u))
                    throw std::runtime_error("invalid debug_tick output projection");
                output << declaration(index) << valueName(index) << " = uint32_t(debug"
                       << owner(node.left.front()) << '_' << *argument << ");\n";
                return;
            }
            auto left = assembly(node.left), right = assembly(node.right);
            if (node.op == "host_jtag_tick") {
                if (node.width != 64 || node.right.size() != 40 || node.select.size() != 1)
                    throw std::runtime_error("invalid jtag_tick effect layout");
                auto prefix = "jtag" + std::to_string(index);
                output << (sharedSlots.count(index) || !settleLive[index] ? "" : "uint64_t ") << valueName(index)
                       << " = (" << right << " & 4294967295ull) << 32;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << "unsigned char " << prefix << '_' << argument << " = static_cast<unsigned char>("
                           << assembly(slice(node.right, 8 * argument, 8)) << ");\n";
                output << "const int " << prefix << "_result = ::jtag_tick(";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << '&' << prefix << '_' << argument << ", ";
                output << "static_cast<unsigned char>(" << assembly(slice(node.right, 32, 8)) << "));\n"
                       << valueName(index) << " = uint64_t(uint32_t(" << prefix << "_result))";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << " | (uint64_t(" << prefix << '_' << argument << ") << " << 32 + 8 * argument << ')';
                output << ";\n} }\n";
                return;
            }
            std::string expr;
            auto op = node.op;
            if (op.compare(0, 3, "fp_") == 0) {
                auto floating = [](const std::string& bits, unsigned width) {
                    if (width != 32 && width != 64) throw std::runtime_error("invalid native floating-point width");
                    return std::string("std::bit_cast<") + (width == 32 ? "float>(uint32_t(" : "double>(uint64_t(") + bits + "))";
                };
                auto integer = [](const std::string& value, unsigned width) {
                    return std::string("std::bit_cast<") + (width == 32 ? "uint32_t>(float(" : "uint64_t>(double(") + value + "))";
                };
                if (op == "fp_from_signed" || op == "fp_from_unsigned") {
                    auto input = left;
                    if (op == "fp_from_signed") input = "(int64_t(" + input + " << " + std::to_string(64-node.left.size()) + ") >> " + std::to_string(64-node.left.size()) + ")";
                    expr = integer(input, node.width);
                }
                else {
                    auto lhs = floating(left, node.left.size());
                    if (op == "fp_to_signed" || op == "fp_to_unsigned")
                        expr = std::string(op == "fp_to_signed" ? "int64_t(" : "uint64_t(") + lhs + ')';
                    else if (op == "fp_cast") expr = integer(lhs, node.width);
                    else if (op == "fp_sqrt" || op == "fp_exp" || op == "fp_tanh" || op == "fp_log" || op == "fp_fabs")
                        expr = integer("std::" + op.substr(3) + '(' + lhs + ')', node.width);
                    else {
                        static const std::map<std::string, std::string> operations{{"fp_add","+"},{"fp_sub","-"},{"fp_mul","*"},{"fp_div","/"},
                            {"fp_eq","=="},{"fp_ne","!="},{"fp_lt","<"},{"fp_le","<="},{"fp_gt",">"},{"fp_ge",">="}};
                        if (!operations.count(op)) throw std::runtime_error("invalid native floating-point operation " + op);
                        expr = lhs + ' ' + operations.at(op) + ' ' + floating(right, node.right.size());
                        if (node.width != 1) expr = integer(expr, node.width);
                    }
                }
            }
            else if (op == "host_random") expr = "Commit && " + assembly(node.select) + " ? uint64_t(::random()) : 0ull";
            else if (op == "mux") expr = assembly(node.select) + " ? " + left + " : " + right;
            else if (op == "any") expr = left + " != 0";
            else if (op == "all") expr = left + " == " + std::to_string(mask(node.left.size())) + "ull";
            else if (op == "parity") expr = "__builtin_parityll(" + left + ")";
            else if (op == "shl" || op == "shr") expr = right + " < 64 ? " + left + (op == "shl" ? " << " : " >> ") + right + " : 0ull";
            else if (op == "sar" || op == "slt" || op == "sdiv" || op == "smod") {
                auto sign = [](std::string value, unsigned width) {
                    return "(int64_t(" + value + " << " + std::to_string(64-width) + ") >> " + std::to_string(64-width) + ")";
                };
                if (op == "slt") expr = sign(left, node.left.size()) + " < " + sign(right, node.right.size());
                else if (op == "sar") expr = sign(left, node.left.size()) + " >> (" + right + " < 64 ? " + right + " : 63)";
                else {
                    auto lhs = sign(left, node.left.size()), rhs = sign(right, node.right.size());
                    expr = "!" + right + " ? 0ull : (" + lhs + " == INT64_MIN && " + rhs + " == -1) ? "
                         + (op == "sdiv" ? left : "0ull") + " : uint64_t(" + lhs
                         + (op == "sdiv" ? " / " : " % ") + rhs + ")";
                }
            } else {
                static const std::map<std::string, std::string> operators{{"and","&"},{"or","|"},{"xor","^"},{"add","+"},{"sub","-"},{"mul","*"},{"div","/"},{"mod","%"},{"eq","=="},{"lt","<"}};
                if (!operators.count(op)) throw std::runtime_error("invalid codegen op " + op);
                expr = left + " " + operators.at(op) + " " + right;
                if (op == "div" || op == "mod") expr = right + " ? (" + expr + ") : 0ull";
            }
            output << declaration(index) << valueName(index) << " = (" << expr << ") & " << mask(node.width) << "ull;\n";
            publishDelta(index);
        };
        int64_t previousChunk = -1;
        if (!chunkSize) output << "template<bool Commit> void evaluate() {\n";
        for (auto index : order) {
            const auto& node = nodes[index];
            if (node.op == "state" || node.op == "input" || node.validation()) continue;
            if (chunkSize && chunkOf[index] != previousChunk) {
                if (chunks) closeChunk();
                if (splitChunks) {
                    if (chunks % splitChunks == 0) {
                        closePart();
                        auto modelPath = std::filesystem::path(path);
                        auto partPath = modelPath.parent_path() /
                            (modelPath.stem().string() + ".part." + std::to_string(parts++) + ".cpp");
                        part.open(partPath);
                        if (!part) throw std::runtime_error("cannot write native model part");
                        output.rdbuf(part.rdbuf());
                        output << "#include " << nativeStringLiteral(modelPath.filename().string())
                               << "\nnamespace cpphdl_native {\n";
                    }
                    header << "template<bool Commit> ";
                    header << "[[gnu::noinline]] void __cpphdl_chunk_" << chunks << "();\n";
                }
                if(memoizeChunks) {
                    header << "bool __cpphdl_cache_valid_" << chunks << " = false;\n"
                           << "bool __cpphdl_settle_cache_valid_" << chunks << " = false;\n";
                    if(!memoizeDeltas) header << "std::array<uint64_t," << chunkInputs[chunkOf[index]].size()
                                             << "> __cpphdl_cache_" << chunks << "[2]{};\n";
                }
                output << "template<bool Commit> ";
                output << "[[gnu::noinline]] void " << (splitChunks ? "Model::" : "")
                       << "__cpphdl_chunk_" << chunks << "() {\n";
                if(memoizeChunks) {
                    output << "constexpr bool __cpphdl_cache_phase = " << (workOnlyChunks.count(chunkOf[index]) ? "Commit" : "true") << ";\n"
                           << "auto& __cpphdl_cache_valid = __cpphdl_cache_phase ? __cpphdl_cache_valid_" << chunks
                           << " : __cpphdl_settle_cache_valid_" << chunks << ";\n";
                }
                if(memoizeChunks) {
                    if(memoizeDeltas) {
                        output << "if(!__cpphdl_dirty[" << chunkOf[index] << "].pending[__cpphdl_cache_phase]) return;\n"
                               << "__cpphdl_dirty[" << chunkOf[index] << "].pending[__cpphdl_cache_phase] = false;\n"
                               << "__cpphdl_cache_valid = true;\n";
                    } else {
                    std::vector<std::string> sources;
                    for(auto [producer,bits]:chunkInputs[chunkOf[index]]) {
                        auto source=names.count(producer)?names.at(producer):valueName(producer);
                        if(bits!=mask(nodes[producer].width))source="("+source+" & "+std::to_string(bits)+"ull)";
                        sources.push_back(std::move(source));
                    }
                    if (sources.size() >= 32) {
                        // A large short-circuit expression makes the host
                        // compiler optimize hundreds of branch edges. Gather
                        // each immutable key once and compare contiguous words.
                        output << "const std::array<uint64_t," << sources.size() << "> __cpphdl_key{{";
                        for (size_t i=0;i<sources.size();++i)
                            output << (i ? "," : "") << sources[i];
                        output << "}};\nif(__cpphdl_cache_valid"
                               << " && __cpphdl_cache_" << chunks << "[__cpphdl_cache_phase] == __cpphdl_key) return;\n"
                               << "__cpphdl_cache_" << chunks << "[__cpphdl_cache_phase] = __cpphdl_key;\n"
                               << "__cpphdl_cache_valid = true;\n";
                    } else {
                        output << "if(__cpphdl_cache_valid";
                        for(size_t i=0;i<sources.size();++i)
                            output << " && __cpphdl_cache_" << chunks << "[__cpphdl_cache_phase][" << i << "] == (" << sources[i] << ')';
                        output << ") return;\n__cpphdl_cache_valid = true;\n";
                        for(size_t i=0;i<sources.size();++i)
                            output << "__cpphdl_cache_" << chunks << "[__cpphdl_cache_phase][" << i << "] = " << sources[i] << ";\n";
                    }
                    }
                    if (adaptiveDispatch) {
                        const auto [task,count]=chunkWork.at(chunkOf[index]);
                        output << "__cpphdl_activity[" << task << "].nodes += " << count << ";\n";
                    }
                }
                ++chunks;
                previousChunk = chunkOf[index];
            }
            // Cross-chunk stores are observable to the host compiler even
            // when no output consumes them. Compile-time liveness removes
            // those stores as well as their producers from the settling phase.
            // Keep local declarations in scope for later work-only consumers.
            if (!settleLive[index]) {
                if (!sharedSlots.count(index)) output << "uint64_t " << valueName(index) << " = 0;\n";
                if (!chunkSize && node.op == "host_debug_tick") {
                    const unsigned sizes[] = {8, 8, 32, 32, 32, 8, 8, 32, 32};
                    for (unsigned argument = 0; argument < 9; ++argument)
                        output << (sizes[argument] == 8 ? "unsigned char " : "int ")
                               << "debug" << index << '_' << argument << " = 0;\n";
                }
                output << "if constexpr(Commit) {\n";
            }
            emitNode(index);
            if (!settleLive[index]) output << "}\n";
        }
        if (chunkSize) {
            if (chunks) closeChunk();
            if (splitChunks) closePart();
            if (threads > 1) {
                // Endpoints are assembled on the same lane as their logic.
                // Only compact host outputs, validation flags and RAM write
                // transactions cross back to the caller. Scratch and inactive
                // registers may be discarded if validation fails.
                auto pending = [](unsigned lane) { return "__cpphdl_pending_" + std::to_string(lane); };
                if(cachedStateTransactions) {
                    output << "bool __cpphdl_last_state_committed = true;\n"
                              "void __cpphdl_begin_state_transactions() noexcept {\n"
                              "if(!__cpphdl_last_state_committed) {\n";
                    for(const auto& group:stateGroups)output << "__cpphdl_dirty[" << group.chunk << "].pending[1]=true;\n";
                    output << "}\n__cpphdl_last_state_committed=false;\n}\n";
                }
                output << "static constexpr bool __cpphdl_cached_state_transactions = " << (cachedStateTransactions?"true":"false") << ";\n";
                output << "static constexpr size_t __cpphdl_write_groups = " << writeGroups.size() << ";\n"
                       << "static constexpr size_t __cpphdl_shadowed_writes = " << std::count(shadowedWrites.begin(),shadowedWrites.end(),true) << ";\n"
                       << "static constexpr size_t __cpphdl_cached_write_groups = " << cachedWriteGroupCount << ";\n"
                       << "static constexpr bool __cpphdl_cached_transactions = " << (cachedTransactions?"true":"false") << ";\n";
                for (unsigned lane = 0; lane < threads; ++lane) {
                    output << "struct alignas(64) __cpphdl_Transaction_" << lane << " {\nunsigned errors = 0;\n";
                    for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input &&
                            std::find(parallel.portWords[i].begin(),parallel.portWords[i].end(),lane)!=parallel.portWords[i].end())
                        output << "std::array<uint32_t," << (ports[i].bits.size()+31)/32 << "> port" << i << "{};\n";
                    for(size_t group=0;group<writeGroups.size();++group)if(writeGroups[group].lane==lane)
                        output << "bool enable" << group << " = false;\n"
                               << (cachedWriteGroups[group]?"bool invalid_address"+std::to_string(group)+" = false;\n":"")
                               << (commitPayloadGroups[group]?"bool payload_pending"+std::to_string(group)+" = false;\n":"");
                    for (size_t i = 0; i < memoryWrites.size(); ++i) if (parallel.writes[i] == lane&&!shadowedWrites[i])
                        output << "uint64_t address" << i << " = 0;\n"
                               << "std::array<uint64_t," << (memoryWrites[i].data.size()+63)/64 << "> data" << i << "{};\n";
                    for(const auto& group:stateGroups)if(group.lane==lane) {
                        output << "bool state_bank_dirty" << group.chunk << " = false;\n";
                        if(!group.triggers.empty())output << "std::array<uint64_t," << (group.triggers.size()+63)/64 << "> state_keys" << group.chunk << "{};\n";
                    }
                    output << "} " << pending(lane) << ";\n";
                    size_t sinkCount =
                        std::count(parallel.accesses.begin(), parallel.accesses.end(), lane) +
                        std::count(parallel.writes.begin(), parallel.writes.end(), lane) +
                        std::count(parallel.states.begin(), parallel.states.end(), lane);
                    for (const auto& words : parallel.portWords)
                        sinkCount += std::count(words.begin(), words.end(), lane);
                    const bool splitSinks = splitChunks && sinkCount > chunkSize;
                    std::vector<std::pair<std::string, bool>> sinkFunctions;
                    size_t sinkItems = 0;
                    bool sinkOpen = false, sinkCommit = false;
                    auto closeSink = [&] {
                        if (!sinkOpen) return;
                        if (sinkCommit) output << "}\n";
                        const auto& name = sinkFunctions.back().first;
                        output << "}\ntemplate void Model::" << name << "<false>() noexcept;\n"
                               << "template void Model::" << name << "<true>() noexcept;\n";
                        closePart(); sinkOpen = false;
                    };
                    auto emitSink = [&](bool commitOnly, auto&& body) {
                        if (splitSinks) {
                            if (!sinkOpen || sinkItems >= chunkSize || sinkCommit != commitOnly) {
                                closeSink();
                                auto name = "__cpphdl_sinks_" + std::to_string(lane) + "_part_" + std::to_string(sinkFunctions.size());
                                sinkFunctions.emplace_back(name, commitOnly);
                                header << "template<bool Commit> [[gnu::noinline]] void " << name << "() noexcept;\n";
                                auto modelPath = std::filesystem::path(path);
                                part.open(modelPath.parent_path() / (modelPath.stem().string() + ".part." + std::to_string(parts++) + ".cpp"));
                                if (!part) throw std::runtime_error("cannot write native sink part");
                                output.rdbuf(part.rdbuf());
                                output << "#include " << nativeStringLiteral(modelPath.filename().string())
                                       << "\nnamespace cpphdl_native {\ntemplate<bool Commit> void Model::" << name << "() noexcept {\n"
                                       << "auto& tx = " << pending(lane) << ";\n";
                                if (commitOnly) output << "if constexpr(Commit) {\n";
                                sinkOpen = true; sinkCommit = commitOnly; sinkItems = 0;
                            }
                            body(); ++sinkItems;
                        } else {
                            if (commitOnly) output << "if constexpr(Commit) {\n";
                            body();
                            if (commitOnly) output << "}\n";
                        }
                    };
                    if (!splitSinks)
                        output << "template<bool Commit> [[gnu::noinline]] void __cpphdl_sinks_" << lane << "() noexcept {\n"
                               << "auto& tx = " << pending(lane) << ";\ntx.errors = 0;\n";
                    for (size_t i = 0; i < memoryAccesses.size(); ++i) if (parallel.accesses[i] == lane) {
                        const auto& access = memoryAccesses[i];
                        emitSink(false, [&] {
                            output << "if(" << (access.transaction ? "Commit && " + clockEdge(access.clock, access.falling) + " && " : "")
                                   << assembly(access.enabled) << " && " << assembly(access.address) << " >= "
                                   << memories[access.memory].depth << "ull) tx.errors |= 1;\n";
                        });
                    }
                    for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input)
                        for (unsigned offset = 0; offset < ports[i].bits.size(); offset += 32)
                            if (parallel.portWords[i][offset/32] == lane) emitSink(false, [&] {
                                output << "tx.port" << i << '[' << offset/32 << "] = uint32_t("
                                       << assembly(slice(ports[i].bits, offset, std::min<size_t>(32,ports[i].bits.size()-offset))) << ");\n";
                            });
                    for(size_t group=0;group<writeGroups.size();++group)if(writeGroups[group].lane==lane) {
                        const auto& batch=writeGroups[group];
                        emitSink(true, [&] {
                            output << "tx.enable" << group << " = " << clockEdge(batch.clock,batch.falling) << " && " << assembly(batch.enable) << ";\n"
                                   << "if(tx.enable" << group << ") {\n";
                            if(cachedWriteGroups[group])
                                output << "if(__cpphdl_dirty[" << transactionChunks[group] << "].pending[1]) {\n"
                                       << "__cpphdl_dirty[" << transactionChunks[group] << "].pending[1] = false;\n"
                                       << "tx.invalid_address" << group << " = false;\n";
                            if(commitPayloadGroups[group])output << "tx.payload_pending" << group << "=true;\n";
                            for(auto i:batch.writes) {
                                const auto& write=memoryWrites[i];
                                output << "tx.address" << i << " = " << assembly(write.address) << ";\n"
                                       << "if(tx.address" << i << " >= " << memories[write.memory].depth << "ull) "
                                       << (cachedWriteGroups[group]?"tx.invalid_address"+std::to_string(group)+" = true":"tx.errors |= 2") << ";\n";
                                for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                                    output << "tx.data" << i << '[' << offset/64 << "] = "
                                           << assembly(slice(write.data, offset, std::min<size_t>(64,write.data.size()-offset))) << ";\n";
                            }
                            if(cachedWriteGroups[group])
                                output << "}\nif(tx.invalid_address" << group << ") tx.errors |= 2;\n";
                            output << "}\n";
                        });
                        // Account for all ports, not just the shared predicate,
                        // when bounding generated sink function size.
                        if(splitSinks)sinkItems+=batch.writes.size()-1;
                    }
                    auto copyStateWord=[&](size_t word) {
                        output << "__cpphdl_state[!__cpphdl_current].state" << word << " = " << names.at(word) << ";\n";
                    };
                    auto prepareState=[&](size_t i,std::string bankDirty={}) {
                        const auto& state=states[i];
                            output << "const bool trigger" << i << " = (" << assembly(state.trigger) << " && " << clockEdge(state.clock, state.falling) << ")";
                            if (!state.reset.empty()) output << " || " << assembly(state.reset);
                            output << ";\nif(trigger" << i << ") {\n";
                            for (unsigned offset = 0; offset < state.bits.size(); offset += 64) {
                                auto index = owner(state.bits[offset]);
                                auto width = std::min<size_t>(64,state.next.size()-offset);
                                auto next = assembly(slice(state.next,offset,width));
                                if (!state.reset.empty()) next = assembly(state.reset) + " ? " + assembly(slice(state.resetValue,offset,width)) + " : " + next;
                                const auto target="__cpphdl_state[!__cpphdl_current].state"+std::to_string(index);
                                if(writeTracking&&rootRoutes.count(index)) {
                                    auto [first,last]=rootRoutes.at(index);
                                    output << "const uint64_t new_value" << index << " = " << next << ";\n"
                                           << "const uint64_t change" << index << " = uint64_t(" << target << ") ^ new_value" << index << ";\n"
                                           << target << " = new_value" << index << ";\n";
                                    if(!bankDirty.empty())output << "if(change" << index << ") " << bankDirty << "=true;\n";
                                    output << "__cpphdl_notify_pending(__cpphdl_pending_state_" << lane << ","
                                           << first << "," << last << ",change" << index << ");\n";
                                }else output << target << " = " << next << ";\n";
                            }
                            output << "}\n";
                    };
                    if(cachedStateTransactions) {
                        for(const auto& group:stateGroups)if(group.lane==lane) {
                            emitSink(true,[&] {
                                const auto key="state_key"+std::to_string(group.chunk);
                                const auto count=(group.triggers.size()+63)/64;
                                if(count) {
                                    output << "const std::array<uint64_t," << count << "> " << key << "{{";
                                    for(size_t first=0;first<group.triggers.size();first+=64) {
                                        if(first)output << ',';output << "0ull";
                                        for(size_t bit=first;bit<std::min(group.triggers.size(),first+64);++bit)
                                            output << " | (uint64_t(bool(" << assembly(group.triggers[bit]) << ")) << " << bit-first << ')';
                                    }
                                    output << "}};\n";
                                }
                                output << "if(" << (group.alwaysActive?"true":"false");
                                for(size_t word=0;word<count;++word)output << " || " << key << '[' << word << ']';
                                for(const auto& reset:group.resets)output << " || " << assembly(reset);
                                output << ") {\nif(__cpphdl_dirty[" << group.chunk << "].pending[1]";
                                if(count)output << " || tx.state_keys" << group.chunk << " != " << key;
                                output << ") {\n__cpphdl_dirty[" << group.chunk << "].pending[1]=false;\n";
                                if(count)output << "tx.state_keys" << group.chunk << " = " << key << ";\n";
                                const auto bankDirty="tx.state_bank_dirty"+std::to_string(group.chunk);
                                output << "if(" << bankDirty << ") {\n";
                                for(auto word:group.words)copyStateWord(word);
                                output << bankDirty << "=false;\n}\n";
                                for(auto i:group.updates)prepareState(i,bankDirty);
                                output << "}\n} else if(" << bankDirty << ") {\n";
                                // No active update: synchronize only banks with
                                // recorded changes. Keep the active trigger key
                                // and dirty payloads for the next active event.
                                for(auto word:group.words)copyStateWord(word);
                                output << bankDirty << "=false;\n}\n";
                            });
                            if(splitSinks)sinkItems+=group.words.size()+group.updates.size()-1;
                        }
                    } else {
                        // Copy all words before ordered updates, including
                        // priority writes crossing compilation boundaries.
                        std::set<size_t> initialized;
                        for(size_t i=0;i<states.size();++i)if(parallel.states[i]==lane)
                            for(auto bit:states[i].bits)if(initialized.insert(owner(bit)).second)
                                emitSink(true,[&]{copyStateWord(owner(bit));});
                        for(size_t i=0;i<states.size();++i)if(parallel.states[i]==lane)
                            emitSink(true,[&]{prepareState(i);});
                    }
                    if (splitSinks) {
                        closeSink();
                        output << "template<bool Commit> [[gnu::noinline]] void __cpphdl_sinks_" << lane << "() noexcept {\n"
                               << pending(lane) << ".errors = 0;\n";
                        for (const auto& [name, commitOnly] : sinkFunctions) {
                            if (commitOnly) output << "if constexpr(Commit) ";
                            output << name << "<Commit>();\n";
                        }
                    }
                    output << "}\n";
                }
                output << "template<bool Commit> static void __cpphdl_lane(void* target, cpphdl::graph_runtime::Threads&, "
                          "unsigned lane, uint32_t) noexcept {\n"
                          "auto& model = *static_cast<Model*>(target);\nswitch(lane) {\n";
                for (unsigned lane = 0; lane < threads; ++lane) {
                    output << "case " << lane << ":\n";
                    if(memoizeDeltas&&parallelRoots&&!writeTracking)output << "model.__cpphdl_mark_roots_lane" << lane << "();\n";
                    for (auto chunk : taskChunks[lane]) {
                        if (!settleChunks[chunk]) output << "if constexpr(Commit) ";
                        if(guardChunkCalls)output << "if(model.__cpphdl_dirty[" << chunk << "].pending["
                            << (workOnlyChunks.count(chunk)?"Commit":"true") << "]) ";
                        output << "model.__cpphdl_chunk_" << chunk << "<Commit>();\n";
                    }
                    output << "model.__cpphdl_sinks_" << lane << "<Commit>();\nbreak;\n";
                }
                output << "}\n}\nvoid __cpphdl_publish(bool commit = true) {\nunsigned errors = 0;\n";
                for (unsigned lane = 0; lane < (hostOverlap ? 1u : threads); ++lane)
                    output << "errors |= " << pending(lane) << ".errors;\n";
                for (const auto& node : nodes) if (node.validation())
                    output << "if(commit && " << assembly(node.left) << ") throw std::runtime_error("
                           << nativeStringLiteral(node.name) << ");\n";
                output << "if(errors & 1) throw std::out_of_range(\"native graph memory address\");\n"
                          "if(errors & 2) throw std::out_of_range(\"native graph memory write address\");\n";
                for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input)
                    for(size_t word=0;word<parallel.portWords[i].size();++word)
                        output << ports[i].name << '[' << word << "] = " << pending(parallel.portWords[i][word])
                               << ".port" << i << '[' << word << "];\n";
                output << "}\n";
                // Prepared operands are immutable after validation. Commit
                // lanes own disjoint RAMs and preserve their original port
                // ordering, including writes spanning compilation helpers.
                auto emitMemoryCommit = [&](size_t i) {
                    const auto& write = memoryWrites[i]; const auto tx = pending(parallel.writes[i]);
                    const auto target="memory"+std::to_string(write.memory)+'['+tx+".address"+std::to_string(i)+']';
                    if(compareMemoryWrite(write.memory)) {
                        output << "if(";
                        for(unsigned word=0;word<(write.data.size()+63)/64;++word)
                            output << (word?" || ":"") << target << '[' << word << "] != " << tx << ".data" << i << '[' << word << ']';
                        output << ") {\n";
                    }
                    noteMemoryChange(write.memory);
                    for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                        output << target << '[' << offset/64 << "] = " << tx << ".data" << i << '[' << offset/64 << "];\n";
                    if(compareMemoryWrite(write.memory))output << "}\n";
                };
                auto prepareMemoryCommit = [&](size_t memory) {
                    const auto suffix=std::to_string(memory);
                    const std::vector<size_t> groups(memoryWriteGroups[memory].begin(),memoryWriteGroups[memory].end());
                    output << "__cpphdl_memory_dirty" << suffix << " |= false";
                    for(auto group:groups)output << " || " << pending(writeGroups[group].lane) << ".payload_pending" << group;
                    output << ";\nconst std::array<uint64_t," << (groups.size()+63)/64 << "> memory_key" << suffix << "{{";
                    for(size_t first=0;first<groups.size();first+=64) {
                        if(first)output << ',';output << "0ull";
                        for(size_t bit=first;bit<std::min(groups.size(),first+64);++bit)
                            output << " | (uint64_t(" << pending(writeGroups[groups[bit]].lane) << ".enable" << groups[bit] << ") << " << bit-first << ')';
                    }
                    output << "}};\n__cpphdl_memory_apply" << suffix << " = (false";
                    for(size_t word=0;word<(groups.size()+63)/64;++word)output << " || memory_key" << suffix << '[' << word << ']';
                    output << ") && (__cpphdl_memory_dirty" << suffix << " || memory_key" << suffix << " != __cpphdl_memory_key" << suffix << ");\n"
                           << "if(__cpphdl_memory_apply" << suffix << ") { __cpphdl_memory_dirty" << suffix << "=false; __cpphdl_memory_key" << suffix << "=memory_key" << suffix << "; }\n";
                };
                auto emitMemoryCommits = [&](unsigned lane,size_t first,size_t last) {
                    const auto none=std::make_pair(size_t(-1),size_t(-1));
                    auto previous=none;
                    auto close=[&] {if(previous!=none)output << "}\n";previous=none;};
                    for(size_t position=first;position<last;++position) {
                        const auto i=commitWrites[lane][position];
                        const auto memory=memoryWrites[i].memory;
                        const bool cached=cachedMemoryCommits[memory];
                        // Prepare once, before this memory's first store. A
                        // memory can span helper boundaries and interleave with
                        // others; every store retains its original global order.
                        if(cached&&i==memoryWritePorts[memory].front()) {
                            close();prepareMemoryCommit(memory);
                            if(auto clear=commitPayloadClearAt.find(i);clear!=commitPayloadClearAt.end())
                                for(auto group:clear->second)
                                    output << pending(writeGroups[group].lane) << ".payload_pending" << group << "=false;\n";
                        }
                        const auto key=std::make_pair(cached?memory:size_t(-1),writeGroup[i]);
                        if(key!=previous) {
                            close();output << "if(";
                            if(cached)output << "__cpphdl_memory_apply" << memory << " && ";
                            output << pending(parallel.writes[i]) << ".enable" << writeGroup[i] << ") {\n";
                            previous=key;
                        }
                        emitMemoryCommit(i);
                    }
                    close();
                };
                std::vector<std::vector<std::string>> commitFunctions(commitLanes);
                size_t commitFunctionCount=0;
                for(unsigned lane=0;lane<commitLanes;++lane)if (splitChunks && commitWrites[lane].size() > chunkSize) {
                    auto modelPath = std::filesystem::path(path);
                    for (size_t first = 0; first < commitWrites[lane].size(); first += chunkSize) {
                        auto name = "__cpphdl_commit_ram_" + std::to_string(commitFunctionCount++);
                        commitFunctions[lane].push_back(name);
                        header << "[[gnu::noinline]] void " << name << "() noexcept;\n";
                        auto partPath = modelPath.parent_path() /
                            (modelPath.stem().string() + ".part." + std::to_string(parts++) + ".cpp");
                        part.open(partPath);
                        if (!part) throw std::runtime_error("cannot write native commit part");
                        output.rdbuf(part.rdbuf());
                        output << "#include " << nativeStringLiteral(modelPath.filename().string())
                               << "\nnamespace cpphdl_native {\nvoid Model::" << name << "() noexcept {\n";
                        emitMemoryCommits(lane,first,std::min(commitWrites[lane].size(), first + chunkSize));
                        output << "}\n";
                        closePart();
                    }
                }
                auto emitCommitLane=[&](unsigned lane) {
                    if(commitFunctions[lane].empty())emitMemoryCommits(lane,0,commitWrites[lane].size());
                    else for(const auto& name:commitFunctions[lane])output << name << "();\n";
                };
                if(parallelMemoryCommits) {
                    output << "void __cpphdl_commit_memory_lane(unsigned lane) noexcept {\nswitch(lane) {\n";
                    for(unsigned lane=0;lane<commitLanes;++lane) {
                        output << "case " << lane << ": {\n";emitCommitLane(lane);output << "break;\n}\n";
                    }
                    output << "}\n}\nstatic void __cpphdl_commit_lane(void* target, cpphdl::graph_runtime::Threads&, unsigned lane, uint32_t) noexcept {\n"
                              "static_cast<Model*>(target)->__cpphdl_commit_memory_lane(lane);\n}\n";
                }
                output << "void __cpphdl_commit() noexcept {\n";
                if(parallelMemoryCommits) {
                    output << "if(" << (adaptiveDispatch?"__cpphdl_dispatch_policy":"__cpphdl_memory_dispatch_policy")
                           << " == cpphdl::graph_runtime::DispatchPolicy::Serial) {\n"
                           << "for(unsigned lane=0;lane<" << commitLanes << ";++lane) __cpphdl_commit_memory_lane(lane);\n"
                           << "} else __cpphdl_threads->run(this,&Model::__cpphdl_commit_lane," << commitLanes << "u);\n";
                } else emitCommitLane(0);
                if(memoryChangeEvents)output << "__cpphdl_publish_memory_changes();\n";
                output << "__cpphdl_current = !__cpphdl_current;\n";
                if(writeTracking)output << "__cpphdl_publish_root_events();\n";
                if(cachedStateTransactions)output << "__cpphdl_last_state_committed=true;\n";
                for (size_t i = 0; i < clocks.size(); ++i)
                    output << "__cpphdl_previous_clock_" << i << " = " << clocks[i].name << ";\n";
                output << "}\ntemplate<bool Commit> [[gnu::noinline]] void evaluate() {\n__cpphdl_refresh_memory_chunks();\n";
                if(cachedStateTransactions)output << "if constexpr(Commit) __cpphdl_begin_state_transactions();\n";
                if(memoizeDeltas&&(!parallelRoots||writeTracking))output << "__cpphdl_mark_roots();\n";
                if (adaptiveDispatch)
                    output << "for(auto& activity:__cpphdl_activity) activity.nodes = 0;\n"
                              "if(__cpphdl_dispatch_policy == cpphdl::graph_runtime::DispatchPolicy::Serial ||\n"
                              "(__cpphdl_dispatch_policy == cpphdl::graph_runtime::DispatchPolicy::Automatic && __cpphdl_serial_by_phase[Commit]"
                           << (parallelRoots?" && __cpphdl_pending_root_work(Commit) < 4096":"") << ")) {\n"
                              "for(unsigned lane=0;lane<__cpphdl_task_count;++lane)\n"
                              "__cpphdl_lane<Commit>(this,*__cpphdl_threads,lane,0);\n"
                              "} else ";
                output << "__cpphdl_threads->" << (dynamicTasks?"runTasks":"run")
                       << "(this, &Model::__cpphdl_lane<Commit>, __cpphdl_task_count);\n";
                if (adaptiveDispatch)
                    output << "uint64_t active_nodes = 0;\n"
                              "for(const auto& activity:__cpphdl_activity) active_nodes += activity.nodes;\n"
                              "__cpphdl_serial_dispatch = active_nodes < 4096;\n"
                              "__cpphdl_serial_by_phase[Commit] = __cpphdl_serial_dispatch;\n";
                output << "__cpphdl_publish(Commit);\nif constexpr(Commit) __cpphdl_commit();\n}\n";
                if (hostOverlap) {
                    // Host continuation may only consume the published ports
                    // and update external host models. Inputs, graph storage
                    // and this shared executor must not be mutated/reentered.
                    // A host exception still commits the graph transaction,
                    // matching the synchronous evaluate-then-host contract.
                    output << "static constexpr bool __cpphdl_host_overlap = true;\n"
                              "template<class Host> void evaluate_with_host(Host&& host) {\n"
                              "bool published = false;\ntry {\n"
                           << "__cpphdl_refresh_memory_chunks();\n" << (cachedStateTransactions?"__cpphdl_begin_state_transactions();\n":"") << (memoizeDeltas&&(!parallelRoots||writeTracking)?"__cpphdl_mark_roots();\n":"") <<
                              "__cpphdl_threads->run(this, &Model::__cpphdl_lane<true>, __cpphdl_thread_count, [&] {\n"
                              "__cpphdl_publish();\npublished = true;\nhost();\n});\n"
                              "} catch(...) { if(published) __cpphdl_commit(); throw; }\n"
                              "__cpphdl_commit();\n}\n";
                } else output << "template<class Host> void evaluate_with_host(Host&& host) { evaluate<true>(); host(); }\n";
                output << "void eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                       << stepBody << "};\n}\n";
                fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), scheduledNodes, states.size());
                return;
            }
            output << "template<bool Commit> [[gnu::noinline]] void evaluate() {\n__cpphdl_refresh_memory_chunks();\n";
            if(memoizeDeltas)output << "__cpphdl_mark_roots();\n";
            for (unsigned index = 0; index < chunks; ++index) {
                if (!settleChunks[index]) output << "if constexpr(Commit) ";
                if(guardChunkCalls)output << "if(__cpphdl_dirty[" << index << "].pending["
                    << (workOnlyChunks.count(index)?"Commit":"true") << "]) ";
                output << "__cpphdl_chunk_" << index << "<Commit>();\n";
            }
        }
        for (const auto& node : nodes) if (node.validation())
            output << "if(Commit && " << assembly(node.left) << ") throw std::runtime_error("
                   << nativeStringLiteral(node.name) << ");\n";
        for (const auto& access : memoryAccesses)
            output << "if(" << (access.transaction ? "Commit && " + clockEdge(access.clock, access.falling) + " && " : "") << assembly(access.enabled)
                   << " && " << assembly(access.address) << " >= " << memories[access.memory].depth
                   << "ull) throw std::out_of_range(\"native graph memory address\");\n";
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
            if(shadowedWrites[index])continue;
            const auto& write = memoryWrites[index];
            output << "const bool memory_enable" << index << " = " << clockEdge(write.clock, write.falling) << " && " << assembly(write.enabled) << ";\n"
                   << "const uint64_t memory_address" << index << " = " << assembly(write.address) << ";\n";
            output << "if(Commit && memory_enable" << index << " && memory_address" << index << " >= "
                   << memories[write.memory].depth << "ull) throw std::out_of_range(\"native graph memory write address\");\n";
            for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                output << "const uint64_t memory_next" << index << '_' << offset / 64 << " = "
                       << assembly(slice(write.data, offset, std::min<size_t>(64, write.data.size() - offset))) << ";\n";
        }
        for (const auto& port : ports) if (!port.input)
            for (unsigned offset = 0; offset < port.bits.size(); offset += 32)
                output << port.name << '[' << offset/32 << "] = uint32_t(" << assembly(slice(port.bits, offset, std::min<size_t>(32,port.bits.size()-offset))) << ");\n";
        for (size_t index = 0; index < states.size(); ++index) {
            auto& state = states[index];
            output << "const bool trigger" << index << " = (" << assembly(state.trigger) << " && " << clockEdge(state.clock, state.falling) << ")";
            if (!state.reset.empty()) output << " || " << assembly(state.reset);
            output << ";\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64)
                output << "const uint64_t next" << index << '_' << offset << " = "
                       << (state.reset.empty() ? "" : assembly(state.reset) + " ? " + assembly(slice(state.resetValue, offset, std::min<size_t>(64,state.next.size()-offset))) + " : ")
                       << assembly(slice(state.next, offset, std::min<size_t>(64,state.next.size()-offset))) << ";\n";
        }
        output << "if constexpr(Commit) {\n";
        // Snapshot all write operands before any state commit. Ordered writes
        // to one address retain the C++ queue's last-write-wins semantics.
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
            if(shadowedWrites[index])continue;
            const auto& write = memoryWrites[index];
            output << "if(memory_enable" << index << ") {\n";
            if(compareMemoryWrite(write.memory)) {
                output << "if(";
                for(unsigned word=0;word<(write.data.size()+63)/64;++word)
                    output << (word?" || ":"") << "memory" << write.memory << "[memory_address" << index << "][" << word
                           << "] != memory_next" << index << '_' << word;
                output << ") {\n";
            }
            noteMemoryChange(write.memory);
            for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                output << "memory" << write.memory << "[memory_address" << index << "][" << offset / 64
                       << "] = memory_next" << index << '_' << offset / 64 << ";\n";
            if(compareMemoryWrite(write.memory))output << "}\n";
            output << "}\n";
        }
        if(memoryChangeEvents)output << "__cpphdl_publish_memory_changes();\n";
        for (size_t index = 0; index < states.size(); ++index) {
            auto& state = states[index];
            output << "if(trigger" << index << ") {\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64) {
                auto producer=owner(state.bits[offset]);
                if(writeTracking)emitInvalidation(producer,"(uint64_t(state"+std::to_string(producer)+") ^ next"+std::to_string(index)+"_"+std::to_string(offset)+")");
                output << "state" << producer << " = next" << index << '_' << offset << ";\n";
            }
            output << "}\n";
        }
        for (size_t i = 0; i < clocks.size(); ++i)
            output << "__cpphdl_previous_clock_" << i << " = " << clocks[i].name << ";\n";
        output << "}\n}\nvoid eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                  "template<class Host> void evaluate_with_host(Host&& host) { evaluate<true>(); host(); }\n"
               << stepBody << "};\n";
        if (splitChunks)
            for (unsigned chunk = 0; chunk < chunks; ++chunk)
                for (const auto* mode : {"false", "true"})
                    output << "extern template void Model::__cpphdl_chunk_" << chunk << '<' << mode << ">();\n";
        output << "}\n";
        fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), scheduledNodes, states.size());
    }
}

#pragma once

#include "cpphdl_graph.h"
#include <numeric>

namespace cpphdl::graph {
// Native tasks own complete combinational cones. Pure shared logic can be
// duplicated to avoid cross-lane intermediate traffic. Each lane also owns
// its sinks and prepares register updates in an inactive state bank.
struct NativeParallelPlan {
    unsigned lanes = 1, stages = 1;
    uint64_t cost = 0;
    std::vector<std::vector<size_t>> tasks;
    std::vector<std::vector<size_t>> waits;
    std::vector<unsigned> ports, states, accesses, writes;
};

inline NativeParallelPlan nativeParallelPlan(Graph& graph, const std::vector<size_t>& order, unsigned lanes,
                                             bool hostOverlap = false) {
    const size_t count = graph.nodes.size();
    std::vector<size_t> values;
    std::vector<std::vector<size_t>> dependencies(count);
    std::vector<uint64_t> weight(count);
    uint64_t total = 0;
    for (auto index : order) {
        const auto& node = graph.nodes[index];
        if (node.op == "input" || node.op == "state") continue;
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
    struct Root { std::vector<Value*> fields; std::vector<size_t> cone; uint64_t cost = 0; unsigned lane = 0; uint64_t sinkCost = 0; };
    std::vector<Root> roots;
    std::vector<size_t> portRoots, stateRoots, accessRoots, writeRoots;
    for (auto& port : graph.ports) {
        portRoots.push_back(port.input ? size_t(-1) : roots.size());
        if (!port.input) roots.push_back({{&port.bits}});
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
    std::vector<size_t> seen(count, size_t(-1));
    size_t active = 0;
    for (size_t r = 0; r < roots.size(); ++r) {
        auto& root = roots[r]; std::vector<size_t> pending;
        // Account for assembling the sink as well as its combinational cone.
        // Constant/direct-state sinks still do work and must not all land on 0.
        root.sinkCost = 1;
        for (auto* field : root.fields) for (auto raw : *field) {
            auto bit = graph.resolve(raw);
            if (bit > 1 && weight[Graph::owner(bit)]) pending.push_back(Graph::owner(bit));
        }
        for (auto* field : root.fields) root.sinkCost += (field->size() + 63) / 64;
        root.cost = root.sinkCost;
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            if (seen[node] == r) continue;
            seen[node] = r; root.cone.push_back(node); root.cost += weight[node];
            pending.insert(pending.end(), dependencies[node].begin(), dependencies[node].end());
        }
        active += !root.cone.empty();
    }
    lanes = std::min<size_t>(lanes, active);
    if (lanes < 2) { NativeParallelPlan p; p.tasks = {values}; p.waits.resize(1); return p; }
    std::vector<size_t> sorted(roots.size()); std::iota(sorted.begin(), sorted.end(), 0);
    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) { return roots[a].cost > roots[b].cost; });
    std::vector<std::vector<bool>> owned(lanes, std::vector<bool>(count));
    std::vector<uint64_t> load(lanes);
    std::set<size_t> callerRoots;
    if (hostOverlap) {
        for (auto root : portRoots) if (root != size_t(-1)) callerRoots.insert(root);
        callerRoots.insert(accessRoots.begin(), accessRoots.end());
        callerRoots.insert(writeRoots.begin(), writeRoots.end());
        // The caller owns every observable output and every validation that
        // can fail. Its host continuation is safe before the workers finish:
        // they only prepare inactive register storage, never observable state.
        for (auto r : callerRoots) {
            load[0] += roots[r].sinkCost;
            for (auto node : roots[r].cone) if (!owned[0][node]) {
                owned[0][node] = true; load[0] += weight[node];
            }
        }
    }
    // Group large overlapping cones first. Balance the accumulated lane cost
    // and penalize extra work, rather than balancing unrelated depth slices.
    for (auto r : sorted) {
        if (callerRoots.count(r)) continue;
        auto& root = roots[r]; uint64_t bestScore = UINT64_MAX, bestAdded = 0;
        const auto peak = *std::max_element(load.begin(), load.end());
        for (unsigned lane = hostOverlap ? 1 : 0; lane < lanes; ++lane) {
            uint64_t added = root.sinkCost; for (auto node : root.cone) if (!owned[lane][node]) added += weight[node];
            const auto score = std::max(peak, load[lane] + added) * lanes * 4 + added;
            if (score < bestScore) { bestScore = score; bestAdded = added; root.lane = lane; }
        }
        load[root.lane] += bestAdded;
        for (auto node : root.cone) owned[root.lane][node] = true;
    }
    NativeParallelPlan plan; plan.lanes = lanes; plan.stages = 1; plan.tasks.resize(lanes); plan.waits.resize(lanes);
    for (auto root : portRoots) plan.ports.push_back(root == size_t(-1) ? 0 : roots[root].lane);
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
    plan.cost = *std::max_element(load.begin(), load.end());
    fprintf(stderr, "native graph cones: base weight %llu; lane weights", (unsigned long long)total);
    for(auto x:load) fprintf(stderr," %llu",(unsigned long long)x); fprintf(stderr,"\n");
    return plan;
}

inline void Graph::emit(const std::string& path, unsigned chunkSize, unsigned threads, bool hostOverlap) {
        if (!threads || threads > 256) throw std::runtime_error("native graph threads must be between 1 and 256");
        if (threads > 1 && !chunkSize) chunkSize = 512;
        validateClocks();
        if (chunkSize)
            for (const auto& node : nodes) if (node.hostEffect())
                throw std::runtime_error("chunked native evaluation requires explicit host boundaries");
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
        // Reject state-derived clocks/resets instead of silently losing deltas.
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
        auto scheduledNodes = order.size();
        NativeParallelPlan parallel;
        std::vector<std::vector<size_t>> taskNodes(1);
        if (threads > 1) {
            parallel = nativeParallelPlan(*this, order, threads, hostOverlap);
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
        std::vector<int64_t> chunkOf(nodes.size(), -1);
        std::vector<std::vector<size_t>> taskChunks(taskNodes.size());
        if (chunkSize) {
            size_t chunk = 0;
            for (size_t task = 0; task < taskNodes.size(); ++task) {
                for (size_t i = 0; i < taskNodes[task].size(); ++i) {
                    if (i % chunkSize == 0) taskChunks[task].push_back(chunk++);
                    chunkOf[taskNodes[task][i]] = chunk - 1;
                }
            }
        }
        if (threads > 1) {
            order.clear();
            for (const auto& task : taskNodes) order.insert(order.end(), task.begin(), task.end());
            fprintf(stderr, "native graph parallel: %u lanes, %u stages, %zu tasks\n",
                    threads, parallel.stages, parallel.tasks.size());
        }
        std::ofstream output(path);
        if (!output) throw std::runtime_error("cannot write native model");
        output << "#pragma once\n";
        if (threads > 1) output << "#include <cpphdl_graph_threads.h>\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_random"; }))
            output << "#include <cstdlib>\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_jtag_tick"; }))
            output << "extern \"C\" int jtag_tick(unsigned char*, unsigned char*, unsigned char*, unsigned char*, unsigned char);\n";
        if (std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.op == "host_debug_tick"; }))
            output << "extern \"C\" int debug_tick(unsigned char*, unsigned char, int*, int*, int*, unsigned char, unsigned char*, int, int);\n";
        output << "#include <array>\n#include <cstdint>\n#include <stdexcept>\n#include <vector>\n"
                  "namespace cpphdl_native {\nstruct Model {\n";
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
                        if (nodes[index].op == "state" && stateOwner[index] == lane) {
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
                if (nodes[index].op == "state" && stateStorageWidth(nodes[index].width) == storageWidth) {
                    auto field = "state" + std::to_string(index);
                    names[index] = "uint64_t(" + field + ")";
                    output << "uint" << storageWidth << "_t " << field << " = "
                           << number(nodes[index].left).value_or(0) << "ull;\n";
                }
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
            output << "static constexpr unsigned __cpphdl_thread_count = " << threads << ";\n"
                   << "std::shared_ptr<cpphdl::graph_runtime::Threads> __cpphdl_threads = "
                      "std::make_shared<cpphdl::graph_runtime::Threads>("
                   << threads << ',' << taskNodes.size() << ");\n";
        auto valueName = [&](size_t index) {
            auto slot = sharedSlots.find(index);
            return slot != sharedSlots.end() ? "__cpphdl_values[" + std::to_string(slot->second) + "]" : "value" + std::to_string(index);
        };
        auto declaration = [&](size_t index) {
            return sharedSlots.count(index) ? "" : "const uint64_t ";
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
                while (offset + count < value.size() && value[offset+count] == bit+count && first+count < nodes[index].width) ++count;
                std::string term = names.count(index) ? names.at(index) : valueName(index);
                if (first) term = "(" + term + " >> " + std::to_string(first) + ")";
                if (count < 64) term = "(" + term + " & " + std::to_string(mask(count)) + "ull)";
                if (offset) term = "(" + term + " << " + std::to_string(offset) + ")";
                if (!result.empty()) result += " | "; result += term; offset += count;
            }
            if (constants || result.empty()) { if (!result.empty()) result += " | "; result += std::to_string(constants) + "ull"; }
            return "(" + result + ")";
        };
        auto clockEdge = [&](int clock, bool falling) {
            if (clock < 0) return std::string("true");
            auto previous = "__cpphdl_previous_clock_" + std::to_string(clock);
            auto current = clocks.at(clock).name;
            return "(" + (falling ? "!" + current + " && " + previous : current + " && !" + previous) + ")";
        };
        // Output settling does not need next-state logic. Make the two phases
        // compile-time distinct so the host compiler can remove that work,
        // rather than testing a runtime flag after computing both schedules.
        unsigned chunks = 0;
        int64_t previousChunk = -1;
        if (!chunkSize) output << "template<bool Commit> void evaluate() {\n";
        for (auto index : order) {
            const auto& node = nodes[index];
            if (node.op == "state" || node.op == "input") continue;
            if (chunkSize && chunkOf[index] != previousChunk) {
                if (chunks) output << "}\n";
                output << "[[gnu::noinline]] void __cpphdl_chunk_" << chunks++ << "() {\n";
                previousChunk = chunkOf[index];
            }
            if (node.op == "memory_read") {
                auto memory = number(node.right), word = number(node.select);
                if (!memory || *memory >= memories.size() || !word || *word >= (memories[*memory].width + 63) / 64 ||
                    node.left.empty() || node.left.size() > 64 || node.width != std::min<uint64_t>(64, memories[*memory].width - *word * 64))
                    throw std::runtime_error("invalid graph memory read");
                auto address = assembly(node.left);
                output << declaration(index) << valueName(index) << " = " << address << " < " << memories[*memory].depth
                       << "ull ? memory" << *memory << '[' << address << "][" << *word << "] : 0ull;\n";
                continue;
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
                    output << type << ' ' << prefix << '_' << argument << " = static_cast<"
                           << type << ">(" << assembly(slice(node.right, offset, sizes[argument])) << ");\n";
                    offset += sizes[argument];
                }
                output << "uint64_t value" << index << " = 0;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n"
                       << "value" << index << " = uint32_t(::debug_tick(";
                for (unsigned argument = 0; argument < 9; ++argument) {
                    if (argument) output << ", ";
                    if (pointers[argument]) output << '&';
                    output << prefix << '_' << argument;
                }
                output << "));\n} }\n";
                continue;
            }
            if (node.op == "host_debug_output") {
                auto argument = number(node.select);
                if (node.left.size() != 1 || node.left.front() < 2 || !argument ||
                    (*argument != 0 && *argument != 2 && *argument != 3 && *argument != 4 && *argument != 6) ||
                    nodes[owner(node.left.front())].op != "host_debug_tick" ||
                    node.width != (*argument == 0 || *argument == 6 ? 8u : 32u))
                    throw std::runtime_error("invalid debug_tick output projection");
                output << "const uint64_t value" << index << " = uint32_t(debug"
                       << owner(node.left.front()) << '_' << *argument << ");\n";
                continue;
            }
            auto left = assembly(node.left), right = assembly(node.right);
            if (node.op == "host_jtag_tick") {
                if (node.width != 64 || node.right.size() != 40 || node.select.size() != 1)
                    throw std::runtime_error("invalid jtag_tick effect layout");
                auto prefix = "jtag" + std::to_string(index);
                output << "uint64_t value" << index << " = (" << right << " & 4294967295ull) << 32;\n"
                       << "if constexpr(Commit) { if(" << assembly(node.select) << ") {\n";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << "unsigned char " << prefix << '_' << argument << " = static_cast<unsigned char>("
                           << assembly(slice(node.right, 8 * argument, 8)) << ");\n";
                output << "const int " << prefix << "_result = ::jtag_tick(";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << '&' << prefix << '_' << argument << ", ";
                output << "static_cast<unsigned char>(" << assembly(slice(node.right, 32, 8)) << "));\n"
                       << "value" << index << " = uint64_t(uint32_t(" << prefix << "_result))";
                for (unsigned argument = 0; argument < 4; ++argument)
                    output << " | (uint64_t(" << prefix << '_' << argument << ") << " << 32 + 8 * argument << ')';
                output << ";\n} }\n";
                continue;
            }
            std::string expr;
            auto op = node.op;
            if (op == "host_random") expr = "Commit && " + assembly(node.select) + " ? uint64_t(::random()) : 0ull";
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
        }
        if (chunkSize) {
            if (chunks) output << "}\n";
            if (threads > 1) {
                // Endpoints are assembled on the same lane as their logic.
                // Only compact host outputs, validation flags and RAM write
                // transactions cross back to the caller. Scratch and inactive
                // registers may be discarded if validation fails.
                auto pending = [](unsigned lane) { return "__cpphdl_pending_" + std::to_string(lane); };
                for (unsigned lane = 0; lane < threads; ++lane) {
                    output << "struct alignas(64) __cpphdl_Transaction_" << lane << " {\nunsigned errors = 0;\n";
                    for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input && parallel.ports[i] == lane)
                        output << "std::array<uint32_t," << (ports[i].bits.size()+31)/32 << "> port" << i << "{};\n";
                    for (size_t i = 0; i < memoryWrites.size(); ++i) if (parallel.writes[i] == lane)
                        output << "bool enable" << i << " = false;\nuint64_t address" << i << " = 0;\n"
                               << "std::array<uint64_t," << (memoryWrites[i].data.size()+63)/64 << "> data" << i << "{};\n";
                    output << "} " << pending(lane) << ";\n";
                    output << "template<bool Commit> [[gnu::noinline]] void __cpphdl_sinks_" << lane << "() noexcept {\n"
                           << "auto& tx = " << pending(lane) << ";\ntx.errors = 0;\n";
                    for (size_t i = 0; i < memoryAccesses.size(); ++i) if (parallel.accesses[i] == lane) {
                        const auto& access = memoryAccesses[i];
                        output << "if(" << (access.transaction ? "Commit && " + clockEdge(access.clock, access.falling) + " && " : "")
                               << assembly(access.enabled) << " && " << assembly(access.address) << " >= "
                               << memories[access.memory].depth << "ull) tx.errors |= 1;\n";
                    }
                    for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input && parallel.ports[i] == lane)
                        for (unsigned offset = 0; offset < ports[i].bits.size(); offset += 32)
                            output << "tx.port" << i << '[' << offset/32 << "] = uint32_t("
                                   << assembly(slice(ports[i].bits, offset, std::min<size_t>(32,ports[i].bits.size()-offset))) << ");\n";
                    output << "if constexpr(Commit) {\n";
                    for (size_t i = 0; i < memoryWrites.size(); ++i) if (parallel.writes[i] == lane) {
                        const auto& write = memoryWrites[i];
                        output << "tx.enable" << i << " = " << clockEdge(write.clock, write.falling) << " && " << assembly(write.enabled) << ";\n"
                               << "if(tx.enable" << i << ") {\ntx.address" << i << " = " << assembly(write.address) << ";\n"
                               << "if(tx.address" << i << " >= " << memories[write.memory].depth << "ull) tx.errors |= 2;\n";
                        for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                            output << "tx.data" << i << '[' << offset/64 << "] = "
                                   << assembly(slice(write.data, offset, std::min<size_t>(64,write.data.size()-offset))) << ";\n";
                        output << "}\n";
                    }
                    std::set<size_t> initialized;
                    for (size_t i = 0; i < states.size(); ++i) if (parallel.states[i] == lane)
                        for (auto bit : states[i].bits) if (initialized.insert(owner(bit)).second)
                            output << "__cpphdl_state[!__cpphdl_current].state" << owner(bit) << " = " << names.at(owner(bit)) << ";\n";
                    for (size_t i = 0; i < states.size(); ++i) if (parallel.states[i] == lane) {
                        const auto& state = states[i];
                        output << "const bool trigger" << i << " = (" << assembly(state.trigger) << " && " << clockEdge(state.clock, state.falling) << ")";
                        if (!state.reset.empty()) output << " || " << assembly(state.reset);
                        output << ";\nif(trigger" << i << ") {\n";
                        for (unsigned offset = 0; offset < state.bits.size(); offset += 64) {
                            auto index = owner(state.bits[offset]);
                            auto width = std::min<size_t>(64,state.next.size()-offset);
                            auto next = assembly(slice(state.next,offset,width));
                            if (!state.reset.empty()) next = assembly(state.reset) + " ? " + assembly(slice(state.resetValue,offset,width)) + " : " + next;
                            output << "__cpphdl_state[!__cpphdl_current].state" << index << " = " << next << ";\n";
                        }
                        output << "}\n";
                    }
                    output << "}\n}\n";
                }
                output << "template<bool Commit> static void __cpphdl_lane(void* target, cpphdl::graph_runtime::Threads&, "
                          "unsigned lane, uint32_t) noexcept {\n"
                          "auto& model = *static_cast<Model*>(target);\nswitch(lane) {\n";
                for (unsigned lane = 0; lane < threads; ++lane) {
                    output << "case " << lane << ":\n";
                    for (auto chunk : taskChunks[lane]) output << "model.__cpphdl_chunk_" << chunk << "();\n";
                    output << "model.__cpphdl_sinks_" << lane << "<Commit>();\nbreak;\n";
                }
                output << "}\n}\nvoid __cpphdl_publish() {\nunsigned errors = 0;\n";
                for (unsigned lane = 0; lane < (hostOverlap ? 1u : threads); ++lane)
                    output << "errors |= " << pending(lane) << ".errors;\n";
                output << "if(errors & 1) throw std::out_of_range(\"native graph memory address\");\n"
                          "if(errors & 2) throw std::out_of_range(\"native graph memory write address\");\n";
                for (size_t i = 0; i < ports.size(); ++i) if (!ports[i].input)
                    output << ports[i].name << " = " << pending(parallel.ports[i]) << ".port" << i << ";\n";
                output << "}\nvoid __cpphdl_commit() noexcept {\n";
                // Preserve global write order; workers only read RAM.
                for (size_t i = 0; i < memoryWrites.size(); ++i) {
                    const auto& write = memoryWrites[i]; const auto tx = pending(parallel.writes[i]);
                    output << "if(" << tx << ".enable" << i << ") {\n";
                    for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                        output << "memory" << write.memory << '[' << tx << ".address" << i << "][" << offset/64
                               << "] = " << tx << ".data" << i << '[' << offset/64 << "];\n";
                    output << "}\n";
                }
                output << "__cpphdl_current = !__cpphdl_current;\n";
                for (size_t i = 0; i < clocks.size(); ++i)
                    output << "__cpphdl_previous_clock_" << i << " = " << clocks[i].name << ";\n";
                output << "}\ntemplate<bool Commit> [[gnu::noinline]] void evaluate() {\n"
                          "__cpphdl_threads->run(this, &Model::__cpphdl_lane<Commit>);\n"
                          "__cpphdl_publish();\nif constexpr(Commit) __cpphdl_commit();\n}\n";
                if (hostOverlap) {
                    // Host continuation may only consume the published ports
                    // and update external host models. Inputs, graph storage
                    // and this shared executor must not be mutated/reentered.
                    // A host exception still commits the graph transaction,
                    // matching the synchronous evaluate-then-host contract.
                    output << "static constexpr bool __cpphdl_host_overlap = true;\n"
                              "template<class Host> void evaluate_with_host(Host&& host) {\n"
                              "bool published = false;\ntry {\n"
                              "__cpphdl_threads->run(this, &Model::__cpphdl_lane<true>, [&] {\n"
                              "__cpphdl_publish();\npublished = true;\nhost();\n});\n"
                              "} catch(...) { if(published) __cpphdl_commit(); throw; }\n"
                              "__cpphdl_commit();\n}\n";
                } else output << "template<class Host> void evaluate_with_host(Host&& host) { evaluate<true>(); host(); }\n";
                output << "void eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                          "void step() { evaluate<true>(); evaluate<false>(); }\n};\n}\n";
                fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), scheduledNodes, states.size());
                return;
            }
            output << "template<bool Commit> [[gnu::noinline]] void evaluate() {\n";
            for (unsigned index = 0; index < chunks; ++index)
                output << "__cpphdl_chunk_" << index << "();\n";
        }
        for (const auto& access : memoryAccesses)
            output << "if(" << (access.transaction ? "Commit && " + clockEdge(access.clock, access.falling) + " && " : "") << assembly(access.enabled)
                   << " && " << assembly(access.address) << " >= " << memories[access.memory].depth
                   << "ull) throw std::out_of_range(\"native graph memory address\");\n";
        for (size_t index = 0; index < memoryWrites.size(); ++index) {
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
            const auto& write = memoryWrites[index];
            output << "if(memory_enable" << index << ") {\n";
            for (unsigned offset = 0; offset < write.data.size(); offset += 64)
                output << "memory" << write.memory << "[memory_address" << index << "][" << offset / 64
                       << "] = memory_next" << index << '_' << offset / 64 << ";\n";
            output << "}\n";
        }
        for (size_t index = 0; index < states.size(); ++index) {
            auto& state = states[index];
            output << "if(trigger" << index << ") {\n";
            for (unsigned offset = 0; offset < state.bits.size(); offset += 64)
                output << "state" << owner(state.bits[offset]) << " = next" << index << '_' << offset << ";\n";
            output << "}\n";
        }
        for (size_t i = 0; i < clocks.size(); ++i)
            output << "__cpphdl_previous_clock_" << i << " = " << clocks[i].name << ";\n";
        output << "}\n}\nvoid eval(bool commit = false) { if(commit) evaluate<true>(); else evaluate<false>(); }\n"
                  "template<class Host> void evaluate_with_host(Host&& host) { evaluate<true>(); host(); }\n"
                  "void step() { evaluate<true>(); evaluate<false>(); }\n};\n}\n";
        fprintf(stderr, "native graph: %zu source nodes, %zu scheduled nodes, %zu state groups\n", nodes.size(), scheduledNodes, states.size());
    }
}

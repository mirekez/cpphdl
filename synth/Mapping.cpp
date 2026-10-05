#include "Mapping.h"
#include "KeepBoxes.h"
#include <tuple>

namespace cpphdl::synth {
using namespace graph;
namespace {
class Mapper {
    Graph source, out;
    KeepBoxes boxes;
    bool partial = false;
    std::set<size_t> expanded;
    std::map<Bit, Bit> mapped;
    std::map<std::tuple<std::string, std::string, Bit, Bit, Bit>, Bit> common;
    std::vector<std::vector<Value>> memories;
    Bit gate(const std::string& op, Bit a, Bit b = 0, Bit s = 0) {
        if (op != "mux" && a > b) std::swap(a, b);
        if (op == "and") { if (!a || !b) return 0; if (a == 1) return b; if (b == 1 || a == b) return a; }
        if (op == "or") { if (a == 1 || b == 1) return 1; if (!a) return b; if (!b || a == b) return a; }
        if (op == "xor") { if (a == b) return 0; if (!a) return b; if (!b) return a; }
        if (op == "mux") { if (a == b) return a; if (s < 2) return s ? a : b; if (a == 1 && b == 0) return s; }
        auto key = std::make_tuple(out.currentScope, op, a, b, s);
        if (auto found = common.find(key); found != common.end()) return found->second;
        auto value = out.add(op, 1, {a}, {b}, op == "mux" ? Value{s} : Value{}).front();
        return common[key] = value;
    }
    Bit invert(Bit b) { return gate("xor", b, 1); }
    Value choose(Bit select, Value a, Value b) {
        if (a.size() != b.size()) throw std::runtime_error("mapper mux width mismatch");
        for (size_t i = 0; i < a.size(); ++i) a[i] = gate("mux", a[i], b[i], select);
        return a;
    }
    Value add(Value a, Value b, bool subtract = false) {
        Bit carry = subtract;
        for (size_t i = 0; i < a.size(); ++i) {
            Bit rhs = subtract ? invert(b[i]) : b[i];
            Bit x = gate("xor", a[i], rhs);
            Bit next = gate("or", gate("and", a[i], rhs), gate("and", x, carry));
            a[i] = gate("xor", x, carry); carry = next;
        }
        return a;
    }
    Bit less(Value a, Value b, bool sign = false) {
        Bit result = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            if (sign && i + 1 == a.size()) { a[i] = invert(a[i]); b[i] = invert(b[i]); }
            result = gate("mux", result, b[i], invert(gate("xor", a[i], b[i])));
        }
        return result;
    }
    Bit equal(Value a, Value b) {
        Bit result = 1;
        for (size_t i = 0; i < a.size(); ++i) result = gate("and", result, invert(gate("xor", a[i], b[i])));
        return result;
    }
    Value shift(Value a, Value amount, bool left, bool arithmetic, unsigned width) {
        a = resize(a, std::max<size_t>(a.size(), width), arithmetic);
        const Bit fill = arithmetic ? a.back() : 0;
        for (size_t stage = 0; stage < amount.size(); ++stage) {
            Value shifted(a.size(), fill);
            if (stage < 63 && (uint64_t(1) << stage) < a.size()) {
                auto distance = uint64_t(1) << stage;
                for (size_t i = 0; i < a.size(); ++i)
                    if (left ? i >= distance : i + distance < a.size()) shifted[i] = a[left ? i - distance : i + distance];
            }
            a = choose(amount[stage], shifted, a);
        }
        return resize(a, width);
    }
    Value divide(Value a, Value b, bool sign, bool remainder) {
        const auto width = a.size();
        auto neg = [&](Value v) { return add(Value(width, 0), v, true); };
        Bit sa = sign ? a.back() : 0, sb = sign ? b.back() : 0;
        if (sign) { a = choose(sa, neg(a), a); b = choose(sb, neg(b), b); }
        Value rem(width + 1, 0), quotient(width, 0), divisor = resize(b, width + 1);
        for (size_t i = width; i-- > 0;) {
            rem.insert(rem.begin(), a[i]); rem.resize(width + 1);
            Bit fits = invert(less(rem, divisor));
            rem = choose(fits, add(rem, divisor, true), rem); quotient[i] = fits;
        }
        Value result = remainder ? resize(rem, width) : quotient;
        if (sign) result = choose(remainder ? sa : gate("xor", sa, sb), neg(result), result);
        return choose(equal(b, Value(width, 0)), Value(width, 0), result);
    }
    Value value(const Value& input) {
        Value result;
        for (auto bit : source.resolved(input)) {
            if (bit < 2) result.push_back(bit);
            else {
                if (!mapped.count(bit)) throw std::runtime_error("gate mapper encountered unordered input");
                result.push_back(mapped.at(bit));
            }
        }
        return result;
    }
    void bind(size_t index, const Value& result) {
        for (size_t i = 0; i < result.size(); ++i) mapped[(index + 1) * 64 + 2 + i] = result[i];
    }
public:
    explicit Mapper(Graph g) : source(std::move(g)), boxes(source) {}
    Mapper(Graph g, const std::set<size_t>& operations)
        : source(std::move(g)), boxes(source), partial(true), expanded(operations) {}
    Graph run() {
        source.validateClocks();
        out.clockContract = source.clockContract; out.clocks = source.clocks; out.attributes = source.attributes;
        auto order = source.dependencyOrder();
        for (size_t n = 0; n < source.nodes.size(); ++n) {
            const auto& node = source.nodes[n];
            if (node.op == "input" || node.op == "state") {
                out.currentScope = node.scope;
                auto bits = out.wire(node.width, node.name, node.op);
                out.nodes.back().left = node.left;
                bind(n, bits);
            }
        }
        if (partial) out.memories = source.memories;
        if (!partial) for (const auto& memory : source.memories) {
            if (memory.depth > 1048576 || memory.width * memory.depth > 16777216)
                throw std::runtime_error("generic gate memory exceeds 16M bits; use an explicit technology memory box");
            memories.emplace_back();
            for (uint64_t i = 0; i < memory.depth; ++i)
                memories.back().push_back(out.wire(memory.width, memory.name + ".word" + std::to_string(i), "state"));
        }
        for (size_t n : order) {
            const auto node = source.nodes[n];
            if (node.op == "input" || node.op == "state") continue;
            out.currentScope = node.scope;
            auto a = value(node.left), b = value(node.right), s = value(node.select);
            const auto& op = node.op;
            unsigned width = node.width;
            Value result;
            if (op == "blackbox" || boxes.owner.count(n) || (partial && !expanded.count(n))) result = out.add(op, width, a, b, s, node.name);
            else if (op == "memory_read") {
                auto memory = number(node.right), part = number(node.select);
                if (!memory || !part || *memory >= memories.size()) throw std::runtime_error("invalid mapped memory read");
                result = Value(width, 0);
                for (size_t row = 0; row < memories[*memory].size(); ++row) {
                    if (a.size() < 64 && row >= (uint64_t(1) << a.size())) break;
                    result = choose(equal(a, constant(row, a.size())), slice(memories[*memory][row], *part * 64, width), result);
                }
            } else if (op == "mux") result = choose(s.at(0), resize(a, width), resize(b, width));
            else if (op == "any" || op == "all" || op == "parity") {
                Bit bit = op == "all";
                for (auto x : a) bit = gate(op == "all" ? "and" : op == "any" ? "or" : "xor", bit, x);
                result = {bit};
            } else if (op == "shl" || op == "shr" || op == "sar") result = shift(a, b, op == "shl", op == "sar", width);
            else if (op == "eq" || op == "lt" || op == "slt") {
                unsigned w = std::max(a.size(), b.size());
                a = resize(a, w, op == "slt"); b = resize(b, w, op == "slt");
                result = {op == "eq" ? equal(a, b) : less(a, b, op == "slt")};
            } else if (op == "div" || op == "mod" || op == "sdiv" || op == "smod") {
                unsigned operandWidth = std::max<size_t>(width, std::max(a.size(), b.size()));
                bool sign = op[0] == 's';
                result = divide(resize(a, operandWidth, sign), resize(b, operandWidth, sign), sign,
                                op == "mod" || op == "smod");
            } else {
                a = resize(a, width, op == "sdiv" || op == "smod"); b = resize(b, width, op == "sdiv" || op == "smod");
                if (op == "add" || op == "sub") result = add(a, b, op == "sub");
                else if (op == "and" || op == "or" || op == "xor") {
                    for (unsigned i = 0; i < width; ++i) result.push_back(gate(op, a[i], b[i]));
                } else if (op == "mul") {
                    result = Value(width, 0);
                    for (unsigned i = 0; i < width; ++i) {
                        Value row(width, 0);
                        for (unsigned j = i; j < width; ++j) row[j] = gate("and", a[j - i], b[i]);
                        result = add(result, row);
                    }
                } else throw std::runtime_error("unsupported gate mapping operation: " + op);
            }
            bind(n, resize(result, width));
        }
        for (const auto& port : source.ports) out.ports.push_back({port.name, value(port.bits), port.input});
        for (const auto& state : source.states)
            out.states.push_back({value(state.bits), value(state.next), value(state.trigger), state.clock, state.falling,
                                  value(state.reset), value(state.resetValue)});
        if (partial) {
            for (const auto& write : source.memoryWrites)
                out.memoryWrites.push_back({write.memory, value(write.address), value(write.data), value(write.enabled), write.clock, write.falling});
            for (const auto& access : source.memoryAccesses)
                out.memoryAccesses.push_back({access.memory, value(access.address), value(access.enabled), access.transaction, access.clock, access.falling});
        }
        for (size_t m = 0; m < memories.size(); ++m) for (size_t row = 0; row < memories[m].size(); ++row) {
            auto next = memories[m][row]; int clock = -1; bool falling = false;
            for (const auto& write : source.memoryWrites) if (write.memory == m) {
                clock = write.clock; falling = write.falling;
                auto address = value(write.address);
                if (address.size() < 64 && row >= (uint64_t(1) << address.size())) continue;
                Bit enable = gate("and", value(write.enabled).at(0), equal(address, constant(row, address.size())));
                next = choose(enable, value(write.data), next);
            }
            if (source.clockContract == ClockContract::NamedEdges && clock < 0) clock = 0;
            out.states.push_back({memories[m][row], next, {1}, clock, falling});
        }
        for (auto pipeline : source.pipelines) {
            for (auto& [name,bits] : pipeline.pins) bits = value(bits);
            for (auto& bits : pipeline.registers) bits = value(bits);
            out.pipelines.push_back(std::move(pipeline));
        }
        return std::move(out);
    }
};
}
Graph mapGates(Graph graph) { graph.optimize(); return Mapper(std::move(graph)).run(); }
Graph expandGates(Graph graph, const std::set<size_t>& operations) { return Mapper(std::move(graph), operations).run(); }

void writeGateReport(Graph& graph, const std::string& path, const std::string& module) {
    std::ofstream out(path);
    auto bits = [&](Value value) {
        out << '['; bool comma = false;
        for (auto bit : graph.resolved(value)) { out << (comma ? "," : "") << bit; comma = true; }
        out << ']';
    };
    out << "{\"modules\":{" << std::quoted(module) << ":{\"ports\":{";
    bool comma = false;
    for (size_t c = 0; c < std::max(size_t(1), graph.clocks.size()); ++c) {
        out << (comma ? "," : "") << std::quoted(graph.clocks.empty() ? "clk" : graph.clocks[c].name)
            << ":{\"bits\":[" << (uint64_t(1) << 63) + c << "]}"; comma = true;
    }
    for (const auto& p : graph.ports) {
        out << ',' << std::quoted(p.name) << ":{\"bits\":"; bits(p.bits); out << '}';
    }
    out << "},\"cells\":{"; comma = false;
    KeepBoxes boxes(graph);
    for (auto n : graph.dependencyOrder()) {
        const auto& node = graph.nodes[n];
        if (node.op == "input" || node.op == "state" || boxes.owner.count(n)) continue;
        if (node.op == "blackbox") {
            Value result;
            for (unsigned bit = 0; bit < node.width; ++bit) result.push_back((n+1)*64+2+bit);
            out << (comma ? "," : "") << std::quoted("g" + std::to_string(n))
                << ":{\"type\":" << std::quoted(node.name) << ",\"parameters\":{\"INPUT_BITS\":"
                << node.left.size() << ",\"OUTPUT_BITS\":" << node.width << "},\"connections\":{\"args\":";
            bits(node.left); out << ",\"result\":"; bits(result); out << "}}";
            comma = true;
            continue;
        }
        std::string kind = node.op;
        for (auto& c : kind) c = std::toupper(static_cast<unsigned char>(c));
        out << (comma ? "," : "") << std::quoted("g" + std::to_string(n)) << ":{\"type\":"
            << std::quoted("$_" + kind + "_") << ",\"connections\":{\"A\":";
        bits(node.left); out << ",\"B\":"; bits(node.right);
        if (!node.select.empty()) { out << ",\"S\":"; bits(node.select); }
        out << ",\"Y\":"; bits({(n+1)*64+2}); out << "}}"; comma = true;
    }
    for (size_t n = 0; n < boxes.boxes.size(); ++n) if (!boxes.boxes[n].outputs.empty()) {
        out << (comma ? "," : "") << std::quoted("box" + std::to_string(n)) << ":{\"type\":" << std::quoted(boxes.boxes[n].module) << ",\"connections\":{\"box_in\":";
        bits(boxes.boxes[n].inputs); out << ",\"box_out\":"; bits(boxes.boxes[n].outputs); out << "}}"; comma = true;
    }
    size_t serial = 0;
    for (const auto& state : graph.states) for (size_t i = 0; i < state.bits.size(); ++i) {
        out << (comma ? "," : "") << std::quoted("ff" + std::to_string(serial++)) << ":{\"type\":\"$_DFF_"
            << (state.falling ? "N" : "P") << "_\",\"connections\":{\"C\":[" << (uint64_t(1) << 63) + std::max(0, state.clock) << ']';
        if (!state.reset.empty()) { out << ",\"R\":"; bits(state.reset); }
        out << ",\"D\":"; bits({state.next[i]}); out << ",\"Q\":"; bits({state.bits[i]}); out << "}}"; comma = true;
    }
    out << "}}}}\n";
    if (!out) throw std::runtime_error("cannot write gate mapping report");
}
}

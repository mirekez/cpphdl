#include "ScheduledGraph.h"
#include "StreamPipeline.h"
#include <memory>

namespace cpphdl::synth {
using namespace graph;
namespace {
struct Expression {
    std::string op;
    std::vector<std::shared_ptr<Expression>> args;
};
using Expr = std::shared_ptr<Expression>;
Expr expr(std::string op, std::vector<Expr> args = {}) { return std::make_shared<Expression>(Expression{std::move(op), std::move(args)}); }
struct Tokens {
    std::vector<std::string> text;
    size_t pos = 0;
    explicit Tokens(const std::string& s) {
        for (size_t i = 0; i < s.size();) {
            if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
            if (s.compare(i, 2, "/*") == 0) {
                auto end = s.find("*/", i + 2);
                if (end == std::string::npos) throw std::runtime_error("unterminated schedule comment");
                i = end + 2; continue;
            }
            size_t first = i++;
            if (std::isalnum(static_cast<unsigned char>(s[first])) || s[first] == '_' || s[first] == '$') {
                while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.' || s[i] == '$')) ++i;
            } else {
                for (const auto* op : {">>>", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+:", "-:"})
                    if (s.compare(first, std::char_traits<char>::length(op), op) == 0) { i = first + std::char_traits<char>::length(op); break; }
            }
            text.push_back(s.substr(first, i - first));
        }
    }
    std::string peek() const { return pos < text.size() ? text[pos] : ""; }
    std::string take() { if (pos == text.size()) throw std::runtime_error("incomplete scheduled expression"); return text[pos++]; }
    bool eat(const std::string& s) { if (peek() != s) return false; ++pos; return true; }
    void need(const std::string& s) { if (!eat(s)) throw std::runtime_error("expected '" + s + "' in schedule, got '" + peek() + "'"); }
};
int precedence(const std::string& op) {
    static const std::map<std::string, int> p{{"||",1},{"&&",2},{"|",3},{"^",4},{"&",5},{"==",6},{"!=",6},
        {"<",7},{">",7},{"<=",7},{">=",7},{"<<",8},{">>",8},{">>>",8},{"+",9},{"-",9},{"*",10},{"/",10},{"%",10}};
    auto it = p.find(op); return it == p.end() ? -1 : it->second;
}
Expr parse(Tokens& t, int level = 0) {
    auto token = t.take(); Expr result;
    if (token == "(" ) { result = parse(t); t.need(")"); }
    else if (token == "~" || token == "!" || token == "-" || token == "+") result = expr("unary" + token, {parse(t, 11)});
    else if (token == "'") result = expr("fill" + t.take());
    else result = expr(token);
    for (;;) {
        if (t.eat("'")) {
            if (t.eat("(")) { auto v = parse(t); t.need(")"); result = expr("cast", {result, v}); }
            else result = expr("literal", {result, expr(t.take())});
        } else if (t.eat("(")) {
            std::vector<Expr> args;
            if (!t.eat(")")) { do { args.push_back(parse(t)); } while(t.eat(",")); t.need(")"); }
            result = expr("call:" + result->op, std::move(args));
        } else if (t.eat("[")) {
            auto low = parse(t);
            if (t.eat("+:")) { auto count = parse(t); result = expr("slice", {result, low, count}); }
            else if (t.eat(":")) { auto bottom = parse(t); result = expr("range", {result, low, bottom}); }
            else result = expr("bit", {result, low});
            t.need("]");
        } else break;
    }
    while (precedence(t.peek()) >= level) {
        auto op = t.take(); result = expr(op, {result, parse(t, precedence(op) + 1)});
    }
    if (level == 0 && t.eat("?")) { auto yes = parse(t); t.need(":"); result = expr("?:", {result, yes, parse(t)}); }
    return result;
}
struct Typed { Value bits; bool sign = false; };
class Exporter {
    Graph& g;
    const ScheduledDesign& d;
    std::string scope;
    std::map<std::string, Value> env, states, ports;
    std::map<std::string, Value> atomicConstants;
    std::set<std::string> unguardedTemporaries;
    Value reset;
    Value evaluationGuard{1};
    Value boolean(Value v) { return g.unary("any", g.resolved(v)); }
    Value inv(Value v) { return g.unary("not", boolean(v)); }
    Value both(Value a, Value b) { return g.binary("and", boolean(a), boolean(b), 1); }
    Value either(Value a, Value b) { return g.binary("or", boolean(a), boolean(b), 1); }
    uint64_t integer(Value v) {
        auto n = number(g.resolved(v));
        if (!n) throw std::runtime_error("scheduled graph requires a constant width or index");
        return *n;
    }
    Value shift(Value data, Value amount, bool left, bool sign = false) {
        if (auto known = number(g.resolved(amount))) {
            Value result(data.size(), sign ? data.back() : 0);
            for (size_t i = 0; i < result.size(); ++i)
                if (left ? *known <= i : *known < data.size() - i) result[i] = data[left ? i - *known : i + *known];
            return result;
        }
        if (data.size() <= 64) return g.binary(left ? "shl" : sign ? "sar" : "shr", data, amount, data.size());
        for (size_t stage = 0; stage < amount.size(); ++stage) {
            auto next = stage < 63 ? shift(data, constant(uint64_t(1) << stage, 64), left, sign) : Value(data.size(), sign ? data.back() : 0);
            data = g.mux({amount[stage]}, next, data);
        }
        return data;
    }
    Value select(Value data, Value offset, unsigned count) {
        if (auto n = number(g.resolved(offset))) return slice(data, *n, count);
        return resize(shift(data, offset, false), count);
    }
    Value valid(Value address, Value count) {
        address = resize(address, d.addressBits); count = resize(count, 32);
        return both(inv(g.binary("lt", constant(d.memoryBytes, 32), count, 1)),
            both(inv(g.binary("lt", address, constant(16, d.addressBits), 1)),
                inv(g.binary("lt", g.binary("sub", constant(d.memoryBytes, d.addressBits), resize(count, d.addressBits), d.addressBits), address, 1))));
    }
    Value bankLoad(const std::vector<Value>& banks, Value address, unsigned count) {
        Value result;
        if (auto known = number(g.resolved(address))) {
            for (unsigned byte = 0; byte < count; ++byte) {
                auto addr = (*known + byte) & mask(d.addressBits);
                auto v = slice(banks[addr % 8], (addr / 8) * 8, 8);
                result.insert(result.end(), v.begin(), v.end());
            }
            return result;
        }
        unsigned indexBits = 0; while ((uint64_t(1) << indexBits) < d.memoryBytes / 8) ++indexBits;
        for (unsigned byte = 0; byte < count; ++byte) {
            auto addr = g.binary("add", resize(address, d.addressBits), constant(byte, d.addressBits), d.addressBits);
            auto row = resize(slice(addr, 3, indexBits), indexBits + 3);
            row.insert(row.begin(), 3, 0); row.resize(indexBits + 3);
            Value v(8, 0);
            for (unsigned bank = 0; bank < 8; ++bank)
                v = g.mux(g.binary("eq", slice(addr, 0, 3), constant(bank, 3), 1), select(banks[bank], row, 8), v);
            result.insert(result.end(), v.begin(), v.end());
        }
        return result;
    }
    Value bankWrite(Value bank, Value address, Value data, Value fault, unsigned lane, unsigned bytes, Value size = {}) {
        if (auto known = number(g.resolved(address))) {
            for (unsigned byte = 0; byte < bytes; ++byte) {
                auto addr = (*known + byte) & mask(d.addressBits);
                if (addr % 8 != lane || addr >= d.memoryBytes) continue;
                auto enable = inv(fault);
                if (!size.empty()) enable = both(enable, g.binary("lt", constant(byte, 32), resize(size, 32), 1));
                auto row = (addr / 8) * 8;
                auto v = g.mux(enable, slice(data, byte * 8, 8), slice(bank, row, 8));
                std::copy(v.begin(), v.end(), bank.begin() + row);
            }
            return bank;
        }
        unsigned indexBits = 0; while ((uint64_t(1) << indexBits) < d.memoryBytes / 8) ++indexBits;
        for (unsigned byte = 0; byte < bytes; ++byte) {
            auto addr = g.binary("add", resize(address, d.addressBits), constant(byte, d.addressBits), d.addressBits);
            auto enable = both(inv(fault), g.binary("eq", slice(addr, 0, 3), constant(lane, 3), 1));
            if (!size.empty()) enable = both(enable, g.binary("lt", constant(byte, 32), resize(size, 32), 1));
            for (unsigned row = 0; row < d.memoryBytes / 8; ++row) {
                auto use = both(enable, g.binary("eq", slice(addr, 3, indexBits), constant(row, indexBits), 1));
                auto v = g.mux(use, slice(data, byte * 8, 8), slice(bank, row * 8, 8));
                std::copy(v.begin(), v.end(), bank.begin() + row * 8);
            }
        }
        return bank;
    }
    Typed call(const std::string& name, const std::vector<Expr>& expressions) {
        std::vector<Value> a;
        for (auto& e : expressions) a.push_back(evaluate(e).bits);
        if (name == "$signed" || name == "$unsigned") return {a.at(0), name == "$signed"};
        const std::string domain=name.find("hls_local_")==0?"local":"external";
        if(name=="hls_"+domain+"_memory_fault") {
            if(a.size()!=1 || !number(a[0]))throw std::runtime_error("invalid streaming memory identity");
            auto key=scope+"."+domain+"_access_"+std::to_string(*number(a[0]));
            for(size_t n=0;n<g.nodes.size();++n)if((g.nodes[n].op==domain+"_load" || g.nodes[n].op==domain+"_store") && g.nodes[n].name==key) {
                Value loaded;
                for(unsigned bit=0;bit<g.nodes[n].width;++bit)loaded.push_back((n+1)*64+2+bit);
                return {g.add(domain+"_fault",32,loaded,{},evaluationGuard,key)};
            }
            throw std::runtime_error("streaming memory fault has no matching access");
        }
        const std::string readPrefix="hls_"+domain+"_read_";
        if(name.find(readPrefix)==0) {
            if(a.size()!=3 || !number(a[2]))throw std::runtime_error("invalid streaming read arguments");
            return {g.add(domain+"_load",std::stoul(name.substr(readPrefix.size())),
                resize(a[0],32),resize(a[1],8),both(evaluationGuard,inv(env.at("fault"))),
                scope+"."+domain+"_access_"+std::to_string(*number(a[2])))};
        }
        const std::string writePrefix="hls_"+domain+"_write_";
        if(name.find(writePrefix)==0) {
            if(a.size()!=4 || !number(a[3]))throw std::runtime_error("invalid streaming store arguments");
            auto payload=resize(a[1],8),data=resize(a[2],std::stoul(name.substr(writePrefix.size())));
            payload.insert(payload.end(),data.begin(),data.end());
            return {g.add(domain+"_store",1,resize(a[0],32),payload,
                both(evaluationGuard,inv(env.at("fault"))),scope+"."+domain+"_access_"+std::to_string(*number(a[3])))};
        }
        if (auto it = d.atomics.find(name); it != d.atomics.end()) {
            const auto& spec = it->second;
            auto known=atomicConstants.find(name);
            if(known==atomicConstants.end()) {
                // A pure leaf callee may prove output bits constant. In
                // particular, a MAC cannot report a memory fault. Hiding its
                // zero fault behind an opaque call creates a false dependency
                // between independent OpenMP lanes and serializes their DSPs.
                Value constants(spec.resultBits+32,2);
                Graph child;child.load(spec.logic.c_str());child.optimize();
                bool pure=child.states.empty() && child.memories.empty() && child.memoryWrites.empty() && child.memoryAccesses.empty();
                for(const auto& node:child.nodes)pure&=!node.hostEffect() &&
                    node.op!="external_load" && node.op!="external_store" &&
                    node.op!="local_load" && node.op!="local_store" &&
                    node.op!="atomic_call" && node.op!="pipeline_call";
                if(pure)for(const auto& port:child.ports)if(!port.input && (port.name=="result_out" || port.name=="fault_out")) {
                    const unsigned offset=port.name=="result_out"?0:spec.resultBits;
                    const unsigned width=port.name=="result_out"?spec.resultBits:32;
                    if(port.bits.size()!=width)throw std::runtime_error("atomic constant output width mismatch");
                    auto value=child.resolved(port.bits);
                    for(unsigned bit=0;bit<width;++bit)if(value[bit]<2)constants[offset+bit]=value[bit];
                }
                known=atomicConstants.emplace(name,std::move(constants)).first;
            }
            Value inputs, result;
            if (a.size() != 3) throw std::runtime_error("atomic call requires three arguments");
            for (const auto& arg : a) {
                auto bits = resize(arg,spec.argumentBits);
                inputs.insert(inputs.end(),bits.begin(),bits.end());
            }
            for (unsigned offset=0;offset<spec.resultBits+32;offset+=64) {
                auto bits=g.add(spec.module.empty()?"pipeline_call":"atomic_call",std::min(64u,spec.resultBits+32-offset),inputs,
                    constant(offset,32),{},name);
                for(unsigned bit=0;bit<bits.size();++bit)
                    if(known->second[offset+bit]<2)bits[bit]=known->second[offset+bit];
                result.insert(result.end(),bits.begin(),bits.end());
            }
            return {result};
        }
        if (auto it = d.blackboxes.find(name); it != d.blackboxes.end()) {
            Value inputs;
            for (auto& arg : a) {
                if (arg.size() != 64) throw std::runtime_error("invalid blackbox argument slot");
                inputs.insert(inputs.end(),arg.begin(),arg.end());
            }
            const auto& spec = it->second.first;
            return {g.add("blackbox",it->second.second,inputs,constant(spec.delayPs,64),{},spec.module)};
        }
        if (name == "hls_storage_address_valid") return {valid(a.at(0), a.at(1))};
        if (name.find("hls_bank_write_") == 0) {
            auto tail = name.substr(15);
            auto split = tail.find('_');
            auto count = tail.substr(0, split);
            unsigned lane = std::stoul(tail.substr(split + 1));
            return {bankWrite(a.at(0), a.at(1), a.at(2), a.at(3), lane,
                count == "port" ? d.portBytes : std::stoul(count) / 8, count == "port" ? a.at(4) : Value{})};
        }
        bool checked = name.find("hls_storage_read_") == 0;
        if (checked || name.find("hls_storage_load_") == 0) {
            unsigned width = std::stoul(name.substr(17));
            Value v = bankLoad(std::vector<Value>(a.begin(), a.begin() + 8), a.at(8), width / 8);
            if (checked) {
                auto fault = g.mux(boolean(a.at(9)), a.at(9), g.mux(valid(a.at(8), constant(width / 8, 32)), constant(0,32), constant(3,32)));
                v = g.mux(inv(fault), v, Value(v.size(), 0)); v.insert(v.end(), fault.begin(), fault.end());
            }
            return {v};
        }
        throw std::runtime_error("unsupported scheduled graph helper: " + name);
    }
    Typed evaluate(const Expr& e, unsigned context = 0) {
        const auto& op = e->op;
        if (op == "cast") {
            unsigned width = integer(evaluate(e->args[0]).bits);
            auto v = evaluate(e->args[1], width); return {resize(v.bits, width, v.sign), v.sign};
        }
        if (op == "literal") {
            unsigned width = integer(evaluate(e->args[0]).bits); auto text = e->args[1]->op;
            bool sign = !text.empty() && text[0] == 's'; if (sign) text.erase(0, 1);
            int base = text[0] == 'h' ? 16 : text[0] == 'b' ? 2 : text[0] == 'o' ? 8 : 10;
            auto digits = text.substr(1); digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
            return {constant(std::stoull(digits, nullptr, base), width), sign};
        }
        if (op == "fill0" || op == "fill1") return {Value(std::max(1u,context), op == "fill1")};
        if (op.find("call:") == 0) return call(op.substr(5), e->args);
        if (op == "slice" || op == "range" || op == "bit") {
            auto data = evaluate(e->args[0]).bits, low = evaluate(e->args[1]).bits; unsigned width = 1;
            if (op == "slice") width = integer(evaluate(e->args[2]).bits);
            else if (op == "range") { auto bottom = evaluate(e->args[2]).bits; width = integer(low) - integer(bottom) + 1; low = bottom; }
            return {select(data, low, width)};
        }
        if (op.find("unary") == 0) {
            auto v = evaluate(e->args[0], op == "unary!" ? 0 : context);
            if (op == "unary!") return {inv(v.bits)};
            v.bits = resize(v.bits, std::max<unsigned>(context, v.bits.size()), v.sign);
            if (op == "unary~") v.bits = g.unary("not", v.bits);
            else if (op == "unary-") v.bits = g.binary("sub", Value(v.bits.size(),0), v.bits, v.bits.size());
            return v;
        }
        if (op == "?:") {
            auto a = evaluate(e->args[1], context), b = evaluate(e->args[2], context);
            unsigned width = std::max(a.bits.size(), b.bits.size()); bool sign = a.sign && b.sign;
            return {g.mux(boolean(evaluate(e->args[0]).bits), resize(a.bits,width,sign), resize(b.bits,width,sign)),sign};
        }
        if (e->args.size() == 2) {
            bool compare = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
            bool logical = op == "&&" || op == "||", shifting = op == "<<" || op == ">>" || op == ">>>";
            auto a = evaluate(e->args[0], compare || logical ? 0 : context), b = evaluate(e->args[1], compare || logical || shifting ? 0 : context);
            if (logical) return {op == "&&" ? both(a.bits,b.bits) : either(a.bits,b.bits)};
            unsigned width = shifting ? std::max<unsigned>(context,a.bits.size()) : std::max<unsigned>(compare ? 0 : context,std::max(a.bits.size(),b.bits.size()));
            bool sign = a.sign && b.sign;
            if (shifting) return {shift(resize(a.bits,width,a.sign),b.bits,op == "<<",op == ">>>" && a.sign),a.sign};
            a.bits = resize(a.bits,width,sign); b.bits = resize(b.bits,width,sign);
            if (op == "==" || op == "!=") {
                Value same = {1}; for (unsigned i = 0; i < width; i += 64)
                    same = both(same, g.binary("eq",slice(a.bits,i,std::min(64u,width-i)),slice(b.bits,i,std::min(64u,width-i)),1));
                return {op == "==" ? same : inv(same)};
            }
            if (compare) {
                if (op == ">" || op == "<=") std::swap(a,b);
                auto result = g.binary(sign ? "slt" : "lt",a.bits,b.bits,1);
                return {op == "<=" || op == ">=" ? inv(result) : result};
            }
            static const std::map<std::string,std::string> ops{{"+","add"},{"-","sub"},{"*","mul"},{"/","div"},{"%","mod"},{"&","and"},{"|","or"},{"^","xor"}};
            auto code = ops.at(op); if (sign && (code == "div" || code == "mod")) code = "s" + code;
            // Address scaling is wiring, not an indivisible multiplier cell.
            if (code == "mul") {
                auto rhs = number(g.resolved(b.bits)), lhs = number(g.resolved(a.bits));
                if (!rhs && lhs) { std::swap(a,b); rhs = lhs; }
                if (rhs && *rhs && (*rhs & (*rhs - 1)) == 0)
                    return {shift(a.bits,constant(__builtin_ctzll(*rhs),32),true),sign};
            }
            return {g.binary(code,a.bits,b.bits,width),sign};
        }
        if (!op.empty() && std::isdigit(static_cast<unsigned char>(op[0]))) return {constant(std::stoull(op),32),true};
        if (auto c = d.constants.find(op); c != d.constants.end()) return {c->second};
        auto found = env.find(op); if (found == env.end()) throw std::runtime_error("unknown scheduled value: " + op);
        return {found->second};
    }
    void assign(const Expr& lhs, Typed rhs, Value enable) {
        if (lhs->args.empty()) {
            if (!env.count(lhs->op)) throw std::runtime_error("unknown scheduled destination: " + lhs->op);
            auto& old = env.at(lhs->op);
            auto value = resize(rhs.bits,old.size(),rhs.sign);
            old = unguardedTemporaries.count(lhs->op) ? value : g.mux(enable,value,old);
            return;
        }
        if ((lhs->op != "slice" && lhs->op != "range" && lhs->op != "bit") || !lhs->args[0]->args.empty())
            throw std::runtime_error("unsupported scheduled assignment target");
        auto low = evaluate(lhs->args[1]).bits; unsigned width = 1;
        if (lhs->op == "slice") width = integer(evaluate(lhs->args[2]).bits);
        else if (lhs->op == "range") { auto bottom = evaluate(lhs->args[2]).bits; width = integer(low) - integer(bottom) + 1; low = bottom; }
        auto& old = env.at(lhs->args[0]->op); rhs.bits = resize(rhs.bits,width,rhs.sign);
        if (auto offset = number(g.resolved(low))) {
            if (*offset + width > old.size()) throw std::runtime_error("scheduled write outside value");
            auto next = g.mux(enable,rhs.bits,slice(old,*offset,width)); std::copy(next.begin(),next.end(),old.begin()+*offset);
        } else {
            for (unsigned i = 0; i + width <= old.size(); ++i) {
                auto next = g.mux(both(enable,g.binary("eq",low,constant(i,low.size()),1)),rhs.bits,slice(old,i,width));
                std::copy(next.begin(),next.end(),old.begin()+i);
            }
        }
    }
    void statement(Tokens& t, Value enable) {
        if (t.eat("begin")) { while (t.peek() != "end") statement(t,enable); t.need("end"); return; }
        if (t.eat("if")) {
            t.need("("); auto cond = boolean(evaluate(parse(t)).bits); t.need(")"); statement(t,both(enable,cond));
            if (t.eat("else")) statement(t,both(enable,inv(cond))); return;
        }
        if (t.eat(";")) return;
        auto lhs = parse(t); t.need("=");
        unsigned width = lhs->args.empty() ? env.at(lhs->op).size() : evaluate(lhs).bits.size();
        auto previousGuard=evaluationGuard;evaluationGuard=enable;
        auto rhs = evaluate(parse(t),width);evaluationGuard=previousGuard;
        t.need(";"); assign(lhs,rhs,enable);
    }
    Value state(const std::string& name, unsigned width) {
        auto v = g.wire(width,scope + "." + name,"state"); states[name] = v; env[name] = v; return v;
    }
public:
    Exporter(Graph& graph, const ScheduledDesign& design, std::string name) : g(graph), d(design), scope(std::move(name)) {}
    std::map<std::string,Value> run(bool pipeline = false, bool initialize = false, bool region = false) {
        g.currentScope = scope;
        for (auto [name,width] : std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},{"response_ready_in",1},{"operation_in",d.argumentBits},{"index_in",d.argumentBits},{"value_in",d.argumentBits}})
            ports[name] = env[name] = g.wire(width,scope + "." + name);
        reset = env["reset"];
        if (pipeline) reset = {0};
        for (auto [name,width] : std::map<std::string,unsigned>{{"phase",32},{"fault",32},{"heap_next",d.addressBits},{"result",d.resultBits},{"pending",1},{"booting",1}}) {
            if (pipeline) env[name] = Value(width,0); else state(name,width);
        }
        for (auto& [name,symbol] : d.symbols) env[name] = Value(symbol.width,0);
        for (auto& name : d.clocked) state(name,d.symbols.at(name).width);
        std::map<std::string,StreamPipeline> atomicPipelines;
        for (const auto& [id,spec] : d.atomics) {
            if (pipeline) { g.attributes.push_back({id,"atomic_method",spec.serialize()}); continue; }
            StreamPipeline child; child.scope = scope + "." + id; child.latency = spec.minimumLatency;
            child.logic = spec.logic;
            child.pins["reset"] = either(reset,boolean(env["fault"]));
            for (const auto& pin : {"operation_in","index_in","value_in","command_valid_in","response_ready_in"}) {
                unsigned width = std::string(pin).find("operation") == 0 || std::string(pin) == "index_in" || std::string(pin) == "value_in" ? spec.argumentBits : 1;
                child.pins[pin] = g.wire(width,child.scope+"."+pin);
                env[id+"_"+pin] = Value(width,0);
            }
            Graph logic; logic.load(spec.logic.c_str());
            buildStreamPipeline(g,child,std::move(logic));
            for (const auto& pin : {"command_ready_out","response_valid_out","result_out","fault_out"})
                env[id+"_"+pin] = child.pins.at(pin);
            if (!spec.module.empty()) {
            g.attributes.push_back({child.scope,"pipeline_replacement",spec.module});
            g.attributes.push_back({child.scope,"pipeline_replacement_body",spec.body});
            g.attributes.push_back({child.scope,"pipeline_replacement_min_latency",std::to_string(spec.minimumLatency)});
            }
            g.attributes.push_back({child.scope,"atomic_memory_parent",scope});
            atomicPipelines.emplace(id,std::move(child));
        }
        env["MEM_BYTES"] = constant(d.memoryBytes,32); env["HEAP_BYTES"] = constant(d.heapBytes,32);
        env["ADDR_BITS"] = constant(d.addressBits,32);
        unsigned scratch = d.memoryBytes * 8 + 32;
        for (auto& [name,symbol] : d.symbols) scratch = std::max(scratch,symbol.width + 32);
        env["CALL_RESULT_BITS"] = constant(scratch,32); env["hls_call_result"] = Value(scratch,0);
        std::vector<Value> banks;
        if (!d.blockRam && !pipeline) for (unsigned i = 0; i < 8; ++i) banks.push_back(state("storage_lane" + std::to_string(i),d.memoryBytes));
        if (d.sharedMemory) {
            env["memory_address"] = Value(d.addressBits,0); env["memory_size"] = Value(32,0);
            env["memory_write_data"] = Value(d.portBytes * 8,0); env["memory_read"] = env["memory_write"] = {0};
            state("memory_read_data",d.portBytes * 8);
        }
        Value externalBusy{0}, externalSent{0}, externalAccept{0}, externalComplete{0}, externalBad{0};
        Value externalPair{0}, externalSent1{0}, externalReceived0{0}, externalError0{0}, externalBad1{0};
        if (d.externalMemory) {
            for (auto [name,width] : std::map<std::string,unsigned>{{"ready_out",1},{"valid_out",1},{"data_out",d.externalDataBits},{"error_out",1}})
                ports["memory_out." + name] = g.wire(width,scope + ".memory_out." + name);
            externalBusy = state("external_busy",1); externalSent = state("external_sent",1);
            for (auto [name,width] : std::map<std::string,unsigned>{{"address",32},{"write_data",d.externalDataBits},{"size",8},{"write",1}}) {
                env["external_" + name] = Value(width,0);
                auto saved = state("external_saved_" + name,width);
                std::string pin = name == "address" ? "addr_in" : name == "write_data" ? "data_in" : name + "_in";
                ports["memory_out." + pin] = saved;
            }
            env["external_read"] = {0}; state("external_read_data",d.externalDataBits);
            for (unsigned bit=0; (1u<<bit)<d.externalDataBits/8; ++bit)
                externalBad = either(externalBad,both(slice(states.at("external_saved_address"),bit,1),
                    g.binary("lt",constant(1u<<bit,8),states.at("external_saved_size"),1)));
            if (d.externalReadWindow > 1) {
                env["external_pair"]={0}; externalPair=state("external_saved_pair",1);
                externalSent1=state("external_sent_1",1);
                externalReceived0=state("external_received_0",1);
                externalError0=state("external_error_0",1);
                for (auto [name,width]:std::map<std::string,unsigned>{{"address_1",32},{"size_1",8}}) {
                    env["external_"+name]=Value(width,0);
                    state("external_saved_"+name,width);
                }
                state("external_read_data_1",d.externalDataBits);
                for (unsigned bit=0;(1u<<bit)<d.externalDataBits/8;++bit)
                    externalBad1=either(externalBad1,both(slice(states.at("external_saved_address_1"),bit,1),
                        g.binary("lt",constant(1u<<bit,8),states.at("external_saved_size_1"),1)));
                ports["memory_out.addr_in"]=g.mux(externalSent,states.at("external_saved_address_1"),states.at("external_saved_address"));
                ports["memory_out.size_in"]=g.mux(externalSent,states.at("external_saved_size_1"),states.at("external_saved_size"));
                auto request=either(inv(externalSent),both(externalPair,both(inv(externalSent1),inv(externalBad1))));
                ports["memory_out.valid_in"]=both(inv(reset),both(externalBusy,both(inv(externalBad),request)));
                externalAccept=both(ports.at("memory_out.valid_in"),ports.at("memory_out.ready_out"));
                auto outstanding=g.mux(externalReceived0,either(externalSent1,both(externalAccept,externalSent)),
                    either(externalSent,both(externalAccept,inv(externalSent))));
                ports["memory_out.ready_in"]=both(inv(reset),both(externalBusy,outstanding));
            } else {
                ports["memory_out.valid_in"] = both(inv(externalBad),both(inv(reset),both(externalBusy,inv(externalSent))));
                externalAccept = both(ports.at("memory_out.valid_in"),ports.at("memory_out.ready_out"));
                ports["memory_out.ready_in"] = both(inv(reset),both(externalBusy,either(externalSent,externalAccept)));
            }
            externalComplete = both(ports.at("memory_out.ready_in"),ports.at("memory_out.valid_out"));
        }
        auto ready = both(both(inv(env["booting"]),inv(env["pending"])),both(inv(env["phase"]),inv(env["fault"])));
        ports["command_ready_out"] = ready; ports["response_valid_out"] = env["pending"];
        ports["result_out"] = env["result"]; ports["fault_out"] = env["fault"];
        auto accepted = both(env["command_valid_in"],ready);
        if (pipeline) accepted = {1};
        if (d.sharedMemory || d.externalMemory) for (const std::string pin : {"operation_in","index_in","value_in"}) {
            auto previous = state("accepted_" + pin,32);
            env["accepted_" + pin] = g.mux(reset,Value(32,0),g.mux(accepted,env[pin],previous));
            env[pin] = d.externalMemory ? previous : g.mux(accepted,env[pin],previous);
        }
        std::set<int> reachable, continuations{d.resetEntry};
        if (d.externalMemory) continuations.insert(d.commandEntry);
        std::function<void(int)> visit = [&](int n) { if (n < 0 || !reachable.insert(n).second) return; visit(d.blocks[n].yes); visit(d.blocks[n].no); };
        if (!pipeline) visit(d.resetEntry);
        visit(initialize ? d.resetEntry : d.commandEntry);
        std::vector<unsigned> incoming(d.blocks.size());
        for (auto n : reachable) {
            const auto& b = d.blocks[n];
            if (b.suspend) continuations.insert(b.yes); else if (b.yes >= 0) ++incoming[b.yes];
            if (b.no >= 0 && (b.no != b.yes || b.suspend)) ++incoming[b.no];
        }
        std::set<int> todo; for (auto n : reachable) if (!incoming[n]) todo.insert(n);
        Value noFault = inv(env["fault"]), start = both(inv(reset),noFault);
        start = both(start,inv(externalBusy));
        std::vector<Value> active(d.blocks.size(),Value{0});
        if (!d.externalMemory) active[initialize ? d.resetEntry : d.commandEntry] = both(start,accepted);
        auto phase = env["phase"], legal = inv(phase);
        for (int n : continuations) {
            auto is = g.binary("eq",phase,constant(n+1,32),1); legal = either(legal,is);
            active[n] = either(active[n],both(start,d.externalMemory ? is : both(inv(accepted),is)));
        }
        env["fault"] = g.mux(both(start,d.externalMemory ? inv(legal) : both(inv(accepted),inv(legal))),constant(4,32),env["fault"]);
        env["pending"] = g.mux(both(inv(reset),env["response_ready_in"]),Value{0},env["pending"]);
        for (auto [name,val] : std::map<std::string,uint64_t>{{"phase",uint64_t(d.resetEntry+1)},{"fault",0},{"pending",0},{"result",0},{"booting",1},{"heap_next",d.heapBase}})
            env[name] = g.mux(reset,constant(val,env[name].size()),env[name]);
        if (d.externalMemory) env["phase"] = g.mux(both(inv(reset),accepted),constant(d.commandEntry+1,32),env["phase"]);
        // Reconverging pure command paths carry the activation of their
        // dominating fork. Rebuilding it as a sum of branch predicates creates
        // spurious dependencies between independent inlined arithmetic calls
        // (especially FP helpers with early returns). Find the nearest such
        // fork using dominator and postdominator trees of this acyclic CFG.
        // Keep the current fault guard at the join: faults still suppress all
        // subsequent effects. For clocked schedules, cut suspended edges and
        // add a virtual entry for every independently activated continuation.
        // A join must never regain activation across a clock/memory wait.
        std::vector<int> joinSource(d.blocks.size(), -1);
        {
            auto degrees = incoming;
            auto queue = todo;
            std::vector<int> order;
            std::vector<std::vector<int>> predecessors(d.blocks.size());
            for (int n : reachable) {
                const auto& b = d.blocks[n];
                for (unsigned edge=0;edge<2;++edge) {
                    int next=edge?b.no:b.yes;
                    if(next<0||(!edge&&b.suspend)||(edge&&b.no==b.yes&&!b.suspend))continue;
                    predecessors[next].push_back(n);
                }
            }
            while (!queue.empty()) {
                int n = *queue.begin(); queue.erase(queue.begin()); order.push_back(n);
                const auto& b = d.blocks[n];
                for (unsigned edge = 0; edge < 2; ++edge) {
                    int next = edge ? b.no : b.yes;
                    if (next < 0 || (!edge && b.suspend) || (edge && b.no == b.yes && !b.suspend)) continue;
                    if (--degrees[next] == 0) queue.insert(next);
                }
            }
            if (order.size() != reachable.size())
                throw std::runtime_error("cycle in combinational command joins");
            const int entry = d.blocks.size(), exit = entry+1;
            std::vector<int> dominator(exit + 1, -1), postdominator(exit + 1, -1);
            std::vector<unsigned> depth(exit + 1), postdepth(exit + 1);
            dominator[entry]=entry;
            std::set<int> entries;
            if(!pipeline)entries=continuations;
            entries.insert(initialize?d.resetEntry:d.commandEntry);
            for(int n:order)if(entries.count(n)||predecessors[n].empty())predecessors[n].push_back(entry);
            auto common = [](int a, int b, const auto& parent, const auto& level) {
                while (a != b) {
                    if (level[a] > level[b]) a = parent[a];
                    else b = parent[b];
                }
                return a;
            };
            for (int n : order) {
                if (predecessors[n].empty()) dominator[n] = n;
                else {
                    int parent = predecessors[n].front();
                    for (int previous : predecessors[n]) parent = common(parent, previous, dominator, depth);
                    dominator[n] = parent; depth[n] = depth[parent] + 1;
                }
            }
            postdominator[exit] = exit;
            for (auto i = order.rbegin(); i != order.rend(); ++i) {
                int n = *i;
                const auto& b = d.blocks[n];
                int parent = b.yes < 0 || b.suspend ? exit : b.yes;
                if (b.no >= 0) parent = common(parent, b.no, postdominator, postdepth);
                else if (!b.condition.empty()) parent = exit;
                postdominator[n] = parent; postdepth[n] = postdepth[parent] + 1;
            }
            for (int n : order) for (int ancestor = dominator[n]; ancestor != n && ancestor != entry;) {
                int cursor = ancestor;
                while (postdepth[cursor] > postdepth[n]) cursor = postdominator[cursor];
                if (cursor == n) { joinSource[n] = ancestor; break; }
                if (dominator[ancestor] == ancestor) break;
                ancestor = dominator[ancestor];
            }

            // A private value defined once and dominating every read need not
            // carry its block's activation through the datapath. Its consumers
            // already have the required control predicate. Keeping a mux on
            // each such definition serializes independent C++ calculations
            // through branch predicates and inflates the retiming registers.
            // Never speculate persistent state, partial/conditional writes,
            // or a value read before its definition (including clock entries).
            using Site = std::pair<int, unsigned>;
            std::map<std::string, std::vector<Site>> definitions, reads;
            std::set<std::string> conditional;
            for (int n : order) {
                unsigned position = 0;
                std::function<void(const Expr&)> read = [&](const Expr& e) {
                    if (e->args.empty() && d.symbols.count(e->op))
                        reads[e->op].push_back({n,position++});
                    for (const auto& a : e->args) read(a);
                };
                std::function<void(Tokens&, bool)> inspect = [&](Tokens& t, bool guarded) {
                    if (t.eat("begin")) {
                        while (t.peek() != "end") inspect(t,guarded);
                        t.need("end"); return;
                    }
                    if (t.eat("if")) {
                        t.need("("); read(parse(t)); t.need(")"); inspect(t,true);
                        if (t.eat("else")) inspect(t,true);
                        return;
                    }
                    if (t.eat(";")) return;
                    auto lhs = parse(t); t.need("="); read(parse(t)); t.need(";");
                    auto base = lhs;
                    while (!base->args.empty()) base = base->args.front();
                    if (!lhs->args.empty()) read(lhs);
                    if (d.symbols.count(base->op)) {
                        definitions[base->op].push_back({n,position++});
                        if (guarded || !lhs->args.empty()) conditional.insert(base->op);
                    }
                };
                for (const auto& s : d.blocks[n].statements) {
                    Tokens t(s); while (!t.peek().empty()) inspect(t,false);
                }
                if (!d.blocks[n].condition.empty()) {
                    Tokens t(d.blocks[n].condition); read(parse(t));
                }
            }
            for (const auto& [name, sites] : definitions) {
                if (sites.size() != 1 || conditional.count(name) || d.clocked.count(name)) continue;
                const auto [definition, position] = sites.front();
                bool dominates = true;
                for (const auto& [use, usePosition] : reads[name]) {
                    if (use == definition) { if (usePosition <= position) dominates = false; }
                    else {
                        int cursor = use;
                        while (depth[cursor] > depth[definition]) cursor = dominator[cursor];
                        if (cursor != definition) dominates = false;
                    }
                }
                if (dominates) unguardedTemporaries.insert(name);
            }
        }
        size_t visited = 0;
        while (!todo.empty()) {
            int n = *todo.begin(); todo.erase(todo.begin()); ++visited; const auto& b = d.blocks[n];
            g.currentScope = scope + "." + b.method;
            if (joinSource[n] >= 0) active[n] = active[joinSource[n]];
            auto enabled = both(active[n],inv(env["fault"]));
            for (auto& s : b.statements) {
                try { Tokens t(s); while(!t.peek().empty()) statement(t,enabled); }
                catch (const std::exception& e) { throw std::runtime_error(std::string(e.what()) + " in " + s); }
            }
            auto next = both(enabled,inv(env["fault"]));
            Value condition{1}; if (!b.condition.empty()) { Tokens t(b.condition); condition = boolean(evaluate(parse(t)).bits); if(!t.peek().empty()) throw std::runtime_error("trailing schedule condition"); }
            if (b.yes >= 0) {
                auto yes = both(next,condition);
                if (b.suspend) env["phase"] = g.mux(yes,constant(b.yes+1,32),env["phase"]);
                else { active[b.yes] = either(active[b.yes],yes); if (--incoming[b.yes] == 0) todo.insert(b.yes); }
            }
            if (b.no >= 0) { active[b.no] = either(active[b.no],both(next,inv(condition))); if((b.no != b.yes || b.suspend) && --incoming[b.no] == 0) todo.insert(b.no); }
        }
        if(visited != reachable.size()) throw std::runtime_error("cycle without a clock boundary in scheduled graph");
        for (auto& [id,child] : atomicPipelines) {
            for (const auto& pin : {"operation_in","index_in","value_in","command_valid_in","response_ready_in"})
                g.connect(child.pins.at(pin),env.at(id+"_"+pin));
            g.pipelines.push_back(std::move(child));
        }
        g.currentScope = scope;
        if (pipeline) {
            ports["result_out"] = env.at("result");
            ports["fault_out"] = env.at("fault");
            if(region)ports["destination_out"]=env.at("phase");
            for (const auto& [name, bits] : states)
                g.states.push_back({bits, env.at(name), {1}});
            return ports;
        }
        if (d.sharedMemory) {
            auto fault = g.mux(boolean(env["fault"]),env["fault"],g.mux(both(either(env["memory_read"],env["memory_write"]),inv(valid(env["memory_address"],env["memory_size"]))),constant(3,32),constant(0,32)));
            env["fault"] = fault;
            auto enable = both(inv(reset),inv(fault));
            if (d.blockRam) {
                Value data;
                unsigned indexBits = 0; while((uint64_t(1)<<indexBits) < d.memoryBytes/8) ++indexBits;
                for (unsigned lane = 0; lane < 8; ++lane) {
                    size_t m = g.memories.size(); g.memories.push_back({scope+".storage_lane"+std::to_string(lane),8,d.memoryBytes/8});
                    auto offset = g.binary("sub",constant(lane,3),slice(env["memory_address"],0,3),3);
                    auto address = resize(g.binary("add",slice(env["memory_address"],3,indexBits),resize(g.binary("lt",constant(lane,3),slice(env["memory_address"],0,3),1),indexBits),indexBits),indexBits);
                    auto readEnable = both(enable,both(env["memory_read"],inv(env["memory_write"])));
                    auto writeEnable = both(enable,both(env["memory_write"],g.binary("lt",resize(offset,32),env["memory_size"],1)));
                    auto read = g.add("memory_read",8,address,constant(m,64),constant(0,64));
                    g.memoryAccesses.push_back({m,address,readEnable,true});
                    auto saved = state("ram_read_"+std::to_string(lane),8); env["ram_read_"+std::to_string(lane)] = g.mux(readEnable,read,saved);
                    data.insert(data.end(),saved.begin(),saved.end());
                    offset.insert(offset.begin(),3,0);
                    g.memoryWrites.push_back({m,address,resize(shift(resize(env["memory_write_data"],64),offset,false),8),writeEnable});
                }
                auto offset = state("ram_offset",3); env["ram_offset"] = g.mux(both(enable,both(env["memory_read"],inv(env["memory_write"]))),slice(env["memory_address"],0,3),offset);
                offset.insert(offset.begin(),3,0); auto duplicate = data; data.insert(data.end(),duplicate.begin(),duplicate.end());
                // Replace the read-data state by the registered raw lane outputs.
                g.connect(states.at("memory_read_data"),resize(shift(data,offset,false),d.portBytes*8));
                states.erase("memory_read_data");
            } else {
                auto data = bankLoad(banks,env["memory_address"],d.portBytes);
                env["memory_read_data"] = g.mux(both(enable,env["memory_read"]),data,states.at("memory_read_data"));
                for (unsigned lane = 0; lane < 8; ++lane) env["storage_lane"+std::to_string(lane)] = g.mux(both(enable,env["memory_write"]),bankWrite(banks[lane],env["memory_address"],env["memory_write_data"],{0},lane,d.portBytes,env["memory_size"]),banks[lane]);
            }
        }
        if (d.externalMemory && d.externalReadWindow > 1) {
            auto issue=either(env["external_read"],env["external_write"]);
            auto bad=both(inv(reset),both(externalBusy,externalBad));
            auto last=both(externalComplete,either(inv(externalPair),either(externalReceived0,externalBad1)));
            // Retire errors in source order. A failed first read cannot abandon
            // an already offered/accepted second request: drain the batch first.
            auto readError=either(both(externalReceived0,externalError0),ports.at("memory_out.error_out"));
            auto fault=g.mux(bad,constant(3,32),g.mux(last,
                g.mux(readError,constant(5,32),g.mux(both(externalPair,externalBad1),constant(3,32),constant(0,32))),constant(0,32)));
            env["fault"]=g.mux(boolean(env["fault"]),env["fault"],fault);
            auto acceptedIssue=both(issue,inv(env["fault"]));
            auto clear=either(reset,either(acceptedIssue,either(bad,last)));
            env["external_busy"]=g.mux(either(reset,either(bad,last)),{0},either(externalBusy,acceptedIssue));
            env["external_sent"]=g.mux(clear,{0},either(externalSent,both(externalAccept,inv(externalSent))));
            env["external_sent_1"]=g.mux(clear,{0},either(externalSent1,both(externalAccept,externalSent)));
            env["external_received_0"]=g.mux(clear,{0},either(externalReceived0,externalComplete));
            env["external_error_0"]=g.mux(clear,{0},g.mux(both(externalComplete,inv(externalReceived0)),ports.at("memory_out.error_out"),externalError0));
            for (const std::string name:{"address","write_data","size","write","pair","address_1","size_1"}) {
                auto key="external_saved_"+name;
                env[key]=g.mux(reset,Value(states.at(key).size(),0),g.mux(acceptedIssue,env.at("external_"+name),states.at(key)));
            }
            env["external_read_data"]=g.mux(reset,Value(d.externalDataBits,0),
                g.mux(both(externalComplete,inv(externalReceived0)),ports.at("memory_out.data_out"),states.at("external_read_data")));
            env["external_read_data_1"]=g.mux(reset,Value(d.externalDataBits,0),
                g.mux(both(externalComplete,externalReceived0),ports.at("memory_out.data_out"),states.at("external_read_data_1")));
        } else if (d.externalMemory) {
            auto issue = either(env["external_read"],env["external_write"]);
            auto bad = both(inv(reset),both(externalBusy,externalBad));
            auto fault = g.mux(bad,constant(3,32),
                g.mux(both(externalComplete,ports.at("memory_out.error_out")),constant(5,32),constant(0,32)));
            env["fault"] = g.mux(boolean(env["fault"]),env["fault"],fault);
            auto acceptedIssue = both(issue,inv(env["fault"]));
            env["external_busy"] = g.mux(either(reset,bad),{0},g.mux(externalComplete,{0},g.mux(acceptedIssue,{1},externalBusy)));
            env["external_sent"] = g.mux(either(reset,either(externalComplete,acceptedIssue)),{0},either(externalSent,externalAccept));
            for (const std::string name : {"address","write_data","size","write"}) {
                auto key = "external_saved_" + name;
                env[key] = g.mux(reset,Value(states.at(key).size(),0),g.mux(acceptedIssue,env.at("external_" + name),states.at(key)));
            }
            env["external_read_data"] = g.mux(reset,Value(d.externalDataBits,0),g.mux(externalComplete,ports.at("memory_out.data_out"),states.at("external_read_data")));
        }
        auto newlyFaulted = both(boolean(env["fault"]),inv(states.at("fault")));
        for (const std::string name : {"phase","pending","booting"}) env[name] = g.mux(newlyFaulted,constant(name == "pending",env[name].size()),env[name]);
        for (const auto& [name,bits] : states) {
            auto next = env.at(name);
            if (d.clocked.count(name)) next = g.mux(reset,Value(next.size(),0),next);
            if (!d.sharedMemory && name.find("storage_lane") == 0) next = g.mux(reset,bits,next);
            g.states.push_back({bits,next,{1}});
        }
        g.attributes.push_back({scope,"scheduled_hls","1"});
        return ports;
    }
};
}
std::map<std::string,Value> exportScheduledGraph(Graph& g,const ScheduledDesign& d,const std::string& scope) { return Exporter(g,d,scope).run(); }
std::map<std::string,Value> exportCombinationalRegion(Graph& g,const ScheduledDesign& d,const std::string& scope) {
    if(d.sharedMemory)throw std::runtime_error("loop-local memory requires a storage schedule");
    return Exporter(g,d,scope).run(true,false,true);
}
std::map<std::string,Value> exportCombinationalCommand(Graph& g,const ScheduledDesign& d,const std::string& scope) {
    if (d.sharedMemory)
        throw std::runtime_error("pipeline command contains scheduled memory");
    for (const auto& b : d.blocks) if (b.suspend)
        throw std::runtime_error("pipeline command contains a clock suspension");
    Graph initial;
    Exporter(initial,d,scope).run(true,true);
    initial.optimize();
    const auto begin = g.states.size();
    auto ports = Exporter(g,d,scope).run(true);
    if (g.states.size() - begin != initial.states.size())
        throw std::runtime_error("pipeline initializer state mismatch");
    for (size_t i = 0; i < initial.states.size(); ++i) {
        auto value = initial.resolved(initial.states[i].next);
        if (std::any_of(value.begin(), value.end(), [](Bit b) { return b > 1; }))
            throw std::runtime_error("ClockedPipeline state must have a constant initializer");
        g.states[begin+i].resetValue = std::move(value);
    }
    return ports;
}
}

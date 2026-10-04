#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cpphdl::hls {

// Value numbering and available loads for one zero-time path. Identities are
// conversion-time objects, not registers or a runtime memory cache.
class MemoryEffects {
public:
    using Value = size_t;
    struct Address {
        Value base = 0;
        int64_t offset = 0;
        unsigned bytes = 0, bits = 0;
        bool local = false;
        bool operator<(const Address& other) const;
    };
    struct Available {
        Value value = 0;
        std::string expression;
        bool operator==(const Available& other) const;
    };
    struct State {
        std::map<std::string, Value> bindings;
        std::map<Address, Available> loads;
    };
private:
    struct Node { unsigned width; };
    std::vector<Node> nodes{{0}};
    std::map<std::string, Value> atoms;
    std::map<std::pair<unsigned, std::vector<Value>>, Value> expressions;
    unsigned region = 0;
    Value cast(unsigned width, Value value);
public:
    State state;
    void enter(unsigned block) { region = block; }
    Value atom(const std::string& name, unsigned width = 0);
    Value expression(const std::string& text, unsigned width = 0);
    void bind(const std::string& name, Value value) { state.bindings[name] = value; }
    const Available* find(const Address& address) const;
    void remember(const Address& address, Value value, const std::string& expression);
    void write(const Address& address);
    // Captured private locals remain values. Knowledge about external memory
    // does not survive an edge; a read response may seed a new region.
    void clock();
    static State intersect(const State& left, const State& right);
};

}

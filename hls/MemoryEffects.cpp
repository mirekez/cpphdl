#include "MemoryEffects.h"
#include <algorithm>
#include <cctype>
#include <tuple>

namespace cpphdl::hls {

bool MemoryEffects::Address::operator<(const Address& other) const {
    return std::tie(base, offset, bytes, bits, local) <
        std::tie(other.base, other.offset, other.bytes, other.bits, other.local);
}
bool MemoryEffects::Available::operator==(const Available& other) const {
    return value == other.value && expression == other.expression;
}
MemoryEffects::Value MemoryEffects::atom(const std::string& name, unsigned width) {
    auto key = std::to_string(width) + ":" + name;
    auto inserted = atoms.emplace(key, nodes.size());
    if (inserted.second) nodes.push_back({width});
    return inserted.first->second;
}
MemoryEffects::Value MemoryEffects::cast(unsigned width, Value value) {
    if (!width || nodes[value].width == width) return value;
    auto inserted = expressions.emplace(std::make_pair(width, std::vector<Value>{value}), nodes.size());
    if (inserted.second) nodes.push_back({width});
    return inserted.first->second;
}

MemoryEffects::Value MemoryEffects::expression(const std::string& text, unsigned width) {
    auto first = text.find_first_not_of(" \t\n");
    if (first == std::string::npos) return atom("empty", width);
    auto source = text.substr(first, text.find_last_not_of(" \t\n") - first + 1);
    auto bound = state.bindings.find(source);
    if (bound != state.bindings.end()) return cast(width, bound->second);
    // Only remove balanced outer parentheses and redundant sized casts. All
    // operators remain literal tokens; this is not an algebraic rewrite pass.
    auto open = source.find('(');
    if (open != std::string::npos && source.back() == ')') {
        int depth = 0;
        bool whole = true;
        for (size_t i = open; i < source.size(); ++i) {
            if (source[i] == '(') ++depth;
            if (source[i] == ')' && --depth == 0 && i + 1 != source.size()) { whole = false; break; }
        }
        if (whole && depth == 0) {
            if (open == 0) return expression(source.substr(1, source.size() - 2), width);
            if (open > 1 && source[open - 1] == '\'' &&
                std::all_of(source.begin(), source.begin() + open - 1, [](unsigned char c) { return std::isdigit(c); }))
                return cast(width, cast(std::stoul(source.substr(0, open - 1)),
                    expression(source.substr(open + 1, source.size() - open - 2))));
        }
    }
    std::vector<Value> parts;
    for (size_t i = 0; i < source.size();) {
        unsigned char c = source[i];
        if (std::isspace(c)) { ++i; continue; }
        size_t begin = i++;
        bool identifier = std::isalpha(c) || c == '_' || c == '$';
        if (identifier || std::isdigit(c)) {
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) ||
                   source[i] == '_' || (identifier && source[i] == '.') || (!identifier && source[i] == '\''))) ++i;
        }
        auto token = source.substr(begin, i - begin);
        auto binding = state.bindings.find(token);
        if (binding != state.bindings.end()) parts.push_back(binding->second);
        else if (identifier) {
            auto value = atom("region" + std::to_string(region) + ":" + token);
            state.bindings[token] = value;
            parts.push_back(value);
        } else parts.push_back(atom("token:" + token));
    }
    if (parts.size() == 1) return cast(width, parts.front());
    auto inserted = expressions.emplace(std::make_pair(0u, parts), nodes.size());
    if (inserted.second) nodes.push_back({0});
    return cast(width, inserted.first->second);
}

const MemoryEffects::Available* MemoryEffects::find(const Address& address) const {
    auto found = state.loads.find(address);
    return found == state.loads.end() ? nullptr : &found->second;
}
void MemoryEffects::remember(const Address& address, Value value, const std::string& expression) {
    state.loads[address] = {value, expression};
}
void MemoryEffects::write(const Address& address) {
    for (auto i = state.loads.begin(); i != state.loads.end();) {
        const auto& old = i->first;
        // Distinct nonescaping objects cannot alias. Dynamic addresses can,
        // even when their value numbers differ.
        bool overlap = old.local == address.local && (!old.local ||
            (old.base == address.base && old.offset < address.offset + address.bytes &&
             address.offset < old.offset + old.bytes));
        if (overlap) i = state.loads.erase(i); else ++i;
    }
}
void MemoryEffects::clock() {
    for (auto i = state.loads.begin(); i != state.loads.end();)
        if (!i->first.local) i = state.loads.erase(i); else ++i;
}
MemoryEffects::State MemoryEffects::intersect(const State& left, const State& right) {
    State result;
    for (const auto& item : left.bindings) {
        auto found = right.bindings.find(item.first);
        if (found != right.bindings.end() && found->second == item.second) result.bindings.insert(item);
    }
    for (const auto& item : left.loads) {
        auto found = right.loads.find(item.first);
        if (found != right.loads.end() && found->second == item.second) result.loads.insert(item);
    }
    return result;
}

}

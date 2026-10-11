#ifdef NDEBUG
#undef NDEBUG
#endif
#include "CppGraphEnvironment.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <map>

using cpphdl::cpp_graph::Environment;
using Reference = std::map<std::string, Environment::SharedValue>;

static void check(const Environment& actual, const Reference& expected) {
    assert(actual.size() == expected.size());
    auto reference = expected.begin();
    for (const auto& [key, bits] : actual) {
        assert(reference != expected.end());
        assert(key == reference->first && bits == reference->second);
        assert(actual.lookup(key) && *actual.lookup(key) == bits);
        ++reference;
    }
    assert(reference == expected.end());
}

int main() {
    Environment actual;
    Reference expected;
    uint64_t random = 0xaf27694191b5e873ull;
    for (unsigned step = 0; step < 10000; ++step) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        const auto key = "field" + std::to_string(random % 512);
        auto before = actual;
        auto reference = expected;
        if ((random >> 11) & 1) {
            auto bits = std::make_shared<const cpphdl::graph::Value>(cpphdl::graph::Value{random});
            actual.setShared(key, bits); expected[key] = bits;
        } else {
            actual.erase(key); expected.erase(key);
        }
        check(before, reference);
        check(actual, expected);
        const bool changed = expected.count(key) != reference.count(key) ||
            (expected.count(key) && expected.at(key) != reference.at(key));
        assert(actual.changedKeys(before) == (changed ? std::set<std::string>{key} : std::set<std::string>{}));
        if (step % 7 == 0) { actual = before; expected = reference; }
    }
    Environment baseline;
    Reference traditional;
    for (unsigned index = 0; index < 5000; ++index) {
        auto key = "global" + std::to_string(index);
        auto bits = std::make_shared<const cpphdl::graph::Value>(cpphdl::graph::Value{index});
        baseline.setShared(key, bits); traditional[key] = bits;
    }
    auto replacement = std::make_shared<const cpphdl::graph::Value>(cpphdl::graph::Value{123456});
    std::size_t observed = 0;
    auto begin = std::chrono::steady_clock::now();
    for (unsigned branch = 0; branch < 2000; ++branch) {
        auto fork = baseline;
        auto key = "global" + std::to_string(branch);
        fork.setShared(key, replacement);
        auto changes = fork.changedKeys(baseline);
        assert(changes == std::set<std::string>{key});
        observed += changes.size();
    }
    auto middle = std::chrono::steady_clock::now();
    for (unsigned branch = 0; branch < 2000; ++branch) {
        auto fork = traditional;
        auto key = "global" + std::to_string(branch);
        fork[key] = replacement;
        for (const auto& [field, bits] : fork) observed += bits != traditional.at(field);
    }
    auto end = std::chrono::steady_clock::now();
    assert(observed == 4000);
    std::printf("branch snapshots: persistent=%.6f s map-copy=%.6f s; 10000 differential checks passed\n",
        std::chrono::duration<double>(middle - begin).count(),
        std::chrono::duration<double>(end - middle).count());
}

#include "../MemoryEffects.h"
#include <cstdio>
#include <stdexcept>

using cpphdl::hls::MemoryEffects;

int main() {
    try {
        auto check = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
        MemoryEffects e;
        auto pointer = e.atom("pointer", 16), value = e.atom("sample", 32);
        e.bind("p", pointer);
        e.bind("copy", e.expression("16'(p)", 16));
        check(e.expression("copy", 16) == pointer, "pointer copies lost their identity");
        check(e.expression("32'(16'(p))", 32) != pointer, "different width conflated");
        check(e.expression("p + 16'd4", 16) != e.expression("p + 16'd8", 16), "offsets conflated");
        check(e.expression("(p + 1) * 2") != e.expression("p + 1 * 2"), "parentheses lost");
        check(e.expression("$signed(p)") != e.expression("p"), "signedness lost");
        MemoryEffects::Address a{pointer, 0, 4, 32, false};
        e.remember(a, value, "sample");
        auto before = e.state;
        check(e.find(a) && e.find(a)->value == value, "load not available");
        e.write({e.atom("possibly aliased pointer", 16), 0, 1, 8, false});
        check(!e.find(a), "unknown alias or partial store kept stale load");
        e.state = MemoryEffects::intersect(before, e.state);
        check(!e.find(a), "branch write did not invalidate at join");
        e.state = MemoryEffects::intersect(before, before);
        check(e.find(a), "unchanged fork lost dominating read");
        auto other = before;
        other.loads[a] = {value, "other_path_sample"};
        e.state = MemoryEffects::intersect(before, other);
        check(!e.find(a), "join used a sample unavailable on one path");
        e.state = before;
        e.clock();
        check(!e.find(a), "memory value survived clock boundary");
        MemoryEffects::Address local{e.atom("local object"), 0, 4, 32, true};
        e.remember(local, value, "");
        e.remember(a, value, "sample");
        e.write({local.base, 4, 4, 32, true});
        check(e.find(local) && e.find(a), "disjoint local store invalidated memory");
        e.clock();
        check(e.find(local) && !e.find(a), "clock must preserve captured local value, not arena knowledge");
        e.write({local.base, 1, 1, 8, true});
        check(!e.find(local), "overlapping local store kept stale value");
        e.state = {};
        e.enter(1); auto first = e.expression("unbound", 16);
        e.state = {};
        e.enter(2); auto second = e.expression("unbound", 16);
        check(first != second, "unknown values at different entries conflated");
        std::puts("memory effects: aliases, forks, joins, widths and clock boundaries passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}

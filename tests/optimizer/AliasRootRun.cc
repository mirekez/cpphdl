#include "AliasRoot.h"
#include "AliasRoot_optimized_combs.h"

#include <cstdio>

long _system_clock = 0;

int main()
{
    AliasRoot root;
    cpphdl::logic<8> input = 7;
    root.input = _ASSIGN_REG(input);
    cpphdl_optimized_bind_ports_abi(&root);

    calc_all(root, true);
    commit_optimized_regs(root);
    ++_system_clock;
    calc_all(root, false);
    commit_optimized_regs(root);
    ++_system_clock;
    calc_all(root, false);

    const auto first_output = root.output();
    const auto second_output = root.output();
    const bool passed = first_output == cpphdl::logic<8>(10) &&
                   second_output == cpphdl::logic<8>(10) &&
                   root.repeated_value == cpphdl::logic<8>(12) &&
                   root.repeated_second_value == cpphdl::logic<8>(13) &&
                   root.collision_value == cpphdl::logic<8>(4) &&
                   root.forwarded_value == cpphdl::logic<8>(7) &&
                   root.comma_forwarded_value == cpphdl::logic<8>(7) &&
                   root.repeated_evaluations == 12 &&
                   root.output_evaluations == 3;
    if (!passed) {
        std::fprintf(stderr,
                     "output=%llu/%llu repeated=%llu/%llu collision=%llu "
                     "forwarded=%llu evaluations=%u/%u\n",
                     static_cast<unsigned long long>(static_cast<uint64_t>(first_output)),
                     static_cast<unsigned long long>(static_cast<uint64_t>(second_output)),
                     static_cast<unsigned long long>(static_cast<uint64_t>(root.repeated_value)),
                     static_cast<unsigned long long>(static_cast<uint64_t>(root.repeated_second_value)),
                     static_cast<unsigned long long>(static_cast<uint64_t>(root.collision_value)),
                     static_cast<unsigned long long>(static_cast<uint64_t>(root.forwarded_value)),
                     root.repeated_evaluations, root.output_evaluations);
    }
    return passed ? 0 : 1;
}

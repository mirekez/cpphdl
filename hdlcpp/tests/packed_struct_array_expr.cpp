#include "generated/packed_struct_array_expr.h"
#ifdef CPPHDL_TEST_OPTIMIZED
#include "packed_struct_array_expr_optimized_combs.h"
#endif
#include <cstdio>

long _system_clock = 0;

int main()
{
    packed_struct_array_expr model;
    cpphdl::logic<88> left = 0;
    cpphdl::logic<88> right = 0;
    model.left_i_in = _ASSIGN_REG(left);
    model.right_i_in = _ASSIGN_REG(right);
    model._assign();
#ifdef CPPHDL_TEST_OPTIMIZED
    bind_optimized_ports(model);
#endif
    for (unsigned step = 0; step < 176; ++step) {
        ++_system_clock;
        left = 0;
        right = 0;
        left[step % 88] = 1;
        right[(step * 17 + 11) % 88] = step & 1;
        const cpphdl::logic<88> expected = left | right;
#ifdef CPPHDL_TEST_OPTIMIZED
        calc_all(model, false);
#endif
        const cpphdl::logic<88> actual = model.merged_o_out();
        const cpphdl::logic<44> narrow = model.narrow_o_out();
        if (actual != expected || narrow != cpphdl::logic<44>(expected)) {
            std::fprintf(stderr, "packed array expression mismatch at step %u: "
                         "actual=%llx expected=%llx narrow=%llx\n", step,
                         static_cast<unsigned long long>(uint64_t(actual)),
                         static_cast<unsigned long long>(uint64_t(expected)),
                         static_cast<unsigned long long>(uint64_t(narrow)));
            return 1;
        }
    }
}

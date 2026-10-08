#include <cstdint>
#include <cstdio>
#ifdef TEST_RTL
#include "VZeroRepeat.h"
#else
#include "model.h"
#endif
int main() {
#ifdef TEST_RTL
    VZeroRepeat model;
#else
    cpphdl_native::Model model;
#endif
    for (unsigned data = 0; data < 256; ++data) {
        for (unsigned selector = 0; selector < 256; ++selector) {
#ifdef TEST_RTL
            model.data = data;
            model.selector = selector;
            model.eval();
            const unsigned result = model.result;
#else
            model.data[0] = data;
            model.selector[0] = selector;
            model.eval();
            const unsigned result = model.result[0];
#endif
            if (result != ((data << 8) | selector)) {
                std::fprintf(stderr, "zero repeat: data %u selector %u result %u\n", data, selector, result);
                return 1;
            }
        }
    }
    std::puts("65536 zero-repeat cases passed");
}

#include <cpphdl.h>

struct UnsupportedFormattingBitfield
{
    uint64_t mem_addr : 48;
};

int main()
{
    UnsupportedFormattingBitfield value{};
    uint64_t materialized = value.mem_addr;
#if defined(TEST_STD_FORMAT)
    return std::format("{}", materialized).empty();
#elif defined(TEST_STD_PRINT)
    std::print("{}", materialized);
    return 0;
#else
#error "formatting failure case was not selected"
#endif
}

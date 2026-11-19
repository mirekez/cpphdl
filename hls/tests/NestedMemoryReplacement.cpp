#include "../Clocked.h"
#include "NestedAtomic.h"

struct NestedMemoryMethods {
    uint64_t command(uint32_t address,uint32_t index,uint32_t value) const {
        auto memory=cpphdl::hls::external_memory<uint32_t>(address);
        uint32_t operand=memory[index];
        return NestedAtomic{}.command(value,index,operand)+9;
    }
};

cpphdl::hls::ClockedMemory<NestedMemoryMethods> worker;

#ifndef SYNTHESIS
#include <cstdio>
int main() {
    uint32_t words[2]={3,7};
    cpphdl::hls::bind_external_memory(words,sizeof(words),0x1000);
    auto expected=NestedAtomic{}.command(5,1,7)+9;
    if(NestedMemoryMethods{}.command(0x1000,1,5)!=expected) return 1;
    std::puts("PASS native nested atomic after external read");
}
#endif

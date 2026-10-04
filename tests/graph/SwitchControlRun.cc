#include "SwitchControl.cc"
using TestModule = SwitchControl;
#ifdef TEST_RTL
#include "VSwitchControl.h"
using RtlModule = VSwitchControl;
#endif
uint64_t oracle(const uint32_t* words, unsigned selector) {
    const unsigned choice = selector % 8;
    if (choice == 0) return words[0];
    if (choice == 5 && (words[0] & 3) == 1) return 80;
    const unsigned values[] = {0, (words[0] & 1) ? 10u : 70u, 40, 40, 60,
                              (words[0] & 3) == 0 ? 70u : 90u, 110, 110};
    return values[choice] + 14;
}
#include "CombinationalRun.h"

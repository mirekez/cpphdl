#include <cpphdl.h>
using namespace cpphdl;
class LocalRegCopyReject : public Module {
public:
    using payload_t = array<1, logic<8>>;
    reg<payload_t> state;
    _PORT(logic<8>) data_in;
    void mutate(reg<payload_t>& ref) { ref[0] = data_in(); }
    void _work(bool) {
#if REG_REJECT == 0
        state[0] = data_in();
#elif REG_REJECT == 1
        auto& alias = state;
        alias[0] = data_in();
#elif REG_REJECT == 2
        payload_t& alias = state;
        alias[0] = data_in();
#elif REG_REJECT == 3
        mutate(state);
#else
        auto local = state;
        state = local;
#endif
    }
    void _strobe() { state.strobe(); }
};
extern LocalRegCopyReject cpphdl_top;

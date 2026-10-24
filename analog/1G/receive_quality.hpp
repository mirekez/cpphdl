#pragma once
#include "pcs_receiver.hpp"
using namespace cpphdl;

// An evidence counter, not a timer: 64 accepted clean PCS idles establish
// local quality. Once established, valid data preserves quality until a fault.
class ReceiveQuality : public Module {
public:
    static constexpr unsigned PIPELINE_WORDS = PCSReceiver::RECEIVE_LATENCY;
    _PORT(u<10>) receive_in;
    _PORT(bool) valid_in;
    _PORT(bool) locked_in;
    _PORT(bool) fault_in;
    _PORT(bool) clipped_in;
    _PORT(bool) enable_in;
    _PORT(bool)
    ready_out = _ASSIGN(qualified &&
                        !bad(enable_in(), locked_in(), fault_in(), clipped_in(), valid_in(), unsigned(receive_in())));
    _PORT(u<6>) flushed_out = _ASSIGN_REG(flushed);
    _PORT(u<7>) idles_out = _ASSIGN_REG(idles);

private:
    reg<u<6>> flushed{};
    reg<u<7>> idles{};
    reg<u1> qualified{};
    bool bad(bool enabled, bool locked, bool fault, bool clipped, bool valid, unsigned word) {
        return !enabled || !locked || fault || clipped || (valid && (word & 512) != 0);
    }

public:
    void _assign() {}
    void _work(bool reset) {
        if (valid_in() && flushed < PIPELINE_WORDS) {
            flushed._next = flushed + 1;
        } else if (valid_in() && !qualified) {
            if (receive_in() == 0) {
                if (idles < 64)
                    idles._next = idles + 1;
                if (idles >= 63)
                    qualified._next = 1;
            } else {
                idles._next = 0;
            }
        }
        if (bad(enable_in(), locked_in(), fault_in(), clipped_in(), valid_in(), unsigned(receive_in())) || reset) {
            flushed._next = 0;
            idles._next = 0;
            qualified._next = 0;
        }
    }
    void _strobe() {
        flushed.strobe();
        idles.strobe();
        qualified.strobe();
    }
};

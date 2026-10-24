#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// PHY startup/control at a 125 MHz symbol clock. Receive qualification comes
// from the actual receiver; elapsed time alone never grants data transmission.
// mode_out uses the existing PCS encoding: zero / training idle / normal.
class PMAControl : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) master_in;
    _PORT(bool) scrambler_ok_in;
    _PORT(bool) local_receiver_ok_in;
    _PORT(bool) remote_receiver_ok_in;
    _PORT(bool) transmit_active_in;
    _PORT(bool) tick_ms_in;
    _PORT(u<2>) mode_out = _ASSIGN_REG(mode);
    _PORT(bool) link_out = _ASSIGN_REG(link);
    _PORT(bool) timeout_out = _ASSIGN_REG(timeout);
    _PORT(bool) master_out = _ASSIGN_REG(master);
    _PORT(u<3>) state_out = _ASSIGN_REG(state);
private:
    reg<u<3>> state{}; // Disabled, silent, training, idle, normal, timeout.
    reg<u<2>> mode{};
    reg<u<7>> dwell{};
    reg<u<10>> waiting{};
    reg<u1> master{}, link{}, timeout{};
public:
    void _assign() {}
    void _work(bool reset) {
        unsigned next;
        bool expired, ready;
        next = unsigned(state);
        expired = dwell >= 124; // 125 symbol periods = 1 us.
        ready = local_receiver_ok_in() && remote_receiver_ok_in();
        if (state == 0) {
            if (enable_in()) {
                next = 1;
                master._next = master_in();
                waiting._next = 0;
            }
        } else if (state == 1) {
            if (master || scrambler_ok_in())
                next = 2;
        } else if (state == 2) {
            if (expired && local_receiver_ok_in())
                next = remote_receiver_ok_in() ? 4 : 3;
        } else if (state == 3) {
            if (expired) {
                if (!local_receiver_ok_in())
                    next = 1;
                else if (remote_receiver_ok_in())
                    next = 4;
            }
        } else if (state == 4) {
            if (expired) {
                if (!local_receiver_ok_in() && !transmit_active_in())
                    next = 1;
                else if (local_receiver_ok_in() && !remote_receiver_ok_in())
                    next = 3;
            }
        }
        if (state == 1 || state == 2) {
            if (tick_ms_in()) {
                if (waiting >= (master ? 749 : 349) && (next == 1 || next == 2))
                    next = 5;
                else
                    waiting._next = waiting + 1;
            }
        } else {
            waiting._next = 0;
        }
        dwell._next = expired ? dwell : dwell + 1;
        if (next != unsigned(state))
            dwell._next = 0;
        // Re-entering silent starts a new bounded acquisition attempt.
        if (next == 1 && state != 1)
            waiting._next = 0;
        state._next = next;
        mode._next = next == 2 || next == 3 ? 1 : next == 4 ? 2 : 0;
        link._next = next == 4 && ready;
        timeout._next = next == 5;
        if (reset || restart_in() || !enable_in()) {
            state._next = 0;
            mode._next = 0;
            dwell._next = 0;
            waiting._next = 0;
            master._next = 0;
            link._next = 0;
            timeout._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        mode.strobe();
        dwell.strobe();
        waiting.strobe();
        master.strobe();
        link.strobe();
        timeout.strobe();
    }
};

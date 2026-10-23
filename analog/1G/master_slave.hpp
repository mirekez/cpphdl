#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Resolve the Clause 40 role fields after acknowledged UP1/UP2 exchange.
// UP1: U0 manual enable, U1 manual role, U2 multiport. UP2: eleven-bit seed.
// The negotiation controller handles page exchange, equal-seed retries and
// the retry limit. A collision never produces a qualified clock-master choice.
class MasterSlaveResolver : public Module {
public:
    _PORT(u<11>) local_up1_in;
    _PORT(u<11>) remote_up1_in;
    _PORT(u<11>) local_seed_in;
    _PORT(u<11>) remote_seed_in;
    _PORT(bool) advance_in;
    _PORT(bool) resolved_out = _ASSIGN((status & 1) != 0);
    _PORT(bool) master_out = _ASSIGN((status & 2) != 0);
    _PORT(bool) fault_out = _ASSIGN((status & 4) != 0);
    _PORT(bool) collision_out = _ASSIGN((status & 8) != 0);

private:
    reg<u<4>> status{};

public:
    void _assign() {}
    void _work(bool reset) {
        bool local_manual, remote_manual, local_master, remote_master;
        local_manual = (local_up1_in() & 1) != 0;
        remote_manual = (remote_up1_in() & 1) != 0;
        local_master = (local_up1_in() & 2) != 0;
        remote_master = (remote_up1_in() & 2) != 0;
        if (advance_in()) {
            if (local_manual && remote_manual && local_master == remote_master)
                status._next = 4;
            else if (local_manual)
                status._next = local_master ? 3 : 1;
            else if (remote_manual)
                status._next = remote_master ? 1 : 3;
            else if (((local_up1_in() ^ remote_up1_in()) & 4) != 0)
                status._next = (local_up1_in() & 4) != 0 ? 3 : 1;
            else if (local_seed_in() == remote_seed_in())
                status._next = 8;
            else
                status._next = local_seed_in() > remote_seed_in() ? 3 : 1;
        }
        if (reset)
            status._next = 0;
    }
    void _strobe() {
        status.strobe();
    }
};

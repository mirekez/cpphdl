#pragma once
#include "master_slave.hpp"

// Legacy Clause 28 page arbitration for a 1000BASE-T full-duplex-only port.
// tick_ms_in must be a one-cycle 1 ms enable derived from the reference.
// FLP timing/physical pulse detection are separate connected components.
// transmit_burst_in marks a completed burst, after its final pulse falls.
class Autonegotiator : public Module {
public:
    _PORT(u<16>) receive_page_in;
    _PORT(bool) receive_valid_in;
    _PORT(bool) receive_fault_in;
    _PORT(bool) transmit_burst_in;
    _PORT(bool) tick_ms_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) pma_link_in;
    _PORT(u<11>) local_up1_in;
    _PORT(u<11>) entropy_seed_in;
    _PORT(u<16>) transmit_page_out = _ASSIGN_REG(transmit_page);
    _PORT(bool) transmit_enable_out = _ASSIGN(state == 1 || state == 2 || state == 3);
    _PORT(bool) pma_enable_out = _ASSIGN(state == 5 || state == 6);
    _PORT(bool) link_out = _ASSIGN(state == 6);
    _PORT(bool) pause_out = _ASSIGN(state == 6 && remote_pause);
    _PORT(bool) master_out = _ASSIGN((state == 5 || state == 6) && roles.master_out());
    _PORT(bool) fault_out = _ASSIGN(state == 7);
    _PORT(u<3>) reason_out = _ASSIGN_REG(reason);
    _PORT(bool) retry_out = _ASSIGN_REG(retry);

private:
    // 0 break link, 1 ability detect/next page wait, 2 acknowledge detect,
    // 3 complete acknowledge, 5 PMA training, 6 link good, 7 configuration fault.
    reg<u<3>> state{}, stage{}, complete_count{}, role_wait{}, collisions{}, reason{};
    reg<u<11>> milliseconds{}, local_up1{}, local_seed{}, remote_up1{}, remote_seed{};
    reg<u<16>> transmit_page{}, candidate{}, ability{}, ack_word{};
    reg<u<2>> same_count{}, ack_count{};
    reg<u1> remote_pause{};
    reg<u1> remote_np{}, remote_toggle{}, local_toggle{}, retry{};
    MasterSlaveResolver roles;

public:
    void _assign() {
        roles.local_up1_in = _ASSIGN_REG(local_up1);
        roles.remote_up1_in = _ASSIGN_REG(remote_up1);
        roles.local_seed_in = _ASSIGN_REG(local_seed);
        roles.remote_seed_in = _ASSIGN_REG(remote_seed);
        roles.advance_in = _ASSIGN(state == 3 && stage >= 3);
        roles._assign();
    }
    void _work(bool reset) {
        unsigned word, failure, following, toggle, accepted;
        bool wanted, acknowledgement_complete;
        word = unsigned(receive_page_in()) & 49151; // Ignore ACK, retain NP and page identity.
        failure = 0;
        following = 0;
        toggle = 0;
        wanted = false;
        acknowledgement_complete = false;
        accepted = 0;
        retry._next = 0;
        if (tick_ms_in() && milliseconds < 2000)
            milliseconds._next = milliseconds + 1;
        switch (unsigned(state)) {
        case 0:
            // 1300 ms lies within the 1200..1500 ms break-link interval.
            if (tick_ms_in() && milliseconds >= 1299) {
                milliseconds._next = 0;
                local_up1._next = unsigned(local_up1_in()) & 31;
                local_seed._next = entropy_seed_in();
                transmit_page._next = 0x8401; // IEEE selector, NP and symmetric PAUSE; no unsupported 10/100 modes.
                state._next = 1;
                if ((local_up1_in() & 8) == 0)
                    failure = 4;
            }
            break;
        case 1:
            if (receive_fault_in()) {
                same_count._next = 0;
                ack_count._next = 0;
            } else if (receive_valid_in()) {
                wanted = stage == 0 || (((word >> 11) & 1) != unsigned(remote_toggle));
                if (!wanted) {
                    same_count._next = 0;
                    ack_count._next = 0;
                } else {
                    // ACK qualification runs alongside ability qualification:
                    // a peer may already acknowledge its first observed page.
                    ack_word._next = word;
                    ack_count._next = (receive_page_in() & 0x4000) == 0 ? 0
                                      : word == candidate ? unsigned(ack_count) + 1 : 1;
                    if (word == candidate && same_count >= 2) {
                        ability._next = word;
                        transmit_page._next = transmit_page | 0x4000;
                        milliseconds._next = 0;
                        state._next = 2;
                        if (stage == 0 && ((word & 31) != 1 || (word & 0x2000) != 0))
                            failure = 3;
                    } else {
                        candidate._next = word;
                        same_count._next = word == candidate ? unsigned(same_count) + 1 : 1;
                    }
                }
            }
            break;
        case 2:
            if (receive_fault_in())
                ack_count._next = 0;
            else if (ack_count >= 3) {
                accepted = unsigned(ack_word);
                acknowledgement_complete = true;
            } else if (receive_valid_in()) {
                if ((receive_page_in() & 0x4000) == 0)
                    ack_count._next = 0;
                else if (word == ack_word && ack_count >= 2) {
                    accepted = word;
                    acknowledgement_complete = true;
                } else {
                    ack_word._next = word;
                    ack_count._next = word == ack_word ? unsigned(ack_count) + 1 : 1;
                }
            }
            break;
        case 3:
            if (role_wait < 2)
                role_wait._next = role_wait + 1;
            if (stage >= 3 && role_wait >= 2) {
                if (roles.fault_out())
                    failure = 1;
                if (roles.collision_out())
                    failure = 2;
            }
            if (transmit_burst_in()) {
                if (complete_count < 6)
                    complete_count._next = complete_count + 1;
                else if (stage >= 3 && !remote_np) {
                    state._next = 5;
                    milliseconds._next = 0;
                    collisions._next = 0;
                } else {
                    following = stage < 4 ? unsigned(stage) + 1 : 4;
                    toggle = unsigned(local_toggle) ^ 1;
                    local_toggle._next = toggle;
                    stage._next = following;
                    transmit_page._next = following == 1 ? (0xB008 | (toggle << 11))
                                           : following == 2 ? (0x9000 | (toggle << 11) | unsigned(local_up1))
                                           : following == 3 ? (0x1000 | (toggle << 11) | unsigned(local_seed))
                                                            : (0x3001 | (toggle << 11));
                    same_count._next = 0;
                    ack_count._next = 0;
                    candidate._next = 0;
                    ack_word._next = 0;
                    milliseconds._next = 0;
                    state._next = 1;
                }
            }
            break;
        case 5:
            if (pma_link_in()) {
                state._next = 6;
                milliseconds._next = 0;
                reason._next = 0;
            } else if (tick_ms_in() && milliseconds >= 849)
                failure = 5; // 850 ms link-fail inhibit for this 1G-only port.
            break;
        case 6:
            if (!pma_link_in())
                failure = 5;
            break;
        default:
            break;
        }
        if (acknowledgement_complete) {
            if (accepted != ability)
                failure = 3;
            else {
                // Freeze the matched page's NP/toggle. Later received pages
                // cannot change this completion decision.
                remote_np._next = (accepted >> 15) & 1;
                // Base-page bit 11 is asymmetric PAUSE, not the next-page toggle.
                remote_toggle._next = stage == 0 ? 0 : (accepted >> 11) & 1;
                complete_count._next = 0;
                role_wait._next = 0;
                milliseconds._next = 0;
                state._next = 3;
                if (stage == 0) {
                    remote_pause._next = (accepted & 0x0400) != 0;
                    if ((accepted & 0x8000) == 0) failure = 4;
                }
                if (stage == 1 && ((accepted & 0x27FF) != 0x2008 || (accepted & 0x8000) == 0))
                    failure = 4;
                if (stage == 2) {
                    remote_up1._next = accepted & 2047;
                    if ((accepted & 0x2008) != 8 || (accepted & 0x8000) == 0)
                        failure = 4;
                }
                if (stage == 3) {
                    remote_seed._next = accepted & 2047;
                    if ((accepted & 0x2000) != 0)
                        failure = 4;
                }
            }
        }
        if (state >= 1 && state <= 3 && tick_ms_in() && milliseconds >= 1399)
            failure = 6;
        if (failure != 0) {
            reason._next = failure;
            retry._next = 1;
            state._next = 0;
            stage._next = 0;
            milliseconds._next = 0;
            complete_count._next = 0;
            role_wait._next = 0;
            same_count._next = 0;
            ack_count._next = 0;
            candidate._next = 0;
            ability._next = 0;
            ack_word._next = 0;
            remote_pause._next = 0;
            remote_np._next = 0;
            remote_toggle._next = 0;
            local_toggle._next = 0;
            transmit_page._next = 0x8401;
            if (failure == 2) {
                collisions._next = collisions + 1;
                if (collisions >= 6)
                    state._next = 7;
            } else {
                collisions._next = 0;
                if (failure == 1)
                    state._next = 7;
            }
        }
        roles._work(reset || restart_in() || !enable_in());
        if (reset || restart_in() || !enable_in()) {
            state._next = 0;
            stage._next = 0;
            complete_count._next = 0;
            role_wait._next = 0;
            collisions._next = 0;
            reason._next = 0;
            milliseconds._next = 0;
            local_up1._next = 0;
            local_seed._next = 0;
            remote_up1._next = 0;
            remote_seed._next = 0;
            transmit_page._next = 0x8401;
            candidate._next = 0;
            ability._next = 0;
            ack_word._next = 0;
            same_count._next = 0;
            ack_count._next = 0;
            remote_pause._next = 0;
            remote_np._next = 0;
            remote_toggle._next = 0;
            local_toggle._next = 0;
            retry._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        stage.strobe();
        complete_count.strobe();
        role_wait.strobe();
        collisions.strobe();
        reason.strobe();
        milliseconds.strobe();
        local_up1.strobe();
        local_seed.strobe();
        remote_up1.strobe();
        remote_seed.strobe();
        transmit_page.strobe();
        candidate.strobe();
        ability.strobe();
        ack_word.strobe();
        same_count.strobe();
        ack_count.strobe();
        remote_pause.strobe();
        remote_np.strobe();
        remote_toggle.strobe();
        local_toggle.strobe();
        retry.strobe();
        roles._strobe();
    }
};

#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Acquire a remote Clause 40 side-stream from idle/training symbols, without
// importing its seed. In idle D0, |A|/2 is Scr[0]. Thirty-three observations
// reconstruct the state; subsequent symbols must satisfy its recurrence,
// odd/even idle coding and consistent signs on all four pairs before lock.
// The PMA must supply deskewed pairs in A/B/C/D order and flag invalid timing.
class PCSReceiveSynchronizer : public Module {
public:
    _PORT(u<12>) symbols_in;
    _PORT(bool) remote_master_in;
    _PORT(bool) pma_ok_in;
    // The framer asserts this only for an observation classified as idle;
    // delimiter/data observations must not be checked against idle coding.
    _PORT(bool) check_idle_in;
    _PORT(bool) restart_in;
    _PORT(bool) locked_out = _ASSIGN_REG(locked);
    _PORT(u<33>) state_out = _ASSIGN_REG(state);
    _PORT(u<4>) sx_out = _ASSIGN_REG(sx_reg);
    _PORT(u<4>) sy_out = _ASSIGN_REG(sy_reg);
    _PORT(u<4>) sg_out = _ASSIGN_REG(sg_reg);
    _PORT(u<4>) low_mask_out = _ASSIGN_REG(low_mask);
    _PORT(u<4>) polarity_out = _ASSIGN_REG(polarity);
    _PORT(bool) remote_receiver_ok_out = _ASSIGN_REG(remote_ok);

private:
    reg<u<33>> state{};
    reg<u<6>> collected{};
    reg<u<7>> qualified{};
    reg<u<2>> phase_candidates{};
    reg<u<4>> known_polarity{}, polarity{};
    reg<u<4>> sx_reg{}, sy_reg{}, sg_reg{}, low_mask{};
    reg<u1> locked{}, odd{}, remote_ok{};

public:
    void _assign() {}
    void _work(bool reset) {
        u<33> current, predicted;
        unsigned sy, sx, sg, previous_sy, raw, point, found_polarity, found_known;
        unsigned candidates, even_mask, odd_mask, mask;
        bool idle, consistent, bit, phase0, phase1;
        predicted = (state << 1) | (((state >> 32) ^ (state >> (remote_master_in() ? 12 : 19))) & 1);
        raw = unsigned(symbols_in()) & 7;
        bit = raw == 0 || raw == 4;
        current = locked ? predicted : u<33>((state << 1) | unsigned(bit));
        state._next = current;
        odd._next = !odd;
        sy = unsigned(current & 1);
        sy = sy | ((unsigned((current >> 3) ^ (current >> 8)) & 1) << 1);
        sy = sy | ((unsigned((current >> 6) ^ (current >> 16)) & 1) << 2);
        sy = sy | ((unsigned((current >> 9) ^ (current >> 14) ^ (current >> 19) ^ (current >> 24)) & 1) << 3);
        sx = unsigned((current >> 4) ^ (current >> 6)) & 1;
        sx = sx | ((unsigned((current >> 7) ^ (current >> 9) ^ (current >> 12) ^ (current >> 14)) & 1) << 1);
        sx = sx | ((unsigned((current >> 10) ^ (current >> 12) ^ (current >> 20) ^ (current >> 22)) & 1) << 2);
        sx = sx | ((unsigned((current >> 13) ^ (current >> 15) ^ (current >> 18) ^ (current >> 20) ^ (current >> 23) ^
                             (current >> 25) ^ (current >> 28) ^ (current >> 30)) &
                    1)
                   << 3);
        sg = unsigned((current >> 1) ^ (current >> 5)) & 1;
        sg = sg | ((unsigned((current >> 4) ^ (current >> 8) ^ (current >> 9) ^ (current >> 13)) & 1) << 1);
        sg = sg | ((unsigned((current >> 7) ^ (current >> 11) ^ (current >> 17) ^ (current >> 21)) & 1) << 2);
        sg = sg | ((unsigned((current >> 10) ^ (current >> 14) ^ (current >> 15) ^ (current >> 19) ^ (current >> 20) ^
                             (current >> 24) ^ (current >> 25) ^ (current >> 29)) &
                    1)
                   << 3);
        previous_sy = unsigned(state & 1);
        previous_sy = previous_sy | ((unsigned((state >> 3) ^ (state >> 8)) & 1) << 1);
        previous_sy = previous_sy | ((unsigned((state >> 6) ^ (state >> 16)) & 1) << 2);
        previous_sy = previous_sy | ((unsigned((state >> 9) ^ (state >> 14) ^ (state >> 19) ^ (state >> 24)) & 1) << 3);
        sx_reg._next = sx;
        sy_reg._next = sy;
        sg_reg._next = sg;
        even_mask = sy;
        odd_mask = ((previous_sy ^ 14) & 14) | (sy & 1);
        // A candidate bit identifies the parity of the first local observation.
        mask = (bool(odd) ^ (phase_candidates == 2)) ? odd_mask : even_mask;
        low_mask._next = mask;
        idle = true;
        point = 0;
        found_polarity = unsigned(polarity);
        found_known = unsigned(known_polarity);
        consistent = true;
        for (unsigned lane = 0; lane < 4; ++lane) {
            raw = (unsigned(symbols_in()) >> (3 * lane)) & 7;
            if (raw != 0 && raw != 2 && raw != 4)
                idle = false;
            if (raw == 0 || raw == 4) {
                point = point | (1 << lane);
                bit = (raw == 4) != (((sg >> lane) & 1) != 0);
                if ((known_polarity & (1 << lane)) && bit != (((polarity >> lane) & 1) != 0))
                    consistent = false;
                found_polarity = (found_polarity & ~(1 << lane)) | (unsigned(bit) << lane);
                found_known = found_known | (1 << lane);
            }
        }
        if (!locked) {
            if (collected < 33)
                collected._next = collected + 1;
            if (collected >= 33) {
                phase0 = (point & 10) == ((odd ? odd_mask : even_mask) & 10);
                phase1 = (point & 10) == ((odd ? even_mask : odd_mask) & 10);
                candidates = unsigned(phase_candidates) & (unsigned(phase0) | (unsigned(phase1) << 1));
                if (!idle || !consistent || current == 0 || (current & 1) != (predicted & 1) || candidates == 0) {
                    qualified._next = 0;
                    phase_candidates._next = 3;
                    known_polarity._next = 0;
                } else {
                    phase_candidates._next = candidates;
                    known_polarity._next = found_known;
                    polarity._next = found_polarity;
                    if (qualified < 64)
                        qualified._next = qualified + 1;
                    if (qualified >= 63 && found_known == 15 && (candidates == 1 || candidates == 2))
                        locked._next = 1;
                }
            }
        } else if (check_idle_in()) {
            // During data, the framer monitors code groups. During known idle,
            // check the actual stream; a stale lock cannot follow a changed seed.
            if (!idle || !consistent || (point & 11) != (mask & 11)) {
                locked._next = 0;
                collected._next = 0;
                qualified._next = 0;
                phase_candidates._next = 3;
                known_polarity._next = 0;
            } else {
                remote_ok._next = ((point ^ mask) & 4) != 0;
            }
        }
        if (!pma_ok_in() || restart_in() || reset) {
            state._next = 0;
            collected._next = 0;
            qualified._next = 0;
            phase_candidates._next = 3;
            known_polarity._next = 0;
            polarity._next = 0;
            sx_reg._next = 0;
            sy_reg._next = 0;
            sg_reg._next = 0;
            low_mask._next = 0;
            locked._next = 0;
            odd._next = 0;
            remote_ok._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        collected.strobe();
        qualified.strobe();
        phase_candidates.strobe();
        known_polarity.strobe();
        polarity.strobe();
        sx_reg.strobe();
        sy_reg.strobe();
        sg_reg.strobe();
        low_mask.strobe();
        locked.strobe();
        odd.strobe();
        remote_ok.strobe();
    }
};

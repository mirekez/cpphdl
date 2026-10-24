#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Identify and align cable pairs from received 3-level training, without a
// transmit seed or a configured cable permutation. Input counts use the PCS
// convention: 128 is zero and one PAM level is 32 counts. Analog gain, timing
// and equalization must establish that convention before signal_ok_in.
class ReceivePairAcquirer : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) signal_ok_in;
    _PORT(bool) advance_in;
    _PORT(bool) remote_master_in;
    _PORT(u<32>) samples_out = _ASSIGN_REG(output);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) locked_out = _ASSIGN(stage == 3);
    _PORT(bool) ambiguous_out = _ASSIGN_REG(ambiguous);
    _PORT(u<2>) state_out = _ASSIGN_REG(stage);
    _PORT(u<8>) mapping_out = _ASSIGN(unsigned(mapping[0]) | (unsigned(mapping[1]) << 2)
        | (unsigned(mapping[2]) << 4) | (unsigned(mapping[3]) << 6));
    _PORT(u<20>) delays_out = _ASSIGN(unsigned(delays[0]) | (unsigned(delays[1]) << 5)
        | (unsigned(delays[2]) << 10) | (unsigned(delays[3]) << 15));
private:
    reg<u<32>> samples[17]{}, output{};
    reg<u<64>> magnitude[4]{}, training_valid[4]{};
    reg<u<8>> qualified[4]{}, consecutive{};
    reg<u<6>> collected{};
    reg<u<2>> stage{}, pair_a{}, test_physical{}, test_logical{};
    reg<u<5>> test_delay{}, delays[4]{};
    reg<u<2>> mapping[4]{};
    reg<u1> phase{}, test_phase{}, test_invert{}, role{}, role_valid{}, valid{}, ambiguous{};
    reg<u<2>> matches[2][3]{}, candidate_mapping[2][3]{};
    reg<u<5>> candidate_delays[2][3]{};

    unsigned side_sequence(unsigned value) {
        return (value & 1) | ((((value >> 3) ^ (value >> 8)) & 1) << 1)
            | ((((value >> 6) ^ (value >> 16)) & 1) << 2)
            | ((((value >> 9) ^ (value >> 14) ^ (value >> 19) ^ (value >> 24)) & 1) << 3);
    }
    void next_candidate() {
        consecutive._next = 0;
        if (test_delay != 16) {
            test_delay._next = test_delay + 1;
        } else {
            test_delay._next = 0;
            if (test_physical != 3) {
                test_physical._next = test_physical + 1;
            } else {
                test_physical._next = 0;
                if (test_logical != 2) {
                    test_logical._next = test_logical + 1;
                } else {
                    test_logical._next = 0;
                    if (!test_phase)
                        test_phase._next = 1;
                    else
                        stage._next = 2;
                }
            }
        }
    }
public:
    void _assign() {}
    void _work(bool reset) {
        unsigned found, selected, active, raw, current, previous, expected, observed, hypotheses, winner, packed;
        int centered;
        bool legal, consistent, polarity, unique, anchor_ok;
        u<33> history;
        found = 0;
        selected = 0;
        anchor_ok = true;
        valid._next = 0;
        if (!role_valid && enable_in()) {
            role._next = remote_master_in();
            role_valid._next = 1;
        }
        if (advance_in()) {
            phase._next = !phase;
            samples[0]._next = samples_in();
            for (unsigned i = 1; i < 17; ++i)
                samples[i]._next = samples[i - 1];
            if (collected != 63)
                collected._next = collected + 1;
            for (unsigned p = 0; p < 4; ++p) {
                centered = int((unsigned(samples_in()) >> (8 * p)) & 255) - 128;
                active = unsigned(centered <= -48 || centered >= 48);
                legal = (centered >= -12 && centered <= 12) || (centered >= 52 && centered <= 76)
                    || (centered >= -76 && centered <= -52);
                history = (magnitude[p] << 1) | active;
                consistent = legal && history != 0 && active == unsigned(((magnitude[p] >> 32)
                    ^ (magnitude[p] >> (role ? 12 : 19))) & 1);
                magnitude[p]._next = (magnitude[p] << 1) | active;
                training_valid[p]._next = (training_valid[p] << 1) | unsigned(legal);
                if (stage == 0) {
                    if (collected >= 33 && consistent) {
                        if (qualified[p] != 128)
                            qualified[p]._next = qualified[p] + 1;
                        if (qualified[p] >= 127) {
                            ++found;
                            selected = p;
                        }
                    } else {
                        qualified[p]._next = 0;
                    }
                } else if (stage != 3 && p == pair_a && !consistent)
                    anchor_ok = false;
            }
            if (stage == 0 && found == 1) {
                pair_a._next = selected;
                stage._next = 1;
                test_phase._next = 0;
                test_logical._next = 0;
                test_physical._next = 0;
                test_delay._next = 0;
                consecutive._next = 0;
                for (unsigned p = 0; p < 2; ++p)
                    for (unsigned j = 0; j < 3; ++j)
                        matches[p][j]._next = 0;
            } else if (stage == 1) {
                // Search serially to avoid hundreds of parallel correlators.
                // The A anchor looks back eight symbols; taps 0..16 cover
                // relative pair delays of +/-64 ns at 125 MBd.
                current = side_sequence(unsigned(magnitude[unsigned(pair_a)] >> 8));
                previous = side_sequence(unsigned(magnitude[unsigned(pair_a)] >> 9));
                raw = (bool(phase) ^ bool(test_phase)) ? (((previous ^ 14) & 14) | (current & 1)) : current;
                expected = (raw >> (unsigned(test_logical) + 1)) & 1;
                observed = unsigned((magnitude[unsigned(test_physical)] >> unsigned(test_delay)) & 1);
                legal = ((training_valid[unsigned(test_physical)] >> unsigned(test_delay)) & 1) != 0;
                polarity = bool(expected ^ observed);
                // Pair C also carries receiver readiness: allow either fixed
                // complement while qualifying its delay, without using signs.
                consistent = legal && (test_logical == 1 ? (consecutive == 0 || polarity == bool(test_invert)) : !polarity);
                if (test_physical == pair_a || !consistent) {
                    next_candidate();
                } else {
                    if (consecutive == 0)
                        test_invert._next = polarity;
                    if (consecutive == 127) {
                        if (matches[unsigned(test_phase)][unsigned(test_logical)] != 2)
                            matches[unsigned(test_phase)][unsigned(test_logical)]._next =
                                matches[unsigned(test_phase)][unsigned(test_logical)] + 1;
                        candidate_mapping[unsigned(test_phase)][unsigned(test_logical)]._next = test_physical;
                        candidate_delays[unsigned(test_phase)][unsigned(test_logical)]._next = test_delay;
                        next_candidate();
                    } else {
                        consecutive._next = consecutive + 1;
                    }
                }
            } else if (stage == 2) {
                hypotheses = 0;
                winner = 0;
                for (unsigned p = 0; p < 2; ++p) {
                    unique = matches[p][0] == 1 && matches[p][1] == 1 && matches[p][2] == 1
                        && candidate_mapping[p][0] != candidate_mapping[p][1]
                        && candidate_mapping[p][0] != candidate_mapping[p][2]
                        && candidate_mapping[p][1] != candidate_mapping[p][2];
                    if (unique) {
                        ++hypotheses;
                        winner = p;
                    }
                }
                if (hypotheses == 1) {
                    mapping[0]._next = pair_a;
                    delays[0]._next = 8;
                    for (unsigned j = 1; j < 4; ++j) {
                        mapping[j]._next = candidate_mapping[winner][j - 1];
                        delays[j]._next = candidate_delays[winner][j - 1];
                    }
                    stage._next = 3;
                    ambiguous._next = 0;
                } else {
                    stage._next = 1;
                    ambiguous._next = hypotheses > 1;
                    test_phase._next = 0;
                    test_logical._next = 0;
                    test_physical._next = 0;
                    test_delay._next = 0;
                    consecutive._next = 0;
                    for (unsigned p = 0; p < 2; ++p)
                        for (unsigned j = 0; j < 3; ++j)
                            matches[p][j]._next = 0;
                }
            } else if (stage == 3) {
                packed = 0;
                for (unsigned j = 0; j < 4; ++j)
                    packed |= ((unsigned(samples[unsigned(delays[j])]) >> (8 * unsigned(mapping[j]))) & 255) << (8 * j);
                output._next = packed;
                valid._next = 1;
            }
            if (stage != 0 && stage != 3 && !anchor_ok) {
                stage._next = 0;
                for (unsigned p = 0; p < 4; ++p)
                    qualified[p]._next = 0;
            }
        }
        if (reset || !enable_in() || restart_in() || !signal_ok_in()) {
            stage._next = 0;
            collected._next = 0;
            phase._next = 0;
            role_valid._next = 0;
            valid._next = 0;
            ambiguous._next = 0;
            output._next = 0;
            consecutive._next = 0;
            for (unsigned p = 0; p < 4; ++p) {
                qualified[p]._next = 0;
                magnitude[p]._next = 0;
                training_valid[p]._next = 0;
                mapping[p]._next = 0;
                delays[p]._next = 0;
            }
            for (unsigned p = 0; p < 2; ++p)
                for (unsigned j = 0; j < 3; ++j) {
                    matches[p][j]._next = 0;
                    candidate_mapping[p][j]._next = 0;
                    candidate_delays[p][j]._next = 0;
                }
            for (unsigned i = 0; i < 17; ++i)
                samples[i]._next = 0;
        }
    }
    void _strobe() {
        output.strobe();
        collected.strobe();
        stage.strobe();
        pair_a.strobe();
        consecutive.strobe();
        phase.strobe();
        test_phase.strobe();
        test_physical.strobe();
        test_logical.strobe();
        test_delay.strobe();
        test_invert.strobe();
        role.strobe();
        role_valid.strobe();
        valid.strobe();
        ambiguous.strobe();
        for (unsigned p = 0; p < 4; ++p) {
            magnitude[p].strobe();
            training_valid[p].strobe();
            qualified[p].strobe();
            mapping[p].strobe();
            delays[p].strobe();
        }
        for (unsigned p = 0; p < 2; ++p)
            for (unsigned j = 0; j < 3; ++j) {
                matches[p][j].strobe();
                candidate_mapping[p][j].strobe();
                candidate_delays[p][j].strobe();
            }
        for (unsigned i = 0; i < 17; ++i)
            samples[i].strobe();
    }
};

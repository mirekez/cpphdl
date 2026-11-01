#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Nearest normal-data point in every 4D-PAM5 subset. Each ADC sample is an
// unsigned byte: 128 = zero, and 32 counts = one PAM level. The caller removes
// the acquired per-pair scrambler signs before trellis decoding.
class PCSBranchMetrics : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<4>) negate_in;
    _PORT(bool) advance_in;
    _PORT(u<18>) metric_out[8];
    _PORT(u<6>) point_out[8];
    _PORT(bool) valid_out = _ASSIGN_REG(valid_reg);

private:
    reg<u<18>> metrics[8]{};
    reg<u<6>> points[8]{};
    reg<u1> valid_reg{};

    // Six Cartesian subconstellations partition the 64 points in each subset.
    // Groups 0/1 each contain 16 points. Groups 2..5 each contain eight points
    // with a fixed +2 on A, C, B or D respectively. Other coordinates have two
    // choices separated by two levels. Codes here use symbol + 2, A in bit 0.
    unsigned base(unsigned index) {
        switch (index) {
        case 0:
            return 1170;
        case 6:
            return 1682;
        case 12:
            return 1746;
        case 18:
            return 1234;
        case 24:
            return 1242;
        case 30:
            return 1754;
        case 36:
            return 1690;
        case 42:
            return 1178;
        case 1:
            return 1755;
        case 7:
            return 1243;
        case 13:
            return 1179;
        case 19:
            return 1691;
        case 25:
            return 1683;
        case 31:
            return 1171;
        case 37:
            return 1235;
        case 43:
            return 1747;
        case 2:
            return 1172;
        case 8:
            return 1684;
        case 14:
            return 1748;
        case 20:
            return 1236;
        case 26:
            return 1244;
        case 32:
            return 1756;
        case 38:
            return 1692;
        case 44:
            return 1180;
        case 3:
            return 1298;
        case 9:
            return 1810;
        case 15:
            return 1307;
        case 21:
            return 1819;
        case 27:
            return 1811;
        case 33:
            return 1299;
        case 39:
            return 1818;
        case 45:
            return 1306;
        case 4:
            return 1186;
        case 10:
            return 1698;
        case 16:
            return 1762;
        case 22:
            return 1250;
        case 28:
            return 1699;
        case 34:
            return 1187;
        case 40:
            return 1251;
        case 46:
            return 1763;
        case 5:
            return 2194;
        case 11:
            return 2267;
        case 17:
            return 2203;
        case 23:
            return 2258;
        case 29:
            return 2266;
        case 35:
            return 2195;
        case 41:
            return 2259;
        case 47:
            return 2202;
        default:
            return 0;
        }
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 8; ++i) {
            metric_out[i] = _ASSIGN_REG_I(metrics[i]);
            point_out[i] = _ASSIGN_REG_I(points[i]);
        }
    }
    void _work(bool reset) {
        u<16> squared[20];
        int sample, difference;
        u<8> distance;
        unsigned packed, level, cost, alternative, group_cost, group_point, bit, fixed;
        unsigned best_cost, best_point;
        valid_reg._next = advance_in();
        for (unsigned lane = 0; lane < 4; ++lane) {
            sample = int((unsigned(samples_in()) >> (8 * lane)) & 255) - 128;
            if ((negate_in() >> lane) & 1)
                sample = -sample;
            for (unsigned code = 0; code < 5; ++code) {
                difference = sample - (int(code) - 2) * 32;
                distance = unsigned(difference < 0 ? -difference : difference);
                squared[lane * 5 + code] = unsigned(distance) * unsigned(distance);
            }
        }
        for (unsigned subset = 0; subset < 8; ++subset) {
            best_cost = 262143;
            best_point = 0;
            for (unsigned group = 0; group < 6; ++group) {
                packed = base(subset * 6 + group);
                fixed = group == 2 ? 0 : group == 3 ? 2 : group == 4 ? 1 : group == 5 ? 3 : 4;
                group_point = group == 0 ? 0 : group == 1 ? 16 : 32 + (group - 2) * 8;
                group_cost = 0;
                bit = 0;
                for (unsigned lane = 0; lane < 4; ++lane) {
                    level = (packed >> (3 * lane)) & 7;
                    cost = squared[lane * 5 + level];
                    if (lane != fixed) {
                        alternative = squared[lane * 5 + level - 2];
                        if (alternative < cost) {
                            cost = alternative;
                            group_point = group_point | (1 << bit);
                        }
                        bit = bit + 1;
                    }
                    group_cost = group_cost + cost;
                }
                if (group_cost < best_cost) {
                    best_cost = group_cost;
                    best_point = group_point;
                }
            }
            if (advance_in()) {
                metrics[subset]._next = best_cost;
                points[subset]._next = best_point;
            }
            if (reset) {
                metrics[subset]._next = 0;
                points[subset]._next = 0;
            }
        }
        if (reset)
            valid_reg._next = 0;
    }
    void _strobe() {
        for (unsigned i = 0; i < 8; ++i) {
            metrics[i].strobe();
            points[i].strobe();
        }
        valid_reg.strobe();
    }
};

// Eight-state, rate-2/3 Clause 40 trellis decoder. Low six bits select the
// closest point in a subset; the two coded bits select its branch. Survivor
// registers retain 32 symbols. Circular metrics avoid global-min subtraction
// on the recurrence path; balanced selection preserves predecessor tie order.
// start_in initializes a new data stream at known convolutional state zero.
class PCSTrellisDecoder : public Module {
    static constexpr unsigned DEPTH = 32;

public:
    // Hold the entire symbol pipeline between recovered observations.
    _PORT(bool) symbol_enable_in;
    _PORT(u<18>) metric_in[8];
    _PORT(u<6>) point_in[8];
    _PORT(bool) advance_in;
    _PORT(bool) start_in;
    // Assert with the second encoder-reset symbol: the known ending state is
    // zero. Remaining decisions drain at one byte per symbol clock, while the
    // next frame can independently start accumulating its survivor paths.
    _PORT(bool) end_in;
    _PORT(u<8>) data_out = _ASSIGN_REG(data_reg);
    _PORT(bool) valid_out = _ASSIGN_REG(valid_reg);
    _PORT(u<3>) state_out = _ASSIGN_REG(state_reg);

private:
    reg<u<21>> costs[8]{};
    reg<u<8>> reachable{};
    reg<u<2>> selectors[8]{};
    reg<u<8>> selected_bytes[8]{};
    reg<u<3>> pending_control{};
    reg<u<6>> pending_count{};
    reg<logic<DEPTH * 8>> survivors[8]{};
    reg<u<6>> count{};
    reg<u<8>> data_reg{};
    reg<u<3>> state_reg{};
    reg<u1> valid_reg{};
    reg<logic<DEPTH * 8>> drain{};
    reg<u<6>> drain_left{};
    reg<u<DEPTH - 1>> drain_valid{};

    // Any state reaches every state in two symbols. With 18-bit branch metrics,
    // reachable costs span at most 2*(2^18-1), and one-step candidates at most
    // 3*(2^18-1). Both are below half the 21-bit circular range. A signed modular
    // difference therefore orders them exactly, even after repeated wraps.
    bool better(unsigned candidate, unsigned current, bool candidate_valid, bool current_valid) {
        bool lower_less;
        unsigned a, b;
        lower_less = false;
        a = 0;
        b = 0;
        // Parallel five-bit comparisons avoid a wide subtraction carry chain.
        for (unsigned group = 0; group < 4; ++group) {
            a = (candidate >> (group * 5)) & 31;
            b = (current >> (group * 5)) & 31;
            lower_less = a < b || (a == b && lower_less);
        }
        return candidate_valid && (!current_valid || ((((candidate ^ current) >> 20) & 1) != unsigned(lower_less)));
    }

    unsigned add_metric(unsigned path, unsigned branch) {
        // Parallel prefix carries compute the exact 21-bit modular sum.
        unsigned initial, propagate, generate;
        initial = path ^ branch;
        propagate = initial;
        generate = path & branch;
        for (unsigned shift = 1; shift < 21; shift = shift * 2) {
            generate = generate | (propagate & (generate << shift));
            propagate = propagate & (propagate << shift);
        }
        return (initial ^ (generate << 1)) & 2097151;
    }

    bool branch_better(unsigned a, unsigned b, unsigned c, unsigned d,
                       bool candidate_valid, bool current_valid) {
        // Carry-save reduction of a + b - c - d, followed by one prefix sum.
        // The 21-bit circular bound is the same as for candidate metrics.
        unsigned x, y, z, sum, carry;
        x = a;
        y = b;
        z = ~c;
        sum = x ^ y ^ z;
        carry = ((x & y) | (x & z) | (y & z)) << 1;
        x = sum;
        y = carry;
        z = ~d;
        sum = x ^ y ^ z;
        carry = ((x & y) | (x & z) | (y & z)) << 1;
        x = sum;
        y = carry;
        z = 2;
        sum = x ^ y ^ z;
        carry = ((x & y) | (x & z) | (y & z)) << 1;
        return candidate_valid && (!current_valid || ((add_metric(sum, carry) >> 20) & 1) != 0);
    }

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned next_cost[8];
        logic<DEPTH * 8> next_survivor[8];
        unsigned candidates[4], candidate_bytes[4], branch[4];
        bool candidate_valid[4];
        unsigned cost2[2], state2[2];
        bool valid2[2], outranks[64], is_best;
        logic<DEPTH * 8> pending, byte_mask, scheduled_byte;
        u<DEPTH - 1> pending_valid;
        unsigned subset, best_state, sd6, sd7, winning_byte, next_reachable;
        unsigned predecessor;
        bool win01, win02, win03, win12, win13, win23, choose0, choose1, choose2, choose3;
        u<2> winner;
        // The converter emits a clocked task. Initialize every temporary on
        // every invocation so a held input cannot infer persistent task storage.
        is_best = false;
        for (unsigned i = 0; i < 64; ++i)
            outranks[i] = false;
        subset = 0;
        best_state = 0;
        sd6 = 0;
        sd7 = 0;
        winner = 0;
        winning_byte = 0;
        byte_mask = 0;
        scheduled_byte = 0;
        next_reachable = 0;
        predecessor = 0;
        win01 = false;
        win02 = false;
        win03 = false;
        win12 = false;
        win13 = false;
        win23 = false;
        choose0 = false;
        choose1 = false;
        choose2 = false;
        choose3 = false;
        for (unsigned i = 0; i < 4; ++i) {
            candidates[i] = 0;
            branch[i] = 0;
            candidate_bytes[i] = 0;
            candidate_valid[i] = false;
        }
        for (unsigned i = 0; i < 2; ++i) {
            cost2[i] = 0;
            state2[i] = 0;
            valid2[i] = false;
        }
        for (unsigned state = 0; state < 8; ++state) {
            next_cost[state] = 0;
            next_survivor[state] = 0;
        }
        valid_reg._next = 0;
        pending_control._next = ((advance_in() && !start_in()) ? 1 : 0) |
                                (start_in() ? 2 : 0) | (end_in() ? 4 : 0);
        pending_count._next = count;
        pending = drain << 8;
        pending_valid = drain_valid << 1;
        if (drain_left != 0) {
            data_reg._next = unsigned(drain >> ((DEPTH - 1) * 8));
            valid_reg._next = ((drain_valid >> (DEPTH - 2)) & 1) != 0;
            drain._next = pending;
            drain_valid._next = pending_valid;
            drain_left._next = drain_left - 1;
        }
        if (advance_in()) {
            for (unsigned destination = 0; destination < 8; ++destination) {
                winner = 0;
                winning_byte = 0;
                // Each next state has four possible predecessors. Keeping each
                // destination's winner local avoids dynamic writes to a large
                // survivor array in RTL synthesis.
                for (unsigned choice = 0; choice < 4; ++choice) {
                    predecessor = (destination & 1) * 4 + choice;
                    sd6 = ((destination >> 1) ^ predecessor) & 1;
                    sd7 = ((destination >> 2) ^ (predecessor >> 1)) & 1;
                    subset = sd6 * 4 + sd7 * 2 + (destination & 1);
                    branch[choice] = unsigned(metric_in[subset]());
                    candidates[choice] = add_metric(unsigned(costs[(destination & 1) * 4 + choice]),
                                                    unsigned(metric_in[subset]()));
                    candidate_valid[choice] = ((unsigned(reachable) >> ((destination & 1) * 4 + choice)) & 1) != 0;
                    candidate_bytes[choice] = unsigned(point_in[subset]()) | (sd6 << 6) | (sd7 << 7);
                }
                // Six comparisons run in parallel. The extra comparators
                // remove a second serial comparison from the metric recurrence.
                win01 = branch_better(unsigned(costs[(destination & 1) * 4 + 1]), branch[1],
                                      unsigned(costs[(destination & 1) * 4 + 0]), branch[0],
                                      candidate_valid[1], candidate_valid[0]);
                win02 = branch_better(unsigned(costs[(destination & 1) * 4 + 2]), branch[2],
                                      unsigned(costs[(destination & 1) * 4 + 0]), branch[0],
                                      candidate_valid[2], candidate_valid[0]);
                win03 = branch_better(unsigned(costs[(destination & 1) * 4 + 3]), branch[3],
                                      unsigned(costs[(destination & 1) * 4 + 0]), branch[0],
                                      candidate_valid[3], candidate_valid[0]);
                win12 = branch_better(unsigned(costs[(destination & 1) * 4 + 2]), branch[2],
                                      unsigned(costs[(destination & 1) * 4 + 1]), branch[1],
                                      candidate_valid[2], candidate_valid[1]);
                win13 = branch_better(unsigned(costs[(destination & 1) * 4 + 3]), branch[3],
                                      unsigned(costs[(destination & 1) * 4 + 1]), branch[1],
                                      candidate_valid[3], candidate_valid[1]);
                win23 = branch_better(unsigned(costs[(destination & 1) * 4 + 3]), branch[3],
                                      unsigned(costs[(destination & 1) * 4 + 2]), branch[2],
                                      candidate_valid[3], candidate_valid[2]);
                choose1 = win01 && !win12 && !win13;
                choose2 = win02 && win12 && !win23;
                choose3 = win03 && win13 && win23;
                winner = (choose1 || choose3 ? 1 : 0) | (choose2 || choose3 ? 2 : 0);
                choose0 = !win01 && !win02 && !win03;
                next_cost[destination] = (candidates[0] & (0 - unsigned(choose0))) |
                                         (candidates[1] & (0 - unsigned(choose1))) |
                                         (candidates[2] & (0 - unsigned(choose2))) |
                                         (candidates[3] & (0 - unsigned(choose3)));
                winning_byte = candidate_bytes[unsigned(winner)];
                if (candidate_valid[unsigned(winner)])
                    next_reachable = next_reachable | (1 << destination);
                // A registered choice separates ACS from the large survivor
                // mux fanout. Survivor history trails the metric by one clock.
                selectors[destination]._next = winner;
                selected_bytes[destination]._next = winning_byte;
            }
            reachable._next = next_reachable;
            for (unsigned state = 0; state < 8; ++state) {
                costs[state]._next = next_cost[state];
            }
            if (count < DEPTH)
                count._next = count + 1;
        }
        // Select a decision from the registered survivor set, one clock after
        // ACS. The recurrence no longer crosses the global best-state tree.
        if ((pending_control & 1) != 0) {
            for (unsigned destination = 0; destination < 8; ++destination) {
                next_survivor[destination] =
                    (survivors[(destination & 1) * 4 + unsigned(selectors[destination])] << 8) |
                    unsigned(selected_bytes[destination]);
                survivors[destination]._next = next_survivor[destination];
            }
            // Compare each unordered pair once; lowest state wins a tie.
            // No selected-cost mux or second comparator lies on this path.
            for (unsigned i = 0; i < 8; ++i) {
                for (unsigned j = 0; j < 8; ++j) {
                    if (i < j)
                        outranks[i * 8 + j] = better(unsigned(costs[j]), unsigned(costs[i]),
                            ((reachable >> j) & 1) != 0, ((reachable >> i) & 1) != 0);
                }
            }
            best_state = 0;
            for (unsigned i = 0; i < 8; ++i) {
                is_best = ((reachable >> i) & 1) != 0;
                for (unsigned j = 0; j < 8; ++j) {
                    if (j < i)
                        is_best = is_best && outranks[j * 8 + i];
                    if (j > i)
                        is_best = is_best && !outranks[i * 8 + j];
                }
                if (is_best)
                    best_state = best_state | i;
            }
            if (pending_count >= DEPTH - 1 || drain_left == 0) {
                valid_reg._next = pending_count >= DEPTH - 1;
                data_reg._next = unsigned(next_survivor[best_state] >> ((DEPTH - 1) * 8));
            }
            state_reg._next = best_state;
            if ((pending_control & 4) != 0) {
                // Schedule only actual decisions. Leading padding in a short
                // stream must not overwrite an earlier frame's pending tail.
                for (unsigned future = 0; future < DEPTH - 1; ++future) {
                    if (pending_count >= DEPTH - 2 - future) {
                        byte_mask = 255;
                        byte_mask = byte_mask << ((DEPTH - 1 - future) * 8);
                        scheduled_byte = unsigned(next_survivor[0] >> ((DEPTH - 2 - future) * 8)) & 255;
                        pending = (pending & ~byte_mask) | (scheduled_byte << ((DEPTH - 1 - future) * 8));
                        pending_valid = pending_valid | (1 << (DEPTH - 2 - future));
                    }
                }
                drain._next = pending;
                drain_valid._next = pending_valid;
                drain_left._next = DEPTH - 1;
                if (pending_count >= DEPTH - 1 || drain_left == 0) {
                    valid_reg._next = pending_count >= DEPTH - 1;
                    data_reg._next = unsigned(next_survivor[0] >> ((DEPTH - 1) * 8));
                }
                state_reg._next = 0;
            }
        }
        if (start_in() || reset) {
            for (unsigned state = 0; state < 8; ++state) {
                costs[state]._next = 0;
                survivors[state]._next = 0;
                selectors[state]._next = 0;
                selected_bytes[state]._next = 0;
            }
            count._next = 0;
            reachable._next = 1;
        }
        if ((pending_control & 2) != 0 || reset) {
            state_reg._next = 0;
            if (reset || drain_left == 0) {
                data_reg._next = 0;
                valid_reg._next = 0;
            }
        }
        if (reset) {
            drain._next = 0;
            drain_left._next = 0;
            drain_valid._next = 0;
            pending_control._next = 0;
            pending_count._next = 0;
        }
        // Gate stored state, not combinational temporaries, so synthesis
        // does not build enable muxes for every unrolled arithmetic temporary.
        if (!symbol_enable_in() && !reset) {
            for (unsigned state = 0; state < 8; ++state) {
                costs[state]._next = costs[state];
                survivors[state]._next = survivors[state];
                selectors[state]._next = selectors[state];
                selected_bytes[state]._next = selected_bytes[state];
            }
            count._next = count;
            reachable._next = reachable;
            pending_control._next = pending_control;
            pending_count._next = pending_count;
            data_reg._next = data_reg;
            state_reg._next = state_reg;
            valid_reg._next = valid_reg;
            drain._next = drain;
            drain_left._next = drain_left;
            drain_valid._next = drain_valid;
        }
    }
    void _strobe() {
        for (unsigned state = 0; state < 8; ++state) {
            costs[state].strobe();
            survivors[state].strobe();
            selectors[state].strobe();
            selected_bytes[state].strobe();
        }
        count.strobe();
        reachable.strobe();
        pending_control.strobe();
        pending_count.strobe();
        data_reg.strobe();
        state_reg.strobe();
        valid_reg.strobe();
        drain.strobe();
        drain_left.strobe();
        drain_valid.strobe();
    }
};

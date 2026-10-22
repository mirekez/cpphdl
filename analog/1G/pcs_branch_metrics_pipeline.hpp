#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Four registered stages: squared distances, coordinate choices, 4D group
// sums, and balanced group selection. One observation per clock; bubbles drain.
class PipelinedPCSBranchMetrics : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<4>) negate_in;
    _PORT(bool) advance_in;
    _PORT(u<18>) metric_out[8];
    _PORT(u<6>) point_out[8];
    _PORT(bool) valid_out = _ASSIGN((accepted & 8) != 0);

private:
    reg<u<16>> squared[20]{}, nearest[12]{}, fixed_costs[4]{};
    reg<u<12>> low_choices{};
    reg<u<18>> group_costs[48]{}, metrics[8]{};
    reg<u<6>> group_points[48]{}, points[8]{};
    reg<u<4>> accepted{};

    // Same six Cartesian groups as the functional reference. Independent unit
    // checks enumerate the complete IEEE tables, rather than using this table.
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


    bool less(unsigned a, unsigned b) {
        // Parallel bounded comparisons avoid a single wide borrow chain.
        return (a >> 12) < (b >> 12) ||
               ((a >> 12) == (b >> 12) &&
                (((a >> 6) & 63) < ((b >> 6) & 63) ||
                 (((a >> 6) & 63) == ((b >> 6) & 63) && (a & 63) < (b & 63))));
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 8; ++i) {
            metric_out[i] = _ASSIGN_REG_I(metrics[i]);
            point_out[i] = _ASSIGN_REG_I(points[i]);
        }
    }

    void _work(bool reset) {
        int sample, difference;
        u<8> distance;
        u<17> left_sum, right_sum;
        unsigned choices, packed, fixed, level, point, bit_index;
        unsigned coordinates[4], pair_cost[3], pair_point[3], left_cost, left_point;
        bool choose_right;
        sample = 0;
        difference = 0;
        distance = 0;
        left_sum = 0;
        right_sum = 0;
        choices = 0;
        packed = 0;
        fixed = 0;
        level = 0;
        point = 0;
        bit_index = 0;
        left_cost = 0;
        left_point = 0;
        choose_right = false;
        for (unsigned i = 0; i < 4; ++i)
            coordinates[i] = 0;
        for (unsigned i = 0; i < 3; ++i) {
            pair_cost[i] = 0;
            pair_point[i] = 0;
        }
        accepted._next = (unsigned(accepted) << 1) | (advance_in() ? 1 : 0);
        if (advance_in()) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                sample = int((unsigned(samples_in()) >> (8 * lane)) & 255) - 128;
                if ((negate_in() >> lane) & 1)
                    sample = -sample;
                for (unsigned code = 0; code < 5; ++code) {
                    difference = sample - (int(code) - 2) * 32;
                    distance = unsigned(difference < 0 ? -difference : difference);
                    squared[lane * 5 + code]._next = unsigned(distance) * unsigned(distance);
                }
            }
        }
        if ((accepted & 1) != 0) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                fixed_costs[lane]._next = squared[lane * 5 + 4];
                for (unsigned pair = 0; pair < 3; ++pair) {
                    choose_right = squared[lane * 5 + pair] < squared[lane * 5 + pair + 2];
                    nearest[lane * 3 + pair]._next =
                        choose_right ? squared[lane * 5 + pair] : squared[lane * 5 + pair + 2];
                    if (choose_right)
                        choices = choices | (1 << (lane * 3 + pair));
                }
            }
            low_choices._next = choices;
        }
        if ((accepted & 2) != 0) {
            for (unsigned subset = 0; subset < 8; ++subset) {
                for (unsigned group = 0; group < 6; ++group) {
                    packed = base(subset * 6 + group);
                    fixed = group == 2 ? 0 : group == 3 ? 2 : group == 4 ? 1 : group == 5 ? 3 : 4;
                    point = group == 0 ? 0 : group == 1 ? 16 : 32 + (group - 2) * 8;
                    bit_index = 0;
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        level = (packed >> (3 * lane)) & 7;
                        coordinates[lane] = unsigned(fixed_costs[lane]);
                        if (lane != fixed) {
                            coordinates[lane] = unsigned(nearest[lane * 3 + level - 2]);
                            if ((low_choices >> (lane * 3 + level - 2)) & 1)
                                point = point | (1 << bit_index);
                            bit_index = bit_index + 1;
                        }
                    }
                    left_sum = coordinates[0] + coordinates[1];
                    right_sum = coordinates[2] + coordinates[3];
                    group_costs[subset * 6 + group]._next = unsigned(left_sum) + unsigned(right_sum);
                    group_points[subset * 6 + group]._next = point;
                }
            }
        }
        if ((accepted & 4) != 0) {
            for (unsigned subset = 0; subset < 8; ++subset) {
                for (unsigned pair = 0; pair < 3; ++pair) {
                    choose_right = less(unsigned(group_costs[subset * 6 + 2 * pair + 1]),
                                        unsigned(group_costs[subset * 6 + 2 * pair]));
                    pair_cost[pair] = choose_right ? unsigned(group_costs[subset * 6 + 2 * pair + 1])
                                                   : unsigned(group_costs[subset * 6 + 2 * pair]);
                    pair_point[pair] = choose_right ? unsigned(group_points[subset * 6 + 2 * pair + 1])
                                                    : unsigned(group_points[subset * 6 + 2 * pair]);
                }
                choose_right = less(pair_cost[1], pair_cost[0]);
                left_cost = choose_right ? pair_cost[1] : pair_cost[0];
                left_point = choose_right ? pair_point[1] : pair_point[0];
                choose_right = less(pair_cost[2], left_cost);
                metrics[subset]._next = choose_right ? pair_cost[2] : left_cost;
                points[subset]._next = choose_right ? pair_point[2] : left_point;
            }
        }
        if (reset) {
            accepted._next = 0;
            low_choices._next = 0;
            for (unsigned i = 0; i < 20; ++i)
                squared[i]._next = 0;
            for (unsigned i = 0; i < 12; ++i)
                nearest[i]._next = 0;
            for (unsigned i = 0; i < 4; ++i)
                fixed_costs[i]._next = 0;
            for (unsigned i = 0; i < 48; ++i) {
                group_costs[i]._next = 0;
                group_points[i]._next = 0;
            }
            for (unsigned i = 0; i < 8; ++i) {
                metrics[i]._next = 0;
                points[i]._next = 0;
            }
        }
    }

    void _strobe() {
        accepted.strobe();
        low_choices.strobe();
        for (unsigned i = 0; i < 20; ++i)
            squared[i].strobe();
        for (unsigned i = 0; i < 12; ++i)
            nearest[i].strobe();
        for (unsigned i = 0; i < 4; ++i)
            fixed_costs[i].strobe();
        for (unsigned i = 0; i < 48; ++i) {
            group_costs[i].strobe();
            group_points[i].strobe();
        }
        for (unsigned i = 0; i < 8; ++i) {
            metrics[i].strobe();
            points[i].strobe();
        }
    }
};


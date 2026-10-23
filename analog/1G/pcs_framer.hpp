#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Receive delimiter/control recognition. The synchronizer and PMA supply the
// current observation and its acquired sign/mask bits. DATA tags request the
// delayed soft-decision byte; delimiter tags reconstruct preamble and preserve
// GMII valid/error semantics. Full-duplex operation does not need CRS/COL.
class PCSReceiveFramer : public Module {
public:
    _PORT(u<12>) symbols_in;
    _PORT(u<4>) sg_in;
    _PORT(u<4>) sx_in;
    _PORT(u<4>) low_mask_in;
    _PORT(u<4>) polarity_in;
    _PORT(bool) locked_in;
    _PORT(bool) pma_ok_in;
    _PORT(u<4>) negate_out = _ASSIGN_REG(negate_reg);
    // advance, start, end, constrained, and subset in bits 0,1,2,3,6:4.
    _PORT(u<7>) control_out = _ASSIGN_REG(control_reg);
    // GMII word [9:0], use-decoder [10], descrambler mask [18:11]. Bit 19
    // confirms SSD2 and asks the caller to insert the preceding SSD1 preamble.
    _PORT(u<20>) tag_out = _ASSIGN_REG(tag_reg);
    _PORT(bool) restart_out = _ASSIGN_REG(restart_reg);
    _PORT(bool) remote_receiver_ok_out = _ASSIGN_REG(remote_ok);
    _PORT(bool) receiving_out = _ASSIGN(state == 1 || state == 2);

private:
    reg<u<3>> state{};
    reg<u<3>> good_idles{}, bad_groups{};
    reg<u<4>> negate_reg{};
    reg<u<7>> control_reg{};
    reg<u<20>> tag_reg{};
    reg<u1> restart_reg{}, error_dv{}, remote_ok{};

    // Control facts from IEEE Tables 40-1/40-2. SSD1/SSD2 and ESD1/ESD2_Ext_0
    // share quartets; their direction and position determine their meaning.
    unsigned special(unsigned symbols) {
        switch (symbols) {
        case 1314:
            return 8;
        case 2331:
            return 10;
        case 2268:
            return 12;
        case 1820:
            return 14;
        case 1700:
            return 9;
        case 2274:
            return 11;
        case 1315:
            return 13;
        case 1308:
            return 15;
        case 288:
            return 16;
        case 2313:
            return 18;
        case 2124:
            return 20;
        case 780:
            return 22;
        case 548:
            return 17;
        case 2144:
            return 19;
        case 289:
            return 21;
        case 268:
            return 23;
        case 2196:
            return 24;
        case 1764:
            return 26;
        case 1827:
            return 28;
        case 2275:
            return 30;
        case 1812:
            return 25;
        case 2260:
            return 27;
        case 2323:
            return 29;
        case 2204:
            return 31;
        case 2052:
            return 32;
        case 612:
            return 34;
        case 801:
            return 36;
        case 2145:
            return 38;
        case 772:
            return 33;
        case 2116:
            return 35;
        case 2305:
            return 37;
        case 2060:
            return 39;
        case 2340:
            return 56;
        case 292:
            return 64;
        case 2084:
            return 72;
        case 2308:
            return 80;
        case 2336:
            return 88;
        default:
            return 255;
        }
    }

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned normalized, reversed, raw, point, condition, subset, control, tag, sign;
        bool idle, valid_levels, bad, reversal;
        normalized = 0;
        reversed = 0;
        point = 0;
        idle = true;
        valid_levels = true;
        sign = unsigned(sg_in()) ^ unsigned(polarity_in());
        reversal = state >= 2 && state <= 5;
        for (unsigned lane = 0; lane < 4; ++lane) {
            raw = (unsigned(symbols_in()) >> (3 * lane)) & 7;
            if (raw > 4)
                valid_levels = false;
            if ((sign >> lane) & 1)
                raw = 4 - raw;
            normalized = normalized | ((raw & 7) << (3 * lane));
            reversed = reversed | (((4 - raw) & 7) << (3 * lane));
            if (raw != 0 && raw != 2)
                idle = false;
            if (raw == 0)
                point = point | (1 << lane);
        }
        idle = idle && ((point & 11) == (unsigned(low_mask_in()) & 11));
        negate_reg._next = sign ^ (reversal ? 15 : 0);
        condition = special(reversal ? reversed : normalized);
        subset = condition & 7;
        condition = condition >> 3;
        control = 0;
        tag = 0;
        bad = false;
        restart_reg._next = 0;
        if (state == 0 || state == 7) {
            if (normalized == 2340) {
                // Delay commitment until SSD2 confirms the pair. A bad SSD
                // must not create a one-octet valid frame on the GMII.
                tag = 0;
                control = 0;
                state._next = 1;
            } else if (idle) {
                bad_groups._next = 0;
                state._next = 0;
                remote_ok._next = ((point ^ unsigned(low_mask_in())) & 4) != 0;
            } else if (state == 7 && ((point & 8) == (unsigned(low_mask_in()) & 8)) && valid_levels) {
                // Carrier extension uses the idle constellation, with Sc[0]
                // and/or Sc[1] toggled. Data remains invalid on the GMII.
                tag = ((point ^ unsigned(low_mask_in())) & 2) ? 0x21F : 0x20F;
            } else {
                tag = 0x20E;
                bad = true;
                state._next = 6;
            }
        } else if (state == 1) {
            if (normalized == 292) {
                tag = 0x80155;
                control = 2;
                state._next = 2;
            } else {
                tag = 0x20E;
                bad = true;
                state._next = 6;
            }
        } else if (state == 2) {
            if (condition == 2 || condition == 3 || condition == 4) {
                control = 9 | (subset << 4);
                tag = condition == 2 ? 0x21F : condition == 3 ? 0x20F : 0;
                state._next = 3;
            } else if (condition == 1) {
                control = 9 | (subset << 4);
                tag = 0x700;
            } else if (condition == 31 && valid_levels) {
                control = 1;
                tag = 0x500;
            } else {
                tag = 0x300;
                bad = true;
                state._next = 6;
            }
        } else if (state == 3) {
            if (condition == 2 || condition == 3 || condition == 4) {
                control = 13 | (subset << 4);
                tag = condition == 2 ? 0x21F : condition == 3 ? 0x20F : 0;
                state._next = 4;
            } else {
                tag = 0x200;
                bad = true;
                state._next = 6;
            }
        } else if (state == 4) {
            if (condition == 7 || condition == 11) {
                tag = condition == 11 ? 0x21F : 0;
                state._next = 5;
            } else {
                tag = 0x200;
                bad = true;
                state._next = 6;
            }
        } else if (state == 5) {
            if (condition >= 8 && condition <= 11) {
                tag = condition == 11 ? 0x21F : condition == 10 ? 0x20F : 0;
                state._next = condition == 10 || condition == 11 ? 7 : 0;
            } else {
                tag = 0x200;
                bad = true;
                state._next = 6;
            }
        } else {
            tag = error_dv ? 0x300 : 0x20E;
            if (idle) {
                if (good_idles >= 3) {
                    tag = 0;
                    good_idles._next = 0;
                    bad_groups._next = 0;
                    state._next = 0;
                } else {
                    good_idles._next = good_idles + 1;
                }
            } else {
                good_idles._next = 0;
                bad = true;
            }
        }
        if (bad) {
            if (state != 6)
                error_dv._next = state == 2;
            good_idles._next = 0;
            if (bad_groups < 4)
                bad_groups._next = bad_groups + 1;
            else {
                restart_reg._next = 1;
                remote_ok._next = 0;
            }
        }
        control_reg._next = control;
        tag_reg._next = tag | (((unsigned(sx_in()) << 4) | unsigned(low_mask_in())) << 11);
        if (!locked_in() || !pma_ok_in() || reset) {
            state._next = 0;
            good_idles._next = 0;
            bad_groups._next = 0;
            negate_reg._next = 0;
            control_reg._next = 0;
            tag_reg._next = !reset && state != 0 ? 0x200 : 0;
            restart_reg._next = 0;
            error_dv._next = 0;
            remote_ok._next = 0;
        }
    }
    void _strobe() {
        state.strobe();
        good_idles.strobe();
        bad_groups.strobe();
        negate_reg.strobe();
        control_reg.strobe();
        tag_reg.strobe();
        restart_reg.strobe();
        error_dv.strobe();
        remote_ok.strobe();
    }
};

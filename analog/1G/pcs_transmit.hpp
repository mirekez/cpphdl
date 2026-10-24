#pragma once
#include "pcs.hpp"

// Clause 40 transmit bit generation, before the separate constellation mapper.
// A symbol clock advances the scrambler even while the MAC is idle. Mode values:
// 0 = SEND_Z, 1 = SEND_I (training), 2 = SEND_N (normal), 3 = invalid.
class PCSTransmitBits : public Module {
public:
    _PORT(u<8>) data_in;
    _PORT(bool) enable_in;
    _PORT(bool) error_in;
    _PORT(bool) master_in;
    _PORT(bool) link_in;
    _PORT(bool) local_receiver_ok_in;
    _PORT(u<2>) mode_in;
    _PORT(bool) seed_load_in;
    _PORT(u<33>) seed_in;
    _PORT(u<6>) point_out = _ASSIGN(point_reg & 63);
    _PORT(u<3>) subset_out = _ASSIGN_REG(subset_reg);
    _PORT(u<4>) condition_out = _ASSIGN_REG(condition_reg);
    _PORT(u<4>) negate_out = _ASSIGN_REG(negate_reg);
    _PORT(bool) fault_out = _ASSIGN_REG(fault_reg);
    // These signals also serve the receiver's independently acquired descrambler.
    _PORT(u<4>) sx_out = _ASSIGN_REG(sx_reg);
    _PORT(u<4>) sy_out = _ASSIGN_REG(sy_reg);
    _PORT(u<4>) sg_out = _ASSIGN_REG(sg_reg);
    _PORT(u<9>) encoded_out = _ASSIGN_REG(point_reg);
    _PORT(u<3>) trellis_out = _ASSIGN_REG(trellis_reg);

private:
    reg<u<33>> scrambler{};
    reg<u<4>> enables{}, errors{};
    reg<u<4>> previous_sy{}, sx_reg{}, sy_reg{}, sg_reg{};
    reg<u1> odd{}, armed{}, delayed_error{}, termination_bad{}, fault_reg{};
    reg<u<3>> trellis_reg{}, subset_reg{};
    reg<u<9>> point_reg{};
    reg<u<4>> condition_reg{}, negate_reg{};

public:
    void _assign() {}
    void _work(bool reset) {
        u<33> s;
        unsigned sx, sy, sg, sc, sd, cs, subset, condition;
        bool enabled, error, e1, e2, e3, e4, csreset, reversal;
        bool extension, extension_error, termination_error;
        s = scrambler;
        // XOR networks implement g(x) = x^3 + x^8 on three decorrelated
        // streams: Scr[0], Scr[4] XOR Scr[6], and Scr[1] XOR Scr[5].
        sy = unsigned(s & 1);
        sy = sy | ((unsigned((s >> 3) ^ (s >> 8)) & 1) << 1);
        sy = sy | ((unsigned((s >> 6) ^ (s >> 16)) & 1) << 2);
        sy = sy | ((unsigned((s >> 9) ^ (s >> 14) ^ (s >> 19) ^ (s >> 24)) & 1) << 3);
        sx = unsigned((s >> 4) ^ (s >> 6)) & 1;
        sx = sx | ((unsigned((s >> 7) ^ (s >> 9) ^ (s >> 12) ^ (s >> 14)) & 1) << 1);
        sx = sx | ((unsigned((s >> 10) ^ (s >> 12) ^ (s >> 20) ^ (s >> 22)) & 1) << 2);
        sx = sx |
             ((unsigned((s >> 13) ^ (s >> 15) ^ (s >> 18) ^ (s >> 20) ^ (s >> 23) ^ (s >> 25) ^ (s >> 28) ^ (s >> 30)) &
               1)
              << 3);
        sg = unsigned((s >> 1) ^ (s >> 5)) & 1;
        sg = sg | ((unsigned((s >> 4) ^ (s >> 8) ^ (s >> 9) ^ (s >> 13)) & 1) << 1);
        sg = sg | ((unsigned((s >> 7) ^ (s >> 11) ^ (s >> 17) ^ (s >> 21)) & 1) << 2);
        sg = sg |
             ((unsigned((s >> 10) ^ (s >> 14) ^ (s >> 15) ^ (s >> 19) ^ (s >> 20) ^ (s >> 24) ^ (s >> 25) ^ (s >> 29)) &
               1)
              << 3);
        sx_reg._next = sx;
        sy_reg._next = sy;
        sg_reg._next = sg;
        previous_sy._next = sy;
        odd._next = !odd;
        scrambler._next = (s << 1) | (((s >> 32) ^ (s >> (master_in() ? 12 : 19))) & 1);

        // Figure 40-8: re-arm only after observing a quiet GMII in SEND_N.
        // Starting or changing link state during a frame cannot emit its suffix.
        enabled = armed && link_in() && mode_in() == 2 && enable_in();
        error = armed && link_in() && mode_in() == 2 && error_in();
        if (!link_in() || mode_in() != 2)
            armed._next = 0;
        else if (!enable_in() && !error_in())
            armed._next = 1;
        e1 = (enables & 1) != 0;
        e2 = (enables & 2) != 0;
        e3 = (enables & 4) != 0;
        e4 = (enables & 8) != 0;
        enables._next = (enables << 1) | unsigned(enabled);
        errors._next = (errors << 1) | unsigned(error);
        csreset = e2 && !enabled;
        reversal = e2 || e4; // '+' in Clause 40 is logical OR, not XOR.
        sc = (e2 ? sx << 4 : 0) | (sy & 1);
        if (mode_in() == 0)
            sc = sc & 240;
        else
            sc = sc | (odd ? (unsigned(previous_sy) ^ 14) & 14 : sy & 14);
        sd = sc;
        if (e2) {
            sd = sc ^ unsigned(data_in());
            if (csreset)
                sd = (sd & 63) | ((unsigned(trellis_reg) & 1) << 6) | ((unsigned(trellis_reg) & 2) << 6);
        } else {
            sd = sd ^ (local_receiver_ok_in() ? 4 : 0);
            extension = !enabled && error && data_in() == 15;
            extension_error = !enabled && error && data_in() != 15;
            sd = sd ^ unsigned(extension) ^ (unsigned(extension_error) << 1);
        }
        cs = (unsigned(trellis_reg) >> 2) & 1;
        if (e2) {
            cs = cs | ((((sd >> 6) ^ unsigned(trellis_reg)) & 1) << 1);
            cs = cs | ((((sd >> 7) ^ (unsigned(trellis_reg) >> 1)) & 1) << 2);
        }
        sd = sd | ((cs & 1) << 8);
        trellis_reg._next = cs;
        // The table's printed Sd[6:8] runs in the opposite order to an integer.
        subset = ((sd >> 4) & 4) | ((sd >> 6) & 2) | ((sd >> 8) & 1);
        condition = 12;
        if (enabled && !e2) {
            condition = e1 ? 6 : 5;
            delayed_error._next = delayed_error || error;
        } else if (enabled && e2) {
            condition = (error || delayed_error) ? 1 : 0;
            delayed_error._next = 0;
        } else if (csreset) {
            condition = termination_bad ? 2 : error ? (data_in() == 15 ? 3 : 2) : 4;
            if (error && data_in() != 15)
                termination_bad._next = 1;
        } else if (!e2 && e4) {
            termination_error = termination_bad || (error && ((errors & 3) == 3) && data_in() != 15);
            if (e3)
                condition = termination_error ? 11 : 7;
            else if (termination_error)
                condition = 11;
            else if (error)
                condition = 10;
            else
                condition = (errors & 1) ? 9 : 8;
        } else {
            termination_bad._next = 0;
        }
        if (mode_in() == 0)
            condition = 13;
        // A malformed mode or zero seed is fail-closed at the DAC boundary.
        if (fault_reg || mode_in() == 3)
            condition = 13;
        point_reg._next = sd;
        subset_reg._next = subset;
        condition_reg._next = condition;
        negate_reg._next = sg ^ (reversal ? 15 : 0);
        fault_reg._next = fault_reg || mode_in() == 3;
        if (seed_load_in() || reset) {
            scrambler._next = reset ? u<33>(1) : seed_in();
            fault_reg._next = !reset && seed_in() == 0;
            enables._next = 0;
            errors._next = 0;
            previous_sy._next = 0;
            sx_reg._next = 0;
            sy_reg._next = 0;
            sg_reg._next = 0;
            odd._next = 0;
            armed._next = 0;
            delayed_error._next = 0;
            termination_bad._next = 0;
            trellis_reg._next = 0;
            subset_reg._next = 0;
            point_reg._next = 0;
            condition_reg._next = 13;
            negate_reg._next = 0;
        }
    }
    void _strobe() {
        scrambler.strobe();
        enables.strobe();
        errors.strobe();
        previous_sy.strobe();
        sx_reg.strobe();
        sy_reg.strobe();
        sg_reg.strobe();
        odd.strobe();
        armed.strobe();
        delayed_error.strobe();
        termination_bad.strobe();
        fault_reg.strobe();
        trellis_reg.strobe();
        subset_reg.strobe();
        point_reg.strobe();
        condition_reg.strobe();
        negate_reg.strobe();
    }
};

// A registered, structural GMII-to-PAM5 transmit path; each child contributes
// one cycle. GMII data is current-cycle data: SSD replaces its first two octets.
class PCSTransmitter : public Module {
public:
    _PORT(u<8>) data_in;
    _PORT(bool) enable_in;
    _PORT(bool) error_in;
    _PORT(bool) master_in;
    _PORT(bool) link_in;
    _PORT(bool) local_receiver_ok_in;
    _PORT(u<2>) mode_in;
    _PORT(bool) seed_load_in;
    _PORT(u<33>) seed_in;
    _PORT(u<12>) symbols_out = _ASSIGN(mapper.symbols_out());
    _PORT(bool) fault_out = _ASSIGN(bits.fault_out() || mapper.invalid_out());

private:
    PCSTransmitBits bits;
    PCSSymbolMapper mapper;

public:
    void _assign() {
        bits.data_in = _ASSIGN(data_in());
        bits.enable_in = _ASSIGN(enable_in());
        bits.error_in = _ASSIGN(error_in());
        bits.master_in = _ASSIGN(master_in());
        bits.link_in = _ASSIGN(link_in());
        bits.local_receiver_ok_in = _ASSIGN(local_receiver_ok_in());
        bits.mode_in = _ASSIGN(mode_in());
        bits.seed_load_in = _ASSIGN(seed_load_in());
        bits.seed_in = _ASSIGN(seed_in());
        mapper.point_in = _ASSIGN(bits.point_out());
        mapper.subset_in = _ASSIGN(bits.subset_out());
        mapper.condition_in = _ASSIGN(bits.condition_out());
        mapper.negate_in = _ASSIGN(bits.negate_out());
        bits._assign();
        mapper._assign();
    }
    void _work(bool reset) {
        bits._work(reset);
        mapper._work(reset);
    }
    void _strobe() {
        bits._strobe();
        mapper._strobe();
    }
};

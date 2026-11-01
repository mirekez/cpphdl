#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Retain the transmit reference and training metadata at the ADC sample time.
// The interpolator returns a center tag after its arithmetic pipeline. Looking
// up that tag prevents the DSP from consuming the current, unrelated TX symbol.
class ReceiveSampleContext : public Module {
public:
    _PORT(u<16>) sample_tag_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(bool) train_in;
    _PORT(bool) clipped_in;
    _PORT(bool) sample_valid_in;
    _PORT(u<16>) center_tag_in;
    _PORT(u<24>) center_fraction_in;
    _PORT(bool) center_valid_in;
    _PORT(u<12>) local_out = _ASSIGN(context_comb_func() & 4095);
    _PORT(u<12>) desired_out = _ASSIGN((context_comb_func() >> 12) & 4095);
    _PORT(bool) train_out = _ASSIGN(((present >> (unsigned(reference_tag_comb_func()) & 15)) & 1) &&
                                   ((context_comb_func() >> 24) & 1));
    _PORT(bool) clipped_out = _ASSIGN(support_clipped_comb_func());
    _PORT(bool) valid_out = _ASSIGN(center_valid_in() && !fault_comb_func());
    _PORT(bool) fault_out = _ASSIGN(fault_comb_func());

private:
    memory<u<41>, 1, 16> history;
    reg<u<16>> present{}, clips{}, latest_tag{};
    reg<u1> fault{};

    _LAZY_COMB(reference_tag_comb, u<16>)
        // Metadata is discrete. Select the nearest ADC sample, with ties to
        // the later sample, instead of changing a whole training symbol when
        // the recovered center crosses an integer tag by a tiny fraction.
        return reference_tag_comb = center_tag_in() + unsigned(center_fraction_in() >= 8388608);
    }
    _LAZY_COMB(context_comb, u<41>)
        return context_comb = uint64_t(history[unsigned(reference_tag_comb_func()) & 15]);
    }
    _LAZY_COMB(fault_comb, bool)
        unsigned distance;
        bool matches;
        distance = unsigned(u<16>(latest_tag - center_tag_in()));
        // The four-tap early/middle/late support is contained in center-3..+4.
        // With 16 entries, the newest stored sample must be center+4..+12.
        matches = ((present >> (unsigned(reference_tag_comb_func()) & 15)) & 1) &&
                  ((context_comb_func() >> 25) == reference_tag_comb_func()) && distance >= 4 && distance <= 12;
        return fault_comb = fault || (center_valid_in() && !matches);
    }
    _LAZY_COMB(support_clipped_comb, bool)
        bool clipped = false;
        for (unsigned offset = 0; offset < 8; ++offset)
            clipped = clipped || ((clips >> ((unsigned(center_tag_in()) + 13 + offset) & 15)) & 1);
        return support_clipped_comb = clipped;
    }

public:
    void _assign() {}
    void _work(bool reset) {
        u<41> packet;
        u<16> mask;
        mask = u<16>(1) << (unsigned(sample_tag_in()) & 15);
        packet = sample_tag_in();
        packet = (packet << 25) | (u<41>(train_in()) << 24) |
                 (u<41>(desired_in()) << 12) | local_in();
        if (sample_valid_in() && !reset && !fault) {
            history[unsigned(sample_tag_in()) & 15] = packet;
            present._next = present | mask;
            clips._next = (clips & ~mask) | (clipped_in() ? mask : u<16>(0));
            latest_tag._next = sample_tag_in();
        }
        if (fault_comb_func())
            fault._next = 1;
        if (reset) {
            present._next = 0;
            clips._next = 0;
            latest_tag._next = 0;
            fault._next = 0;
        }
    }
    void _strobe() {
        history.apply();
        present.strobe();
        clips.strobe();
        latest_tag.strobe();
        fault.strobe();
    }
};

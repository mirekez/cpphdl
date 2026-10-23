#pragma once
#include "pcs_synchronize.hpp"
#include "pcs_framer.hpp"
#include "pcs_receive.hpp"
#include "pcs_branch_metrics_pipeline.hpp"

// Digital receive PCS: an unseeded synchronizer, delimiter recognition, soft
// branch metrics, terminated trellis decoding and a fixed GMII metadata delay.
// The PMA supplies four simultaneous, deskewed ADC samples on its recovered
// symbol clock. Clock alignment to the board reference follows this block.
class PCSReceiver : public Module {
    // Three-bit output kind plus eight descrambler bits. Storing the original
    // mostly constant GMII byte in every delay slot wastes registers and makes
    // old synthesis tools repeatedly propagate constants through the FIFO.
    static constexpr unsigned TAG_BITS = 11;
    static constexpr unsigned TAG_DEPTH = 37;

public:
    _PORT(u<32>) samples_in;
    _PORT(bool) remote_master_in;
    _PORT(bool) pma_ok_in;
    _PORT(u<10>) receive_out = _ASSIGN_REG(output_reg);
    _PORT(bool) locked_out = _ASSIGN(sync.locked_out());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(framer.remote_receiver_ok_out());

private:
    PCSReceiveSynchronizer sync;
    PCSReceiveFramer framer;
    PipelinedPCSBranchMetrics metrics;
    PCSTrellisDecoder decoder;
    reg<u<12>> hard_delayed{};
    reg<u<32>> samples_delayed{}, samples_for_metrics{};
    reg<u<28>> control_history{};
    reg<logic<TAG_BITS * TAG_DEPTH>> tags{};
    reg<u<10>> output_reg{};

    unsigned decoder_control() {
        return (unsigned(control_history) >> 21) & 127;
    }

    unsigned slice(unsigned samples) {
        unsigned packed, sample, code;
        packed = 0;
        for (unsigned lane = 0; lane < 4; ++lane) {
            sample = (samples >> (lane * 8)) & 255;
            code = sample <= 80 ? 0 : sample <= 112 ? 1 : sample <= 144 ? 2 : sample <= 176 ? 3 : 4;
            packed = packed | (code << (lane * 3));
        }
        return packed;
    }

public:
    void _assign() {
        sync.symbols_in = _ASSIGN(slice(unsigned(samples_in())));
        sync.remote_master_in = _ASSIGN(remote_master_in());
        sync.pma_ok_in = _ASSIGN(pma_ok_in());
        // The framer checks delayed observations and requests re-acquisition.
        sync.check_idle_in = _ASSIGN(false);
        sync.restart_in = _ASSIGN(framer.restart_out());
        framer.symbols_in = _ASSIGN_REG(hard_delayed);
        framer.sg_in = _ASSIGN(sync.sg_out());
        framer.sx_in = _ASSIGN(sync.sx_out());
        framer.low_mask_in = _ASSIGN(sync.low_mask_out());
        framer.polarity_in = _ASSIGN(sync.polarity_out());
        framer.locked_in = _ASSIGN(sync.locked_out());
        framer.pma_ok_in = _ASSIGN(pma_ok_in());
        metrics.samples_in = _ASSIGN_REG(samples_for_metrics);
        metrics.negate_in = _ASSIGN(framer.negate_out());
        metrics.advance_in = _ASSIGN((framer.control_out() & 1) != 0);
        for (unsigned i = 0; i < 8; ++i) {
            decoder.metric_in[i] =
                _ASSIGN_I((decoder_control() & 8) ? (((decoder_control() >> 4) & 7) == i ? 0 : 262143)
                                                : unsigned(metrics.metric_out[i]()));
            decoder.point_in[i] = _ASSIGN_I((decoder_control() & 8) ? 0 : unsigned(metrics.point_out[i]()));
        }
        decoder.advance_in = _ASSIGN((decoder_control() & 1) != 0);
        decoder.start_in = _ASSIGN((decoder_control() & 2) != 0);
        decoder.end_in = _ASSIGN((decoder_control() & 4) != 0);
        sync._assign();
        framer._assign();
        metrics._assign();
        decoder._assign();
    }
    void _work(bool reset) {
        unsigned tag, byte, kind, source, encoded;
        tag = unsigned(tags >> ((TAG_DEPTH - 1) * TAG_BITS)) & 2047;
        kind = tag & 7;
        byte = unsigned(decoder.data_out()) ^ (tag >> 3);
        output_reg._next = kind == 0   ? 0
                           : kind == 1 ? 0x155
                           : kind == 2 ? 0x100 | byte
                           : kind == 3 ? 0x300 | byte
                           : kind == 4 ? 0x20E
                           : kind == 5 ? 0x20F
                           : kind == 6 ? 0x21F
                                       : 0x300;
        if ((kind == 2 || kind == 3) && !decoder.valid_out())
            output_reg._next = 0x300 | byte;
        source = unsigned(framer.tag_out());
        kind = (source & 1024)            ? ((source & 512) ? 3 : 2)
               : (source & 1023) == 0x155 ? 1
               : (source & 1023) == 0x300 ? 7
               : (source & 1023) == 0x21F ? 6
               : (source & 1023) == 0x20F ? 5
               : (source & 512)           ? 4
                                          : 0;
        encoded = kind | (((source >> 11) & 255) << 3);
        tags._next = (tags << TAG_BITS) | encoded;
        if (source & 524288) {
            // SSD2 confirms both preamble octets before either can reach GMII.
            // Replace the previous provisional tag, preserving fixed latency.
            logic<TAG_BITS * TAG_DEPTH> previous_mask;
            previous_mask = 2047;
            previous_mask = previous_mask << TAG_BITS;
            tags._next = (tags._next & ~previous_mask) | (logic<TAG_BITS * TAG_DEPTH>(1) << TAG_BITS);
        }
        hard_delayed._next = slice(unsigned(samples_in()));
        samples_delayed._next = samples_in();
        samples_for_metrics._next = samples_delayed;
        control_history._next = (control_history << 7) | unsigned(framer.control_out());
        sync._work(reset);
        framer._work(reset);
        metrics._work(reset);
        decoder._work(reset);
        if (!pma_ok_in()) {
            output_reg._next = 512;
            tags._next = 0;
            control_history._next = 0;
        }
        if (reset) {
            hard_delayed._next = 1170;
            samples_delayed._next = 0x80808080;
            samples_for_metrics._next = 0x80808080;
            control_history._next = 0;
            tags._next = 0;
            output_reg._next = 0;
        }
    }
    void _strobe() {
        sync._strobe();
        framer._strobe();
        metrics._strobe();
        decoder._strobe();
        hard_delayed.strobe();
        samples_delayed.strobe();
        samples_for_metrics.strobe();
        control_history.strobe();
        tags.strobe();
        output_reg.strobe();
    }
};

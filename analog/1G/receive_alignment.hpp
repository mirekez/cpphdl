#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Cross recovered GMII to a reference clock without sampling a multibit bus directly.
// Both clock domains must observe reset before traffic resumes. The board supplies
// one reference oscillator; rx_clk may be a recovered symbol clock or the
// fixed ADC clock with receive_enable_in marking recovered symbol ticks.
class ReceiveClockAligner : public Module {
    static constexpr unsigned DEPTH = 64;
    static constexpr unsigned MASK = DEPTH - 1;
    static constexpr unsigned POINTER_BITS = 7;
    static constexpr unsigned PREFILL = 8;

public:
    _PORT(u<10>) receive_in;
    _PORT(bool) receive_enable_in;
    _PORT(u<10>) aligned_out = _ASSIGN_REG(output_reg);
    _PORT(bool) overflow_out = _ASSIGN_REG(overflow_reg);
    _PORT(bool) fault_out = _ASSIGN_REG(fault_reg);

private:
    memory<u<10>, 1, DEPTH> words;
    reg<u<POINTER_BITS>> write_bin{}, write_gray{}, read_bin{}, read_gray{};
    // Only Gray pointers and single-bit fault indications cross clock domains.
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> read_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> read_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> write_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> write_sync2{};
    reg<u<POINTER_BITS>> written_frames{}, written_frames_gray{}, read_frames{}, read_frames_gray{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> frames_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> frames_sync2{};
    reg<u1> previous_valid{}, overflow_reg{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> overflow_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> overflow_sync2{};
    reg<u1> in_frame{}, fault_reg{};
    reg<u<4>> gap{};
    reg<u<10>> output_reg{};

public:
    void _assign() {}

    void _work_rx_clk(bool reset) {
        u<POINTER_BITS> next;
        bool valid, write, full;
        read_sync1._next = read_gray;
        read_sync2._next = read_sync1;
        valid = (receive_in() & 0x100) != 0;
        write = receive_enable_in() && (valid || previous_valid);
        if (receive_enable_in())
            previous_valid._next = valid;
        // Invert the two most significant Gray bits for the full comparison.
        full = write_gray == (read_sync2 ^ u<POINTER_BITS>(96));
        if (write && !overflow_reg) {
            if (full) {
                overflow_reg._next = 1;
            } else {
                // One explicit end token retains frame boundaries; idle clock
                // cycles consume no storage, so drift is absorbed between frames.
                words[(unsigned)write_bin & MASK] = valid ? receive_in() : u<10>(0);
                next = write_bin + 1;
                write_bin._next = next;
                write_gray._next = next ^ (next >> 1);
                if (!valid) {
                    next = written_frames + 1;
                    written_frames._next = next;
                    written_frames_gray._next = next ^ (next >> 1);
                }
            }
        }
        if (reset) {
            write_bin.clr();
            write_gray.clr();
            read_sync1.clr();
            read_sync2.clr();
            previous_valid.clr();
            overflow_reg.clr();
            written_frames.clr();
            written_frames_gray.clr();
        }
    }

    void _strobe_rx_clk() {
        words.apply();
        write_bin.strobe();
        write_gray.strobe();
        read_sync1.strobe();
        read_sync2.strobe();
        previous_valid.strobe();
        overflow_reg.strobe();
        written_frames.strobe();
        written_frames_gray.strobe();
    }
    void _work_neg_rx_clk(bool) {}
    void _strobe_neg_rx_clk() {}

    void _work_ref_clk(bool reset) {
        u<POINTER_BITS> remote, available, next;
        u<10> word;
        bool empty;
        write_sync1._next = write_gray;
        write_sync2._next = write_sync1;
        frames_sync1._next = written_frames_gray;
        frames_sync2._next = frames_sync1;
        overflow_sync1._next = overflow_reg;
        overflow_sync2._next = overflow_sync1;
        output_reg._next = 0;
        // Decode only the synchronized Gray pointer, in the destination domain.
        remote = write_sync2;
        remote = remote ^ (remote >> 1);
        remote = remote ^ (remote >> 2);
        remote = remote ^ (remote >> 4);
        available = remote - read_bin;
        empty = read_gray == write_sync2;
        if (gap < 12)
            gap._next = gap + 1;
        if (!fault_reg && !overflow_sync2) {
            // A complete short/error frame may start without eight bytes of
            // prefill. Its completion counter also crosses as a Gray sequence.
            if (in_frame || (gap >= 12 && !empty && (available >= PREFILL || frames_sync2 != read_frames_gray))) {
                if (empty) {
                    // Mark the truncated frame erroneous and stop until reset.
                    output_reg._next = 0x300;
                    fault_reg._next = 1;
                    in_frame._next = 0;
                } else {
                    word = (unsigned)words[(unsigned)read_bin & MASK];
                    output_reg._next = word;
                    in_frame._next = (word & 0x100) != 0;
                    if ((word & 0x100) == 0) {
                        gap._next = 1;
                        next = read_frames + 1;
                        read_frames._next = next;
                        read_frames_gray._next = next ^ (next >> 1);
                    }
                    next = read_bin + 1;
                    read_bin._next = next;
                    read_gray._next = next ^ (next >> 1);
                }
            }
        }
        if (overflow_sync2 && !fault_reg) {
            if (in_frame)
                output_reg._next = 0x300;
            fault_reg._next = 1;
            in_frame._next = 0;
        }
        if (reset) {
            read_bin.clr();
            read_gray.clr();
            write_sync1.clr();
            write_sync2.clr();
            frames_sync1.clr();
            frames_sync2.clr();
            read_frames.clr();
            read_frames_gray.clr();
            overflow_sync1.clr();
            overflow_sync2.clr();
            in_frame.clr();
            fault_reg.clr();
            gap.clr();
            output_reg.clr();
        }
    }

    void _strobe_ref_clk() {
        read_bin.strobe();
        read_gray.strobe();
        write_sync1.strobe();
        write_sync2.strobe();
        frames_sync1.strobe();
        frames_sync2.strobe();
        read_frames.strobe();
        read_frames_gray.strobe();
        overflow_sync1.strobe();
        overflow_sync2.strobe();
        in_frame.strobe();
        fault_reg.strobe();
        gap.strobe();
        output_reg.strobe();
    }
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
};

#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Frame buffer with a synchronous read port suitable for ASIC SRAM mapping.
// Each SRAM bank has a local output register before bank selection. A four-word
// queue reserves space for both in-flight read stages, including during PAUSE.
// SRAM contents and data pipeline registers do not need reset.
class SynchronousFrameFIFO : public Module {
    static constexpr unsigned AddressBits = 12;
    static constexpr unsigned DEPTH = 1u << AddressBits;

public:
    _PORT(u<10>) receive_in;
    _PORT(bool) valid_in;
    _PORT(bool) fault_in;
    // Domain-local reset release comes from the paired reset coordinator.
    _PORT(bool) clear_rx_in;
    _PORT(bool) clear_tx_in;
    // Only inhibits starting a frame; an active frame always runs to its end.
    _PORT(bool) hold_tx_in;
    _PORT(u<10>) transmit_out = _ASSIGN_REG(output);
    _PORT(bool) source_fault_out = _ASSIGN_REG(source_fault);
    _PORT(bool) fault_out = _ASSIGN_REG(destination_fault);
    _PORT(bool) idle_out = _ASSIGN(!destination_frame && gap == 0 && output == 0);
    // The synchronized read pointer makes this a conservative source-domain
    // occupancy: recent destination reads may not have arrived yet.
    _PORT(u<13>) used_out = _ASSIGN(u<13>(unsigned(u<AddressBits + 1>(unsigned(write_bin) - binary(unsigned(read_sync2))))));

private:
    memory<u<11>, 1, 1024> words0, words1, words2, words3;
    reg<u<11>> bank0_read{}, bank1_read{}, bank2_read{}, bank3_read{};
    reg<u<11>> bank0_pipe{}, bank1_pipe{}, bank2_pipe{}, bank3_pipe{};
    reg<u<2>> read_bank{}, pipe_bank{};
    reg<u1> read_valid{}, pipe_valid{};
    reg<u<44>> queue{};
    reg<u<3>> queued{};
    // Registered conservative read credits keep Gray decoding and pointer
    // subtraction out of the queue-consumption and SRAM-issue control paths.
    reg<u<AddressBits + 1>> available_words{};
    reg<u<AddressBits + 1>> write_bin{}, write_gray{}, read_bin{}, read_gray{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<AddressBits + 1>> read_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<AddressBits + 1>> read_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<AddressBits + 1>> write_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<AddressBits + 1>> write_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> fault_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> fault_sync2{};
    reg<u1> source_frame{}, destination_frame{}, source_fault{}, destination_fault{};
    reg<u<5>> waiting{};
    reg<u<4>> gap{};
    reg<u<10>> output{};

    unsigned binary(unsigned gray) {
        gray ^= gray >> 1;
        gray ^= gray >> 2;
        gray ^= gray >> 4;
        return gray ^ (gray >> 8);
    }

public:
    void _assign() {}
    void _work_rx_clk(bool reset) {
        u<AddressBits + 1> next;
        bool frame, full;
        frame = (receive_in() & 256) != 0;
        full = write_gray == (read_sync2 ^ (3u << (AddressBits - 1)));
        read_sync1._next = read_gray;
        read_sync2._next = read_sync1;
        if (valid_in() && !source_fault && !fault_in() && !reset && !clear_rx_in()) {
            // Only the first idle following a frame needs storage. Its explicit
            // end marker distinguishes a real frame end from an empty FIFO.
            if (frame || source_frame) {
                if (full) {
                    source_fault._next = 1;
                } else {
                    switch ((unsigned(write_bin) >> 10) & 3) {
                    case 0: words0[unsigned(write_bin) & 1023] = frame ? unsigned(receive_in()) : 1024; break;
                    case 1: words1[unsigned(write_bin) & 1023] = frame ? unsigned(receive_in()) : 1024; break;
                    case 2: words2[unsigned(write_bin) & 1023] = frame ? unsigned(receive_in()) : 1024; break;
                    default: words3[unsigned(write_bin) & 1023] = frame ? unsigned(receive_in()) : 1024; break;
                    }
                    next = write_bin + 1;
                    write_bin._next = next;
                    write_gray._next = next ^ (next >> 1);
                }
            }
            source_frame._next = frame;
        }
        if (fault_in())
            source_fault._next = 1;
        if (reset || clear_rx_in()) {
            write_bin._next = 0;
            write_gray._next = 0;
            read_sync1._next = 0;
            read_sync2._next = 0;
            source_frame._next = 0;
            source_fault._next = 0;
        }
    }
    void _strobe_rx_clk() {
        words0.apply(); words1.apply(); words2.apply(); words3.apply();
        write_bin.strobe(); write_gray.strobe();
        read_sync1.strobe(); read_sync2.strobe();
        source_frame.strobe(); source_fault.strobe();
    }
    void _work_tx_clk(bool reset) {
        u<AddressBits + 1> available, next;
        u<44> pending;
        unsigned head, count, incoming, buffered;
        bool consume, head_valid, issue;
        available = available_words;
        head = unsigned(queue) & 2047;
        head_valid = queued != 0;
        buffered = unsigned(queued) + unsigned(read_valid) + unsigned(pipe_valid);
        count = unsigned(queued);
        pending = queue;
        consume = false;
        write_sync1._next = write_gray;
        write_sync2._next = write_sync1;
        fault_sync1._next = source_fault;
        fault_sync2._next = fault_sync1;
        output._next = 0;
        if (gap != 0) gap._next = gap - 1;
        if (!destination_fault && !fault_sync2 && gap == 0 && (destination_frame || !hold_tx_in())) {
            if (!destination_frame && (available != 0 || head_valid) && waiting < 16)
                waiting._next = waiting + 1;
            // An initial SRAM read may still be pending after a stopped TX
            // clock resumes. Only an active frame can underrun at that point.
            if (destination_frame || (head_valid &&
                (unsigned(available) + buffered >= 8 || waiting == 16))) {
                waiting._next = 0;
                if (!head_valid) {
                    output._next = 768;
                    destination_frame._next = 0;
                    destination_fault._next = 1;
                } else {
                    consume = true;
                    if ((head & 1024) != 0) {
                        destination_frame._next = 0;
                        gap._next = 11;
                    } else {
                        output._next = unsigned(head) & 1023;
                        destination_frame._next = 1;
                    }
                }
            }
        }
        if (consume) {
            pending = queue >> 11;
            --count;
        }
        incoming = pipe_bank == 0 ? unsigned(bank0_pipe) : pipe_bank == 1 ? unsigned(bank1_pipe) :
                   pipe_bank == 2 ? unsigned(bank2_pipe) : unsigned(bank3_pipe);
        if (pipe_valid) {
            pending = pending | (u<44>(incoming) << (11 * count));
            ++count;
        }
        queue._next = pending;
        queued._next = count;
        // Reserve using the occupancy before this edge's consumption. Three
        // reserved words sustain one read per cycle through the two-stage
        // pipeline; a full queue waits one edge before reusing its freed slot.
        // This keeps frame/PAUSE consumption logic out of the read-credit
        // feedback path and never overwrites an in-flight SRAM result.
        issue = buffered < 4 && available != 0 &&
                !destination_fault && !fault_sync2 && !reset && !clear_tx_in();
        // Use only the second synchronizer stage. Newly observed writes become
        // visible one TX edge later; never decode the metastable first stage.
        // Deduct this edge's issued read so those delayed credits stay safe.
        available_words._next = binary(unsigned(write_sync2)) - unsigned(read_bin) - unsigned(issue);
        read_valid._next = issue;
        pipe_valid._next = read_valid;
        read_bank._next = (unsigned(read_bin) >> 10) & 3;
        pipe_bank._next = read_bank;
        bank0_pipe._next = bank0_read; bank1_pipe._next = bank1_read;
        bank2_pipe._next = bank2_read; bank3_pipe._next = bank3_read;
        if (issue) {
            bank0_read._next = unsigned(words0[unsigned(read_bin) & 1023]);
            bank1_read._next = unsigned(words1[unsigned(read_bin) & 1023]);
            bank2_read._next = unsigned(words2[unsigned(read_bin) & 1023]);
            bank3_read._next = unsigned(words3[unsigned(read_bin) & 1023]);
            next = read_bin + 1;
            read_bin._next = next;
            read_gray._next = next ^ (next >> 1);
        }
        if (fault_sync2 && !destination_fault) {
            if (destination_frame) output._next = 768;
            destination_frame._next = 0;
            destination_fault._next = 1;
        }
        if (reset || clear_tx_in()) {
            read_bin._next = 0; read_gray._next = 0;
            write_sync1._next = 0; write_sync2._next = 0;
            fault_sync1._next = 0; fault_sync2._next = 0;
            destination_frame._next = 0; destination_fault._next = 0;
            output._next = 0; waiting._next = 0; gap._next = 0;
            queued._next = 0; queue._next = 0;
            available_words._next = 0;
            read_valid._next = 0; pipe_valid._next = 0;
        }
    }
    void _strobe_tx_clk() {
        read_bin.strobe(); read_gray.strobe();
        write_sync1.strobe(); write_sync2.strobe();
        fault_sync1.strobe(); fault_sync2.strobe();
        destination_frame.strobe(); destination_fault.strobe();
        output.strobe(); waiting.strobe(); gap.strobe(); queued.strobe(); queue.strobe();
        available_words.strobe();
        read_valid.strobe(); pipe_valid.strobe(); read_bank.strobe(); pipe_bank.strobe();
        bank0_read.strobe(); bank1_read.strobe(); bank2_read.strobe(); bank3_read.strobe();
        bank0_pipe.strobe(); bank1_pipe.strobe(); bank2_pipe.strobe(); bank3_pipe.strobe();
    }
    void _work_neg_rx_clk(bool) {}
    void _strobe_neg_rx_clk() {}
    void _work_neg_tx_clk(bool) {}
    void _strobe_neg_tx_clk() {}
    // The two-port parent declares these clocks for its other crossing.
    // Actual instance clock sources follow the parent's lifecycle calls.
    void _work_b_rx_clk(bool) {}
    void _strobe_b_rx_clk() {}
    void _work_a_tx_clk(bool) {}
    void _strobe_a_tx_clk() {}
};


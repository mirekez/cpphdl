#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Stream GMII frames between independently clocked PHY ports. The source clock
// may run faster than the recovered symbol rate; valid_in admits one symbol.
// Eight buffered words cover synchronizer latency and short-term clock drift.
// A finite buffer cannot absorb an indefinitely faster source: overflow or a
// mid-frame underrun raises a sticky fault, and both domains then need reset.
template <unsigned AddressBits>
class FrameCrossingFIFO : public Module {
    static_assert(AddressBits >= 6 && AddressBits <= 12, "frame FIFO supports 64 through 4096 words");
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
    memory<u<11>, 1, DEPTH> words;
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
                    words[unsigned(write_bin) & (DEPTH - 1)] = frame ? unsigned(receive_in()) : 1024;
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
        words.apply();
        write_bin.strobe(); write_gray.strobe();
        read_sync1.strobe(); read_sync2.strobe();
        source_frame.strobe(); source_fault.strobe();
    }
    void _work_tx_clk(bool reset) {
        u<AddressBits + 1> available, next;
        u<11> word;
        available = binary(unsigned(write_sync2)) - unsigned(read_bin);
        write_sync1._next = write_gray;
        write_sync2._next = write_sync1;
        fault_sync1._next = source_fault;
        fault_sync2._next = fault_sync1;
        output._next = 0;
        if (gap != 0)
            gap._next = gap - 1;
        if (!destination_fault && !fault_sync2 && gap == 0 && (destination_frame || !hold_tx_in())) {
            if (!destination_frame && available != 0 && waiting < 16)
                waiting._next = waiting + 1;
            if (destination_frame || available >= 8 || (available != 0 && waiting == 16)) {
                waiting._next = 0;
                if (available == 0) {
                    // Explicit TX_ER corrupts the truncated frame; an empty
                    // FIFO must never silently turn a partial frame into a good one.
                    output._next = 768;
                    destination_frame._next = 0;
                    destination_fault._next = 1;
                } else {
                    word = unsigned(words[unsigned(read_bin) & (DEPTH - 1)]);
                    next = read_bin + 1;
                    read_bin._next = next;
                    read_gray._next = next ^ (next >> 1);
                    if ((word & 1024) != 0) {
                        destination_frame._next = 0;
                        gap._next = 11; // This end-marker cycle is the first of 12 idle symbols.
                    } else {
                        output._next = word & 1023;
                        destination_frame._next = 1;
                    }
                }
            }
        }
        if (fault_sync2 && !destination_fault) {
            if (destination_frame)
                output._next = 768;
            destination_frame._next = 0;
            destination_fault._next = 1;
        }
        if (reset || clear_tx_in()) {
            read_bin._next = 0; read_gray._next = 0;
            write_sync1._next = 0; write_sync2._next = 0;
            fault_sync1._next = 0; fault_sync2._next = 0;
            destination_frame._next = 0; destination_fault._next = 0;
            output._next = 0; waiting._next = 0; gap._next = 0;
        }
    }
    void _strobe_tx_clk() {
        read_bin.strobe(); read_gray.strobe();
        write_sync1.strobe(); write_sync2.strobe();
        fault_sync1.strobe(); fault_sync2.strobe();
        destination_frame.strobe(); destination_fault.strobe();
        output.strobe(); waiting.strobe(); gap.strobe();
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

class ReceiveTransmitCrossing : public FrameCrossingFIFO<6> {
    static constexpr unsigned AddressBits = 6;
public:
    // Explicit lifecycle forwarding keeps inherited clock processes visible
    // to the converter as well as ordinary C++ virtual-free dispatch.
    void _work_rx_clk(bool reset) { FrameCrossingFIFO<6>::_work_rx_clk(reset); }
    void _strobe_rx_clk() { FrameCrossingFIFO<6>::_strobe_rx_clk(); }
    void _work_tx_clk(bool reset) { FrameCrossingFIFO<6>::_work_tx_clk(reset); }
    void _strobe_tx_clk() { FrameCrossingFIFO<6>::_strobe_tx_clk(); }
    void _work_b_rx_clk(bool reset) { FrameCrossingFIFO<6>::_work_b_rx_clk(reset); }
    void _strobe_b_rx_clk() { FrameCrossingFIFO<6>::_strobe_b_rx_clk(); }
    void _work_a_tx_clk(bool reset) { FrameCrossingFIFO<6>::_work_a_tx_clk(reset); }
    void _strobe_a_tx_clk() { FrameCrossingFIFO<6>::_strobe_a_tx_clk(); }
};


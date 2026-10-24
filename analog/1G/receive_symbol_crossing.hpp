#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Transfer complete recovered symbols from the ADC clock to a faster-than-
// symbol-rate PCS clock. Both domains must observe reset before traffic resumes.
// Empty periods produce no symbol tick; a fault stops traffic until joint reset.
class ReceiveSymbolCrossing : public Module {
    static constexpr unsigned DEPTH = 8;
    static constexpr unsigned POINTER_BITS = 4;

public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) clipped_in;
    _PORT(bool) valid_in;
    _PORT(bool) fault_in;
    _PORT(u<32>) samples_out = _ASSIGN(packet & 0xffffffffu);
    _PORT(u<16>) tag_out = _ASSIGN((packet >> 32) & 65535);
    _PORT(bool) clipped_out = _ASSIGN((packet >> 48) != 0);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) source_fault_out = _ASSIGN_REG(source_fault);
    _PORT(bool) fault_out = _ASSIGN_REG(destination_fault);

private:
    memory<u<49>, 1, DEPTH> words;
    reg<u<POINTER_BITS>> write_bin{}, write_gray{}, read_bin{}, read_gray{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> read_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> read_sync2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> write_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u<POINTER_BITS>> write_sync2{};
    reg<u1> source_fault{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> fault_sync1{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> fault_sync2{};
    reg<u1> destination_fault{}, valid{};
    reg<u<49>> packet{};

public:
    void _assign() {}

    void _work_adc_clk(bool reset) {
        u<POINTER_BITS> next;
        u<49> word;
        bool full;
        read_sync1._next = read_gray;
        read_sync2._next = read_sync1;
        full = write_gray == (read_sync2 ^ u<POINTER_BITS>(12));
        if (valid_in() && !fault_in() && !source_fault && !reset) {
            if (full) {
                source_fault._next = 1;
            } else {
                word = tag_in();
                word = (word << 32) | samples_in();
                if (clipped_in())
                    word = word | (u<49>(1) << 48);
                words[unsigned(write_bin) & (DEPTH - 1)] = word;
                next = write_bin + 1;
                write_bin._next = next;
                write_gray._next = next ^ (next >> 1);
            }
        }
        if (fault_in())
            source_fault._next = 1;
        if (reset) {
            write_bin.clr();
            write_gray.clr();
            read_sync1.clr();
            read_sync2.clr();
            source_fault.clr();
        }
    }
    void _strobe_adc_clk() {
        words.apply();
        write_bin.strobe();
        write_gray.strobe();
        read_sync1.strobe();
        read_sync2.strobe();
        source_fault.strobe();
    }
    void _work_neg_adc_clk(bool) {}
    void _strobe_neg_adc_clk() {}

    void _work_pcs_clk(bool reset) {
        u<POINTER_BITS> next;
        write_sync1._next = write_gray;
        write_sync2._next = write_sync1;
        fault_sync1._next = source_fault;
        fault_sync2._next = fault_sync1;
        valid._next = 0;
        if (!destination_fault && !fault_sync2 && read_gray != write_sync2) {
            packet._next = uint64_t(words[unsigned(read_bin) & (DEPTH - 1)]);
            valid._next = 1;
            next = read_bin + 1;
            read_bin._next = next;
            read_gray._next = next ^ (next >> 1);
        }
        if (fault_sync2)
            destination_fault._next = 1;
        if (reset) {
            read_bin.clr();
            read_gray.clr();
            write_sync1.clr();
            write_sync2.clr();
            fault_sync1.clr();
            fault_sync2.clr();
            destination_fault.clr();
            valid.clr();
            packet.clr();
        }
    }
    void _strobe_pcs_clk() {
        read_bin.strobe();
        read_gray.strobe();
        write_sync1.strobe();
        write_sync2.strobe();
        fault_sync1.strobe();
        fault_sync2.strobe();
        destination_fault.strobe();
        valid.strobe();
        packet.strobe();
    }
    void _work_neg_pcs_clk(bool) {}
    void _strobe_neg_pcs_clk() {}
};

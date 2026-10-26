#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Transfer recovered samples, aperture-aligned local references and training
// targets as one indivisible packet from the ADC clock to a faster-than-
// symbol-rate PCS clock. Both domains must observe reset before traffic resumes.
// Empty periods produce no symbol tick; a fault stops traffic until joint reset.
class ReceiveDSPCrossing : public Module {
    static constexpr unsigned DEPTH = 8;
    static constexpr unsigned POINTER_BITS = 4;

public:
    // Destination-local synchronized reset release. The global reset remains
    // common to both clock processes; neither side may resume with old pointers.
    _PORT(bool) clear_adc_in;
    _PORT(bool) clear_pcs_in;
    _PORT(u<12>) local_in;
    _PORT(u<12>) desired_in;
    _PORT(bool) train_in;
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) clipped_in;
    _PORT(bool) valid_in;
    _PORT(bool) fault_in;
    _PORT(u<12>) local_out = _ASSIGN(unsigned(packet >> 49) & 4095);
    _PORT(u<12>) desired_out = _ASSIGN(unsigned(packet >> 61) & 4095);
    _PORT(bool) train_out = _ASSIGN((packet >> 73) & 1);
    _PORT(u<32>) samples_out = _ASSIGN(unsigned(packet));
    _PORT(u<16>) tag_out = _ASSIGN(unsigned(packet >> 32) & 65535);
    _PORT(bool) clipped_out = _ASSIGN(((packet >> 48) & 1) != 0);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    _PORT(bool) source_fault_out = _ASSIGN_REG(source_fault);
    _PORT(bool) fault_out = _ASSIGN_REG(destination_fault);

private:
    memory<logic<74>, 1, DEPTH> words;
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
    reg<logic<74>> packet{};

public:
    void _assign() {}

    void _work_adc_clk(bool reset) {
        u<POINTER_BITS> next;
        logic<74> word;
        bool full;
        read_sync1._next = read_gray;
        read_sync2._next = read_sync1;
        full = write_gray == (read_sync2 ^ u<POINTER_BITS>(12));
        if (valid_in() && !fault_in() && !source_fault && !reset && !clear_adc_in()) {
            if (full) {
                source_fault._next = 1;
            } else {
                word = desired_in();
                word = (word << 12) | local_in();
                word = (word << 17) | tag_in();
                word = (word << 32) | samples_in();
                if (train_in())
                    word = word | (logic<74>(1) << 73);
                if (clipped_in())
                    word = word | (logic<74>(1) << 48);
                words[unsigned(write_bin) & (DEPTH - 1)] = word;
                next = write_bin + 1;
                write_bin._next = next;
                write_gray._next = next ^ (next >> 1);
            }
        }
        if (fault_in())
            source_fault._next = 1;
        if (reset || clear_adc_in()) {
            write_bin._next = 0;
            write_gray._next = 0;
            read_sync1._next = 0;
            read_sync2._next = 0;
            source_fault._next = 0;
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
            packet._next = words[unsigned(read_bin) & (DEPTH - 1)];
            valid._next = 1;
            next = read_bin + 1;
            read_bin._next = next;
            read_gray._next = next ^ (next >> 1);
        }
        if (fault_sync2)
            destination_fault._next = 1;
        if (reset || clear_pcs_in()) {
            read_bin._next = 0;
            read_gray._next = 0;
            write_sync1._next = 0;
            write_sync2._next = 0;
            fault_sync1._next = 0;
            fault_sync2._next = 0;
            destination_fault._next = 0;
            valid._next = 0;
            packet._next = 0;
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
    void _work_ref_clk(bool) {}
    void _strobe_ref_clk() {}
    void _work_neg_ref_clk(bool) {}
    void _strobe_neg_ref_clk() {}
};

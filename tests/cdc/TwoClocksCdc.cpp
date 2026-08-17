#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include <cpphdl.h>

using namespace cpphdl;

class TwoClocksCdc : public Module
{
    static constexpr size_t FIFO_DEPTH = 4;
    static constexpr size_t FIFO_ADDR_BITS = 2;
    static constexpr size_t FIFO_PTR_BITS = FIFO_ADDR_BITS + 1;

public:
    _PORT(bool) fast_enable_in;
    _PORT(bool) slow_enable_in;
    _PORT(bool) level_fast_in;
    _PORT(bool) pulse_fast_in;
    _PORT(bool) mailbox_send_fast_in;
    _PORT(u<16>) mailbox_data_fast_in;
    _PORT(bool) reset_release_in;
    _PORT(bool) fifo_write_valid_in;
    _PORT(u<8>) fifo_write_data_in;
    _PORT(bool) fifo_read_ready_in;

    _PORT(u<8>) fast_count_out = _ASSIGN_REG(fast_count_reg);
    _PORT(u<8>) slow_count_out = _ASSIGN_REG(slow_count_reg);
    _PORT(u<8>) slow_gray_fast_out = _ASSIGN_REG(slow_gray_fast2_reg);
    _PORT(u<8>) fast_gray_slow_out = _ASSIGN_REG(fast_gray_slow2_reg);
    _PORT(bool) level_slow_out = _ASSIGN_REG(level_slow2_reg);
    _PORT(bool) pulse_slow_out = _ASSIGN_REG(pulse_slow_reg);
    _PORT(bool) mailbox_busy_fast_out = _ASSIGN_COMB(mailbox_busy_fast_comb_func());
    _PORT(bool) mailbox_valid_slow_out = _ASSIGN_REG(mailbox_valid_slow_reg);
    _PORT(u<16>) mailbox_data_slow_out = _ASSIGN_REG(mailbox_data_slow_reg);
    _PORT(bool) reset_released_fast_out = _ASSIGN_REG(reset_release_fast2_reg);
    _PORT(bool) reset_released_slow_out = _ASSIGN_REG(reset_release_slow2_reg);
    _PORT(u<8>) fast_negedge_count_out = _ASSIGN_REG(fast_negedge_count_reg);
    _PORT(bool) fifo_write_ready_out = _ASSIGN_COMB(fifo_write_ready_comb_func());
    _PORT(bool) fifo_read_valid_out = _ASSIGN_COMB(fifo_read_valid_comb_func());
    _PORT(u<8>) fifo_read_data_out = _ASSIGN_COMB(fifo_read_data_comb_func());

private:
    reg<u<8>> fast_count_reg;
    reg<u<8>> fast_gray_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> slow_gray_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> slow_gray_fast2_reg;

    reg<u<8>> slow_count_reg;
    reg<u<8>> slow_gray_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> fast_gray_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<8>> fast_gray_slow2_reg;

    reg<u1> level_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> level_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> level_slow2_reg;

    reg<u1> pulse_toggle_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> pulse_toggle_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> pulse_toggle_slow2_reg;
    reg<u1> pulse_toggle_slow_last_reg;
    reg<u1> pulse_slow_reg;

    reg<u<16>> mailbox_data_fast_reg;
    reg<u1> mailbox_request_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> mailbox_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> mailbox_ack_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> mailbox_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> mailbox_request_slow2_reg;
    reg<u1> mailbox_ack_slow_reg;
    reg<u<16>> mailbox_data_slow_reg;
    reg<u1> mailbox_valid_slow_reg;

    // (* ASYNC_REG = "TRUE" *)
    reg<u1> reset_release_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> reset_release_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> reset_release_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> reset_release_slow2_reg;

    reg<u<8>> fast_negedge_count_reg;

    memory<u8, 1, FIFO_DEPTH> fifo_data_mem;
    reg<u<FIFO_PTR_BITS>> fifo_write_bin_reg;
    reg<u<FIFO_PTR_BITS>> fifo_write_gray_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<FIFO_PTR_BITS>> fifo_read_gray_write1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<FIFO_PTR_BITS>> fifo_read_gray_write2_reg;
    reg<u<FIFO_PTR_BITS>> fifo_read_bin_reg;
    reg<u<FIFO_PTR_BITS>> fifo_read_gray_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<FIFO_PTR_BITS>> fifo_write_gray_read1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u<FIFO_PTR_BITS>> fifo_write_gray_read2_reg;

    bool mailbox_busy_fast_comb;
    bool& mailbox_busy_fast_comb_func()
    {
        mailbox_busy_fast_comb = mailbox_request_fast_reg != mailbox_ack_fast2_reg;
        return mailbox_busy_fast_comb;
    }

    bool fifo_write_ready_comb;
    bool& fifo_write_ready_comb_func()
    {
        u<FIFO_PTR_BITS> full_gray;
        full_gray = fifo_read_gray_write2_reg
            ^ u<FIFO_PTR_BITS>((1u << FIFO_ADDR_BITS) | (1u << (FIFO_ADDR_BITS - 1)));
        fifo_write_ready_comb = fifo_write_gray_reg != full_gray;
        return fifo_write_ready_comb;
    }

    bool fifo_read_valid_comb;
    bool& fifo_read_valid_comb_func()
    {
        fifo_read_valid_comb = fifo_read_gray_reg != fifo_write_gray_read2_reg;
        return fifo_read_valid_comb;
    }

    u<8> fifo_read_data_comb;
    u<8>& fifo_read_data_comb_func()
    {
        fifo_read_data_comb = (uint8_t)fifo_data_mem[
            (uint32_t)fifo_read_bin_reg & (uint32_t)(FIFO_DEPTH - 1)];
        return fifo_read_data_comb;
    }

public:
    void _work_fast_clk(bool reset)
    {
        u<8> next_count;
        u<FIFO_PTR_BITS> next_fifo_write;

        slow_gray_fast1_reg._next = slow_gray_reg;
        slow_gray_fast2_reg._next = slow_gray_fast1_reg;
        mailbox_ack_fast1_reg._next = mailbox_ack_slow_reg;
        mailbox_ack_fast2_reg._next = mailbox_ack_fast1_reg;
        fifo_read_gray_write1_reg._next = fifo_read_gray_reg;
        fifo_read_gray_write2_reg._next = fifo_read_gray_write1_reg;
        level_fast_reg._next = level_fast_in();

        if (fast_enable_in()) {
            next_count = fast_count_reg + 1;
            fast_count_reg._next = next_count;
            fast_gray_reg._next = next_count ^ (next_count >> 1);
        }
        if (pulse_fast_in()) {
            pulse_toggle_fast_reg._next = !pulse_toggle_fast_reg;
        }
        if (mailbox_send_fast_in() && !mailbox_busy_fast_comb_func()) {
            mailbox_data_fast_reg._next = mailbox_data_fast_in();
            mailbox_request_fast_reg._next = !mailbox_request_fast_reg;
        }
        if (!reset_release_in()) {
            reset_release_fast1_reg.clr();
            reset_release_fast2_reg.clr();
        }
        else {
            reset_release_fast1_reg._next = 1;
            reset_release_fast2_reg._next = reset_release_fast1_reg;
        }
        if (fifo_write_valid_in() && fifo_write_ready_comb_func()) {
            fifo_data_mem[(uint32_t)fifo_write_bin_reg & (uint32_t)(FIFO_DEPTH - 1)]
                = fifo_write_data_in();
            next_fifo_write = fifo_write_bin_reg + 1;
            fifo_write_bin_reg._next = next_fifo_write;
            fifo_write_gray_reg._next = next_fifo_write ^ (next_fifo_write >> 1);
        }

        if (reset) {
            fast_count_reg.clr();
            fast_gray_reg.clr();
            slow_gray_fast1_reg.clr();
            slow_gray_fast2_reg.clr();
            level_fast_reg.clr();
            pulse_toggle_fast_reg.clr();
            mailbox_data_fast_reg.clr();
            mailbox_request_fast_reg.clr();
            mailbox_ack_fast1_reg.clr();
            mailbox_ack_fast2_reg.clr();
            reset_release_fast1_reg.clr();
            reset_release_fast2_reg.clr();
            fifo_write_bin_reg.clr();
            fifo_write_gray_reg.clr();
            fifo_read_gray_write1_reg.clr();
            fifo_read_gray_write2_reg.clr();
        }
    }

    void _strobe_fast_clk()
    {
        fifo_data_mem.apply();
        fast_count_reg.strobe();
        fast_gray_reg.strobe();
        slow_gray_fast1_reg.strobe();
        slow_gray_fast2_reg.strobe();
        level_fast_reg.strobe();
        pulse_toggle_fast_reg.strobe();
        mailbox_data_fast_reg.strobe();
        mailbox_request_fast_reg.strobe();
        mailbox_ack_fast1_reg.strobe();
        mailbox_ack_fast2_reg.strobe();
        reset_release_fast1_reg.strobe();
        reset_release_fast2_reg.strobe();
        fifo_write_bin_reg.strobe();
        fifo_write_gray_reg.strobe();
        fifo_read_gray_write1_reg.strobe();
        fifo_read_gray_write2_reg.strobe();
    }

    void _work_neg_fast_clk(bool reset)
    {
        fast_negedge_count_reg._next = fast_negedge_count_reg + 1;
        if (reset) {
            fast_negedge_count_reg.clr();
        }
    }

    void _strobe_neg_fast_clk()
    {
        fast_negedge_count_reg.strobe();
    }

    void _work_slow_clk(bool reset)
    {
        u<8> next_count;
        u<FIFO_PTR_BITS> next_fifo_read;

        fast_gray_slow1_reg._next = fast_gray_reg;
        fast_gray_slow2_reg._next = fast_gray_slow1_reg;
        level_slow1_reg._next = level_fast_reg;
        level_slow2_reg._next = level_slow1_reg;
        pulse_toggle_slow1_reg._next = pulse_toggle_fast_reg;
        pulse_toggle_slow2_reg._next = pulse_toggle_slow1_reg;
        pulse_slow_reg._next = pulse_toggle_slow2_reg != pulse_toggle_slow_last_reg;
        pulse_toggle_slow_last_reg._next = pulse_toggle_slow2_reg;
        mailbox_request_slow1_reg._next = mailbox_request_fast_reg;
        mailbox_request_slow2_reg._next = mailbox_request_slow1_reg;
        mailbox_valid_slow_reg._next = 0;
        fifo_write_gray_read1_reg._next = fifo_write_gray_reg;
        fifo_write_gray_read2_reg._next = fifo_write_gray_read1_reg;

        if (slow_enable_in()) {
            next_count = slow_count_reg + 1;
            slow_count_reg._next = next_count;
            slow_gray_reg._next = next_count ^ (next_count >> 1);
        }
        if (mailbox_request_slow2_reg != mailbox_ack_slow_reg) {
            mailbox_data_slow_reg._next = mailbox_data_fast_reg;
            mailbox_valid_slow_reg._next = 1;
            mailbox_ack_slow_reg._next = mailbox_request_slow2_reg;
        }
        if (!reset_release_in()) {
            reset_release_slow1_reg.clr();
            reset_release_slow2_reg.clr();
        }
        else {
            reset_release_slow1_reg._next = 1;
            reset_release_slow2_reg._next = reset_release_slow1_reg;
        }
        if (fifo_read_ready_in() && fifo_read_valid_comb_func()) {
            next_fifo_read = fifo_read_bin_reg + 1;
            fifo_read_bin_reg._next = next_fifo_read;
            fifo_read_gray_reg._next = next_fifo_read ^ (next_fifo_read >> 1);
        }

        if (reset) {
            slow_count_reg.clr();
            slow_gray_reg.clr();
            fast_gray_slow1_reg.clr();
            fast_gray_slow2_reg.clr();
            level_slow1_reg.clr();
            level_slow2_reg.clr();
            pulse_toggle_slow1_reg.clr();
            pulse_toggle_slow2_reg.clr();
            pulse_toggle_slow_last_reg.clr();
            pulse_slow_reg.clr();
            mailbox_request_slow1_reg.clr();
            mailbox_request_slow2_reg.clr();
            mailbox_ack_slow_reg.clr();
            mailbox_data_slow_reg.clr();
            mailbox_valid_slow_reg.clr();
            reset_release_slow1_reg.clr();
            reset_release_slow2_reg.clr();
            fifo_read_bin_reg.clr();
            fifo_read_gray_reg.clr();
            fifo_write_gray_read1_reg.clr();
            fifo_write_gray_read2_reg.clr();
        }
    }

    void _strobe_slow_clk()
    {
        slow_count_reg.strobe();
        slow_gray_reg.strobe();
        fast_gray_slow1_reg.strobe();
        fast_gray_slow2_reg.strobe();
        level_slow1_reg.strobe();
        level_slow2_reg.strobe();
        pulse_toggle_slow1_reg.strobe();
        pulse_toggle_slow2_reg.strobe();
        pulse_toggle_slow_last_reg.strobe();
        pulse_slow_reg.strobe();
        mailbox_request_slow1_reg.strobe();
        mailbox_request_slow2_reg.strobe();
        mailbox_ack_slow_reg.strobe();
        mailbox_data_slow_reg.strobe();
        mailbox_valid_slow_reg.strobe();
        reset_release_slow1_reg.strobe();
        reset_release_slow2_reg.strobe();
        fifo_read_bin_reg.strobe();
        fifo_read_gray_reg.strobe();
        fifo_write_gray_read1_reg.strobe();
        fifo_write_gray_read2_reg.strobe();
    }

    void _work_neg_slow_clk(bool) {}
    void _strobe_neg_slow_clk() {}

    void _assign() {}
};

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>

#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long _system_clock = -1;

static uint8_t gray(uint8_t value)
{
    return uint8_t(value ^ (value >> 1));
}

#ifdef VERILATOR
#define CDC_VALUE(name) (dut.name)
#else
#define CDC_VALUE(name) (dut.name())
#endif

class TwoClocksCdcTest
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    TwoClocksCdc dut;
#endif
    bool fast_enable = false;
    bool slow_enable = false;
    bool level_fast = false;
    bool pulse_fast = false;
    bool mailbox_send_fast = false;
    u<16> mailbox_data_fast = 0;
    bool reset_release = false;
    bool fifo_write_valid = false;
    u<8> fifo_write_data = 0;
    bool fifo_read_ready = false;
    std::string generated_sv;

    bool check(bool condition, const char* feature)
    {
        if (!condition) {
            std::print("CDC feature failed: {}\n", feature);
        }
        return condition;
    }

    void clear_inputs()
    {
        fast_enable = false;
        slow_enable = false;
        level_fast = false;
        pulse_fast = false;
        mailbox_send_fast = false;
        mailbox_data_fast = 0;
        reset_release = false;
        fifo_write_valid = false;
        fifo_write_data = 0;
        fifo_read_ready = false;
    }

    void drive_inputs(bool reset)
    {
#ifdef VERILATOR
        dut.reset = reset;
        dut.fast_enable_in = fast_enable;
        dut.slow_enable_in = slow_enable;
        dut.level_fast_in = level_fast;
        dut.pulse_fast_in = pulse_fast;
        dut.mailbox_send_fast_in = mailbox_send_fast;
        dut.mailbox_data_fast_in = (uint16_t)mailbox_data_fast;
        dut.reset_release_in = reset_release;
        dut.fifo_write_valid_in = fifo_write_valid;
        dut.fifo_write_data_in = (uint8_t)fifo_write_data;
        dut.fifo_read_ready_in = fifo_read_ready;
#else
        (void)reset;
#endif
    }

    void fast_cycle(bool reset = false)
    {
        drive_inputs(reset);
#ifdef VERILATOR
        dut.fast_clk = 0;
        dut.eval();
        dut.fast_clk = 1;
        dut.eval();
#else
        dut._work_neg_fast_clk(reset);
        dut._strobe_neg_fast_clk();
        dut._work_fast_clk(reset);
        dut._strobe_fast_clk();
#endif
        ++_system_clock;
    }

    void slow_cycle(bool reset = false)
    {
        drive_inputs(reset);
#ifdef VERILATOR
        dut.slow_clk = 0;
        dut.eval();
        dut.slow_clk = 1;
        dut.eval();
#else
        dut._work_neg_slow_clk(reset);
        dut._strobe_neg_slow_clk();
        dut._work_slow_clk(reset);
        dut._strobe_slow_clk();
#endif
        ++_system_clock;
    }

    void reset_dut()
    {
        clear_inputs();
        fast_cycle(true);
        slow_cycle(true);
        fast_cycle(true);
        slow_cycle(true);
    }

    bool load_generated_sv()
    {
        if (!generated_sv.empty()) {
            return true;
        }
#ifdef VERILATOR
        const std::filesystem::path path = "TwoClocksCdc/TwoClocksCdc.sv";
#else
        const std::filesystem::path path = "generated/TwoClocksCdc.sv";
#endif
        std::ifstream input(path);
        if (!input) {
            std::print("can't open generated CDC RTL: {}\n", path.string());
            return false;
        }
        generated_sv.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    bool test_generated_clock_domains()
    {
        if (!load_generated_sv()) {
            return false;
        }
        return check(generated_sv.find("input wire fast_clk") != std::string::npos
            && generated_sv.find("input wire slow_clk") != std::string::npos
            && generated_sv.find("always_ff @(posedge fast_clk)") != std::string::npos
            && generated_sv.find("always_ff @(negedge fast_clk)") != std::string::npos
            && generated_sv.find("always_ff @(posedge slow_clk)") != std::string::npos
            && generated_sv.find("always_ff @(negedge slow_clk)") != std::string::npos,
            "generated clock ports and edge blocks");
    }

    bool test_register_domain_ownership()
    {
        if (!load_generated_sv()) {
            return false;
        }
        const size_t fast_pos = generated_sv.find("always_ff @(posedge fast_clk)");
        const size_t fast_neg = generated_sv.find("always_ff @(negedge fast_clk)");
        const size_t slow_pos = generated_sv.find("always_ff @(posedge slow_clk)");
        const size_t slow_neg = generated_sv.find("always_ff @(negedge slow_clk)");
        if (fast_pos == std::string::npos || fast_neg == std::string::npos
            || slow_pos == std::string::npos || slow_neg == std::string::npos) {
            return check(false, "register domain ownership block ranges");
        }
        const std::string fast_block = generated_sv.substr(fast_pos, fast_neg - fast_pos);
        const std::string fast_neg_block = generated_sv.substr(fast_neg, slow_pos - fast_neg);
        const std::string slow_block = generated_sv.substr(slow_pos, slow_neg - slow_pos);
        return check(fast_block.find("fast_count_reg <=") != std::string::npos
            && fast_block.find("slow_count_reg <=") == std::string::npos
            && fast_block.find("fast_negedge_count_reg <=") == std::string::npos
            && fast_neg_block.find("fast_negedge_count_reg <=") != std::string::npos
            && slow_block.find("slow_count_reg <=") != std::string::npos
            && slow_block.find("fast_count_reg <=") == std::string::npos,
            "one sequential owner per register");
    }

    bool test_synchronizer_attributes()
    {
        if (!load_generated_sv()) {
            return false;
        }
        const std::string attribute = "(* ASYNC_REG = \"TRUE\" *)";
        const size_t level1 = generated_sv.find("level_slow1_reg");
        const size_t attr = generated_sv.rfind(attribute, level1);
        return check(level1 != std::string::npos && attr != std::string::npos
            && level1 - attr < 128
            && generated_sv.find(attribute, attr + attribute.size()) != std::string::npos,
            "synchronizer synthesis attributes");
    }

    bool test_independent_clock_domains()
    {
        unsigned fast_edges;
        unsigned slow_edges;

        reset_dut();
        fast_enable = true;
        slow_enable = true;
        fast_edges = 0;
        slow_edges = 0;
        for (unsigned step = 0; step < 24; ++step) {
            if ((step % 2) == 0) {
                fast_cycle();
                ++fast_edges;
            }
            if ((step % 5) == 1) {
                slow_cycle();
                ++slow_edges;
            }
        }
        fast_enable = false;
        slow_enable = false;
        return check((uint8_t)CDC_VALUE(fast_count_out) == fast_edges
            && (uint8_t)CDC_VALUE(slow_count_out) == slow_edges,
            "independent clocks with unrelated phase and frequency");
    }

    bool test_two_flop_level_synchronizer()
    {
        reset_dut();
        level_fast = true;
        fast_cycle();
        slow_cycle();
        if (!check(!(bool)CDC_VALUE(level_slow_out), "level synchronizer first stage latency")) {
            return false;
        }
        slow_cycle();
        return check((bool)CDC_VALUE(level_slow_out), "two-flop level synchronizer");
    }

    bool test_gray_counter_bus()
    {
        reset_dut();
        fast_enable = true;
        for (unsigned i = 0; i < 9; ++i) {
            fast_cycle();
        }
        fast_enable = false;
        slow_cycle();
        slow_cycle();
        if (!check((uint8_t)CDC_VALUE(fast_gray_slow_out) == gray(9),
                "Gray bus fast to slow")) {
            return false;
        }
        slow_enable = true;
        for (unsigned i = 0; i < 5; ++i) {
            slow_cycle();
        }
        slow_enable = false;
        fast_cycle();
        fast_cycle();
        return check((uint8_t)CDC_VALUE(slow_gray_fast_out) == gray(5),
            "Gray bus slow to fast");
    }

    bool test_toggle_pulse_synchronizer()
    {
        reset_dut();
        pulse_fast = true;
        fast_cycle();
        pulse_fast = false;
        slow_cycle();
        slow_cycle();
        if (!check(!(bool)CDC_VALUE(pulse_slow_out), "pulse synchronizer latency")) {
            return false;
        }
        slow_cycle();
        if (!check((bool)CDC_VALUE(pulse_slow_out), "toggle pulse transfer")) {
            return false;
        }
        slow_cycle();
        return check(!(bool)CDC_VALUE(pulse_slow_out), "one destination-cycle pulse width");
    }

    bool test_multibit_request_acknowledge()
    {
        reset_dut();
        mailbox_data_fast = 0xa53c;
        mailbox_send_fast = true;
        fast_cycle();
        mailbox_send_fast = false;
        if (!check((bool)CDC_VALUE(mailbox_busy_fast_out), "mailbox source backpressure")) {
            return false;
        }
        slow_cycle();
        slow_cycle();
        slow_cycle();
        if (!check((bool)CDC_VALUE(mailbox_valid_slow_out)
                && (uint16_t)CDC_VALUE(mailbox_data_slow_out) == 0xa53c,
                "coherent multi-bit mailbox transfer")) {
            return false;
        }
        slow_cycle();
        if (!check(!(bool)CDC_VALUE(mailbox_valid_slow_out), "mailbox valid pulse")) {
            return false;
        }
        fast_cycle();
        fast_cycle();
        return check(!(bool)CDC_VALUE(mailbox_busy_fast_out), "mailbox acknowledgement return");
    }

    bool test_async_fifo()
    {
        reset_dut();
        for (uint8_t value = 1; value <= 4; ++value) {
            if (!check((bool)CDC_VALUE(fifo_write_ready_out), "FIFO accepts available entry")) {
                return false;
            }
            fifo_write_data = value;
            fifo_write_valid = true;
            fast_cycle();
        }
        fifo_write_valid = false;
        if (!check(!(bool)CDC_VALUE(fifo_write_ready_out), "FIFO full crosses no raw bus")) {
            return false;
        }
        slow_cycle();
        slow_cycle();
        fifo_read_ready = true;
        for (uint8_t expected = 1; expected <= 4; ++expected) {
            if (!check((bool)CDC_VALUE(fifo_read_valid_out)
                    && (uint8_t)CDC_VALUE(fifo_read_data_out) == expected,
                    "asynchronous FIFO ordering")) {
                return false;
            }
            slow_cycle();
        }
        fifo_read_ready = false;
        return check(!(bool)CDC_VALUE(fifo_read_valid_out), "FIFO empty indication");
    }

    bool test_synchronized_reset_release()
    {
        reset_dut();
        reset_release = true;
        fast_cycle();
        slow_cycle();
        if (!check(!(bool)CDC_VALUE(reset_released_fast_out)
                && !(bool)CDC_VALUE(reset_released_slow_out),
                "reset release first stage")) {
            return false;
        }
        fast_cycle();
        slow_cycle();
        if (!check((bool)CDC_VALUE(reset_released_fast_out)
                && (bool)CDC_VALUE(reset_released_slow_out),
                "synchronized reset deassertion")) {
            return false;
        }
        reset_release = false;
        fast_cycle();
        slow_cycle();
        return check(!(bool)CDC_VALUE(reset_released_fast_out)
            && !(bool)CDC_VALUE(reset_released_slow_out),
            "clocked reset assertion");
    }

    bool test_shared_reset_sampling_per_domain()
    {
        uint8_t slow_before_reset;

        reset_dut();
        fast_enable = true;
        slow_enable = true;
        fast_cycle();
        slow_cycle();
        if (!check((uint8_t)CDC_VALUE(fast_count_out) == 1
                && (uint8_t)CDC_VALUE(slow_count_out) == 1,
                "both domains active before shared reset")) {
            return false;
        }

        fast_enable = false;
        slow_enable = false;
        slow_before_reset = (uint8_t)CDC_VALUE(slow_count_out);
        fast_cycle(true);
        if (!check((uint8_t)CDC_VALUE(fast_count_out) == 0
                && (uint8_t)CDC_VALUE(fast_negedge_count_out) == 0
                && (uint8_t)CDC_VALUE(slow_count_out) == slow_before_reset,
                "reset is sampled only by the clocked domain")) {
            return false;
        }

        fast_cycle(true);
        if (!check((uint8_t)CDC_VALUE(fast_count_out) == 0
                && (uint8_t)CDC_VALUE(slow_count_out) == slow_before_reset,
                "held reset is idempotent on repeated edges")) {
            return false;
        }

        slow_cycle(true);
        if (!check((uint8_t)CDC_VALUE(fast_count_out) == 0
                && (uint8_t)CDC_VALUE(slow_count_out) == 0,
                "shared reset completes after every domain edge")) {
            return false;
        }

        fast_enable = true;
        fast_cycle();
        if (!check((uint8_t)CDC_VALUE(fast_count_out) == 1
                && (uint8_t)CDC_VALUE(slow_count_out) == 0,
                "fast domain resumes independently after reset")) {
            return false;
        }
        slow_enable = true;
        slow_cycle();
        return check((uint8_t)CDC_VALUE(slow_count_out) == 1,
            "slow domain resumes independently after reset");
    }

    bool test_edge_specific_processes()
    {
        reset_dut();
        for (unsigned i = 0; i < 6; ++i) {
            fast_cycle();
        }
        return check((uint8_t)CDC_VALUE(fast_negedge_count_out) == 6,
            "negative-edge process and ownership");
    }

public:
    bool run()
    {
#ifndef VERILATOR
        dut.fast_enable_in = _ASSIGN(fast_enable);
        dut.slow_enable_in = _ASSIGN(slow_enable);
        dut.level_fast_in = _ASSIGN(level_fast);
        dut.pulse_fast_in = _ASSIGN(pulse_fast);
        dut.mailbox_send_fast_in = _ASSIGN(mailbox_send_fast);
        dut.mailbox_data_fast_in = _ASSIGN(mailbox_data_fast);
        dut.reset_release_in = _ASSIGN(reset_release);
        dut.fifo_write_valid_in = _ASSIGN(fifo_write_valid);
        dut.fifo_write_data_in = _ASSIGN(fifo_write_data);
        dut.fifo_read_ready_in = _ASSIGN(fifo_read_ready);
        dut._assign();
#else
        dut.fast_clk = 1;
        dut.slow_clk = 1;
        drive_inputs(true);
        dut.eval();
#endif
        return test_generated_clock_domains()
            && test_register_domain_ownership()
            && test_synchronizer_attributes()
            && test_independent_clock_domains()
            && test_two_flop_level_synchronizer()
            && test_gray_counter_bus()
            && test_toggle_pulse_synchronizer()
            && test_multibit_request_acknowledge()
            && test_async_fifo()
            && test_shared_reset_sampling_per_domain()
            && test_synchronized_reset_release()
            && test_edge_specific_processes();
    }
};

#undef CDC_VALUE

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        noveril |= std::strcmp(argv[i], "--noveril") == 0;
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        ok = VerilatorCompileInExactFolder(__FILE__, "TwoClocksCdc", "TwoClocksCdc",
            {"Predef_pkg"}, {"../../../../include"});
        ok = ok && std::system("TwoClocksCdc/obj_dir/VTwoClocksCdc") == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif
    ok = ok && TwoClocksCdcTest().run();
    std::print("Two-clock CDC {}\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}

#endif

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

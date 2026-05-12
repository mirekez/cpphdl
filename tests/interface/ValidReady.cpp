#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>

using namespace cpphdl;

#define VALID_READY_COUNT 256

template<size_t DATAWIDTH>
struct DataValidReady
{
    bool valid;
    bool ready;
    logic<DATAWIDTH> data;
};

template<size_t DATAWIDTH>
struct DataValidReadyIf : public Interface
{
    _PORT(bool) valid_in;
    _PORT(bool) ready_out;
    _PORT(logic<DATAWIDTH>) data_in;
//
//    DataValidReadyIf& operator=(DataValidReady<DATAWIDTH>& other)
//    {
//        DataValidReady<DATAWIDTH>* p = &other;
//        valid_in = [p]() { return &p->valid; };
//        ready_out = [p]() { return &p->ready; };
//        data_in = [p]() { return &p->data; };
//        return *this;
//    }
};

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t DATAWIDTH>
class VRDriver : public Module
{
public:
    DataValidReadyIf<DATAWIDTH> source_out;

private:
    reg<u<32>> state_reg;
    reg<u<16>> sent_reg;
    reg<u1> valid_reg;
    reg<logic<DATAWIDTH>> data_reg;
    bool done_comb;

    bool& done_comb_func()
    {
        return done_comb = sent_reg >= VALID_READY_COUNT;
    }

public:
    _PORT(bool) done_out = _ASSIGN_REG(done_comb_func());

    void _assign()
    {
        source_out.valid_in = _ASSIGN_REG(valid_reg);
        source_out.data_in = _ASSIGN_REG(data_reg);
    }

    void _work(bool reset)
    {
        if (reset) {
            state_reg.set(u<32>(0x13579bdf));
            sent_reg.clr();
            valid_reg.clr();
            data_reg.clr();
            data_reg._next = 0;
            data_reg._next.bits(31, 0) = u<32>(0x13579bdf);
            data_reg._next.bits(47, 32) = u<16>(0);
            data_reg._next.bits(63, 48) = u<16>(0x9bdf);
            return;
        }

        state_reg._next = state_reg;
        sent_reg._next = sent_reg;
        valid_reg._next = sent_reg < VALID_READY_COUNT;
        data_reg._next = data_reg;

        if (valid_reg && source_out.ready_out()) {
            sent_reg._next = sent_reg + u<16>(1);
            state_reg._next = state_reg * u<32>(1664525) + u<32>(1013904223);
            valid_reg._next = sent_reg._next < VALID_READY_COUNT;
        }

        if (!valid_reg || source_out.ready_out()) {
            data_reg._next = 0;
            data_reg._next.bits(31, 0) = state_reg._next;
            data_reg._next.bits(47, 32) = sent_reg._next;
            data_reg._next.bits(63, 48) = (((uint64_t)state_reg._next) & 0xffff) ^ sent_reg._next;
        }
    }

    void _strobe()
    {
        state_reg.strobe();
        sent_reg.strobe();
        valid_reg.strobe();
        data_reg.strobe();
    }
};

template<size_t DATAWIDTH>
class VRResponder : public Module
{
public:
    DataValidReadyIf<DATAWIDTH> sink_in;

private:
    reg<u<32>> state_reg;
    reg<u<16>> received_reg;
    reg<u<8>> ready_lfsr_reg;
    reg<u1> ready_reg;
    reg<u1> error_reg;
    logic<DATAWIDTH> expected_data;
    bool done_comb;
    bool error_comb;

    bool& done_comb_func()
    {
        return done_comb = received_reg >= VALID_READY_COUNT;
    }

    bool& error_comb_func()
    {
        return error_comb = error_reg;
    }

public:
    _PORT(bool) done_out = _ASSIGN_REG(done_comb_func());
    _PORT(bool) error_out = _ASSIGN_REG(error_comb_func());

    void _assign()
    {
        sink_in.ready_out = _ASSIGN_REG(ready_reg);
    }

    void _work(bool reset)
    {
        if (reset) {
            state_reg.set(u<32>(0x13579bdf));
            received_reg.clr();
            ready_lfsr_reg.set(u<8>(0x5a));
            ready_reg.clr();
            error_reg.clr();
            return;
        }

        state_reg._next = state_reg;
        received_reg._next = received_reg;
        ready_lfsr_reg._next = ready_lfsr_reg;
        ready_reg._next = ready_reg;
        error_reg._next = error_reg;

        ready_lfsr_reg._next = (ready_lfsr_reg << 1) ^ u<8>((((uint64_t)ready_lfsr_reg >> 7) ^ ((uint64_t)ready_lfsr_reg >> 5) ^ 1) & 1);
        ready_reg._next = ((uint64_t)ready_lfsr_reg & 0x7) != 0;

        if (sink_in.valid_in() && sink_in.ready_out()) {
            expected_data = 0;
            expected_data.bits(31, 0) = state_reg;
            expected_data.bits(47, 32) = received_reg;
            expected_data.bits(63, 48) = (((uint64_t)state_reg) & 0xffff) ^ received_reg;
            if (sink_in.data_in() != expected_data) {
                error_reg._next = 1;
            }
            state_reg._next = state_reg * u<32>(1664525) + u<32>(1013904223);
            received_reg._next = received_reg + u<16>(1);
        }
    }

    void _strobe()
    {
        state_reg.strobe();
        received_reg.strobe();
        ready_lfsr_reg.strobe();
        ready_reg.strobe();
        error_reg.strobe();
    }
};

/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

template class VRDriver<64>;
template class VRResponder<64>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

#ifndef VERILATOR
inline bool VerilatorCompileValidReady(std::string cpp_name, std::string top, std::string model_define, size_t datawidth)
{
    std::string folder_name = top + "_" + std::to_string(datawidth);
    std::filesystem::remove_all(folder_name);
    std::filesystem::create_directory(folder_name);
    std::filesystem::copy_file("generated/Predef_pkg.sv", folder_name + "/Predef_pkg.sv", std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("generated/" + top + ".sv", folder_name + "/" + top + ".sv", std::filesystem::copy_options::overwrite_existing);

    std::ignore = std::system((std::string("gawk -i inplace '{ if ($0 ~ /parameter/) count++; if (count == 1") +
        " ) sub(/^.*parameter +[^ ]+/, \"& = " + std::to_string(datawidth) + "\"); print }' " +
        folder_name + "/" + top + ".sv").c_str());

    SystemEcho((std::string("cd ") + folder_name +
        "; verilator -cc Predef_pkg.sv " + top + ".sv --exe " + cpp_name + " --top-module " + top +
        " --Wno-fatal --CFLAGS \"-DVERILATOR -I../../../../include -DVERILATOR_MODEL=V" + top + " " +
        model_define + " " + compilerParams + "\"").c_str());
    return SystemEcho((std::string("cd ") + folder_name + "/obj_dir" +
        "; make -j4 -f V" + top + ".mk CXX=clang++ LINK=\"clang++ -L$CONDA_PREFIX/lib -static-libstdc++ -static-libgcc\"").c_str()) == 0;
}
#endif

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t DATAWIDTH>
class TestValidReady : Module
{
#ifdef VERILATOR
    VERILATOR_MODEL verilated;
    DataValidReady<DATAWIDTH> bus;
#ifdef VERILATOR_DRIVER
    u<32> ref_state;
    u<16> ref_count;
    u<8> ref_lfsr;
    bool ref_error = false;
#else
    VRDriver<DATAWIDTH> driver;
#endif
#else
    VRDriver<DATAWIDTH> driver;
    VRResponder<DATAWIDTH> responder;
#endif

    bool error = false;

    static logic<DATAWIDTH> ref_data(u<32> state, u<16> index)
    {
        logic<DATAWIDTH> ret;
        ret = 0;
        ret.bits(31, 0) = state;
        ret.bits(47, 32) = index;
        ret.bits(63, 48) = (((uint64_t)state) & 0xffff) ^ index;
        return ret;
    }

public:
    void _assign()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
#else
        driver.source_out.ready_out = _ASSIGN_REG(bus.ready);
        driver._assign();
#endif
        verilated.clk = 0;
        verilated.reset = 1;
        verilated.eval();
#else
        driver.__inst_name = __inst_name + "/driver";
        responder.__inst_name = __inst_name + "/responder";
        assignIf(driver, responder, driver.source_out, responder.sink_in);
#endif
    }

    void eval_pos(bool reset)
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
        if (reset) {
            ref_state = 0x13579bdf;
            ref_count = 0;
            ref_lfsr = 0x5a;
            ref_error = false;
            verilated.source_out___05Fready_in = 0;
            verilated.clk = 1;
            verilated.reset = 1;
            verilated.eval();
            return;
        }
        bus.valid = verilated.source_out___05Fvalid_out;
        bus.data = logic<DATAWIDTH>(verilated.source_out___05Fdata_out);
        bus.ready = ((uint64_t)ref_lfsr & 0x7) != 0;
        verilated.source_out___05Fready_in = bus.ready;
        if (bus.valid && bus.ready) {
            ref_state = ref_state * u<32>(1664525) + u<32>(1013904223);
            ref_count = ref_count + u<16>(1);
        }
        ref_lfsr = (ref_lfsr << 1) ^ u<8>((((uint64_t)ref_lfsr >> 7) ^ ((uint64_t)ref_lfsr >> 5) ^ 1) & 1);
        verilated.clk = 1;
        verilated.reset = 0;
        verilated.eval();
#else
        bus.ready = verilated.sink_in___05Fready_out;
        driver._work(reset);
        bus.valid = driver.source_out.valid_in();
        bus.data = driver.source_out.data_in();
        verilated.sink_in___05Fvalid_in = bus.valid;
        verilated.sink_in___05Fdata_in = (uint64_t)bus.data;
        verilated.clk = 1;
        verilated.reset = reset;
        verilated.eval();
#endif
#else
        driver._work(reset);
        responder._work(reset);
#endif
    }

    void eval_neg(bool reset)
    {
#ifdef VERILATOR
        verilated.clk = 0;
        verilated.reset = reset;
        verilated.eval();
#else
        (void)reset;
#endif
    }

    void _strobe()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
#else
        driver._strobe();
#endif
#else
        driver._strobe();
        responder._strobe();
#endif
    }

    bool driver_done()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
        return verilated.done_out;
#else
        return driver.done_out();
#endif
#else
        return driver.done_out();
#endif
    }

    bool responder_done()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
        return ref_count >= VALID_READY_COUNT;
#else
        return verilated.done_out;
#endif
#else
        return responder.done_out();
#endif
    }

    bool responder_error()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
        return ref_error;
#else
        return verilated.error_out;
#endif
#else
        return responder.error_out();
#endif
    }

    bool run()
    {
#ifdef VERILATOR
#ifdef VERILATOR_DRIVER
        std::print("VERILATOR TestValidReady Driver, DATAWIDTH: {}, COUNT: {}...", DATAWIDTH, VALID_READY_COUNT);
#else
        std::print("VERILATOR TestValidReady Responder, DATAWIDTH: {}, COUNT: {}...", DATAWIDTH, VALID_READY_COUNT);
#endif
#else
        std::print("CppHDL TestValidReady, DATAWIDTH: {}, COUNT: {}...", DATAWIDTH, VALID_READY_COUNT);
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "valid_ready_test";
        _assign();

        eval_pos(true);
        _strobe();
        eval_neg(true);
        ++sys_clock;

        int cycles = 10000;
        while (--cycles) {
            eval_pos(false);
            if (responder_error()) {
                error = true;
                break;
            }
#if defined(VERILATOR) && defined(VERILATOR_RESPONDER)
            if (driver_done() && !responder_error()) {
                break;
            }
#else
            if (driver_done() && responder_done()) {
                break;
            }
#endif
            _strobe();
            eval_neg(false);
            ++sys_clock;
        }

#if defined(VERILATOR) && defined(VERILATOR_RESPONDER)
        if (!driver_done() || responder_error()) {
#else
        if (!driver_done() || !responder_done()) {
#endif
            error = true;
            std::print("\nTimeout: driver_done={}, responder_done={}\n", driver_done(), responder_done());
        }
        if (responder_error()) {
            error = true;
            std::print("\nResponder data mismatch\n");
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompileValidReady(__FILE__, "VRDriver", "-DVERILATOR_DRIVER", 64);
        ok &= VerilatorCompileValidReady(__FILE__, "VRResponder", "-DVERILATOR_RESPONDER", 64);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok
            && std::system("VRDriver_64/obj_dir/VVRDriver") == 0
            && std::system("VRResponder_64/obj_dir/VVRResponder") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !(ok && TestValidReady<64>().run());
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

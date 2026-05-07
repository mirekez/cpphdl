#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include "BranchPredictor.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include "../../examples/tools.h"

using namespace cpphdl;

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

static constexpr size_t ENTRIES = 8;
static constexpr size_t COUNTER_BITS = 2;

class TestBranchPredictor
{
#ifdef VERILATOR
    VERILATOR_MODEL predictor;
#else
    BranchPredictor<ENTRIES, COUNTER_BITS> predictor;
#endif

    bool lookup_valid = false;
    uint32_t lookup_pc = 0;
    uint32_t lookup_target = 0;
    uint32_t lookup_fallthrough = 0;
    u<4> lookup_br_op = Br::BNONE;
    bool update_valid = false;
    uint32_t update_pc = 0;
    bool update_taken = false;
    uint32_t update_target = 0;
    bool error = false;

public:
    void _assign()
    {
#ifndef VERILATOR
        predictor.lookup_valid_in = __VAR(lookup_valid);
        predictor.lookup_pc_in = __VAR(lookup_pc);
        predictor.lookup_target_in = __VAR(lookup_target);
        predictor.lookup_fallthrough_in = __VAR(lookup_fallthrough);
        predictor.lookup_br_op_in = __VAR(lookup_br_op);
        predictor.update_valid_in = __VAR(update_valid);
        predictor.update_pc_in = __VAR(update_pc);
        predictor.update_taken_in = __VAR(update_taken);
        predictor.update_target_in = __VAR(update_target);
        predictor.__inst_name = "branch_predictor_test/predictor";
        predictor._assign();
#endif
    }

    void drive(bool reset)
    {
#ifdef VERILATOR
        predictor.lookup_valid_in = lookup_valid;
        predictor.lookup_pc_in = lookup_pc;
        predictor.lookup_target_in = lookup_target;
        predictor.lookup_fallthrough_in = lookup_fallthrough;
        predictor.lookup_br_op_in = (uint8_t)lookup_br_op;
        predictor.update_valid_in = update_valid;
        predictor.update_pc_in = update_pc;
        predictor.update_taken_in = update_taken;
        predictor.update_target_in = update_target;
        predictor.reset = reset;
#else
        (void)reset;
#endif
    }

    void cycle(bool reset = false)
    {
        drive(reset);
#ifdef VERILATOR
        predictor.clk = 0;
        predictor.eval();
        predictor.clk = 1;
        predictor.eval();
        predictor.clk = 0;
        predictor.eval();
#else
        predictor._work(reset);
        predictor._strobe();
#endif
        ++sys_clock;
    }

    bool predict_taken()
    {
#ifdef VERILATOR
        predictor.eval();
        return predictor.predict_taken_out;
#else
        return predictor.predict_taken_out();
#endif
    }

    uint32_t predict_next()
    {
#ifdef VERILATOR
        predictor.eval();
        return predictor.predict_next_out;
#else
        return predictor.predict_next_out();
#endif
    }

    void set_lookup(bool valid, uint32_t pc, uint32_t target, uint32_t fallthrough, uint8_t br_op)
    {
        lookup_valid = valid;
        lookup_pc = pc;
        lookup_target = target;
        lookup_fallthrough = fallthrough;
        lookup_br_op = br_op;
        ++sys_clock;
        drive(false);
#ifdef VERILATOR
        predictor.eval();
#endif
    }

    void train(uint32_t pc, bool taken, uint32_t target)
    {
        update_valid = true;
        update_pc = pc;
        update_taken = taken;
        update_target = target;
        cycle(false);
        update_valid = false;
        drive(false);
    }

    void check(const char* phase, bool expected_taken, uint32_t expected_next)
    {
        bool got_taken = predict_taken();
        uint32_t got_next = predict_next();
        if (got_taken != expected_taken || got_next != expected_next) {
            std::print("\n{} ERROR: taken={} expected={} next={:#x} expected={:#x} pc={:#x} target={:#x} fallthrough={:#x} op={}\n",
                phase, got_taken, expected_taken, got_next, expected_next,
                lookup_pc, lookup_target, lookup_fallthrough, (uint32_t)lookup_br_op);
            error = true;
        }
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestBranchPredictor...");
#else
        std::print("CppHDL TestBranchPredictor...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        _assign();

        cycle(true);

        set_lookup(true, 0x100, 0x180, 0x104, Br::BEQ);
        check("reset conditional miss", false, 0x104);

        set_lookup(true, 0x104, 0x1c0, 0x108, Br::JAL);
        check("unconditional miss", true, 0x1c0);

        train(0x100, true, 0x180);
        set_lookup(true, 0x100, 0x1a0, 0x104, Br::BEQ);
        check("trained taken hit", true, 0x180);

        train(0x100, false, 0x184);
        set_lookup(true, 0x100, 0x1a0, 0x104, Br::BEQ);
        check("trained not-taken hit", false, 0x104);

        train(0x100, true, 0x188);
        train(0x100, true, 0x18c);
        train(0x100, true, 0x190);
        set_lookup(true, 0x100, 0x1a0, 0x104, Br::BEQ);
        check("taken saturation target", true, 0x190);

        set_lookup(false, 0x100, 0x1a0, 0x104, Br::BEQ);
        check("invalid lookup gate", false, 0x104);

        train(0x110, true, 0x210);
        set_lookup(true, 0x100, 0x1a0, 0x104, Br::BEQ);
        check("alias invalidates old tag", false, 0x104);
        set_lookup(true, 0x110, 0x220, 0x114, Br::BEQ);
        check("alias new tag hit", true, 0x210);

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "BranchPredictor", {"Predef_pkg", "Br_pkg"},
            {"../../../../../include", "../../../../../tribe", "../../../../../tribe/spec"},
            ENTRIES, COUNTER_BITS);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ok && std::system("BranchPredictor_8_2/obj_dir/VBranchPredictor") == 0;
        std::cout << "Verilator compilation time: " << compile_us << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok = ok && TestBranchPredictor().run();
    return !ok;
}

/////////////////////////////////////////////////////////////////////////

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#ifndef ENABLE_RV32IA
#define ENABLE_RV32IA
#endif

#include "cpphdl.h"
#include "../spec/State.h"
#include "../ExecuteMem.h"

#include <print>

using namespace cpphdl;

long _system_clock = -1;

class TestExecuteMem
{
    ExecuteMem execute_mem;
    State state = {};
    uint32_t address = 0x1000;
    bool transaction_owner_valid = false;
    bool memory_stall = false;

    void _assign()
    {
        execute_mem.state_in = _ASSIGN_REG(state);
        execute_mem.alu_result_in = _ASSIGN_REG(address);
        execute_mem.dcache_read_valid_in = _ASSIGN(false);
        execute_mem.dcache_read_addr_in = _ASSIGN((uint32_t)0);
        execute_mem.dcache_read_expected_addr_in = _ASSIGN_REG(address);
        execute_mem.dcache_read_data_in = _ASSIGN((uint32_t)0);
        execute_mem.mem_stall_in = _ASSIGN_REG(memory_stall);
        execute_mem.hold_in = _ASSIGN(false);
        execute_mem.transaction_owner_valid_in = _ASSIGN_REG(transaction_owner_valid);
        execute_mem.__inst_name = "execute_mem_test/execute_mem";
        execute_mem._assign();
    }

    void cycle(bool reset = false)
    {
        execute_mem._work(reset);
        execute_mem._strobe();
        ++_system_clock;
    }

public:
    bool run()
    {
        _assign();
        cycle(true);

        state = {};
        state.valid = true;
        state.amo_op = Amo::AMOADD_W;
        state.rs2_val = 1;
        transaction_owner_valid = false;
        cycle();
        if (!execute_mem.atomic_busy_out() || !execute_mem.mem_read_out()) {
            std::print("ExecuteMem did not start the atomic request\n");
            return false;
        }

        // A live memory-stage owner keeps the pending request stable while
        // the cache stalls, even though the execute input has moved on.
        state = {};
        transaction_owner_valid = true;
        memory_stall = true;
        cycle();
        if (!execute_mem.atomic_busy_out() || !execute_mem.mem_read_out()) {
            std::print("ExecuteMem dropped an owned stalled atomic request\n");
            return false;
        }

        // Model a trap flush. The cache can still report busy because of the
        // old request, but no pipeline instruction remains to consume it.
        transaction_owner_valid = false;
        cycle();
        if (execute_mem.atomic_busy_out() || execute_mem.mem_read_out()) {
            std::print("ExecuteMem retained an orphaned atomic request\n");
            return false;
        }

        std::print("ExecuteMem orphan cancellation PASSED\n");
        return true;
    }
};

int main()
{
    TestExecuteMem test;
    return test.run() ? 0 : 1;
}

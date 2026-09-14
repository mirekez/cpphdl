#pragma once

#include <cstdio>
#include <cstdlib>
#include <random>

long _system_clock = 0;

static void check(bool okay, const char* message)
{
    if (!okay) {
        std::fprintf(stderr, "cycle %ld: %s\n", _system_clock, message);
        std::exit(1);
    }
}

// Native behavioral tests only. Unsupported RTL conversion has separate tests;
// it must not be reported as a passing Verilator container simulation.
template<class Model>
struct Bench
{
    Model dut;
    bool valid = false, ready = false;
    uint32_t operation = 0, key = 0, value = 0;
    Bench() { _assign(); }
    void _assign()
    {
        dut.command_valid_in = _ASSIGN_REG(valid);
        dut.response_ready_in = _ASSIGN_REG(ready);
        dut.operation_in = _ASSIGN_REG(operation);
        dut.key_in = _ASSIGN_REG(key);
        dut.value_in = _ASSIGN_REG(value);
        dut._assign();
    }
    void tick(bool reset = false)
    {
        dut._work(reset); dut._strobe(); ++_system_clock;
    }
    void transaction(uint32_t op, uint32_t k, uint32_t v,
                     uint32_t status, uint32_t result, uint32_t size)
    {
        check(dut.command_ready_out(), "not ready for command");
        operation = op; key = k; value = v; valid = true; ready = false;
        tick(); valid = false;
        operation = 99; key = 0xdeadbeef; value = 0xfedcba98;
        for (unsigned hold = 0; hold < 4; ++hold) {
            check(dut.response_valid_out(), "missing response");
            check(!dut.command_ready_out(), "accepted command over pending response");
            check(dut.status_out() == status && dut.value_out() == result && dut.size_out() == size,
                  "reference mismatch or unstable response");
            tick();
        }
        ready = true; tick(); ready = false;
    }
};

template<class Model, class ApplyOperation>
int run_container_test(const char* name, ApplyOperation apply_operation)
{
    Bench<Model> bench;
    SoftwareContainer reference;
    std::mt19937 random(142);
    bench.tick(true); bench.tick(true);
    for (unsigned test = 0; test < 400; ++test) {
        uint32_t op = test < HLS_CAPACITY + 2 ? 0 : random() % 5;
        uint32_t key = test < HLS_CAPACITY + 2 ? test : random() % (HLS_CAPACITY + 2);
        uint32_t value = random();
        uint32_t status = 0, result = 0;
        if (op == 3) result = reference.size();
        else if (op > 3) status = 3;
        else apply_operation(reference, op, key, value, status, result);
        bench.transaction(op, key, value, status, result, reference.size());
        if (test == 199) { bench.tick(true); reference.clear(); }
    }
    bench.valid = true; bench.operation = 0; bench.key = 42; bench.value = 123;
    bench.tick(); bench.valid = false; bench.tick(true);
    bench.transaction(3, 0, 0, 0, 0, 0);
    std::printf("PASS native std::%s %s: 400 commands and reset/backpressure\n",
                name, HLS_HEAP ? "heap object" : "member object");
    return 0;
}

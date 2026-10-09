#include "cpphdl_runtime.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

long _system_clock = 0;
using namespace firtool_cpphdl_runtime;

static void store(DRAMModel& memory, uint64_t address, uint64_t value, uint64_t mask)
{
    DRAMInputs in;
    DRAMOutputs out;
    in.awValid = true;
    in.awAddr = address;
    in.awSize = 3;
    memory.tick(in, out);
    in = {};
    in.wValid = true;
    in.wData = value;
    in.wStrb = mask;
    in.wLast = true;
    memory.tick(in, out);
    if (!out.bValid) throw std::runtime_error("missing write response");
    in = {};
    in.bReady = true;
    memory.tick(in, out);
}

static uint64_t load(DRAMModel& memory, uint64_t address)
{
    DRAMInputs in;
    DRAMOutputs out;
    in.arValid = true;
    in.arAddr = address;
    in.arSize = 3;
    memory.tick(in, out);
    if (!out.rValid || !out.rLast) throw std::runtime_error("missing read response");
    uint64_t result = out.rData;
    in = {};
    in.rReady = true;
    memory.tick(in, out);
    return result;
}

int main()
{
    auto first = std::make_unique<DRAMModel>();
    DRAMModel second;
    store(*first, 0x80001000, 0x0123456789abcdef, 0xff);
    if (load(second, 0x80001000) != 0x0123456789abcdef) return 1;
    store(second, 0x80001000, 0xffeeddccbbaa9988, 0x0f);
    if (load(*first, 0x80001000) != 0x01234567bbaa9988) return 2;

    DRAMInputs request;
    DRAMOutputs firstResponse, secondResponse;
    request.arValid = true;
    request.arAddr = 0x80001000;
    request.arSize = 3;
    request.arId = 3;
    first->tick(request, firstResponse);
    request.arId = 7;
    second.tick(request, secondResponse);
    if (!firstResponse.rValid || firstResponse.rId != 3 ||
        !secondResponse.rValid || secondResponse.rId != 7) return 3;
    request = {};
    request.rReady = true;
    second.tick(request, secondResponse);
    request.rReady = false;
    first->tick(request, firstResponse);
    if (!firstResponse.rValid || firstResponse.rId != 3 ||
        firstResponse.rData != 0x01234567bbaa9988 || secondResponse.rValid) return 4;
    request.rReady = true;
    first->tick(request, firstResponse);

    first.reset();
    if (load(second, 0x80001000) != 0x01234567bbaa9988) return 5;
    std::puts("Dual DRAM runtime: shared backing, byte masks, independent responses and lifetimes PASS");
}

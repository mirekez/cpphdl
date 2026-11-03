#pragma once
#include <cstdint>
struct Observation {
    uint64_t pc[3], wb[3];
    uint32_t wdata[4];
    uint32_t address;
    unsigned retire, awlen, wstrb;
    bool wvalid, clock_enable;
};
void model_create();
void model_write_byte(uint64_t offset, uint8_t value);
void model_inputs(bool reset_n, bool jreset_n, bool jclk);
Observation model_observe();
void model_step();

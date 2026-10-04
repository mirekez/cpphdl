// Exercise the same RAM traffic with asynchronous register resets. RAM contents
// survive reset; no write is allowed while the owning process is in reset.
#define SYNTH_MEMORY_ASYNC 1
#include "memory.cpp"

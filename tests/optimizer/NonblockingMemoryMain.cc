#include "NonblockingMemory.h"
#include "NonblockingMemory_optimized_combs.h"

#include <cstdint>

long _system_clock = -1;

int main() {
  NonblockingMemory dut;
  dut.memory[1] = 0x12;
  dut.read_data.set(0);
  dut.address = 1;
  dut.write_data = 0x34;
  dut.write_enable = true;
  dut.read_enable = true;

  calc_all(dut);
  if (static_cast<uint64_t>(dut.memory[1]) != 0x12 ||
      static_cast<uint64_t>(dut.read_data) != 0) {
    return 1;
  }
  commit_optimized_regs(dut);
  if (static_cast<uint64_t>(dut.memory[1]) != 0x34 ||
      static_cast<uint64_t>(dut.read_data) != 0x12) {
    return 2;
  }

  dut.write_enable = false;
  calc_all(dut);
  commit_optimized_regs(dut);
  return static_cast<uint64_t>(dut.read_data) == 0x34 ? 0 : 3;
}

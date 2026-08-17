#include "ProceduralComb.h"
#include "ProceduralComb_optimized_combs.h"

long _system_clock = 0;

int main() {
  ProceduralComb dut;
  dut.result.set(false);
  dut.input = true;

  calc_all(dut);
  if (!dut.first_cache || !dut.second_cache || dut.first_clock != -1 ||
      dut.second_clock != -1) {
    return 1;
  }
  commit_optimized_regs(dut);
  if (!dut.result) {
    return 2;
  }

  ++_system_clock;
  dut.input = false;
  calc_all(dut);
  commit_optimized_regs(dut);
  return dut.result ? 3 : 0;
}

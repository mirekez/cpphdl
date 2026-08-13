#include "OptimizeMath.h"
#include "OptimizeMath_optimized_combs.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef CPPHDL_OPTIMIZE_MATH_BASELINE_DIR
#error "CPPHDL_OPTIMIZE_MATH_BASELINE_DIR is required"
#endif
#ifndef CPPHDL_OPTIMIZE_MATH_COMBS_DIR
#error "CPPHDL_OPTIMIZE_MATH_COMBS_DIR is required"
#endif
#ifndef CPPHDL_OPTIMIZE_MATH_L1_DIR
#error "CPPHDL_OPTIMIZE_MATH_L1_DIR is required"
#endif

long _system_clock = 0;

namespace {

std::string readGeneratedCpp(const std::filesystem::path &directory) {
  std::string result;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
      continue;
    }
    std::ifstream input(entry.path());
    result.append(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
  }
  return result;
}

uint32_t reverse32(uint32_t value) {
  value = ((value >> 1) & 0x55555555u) | ((value & 0x55555555u) << 1);
  value = ((value >> 2) & 0x33333333u) | ((value & 0x33333333u) << 2);
  value = ((value >> 4) & 0x0f0f0f0fu) | ((value & 0x0f0f0f0fu) << 4);
  value = ((value >> 8) & 0x00ff00ffu) | ((value & 0x00ff00ffu) << 8);
  return (value >> 16) | (value << 16);
}

int checkStructure() {
  const std::string baseline =
      readGeneratedCpp(CPPHDL_OPTIMIZE_MATH_BASELINE_DIR);
  const std::string combs = readGeneratedCpp(CPPHDL_OPTIMIZE_MATH_COMBS_DIR);
  const std::string l1 = readGeneratedCpp(CPPHDL_OPTIMIZE_MATH_L1_DIR);
  if (baseline.empty() || combs.empty() || l1.empty()) {
    std::cerr << "missing optimize-math generated source\n";
    return 1;
  }
  if (baseline.find("reverse_cache[0]") == std::string::npos ||
      baseline.find("sign_extend_cache[31]") == std::string::npos) {
    std::cerr << "baseline unexpectedly collapsed math network\n";
    return 2;
  }
  for (const auto *optimized : {&combs, &l1}) {
    if (optimized->find("cpphdl_optimized_math::bit_reverse32") ==
            std::string::npos ||
        optimized->find("0xffffff00ull") == std::string::npos ||
        optimized->find("0xfffffull") == std::string::npos ||
        optimized->find("reverse_cache[0]") != std::string::npos ||
        optimized->find("sign_extend_cache[31]") != std::string::npos) {
      std::cerr << "math replacement structure mismatch\n";
      return 3;
    }
  }
  // The concrete hierarchy optimizer schedules unreplaced procedural combs
  // directly in both modes. Keep the partial bit writes, rather than requiring
  // the legacy non-L1 path to retain an out-of-line partial() call.
  if (combs.find("partial_cache[0]") == std::string::npos ||
      l1.find("partial_cache[0]") == std::string::npos) {
    std::cerr << "partial comb was incorrectly replaced\n";
    return 3;
  }
  return 0;
}

} // namespace

int main() {
  if (const int structure = checkStructure(); structure != 0) {
    return structure;
  }

  OptimizeMath reference;
  OptimizeMath optimized;
  const std::array<uint32_t, 10> values{
      0u,          1u,          0xffffffffu, 0x80000000u, 0x7fffffffu,
      0x01234567u, 0x89abcdefu, 0x00000080u, 0x0000007fu, 0xa5a55a5au};
  for (size_t index = 0; index < values.size(); ++index) {
    reference.input = values[index];
    optimized.input = values[index];
    reference.sign = (index & 1u) != 0;
    optimized.sign = reference.sign;
    reference.partial_cache = 0xa0;
    optimized.partial_cache = 0xa0;

    reference._work(false);
    reference._strobe();
    calc_all(optimized);
    commit_optimized_regs(optimized);

    const uint32_t expectedReverse = reverse32(values[index]);
    const uint32_t expectedReplicate = reference.sign ? 0xfffffu : 0u;
    const uint32_t expectedExtend =
        (reference.sign ? 0xffffff00u : 0u) | (values[index] & 0xffu);
    const uint32_t expectedPartial = reference.sign ? 0xafu : 0xa0u;
    if (static_cast<uint64_t>(reference.reverse_result) != expectedReverse ||
        static_cast<uint64_t>(optimized.reverse_result) != expectedReverse ||
        static_cast<uint64_t>(reference.replicate_result) != expectedReplicate ||
        static_cast<uint64_t>(optimized.replicate_result) != expectedReplicate ||
        static_cast<uint64_t>(reference.sign_extend_result) != expectedExtend ||
        static_cast<uint64_t>(optimized.sign_extend_result) != expectedExtend ||
        static_cast<uint64_t>(reference.partial_result) != expectedPartial ||
        static_cast<uint64_t>(optimized.partial_result) != expectedPartial) {
      std::cerr << "functional mismatch at vector " << index << '\n';
      std::cerr << "partial reference=0x" << std::hex
                << static_cast<uint64_t>(reference.partial_result)
                << " optimized=0x"
                << static_cast<uint64_t>(optimized.partial_result)
                << " expected=0x" << expectedPartial << std::dec << '\n';
      return 4;
    }
    ++_system_clock;
  }
  return 0;
}

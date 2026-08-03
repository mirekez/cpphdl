#include "OptimizeL1.h"
#include "OptimizeL1_optimized_combs.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef CPPHDL_OPTIMIZE_L1_NORMAL_DIR
#error "CPPHDL_OPTIMIZE_L1_NORMAL_DIR must name the normal generated directory"
#endif

#ifndef CPPHDL_OPTIMIZE_L1_L1_DIR
#error "CPPHDL_OPTIMIZE_L1_L1_DIR must name the L1 generated directory"
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

size_t occurrences(const std::string &text, const std::string &needle) {
  size_t count = 0;
  for (size_t position = 0;
       (position = text.find(needle, position)) != std::string::npos;
       position += needle.size()) {
    ++count;
  }
  return count;
}

int checkGeneratedStructure() {
  const std::string normal =
      readGeneratedCpp(CPPHDL_OPTIMIZE_L1_NORMAL_DIR);
  const std::string l1 = readGeneratedCpp(CPPHDL_OPTIMIZE_L1_L1_DIR);
  if (normal.empty() || l1.empty()) {
    std::cerr << "optimizer did not produce inspectable C++ sources\n";
    return 1;
  }

  const size_t normal_wrappers =
      occurrences(normal, "optimize_l1_leaf_value(n1)");
  const size_t l1_wrappers = occurrences(l1, "optimize_l1_leaf_value(");
  if (normal_wrappers != 2 || l1_wrappers != 0) {
    std::cerr << "L1 wrapper elimination regression: normal="
              << normal_wrappers << " l1=" << l1_wrappers << '\n';
    return 2;
  }

  const std::string duplicate_expression =
      "(n0.input) + cpphdl::logic<16>(3)";
  const size_t normal_duplicates = occurrences(normal, duplicate_expression);
  const size_t l1_duplicates = occurrences(l1, duplicate_expression);
  if (normal_duplicates != 2 || l1_duplicates != 1) {
    std::cerr << "L1 exact-expression CSE regression: normal="
              << normal_duplicates << " l1=" << l1_duplicates << '\n';
    return 3;
  }

  if (l1.find("procedural()") != std::string::npos ||
      l1.find("_optimized_value()") != std::string::npos ||
      l1.find("procedural_clock = _system_clock") != std::string::npos ||
      l1.find("procedural_clock == _system_clock") != std::string::npos) {
    std::cerr << "L1 output retained a comb call or memoization clock\n";
    return 4;
  }
  return 0;
}

uint64_t expectedResult(uint64_t input, bool enable) {
  const uint64_t term = (input + 3) & 0xffffu;
  return ((enable ? 6u : 4u) * term) & 0xffffu;
}

} // namespace

int main() {
  if (const int structure = checkGeneratedStructure(); structure != 0) {
    return structure;
  }

  OptimizeL1 normal;
  OptimizeL1 optimized;
  normal._assign();
  normal.result.set(0);
  optimized.result.set(0);

  for (unsigned cycle = 0; cycle < 32; ++cycle) {
    const uint64_t input = cycle * 197u + 11u;
    const bool enable = (cycle % 3u) != 0;
    normal.input = input;
    normal.enable = enable;
    optimized.input = input;
    optimized.enable = enable;

    const unsigned normal_calls = normal.leaf->procedural_invocations;
    const unsigned optimized_calls = optimized.leaf->procedural_invocations;

    normal._work(false);
    normal._strobe();
    calc_all(optimized);
    commit_optimized_regs(optimized);

    const uint64_t expected = expectedResult(input, enable);
    if (static_cast<uint64_t>(normal.result) != expected ||
        static_cast<uint64_t>(optimized.result) != expected) {
      std::cerr << "functional mismatch at cycle " << cycle << '\n';
      return 5;
    }
    if (normal.leaf->work_observed != enable ||
        optimized.leaf->work_observed != enable) {
      std::cerr << "child field rewrite mismatch at cycle " << cycle << '\n';
      return 6;
    }

    const unsigned normal_delta =
        normal.leaf->procedural_invocations - normal_calls;
    const unsigned optimized_delta =
        optimized.leaf->procedural_invocations - optimized_calls;
    if (normal_delta != 2 || optimized_delta != 1) {
      std::cerr << "procedural comb call reduction regression at cycle "
                << cycle << ": normal=" << normal_delta
                << " l1=" << optimized_delta << '\n';
      return 7;
    }
    if (optimized.leaf->procedural_clock != -1) {
      std::cerr << "L1 calc_all updated a removed memoization clock\n";
      return 8;
    }
    ++_system_clock;
  }
  return 0;
}

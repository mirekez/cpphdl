#include "OptimizeThreads.h"
#include "OptimizeThreads_optimized_combs.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef CPPHDL_OPTIMIZE_THREADS_DIR
#error "CPPHDL_OPTIMIZE_THREADS_DIR must name the generated directory"
#endif

long _system_clock = 0;

namespace {

std::string readGeneratedSources() {
  std::string result;
  for (const auto &entry :
       std::filesystem::directory_iterator(CPPHDL_OPTIMIZE_THREADS_DIR)) {
    if (!entry.is_regular_file() ||
        (entry.path().extension() != ".cpp" &&
         entry.path().extension() != ".h")) {
      continue;
    }
    std::ifstream input(entry.path());
    result.append(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
  }
  return result;
}

int checkGeneratedStructure() {
  const std::string source = readGeneratedSources();
  for (unsigned lane = 0; lane < 4; ++lane) {
    const std::string name =
        "OptimizeThreads_optimized_combs_thread_0_" +
        std::to_string(lane) + "_0";
    if (source.find(name) == std::string::npos) {
      std::cerr << "missing generated comb lane " << lane << '\n';
      return 1;
    }
  }
  if (source.find("command_.store(command") == std::string::npos ||
      source.find("completed_[lane - 1].value.store") == std::string::npos ||
      source.find("struct alignas(64) completion_slot") == std::string::npos ||
      source.find("std::memory_order_release") == std::string::npos ||
      source.find("std::memory_order_acquire") == std::string::npos ||
      source.find("pin_caller();") == std::string::npos ||
      source.find("pin_worker(lane);") == std::string::npos ||
      source.find("restore_caller();") == std::string::npos ||
      source.find("pthread_setaffinity_np") == std::string::npos ||
      source.find("allowed_cpus < lane_count") == std::string::npos ||
      source.find("run_lane(0, obj, state);") == std::string::npos ||
      source.find("workers_.emplace_back") == std::string::npos ||
      source.find("optimized_runtime(obj).run(obj, s)") == std::string::npos) {
    std::cerr << "generated comb lanes lack synchronization or dispatch\n";
    return 2;
  }
  if (source.find("cpphdl_optimized_wait_ready") != std::string::npos ||
      source.find("condition_variable") != std::string::npos ||
      source.find("notify_all") != std::string::npos ||
      source.find("void run_stage(") != std::string::npos ||
      source.find("stage_release_.store") != std::string::npos ||
      source.find(".wait(") != std::string::npos) {
    std::cerr << "generated comb lanes contain fine-grained or OS waits\n";
    return 3;
  }
  return 0;
}

} // namespace

int main() {
  if (const int structure = checkGeneratedStructure(); structure != 0) {
    return structure;
  }

  OptimizeThreads normal;
  OptimizeThreads threaded;
  normal.result_a.set(0);
  normal.result_b.set(0);
  normal.result_c.set(0);
  normal.result_d.set(0);
  threaded.result_a.set(0);
  threaded.result_b.set(0);
  threaded.result_c.set(0);
  threaded.result_d.set(0);
  for (unsigned cycle = 0; cycle < 1000; ++cycle) {
    const uint32_t input = cycle * 0x9e3779b9u + 0x31415926u;
    normal.input = input;
    threaded.input = input;
    normal._work(false);
    normal._strobe();
    calc_all(threaded);
    commit_optimized_regs(threaded);
    if (static_cast<uint64_t>(normal.result_a) !=
            static_cast<uint64_t>(threaded.result_a) ||
        static_cast<uint64_t>(normal.result_b) !=
            static_cast<uint64_t>(threaded.result_b) ||
        static_cast<uint64_t>(normal.result_c) !=
            static_cast<uint64_t>(threaded.result_c) ||
        static_cast<uint64_t>(normal.result_d) !=
            static_cast<uint64_t>(threaded.result_d)) {
      std::cerr << "threaded comb mismatch at cycle " << cycle << '\n';
      return 4;
    }
    ++_system_clock;
  }
  return 0;
}

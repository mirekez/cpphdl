#include "Vtop.h"
#include <verilated.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

double sc_time_stamp() { return 0; }

int main(int argc, char **argv)
{
    VerilatedContext context;
    context.randReset(0);
    context.commandArgs(argc, argv);
    auto model = std::make_unique<Vtop>(&context);
    const bool trace = std::getenv("OPENC910_TRACE") != nullptr;
    FILE *trace_file = trace ? std::fopen("retire.log", "w") : nullptr;
    if (trace && !trace_file) return 2;
    uint64_t cycles = 0, commits = 0;
    model->clk = 0;
    model->eval();
    const auto start = std::chrono::steady_clock::now();
    while (!context.gotFinish() && cycles < 2000000) {
        // Observe the retirement bundle consumed by the upcoming rising edge.
        const uint64_t pcs[] = {model->bench_pc0, model->bench_pc1, model->bench_pc2};
        if (model->bench_reset_n) {
            for (unsigned lane = 0; lane != 3; ++lane) {
                if ((model->bench_retire >> lane) & 1) {
                    ++commits;
                    if (trace) std::fprintf(trace_file, "COMMIT %llu %010llx\n",
                        (unsigned long long)commits, (unsigned long long)pcs[lane]);
                }
            }
        }
        ++cycles;
        context.timeInc(1);
        model->clk = 1;
        model->eval();
        if (context.gotFinish()) break;
        context.timeInc(1);
        model->clk = 0;
        model->eval();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    model->final();
    if (trace_file && std::fclose(trace_file)) return 2;
    std::printf("OPENC910_BENCH total_cycles=%llu commits=%llu work_seconds=%.9f\n",
        (unsigned long long)cycles, (unsigned long long)commits, seconds);
    return context.gotFinish() && cycles < 2000000 ? 0 : 1;
}

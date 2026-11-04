#include "model_api.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: run matrix.bin\n"); return 2; }
    FILE* image = std::fopen(argv[1], "rb");
    if (!image) return 2;
    model_create();
    uint64_t offset = 0;
    for (int byte; (byte = std::fgetc(image)) != EOF; ++offset) {
        if (offset >= 524288) return 2;
        model_write_byte(offset, byte);
    }
    const bool image_error = std::ferror(image);
    std::fclose(image);
    if (image_error) return 2;
    const bool trace = std::getenv("OPENC910_TRACE") != nullptr;
    FILE* trace_file = trace ? std::fopen("retire.log", "w") : nullptr;
    if (trace && !trace_file) return 2;
    uint64_t cycles = 0, commits = 0;
    bool reset_n = true, jreset_n = true, jclk = false, finished = false, pass = false;
    Observation previous{};
    model_inputs(reset_n, jreset_n, jclk);
    // Match the original runner's initial eval() before starting its timer.
    (void)model_observe();
    const auto start = std::chrono::steady_clock::now();
    while (!finished && cycles < 2000000) {
        const auto state = model_observe();
        if (reset_n) for (unsigned lane = 0; lane < 3; ++lane) {
            if ((state.retire >> lane) & 1) {
                ++commits;
                if (trace) std::fprintf(trace_file, "COMMIT %llu %010llx\n",
                    (unsigned long long)commits, (unsigned long long)state.pc[lane]);
            }
        }
        ++cycles;
        // Reproduce smart_run/tb_verilator.v's reset counters and delayed
        // console/writeback monitor, including its one-cycle observation delay.
        if (cycles > 10 && cycles < 20) reset_n = false;
        else if (cycles > 20) reset_n = true;
        if (cycles > 40 && cycles < 80) jreset_n = false;
        else if (cycles > 80) jreset_n = true;
        if (cycles % 2 == 0) jclk = !jclk;
        for (unsigned lane = 0; lane < 3; ++lane) pass |= previous.wb[lane] == 0x444333222ull;
        // The upstream third failure term repeats the success signature.
        const bool fail = previous.wb[0] == 0x2382348720ull || previous.wb[1] == 0x2382348720ull;
        finished = pass || fail;
        if (!finished && previous.awlen == 0 && previous.address == 0x01fffff0 &&
            previous.wvalid && state.clock_enable) {
            for (unsigned word = 0; word < 4; ++word)
                if (previous.wstrb == (0xfu << (word * 4))) std::putchar(state.wdata[word] & 255);
        }
        previous = state;
        model_inputs(reset_n, jreset_n, jclk);
        model_step();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (trace_file && std::fclose(trace_file)) return 2;
    FILE* report = std::fopen("run_case.report", "w");
    if (!report) return 2;
    std::fputs(pass ? "TEST PASS" : "TEST FAIL", report);
    if (std::fclose(report)) return 2;
    std::printf("OPENC910_BENCH total_cycles=%llu commits=%llu work_seconds=%.9f\n",
        (unsigned long long)cycles, (unsigned long long)commits, seconds);
    return pass ? 0 : 1;
}

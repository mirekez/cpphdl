#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

static std::filesystem::path tribe_code_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "code";
}

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static bool run_uart_smoke(bool debug = false)
{
    const auto code_dir = tribe_code_dir();
    TestTribe test(debug);
    return test.run((code_dir / "uart.elf").string(),
        0,
        (code_dir / "uart.log").string(),
        100000,
        0,
        0,
        DEFAULT_RAM_SIZE,
        false);
}

int main(int argc, char** argv)
{
    bool noveril = false;
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }

    bool ok = true;

#ifndef VERILATOR
    if (!noveril) {
        std::cout << "Building IOUART Verilator smoke simulation...\n";
        const auto source_root = source_root_dir();
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        ok &= VerilatorCompileInFolder(__FILE__, "IOUART", "Tribe", {"Predef_pkg",
                  "Amo_pkg",
                  "State_pkg",
                  "Rv32i_pkg",
                  "Rv32ic_pkg",
                  "Rv32ic_rv16_pkg",
                  "Rv32im_pkg",
                  "Rv32ia_pkg",
                  "Zicsr_pkg",
                  "Alu_pkg",
                  "Br_pkg",
                  "Sys_pkg",
                  "Csr_pkg",
                  "Mem_pkg",
                  "Wb_pkg",
                  "L1CachePerf_pkg",
                  "TribePerf_pkg",
                  "File",
                  "RAM1PORT",
                  "L1Cache",
                  "L2Cache",
                  "BranchPredictor",
                  "Decode",
                  "Execute",
                  "ExecuteMem",
                  "CSR",
                  "Writeback",
                  "WritebackMem"}, {
                      (source_root / "include").string(),
                      (source_root / "tribe").string(),
                      (source_root / "tribe" / "common").string(),
                      (source_root / "tribe" / "spec").string(),
                      (source_root / "tribe" / "cache").string(),
                      (source_root / "tribe" / "devices").string()});
        ok &= std::system((std::string("IOUART/obj_dir/VTribe") + (debug ? " --debug" : "")).c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    ok &= run_uart_smoke(debug);
    return ok ? 0 : 1;
}

#endif

#include "Variane_testharness.h"
#include "Variane_testharness___024root.h"
#include "RegfileTrace.h"
#include <cstdio>
#include <cstdlib>

extern "C" void __real__ZN19Variane_testharness9eval_stepEv(Variane_testharness*);

// Link-time observation only. Reuse every original CVA6 object unchanged and
// record stable register-file inputs immediately before each rising edge.
// Signal names/bit positions below belong to the retained Verilator build.
extern "C" void __wrap__ZN19Variane_testharness9eval_stepEv(Variane_testharness* model) {
    static auto* file = [] {
        const auto* path = std::getenv("CPPHDL_REGFILE_TRACE");
        auto* output = path ? std::fopen(path, "wb") : nullptr;
        if (!output) std::abort();
        return output;
    }();
    if (model->clk_i) {
        const auto& root = *model->rootp;
        const auto& issue = root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__id_stage_i__DOT__issue_q;
        const unsigned first = (issue[7] >> 23) & 31;
        const unsigned second = ((issue[7] >> 28) | (issue[8] << 4)) & 31;
        const auto& memory = root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__issue_stage_i__DOT__i_issue_read_operands__DOT__gen_asic_regfile__DOT__i_ariane_regfile__DOT__mem;
        static_assert(sizeof(memory) == 32 * sizeof(uint32_t), "observer requires the RV32 register file");
        static_assert(sizeof(root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__wdata_commit_id) == sizeof(uint64_t),
                      "observer requires two RV32 commit ports");
        RegfileTrace record{};
        record.write_data = root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__wdata_commit_id;
        record.expected = uint64_t(memory[first]) | (uint64_t(memory[second]) << 32);
        record.read_addresses = first | (second << 5);
        record.write_addresses = root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__waddr_commit_id;
        record.enables = root.ariane_testharness__DOT__i_ariane__DOT__i_cva6__DOT__we_gpr_commit_id;
        record.reset_n = model->rst_ni;
        if (std::fwrite(&record, sizeof(record), 1, file) != 1) std::abort();
    }
    __real__ZN19Variane_testharness9eval_stepEv(model);
}

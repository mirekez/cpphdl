#include "../hdlcpp_comb.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static void expectVector(const std::vector<std::string>& got,
                         const std::vector<std::string>& expected)
{
    if (got != expected) {
        auto print = [](const char* label, const std::vector<std::string>& values) {
            std::cerr << label << ":";
            for (const auto& value : values) {
                std::cerr << " " << value;
            }
            std::cerr << "\n";
        };
        print("got", got);
        print("expected", expected);
    }
    assert(got == expected);
}

static void testStandaloneIndependent()
{
    std::vector<std::string> lines = {
        "y = a;",
        "z = b;",
    };
    std::vector<std::string> vars = {"y", "z"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"y", "z"});
    expectVector(plan.combined, {});

    auto yLines = hdlcpp::extractIndependentCombLines(lines, "y");
    expectVector(yLines, {"y = a;"});
}

static void testTangledComb()
{
    std::vector<std::string> lines = {
        "a = in;",
        "b = a;",
        "out = b;",
    };
    std::vector<std::string> vars = {"b", "out"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"b", "out"});
    expectVector(plan.combined, {});

    auto bLines = hdlcpp::extractTargetCombLines(lines, vars, "b");
    expectVector(bLines, {
        "a = in;",
        "b = a;",
    });

    auto outLines = hdlcpp::extractTargetCombLines(lines, vars, "out");
    expectVector(outLines, {
        "a = in;",
        "__comb_local_b = a;",
        "out = __comb_local_b;",
    });
}

static void testLocalRewriteDoesNotTouchMemberField()
{
    std::vector<std::string> lines = {
        "target_address = in;",
        "resolved.target_address = target_address;",
    };
    std::vector<std::string> vars = {"resolved", "target_address"};

    auto resolvedLines = hdlcpp::extractTargetCombLines(lines, vars, "resolved");
    expectVector(resolvedLines, {
        "__comb_local_target_address = in;",
        "resolved.target_address = __comb_local_target_address;",
    });
}

static void testRetainedForLoopKeepsLoopMaintenance()
{
    std::vector<std::string> lines = {
        "for (unsigned i = N - 1;;) {",
        "    if (!((uint64_t)(i) >= 0)) break;",
        "    unrelated = i;",
        "    target[i] = data[i];",
        "    if ((uint64_t)(i) == 0) break;",
        "    i--;",
        "}",
    };
    std::vector<std::string> vars = {"target", "unrelated"};

    auto targetLines = hdlcpp::extractTargetCombLines(lines, vars, "target");
    expectVector(targetLines, {
        "for (unsigned i = N - 1;;) {",
        "    if (!((uint64_t)(i) >= 0)) break;",
        "    target[i] = data[i];",
        "    if ((uint64_t)(i) == 0) break;",
        "    i--;",
        "}",
    });
}

static void testMixedIndependentThenTangled()
{
    std::vector<std::string> lines = {
        "ok = in0;",
        "t = in1;",
        "bad = t;",
        "also_bad = bad ^ t;",
    };
    std::vector<std::string> vars = {"ok", "bad", "also_bad"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"ok", "bad", "also_bad"});
    expectVector(plan.combined, {});

    auto badLines = hdlcpp::extractTargetCombLines(lines, vars, "bad");
    expectVector(badLines, {
        "t = in1;",
        "bad = t;",
    });
}

static void testControlLinesStayWithIndependentTarget()
{
    std::vector<std::string> lines = {
        "if (sel) {",
        "    y = a;",
        "}",
        "z = y;",
    };
    std::vector<std::string> vars = {"y", "z"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"y", "z"});
    expectVector(plan.combined, {});

    auto yLines = hdlcpp::extractTargetCombLines(lines, vars, "y");
    expectVector(yLines, {"if (sel) {", "    y = a;", "}"});

    auto zLines = hdlcpp::extractTargetCombLines(lines, vars, "z");
    expectVector(zLines, {"if (sel) {", "    __comb_local_y = a;", "}", "z = __comb_local_y;"});
}

static void testSharedControlAssignmentsStayCombined()
{
    std::vector<std::string> lines = {
        "a = 0;",
        "b = 0;",
        "if (sel0) {",
        "    a = 1;",
        "    b = 1;",
        "}",
        "else if (sel1) {",
        "    a = 1;",
        "}",
        "else {",
        "    a = 0;",
        "    b = 0;",
        "}",
    };
    std::vector<std::string> vars = {"a", "b"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"a", "b"});
    expectVector(plan.combined, {});
}

static void testControlFlowIndependentOverride()
{
    std::vector<std::string> lines = {
        "flu_result_o = branch_result;",
        "flu_trans_id_o = one_cycle_data.trans_id;",
        "if (alu_valid_i) {",
        "    flu_result_o = alu_result;",
        "}",
        "else {",
        "    if (csr_valid_i) {",
        "        flu_result_o = csr_result;",
        "    }",
        "    else {",
        "        if (mult_valid) {",
        "            flu_result_o = mult_result;",
        "            flu_trans_id_o = mult_trans_id;",
        "        }",
        "    }",
        "}",
    };
    std::vector<std::string> vars = {"flu_result_o", "flu_trans_id_o"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"flu_result_o", "flu_trans_id_o"});
    expectVector(plan.combined, {});
}

static void testSharedHandshakeStateStaysCombined()
{
    std::vector<std::string> lines = {
        "issue_n = issue_q;",
        "fetch_entry_ready_o = 0;",
        "decoded_instruction_valid[0] = valid_decode;",
        "if (issue_instr_ack_i[0]) {",
        "    issue_n[0].valid = 0;",
        "}",
        "if (!issue_n[0].valid && fetch_entry_valid_i[0]) {",
        "    fetch_entry_ready_o[0] = ready_decode;",
        "    issue_n[0] = next_issue;",
        "}",
        "if (flush_i) {",
        "    issue_n[0].valid = 0;",
        "}",
    };
    std::vector<std::string> vars = {"decoded_instruction_valid", "fetch_entry_ready_o", "issue_n"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"decoded_instruction_valid", "fetch_entry_ready_o", "issue_n"});
    expectVector(plan.combined, {});
}

static void testGeneratedIndexedHandshakeStateStaysCombined()
{
    std::vector<std::string> lines = {
        "issue_n = issue_q;",
        "fetch_entry_ready_o = 0;",
        "decoded_instruction_valid[(unsigned)(uint64_t)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))] = valid_decode;",
        "if (logic<1>(issue_instr_ack_i_in()[(unsigned)(uint64_t)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))])) {",
        "    issue_n[0].valid = logic<1>(0b0);",
        "}",
        "if (!issue_n[(unsigned)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))].valid && fetch_entry_valid_i_in()[0]) {",
        "    fetch_entry_ready_o[(unsigned)(uint64_t)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))] = ready_decode;",
        "    issue_n[(unsigned)((uint64_t)(((uint64_t)(0) & ((1ull << 32) - 1ull))))] = { decoded_instruction_valid[0], decoded_instruction[0] };",
        "}",
        "if (flush_i_in()) {",
        "    issue_n[0].valid = logic<1>(0b0);",
        "}",
    };
    std::vector<std::string> vars = {"decoded_instruction_valid", "fetch_entry_ready_o", "issue_n"};
    auto plan = hdlcpp::planCombExtraction(lines, vars);
    expectVector(plan.independent, {"decoded_instruction_valid", "fetch_entry_ready_o", "issue_n"});
    expectVector(plan.combined, {});
}

static void testRewriteGeneratedLhsBase()
{
    std::string storageLine = "    result_o_comb[i].payload = data;";
    assert(hdlcpp::rewriteLhsBase(storageLine, "result_o_comb", "result_o"));
    assert(storageLine == "    result_o[i].payload = data;");

    std::string portLine = "result_o_out()[i].addr = pc;";
    assert(hdlcpp::rewriteLhsBase(portLine, "result_o_out", "result_o"));
    assert(portLine == "result_o[i].addr = pc;");

    std::string rhsOnly = "x = result_o_comb[i].payload;";
    assert(!hdlcpp::rewriteLhsBase(rhsOnly, "result_o_comb", "result_o"));
    assert(rhsOnly == "x = result_o_comb[i].payload;");
}

static void testDeclarationIsNotAssignmentBase()
{
    assert(hdlcpp::assignmentBase("u32 count = 0;").empty());
    assert(hdlcpp::assignmentBase("logic<1> flag = 0;").empty());
    assert(hdlcpp::assignmentBase("flag = 0;") == "flag");
    assert(hdlcpp::declarationName("u32 count = 0;") == "count");
    assert(hdlcpp::declarationName("satp_t satp;") == "satp");
    assert(hdlcpp::declarationName("flag = 0;").empty());
}

static void testStructOutputFieldThroughLocal()
{
    std::vector<std::string> lines = {
        "addr = base;",
        "if (sel) {",
        "    addr = next;",
        "}",
        "out.vaddr = addr;",
    };
    std::vector<std::string> vars = {"out", "addr"};

    auto outLines = hdlcpp::extractTargetCombLines(lines, vars, "out");
    expectVector(outLines, {
        "__comb_local_addr = base;",
        "if (sel) {",
        "    __comb_local_addr = next;",
        "}",
        "out.vaddr = __comb_local_addr;",
    });
}

static void testLargeCombStaysCombined()
{
    std::vector<std::string> lines;
    for (int i = 0; i < 70; ++i) {
        lines.push_back("if (cond" + std::to_string(i) + ") {");
        lines.push_back("}");
    }
    lines.push_back("a = b;");
    lines.push_back("c = d;");

    auto plan = hdlcpp::planCombExtraction(lines, {"a", "c"});
    expectVector(plan.independent, {"a", "c"});
    expectVector(plan.combined, {});
}

static void testFieldExtractionAvoidsUnneededFields()
{
    std::vector<std::string> lines = {
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(ISSUE);i++) {",
        "    entry[i].instruction = 0;",
        "    entry[i].address = pc_j_comb_func()[i];",
        "    entry[i].branch_predict.predict_address = next_pc;",
        "    entry[i].branch_predict.cf = NoCF;",
        "}",
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(FETCH);i++) {",
        "    if (idx[i]) {",
        "        entry[0].instruction = data[i].instr;",
        "        entry[0].address = next_addr;",
        "        entry[0].branch_predict.cf = data[i].cf;",
        "    }",
        "}",
    };

    auto fieldLines = hdlcpp::extractTargetFieldCombLines(
        lines, "entry", "instruction", "__field", "__idx");
    expectVector(fieldLines, {
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(ISSUE);i++) {",
        "if ((uint64_t)(i) == (uint64_t)(__idx)) {",
        "    __field = 0;",
        "}",
        "}",
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(FETCH);i++) {",
        "    if (idx[i]) {",
        "if ((uint64_t)(0) == (uint64_t)(__idx)) {",
        "    __field = data[i].instr;",
        "}",
        "    }",
        "}",
    });

    auto nestedFieldLines = hdlcpp::extractTargetFieldCombLines(
        lines, "entry", "branch_predict.cf", "__field", "__idx");
    expectVector(nestedFieldLines, {
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(ISSUE);i++) {",
        "if ((uint64_t)(i) == (uint64_t)(__idx)) {",
        "    __field = NoCF;",
        "}",
        "}",
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(FETCH);i++) {",
        "    if (idx[i]) {",
        "if ((uint64_t)(0) == (uint64_t)(__idx)) {",
        "    __field = data[i].cf;",
        "}",
        "    }",
        "}",
    });
}

static void testFieldExtractionHandlesIndexedField()
{
    std::vector<std::string> lines = {
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(ENTRIES);i++) {",
        "    fwd_o.sbe[i] = mem_q[i].sbe;",
        "}",
        "fwd_o.wb = wb_comb_func();",
    };

    auto fieldLines = hdlcpp::extractTargetFieldCombLines(
        lines, "fwd_o", "sbe", "__field", "__idx");
    expectVector(fieldLines, {
        "for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(ENTRIES);i++) {",
        "if ((uint64_t)(i) == (uint64_t)(__idx)) {",
        "    __field = mem_q[i].sbe;",
        "}",
        "}",
    });

    auto allLines = hdlcpp::extractTargetCombLines(lines, {"fwd_o"}, "fwd_o");
    expectVector(allLines, lines);
}

int main()
{
    testStandaloneIndependent();
    testTangledComb();
    testLocalRewriteDoesNotTouchMemberField();
    testRetainedForLoopKeepsLoopMaintenance();
    testMixedIndependentThenTangled();
    testControlLinesStayWithIndependentTarget();
    testSharedControlAssignmentsStayCombined();
    testControlFlowIndependentOverride();
    testSharedHandshakeStateStaysCombined();
    testGeneratedIndexedHandshakeStateStaysCombined();
    testRewriteGeneratedLhsBase();
    testDeclarationIsNotAssignmentBase();
    testStructOutputFieldThroughLocal();
    testLargeCombStaysCombined();
    testFieldExtractionAvoidsUnneededFields();
    testFieldExtractionHandlesIndexedField();
    return 0;
}

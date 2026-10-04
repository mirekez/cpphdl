#include "../SharedBlocks.h"
#include "../Combinational.h"
#include <cstdio>
#include <stdexcept>

using namespace cpphdl::hls;

int main() {
    try {
        auto check = [](bool ok) { if (!ok) throw std::runtime_error("shared block regression failed"); };
        std::map<std::string, BlockSymbol> symbols{
            {"left_addr", {64, "left_addr", false}}, {"right_addr", {64, "right_addr", false}},
            {"scratch.first", {32, "first", true}}, {"scratch.second", {32, "second", true}},
            {"scratch.wide", {64, "wide", true}}, {"scratch.input", {32, "input", false}}};
        ScheduledBlock first;
        first.method = "hls_Tree__left";
        first.statements = {"scratch.first = 32'(left_addr);", "scratch.first += scratch.first;"};
        first.condition = "scratch.first != 0";
        first.yes = 2; first.no = 3;
        auto second = first;
        second.statements = {"scratch.second = 32'(right_addr);", "scratch.second += scratch.second;"};
        second.condition = "scratch.second != 0";
        second.yes = 7; second.no = 8;
        auto shared = shareBlocks({first, second}, symbols);
        check(shared.functions.size() == 1 && shared.functions[0].uses == 2);
        check(shared.calls[0].arguments == std::vector<std::string>({"scratch.first", "left_addr"}));
        check(shared.calls[1].arguments == std::vector<std::string>({"scratch.second", "right_addr"}));
        check(shared.functions[0].parameters[0].writable && !shared.functions[0].parameters[1].writable);
        check(shared.functions[0].body.statements[0] == "p_first_0 = 32'(p_left_addr_1);");
        BlockSharing limitedOutputs(symbols);
        const std::set<std::string> none, firstLive{"scratch.first"};
        auto privateCall = limitedOutputs.add(first, &none);
        auto visibleCall = limitedOutputs.add(first, &firstLive);
        check(privateCall.function != visibleCall.function);
        check(!limitedOutputs.functions[privateCall.function].parameters[0].writable);
        check(limitedOutputs.functions[visibleCall.function].parameters[0].writable);
        check(limitedOutputs.functions[privateCall.function].body.statements == shared.functions[0].body.statements);

        auto distinct = [&](ScheduledBlock changed) { check(shareBlocks({first, changed}, symbols).functions.size() == 2); };
        auto changed = first; changed.suspend = true; distinct(changed);
        changed = first; changed.no = -1; distinct(changed);
        changed = first; changed.condition = "scratch.first == 0"; distinct(changed);
        changed = first; changed.method = "hls_Tree__right"; distinct(changed);
        changed = first; changed.statements[1] = "scratch.first += scratch.second;"; distinct(changed);
        changed = first; changed.statements[0] = "scratch.first = 33'(left_addr);"; distinct(changed);
        for (auto name : {"wide", "input"}) {
            changed = first;
            changed.statements = {std::string("scratch.") + name + " = 32'(left_addr);",
                std::string("scratch.") + name + " += scratch." + name + ";"};
            changed.condition = std::string("scratch.") + name + " != 0";
            distinct(changed);
        }
        changed = first;
        changed.statements = {"scratch.first = left_addr_suffix; /* left_addr scratch.first */",
            "$display(\"left_addr scratch.first \\\"text\\\"\"); // scratch.first",
            "other.left_addr = scratch.first;"};
        shared = shareBlocks({changed}, symbols);
        check(shared.calls[0].arguments == std::vector<std::string>({"scratch.first"}));
        check(shared.functions[0].body.statements[0].find("/* left_addr scratch.first */") != std::string::npos);
        check(shared.functions[0].body.statements[1] == changed.statements[1]);
        changed = first; changed.combinational = true; distinct(changed);
        changed = first; changed.memoryBoundary = true; distinct(changed);

        std::vector<ScheduledBlock> memoryLive(3);
        memoryLive[0].statements = {"scratch.first = 32'(scratch.input);", "scratch.second = 0;"};
        memoryLive[0].suspend = memoryLive[0].memoryBoundary = true;
        memoryLive[0].yes = 1;
        memoryLive[1].statements = {"scratch.second = 7;", "scratch.wide = 64'(scratch.first + scratch.second);"};
        memoryLive[1].yes = 2;
        memoryLive[2].statements = {"result = scratch.wide;"};
        check(liveAcrossClocks(memoryLive, symbols) == std::set<std::string>{"scratch.first"});
        memoryLive[1].statements[0] = "if (fault == 0) begin scratch.second = 7; end";
        check(liveAcrossClocks(memoryLive, symbols) == std::set<std::string>({"scratch.first", "scratch.second"}));

        std::vector<ScheduledBlock> diamond(5);
        diamond[0].method = "hls_Tree__left";
        diamond[0].condition = "scratch.first != 0";
        diamond[0].yes = 1; diamond[0].no = 2;
        diamond[1].statements = {"scratch.second = scratch.first;"}; diamond[1].yes = 3;
        diamond[2].statements = {"scratch.second = 0;"}; diamond[2].yes = 3;
        diamond[3].statements = {"scratch.wide = scratch.second + 7;"}; diamond[3].yes = 4;
        BlockSharing regionSharing(symbols);
        auto original = diamond;
        check(outlineCall(diamond, 0, 4, "hls_Tree__caller", regionSharing));
        check(regionSharing.functions.size() == 1 && regionSharing.functions[0].body.combinational);
        const auto& body = regionSharing.functions[0].body.statements[0];
        check(body.find("end else begin") != std::string::npos);
        check(body.find("+ 7;") == body.rfind("+ 7;") && body.find("+ 7;") != std::string::npos);
        check(body.find("active") == std::string::npos && body.find("phase") == std::string::npos);
        check(diamond[0].yes == 4 && diamond[0].no == -1 && diamond[0].method == "hls_Tree__caller");
        diamond = original; diamond[1].suspend = true;
        check(!outlineCall(diamond, 0, 4, "caller", regionSharing));
        diamond = original; diamond[1].yes = 0;
        check(!outlineCall(diamond, 0, 4, "caller", regionSharing));
        check(regionSharing.functions.size() == 1);

        auto copyBody = [&](bool overwrite, bool indexed) {
            std::vector<ScheduledBlock> copies(2);
            copies[0].method = "hls_Copy";
            copies[0].statements = {"scratch.second = 32'(scratch.first);"};
            if (overwrite) copies[0].statements.push_back("scratch.first = 32'(0);");
            copies[0].statements.push_back("scratch.wide = 64'(scratch.second);");
            copies[0].yes = 1;
            copies[1].statements = {"use(scratch.wide);"};
            BlockSharing sharing(symbols);
            BlockUses uses(copies, symbols);
            check(outlineCall(copies, 0, 1, "caller", sharing, {}, indexed ? &uses : nullptr));
            if (indexed) {
                auto outside = uses.outside({{1, {1}}}, -1);
                check(outside.count("scratch.wide"));
                copies[0].statements.clear(); uses.update(0, copies[0]);
                check(!uses.outside({{1, {1}}}, -1).count("scratch.wide"));
            }
            return sharing.functions[0].body.statements[0];
        };
        check(copyBody(false, true).find("p_second_") == std::string::npos);
        check(copyBody(true, true).find("p_second_") != std::string::npos);
        for (bool overwrite : {false, true}) check(copyBody(overwrite, true) == copyBody(overwrite, false));

        std::vector<ScheduledBlock> nested(4);
        nested[0].method = "inner";
        nested[0].statements = {"scratch.second = scratch.first + 1;"}; nested[0].yes = 1;
        nested[1].statements = {"scratch.wide = 64'(scratch.second * 2);"}; nested[1].yes = 2;
        nested[2].statements = {"scratch.first = 32'(scratch.wide);"}; nested[2].yes = 3;
        nested[3].statements = {"use(scratch.first);"};
        BlockSharing nestedSharing(symbols);
        BlockUses nestedUses(nested, symbols);
        check(outlineCall(nested, 0, 2, "outer", nestedSharing, {}, &nestedUses));
        check(outlineCall(nested, 0, 3, "caller", nestedSharing, {}, &nestedUses));
        unsigned liveCount = 0;
        for (const auto& parameter : nestedSharing.functions.back().parameters)
            if (parameter.writable) {
                ++liveCount;
                check(parameter.name.find("p_first_") == 0);
            }
        check(liveCount == 1);

        std::vector<ScheduledBlock> chain(5);
        for (int i = 0; i < 4; ++i) {
            chain[i].yes = i + 1;
            chain[i].statements = {"step" + std::to_string(i)};
        }
        chain[2].suspend = true;
        coalesceBlocks(chain, 0, 0);
        check(chain[0].statements == std::vector<std::string>({"step0", "step1", "step2"}));
        check(chain[0].suspend && chain[0].yes == 3);
        check(chain[3].statements == std::vector<std::string>({"step3"}));
        check(!chain[3].suspend && chain[3].yes == -1);

        SharedBlocks state;
        SharedBlock scalar;
        scalar.body.name = "scalar"; scalar.body.combinational = true;
        scalar.parameters = {{32, "p_value", true}};
        scalar.body.statements = {"if (fault == 0) p_value = p_value + 1; // storage heap_next\n"};
        SharedBlock caller = scalar;
        caller.body.name = "caller";
        caller.body.statements = {"scalar(storage, fault, heap_next, operation_in, index_in, value_in, p_value);"};
        caller.body.statements.push_back("// scalar(storage, fault, heap_next, operation_in, index_in, value_in, p_value);\n");
        SharedBlock memory = scalar;
        memory.body.name = "memory";
        memory.body.statements = {"{storage, fault} = hls_storage_write_32(storage, p_value, 1, fault);"};
        SharedBlock empty;
        empty.body.name = "empty"; empty.body.combinational = true;
        SharedBlock end;
        end.body.name = "end_call";
        end.body.statements = {"empty(storage, fault, heap_next, operation_in, index_in, value_in);"};
        end.body.yes = 1; end.body.no = 2; end.body.suspend = true;
        state.functions = {scalar, caller, memory, empty, end};
        pruneFunctionState(state);
        check(state.functions[0].stateArguments == std::vector<std::string>{"fault"});
        check(state.functions[1].stateArguments == std::vector<std::string>{"fault"});
        check(state.functions[1].body.statements[0] == "scalar(fault, p_value);");
        check(state.functions[1].body.statements[1] == caller.body.statements[1]);
        check(state.functions[2].stateArguments == std::vector<std::string>({"storage", "fault"}));
        check(state.functions[3].stateArguments.empty());
        check(state.functions[4].body.statements[0] == "empty();");
        check(state.functions[4].stateArguments == std::vector<std::string>({"phase", "fault", "active", "next_block", "else_block"}));
        lowerValueFunctionCalls(state);
        check(state.functions[1].body.statements[0] == "hls_call_result = CALL_RESULT_BITS'(scalar(fault, p_value, '0)); {p_value} = $bits({p_value})'(hls_call_result);");
        check(state.functions[1].body.statements[1] == caller.body.statements[1]);
        check(state.functions[4].body.statements[0] == "empty('0);");
        check(valueFunctionCall(state.functions[4], {"phase", "fault", "active", "1", "2"}) ==
              "hls_call_result = CALL_RESULT_BITS'(end_call(phase, fault, active, 1, 2, '0)); {phase, active} = $bits({phase, active})'(hls_call_result)");
        check(state.functions[2].readOnlyState.empty());
        check(state.functions[1].readOnlyState == std::set<std::string>{"fault"});
        SharedBlocks storageState;
        SharedBlock reader = scalar;
        reader.body.name = "reader";
        reader.body.statements = {"p_value = hls_storage_load_32(storage, 16);"};
        SharedBlock writer = caller;
        writer.body.name = "writer";
        writer.body.statements = {"memory(storage, fault, heap_next, operation_in, index_in, value_in, p_value);"};
        storageState.functions = {memory, reader, writer};
        pruneFunctionState(storageState);
        check(storageState.functions[1].readOnlyState == std::set<std::string>{"storage"});
        check(functionOutputs(storageState.functions[1]) == std::vector<size_t>{1});
        check(storageState.functions[2].stateArguments == std::vector<std::string>({"storage", "fault"}));
        check(storageState.functions[2].readOnlyState.empty());
        SharedBlocks banks;
        SharedBlock laneWriter = scalar;
        laneWriter.body.name = "lane_writer";
        laneWriter.body.statements = {"lane0 = lane0 ^ p_value;"};
        SharedBlock laneCaller = caller;
        laneCaller.body.statements = {"lane_writer(lane0, lane1, fault, heap_next, operation_in, index_in, value_in, p_value);"};
        banks.functions = {laneWriter, laneCaller};
        pruneFunctionState(banks, {"lane0", "lane1"});
        for (const auto& function : banks.functions) {
            check(function.stateArguments == std::vector<std::string>{"lane0"});
            check(!function.readOnlyState.count("lane0"));
        }
        check(banks.functions[1].body.statements[0] == "lane_writer(lane0, p_value);");
        ScheduledBlock narrowScratch;
        const std::map<std::string, size_t> resultWidths{{"scalar", 32}, {"wide", 129}, {"unused", 4096}};
        check(callResultScratchWidth(narrowScratch, resultWidths) == 1);
        narrowScratch.statements = {"hls_call_result = scalar(x); // unused(x)", "/* wide(x) */", "prefix_wide(x);"};
        check(callResultScratchWidth(narrowScratch, resultWidths) == 32);
        narrowScratch.statements.push_back("hls_call_result = wide(x);");
        check(callResultScratchWidth(narrowScratch, resultWidths) == 129);
        {
            std::vector<ScheduledBlock> calls(3);
            calls[0].yes = 1; calls[0].suspend = true; calls[0].sharedCallBoundary = true;
            calls[1].yes = 2; calls[1].suspend = true; calls[1].memoryBoundary = true;
            calls[2].yes = 0;
            releaseSharedCallBoundaries(calls);
            check(!calls[0].suspend && calls[1].suspend);
            calls[0].suspend = true;
            calls[1].no = 2; // An early return bypasses the memory clock.
            releaseSharedCallBoundaries(calls);
            check(calls[0].suspend);
            calls[1].no = -1; calls[1].memoryBoundary = false; calls[1].sharedCallBoundary = true;
            releaseSharedCallBoundaries(calls);
            check(!calls[0].suspend && calls[1].suspend);
        }
        std::puts("shared blocks preserve widths, aliases, literals, names, and clock boundaries");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

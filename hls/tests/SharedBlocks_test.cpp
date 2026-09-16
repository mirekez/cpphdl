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
        std::puts("shared blocks preserve widths, aliases, literals, names, and clock boundaries");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

#include "Method.h"
#include "Field.h"
#include "Module.h"

#include <iostream>
#include <fstream>

using namespace cpphdl;

Method* currMethod = nullptr;

bool Method::print(std::ofstream& out)
{
    currMethod = this;

    if (name == "_strobe") {
        return true;
    }

    // make strong comparison here
    if (str_ending(name, "_connect")) {  // important for inherited methods
        return printConns(out);
    }

    if (str_ending(name, "_comb_func")) {
        return printComb(out);
    }

    size_t args_cnt = arguments.size();
    for (auto& arg : arguments) {
        if (arg.name == "clk") {
            --args_cnt;
        }
    }

    if (ret.size() == 0) {
        out << "    task " << escapeIdentifier(name) << " (" << (args_cnt > 1 ? "\n" : "");
    }
    else {
        out << "    function " << (ret[0].value=="std::string"?"string":ret[0].str()) << " " << escapeIdentifier(name) << " (" << (args_cnt > 1 ? "\n" : "");
    }

    bool first = true;
    for (auto& arg : arguments) {
        if (arg.name != "clk") {
//        out << "\n" << arg.expr.debug() << " " << arg.name << "\n";
            out << (args_cnt > 1 ? (first ? "        " : ",       ") : (first ? "" : ", "))
                << (str_ending(arg.name, "_out") ? "output " : "input ") << arg.expr.str() << " " << arg.name << (args_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << (args_cnt>1?"    ":"") << ");" << "\n";

    if (ret.size() == 0) {
        out << "    begin: " << name << "\n";
    }

    for (auto& stmt : statements) {
        stmt.indent = 2;
//        out << stmt.debug() << "\n";
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }

    if (ret.size() == 0) {
        out << "    end\n";
        out << "    endtask\n";
    }
    else {
        out << "    endfunction\n";
    }
    return true;
}

bool Method::printConns(std::ofstream& out)
{
    currMethod = this;

    std::vector<std::string> vars;
    for (auto& stmt : statements) {  // collecting vars from for
        stmt.traverseIf( [&](Expr& e)
            {  // EXPR_FOR (=: EXPR_BINARY (j: EXPR_MEMBER
                if (e.type == Expr::EXPR_FOR
                    && e.sub.size() > 0 && e.sub[0].type == Expr::EXPR_BINARY
                    && e.sub[0].sub.size() > 0 && (e.sub[0].sub[0].type == Expr::EXPR_MEMBER || e.sub[0].sub[0].type == Expr::EXPR_VAR)) {
                    for (auto& str : vars) {
                        if (str == e.sub[0].sub[0].value) {
                            return false;
                        }
                    }
                    vars.push_back(e.sub[0].sub[0].value);
                }
                return false;
            } );
    }
    for (auto& stmt : statements) {  // replacing index names
        stmt.traverseIf( [&](Expr& e)
            {
                if (e.type == Expr::EXPR_MEMBER || e.type == Expr::EXPR_VAR) {
                    for (auto& str : vars) {
                        if (str == e.value) {
                            e.value = std::string("g") + str;
                            return false;
                        }
                    }
                }
                return false;
            } );
    }

    out << "    generate  // " << name <<"\n";
//    if (vars.size()) {
//        out << "    genvar ";
//    }
//    bool first = true;
//    for (auto& var : vars) {
//        out << (!first?",":"") << "g" << var;
//        first = false;
//    }
//    if (vars.size()) {
//        out << ";\n";
//    }
    for (auto& stmt : statements) {
        if (stmt.traverseIf( [](Expr& e) { return e.value == "__inst_name";} )) {
            continue;
        }

//?        if (stmt.type == Expr::EXPR_IF) {
//?            out << stmt.str();
//?        }

        stmt.traverseIf( [&](Expr& e) {
                    if ((e.type == Expr::EXPR_OPERATORCALL && e.value == "=") ||
                        (e.type == Expr::EXPR_BINARY && e.value == "=")) {
                        std::string wname;
                        if (e.sub[0].type == Expr::EXPR_MEMBER && e.sub[0].sub[0].type == Expr::EXPR_INDEX) {
                            wname = e.sub[0].sub[0].sub[0].str() + "__" + e.sub[0].value;
                        } else
                        if (e.sub[0].type == Expr::EXPR_INDEX) {
                            wname = e.sub[0].sub[0].str();
                        } else
                        if (e.sub[0].type == Expr::EXPR_ARRAY) {
                            wname = e.sub[0].sub[1].str();
                        }
                        else {
                            wname = e.sub[0].str();
                        }
                        if (!any_of(currModule->wires.begin(), currModule->wires.end(), [&](auto& w){ return w.name == wname; } ) && wname.length() > 2) {
                             std::cout << "!!! WARNING: can't find wire: " << wname << ": " << e.debug() << " in '" << currModule->name << "'\n";
                        }
                    }
                    if ((e.type == Expr::EXPR_OPERATORCALL || e.type == Expr::EXPR_MEMBERCALL ) && e.value == "assign" && e.sub.size() > 4) {  // Port structire
                        auto tmp = Expr{"gen", Expr::EXPR_BODY};
                        std::string left = e.sub[3].str();
                        if (e.sub[3].type == Expr::EXPR_MEMBER && e.sub[3].sub[0].type == Expr::EXPR_INDEX) {
                            left = e.sub[3].sub[0].sub[0].str() + "__" + e.sub[3].value;
                        } else
                        if (e.sub[3].type == Expr::EXPR_INDEX) {
                            left = e.sub[3].sub[0].str();
                        }
                        std::string right = e.sub[4].str();
                        if (e.sub[4].type == Expr::EXPR_MEMBER && e.sub[4].sub[0].type == Expr::EXPR_INDEX) {
                            right = e.sub[4].sub[0].sub[0].str() + "__" + e.sub[4].value;
                        } else
                        if (e.sub[4].type == Expr::EXPR_INDEX) {
                            right = e.sub[4].sub[0].str();
                        }
                        for (auto& wire : currModule->wires) {
                            if (wire.name.find(left) == 0 && str_ending(wire.name, "_in")) {
                                std::string left1 = e.sub[3].str();
                                std::string array1 = left1.find("[") != (size_t)-1 ? left1.substr(left1.find("[")) : "";
                                std::string right1 = e.sub[4].str();
                                std::string array2 = right1.find("[") != (size_t)-1 ? right1.substr(right1.find("[")) : "";
                                std::string peer = wire.name;
                                str_replace(peer, left.c_str(), right.c_str()); // fix bug
                                peer.replace(peer.length() - 3, 3, "_out");
//                                str_replace(peer, "_in", "_out"); // fix bug _ending
                                tmp.sub.push_back(Expr{"=", Expr::EXPR_BINARY, {Expr{wire.name + array1, Expr::EXPR_VAR}, Expr{peer + array2, Expr::EXPR_VAR}}});
                            }
                            if (wire.name.find(left) == 0 && str_ending(wire.name, "_out")) {
                                std::string left1 = e.sub[3].str();
                                std::string array1 = left1.find("[") != (size_t)-1 ? left1.substr(left1.find("[")) : "";
                                std::string right1 = e.sub[4].str();
                                std::string array2 = right1.find("[") != (size_t)-1 ? right1.substr(right1.find("[")) : "";
                                std::string peer = wire.name;
                                str_replace(peer, left.c_str(), right.c_str()); // fix bug
                                peer.replace(peer.length() - 4, 4, "_in");
//                                str_replace(peer, "_out", "_in"); // fix bug
                                tmp.sub.push_back(Expr{"=", Expr::EXPR_BINARY, {Expr{peer + array1, Expr::EXPR_VAR}, Expr{wire.name + array2, Expr::EXPR_VAR}}});
                            }
                        }
                        e = tmp;
                    }
                    return false;
                } );

        stmt.indent = 2;
        stmt.flags = Expr::FLAG_ASSIGN;
//        out << stmt.debug() << "\n";
        auto s = stmt.str("assign ");
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }
    out << "    endgenerate\n";

    return true;
}

bool Method::printComb(std::ofstream& out)
{
    currMethod = this;

    out << "    always @(*) begin  // " << name <<"\n";
    for (auto& stmt : statements) {
//        out << stmt.debug() << "\n";
        stmt.indent = 2;
        stmt.flags = Expr::FLAG_COMB;
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }
    out << "    end\n";

    return true;
}

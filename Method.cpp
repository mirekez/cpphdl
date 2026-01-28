#include "Method.h"
#include "Field.h"
#include "Module.h"

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

    size_t params_cnt = parameters.size();
    for (auto& param : parameters) {
        if (param.name == "clk") {
            --params_cnt;
        }
    }

    if (ret.size() == 0) {
        out << "    task " << escapeIdentifier(name) << " (" << (params_cnt > 1 ? "\n" : "");
    }
    else {
        out << "    function " << (ret[0].value=="std::string"?"[63:0]":ret[0].str()) << " " << escapeIdentifier(name) << " (" << (params_cnt > 1 ? "\n" : "");
    }

    bool first = true;
    for (auto& param : parameters) {
        if (param.name != "clk") {
            out << (params_cnt > 1 ? (first ? "        " : ",       ") : (first ? "" : ", "))
                << (str_ending(param.name, "_out") ? "output " : "input ") << param.expr.str() << " " << param.name << (params_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << (params_cnt>1?"    ":"") << ");" << "\n";

    if (ret.size() == 0) {
        out << "    begin: " << name << "\n";
    }

    for (auto& stmt : statements) {
        stmt.indent = 2;
//        if (ret.size() != 0) {
//            stmt.flags = Expr::FLAG_RETURN;
//        }
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
//        out << stmt.debug() << "\n";
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
        stmt.traverseIf( [&](Expr& e/*, std::vector<std::string>& vars*/)
            {  // EXPR_FOR (=: EXPR_BINARY (j: EXPR_MEMBER
                if (e.type == Expr::EXPR_FOR
                    && e.sub.size() > 0 && e.sub[0].type == Expr::EXPR_BINARY
                    && e.sub[0].sub.size() > 0 && e.sub[0].sub[0].type == Expr::EXPR_MEMBER) {
                    for (auto& str : vars) {
                        if (str == e.sub[0].sub[0].value) {
                            return false;
                        }
                    }
                    vars.push_back(e.sub[0].sub[0].value);
                }
                return false;
            }/*, vars*/ );
    }
    for (auto& stmt : statements) {  // replacing index names
        stmt.traverseIf( [&](Expr& e/*, std::vector<std::string>& vars*/)
            {
                if (e.type == Expr::EXPR_MEMBER) {
                    for (auto& str : vars) {
                        if (str == e.value) {
                            e.value = std::string("g") + str;
                            return false;
                        }
                    }
                }
                return false;
            }/*, vars*/ );
    }

    out << "    generate\n";
    if (vars.size()) {
        out << "    genvar ";
    }
    bool first = true;
    for (auto& var : vars) {
        out << (!first?",":"") << "g" << var;
        first = false;
    }
    if (vars.size()) {
        out << ";\n";
    }
    for (auto& stmt : statements) {
        if (stmt.traverseIf( [](Expr& e) { return e.value == "__inst_name";} )) {
            continue;
        }

//        bool has_body = false;
        stmt.traverseIf( [&](Expr& e) {
                    e.flags = Expr::FLAG_ASSIGN | Expr::FLAG_NOCALLS;
//                    if (e.type == Expr::EXPR_BODY) {
//                        has_body = true;  // lambda
//                    }
                    return false;
                } );  // we set flags individually, not all Exprs propagate its flags - fix it later

        stmt.indent = 2;
        stmt.flags = Expr::FLAG_ASSIGN | Expr::FLAG_NOCALLS;
        auto s = stmt.str("assign ");
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
//        out << stmt.debug() << "\n";
    }
    out << "    endgenerate\n";

    for (auto& port : currModule->ports) {  // outport initializers
//        out << port.initializer.debug() << "\n";
        if (port.initializer.type != Expr::EXPR_NONE
            && str_ending(port.name, "_out")  // sometimes in ports are assigned 0 in cpphdl, we dont need it in SV
            && port.initializer.sub.size() >= 1 && /*outdated*/ port.initializer.sub[0].value.find("__ZERO") != 0 /*we need assigning to zero only in C++, it's default in Verilog*/
            /*outdated*/ && port.initializer.sub[0].value != "nullptr") {
            port.initializer.flags = Expr::FLAG_ASSIGN;
            std::string s = port.initializer.str();
            if (!s.empty() && s.back() != '\n') {
                s += ";\n";
            }
            out << "    assign " << port.name << " = " << s << "\n";
        }
    }

    return true;
}

bool Method::printComb(std::ofstream& out)
{
    currMethod = this;

/*    std::vector<std::string> vars;
    for (auto& stmt : statements) {  // collecting vars from for
        stmt.traverseIf( [&](Expr& e)
            {  // EXPR_FOR (=: EXPR_BINARY (j: EXPR_MEMBER
                if (e.type == Expr::EXPR_FOR
                    && e.sub.size() > 0 && e.sub[0].type == Expr::EXPR_BINARY
                    && e.sub[0].sub.size() > 0 && e.sub[0].sub[0].type == Expr::EXPR_MEMBER) {
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
                if (e.type == Expr::EXPR_MEMBER) {
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
*/
    out << "    always @(*) begin\n";
    for (auto& stmt : statements) {
        stmt.indent = 2;
        stmt.flags = Expr::FLAG_COMB;
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
//        out << stmt.debug() << "\n";
    }
    out << "    end\n";

    return true;
}

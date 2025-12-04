#include "Method.h"
#include "Field.h"
#include "Module.h"

#include <fstream>

using namespace cpphdl;

Method* currMethod = nullptr;

bool Method::print(std::ofstream& out)
{
    currMethod = this;

    if (name == "strobe") {
        return true;
    }

    if (name == "comb") {
        return true;
    }

    if (name == "connect") {
        return printConns(out);
    }

    if (name.length() > 10 && name.rfind("_comb_func") == name.length()-10) {
        return printComb(out);
    }

    size_t params_cnt = parameters.size();
    for (auto& param : parameters) {
        if (param.name == "clk") {
            --params_cnt;
        }
    }

    if (ret.size() == 0) {
        out << "    task " << name << " (" << (params_cnt > 1 ? "\n" : "");
    }
    else {
        out << "    function " << ret[0].str() << " " << name << " (" << (params_cnt > 1 ? "\n" : "");
    }

    bool first = true;
    for (auto& param : parameters) {
        if (param.name != "clk") {
            out << (params_cnt > 1 ? (first ? "        " : ",       ") : (first ? "" : ", "))
                << (param.name.find("_out") == param.name.size()-4 ? "output " : "input ") << param.expr.str() << " " << param.name << (params_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << (params_cnt>1?"    ":"") << ");" << "\n";

    if (ret.size() == 0) {
        out << "    begin: " << name << "\n";
    }

    for (auto& stmt : statements) {
        stmt.indent = 2;
        if (ret.size() != 0) {
            stmt.flags = Expr::FLAG_RETURN;
        }
        auto s = stmt.str();
        if (s.length() && !stmt.isMultiline()) {
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
//        std::string param;
        if (stmt.traverseIf( [](Expr& e/*, std::string& param*/) { return e.value == "__inst_name";}/*, param*/ )) {
            continue;
        }

        stmt.indent = 2;
        stmt.flags = Expr::FLAG_NORETURN | Expr::FLAG_NOCALLS;
        auto s = stmt.str("assign ");
        if (s.length() && !stmt.isMultiline()) {
            s += ";\n";
        }
        out << s;
    }
    out << "    endgenerate\n";

    for (auto& port : currModule->ports) {  // outport initializers
        if (port.initializer.type != Expr::EXPR_EMPTY
            && port.initializer.sub.size() >= 1 && port.initializer.sub[0].value.find("__ZERO") != 0 /*we need assigning to zero only in C++, it's default in Verilog*/
            && port.initializer.sub[0].value != "nullptr") {
            out << "    assign " << port.name << " = " << port.initializer.str() << ";\n";
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
        stmt.flags = Expr::FLAG_NORETURN;
        auto s = stmt.str();
        if (s.length() && !stmt.isMultiline()) {
            s += ";\n";
        }
        out << s;
    }
    out << "    end\n";

    return true;
}

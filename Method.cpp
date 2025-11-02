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
            out << (params_cnt > 1 ? (first ? "    " : ",    ") : (first ? "" : ", "))
                << (param.name.find("_out") == param.name.size()-4 ? "output " : "input ") << param.type.str() << " " << param.name << (params_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << ");" << "\n";

    for (auto& stmt : statements) {
        stmt.indent = 2;
        stmt.flags = Expr::FLAG_RETURN;
        auto s = stmt.str();
        if (s.length() && !stmt.isMultiline()) {
            s += ";\n";
        }
        out << s;
    }

    if (ret.size() == 0) {
        out << "    endtask" << "\n";
    }
    else {
        out << "    endfunction" << "\n";
    }
    return true;
}

bool Method::printConns(std::ofstream& out)
{
    currMethod = this;

    std::vector<std::string> vars;
    for (auto& stmt : statements) {  // collecting vars from for
        stmt.traverseIf( [](Expr& e, std::vector<std::string>& vars)
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
            }, vars );
    }
    for (auto& stmt : statements) {  // replacing index names
        stmt.traverseIf( [](Expr& e, std::vector<std::string>& vars)
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
            }, vars );
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
        stmt.indent = 2;
        stmt.flags = Expr::FLAG_NORETURN;
        auto s = stmt.str("assign ");
        if (s.length() && !stmt.isMultiline()) {
            s += ";\n";
        }
        out << s;
    }
    out << "    endgenerate\n";


    for (auto& port : currModule->ports) {
        if (port.initializer.type != Expr::EXPR_EMPTY
            && port.initializer.sub.size() >= 1 && port.initializer.sub[0].value != "ZERO" && port.initializer.sub[0].value != "nullptr") {
            out << "    assign " << port.name << " = " << port.initializer.str() << ";\n";
        }
    }

    return true;
}

bool Method::printComb(std::ofstream& out)
{
    currMethod = this;

    for (auto& stmt : statements) {
        stmt.indent = 1;
        stmt.flags = Expr::FLAG_NORETURN;
        auto s = stmt.str("assign ");
        if (s.length() && !stmt.isMultiline()) {
            s += ";\n";
        }
        out << s;
    }
    return true;
}

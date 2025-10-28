#include "Method.h"
#include "Field.h"

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
                << param.type.str() << " " << param.name << (params_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << ")" << "\n";

    for (auto& stmt : statements) {
        stmt.indent = 2;
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

    out << "    generate\n";
    out << "    genvar gi, gj, gz;\n";
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

#include "Method.h"
#include "Field.h"

#include <fstream>

using namespace cpphdl;

bool Method::print(std::ofstream& out)
{
    if (name == "connect") {
        return printConns(out);
    }

    if (ret.size() == 0) {
        out << "    task " << name << " (" << (parameters.size() > 1 ? "\n" : "");
    }
    else {
        out << "    function " << ret[0].str() << " " << name << " (" << (parameters.size() > 1 ? "\n" : "");
    }

    bool first = true;
    for (auto& param : parameters) {
        out << (parameters.size() > 1 ? (first ? "    " : ",    ") : (first ? "" : ", ")) << param.type.str() << " " << param.name << (parameters.size() > 1 ? "\n" : "");
        first = false;
    }
    out << ")" << "\n";

    for (auto& stmt : statements) {
        stmt.indent = 2;
        out << stmt.str();
        if (!stmt.isMultiline()) {
            out << ";\n";
        }
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
    out << "    generate\n";
    out << "    genvar gi, gj, gz;\n";
    for (auto& stmt : statements) {
        stmt.indent = 2;
        out << stmt.str("assign ");
        if (!stmt.isMultiline()) {
            out << ";\n";
        }
    }
    out << "    endgenerate\n";
    return true;
}

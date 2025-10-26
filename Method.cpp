#include "Method.h"
#include "Field.h"

#include <fstream>

using namespace cpphdl;

bool Method::print(std::ofstream& out)
{
    if (name == "connect") {
        printConns(out);
    }

    if (ret == "void") {
        out << "    task " << name << " (" << (parameters.size() > 1 ? "\n" : "");
    }
    else {
        out << "    function " << ret << " " << name << " (" << (parameters.size() > 1 ? "\n" : "");
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

    if (ret == "void") {
        out << "    endtask" << "\n";
    }
    else {
        out << "    endfunction" << "\n";
    }
    return true;
}

bool Method::printConns(std::ofstream& out)
{
    return true;
}

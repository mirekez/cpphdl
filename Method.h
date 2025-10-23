#pragma once

#include <vector>
#include <string>

#include "Expr.h"

namespace cpphdl
{

struct Method
{
    std::string name;
    std::string ret;
    std::vector<Field> parameters;
    std::vector<Expr> statements;

    // methods

    bool print(std::ofstream& out)
    {
        if (ret == "void") {
            out << "    task " << name << "(" << (parameters.size() > 1 ? "\n" : "");
            bool first = true;
            for (auto& param : parameters) {
                out << (first ? "    " : ",    ") << param.type.str() << " " << param.name << " " << (parameters.size() > 1 ? "\n" : "");
                first = false;
            }
            out << ")" << "\n";
        }
        else {
            out << "    function " << ret << " " << name << "("  << ")" << "\n";
        }

        for (auto& stmt : statements) {
            out << "        ";
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

};


}

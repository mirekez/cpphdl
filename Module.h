#pragma once

#include <string>
#include <vector>

#include "Port.h"
#include "Field.h"
#include "Method.h"
#include "Comb.h"

namespace cpphdl
{

struct Module
{
    std::string name;
    std::vector<Field> parameters;
    std::vector<Port> ports;
    std::vector<Field> fields;
    std::vector<Method> methods;
    std::vector<Comb> combs;

    // methods

    bool print(std::ofstream& out)
    {
        out << "module ";
        if (parameters.size()) {
            out <<  " #(\n";
            bool first = true;
            for (auto& param : parameters) {
                out << (first?"    ":",   ") << "parameter " << param.name << "\n";
                first = false;
            }
            out <<  " )\n";
        }
        out << name << "(\n";
        bool first = true;
        for (auto& port : ports) {
            out << (first?"    ":",   ");
            if (!port.print(out)) {
                return false;
            }
            first = false;
        }
        out << ");\n";
        out << "\n";
        for (auto& field : fields) {
            if (!field.print(out)) {
                return false;
            }
        }

        out << "\n";
        for (auto& method : methods) {
            out << "\n";
            if (!method.print(out)) {
                return false;
            }
        }

        out << "\n";
        out << "endmodule\n";
        return true;
    }
};


}

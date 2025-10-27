#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"

#include <fstream>

#include "Project.h"
extern cpphdl::Project prj;

using namespace cpphdl;

bool Module::print(std::ofstream& out)
{
    out << "module";
    if (parameters.size()) {
        out <<  " #(\n";
        bool first = true;
        for (auto& param : parameters) {
            out << (first?"    ":",   ") << "parameter " << param.name << "\n";
            first = false;
        }
        out <<  " )\n";
    }
    out << name << " (\n";
    bool first = true;
    for (auto& port : ports) {
        out << (first?"    ":",   ");
        if (!port.printPort(out)) {
            return false;
        }
        first = false;
    }
    out << ");\n";
    out << "\n";
    for (auto& field : vars) {
        if (!field.print(out)) {
            return false;
        }
    }
    out << "\n";

    printWires(out);

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

bool Module::printWires(std::ofstream& out)
{
    for (auto& field : members) {
        Module* mod = prj.findModule(field.type.value);
        if (mod) {
            for (auto& port : mod->ports) {
                port.type.flags = Expr::FLAG_WIRE;
                out << "    " << port.type.str() << " " << field.name << "__" << port.name << ";\n";  // cant be reg or memory
            }
        }
        else {
            std::cerr << "ERROR: cant find module '" << field.type.value << "' declaration\n";
            return false;
        }
    }
    return true;
}

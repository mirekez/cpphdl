#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"

#include <fstream>

#include "Project.h"

using namespace cpphdl;

Module* currModule = nullptr;

bool Module::print(std::ofstream& out)
{
    currModule = this;

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

    out << "    input wire clk\n";
    out << ",   input wire reset\n";


//    bool first = true;
    for (auto& port : ports) {
        out << ",   ";
        if (!port.printPort(out)) {
            return false;
        }
//        first = false;
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
        method.currModule = name;
        if (!method.print(out)) {
            return false;
        }
    }


    out << "always @(posedge clk) begin\n";
    out << "    work(reset);\n";
    out << "end\n";

    out << "\n";
    out << "endmodule\n";
    return true;
}

bool Module::printWires(std::ofstream& out)
{
    currModule = this;

    for (auto& field : members) {
        Module* mod = currProject->findModule(field.type.value);
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

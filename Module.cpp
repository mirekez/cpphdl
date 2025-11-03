#include "Module.h"
#include "Project.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"

#include <fstream>

using namespace cpphdl;

Module* currModule = nullptr;

bool Module::print(std::ofstream& out)
{
    currModule = this;

    out << "`default_nettype none\n\n";
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
    for (auto& import : imports) {
        std::string str = import;
        size_t pos;
        while ((pos = str.find("::")) != (size_t)-1) {
            str.replace(pos, 2, "__");
        }

        out << "    import " << str << "_pkg" << "::*;\n";
    }
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
    out << "    always @(posedge clk) begin\n";
    out << "        work(reset);\n";
    out << "    end\n";

    out << "\n";
    out << "endmodule\n";
    return true;
}

bool Module::printWires(std::ofstream& out)
{
    currModule = this;

    for (auto& field : members) {
        if (field.type.type == Expr::EXPR_ARRAY && field.type.sub.size() >= 2) {
            Module* mod = currProject->findModule(field.type.sub[1].str());
            if (mod) {
                for (auto& port : mod->ports) {
                    port.type.flags = Expr::FLAG_WIRE;
                    out << "      " << port.type.str() << " " << field.name << "__" << port.name << "[" << field.type.sub[0].str() << "]" << ";\n";  // cant be reg or memory
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << field.type.value << "' declaration\n";
                return false;
            }

            out << "    generate\n";
            out << "    genvar gi;\n";
            out << "    for (gi=0; gi < " << field.type.sub[0].str() << "; gi = gi + 1) begin\n";
            out << "        " << field.type.sub[1].str() << " " << field.name << "(" << "\n";
            out << "            .clk(clk)\n" ;
            out << ",           .reset(reset)\n" ;
            for (auto& port : mod->ports) {
                out << ",           ." << port.name << "(" << field.name << "__" << port.name << "[gi]" << ")" << "\n";  // cant be reg or memory
            }
            out << "        );\n";
            out << "    end\n";
            out << "    endgenerate\n";
        }
        else {
            Module* mod = currProject->findModule(field.type.value);
            if (mod) {
                for (auto& port : mod->ports) {
                    port.type.flags = Expr::FLAG_WIRE;
                    out << "      " << port.type.str() << " " << field.name << "__" << port.name << ";\n";  // cant be reg or memory
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << field.type.value << "' declaration\n";
                return false;
            }

            out << "    " << field.type.value << " " << field.name << "(" << "\n";
            out << "        .clk(clk)\n" ;
            out << ",       .reset(reset)\n" ;
            for (auto& port : mod->ports) {
                out << ",       ." << port.name << "(" << field.name << "__" << port.name << ")" << "\n";  // cant be reg or memory
            }
            out << "    );\n";
        }
    }
    return true;
}

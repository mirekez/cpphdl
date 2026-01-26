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
    out << "import Predef_pkg::*;\n";
    for (auto& import : imports) {
        std::string str = genTypeName(import);
        out << "import " << str << "_pkg::*;\n";
    }
    out << "\n\n";
    out << "module ";
    out << name;
    if (parameters.size()) {
        out <<  " #(\n";
        bool first = true;
        for (auto& param : parameters) {
            out << (first?"    ":",   ") << "parameter " << param.name << "\n";
            first = false;
        }
        out <<  " )\n";
    }
    out << " (\n";
    out << "    input wire clk\n";
    out << ",   input wire reset\n";


//    bool first = true;
    for (auto& port : ports) {
        port.indent = 1;
        out << ",   ";
        if (!port.printPort(out)) {
            return false;
        }
//        first = false;
    }
    out << ");\n";
    for (auto& field : consts) {
        field.indent = 1;
        out << "    parameter ";
        if (!field.print(out)) {
            return false;
        }
    }
    out << "\n";
    for (auto& field : vars) {
        field.indent = 1;
        if (!field.print(out)) {
            return false;
        }
    }
    out << "\n";

    printMembers(out);

    for (auto& method : methods) {
        out << "\n";
        if (!method.print(out)) {
            return false;
        }
    }


    out << "\n";
    out << "    always @(posedge clk) begin\n";
    out << "        _work(reset);\n";
    out << "    end\n";

    out << "\n";
    out << "endmodule\n";
    return true;
}

bool Module::printMembers(std::ofstream& out)
{
    currModule = this;

    for (auto& member : members) {
        member.indent = 1;

        if (member.expr.type == Expr::EXPR_ARRAY && member.expr.sub.size() >= 2) {
            Module* mod = currProject->findModule(member.expr.sub[1].str());
            if (mod) {
                for (auto& port : mod->ports) {  // we cant use parameters of nested module's port in parent module, so we need to replace them with corresponding parameters
                    Expr expr = port.expr;
                    expr.flags = Expr::FLAG_WIRE;
                    for (size_t i=0; i < mod->parameters.size(); ++i) {
                        expr.traverseIf( [&](Expr& e) {
                                if (e.value == mod->parameters[i].name && member.expr.sub.size() > i) {  // port depends on parameter
                                    e.value = std::string("(") + member.expr.sub[i].str() + ")";
                                }
                                return false;
                            });
                    }
                    out << "      " << expr.str() << " " << member.name << "__" << port.name << "[" << member.expr.sub[0].str() << "]" << ";\n";  // cant be reg or memory
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << member.expr.sub[1].str() << "' declaration\n" << member.expr.sub[1].debug() << "\n";
                return false;
            }

            out << "    generate\n";
            out << "    genvar gi;\n";
            out << "    for (gi=0; gi < " << member.expr.sub[0].str() << "; gi = gi + 1) begin\n";
            out << "        " << member.expr.sub[1].str() << " ";
            if (member.expr.sub[0].sub.size()) {
                out << "#(\n";
            }
            bool first = true;
            for (auto& param : member.expr.sub[0].sub) {
                if (param.type == Expr::EXPR_PARAM) {
                    out << (first?"        ":",       ") << param.str() << "\n";
                    first = false;
                }
            }
            if (member.expr.sub[0].sub.size()) {
                out << "    )";
            }
            else {
                out << "    ";
            }
            out << " " << member.name << " (" << "\n";
            out << "            .clk(clk)\n" ;
            out << ",           .reset(reset)\n" ;
            for (auto& port : mod->ports) {
                out << ",           ." << port.name << "(" << member.name << "__" << port.name << "[gi]" << ")" << "\n";  // cant be reg or memory
            }
            out << "        );\n";
            out << "    end\n";
            out << "    endgenerate\n";
        }
        else {
            Module* mod = currProject->findModule(member.expr.str());
            if (mod) {
                for (auto& port : mod->ports) {  // we cant use parameters of nested module's port in parent module, so we need to replace them with corresponding parameters
                    Expr expr = port.expr;
                    expr.flags = Expr::FLAG_WIRE;
                    for (size_t i=0; i < mod->parameters.size(); ++i) {  // we want to extract parameters values which influence port width
                        expr.traverseIf( [&](Expr& e) {
                                if (e.value == mod->parameters[i].name) {
                                    e.value = std::string("(") + member.expr.sub[i].str() + ")";
                                }
                                return false;
                            });
                    }
                    out << "      " << expr.str() << " " << member.name << "__" << port.name << ";\n";  // cant be reg or memory
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << member.expr.str() << "' declaration\n" <<  member.expr.debug() << "\n";
                return false;
            }

            out << "    " << member.expr.str() << " ";
            if (member.expr.type == Expr::EXPR_TEMPLATE && member.expr.sub.size()) {
                out << "#(\n";
//            }
            bool first = true;
            for (auto& param : member.expr.sub) {
                if (param.type == Expr::EXPR_PARAM) {
                    out << (first?"        ":",       ") << param.str() << "\n";
                    first = false;
                }
            }
//            if (member.expr.sub.size()) {
                out << "    )";
            }
            else {
                out << "    ";
            }
            out << " " << member.name << " (" << "\n";
            out << "        .clk(clk)\n" ;
            out << ",       .reset(reset)\n" ;
            for (auto& port : mod->ports) {
                out << ",       ." << port.name << "(" << member.name << "__" << port.name << ")" << "\n";  // cant be reg or memory
            }
            out << "    );\n";
        }
    }
    return true;
}

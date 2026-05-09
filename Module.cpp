#include "Module.h"
#include "Project.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Optimizer.h"

#include <fstream>

using namespace cpphdl;

Module* currModule = nullptr;

void Module::printImports(std::ofstream& out, std::unordered_set<std::string>* importsSet)
{
    std::unordered_set<std::string> importsRoot;
    if (!importsSet) {
        importsSet = &importsRoot;
    }
    for (auto& imp : imports) {
        for (auto& member : members) {
            auto it = std::find_if(currProject->modules.begin(), currProject->modules.end(), [&](auto& m){
//out << "importing " << (member.expr.type == Expr::EXPR_TEMPLATE?member.expr.sub[member.expr.sub.size()-1].str():member.expr.str()) << "..." << m.origName << "\n";
 return (member.expr.type == Expr::EXPR_TEMPLATE?member.expr.sub[member.expr.sub.size()-1].str():member.expr.str()) == m.origName; });

            if (it != currProject->modules.end()) {
                it->printImports(out, importsSet);
            }
        }
        std::string name = genTypeName(imp.name);
        if (importsSet->find(name) == importsSet->end()) {
            importsSet->insert(name);
            out << "import " << name << "_pkg::*;\n";
        }
        auto st = std::find_if(currProject->structs.begin(), currProject->structs.end(), [&](auto& s) {
            return s.name == name;
        });
        if (st != currProject->structs.end()) {
            for (const auto& subImport : collectStructPackageImports(*st)) {
                if (importsSet->find(subImport) == importsSet->end()) {
                    importsSet->insert(subImport);
                    out << "import " << subImport << "_pkg::*;\n";
                }
            }
        }
    }
}

bool Module::print(std::ofstream& out)
{
    currModule = this;
    onceAccessedRegs.clear();

    out << "`default_nettype none\n\n";
    out << "import Predef_pkg::*;\n";
    printImports(out);
    out << "\n\n";
    out << "module ";
    out << name;
    if (parameters.size()) {
        out <<  " #(\n";
        bool first = true;
        for (auto& p : parameters) {
            out << (first?"    ":",   ") << "parameter " << p.name << "\n";
            first = false;
        }
        out <<  " )\n";
    }
    out << " (\n";
    out << "    input wire clk\n";
    out << ",   input wire reset\n";
    for (auto& p : ports) {
        p.indent = 1;
        out << ",   ";
//        out << p.expr.debug() << " : " << (p.array.size()?p.array[0].debug():std::string()) << "\n";
        if (!p.printPort(out)) {
            return false;
        }
    }
    out << ");\n";

    for (auto& f : consts) {
//        out << f.expr.debug();
        out << "    parameter ";
        f.expr.value = f.name;
        if (!f.print(out)) {
            return false;
        }
    }
    out << "\n";

    for (auto& a : aliases) {
//        out << a.expr.debug() << "\n";
        out << "    typedef " << a.expr.str() << " " << a.name << ";\n";
    }
    out << "\n";

    out << "    // regs and combs\n";
    for (auto& field : vars) {
        field.indent = 1;
//        out << field.expr.debug() << "\n";
        if (!field.print(out)) {
            return false;
        }
    }
    out << "\n";

    out << "    // members\n";
    printMembers(out);

    Optimizer opt;
    opt.optimizeBlocking(methods);

    out << "\n";
    out << "    // tmp variables\n";
    for (auto& field : vars) {  // tmp vars
        field.indent = 1;
        if (onceAccessedRegs.find(field.name) == onceAccessedRegs.end()
            && field.expr.traverseIf([](auto& e){ return e.type == Expr::EXPR_TEMPLATE && e.value == "cpphdl_reg"; })) {
            field.expr.flags |= Expr::FLAG_NOTREG;
            if (!field.print(out, "_tmp")) {
                return false;
            }
        }
    }
    out << "\n";

    bool hasWorkNeg = false;
    for (auto& method : methods) {
        out << "\n";
        if (!method.print(out)) {
            return false;
        }
        if (method.name == "_work_neg") {
            hasWorkNeg = true;
        }
    }

    out << "\n";
    out << "    always @(posedge clk) begin\n";
    for (auto& field : vars) {  // strobe
        field.indent = 1;
        if (onceAccessedRegs.find(field.name) == onceAccessedRegs.end()
            && field.expr.traverseIf([](auto& e){ return e.type == Expr::EXPR_TEMPLATE && e.value == "cpphdl_reg"; })) {
            out << "        " << field.name << "_tmp = " << field.name << ";\n";
        }
    }
    out << "\n";
    out << "        _work(reset);\n";
    out << "\n";
    for (auto& field : vars) {  // strobe
        field.indent = 1;
        if (onceAccessedRegs.find(field.name) == onceAccessedRegs.end()
            && field.expr.traverseIf([](auto& e){ return e.type == Expr::EXPR_TEMPLATE && e.value == "cpphdl_reg"; })) {
            out << "        " << field.name << " <= " << field.name << "_tmp;\n";
        }
    }
    out << "    end\n";

    if (hasWorkNeg) {
        out << "\n";
        out << "    always @(negedge clk) begin\n";
        out << "        _work_neg(reset);\n";
        out << "    end\n";
    }

    out << "\n";

    for (auto& port : currModule->ports) {  // outport initializers
//        out << port.initializer.debug() << "\n";
        if (port.initializer.type != Expr::EXPR_NONE
            && str_ending(port.name, "_out")  // sometimes in ports are assigned 0 in cpphdl, we dont need it in SV
            && port.initializer.sub.size() >= 1 && /*outdated*/ port.initializer.sub[0].value.find("__ZERO") != 0 /*we need assigning to zero only in C++, it's default in Verilog*/
            /*outdated*/ && port.initializer.sub[0].value != "nullptr") {
            port.initializer.flags = Expr::FLAG_ASSIGN;
            std::string s = port.initializer.str();
            if (!s.empty() && s.back() != '\n') {
                s += ";\n";
            }
            out << "    assign " << port.name << " = " << s << "\n";
        }
    }
    out << "\n";

    out << "endmodule\n";
    return true;
}

bool Module::printMembers(std::ofstream& out)
{
    currModule = this;

    out << "    genvar gi, gj, gk;\n";

    // printWires for members
    for (auto& member : members) {
        member.indent = 1;
        if (member.array.size()) {  // array of members
            Module* mod = currProject->findModule(member.expr.str());
            if (mod) {
                for (auto& port : mod->ports) {  // we cant use parameters of nested module's port in parent module, so we need to replace them with corresponding parameters
                    Expr expr = port.expr;
                    expr.flags = Expr::FLAG_WIRE;
                    for (size_t i=0; i < mod->consts.size(); ++i) {
                        expr.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->consts[i].name) {  // port of mod depends on its parameter
                                    e = mod->consts[i].expr.sub[0];
                                }
                                return false;
                            });
                    }
                    for (size_t i=0; i < mod->parameters.size(); ++i) {
                        expr.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->parameters[i].name) {  // port of mod depends on its parameter
                                    size_t memberParamIndex = -1;
                                    for (size_t j=0; j < i+1 && memberParamIndex+1 < member.expr.sub.size();) {  // skip all non numeric parameters
                                        ++memberParamIndex;
                                        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
                                            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {  // need to count only number parameters for members templates
                                            ++j;
                                        }
                                    }
                                    e.value = std::string("(") + member.expr.sub[memberParamIndex].str() + ")";
                                }
                                return false;
                            });
                    }
                    Expr array;
                    if (port.array.size()) {  // supporting only one dimension now
                        array = port.array[0];
                    }
                    for (size_t i=0; i < mod->parameters.size(); ++i) {
                        array.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->parameters[i].name) {  // port of mod depends on its parameter
                                    size_t memberParamIndex = -1;
                                    for (size_t j=0; j < i+1 && memberParamIndex+1 < member.expr.sub.size();) {  // skip all non numeric parameters
                                        ++memberParamIndex;
                                        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
                                            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {  // need to count only number parameters for members templates
                                            ++j;
                                        }
                                    }
                                    e.value = std::string("(") + member.expr.sub[memberParamIndex].str() + ")";
                                }
                                return false;
                            });
                    }
//                    out << expr.debug() << "\n";
                    if (array.type != Expr::EXPR_NONE) {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name << "[" << member.expr.sub[0].str() << "]" << "[" << array.str() << "]" << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    }
                    else {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name << "[" << member.array[0].str() << "]" << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    }
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << member.expr.str() << "' declaration\n" << member.expr.debug() << "\n";
                return false;
            }

            out << "    generate\n";
            out << "    for (gi=0; gi < " << member.array[0].str() << "; gi = gi + 1) begin\n";
            out << "        " << member.expr.str() << " ";
            if (member.expr.sub.size()) {
                out << "#(\n";
            }
            bool first = true;
            for (auto& param : member.expr.sub) {
                if (param.type == Expr::EXPR_PARAM) {
//                    out << param.debug() << "\n";
                    out << (first?"        ":",       ") << param.str() << "\n";
                    first = false;
                }
            }
            if (member.expr.sub.size()) {
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
                    for (size_t i=0; i < mod->consts.size(); ++i) {  // we want to extract parameters values which influence port width
                        expr.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->consts[i].name) {  // port of mod depends on its parameter
                                    e = mod->consts[i].expr.sub[0];
                                }
                                return false;
                            });
                    }
                    for (size_t i=0; i < mod->parameters.size(); ++i) {  // we want to extract parameters values which influence port width
                        expr.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->parameters[i].name) {  // port of mod depends on its parameter
                                    size_t memberParamIndex = -1;
                                    for (size_t j=0; j < i+1 && memberParamIndex+1 < member.expr.sub.size();) {  // skip all non numeric parameters
                                        ++memberParamIndex;
                                        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
                                            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {  // need to count only number parameters for members templates
                                            ++j;
                                        }
                                    }
                                    e.value = std::string("(") + member.expr.sub[memberParamIndex].str() + ")";
                                }
                                return false;
                            });
                    }
                    Expr array;
                    if (port.array.size()) {  // supporting only one dimension now
                        array = port.array[0];
                    }
                    for (size_t i=0; i < mod->parameters.size(); ++i) {  // we want to extract parameters values which influence array sizwe
                        array.traverseIf( [&](Expr& e) {
                                if (e.type == Expr::EXPR_VAR && e.value == mod->parameters[i].name) {  // port of mod depends on its parameter
                                    size_t memberParamIndex = -1;
                                    for (size_t j=0; j < i+1 && memberParamIndex+1 < member.expr.sub.size();) {  // skip all non numeric parameters
                                        ++memberParamIndex;
                                        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
                                            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {  // need to count only number parameters for members templates
                                            ++j;
                                        }
                                    }
                                    e.value = std::string("(") + member.expr.sub[memberParamIndex].str() + ")";
                                }
                                return false;
                            });
                    }
//                    out << array.debug() << "\n";
//                    out << expr.debug() << "\n";
//                    out << port.expr.debug() << "\n";
                    if (array.type != Expr::EXPR_NONE) {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name << "[" << array.str() << "]" << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    } else {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    }
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
//                    out << param.debug() << "\n";
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

bool Module::isReg(const std::string& name)
{
    return std::any_of(vars.begin(), vars.end(), [&](auto& field) {
        return field.name == name && field.expr.traverseIf([](auto& e) {
            return e.type == Expr::EXPR_TEMPLATE && e.value == "cpphdl_reg";
        });
    });
}

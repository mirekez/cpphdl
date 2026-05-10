#include "Module.h"
#include "Project.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Optimizer.h"

#include <fstream>
#include <sstream>

using namespace cpphdl;

Module* currModule = nullptr;

namespace
{

std::string svArrayDims(const std::vector<Expr>& dims)
{
    std::string text;
    for (auto dim : dims) {
        text += "[" + dim.str() + "]";
    }
    return text;
}

std::string svIndex(const std::vector<std::string>& indices)
{
    std::string text;
    for (const auto& index : indices) {
        text += "[" + index + "]";
    }
    return text;
}

std::vector<std::string> memberArrayIndices(size_t dims)
{
    static const std::vector<std::string> names = {"gi", "gj", "gk"};
    return std::vector<std::string>(names.begin(), names.begin() + std::min(dims, names.size()));
}

void replaceModuleParameterRefs(Expr& expr, const Module& mod, const Field& member)
{
    for (size_t i=0; i < mod.parameters.size(); ++i) {
        expr.traverseIf( [&](Expr& e) {
                if (e.type == Expr::EXPR_VAR && e.value == mod.parameters[i].name) {
                    size_t memberParamIndex = -1;
                    for (size_t j=0; j < i+1 && memberParamIndex+1 < member.expr.sub.size();) {
                        ++memberParamIndex;
                        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
                            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {
                            ++j;
                        }
                    }
                    Expr memberParam = member.expr.sub[memberParamIndex];
                    e.value = std::string("(") + memberParam.str() + ")";
                }
                return false;
            });
    }
}

void replaceModuleConstRefs(Expr& expr, const Module& mod)
{
    for (size_t i=0; i < mod.consts.size(); ++i) {
        expr.traverseIf( [&](Expr& e) {
                if (e.type == Expr::EXPR_VAR && e.value == mod.consts[i].name) {
                    e = mod.consts[i].expr.sub[0];
                }
                return false;
            });
    }
}

Expr portArrayExpr(const Field& port, const Module& mod, const Field& member)
{
    Expr array;
    if (port.array.size()) {
        array = port.array[0];
        replaceModuleParameterRefs(array, mod, member);
    }
    return array;
}

Expr portWireExpr(const Field& port, const Module& mod, const Field& member)
{
    Expr expr = port.expr;
    expr.flags = Expr::FLAG_WIRE;
    replaceModuleConstRefs(expr, mod);
    replaceModuleParameterRefs(expr, mod, member);
    return expr;
}

}

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
            if (member.array.size() > 3) {
                std::cerr << "ERROR: member array '" << member.name << "' has " << member.array.size()
                          << " dimensions, but only up to 3 dimensions are supported\n";
                return false;
            }

            Module* mod = currProject->findModule(member.expr.str());
            if (mod) {
                for (auto& port : mod->ports) {
                    Expr expr = portWireExpr(port, *mod, member);
                    Expr array = portArrayExpr(port, *mod, member);
                    if (array.type != Expr::EXPR_NONE) {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name
                            << svArrayDims(member.array) << "[" << array.str() << "]" << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    }
                    else {
                        out << "      " << expr.str() << " " << member.name << "__" << port.name
                            << svArrayDims(member.array) << ";\n";  // cant be reg or memory
                        wires.push_back(Field{member.name + "__" + port.name, expr});
                    }
                }
            }
            else {
                std::cerr << "ERROR: cant find module '" << member.expr.str() << "' declaration\n" << member.expr.debug() << "\n";
                return false;
            }

            out << "    generate\n";
            auto indices = memberArrayIndices(member.array.size());
            for (size_t dim = 0; dim < member.array.size(); ++dim) {
                out << std::string(4 + dim * 4, ' ') << "for (" << indices[dim] << "=0; "
                    << indices[dim] << " < " << member.array[dim].str() << "; "
                    << indices[dim] << " = " << indices[dim] << " + 1) begin\n";
            }
            out << std::string(4 + member.array.size() * 4, ' ') << member.expr.str() << " ";
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
                out << std::string(4 + member.array.size() * 4, ' ') << ")";
            }
            else {
                out << std::string(4 + member.array.size() * 4, ' ');
            }
            out << " " << member.name << " (" << "\n";
            out << std::string(8 + member.array.size() * 4, ' ') << ".clk(clk)\n" ;
            out << std::string(4 + member.array.size() * 4, ' ') << ",           .reset(reset)\n" ;
            for (auto& port : mod->ports) {
                out << std::string(4 + member.array.size() * 4, ' ') << ",           ." << port.name
                    << "(" << member.name << "__" << port.name << svIndex(indices) << ")" << "\n";  // cant be reg or memory
            }
            out << std::string(4 + member.array.size() * 4, ' ') << ");\n";
            for (size_t dim = member.array.size(); dim > 0; --dim) {
                out << std::string(4 + (dim - 1) * 4, ' ') << "end\n";
            }
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

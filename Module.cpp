#include "Module.h"
#include "Project.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Enum.h"
#include "Optimizer.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

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
    static const std::vector<std::string> names = {
        "__i", "__j", "__k", "__l", "__m", "__n", "__o", "__p"
    };
    std::vector<std::string> indices;
    for (size_t i = 0; i < dims; ++i) {
        indices.push_back(i < names.size() ? names[i] : "__i" + std::to_string(i));
    }
    return indices;
}

Expr memberNumericParameterActual(const Module& mod, const Field& member, size_t parameterIndex)
{
    // Member expressions keep only numeric template actuals as SV parameters.
    // Walk through the original argument list and count only those numeric
    // entries so mod.parameters[N] maps to the correct member actual.
    size_t memberParamIndex = static_cast<size_t>(-1);
    for (size_t j = 0; j < parameterIndex + 1 && memberParamIndex + 1 < member.expr.sub.size();) {
        ++memberParamIndex;
        if (member.expr.sub[memberParamIndex].type == Expr::EXPR_PARAM
            || member.expr.sub[memberParamIndex].type == Expr::EXPR_NUM) {
            ++j;
        }
    }
    if (memberParamIndex < member.expr.sub.size()) {
        return member.expr.sub[memberParamIndex];
    }
    return mod.parameters[parameterIndex].expr;
}

bool replaceModuleParameterRefs(Expr& expr, const Module& mod, const Field& member)
{
    // Parent-side wires for a nested module must use the instantiated member's
    // numeric actuals. A child port sized by WIDTH must become [4] for
    // Child<4> child; otherwise WIDTH is undefined in the parent module.
    bool changed = false;
    for (size_t i=0; i < mod.parameters.size(); ++i) {
        expr.traverseIf( [&](Expr& e) {
                if (e.type == Expr::EXPR_VAR && e.value == mod.parameters[i].name) {
                    e = memberNumericParameterActual(mod, member, i);
                    changed = true;
                    return true;
                }
                return false;
            });
    }
    return changed;
}

bool replaceModuleConstRefs(Expr& expr, const Module& mod)
{
    // Child ports can be sized through local static constexpr aliases, for
    // example AAA = WIDTH. The parent cannot see AAA, so expand it before
    // writing the parent-side child__port wire declaration.
    bool changed = false;
    for (size_t i=0; i < mod.consts.size(); ++i) {
        expr.traverseIf( [&](Expr& e) {
                if (e.type == Expr::EXPR_VAR && e.value == mod.consts[i].name) {
                    e = mod.consts[i].expr.sub[0];
                    changed = true;
                    return true;
                }
                return false;
            });
    }
    return changed;
}

void resolveNestedModuleRefs(Expr& expr, const Module& mod, const Field& member)
{
    // Resolve child constexpr aliases first because they can chain into each
    // other before reaching a template parameter: AAA -> BBB -> WIDTH.
    for (size_t pass = 0; pass < 8; ++pass) {
        if (!replaceModuleConstRefs(expr, mod)) {
            break;
        }
    }

    // Substitute child template parameters only once. The member actual can be
    // an expression in the parent module, such as WIDTH + 1. Repeating this
    // step would treat the parent WIDTH as the child WIDTH and expand forever.
    replaceModuleParameterRefs(expr, mod, member);
}

Expr portArrayExpr(const Field& port, const Module& mod, const Field& member)
{
    Expr array;
    if (port.array.size()) {
        array = port.array[0];
        resolveNestedModuleRefs(array, mod, member);
    }
    return array;
}

Expr portWireExpr(const Field& port, const Module& mod, const Field& member)
{
    Expr expr = port.expr;
    expr.flags = Expr::FLAG_WIRE;
    resolveNestedModuleRefs(expr, mod, member);
    return expr;
}

const Struct* findStructPackage(const std::string& name)
{
    auto it = std::find_if(currProject->structs.begin(), currProject->structs.end(), [&](const auto& st) {
        return st.name == name;
    });
    return it == currProject->structs.end() ? nullptr : &*it;
}

bool isSpecializationOf(const Struct& candidate, const Struct& primary)
{
    if (candidate.name == primary.name || primary.origName.empty()) {
        return false;
    }
    const std::string prefix = primary.origName + "<";
    if (candidate.origName.rfind(prefix, 0) == 0) {
        return true;
    }
    return candidate.origName == primary.origName
        && candidate.name.rfind(primary.name, 0) == 0
        && candidate.name.size() > primary.name.size();
}

bool hasImportedSpecialization(const std::string& name, const std::unordered_set<std::string>& availablePackages)
{
    const Struct* primary = findStructPackage(name);
    if (!primary || primary->origName.find('<') != std::string::npos) {
        return false;
    }

    return std::any_of(availablePackages.begin(), availablePackages.end(), [&](const std::string& package) {
        const Struct* candidate = findStructPackage(package);
        return candidate && isSpecializationOf(*candidate, *primary);
    });
}

std::unordered_set<std::string> ModuleAvailableImportPackages(const Module& mod)
{
    std::unordered_set<std::string> packages;
    for (const auto& imp : mod.imports) {
        std::string name = genTypeName(imp.name);
        packages.insert(name);
        if (const Struct* st = findStructPackage(name)) {
            for (const auto& subImport : collectStructPackageImports(*st)) {
                packages.insert(subImport);
            }
        }
    }
    return packages;
}

}

void Module::printImports(std::ofstream& out, std::unordered_set<std::string>* importsSet)
{
    std::unordered_set<std::string> importsRoot;
    if (!importsSet) {
        importsSet = &importsRoot;
    }
    const std::unordered_set<std::string> availablePackages = ModuleAvailableImportPackages(*this);
    for (auto& imp : imports) {
        for (auto& member : members) {
            auto it = std::find_if(currProject->modules.begin(), currProject->modules.end(), [&](auto& m){
//out << "importing " << (member.expr.type == Expr::EXPR_TEMPLATE?member.expr.sub[member.expr.sub.size()-1].str():member.expr.str()) << "..." << m.origName << "\n";
 return (member.expr.type == Expr::EXPR_TEMPLATE?member.expr.sub[member.expr.sub.size()-1].str():member.expr.str()) == m.origName; });

            if (it != currProject->modules.end() && it->replacement.empty()) {
                it->printImports(out, importsSet);
            }
        }
        std::string name = genTypeName(imp.name);
        const bool hasStructPackage = std::find_if(currProject->structs.begin(), currProject->structs.end(), [&](auto& s) {
            return s.name == name;
        }) != currProject->structs.end();
        const bool hasEnumPackage = std::find_if(currProject->enums.begin(), currProject->enums.end(), [&](auto& e) {
            return e.name == name;
        }) != currProject->enums.end();
        if (name == genTypeName(this->name) && !hasStructPackage && !hasEnumPackage) {
            continue;
        }
        if (hasImportedSpecialization(name, availablePackages)) {
            continue;
        }
        if (importsSet->find(name) == importsSet->end()) {
            importsSet->insert(name);
            out << "import " << name << "_pkg::*;\n";
        }
        auto st = std::find_if(currProject->structs.begin(), currProject->structs.end(), [&](auto& s) {
            return s.name == name;
        });
        if (st != currProject->structs.end()) {
            for (const auto& subImport : collectStructPackageImports(*st)) {
                if (hasImportedSpecialization(subImport, availablePackages)) {
                    continue;
                }
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
            out << (first?"    ":",   ") << "parameter " << p.name;
            if (p.initializer.type != Expr::EXPR_NONE) {
                out << " = " << p.initializer.str();
            } else if (p.expr.type == Expr::EXPR_NUM) {
                out << " = " << p.expr.str();
            }
            out << "\n";
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

    size_t maxMemberDims = 0;
    for (auto& member : members) {
        maxMemberDims = std::max(maxMemberDims, member.array.size());
    }
    if (maxMemberDims) {
        auto indices = memberArrayIndices(maxMemberDims);
        out << "    genvar ";
        for (size_t i = 0; i < indices.size(); ++i) {
            out << (i ? ", " : "") << indices[i];
        }
        out << ";\n";
    }

    // printWires for members
    for (auto& member : members) {
        member.indent = 1;
        if (member.array.size()) {  // array of members
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
                    Expr expr = portWireExpr(port, *mod, member);
                    Expr array = portArrayExpr(port, *mod, member);
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

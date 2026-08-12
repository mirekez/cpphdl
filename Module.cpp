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

Field portWireField(const Field& port, const Module& mod, const Field& member)
{
    Field wire{member.name + "__" + port.name, port.expr};
    wire.expr.flags = Expr::FLAG_WIRE;
    resolveNestedModuleRefs(wire.expr, mod, member);

    // Arrays of module instances are unpacked around the complete child port.
    wire.array = member.array;
    for (auto dim : port.array) {
        resolveNestedModuleRefs(dim, mod, member);
        wire.array.emplace_back(std::move(dim));
    }

    // Preserve the child's packed port dimensions without packing outer member dimensions.
    wire.packedArrayDims = port.packedArrayDims
        ? port.packedArrayDims
        : (port.packedArray ? port.array.size() : 0);
    wire.packedArray = wire.packedArrayDims != 0;
    return wire;
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

void ensureWorkMethod(std::vector<Method>& methods)
{
    if (std::any_of(methods.begin(), methods.end(), [](const Method& method) {
            return method.name == "_work";
        })) {
        return;
    }

    // Every generated module has a posedge block that calls _work(reset).
    // A C++ module can still be valid without an explicit _work() when only
    // comb logic is used, so synthesize the no-op task that the clock block
    // requires instead of depending on call-site discovery to create it.
    Method work;
    work.name = "_work";
    work.arguments.emplace_back(Field{"reset", Expr{"bool", Expr::EXPR_TYPE}});
    methods.emplace_back(std::move(work));
}

struct ClockProcess
{
    ClockDomain clock;
    std::string work;
    std::string strobe;
    std::string workNeg;
    std::string strobeNeg;
    std::string resetPos;
    std::string resetNeg;
    std::unordered_set<std::string> regs;
    std::unordered_set<std::string> negRegs;
};

const Method* findMethod(const Module& module, const std::string& name)
{
    auto it = std::find_if(module.methods.begin(), module.methods.end(), [&](const Method& method) {
        return method.name == name;
    });
    return it == module.methods.end() ? nullptr : &*it;
}

bool hasClockWorkSignature(const Method& method)
{
    return method.ret.empty()
        && method.arguments.size() == 1
        && method.arguments[0].expr.type == Expr::EXPR_TYPE
        && method.arguments[0].expr.value == "bool";
}

bool hasClockStrobeSignature(const Method& method)
{
    return method.ret.empty() && method.arguments.empty();
}

bool hasNamedClockProcess(const Module& module)
{
    return std::any_of(currProject->clocks.begin(), currProject->clocks.end(),
        [&](const ClockDomain& clock) {
            return findMethod(module, "_work_" + clock.name) != nullptr
                || findMethod(module, "_strobe_" + clock.name) != nullptr;
        });
}

void ensureNoopClockProcess(Module& module, const ClockDomain& clock)
{
    const std::string workName = "_work_" + clock.name;
    const std::string strobeName = "_strobe_" + clock.name;
    if (!findMethod(module, workName)) {
        Method work;
        work.name = workName;
        work.arguments.emplace_back(Field{"reset", Expr{"bool", Expr::EXPR_TYPE}});
        module.methods.emplace_back(std::move(work));
    }
    if (!findMethod(module, strobeName)) {
        Method strobe;
        strobe.name = strobeName;
        module.methods.emplace_back(std::move(strobe));
    }
}

bool exprContainsMember(const Expr& expr, const std::string& name)
{
    if (expr.type == Expr::EXPR_MEMBER && expr.value == name) {
        return true;
    }
    return std::any_of(expr.sub.begin(), expr.sub.end(), [&](const Expr& sub) {
        return exprContainsMember(sub, name);
    });
}

std::string expressionRootName(const Expr& expression)
{
    const Expr* expr = &expression;
    while (expr) {
        switch (expr->type) {
        case Expr::EXPR_INDEX:
        case Expr::EXPR_CAST:
        case Expr::EXPR_UNARY:
            expr = expr->sub.empty() ? nullptr : &expr->sub[0];
            break;
        case Expr::EXPR_MEMBER:
            if (expr->sub.empty() || expr->sub[0].type == Expr::EXPR_NONE) {
                return expr->value;
            }
            expr = &expr->sub[0];
            break;
        case Expr::EXPR_VAR:
            return expr->value;
        default:
            return "";
        }
    }
    return "";
}

void collectMethodRegisters(Module& module, const std::string& methodName,
    bool strobes, std::unordered_set<std::string>& registers,
    std::unordered_set<std::string>& visited)
{
    if (!visited.insert(methodName).second) {
        return;
    }

    for (const auto& method : module.methods) {
        if (method.name != methodName) {
            continue;
        }
        for (const auto& statement : method.statements) {
            Expr copy = statement;
            copy.traverseIf([&](Expr& expr) {
                if (strobes && expr.type == Expr::EXPR_MEMBERCALL && expr.value == "strobe"
                    && !expr.sub.empty()) {
                    const std::string root = expressionRootName(expr.sub[0]);
                    if (module.isReg(root)) {
                        registers.insert(root);
                    }
                }
                if (!strobes
                    && (expr.type == Expr::EXPR_BINARY || expr.type == Expr::EXPR_OPERATORCALL)
                    && expr.value == "=" && !expr.sub.empty()
                    && exprContainsMember(expr.sub[0], "_next")) {
                    const std::string root = expressionRootName(expr.sub[0]);
                    if (module.isReg(root)) {
                        registers.insert(root);
                    }
                }
                if (!strobes && expr.type == Expr::EXPR_MEMBERCALL
                    && (expr.value == "set" || expr.value == "clr") && !expr.sub.empty()) {
                    const std::string root = expressionRootName(expr.sub[0]);
                    if (module.isReg(root)) {
                        registers.insert(root);
                    }
                }

                if ((expr.type == Expr::EXPR_CALL || expr.type == Expr::EXPR_MEMBERCALL)
                    && findMethod(module, expr.value)) {
                    collectMethodRegisters(module, expr.value, strobes, registers, visited);
                }
                return false;
            });
        }
    }
}

std::unordered_set<std::string> methodRegisters(Module& module,
    const std::string& methodName, bool strobes)
{
    std::unordered_set<std::string> registers;
    std::unordered_set<std::string> visited;
    collectMethodRegisters(module, methodName, strobes, registers, visited);
    return registers;
}

bool validateClockProcesses(Module& module, std::vector<ClockProcess>& processes)
{
    std::unordered_map<std::string, std::string> owners;
    const bool namedClockProcess = hasNamedClockProcess(module);
    const auto& primaryClock = currProject->clocks.front();
    const bool explicitPrimary = findMethod(module, "_work_" + primaryClock.name) != nullptr
        || findMethod(module, "_strobe_" + primaryClock.name) != nullptr;
    // Existing modules keep _work/_strobe as their primary-domain process even
    // after adding named secondary clocks. An explicit named primary process
    // takes precedence when both APIs are present.
    const bool legacyPrimary = findMethod(module, "_work") != nullptr && !explicitPrimary;
    if (namedClockProcess && currProject->clocks.size() > 1) {
        const auto& primary = currProject->clocks.front();
        const bool hasPrimaryWork = legacyPrimary
            || findMethod(module, "_work_" + primary.name) != nullptr;
        const bool hasPrimaryStrobe = legacyPrimary
            || findMethod(module, "_strobe_" + primary.name) != nullptr;
        const bool hasCompleteSecondary = std::any_of(
            currProject->clocks.begin() + 1, currProject->clocks.end(),
            [&](const ClockDomain& clock) {
                return findMethod(module, "_work_" + clock.name) != nullptr
                    && findMethod(module, "_strobe_" + clock.name) != nullptr;
            });
        // A secondary-only module has no state in the primary domain. Permit
        // that local clock subset without weakening validation for modules
        // that define a primary process but forget a secondary process.
        if (!hasPrimaryWork && !hasPrimaryStrobe && hasCompleteSecondary) {
            ensureNoopClockProcess(module, primary);
        }
    }
    if (!namedClockProcess) {
        const size_t firstNoopClock = legacyPrimary ? 1 : 0;
        for (size_t i = firstNoopClock; i < currProject->clocks.size(); ++i) {
            ensureNoopClockProcess(module, currProject->clocks[i]);
        }
    }

    for (size_t clockIndex = 0; clockIndex < currProject->clocks.size(); ++clockIndex) {
        const auto& clock = currProject->clocks[clockIndex];
        ClockProcess process;
        process.clock = clock;
        const bool useLegacyPrimary = legacyPrimary && clockIndex == 0;
        process.work = useLegacyPrimary ? "_work" : "_work_" + clock.name;
        process.strobe = useLegacyPrimary ? "_strobe" : "_strobe_" + clock.name;
        process.workNeg = useLegacyPrimary ? "_work_neg" : "_work_neg_" + clock.name;
        process.strobeNeg = useLegacyPrimary ? "_strobe_neg" : "_strobe_neg_" + clock.name;
        process.resetPos = "_reset_pos_" + clock.name;
        process.resetNeg = "_reset_neg_" + clock.name;

        const Method* workMethod = findMethod(module, process.work);
        const Method* strobeMethod = findMethod(module, process.strobe);
        if (!workMethod || (!useLegacyPrimary && !strobeMethod)) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.work << "(bool) and " << process.strobe << "()\n";
            return false;
        }
        if (!useLegacyPrimary && !hasClockStrobeSignature(*strobeMethod)) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.strobe << " with signature void " << process.strobe
                      << "()\n";
            return false;
        }
        if (!hasClockWorkSignature(*workMethod)) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.work << " with signature void " << process.work
                      << "(bool reset)\n";
            return false;
        }

        const bool hasWorkNeg = findMethod(module, process.workNeg) != nullptr;
        const bool hasStrobeNeg = findMethod(module, process.strobeNeg) != nullptr;
        const Method* resetPosMethod = findMethod(module, process.resetPos);
        const Method* resetNegMethod = findMethod(module, process.resetNeg);
        if (!useLegacyPrimary && hasWorkNeg != hasStrobeNeg) {
            std::cerr << "ERROR: module '" << module.name << "' must define both "
                      << process.workNeg << "(bool) and " << process.strobeNeg
                      << "(), or neither\n";
            return false;
        }
        if (resetPosMethod && !hasClockStrobeSignature(*resetPosMethod)) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.resetPos << " with signature void " << process.resetPos
                      << "()\n";
            return false;
        }
        if (resetNegMethod && !hasClockStrobeSignature(*resetNegMethod)) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.resetNeg << " with signature void " << process.resetNeg
                      << "()\n";
            return false;
        }
        if (resetNegMethod && !hasWorkNeg) {
            std::cerr << "ERROR: module '" << module.name << "' defines "
                      << process.resetNeg << "() without the negative-edge process "
                      << process.workNeg << "(bool) and " << process.strobeNeg << "()\n";
            return false;
        }
        if (hasWorkNeg && !hasClockWorkSignature(*findMethod(module, process.workNeg))) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.workNeg << " with signature void " << process.workNeg
                      << "(bool reset)\n";
            return false;
        }
        if (!useLegacyPrimary && hasStrobeNeg && !hasClockStrobeSignature(*findMethod(module, process.strobeNeg))) {
            std::cerr << "ERROR: module '" << module.name << "' must define "
                      << process.strobeNeg << " with signature void " << process.strobeNeg
                      << "()\n";
            return false;
        }

        if (useLegacyPrimary) {
            if (namedClockProcess) {
                // In a multi-clock module, _strobe is the ownership declaration
                // for only the legacy primary-domain registers.
                process.regs = methodRegisters(module, "_strobe", true);
            }
            else {
                for (const auto& field : module.vars) {
                    if (module.isReg(field.name)) {
                        process.regs.insert(field.name);
                    }
                }
            }
        }
        else {
            process.regs = methodRegisters(module, process.strobe, true);
        }
        const auto written = methodRegisters(module, process.work, false);
        for (const auto& reg : written) {
            if (process.regs.find(reg) == process.regs.end()) {
                std::cerr << "ERROR: register '" << reg << "' is written by "
                          << module.name << "::" << process.work << " but is not strobed by "
                          << process.strobe << "\n";
                return false;
            }
        }

        if (hasWorkNeg) {
            process.negRegs = methodRegisters(module, process.strobeNeg, true);
            const auto negWritten = methodRegisters(module, process.workNeg, false);
            for (const auto& reg : negWritten) {
                if (process.negRegs.find(reg) == process.negRegs.end()) {
                    std::cerr << "ERROR: register '" << reg << "' is written by "
                              << module.name << "::" << process.workNeg << " but is not strobed by "
                              << process.strobeNeg << "\n";
                    return false;
                }
            }
        }

        auto validateResetRegisters = [&](const Method* resetMethod,
                const std::string& resetName,
                const std::unordered_set<std::string>& ownedRegisters,
                const std::string& owner) {
            if (!resetMethod) {
                return true;
            }
            const auto resetRegisters = methodRegisters(module, resetName, false);
            for (const auto& reg : resetRegisters) {
                if (ownedRegisters.find(reg) == ownedRegisters.end()) {
                    std::cerr << "ERROR: register '" << reg << "' is written by "
                              << module.name << "::" << resetName
                              << " but is not owned by " << owner << "\n";
                    return false;
                }
            }
            return true;
        };
        if (!validateResetRegisters(resetPosMethod, process.resetPos, process.regs,
                clock.name + " posedge")
            || !validateResetRegisters(resetNegMethod, process.resetNeg, process.negRegs,
                clock.name + " negedge")) {
            return false;
        }

        auto claim = [&](const std::unordered_set<std::string>& registers, const std::string& edge) {
            for (const auto& reg : registers) {
                const std::string owner = clock.name + " " + edge;
                auto [it, inserted] = owners.emplace(reg, owner);
                if (!inserted) {
                    std::cerr << "ERROR: register '" << reg << "' in module '" << module.name
                              << "' is strobed by both " << it->second << " and " << owner << "\n";
                    return false;
                }
            }
            return true;
        };
        if (!claim(process.regs, "posedge") || !claim(process.negRegs, "negedge")) {
            return false;
        }
        processes.emplace_back(std::move(process));
    }
    return true;
}

bool isRegisterField(const Field& field)
{
    Expr expr = field.expr;
    return expr.traverseIf([](auto& sub) {
        return sub.type == Expr::EXPR_TEMPLATE && sub.value == "cpphdl_reg";
    });
}

void printClockBlock(std::ofstream& out, Module& module, const std::string& edge,
    const std::string& clock, const std::string& work,
    const std::unordered_set<std::string>& registers, bool alwaysFf,
    const std::string& asyncReset = {})
{
    out << "\n";
    out << "    " << (alwaysFf ? "always_ff" : "always") << " @("
        << edge << " " << clock;
    if (!asyncReset.empty()) {
        out << " or posedge reset";
    }
    out << ") begin\n";
    for (auto& field : module.vars) {
        if (registers.find(field.name) != registers.end()
            && module.onceAccessedRegs.find(field.name) == module.onceAccessedRegs.end()
            && isRegisterField(field)) {
            out << "        " << field.name << "_tmp = " << field.name << ";\n";
        }
    }
    out << "\n";
    if (!asyncReset.empty()) {
        out << "        if (reset) begin\n";
        out << "            " << asyncReset << "();\n";
        out << "        end\n";
        out << "        else begin\n";
        out << "            " << work << "(reset);\n";
        out << "        end\n";
    }
    else {
        out << "        " << work << "(reset);\n";
    }
    out << "\n";
    for (auto& field : module.vars) {
        if (registers.find(field.name) != registers.end()
            && module.onceAccessedRegs.find(field.name) == module.onceAccessedRegs.end()
            && isRegisterField(field)) {
            out << "        " << field.name << " <= " << field.name << "_tmp;\n";
        }
    }
    out << "    end\n";
}

std::vector<std::string> generatedClockNames()
{
    if (currProject->clocks.empty()) {
        return {"clk"};
    }
    std::vector<std::string> names;
    for (const auto& clock : currProject->clocks) {
        names.push_back(clock.name);
    }
    return names;
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
    const bool multipleClocks = currProject->clocks.size() >= 2;
    std::vector<ClockProcess> clockProcesses;
    if (multipleClocks && !validateClockProcesses(*this, clockProcesses)) {
        return false;
    }

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
    const auto clockNames = generatedClockNames();
    out << "    input wire " << clockNames.front() << "\n";
    for (size_t i = 1; i < clockNames.size(); ++i) {
        out << ",   input wire " << clockNames[i] << "\n";
    }
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
    if (!printMembers(out)) {
        return false;
    }

    if (!multipleClocks) {
        ensureWorkMethod(methods);
    }

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
        if (multipleClocks && std::any_of(clockProcesses.begin(), clockProcesses.end(), [&](const auto& process) {
                return method.name == process.strobe || method.name == process.strobeNeg;
            })) {
            continue;
        }
        out << "\n";
        if (!method.print(out)) {
            return false;
        }
        if (method.name == "_work_neg") {
            hasWorkNeg = true;
        }
    }

    if (multipleClocks) {
        for (const auto& process : clockProcesses) {
            printClockBlock(out, *this, "posedge", process.clock.name,
                process.work, process.regs, true,
                findMethod(*this, process.resetPos) ? process.resetPos : std::string{});
            if (findMethod(*this, process.workNeg)) {
                printClockBlock(out, *this, "negedge", process.clock.name,
                    process.workNeg, process.negRegs, true,
                    findMethod(*this, process.resetNeg) ? process.resetNeg : std::string{});
            }
        }
    }
    else {
        std::unordered_set<std::string> allRegisters;
        for (const auto& field : vars) {
            if (isRegisterField(field)) {
                allRegisters.insert(field.name);
            }
        }
        printClockBlock(out, *this, "posedge", clockNames.front(), "_work",
            allRegisters, false);
        if (hasWorkNeg) {
            out << "\n";
            out << "    always @(negedge " << clockNames.front() << ") begin\n";
            out << "        _work_neg(reset);\n";
            out << "    end\n";
        }
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
                    Field wire = portWireField(port, *mod, member);
                    wire.indent = 1;
                    if (!wire.print(out)) {
                        return false;
                    }
                    wires.emplace_back(std::move(wire));
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
            const auto clockNames = generatedClockNames();
            out << std::string(8 + member.array.size() * 4, ' ') << "."
                << clockNames.front() << "(" << clockNames.front() << ")\n";
            for (size_t i = 1; i < clockNames.size(); ++i) {
                out << std::string(4 + member.array.size() * 4, ' ') << ",           ."
                    << clockNames[i] << "(" << clockNames[i] << ")\n";
            }
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
                    Field wire = portWireField(port, *mod, member);
//                    out << wire.expr.debug() << "\n";
//                    out << port.expr.debug() << "\n";
                    wire.indent = 1;
                    if (!wire.print(out)) {
                        return false;
                    }
                    wires.emplace_back(std::move(wire));
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
            const auto clockNames = generatedClockNames();
            out << "        ." << clockNames.front() << "(" << clockNames.front() << ")\n";
            for (size_t i = 1; i < clockNames.size(); ++i) {
                out << ",       ." << clockNames[i] << "(" << clockNames[i] << ")\n";
            }
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

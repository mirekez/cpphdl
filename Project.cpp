#include "Project.h"
#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Struct.h"
#include "Enum.h"

#include <cctype>
#include <cstring>
#include <unordered_set>

using namespace cpphdl;

Project prj;
Project* currProject = &prj;

namespace
{

const Struct* findStructByName(const std::string& name)
{
    for (const auto& st : currProject->structs) {
        if (st.name == name) {
            return &st;
        }
    }
    return nullptr;
}

void collectStructImports(const Struct& st, std::vector<std::string>& imports, std::unordered_set<std::string>& seen);

void addStructImport(const std::string& name, std::vector<std::string>& imports, std::unordered_set<std::string>& seen)
{
    if (!seen.insert(name).second) {
        return;
    }

    imports.push_back(name);
    if (const Struct* imported = findStructByName(name)) {
        collectStructImports(*imported, imports, seen);
    }
}

void collectExprStructImports(const Expr& expr, const Struct& owner, std::vector<std::string>& imports, std::unordered_set<std::string>& seen)
{
    if (expr.type == Expr::EXPR_TYPE && expr.value != owner.name && findStructByName(expr.value)) {
        addStructImport(expr.value, imports, seen);
    }
    for (const auto& sub : expr.sub) {
        collectExprStructImports(sub, owner, imports, seen);
    }
}

void collectStructImports(const Struct& st, std::vector<std::string>& imports, std::unordered_set<std::string>& seen)
{
    for (const auto& imp : st.imports) {
        addStructImport(imp.name, imports, seen);
    }

    for (const auto& field : st.fields) {
        collectExprStructImports(field.expr, st, imports, seen);
        if (field.definition.type != Struct::STRUCT_EMPTY) {
            collectStructImports(field.definition, imports, seen);
        }
    }
}

bool isSvIdentChar(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$';
}

std::string normalizeReplacementModuleName(std::string text, const std::string& moduleName)
{
    if (moduleName.empty()) {
        return text;
    }

    for (size_t pos = 0; (pos = text.find("module", pos)) != std::string::npos; ++pos) {
        const bool leftOk = pos == 0 || !isSvIdentChar(text[pos - 1]);
        const size_t afterModule = pos + strlen("module");
        const bool rightOk = afterModule >= text.size() || !isSvIdentChar(text[afterModule]);
        if (!leftOk || !rightOk) {
            continue;
        }

        size_t nameStart = text.find_first_not_of(" \t\r\n", afterModule);
        if (nameStart == std::string::npos || nameStart >= text.size()) {
            return text;
        }
        if (!(std::isalpha(static_cast<unsigned char>(text[nameStart])) || text[nameStart] == '_' || text[nameStart] == '$')) {
            continue;
        }

        size_t nameEnd = nameStart + 1;
        while (nameEnd < text.size() && isSvIdentChar(text[nameEnd])) {
            ++nameEnd;
        }
        const std::string found = text.substr(nameStart, nameEnd - nameStart);
        if (found != moduleName) {
            text.replace(nameStart, nameEnd - nameStart, moduleName);
        }
        return text;
    }

    return text;
}

}

std::vector<std::string> cpphdl::collectStructPackageImports(const Struct& st)
{
    std::vector<std::string> imports;
    std::unordered_set<std::string> seen;
    collectStructImports(st, imports, seen);
    return imports;
}

void Project::generate(const std::string& outDir)
{
    namespace fs = std::filesystem;

    fs::create_directories(outDir);
    std::set<std::string> modules_uniq;
    for (auto& mod : modules) {
        if (modules_uniq.find(mod.name) != modules_uniq.end()) {
            continue;
        }
        modules_uniq.emplace(mod.name);

        fs::path filePath = fs::path(outDir) / (mod.name + ".sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
            continue;
        }

        if (!mod.replacement.empty()) {
            std::string replacement = normalizeReplacementModuleName(mod.replacement, mod.name);
            out << replacement;
            if (replacement.back() != '\n') {
                out << "\n";
            }
        } else {
            mod.print(out);
        }

        std::cout << "Generated: " << filePath << " (" << mod.name << "/" << mod.origName << ")" << "\n";
    }

    std::set<std::string> structures_uniq;
    for (auto& str : structs) {  // export structs but not structures
        if (structures_uniq.find(str.name) != structures_uniq.end()) {
            continue;
        }
        structures_uniq.emplace(str.name);
        if (std::find_if(modules.begin(), modules.end(), [&](auto& m){ return str.origName == m.origName; }) != modules.end()) {
            continue;
        }

        std::string fname = str.name;
        fs::path filePath = fs::path(outDir) / (fname + "_pkg.sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
            continue;
        }
        out << "package " << fname << "_pkg;\n";

        for (const auto& imp : collectStructPackageImports(str)) {
            out << "import " << imp << "_pkg::*;\n";
        }

        out << "\n";

        for (auto& param : str.parameters) {
            param.expr.flags = Expr::FLAG_SPECVAL;
            out << "parameter " << param.name << " = " << param.expr.str() << ";\n";
        }
        out << "typedef ";
        str.print(out);
        out << "\n\nendpackage\n";

        std::cout << "Generated: " << filePath << "\n";
    }

    std::set<std::string> enums_uniq;
    for (auto& en : enums) {
        if (enums_uniq.find(en.name) != enums_uniq.end()) {
            continue;
        }
        enums_uniq.emplace(en.name);
        std::string fname = en.name;
        fs::path filePath = fs::path(outDir) / (fname + "_pkg.sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
            continue;
        }
        out << "package " << fname << "_pkg;\n\n";
//        out << "typedef ";
        en.print(out);
        out << "\n\nendpackage\n";

        std::cout << "Generated: " << filePath << "\n";
    }

    // predef
    {
        fs::path filePath = fs::path(outDir) / "Predef_pkg.sv";
        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
        }
        else {
            out << "package Predef_pkg;\n";
            out << "endpackage\n";
        }
    }
}

Module* Project::findModule(const std::string& name)
{
    for (auto& module : modules) {
        if (module.name == name) {
            return &module;
        }
    }
    return nullptr;
}

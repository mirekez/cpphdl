#include "Project.h"
#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Struct.h"
#include "Enum.h"

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

void collectStructImports(const Struct& st, std::vector<std::string>& imports, std::unordered_set<std::string>& seen)
{
    auto addImport = [&](const std::string& name) {
        if (!seen.insert(name).second) {
            return;
        }

        imports.push_back(name);
        if (const Struct* imported = findStructByName(name)) {
            collectStructImports(*imported, imports, seen);
        }
    };

    for (const auto& imp : st.imports) {
        addImport(imp.name);
    }

    for (const auto& field : st.fields) {
        if (field.definition.type != Struct::STRUCT_EMPTY) {
            collectStructImports(field.definition, imports, seen);
        }
    }
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
            out << mod.replacement;
            if (mod.replacement.back() != '\n') {
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

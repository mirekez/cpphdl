#include "Project.h"
#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"
#include "Struct.h"
#include "Enum.h"

using namespace cpphdl;

Project prj;
Project* currProject = &prj;

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

        mod.print(out);

        std::cout << "Generated: " << filePath << " (" << mod.name << "/" << mod.origName << ")" << "\n";
    }

    std::set<std::string> structures_uniq;
    for (auto& str : structs) {  // export structs but not structures
        if (structures_uniq.find(str.name) != structures_uniq.end()) {
            continue;
        }
        structures_uniq.emplace(str.name);
        if (std::find_if(modules.begin(), modules.end(), [&](auto& m){ return str.name.find(m.name) == 0; }) != modules.end()) {
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

        for (auto& imp : str.imports) {
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

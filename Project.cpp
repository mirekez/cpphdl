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
    for (auto& mod : modules) {
        fs::path filePath = fs::path(outDir) / (mod.name + ".sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
            continue;
        }

        mod.print(out);

        std::cout << "\n" << "Generated: " << filePath << " (" << mod.name << "/" << mod.origName << ")" << "\n";
    }

    for (auto& str : structs) {
        std::string fname = str.name;
        fs::path filePath = fs::path(outDir) / (fname + "_pkg.sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open '" << filePath << "' for writing\n";
            continue;
        }
        out << "package " << fname << "_pkg;\n\n";
        for (auto& param : str.parameters) {
            param.expr.flags = Expr::FLAG_SPECVAL;
            out << "parameter " << param.name << " = " << param.expr.str() << ";\n";
        }
        out << "typedef ";
        str.print(out);
        out << "\n\nendpackage\n";

        std::cout << "\n" << "Generated: " << filePath << "\n";
    }

    for (auto& en : enums) {
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

        std::cout << "\n" << "Generated: " << filePath << "\n";
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

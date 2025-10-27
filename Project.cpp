#include "Project.h"
#include "Module.h"

using namespace cpphdl;

void Project::generate(const std::string& outDir)
{
    namespace fs = std::filesystem;

    fs::create_directories(outDir);
    for (auto& mod : modules) {
        fs::path filePath = fs::path(outDir) / (mod.name + ".sv");

        std::ofstream out(filePath);
        if (!out) {
            std::cerr << "Failed to open " << filePath << " for writing\n";
            continue;
        }

        mod.print(out);

        std::cout << "Generated: " << filePath << "\n";
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

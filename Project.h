#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "Module.h"

namespace cpphdl
{

struct Project
{
    std::vector<Module> modules;

    void generate(const std::string& outDir)
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

};


}

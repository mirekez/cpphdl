#pragma once

#include <vector>
#include <unordered_map>

#include "Module.h"

namespace cpphdl
{

struct Project
{
    std::unordered_map<std::string,Module> modules;

    void generate(const std::string& outDir)
    {
        namespace fs = std::filesystem;

        fs::create_directories(outDir);
        for (auto& [mod_name, mod] : modules) {
            fs::path filePath = fs::path(outDir) / (mod.name + ".sv");

            std::ofstream out(filePath);
            if (!out) {
                std::cerr << "Failed to open " << filePath << " for writing\n";
                continue;
            }

            out << "module " << mod.name << " (\n";
            bool first = true;
            for (auto& [port_name, port] : mod.ports) {
                out << (first?" ":",")  << "   " << port.type.str() << " " << port.name << "\n";
                first = false;
            }
            out << ");";
            out << "\n";
            out << "endmodule\n";

            std::cout << "Generated: " << filePath << "\n";
        }
    }

};


}

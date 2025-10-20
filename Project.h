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
                out << (first?" ":",")  << "   " << port.type.str(Expr::FLAG_PORT, port.name.find("_out") == (size_t)-1 ? "input " : "output ") << " " << port.name << "\n";
                first = false;
            }
            out << ");";
            out << "\n";
            for (auto& [field_name, field] : mod.fields) {
                if (field.type.value == "cpphdl::memory") {
                    ASSERT(field.type.sub.size() == 2);
                    out << "    " << field.type.sub[0].str(Expr::FLAG_REG) << " " << field.name << std::string("[") << field.type.sub[1].value << "]" << ";\n";
                } else
                if (field.type.value == "cpphdl::array") {
                    ASSERT(field.type.sub.size() == 2);
                    out << "    " << field.type.sub[0].str(Expr::FLAG_REG, "", std::string("[") + field.type.sub[1].value + "]") << " " << field.name << ";\n";
                } else
                if (field.type.value == "cpphdl::reg") {
                    ASSERT(field.type.sub.size() == 1);
                    out << "    " << field.type.sub[0].str(Expr::FLAG_REG) << " " << field.name << ";\n";
                }
                else {
                    out << "    " << field.type.str() << " " << field.name << ";\n";
                }
            }


            out << "\n";
            out << "endmodule\n";

            std::cout << "Generated: " << filePath << "\n";
        }
    }

};


}

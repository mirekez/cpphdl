#pragma once

#include "Expr.h"

namespace cpphdl
{

struct Field
{
    std::string name;
    Expr type;

    // methods

    bool print(std::ofstream& out)
    {
        if (type.value == "cpphdl::memory") {
            ASSERT1(type.sub.size() == 3, std::string("memory subs size = ") + std::to_string(type.sub.size()) );
            out << "    " << type.sub[0].str(Expr::FLAG_REG, "", std::string("[") + type.sub[1].value + "-1:0]") << " " << name << std::string("[") << type.sub[2].value << "]" << ";\n";
        } else
        if (type.value == "cpphdl::array") {
            ASSERT1(type.sub.size() == 2, std::string("array subs size = ") + std::to_string(type.sub.size()) );
            out << "    " << type.sub[0].str(Expr::FLAG_REG, "", std::string("[") + type.sub[1].value + "]") << " " << name << ";\n";
        } else
        if (type.value == "cpphdl::reg") {
            ASSERT1(type.sub.size() == 1, std::string("reg subs size = ") + std::to_string(type.sub.size()) );
            out << "    " << type.sub[0].str(Expr::FLAG_REG) << " " << name << ";\n";
        }
        else {
            out << "    " << type.str() << " " << name << ";\n";
        }
        return true;
    }
};


}

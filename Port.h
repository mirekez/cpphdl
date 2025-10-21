#pragma once

#include "Expr.h"

namespace cpphdl
{

struct Port
{
    std::string name;
    Expr type;

    // methods

    bool print(std::ofstream& out)
    {
        out << type.str(Expr::FLAG_PORT, name.find("_out") == (size_t)-1 ? "input " : "output ") << " " << name << "\n";
        return true;
    }

};


}

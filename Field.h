#pragma once

#include "Expr.h"

namespace cpphdl
{

struct Field
{
    std::string name;
    Expr type;

    // methods

    bool print(std::ofstream& out);
    bool printPort(std::ofstream& out);
};


}

extern cpphdl::Field* currField;

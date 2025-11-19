#pragma once

#include "Expr.h"
#include "Struct.h"

namespace cpphdl
{

struct Field
{
    std::string name;
    Expr expr;
    Expr initializer;
    Expr bitwidth;
    Struct definition;

    // methods
    int indent = 0;

    bool print(std::ofstream& out, bool forcePacked = false);
    bool printPort(std::ofstream& out);
};


}

extern cpphdl::Field* currField;

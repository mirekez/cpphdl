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
    std::vector<Expr> array;
    Expr bitwidth;
    Struct definition;
    bool packedArray = false;
    size_t packedArrayDims = 0;

    // methods
    int indent = 0;

    bool print(std::ofstream& out, std::string nameSuffix = "", bool inStruct = false);
    bool printPort(std::ofstream& out);
};


}

extern cpphdl::Field* currField;

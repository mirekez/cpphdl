#pragma once

#include <vector>
#include <string>

namespace cpphdl
{

struct Field;
struct Expr;

struct Method
{
    std::string name;
    std::vector<Expr> ret;
    std::vector<Field> parameters;
    std::vector<Expr> statements;
    std::vector<Field> temps;

    // methods

    bool print(std::ofstream& out);
    bool printConns(std::ofstream& out);
    bool printComb(std::ofstream& out);
};


}

extern cpphdl::Method* currMethod;

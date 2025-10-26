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
    std::string ret;
    std::vector<Field> parameters;
    std::vector<Expr> statements;

    // methods

    bool print(std::ofstream& out);
    bool printConns(std::ofstream& out);

};


}

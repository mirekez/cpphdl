#pragma once

#include <string>
#include <vector>

namespace cpphdl
{

struct Field;
struct Method;
struct Comb;

struct Module
{
    std::string name;
    std::vector<Field> parameters;
    std::vector<Field> ports;
    std::vector<Field> vars;
    std::vector<Field> members;
    std::vector<Method> methods;
    std::vector<Comb> combs;

    // methods

    bool print(std::ofstream& out);
    bool printWires(std::ofstream& out);
};

}

extern cpphdl::Module* currModule;

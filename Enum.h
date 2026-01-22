#pragma once

#include <string>
#include <vector>

namespace cpphdl
{

struct Field;

struct Enum
{
    std::string name;
    std::string origName;

    std::vector<Field> fields;

    // methods
    int indent = 0;

    bool print(std::ofstream& out);
};

}

extern cpphdl::Enum* currEnum;

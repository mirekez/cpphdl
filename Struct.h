#pragma once

#include <string>
#include <vector>

namespace cpphdl
{

struct Field;

struct Struct
{
    std::string name;
    enum {
        STRUCT_EMPTY,
        STRUCT_STRUCT,
        STRUCT_UNION,
//        STRUCT_ENUM,
    } type = STRUCT_EMPTY;

//    std::vector<Field> parameters;
    std::vector<Field> fields;
    std::string origName;

    // methods
    int indent = 0;

    bool print(std::ofstream& out);
};

}

extern cpphdl::Struct* currStruct;

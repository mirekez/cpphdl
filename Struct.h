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

    std::vector<Field> fields;
    std::string origName;
    std::vector<Field> parameters;  // constexpr inside struct

    int indent = 0;
    int alignNo = 0;  // count alignment insertions

    bool print(std::ofstream* out, std::vector<Field>* params = nullptr, size_t* declSize = nullptr);
};

}

extern cpphdl::Struct* currStruct;

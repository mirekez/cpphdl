#pragma once

#include <string>
#include <string.h>
#include <vector>
#include <set>

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
    std::set<std::string> imports;
    std::string origName;

    // methods

    bool print(std::ofstream& out);
    bool printMembers(std::ofstream& out);
};

}

inline bool str_ending(const std::string& str, const char* ending)
{
    return str.rfind(ending) == str.length()-strlen(ending) && str.length() >= strlen(ending);
}

extern cpphdl::Module* currModule;

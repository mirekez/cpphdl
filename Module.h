#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "Port.h"
#include "Register.h"
#include "Comb.h"

namespace cpphdl
{

struct Module
{
    std::string name;
    std::vector<Port> ports;
    std::vector<Register> regs;
    std::vector<Comb> combs;

    std::unordered_map<std::string,Module> submodules;



};


}

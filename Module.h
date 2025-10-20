#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Port.h"
#include "Field.h"
#include "Comb.h"

namespace cpphdl
{

struct Module
{
    std::string name;
    std::vector<std::string> params;
    std::unordered_map<std::string,Port> ports;
    std::unordered_map<std::string,Field> fields;
    std::vector<Comb> combs;

};


}

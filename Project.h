#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

namespace cpphdl
{

struct Module;
struct Struct;

struct Project
{
    std::vector<Module> modules;
    std::vector<Struct> structs;

    void generate(const std::string& outDir);

    Module* findModule(const std::string& name);
};


}

extern cpphdl::Project* currProject;

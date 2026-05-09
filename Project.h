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
struct Enum;

struct Project
{
    std::vector<Module> modules;
    std::vector<Struct> structs;
    std::vector<Enum> enums;

    void generate(const std::string& outDir);

    Module* findModule(const std::string& name);
};

std::vector<std::string> collectStructPackageImports(const Struct& st);

}

extern cpphdl::Project* currProject;

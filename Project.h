#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <set>
#include <vector>
#include <string>

namespace cpphdl
{

struct Module;
struct Struct;
struct Enum;

struct ClockDomain
{
    std::string name;
    uint64_t frequency = 0;
};

struct Project
{
    std::vector<Module> modules;
    std::vector<Struct> structs;
    std::vector<Enum> enums;
    // Module classes normally emit only .sv modules. Add a package name here
    // only when another module references one of their static constexprs.
    std::set<std::string> modulePackages;
    std::vector<ClockDomain> clocks;

    bool generate(const std::string& outDir);

    Module* findModule(const std::string& name);
};

std::vector<std::string> collectStructPackageImports(const Struct& st);

}

extern cpphdl::Project* currProject;

#pragma once

#include <string>

namespace cpphdl
{

struct Project;

bool writeJsonOutput(const Project& project, const std::string& filename);

}

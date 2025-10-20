#pragma once

#include "Expr.h"

namespace cpphdl
{

struct Port
{
    std::string name;
    Expr type;
};


}

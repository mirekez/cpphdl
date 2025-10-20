#pragma once

#include "Expr.h"

namespace cpphdl
{

struct Field
{
    std::string name;
    Expr type;
    bool reg;
};


}

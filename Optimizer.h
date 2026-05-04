#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace cpphdl
{

struct Expr;
struct Field;
struct Method;
struct Comb;
struct Import;

struct Optimizer
{
    struct VarStat
    {
        size_t accessed = 0;
    };

    void collectAccesses(Expr& expr, std::unordered_map<std::string, VarStat>& vars);
    void replaceAssignments(Expr& expr, const std::unordered_map<std::string, VarStat>& vars);
    void optimizeBlocking(std::vector<Method>& methods);
};


}

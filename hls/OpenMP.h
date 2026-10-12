#pragma once
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/Expr.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <optional>

namespace cpphdl::hls {
inline constexpr unsigned OpenMPMaxLanes = 4096;
using StaticIntegers = std::map<const clang::VarDecl*, int64_t>;
using OpenMPCallResolver = std::function<clang::FunctionDecl*(const clang::CallExpr*)>;
std::optional<int64_t> staticInteger(clang::ASTContext&, const clang::Expr*, const StaticIntegers& = {});
struct OpenMPLoop {
    const clang::VarDecl* variable;
    std::vector<int64_t> values;
};
struct OpenMPReduction {
    const clang::VarDecl* variable;
    std::string operation, identity;
};
struct OpenMPPlan {
    std::vector<OpenMPLoop> loops;
    std::vector<const clang::VarDecl*> privates, firstprivates;
    std::vector<OpenMPReduction> reductions;
    const clang::Stmt* body = nullptr;
    unsigned lanes = 1;
};
// Validate a bounded, race-free hardware subset before allocating any lanes.
OpenMPLoop planStaticLoop(clang::ASTContext&, const clang::ForStmt*);
OpenMPPlan planOpenMP(clang::ASTContext&, const clang::OMPExecutableDirective*, const OpenMPCallResolver& = {}, const StaticIntegers& = {});
}

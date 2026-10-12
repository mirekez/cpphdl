#pragma once
#include "clang/AST/StmtOpenMP.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cpphdl::hls {
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
OpenMPPlan planOpenMP(clang::ASTContext&, const clang::OMPExecutableDirective*);
}

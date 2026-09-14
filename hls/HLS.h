#pragma once

#include <string>

namespace clang { class ASTContext; class Sema; class CXXMethodDecl; }
namespace cpphdl { struct Module; struct Project; }

// Optional frontend/IR hooks. No HLS policy belongs in the RTL runtime headers.
namespace cpphdl::hls {
void enable();
bool prepare(clang::ASTContext& context, clang::Sema& sema);
bool writeAnalysis(const std::string& directory);
bool enterMethod(Module& module, const std::string& name,
                 const clang::CXXMethodDecl& declaration);
void leaveMethod(Module& module, const std::string& name);
bool lower(Project& project);
}

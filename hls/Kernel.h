#pragma once
#include <string>
namespace clang::tooling { class ClangTool; }
namespace cpphdl::hls {
int compileKernel(clang::tooling::ClangTool& tool, const std::string& entry,
                  const std::string& directory);
}

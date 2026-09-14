#pragma once

#include <string>
namespace clang { class ASTContext; class Sema; }

namespace cpphdl::hls {
bool inspectStdContainers(clang::ASTContext& context, clang::Sema& sema);
bool writeStdContainerAnalysis(const std::string& directory);
}

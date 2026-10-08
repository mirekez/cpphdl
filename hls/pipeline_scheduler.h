#pragma once
#include "AstClocked.h"
namespace cpphdl::hls {
unsigned pipelineStages(const clang::CXXRecordDecl*);
std::string generatePipeline(clang::ASTContext&, clang::Sema&, const clang::CXXRecordDecl*, const std::string&);
std::map<std::string, graph::Value> exportPipelineGraph(clang::ASTContext&, clang::Sema&,
    const clang::CXXRecordDecl*, graph::Graph&, const std::string&);
}

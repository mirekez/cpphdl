#pragma once
#include "AstClocked.h"
#include "../synth/ScheduledGraph.h"
namespace cpphdl::hls {
std::string generateDelayed(clang::ASTContext&, clang::Sema&, const clang::CXXRecordDecl*, const std::string&);
std::map<std::string, graph::Value> exportDelayedGraph(clang::ASTContext&, clang::Sema&,
    const clang::CXXRecordDecl*, graph::Graph&, const std::string&);
// Reuse AST elaboration, before selecting a clock scheduling policy.
synth::ScheduledDesign lowerPipelineSource(clang::ASTContext&, clang::Sema&, const clang::CXXRecordDecl*);
}

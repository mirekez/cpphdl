#pragma once
#include "AstClocked.h"
#include "../synth/ScheduledGraph.h"
namespace clang { class CXXMethodDecl; class FunctionDecl; }
namespace cpphdl::hls {
std::string generateDelayed(clang::ASTContext&, clang::Sema&, const clang::CXXRecordDecl*, const std::string&);
std::map<std::string, graph::Value> exportDelayedGraph(clang::ASTContext&, clang::Sema&,
    const clang::CXXRecordDecl*, graph::Graph&, const std::string&);
// Reuse AST elaboration, before selecting a clock scheduling policy.
synth::ScheduledDesign lowerPipelineSource(clang::ASTContext&, clang::Sema&, const clang::CXXRecordDecl*);
synth::ScheduledDesign lowerValueFunctionSource(clang::ASTContext&, clang::Sema&, clang::FunctionDecl*);
synth::ScheduledDesign lowerAtomicSource(clang::ASTContext&, clang::Sema&, clang::CXXMethodDecl*);
}

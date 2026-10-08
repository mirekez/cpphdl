#pragma once
#include <string>
#include "../include/cpphdl_graph.h"
namespace clang { class ASTContext; class Sema; class CXXRecordDecl; }
namespace cpphdl { struct Module; }
namespace cpphdl::hls {
void prepareClocked(clang::ASTContext&, clang::Sema&);
bool isClocked(const clang::CXXRecordDecl*);
std::string clockedName(const clang::CXXRecordDecl*, const std::string&);
bool exportClocked(const clang::CXXRecordDecl*, cpphdl::Module&);
bool clockedSucceeded();
std::map<std::string, graph::Value> exportClockedGraph(clang::ASTContext&, clang::Sema&,
    const clang::CXXRecordDecl*, graph::Graph&, const std::string& scope);
}

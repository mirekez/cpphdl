#pragma once
#include <string>
namespace clang { class ASTContext; class Sema; class CXXRecordDecl; }
namespace cpphdl { struct Module; }
namespace cpphdl::hls {
void prepareClocked(clang::ASTContext&, clang::Sema&);
bool isClocked(const clang::CXXRecordDecl*);
std::string clockedName(const clang::CXXRecordDecl*, const std::string&);
bool exportClocked(const clang::CXXRecordDecl*, cpphdl::Module&);
bool clockedSucceeded();
}

#pragma once
#include <map>
#include <string>
#include <vector>
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"

namespace cpphdl::hls {
class FunctionOverrides {
    struct Entry { clang::FunctionDecl* function; bool rawBits; };
    clang::ASTContext& context;
    std::map<std::string, std::vector<Entry>> entries;
    void collect(clang::DeclContext*);
    clang::FunctionDecl* find(const std::string&, clang::QualType,
                             const std::vector<clang::QualType>&);
public:
    explicit FunctionOverrides(clang::ASTContext&);
    bool hasFloatingHooks() const;
    clang::FunctionDecl* find(clang::FunctionDecl*, const std::vector<clang::QualType>& actualTypes);
    clang::FunctionDecl* operation(const std::string&, clang::QualType,
                                 const std::vector<clang::QualType>&);
};
}

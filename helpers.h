#include "clang/AST/AST.h"

#include "Module.h"
#include "Struct.h"
#include "Expr.h"

using namespace clang;

struct Helpers
{
    Helpers(ASTContext* context, cpphdl::Module* module) : Ctx(*context), mod(*module) {}

    ASTContext& Ctx;
    cpphdl::Module& mod;

    enum {
        FLAG_NONE = 0,
        FLAG_EXTERNAL_THIS = 1,  // method from other struct (not Module)
        FLAG_ABSTRACT = 2  // we're in abstract declaration of template module
    };
    unsigned flags = 0;

    cpphdl::Expr exprToExpr(const Stmt* E);
    bool templateToExpr(QualType QT, cpphdl::Expr& expr);
    cpphdl::Expr digQT(QualType& QT);
    cpphdl::Expr ArgToExpr(const TemplateArgument& Arg, std::string name = "");
    void genSpecializationTypeName(bool first, std::string& name, cpphdl::Expr& param, bool onlyTypes = false);
    bool skipStdFunctionType(QualType& QT);
    CXXRecordDecl* resolveCXXRecordDecl(QualType& QT);
    CXXRecordDecl* lookupQualifiedRecord(llvm::StringRef QualifiedName);
};

extern unsigned debugIndent;

#include <functional>

class on_return {
public:
    on_return(std::function<void()> lambda) : lambda_(std::move(lambda)) {}
    ~on_return() { lambda_(); }

private:
    std::function<void()> lambda_;
};

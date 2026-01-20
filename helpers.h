#include "clang/AST/AST.h"

#include "Module.h"
#include "Struct.h"
#include "Expr.h"

using namespace clang;

struct Helpers
{
    Helpers(ASTContext* context, cpphdl::Module* module) : ctx(*context), mod(*module) {}

    ASTContext& ctx;
    cpphdl::Module& mod;

    enum {
        FLAG_NONE = 0,
        FLAG_EXTERNAL_THIS = 1,  // method from other struct (not Module)
        FLAG_ABSTRACT = 2  // we're in abstract declaration of template module
    };
    unsigned flags = 0;

    cpphdl::Expr exprToExpr(const Stmt* E);
    void ArgToExpr(const TemplateArgument& Arg, cpphdl::Expr& expr, bool specialization = true);
    bool templateToExpr(QualType QT, cpphdl::Expr& expr);
    cpphdl::Expr digQT(QualType& QT);
    void genSpecializationTypeName(bool first, std::string& name, cpphdl::Expr& param, bool onlyTypes = false);
    void followSpecialization(const CXXRecordDecl* RD, std::string& name, std::vector<cpphdl::Field>* params = nullptr, bool onlyTypes = false);
    bool skipStdFunctionType(QualType& QT);
    CXXRecordDecl* resolveCXXRecordDecl(QualType QT);
    CXXRecordDecl* lookupQualifiedRecord(llvm::StringRef QualifiedName);
    bool checkCaseBreaks(const Stmt *S);
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

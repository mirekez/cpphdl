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
        FLAG_EXTERNAL_THIS = 1
    };
    unsigned flags;

    cpphdl::Expr exprToExpr(const Stmt* E);
    bool templateToExpr(QualType QT, cpphdl::Expr& expr);
    cpphdl::Expr digQT(QualType& QT);
    void addSpecializationName(std::string& name, std::vector<cpphdl::Field>& params, bool onlyTypes = true);
    bool specializationToParameters(CXXRecordDecl*RD, std::vector<cpphdl::Field>& params);

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

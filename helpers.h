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
    bool thisIsModule = false;

    cpphdl::Expr exprToExpr(const Stmt* E);
    bool templateToExpr(QualType QT, cpphdl::Expr& expr);
    cpphdl::Struct exportStruct(CXXRecordDecl* RD);
    cpphdl::Expr digQT(QualType& QT);
    void addSpecializationName(std::string& name, std::vector<cpphdl::Field>& params, bool onlyTypes = true);
    bool specializationToParameters(CXXRecordDecl*RD, std::vector<cpphdl::Field>& params);
    void updateExpr(cpphdl::Expr& expr1, cpphdl::Expr& expr2);
    bool putField(FieldDecl* FD);

};

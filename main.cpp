#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Tooling/ArgumentsAdjusters.h"

#include "Debug.h"
#include "Project.h"
#include "main.h"

cpphdl::Project prj;

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext *Context)
        : Context(Context), SM(Context->getSourceManager()) {}

    bool putMethod(const CXXMethodDecl *MD, cpphdl::Module& mod)
    {
//        if (!MD->hasBody()) {
//            return true;
//        }

        struct LocalVisitor : RecursiveASTVisitor<LocalVisitor>
        {
            ASTContext& Ctx;
            cpphdl::Method& method;
            LocalVisitor(ASTContext &Ctx, cpphdl::Method& method) : Ctx(Ctx), method(method) {}

            bool VisitBinaryOperator(BinaryOperator *BO)
            {
                method.statements.emplace_back(exprToExpr(BO, Ctx));
                return false;
            }

            bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *OCE)
            {
                method.statements.emplace_back(exprToExpr(OCE, Ctx));
                return false;
            }

            bool VisitForStmt(clang::ForStmt *FS)
            {
                DEBUG_AST(std::cout << " ForStmt " << "\n");

                cpphdl::Expr expr = cpphdl::Expr{"for", cpphdl::Expr::EXPR_FOR};

                if (auto *E = llvm::dyn_cast<clang::Expr>(FS->getInit())) {
                    expr.sub.push_back(exprToExpr(E, Ctx));
                }
                if (auto *E = llvm::dyn_cast<clang::Expr>(FS->getCond())) {
                    expr.sub.push_back(exprToExpr(E, Ctx));
                }
                if (auto *E = llvm::dyn_cast<clang::Expr>(FS->getInc())) {
                    expr.sub.push_back(exprToExpr(E, Ctx));
                }

                cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
                if (auto *CS = dyn_cast_or_null<CompoundStmt>(FS->getBody())) {
                    for (auto *S : CS->body()) {
                        if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(S)) {
                            expr1.sub.push_back(exprToExpr(E, Ctx));
                        }
                    }
                    expr.sub.emplace_back(std::move(expr1));
                } else {
                    if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(FS->getBody())) {
                        expr1.sub.push_back(exprToExpr(E, Ctx));
                        expr.sub.emplace_back(std::move(expr1));
                    }
                }

                method.statements.emplace_back(expr);
                return false;
            }

            bool VisitWhileStmt(clang::WhileStmt *WS)
            {
                DEBUG_AST(std::cout << " WhileStmt " << "\n");

                cpphdl::Expr expr = cpphdl::Expr{"while", cpphdl::Expr::EXPR_WHILE};

                if (auto *E = llvm::dyn_cast<clang::Expr>(WS->getCond())) {
                    expr.sub.push_back(exprToExpr(E, Ctx));
                }

                cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
                if (auto *CS = dyn_cast_or_null<CompoundStmt>(WS->getBody())) {
                    for (auto *S : CS->body()) {
                        if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(S)) {
                            expr1.sub.push_back(exprToExpr(E, Ctx));
                        }
                    }
                    expr.sub.emplace_back(std::move(expr1));
                } else {
                    if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(WS->getBody())) {
                        expr1.sub.push_back(exprToExpr(E, Ctx));
                        expr.sub.emplace_back(std::move(expr1));
                    }
                }

                method.statements.emplace_back(expr);
                return false;
            }

            bool VisitIfStmt(clang::IfStmt *IS)
            {
                DEBUG_AST(std::cout << " IfStmt " << "\n");

                cpphdl::Expr expr = cpphdl::Expr{"if", cpphdl::Expr::EXPR_IF};

                if (auto *E = llvm::dyn_cast<clang::Expr>(IS->getCond())) {
                    expr.sub.push_back(exprToExpr(E, Ctx));
                }

                cpphdl::Expr expr1 = cpphdl::Expr{"then", cpphdl::Expr::EXPR_BODY};
                if (auto *CS = dyn_cast_or_null<CompoundStmt>(IS->getThen())) {
                    for (auto *S : CS->body()) {
                        if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(S)) {
                            expr1.sub.push_back(exprToExpr(E, Ctx));
                        }
                    }
                    expr.sub.emplace_back(std::move(expr1));
                } else {
                    if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(IS->getThen())) {
                        expr1.sub.push_back(exprToExpr(E, Ctx));
                        expr.sub.emplace_back(std::move(expr1));
                    }
                }

                cpphdl::Expr expr2 = cpphdl::Expr{"else", cpphdl::Expr::EXPR_BODY};
                if (auto *CS = dyn_cast_or_null<CompoundStmt>(IS->getElse())) {
                    for (auto *S : CS->body()) {
                        if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(S)) {
                            expr2.sub.push_back(exprToExpr(E, Ctx));
                        }
                    }
                    expr.sub.emplace_back(std::move(expr2));
                } else {
                    if (auto *E = llvm::dyn_cast_or_null<clang::Expr>(IS->getElse())) {
                        expr2.sub.push_back(exprToExpr(E, Ctx));
                        expr.sub.emplace_back(std::move(expr2));
                    }
                }

                method.statements.emplace_back(expr);
                return false;
            }

//            bool shouldVisitImplicitCode() const { return false; }
        };

        cpphdl::Method method = cpphdl::Method{MD->getNameAsString(), MD->getReturnType().getAsString()};
        for (const clang::ParmVarDecl *Param : MD->parameters()) {
            method.parameters.emplace_back(cpphdl::Field{Param->getNameAsString(),cpphdl::Expr{Param->getType().getAsString(),cpphdl::Expr::EXPR_TYPE}});
        }

        LocalVisitor(*Context, method).TraverseStmt(const_cast<Stmt*>(MD->getBody()));
        if (MD->getNameAsString() != mod.name && MD->getNameAsString() != std::string("~") + mod.name && MD->getNameAsString().find("operator") != 0) {
            mod.methods.emplace_back(std::move(method));
        }
        return true;
    }

    bool putModule(CXXRecordDecl *RD)
    {
        DEBUG_AST(std::cout << "\nClass: " << RD->getQualifiedNameAsString());
        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        if (auto *SD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            DEBUG_AST(std::cout << " (template), parameters: ");

            const TemplateArgumentList& Args = SD->getTemplateArgs();
            const TemplateParameterList *Params = SD->getSpecializedTemplate()->getTemplateParameters();
            for (unsigned i = 0; i < Args.size(); ++i) {
                const TemplateArgument& Arg = Args[i];
                switch (Arg.getKind()) {
                    case TemplateArgument::Type:
                    {
                        QualType QT = Arg.getAsType();
                        std::string str;
                        llvm::raw_string_ostream OS(str);
                        QT.print(OS, Context->getPrintingPolicy());
                        OS.flush();
                        DEBUG_AST(std::cout << " type: " << OS.str());
                        break;
                    }
                    case TemplateArgument::Integral:
                    {
                        std::string str;
                        llvm::raw_string_ostream OS(str);
                        Arg.getAsIntegral().print(OS, true);
                        OS.flush();
                        DEBUG_AST(std::cout << " integral: " << OS.str());

                        mod.parameters.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(),cpphdl::Expr{OS.str(),cpphdl::Expr::EXPR_VALUE}});
                        break;
                    }
                    case TemplateArgument::Declaration:
                        DEBUG_AST(std::cout << " declaration: " << Arg.getAsDecl()->getNameAsString());
                        break;
                    case TemplateArgument::Template:
                    {
                        TemplateName TN = Arg.getAsTemplate();
                        std::string str;
                        llvm::raw_string_ostream OS(str);
                        TN.print(OS, Context->getPrintingPolicy());
                        OS.flush();
                        DEBUG_AST(std::cout << " template: " << OS.str());
                        break;
                    }
                    case TemplateArgument::Expression:
                    {
                        const Expr *E = Arg.getAsExpr();
                        SourceManager &SM = Context->getSourceManager();
                        LangOptions LO = Context->getLangOpts();
                        SourceRange Range = E->getSourceRange();
                        if (!Range.isInvalid()) {
                            llvm::StringRef SR = Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, LO);
                            DEBUG_AST(std::cout << " expression: " << SR.str());
                        }
                        break;
                    }
                    case TemplateArgument::Pack:
                    default:
                        DEBUG_AST(std::cout << " unhandled");
                    break;
                }
            }
            DEBUG_AST(std::cout << "\n");
        }

        for (Decl *D : RD->decls()) {
            if (auto *FD = dyn_cast<FieldDecl>(D)) {
                DEBUG_AST(std::cout << "  Member: ");

                bool pointer = false;
                QualType QT = FD->getType().getNonReferenceType();
                if (const auto *PT = QT->getAs<PointerType>()) {
                    QT = PT->getPointeeType();
                    pointer = true;
                }
                QT = QT.getCanonicalType();
                std::string str;
                llvm::raw_string_ostream OS(str);
                QT.print(OS, Context->getPrintingPolicy());
                OS.flush();

                cpphdl::Expr expr;
                const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(QT->getAsCXXRecordDecl());
                if (SD) {
                    DEBUG_AST(std::cout << " [template] ");
                    str = SD->getQualifiedNameAsString();
                    expr.value = str;
                    expr.type = cpphdl::Expr::EXPR_TEMPLATE;
                    ASSERT(SD->getTemplateArgs().asArray().size()>0);
                    std::vector<std::string> params;
                    getParametersFromInstantiation(FD, "", *Context, params);
                    templateToExpr(SD, &params, expr, *Context);
                }
                else {
                    DEBUG_AST(std::cout << " [type] ");
                    expr.value = str;
                    expr.type = cpphdl::Expr::EXPR_TYPE;
                }

                if (pointer) {
                    DEBUG_AST(std::cout << " (port) " << str << ", " << FD->getNameAsString() << "\n");
                    mod.ports.emplace_back(cpphdl::Port{FD->getNameAsString(), std::move(expr)});
                }
                else {
                    DEBUG_AST(std::cout << " (var) " << str << ", " << FD->getNameAsString() << "\n");
                    mod.fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
                }
            } else
            if (auto *VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST(std::cout << " (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(llvm::outs(), RD->getASTContext().getPrintingPolicy());
//                    llvm::outs() << "\n";
                }
            } else
            if (auto *Nested = dyn_cast<CXXRecordDecl>(D)) {
                llvm::outs() << "  Nested class: " << Nested->getNameAsString() << "\n";
            } else
            if (auto *EnumD = dyn_cast<EnumDecl>(D)) {
                llvm::outs() << "  Enum: " << EnumD->getNameAsString() << "\n";
            } else
            if (auto *TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                llvm::outs() << "  Type alias: " << TypeAlias->getNameAsString() << "\n";
            } else
            if (auto *MD = llvm::dyn_cast<clang::CXXMethodDecl>(D)) {
                llvm::outs() << "  Method: " << MD->getQualifiedNameAsString() << "\n";
                if (!putMethod(MD, mod)) {
                    return false;
                }
            }
        }

        prj.modules.emplace_back(std::move(mod));
        return true;
    }

    CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName)
    {
        SmallVector<StringRef, 4> Parts;
        QualifiedName.split(Parts, "::");

        DeclContext* DC = Ctx->getTranslationUnitDecl();

        for (unsigned i = 0; i < Parts.size(); ++i) {
            IdentifierInfo& Id = Ctx->Idents.get(Parts[i]);
            auto strs = DC->lookup(&Id);

            if (strs.empty()) {
                return nullptr;
            }

            NamedDecl* ND = strs.front();

            if (i < Parts.size()-1) {
                if (auto* NS = dyn_cast<NamespaceDecl>(ND)) {
                    DC = NS;
                }
                else {
                    return nullptr;
                }
            } else {
                if (auto* CXX = dyn_cast<CXXRecordDecl>(ND)) {
                    return CXX;
                }
                return nullptr;
            }
        }
        return nullptr;
    }


//    CXXRecordDecl *currentClass = nullptr;

    bool VisitCXXRecordDecl(CXXRecordDecl *RD)
    {
        auto *ModuleClass = lookupQualifiedRecord(Context, "cpphdl::Module");
        if (!ModuleClass) {
            return true;
        }

        if (!RD || !RD->hasDefinition() || !RD->isDerivedFrom(ModuleClass)) {
            return true;
        }

        if (RD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            return true;
        }

//        if (currentClass != RD) {
            if (!putModule(RD)) {
                return false;
            }
//        }

        return true;
    }

    bool shouldVisitTemplateInstantiations() const { return true; }

    ASTContext *Context;
    const SourceManager &SM;
};

struct MethodConsumer : public ASTConsumer
{
    explicit MethodConsumer(ASTContext *Context) : Visitor(Context) {}

    void HandleTranslationUnit(ASTContext &Context) override
    {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

    MethodVisitor Visitor;
};

static llvm::cl::OptionCategory MyToolCategory("cpphdl options");

struct MyFrontendAction : public ASTFrontendAction
{
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override
    {
        return std::make_unique<MethodConsumer>(&CI.getASTContext());
    }
};

int main(int argc, const char **argv)
{
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser& Options = ExpectedParser.get();

    ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

    Tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
        {"-nostdinc", //"-x", "c++",
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/include/c++/v1").str(),
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/lib/clang/21/include").str(),
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/x86_64-conda-linux-gnu/sysroot/usr/include").str(),
         "-std=c++26"},
        clang::tooling::ArgumentInsertPosition::BEGIN));

    int ret = Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
    prj.generate("generated");
    return ret;
}

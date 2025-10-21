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

cpphdl::Project prj;

using namespace clang;
using namespace clang::tooling;

cpphdl::Expr exprToExpr(const Expr *E, ASTContext& Ctx);

bool templateToExpr(const TemplateArgumentList& Args, cpphdl::Expr& expr, ASTContext& Ctx)
{
    DEBUG_AST(std::cout << " templateToExpr: ");
    for (unsigned i = 0; i < Args.size(); ++i) {
        const TemplateArgument& Arg = Args[i];

        cpphdl::Expr expr1;

        switch (Arg.getKind()) {
            case TemplateArgument::Type:  // sub template is a type
            {
                std::string str;
                llvm::raw_string_ostream OS(str);
                Arg.getAsType().print(OS, Ctx.getPrintingPolicy());
                OS.flush();
                DEBUG_AST(std::cout << " type " << str);
                const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Arg.getAsType()->getAsCXXRecordDecl());
                if (SD) {
                    str = SD->getQualifiedNameAsString();
                    expr1.value = str;
                    expr1.type = cpphdl::Expr::EXPR_TEMPLATE;
                    templateToExpr(SD->getTemplateArgs(), expr1, Ctx);
                    expr.sub.emplace_back(std::move(expr1));
                }
                else {
                    expr1.value = str;
                    expr1.type = cpphdl::Expr::EXPR_TYPE;
                    expr.sub.emplace_back(std::move(expr1));
                }
                break;
            }
            case TemplateArgument::Template:
            {
                const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Arg.getAsType()->getAsCXXRecordDecl());
                ASSERT(SD);
                expr1.value = SD->getQualifiedNameAsString();
                expr1.type = cpphdl::Expr::EXPR_TEMPLATE;
                templateToExpr(SD->getTemplateArgs(), expr1, Ctx);
                DEBUG_AST(std::cout << " template  " << expr1.value);
                expr.sub.emplace_back(std::move(expr1));
                break;
            }
            case TemplateArgument::Integral:
            {
                std::string str;
                llvm::raw_string_ostream OS(str);
                Arg.getAsIntegral().print(OS, true);
                OS.flush();
                expr1.value = str;
                DEBUG_AST(std::cout << " integral " << str);
                expr1.type = cpphdl::Expr::EXPR_VALUE;
                expr.sub.emplace_back(std::move(expr1));
                break;
            }
            case TemplateArgument::Declaration:
            {
                std::string str;
                llvm::raw_string_ostream OS(str);
                Arg.print(Ctx.getPrintingPolicy(), OS, true);
                expr1.value = str;
                DEBUG_AST(std::cout << " decl " << str);
                break;
            }
            case TemplateArgument::Expression:
            {
                DEBUG_AST(std::cout << " expression ");
                expr.sub.emplace_back(exprToExpr(Arg.getAsExpr(), Ctx));
                break;
            }
            case TemplateArgument::Pack:
            default:
            {
                std::string str;
                llvm::raw_string_ostream OS(str);
                Arg.print(Ctx.getPrintingPolicy(), OS, true);
                expr1.value = str;
                DEBUG_AST(std::cout << " unhandled");
                break;
            }
        }
    }
    return true;
}

cpphdl::Expr exprToExpr(const Expr *E, ASTContext& Ctx)
{
    DEBUG_AST(std::cout << " exprToExpr ");
    E = E->IgnoreParenImpCasts();

    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST(std::cout << " binary " << BO->getOpcodeStr().data());
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS(),Ctx),exprToExpr(BO->getRHS(),Ctx)}};
    }
    else if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST(std::cout << " decl " << DRE->getNameInfo().getAsString());
        return cpphdl::Expr{DRE->getNameInfo().getAsString(), cpphdl::Expr::EXPR_DECLARE};
    }
    else if (auto *IL = dyn_cast<IntegerLiteral>(E)) {
        DEBUG_AST(std::cout << " value " << std::to_string(IL->getValue().getSExtValue()));
        return cpphdl::Expr{std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_VALUE};
    }
    else if (auto *CE = dyn_cast<CallExpr>(E)) {
        if (const FunctionDecl *FD = CE->getDirectCallee()) {
            DEBUG_AST(std::cout << " expr " << FD->getNameAsString());
            cpphdl::Expr call = cpphdl::Expr{FD->getNameAsString(), cpphdl::Expr::EXPR_CALL};
            for (auto *arg : CE->arguments()) {
                call.sub.push_back(exprToExpr(arg, Ctx));
            }
            return call;
        }
    }
    else if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
        DEBUG_AST(std::cout << " binary operator");

        if (OCE->getNumArgs() == 2) {
            return cpphdl::Expr{"=", cpphdl::Expr::EXPR_BINARY, {exprToExpr(OCE->getArg(0),Ctx),exprToExpr(OCE->getArg(1),Ctx)}};
        } else if (OCE->getNumArgs() == 1) {
            return cpphdl::Expr{"=", cpphdl::Expr::EXPR_BINARY, {exprToExpr(OCE->getArg(0), Ctx)}};
        }
    }
    else {
        SourceManager &SM = Ctx.getSourceManager();
        LangOptions LangOpts = Ctx.getLangOpts();

        SourceLocation StartLoc = E->getBeginLoc();
        SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);

        CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);

        DEBUG_AST(std::cout << " unknown " << std::string(Lexer::getSourceText(Range, SM, LangOpts)));

        return cpphdl::Expr{std::string(Lexer::getSourceText(Range, SM, LangOpts)), cpphdl::Expr::EXPR_UNKNOWN};
    }
    return cpphdl::Expr{"", cpphdl::Expr::EXPR_UNKNOWN};
}

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
//                llvm::outs() << "Assignment:";
//                BO->getExprLoc().print(llvm::outs(), Ctx.getSourceManager());
                clang::Expr *RHS = BO->getRHS();
                if (RHS) {
                    llvm::outs() << "        BO: "
                     << Lexer::getSourceText(
                          CharSourceRange::getTokenRange(RHS->getSourceRange()),
                          Ctx.getSourceManager(),
                          Ctx.getLangOpts());
                }
                llvm::outs() << "\n";

                method.statements.emplace_back(exprToExpr(BO, Ctx));
                return false;
            }

            bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *OCE)
            {
//                llvm::outs() << "Assignment:";
//                OCE->getExprLoc().print(llvm::outs(), Ctx.getSourceManager());
                clang::OverloadedOperatorKind OpKind = OCE->getOperator();
                llvm::outs() << "        OC: "
                    << Lexer::getSourceText(
                        CharSourceRange::getTokenRange(OCE->getSourceRange()),
                        Ctx.getSourceManager(),
                        Ctx.getLangOpts());
                llvm::outs() << ", found operator: " << clang::getOperatorSpelling(OpKind) << "\n";
                llvm::outs() << "\n";

                method.statements.emplace_back(exprToExpr(OCE, Ctx));
                return false;
            }

            bool VisitForStmt(clang::ForStmt *FS)
            {
                clang::SourceManager &SM = Ctx.getSourceManager();
                clang::SourceLocation Loc = FS->getForLoc();

                llvm::outs() << "        FS: "
                             << Loc.printToString(SM);

                // Optional: inspect parts
                if (clang::Stmt *Init = FS->getInit()) {
                    llvm::outs() << "  Has init statement";
                }
                if (clang::Expr *Cond = FS->getCond()) {
                    llvm::outs() << "  Has condition: ";
                    Cond->dumpColor();
                }
                if (clang::Expr *Inc = FS->getInc()) {
                    llvm::outs() << "  Has increment: ";
                    Inc->dumpColor();
                }
                llvm::outs() << "\n";

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

        if (auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            DEBUG_AST(std::cout << " (template), parameters: ");

            const TemplateArgumentList& Args = Spec->getTemplateArgs();
            const TemplateParameterList *Params = Spec->getSpecializedTemplate()->getTemplateParameters();
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
                QualType BT = FD->getType().getNonReferenceType();
                if (const auto *PT = BT->getAs<PointerType>()) {
                    BT = PT->getPointeeType();
                    pointer = true;
                }
                BT = BT.getCanonicalType();
                std::string str;
                llvm::raw_string_ostream OS(str);
                BT.print(OS, Context->getPrintingPolicy());
                OS.flush();

                cpphdl::Expr expr;
                const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(BT->getAsCXXRecordDecl());
                if (SD) {
                    str = SD->getQualifiedNameAsString();
                    expr.value = str;
                    expr.type = cpphdl::Expr::EXPR_TEMPLATE;
                    templateToExpr(SD->getTemplateArgs(), expr, *Context);
                    ASSERT(SD->getTemplateArgs().asArray().size()>0);
                }
                else {
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

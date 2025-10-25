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

            bool TraverseBinaryOperator(BinaryOperator *E)
            {
                VisitBinaryOperator(E);
                return true;
            }

            bool VisitBinaryOperator(BinaryOperator *BO)
            {
                method.statements.emplace_back(exprToExpr(BO, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr *E)
            {
                VisitCXXOperatorCallExpr(E);
                return true;
            }

            bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *OCE)
            {
                method.statements.emplace_back(exprToExpr(OCE, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseCXXMemberCallExpr(CXXMemberCallExpr *E)
            {
                VisitCXXMemberCallExpr(E);
                return true;
            }

            bool VisitCXXMemberCallExpr(CXXMemberCallExpr *MCE)
            {
                method.statements.emplace_back(exprToExpr(MCE, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseMemberExpr(MemberExpr *E)
            {
                VisitMemberExpr(E);
                return true;
            }

            bool VisitMemberExpr(MemberExpr *ME)
            {
                method.statements.emplace_back(exprToExpr(ME, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseForStmt(ForStmt *E)
            {
                VisitForStmt(E);
                return true;
            }

            bool VisitForStmt(ForStmt *FS)
            {
                method.statements.emplace_back(exprToExpr(FS, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseWhileStmt(WhileStmt *E)
            {
                VisitWhileStmt(E);
                return true;
            }

            bool VisitWhileStmt(WhileStmt *WS)
            {
                method.statements.emplace_back(exprToExpr(WS, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool TraverseIfStmt(IfStmt *E)
            {
                VisitIfStmt(E);
                return true;
            }

            bool VisitIfStmt(IfStmt *IS)
            {
                method.statements.emplace_back(exprToExpr(IS, Ctx));
                DEBUG_AST(std::cout << "\n");
                return true;
            }

            bool shouldVisitTemplateInstantiations() const { return true; }
        };

        cpphdl::Method method = cpphdl::Method{MD->getNameAsString(), MD->getReturnType().getAsString()};
        for (const ParmVarDecl *Param : MD->parameters()) {
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
            if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(D)) {
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
    int argc1 = 0;
    char args1[8192];
    char* argv1[256];
    char* ptr = args1;
    bool wasSplitter = false;
    for (int i=0; i < argc; ++i) {
        if (i > 0 && !wasSplitter
            && strstr(argv[i], ".cpp") != argv[i] + strlen(argv[i]) - 4
            && strstr(argv[i], ".cc") != argv[i] + strlen(argv[i]) - 3
            && strstr(argv[i], ".h") != argv[i] + strlen(argv[i]) - 2
            && strstr(argv[i], ".hpp") != argv[i] + strlen(argv[i]) - 4
            && strcmp(argv[i], "--") != 0) {
            memcpy(ptr, "--", 2+1);
            argv1[argc1++] = ptr;
            ptr += 2+1;
        }

        if (strcmp(argv[i], "--") == 0) {
            wasSplitter = true;
        }

        memcpy(ptr, argv[i], strlen(argv[i])+1);
        argv1[argc1++] = ptr;
        ptr += strlen(argv[i])+1;
    }

    auto ExpectedParser = tooling::CommonOptionsParser::create(argc1, (const char**)argv1, MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    tooling::CommonOptionsParser& Options = ExpectedParser.get();

    tooling::ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

    Tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster(
        {"-nostdinc",
         "-x",
         "c++",
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/include/c++/v1").str(),
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/lib/clang/21/include").str(),
         "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/x86_64-conda-linux-gnu/sysroot/usr/include").str(),
         "-std=c++26",
         "-DSYNTHESIS"},
        tooling::ArgumentInsertPosition::BEGIN));

    int ret = Tool.run(tooling::newFrontendActionFactory<MyFrontendAction>().get());
    prj.generate("generated");
    return ret;
}

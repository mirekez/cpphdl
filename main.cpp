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


struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext *Context)
        : Context(Context), SM(Context->getSourceManager()) {}

    bool templateToExpr(cpphdl::Expr& expr, const TemplateArgumentList& Args)
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
                    Arg.getAsType().print(OS, Context->getPrintingPolicy());
                    OS.flush();
                    DEBUG_AST(std::cout << " type " << str);
                    const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Arg.getAsType()->getAsCXXRecordDecl());
                    if (SD) {
                        str = SD->getQualifiedNameAsString();
                        expr1.value = str;
                        expr1.type = cpphdl::Expr::EXPR_TEMPLATE;
                        templateToExpr(expr1, SD->getTemplateArgs());
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
                    templateToExpr(expr1, SD->getTemplateArgs());
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
                    Arg.print(Context->getPrintingPolicy(), OS, true);
                    expr1.value = str;
                    DEBUG_AST(std::cout << " decl " << str);
                    break;
                }
                case TemplateArgument::Expression:
                {
                    DEBUG_AST(std::cout << " expression ");
                    expr.sub.emplace_back(exprToExpr(Arg.getAsExpr()));
                    break;
                }
                default:
                {
                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    Arg.print(Context->getPrintingPolicy(), OS, true);
                    expr1.value = str;
                    DEBUG_AST(std::cout << " unhandled");
                    break;
                }
            }
        }
        return true;
    }

    cpphdl::Expr exprToExpr(const Expr *E)
    {
        DEBUG_AST(std::cout << " exprToExpr ");
        E = E->IgnoreParenImpCasts();

        if (auto *BO = dyn_cast<BinaryOperator>(E)) {
            DEBUG_AST(std::cout << " binary " << BO->getOpcodeStr().data());
            cpphdl::Expr LHS = exprToExpr(BO->getLHS());
            cpphdl::Expr RHS = exprToExpr(BO->getRHS());
            auto ret = cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {LHS,RHS}};
            return ret;
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
                    call.sub.push_back(exprToExpr(arg));
                }
                return call;
            }
        }
        else {
            SourceManager &SM = Context->getSourceManager();
            LangOptions LangOpts = Context->getLangOpts();

            SourceLocation StartLoc = E->getBeginLoc();
            SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);

            CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);

            DEBUG_AST(std::cout << " unknown " << std::string(Lexer::getSourceText(Range, SM, LangOpts)));

            return cpphdl::Expr{std::string(Lexer::getSourceText(Range, SM, LangOpts)), cpphdl::Expr::EXPR_UNKNOWN};
        }
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_UNKNOWN};
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

    bool newModule(CXXRecordDecl *RD)
    {
        DEBUG_AST(std::cout << "\nClass: " << RD->getQualifiedNameAsString());
        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        if (auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            DEBUG_AST(std::cout << " (template), parameters: ");

            const TemplateArgumentList& Args = Spec->getTemplateArgs();
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

                        mod.params.push_back(OS.str());
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
                    templateToExpr(expr, SD->getTemplateArgs());
                    ASSERT(SD->getTemplateArgs().asArray().size()>0);
                }
                else {
                    expr.value = str;
                    expr.type = cpphdl::Expr::EXPR_TYPE;
                }

                if (pointer) {
                    DEBUG_AST(std::cout << " (port) " << str << ", " << FD->getNameAsString() << "\n");
                    mod.ports.emplace(FD->getNameAsString(), cpphdl::Port{FD->getNameAsString(), std::move(expr)});
                }
                else {
                    DEBUG_AST(std::cout << " (var) " << str << ", " << FD->getNameAsString() << "\n");
                    mod.fields.emplace(FD->getNameAsString(), cpphdl::Field{FD->getNameAsString(), std::move(expr)});
                }
            } else if (auto *VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST(std::cout << " (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(llvm::outs(), RD->getASTContext().getPrintingPolicy());
//                    llvm::outs() << "\n";
                }
            } else if (auto *Nested = dyn_cast<CXXRecordDecl>(D)) {
                llvm::outs() << "  Nested class: " << Nested->getNameAsString() << "\n";
            } else if (auto *EnumD = dyn_cast<EnumDecl>(D)) {
                llvm::outs() << "  Enum: " << EnumD->getNameAsString() << "\n";
            } else if (auto *TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                llvm::outs() << "  Type alias: " << TypeAlias->getNameAsString() << "\n";
            }
        }

        prj.modules.emplace(mod.name, std::move(mod));
        return true;
    }

    CXXRecordDecl *currentClass = nullptr;

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

        if (currentClass != RD) {
            if (!newModule(RD)) {
                return false;
            }
        }

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

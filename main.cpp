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

CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName)
{
    SmallVector<StringRef, 4> Parts;
    QualifiedName.split(Parts, "::");

    DeclContext* DC = Ctx->getTranslationUnitDecl();

    for (unsigned i = 0; i < Parts.size(); ++i) {
        IdentifierInfo& Id = Ctx->Idents.get(Parts[i]);
        auto Results = DC->lookup(&Id);

        if (Results.empty()) {
            return nullptr;
        }

        NamedDecl* ND = Results.front();

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

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext *Context)
        : Context(Context), SM(Context->getSourceManager()) {}

    void newModule(CXXRecordDecl *RD)
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
                    std::string Result;
                    llvm::raw_string_ostream OS(Result);
                    QT.print(OS, Context->getPrintingPolicy());
                    OS.flush(); // ensures the string is finalized
                    DEBUG_AST(std::cout << " type: " << OS.str());
                    break;
                }
                case TemplateArgument::Integral:
                {
                    const llvm::APSInt &Val = Arg.getAsIntegral();
                    std::string Result;
                    llvm::raw_string_ostream OS(Result);
                    Val.print(OS, /*isSigned*/ true); // true = respect signedness
                    OS.flush();
                    DEBUG_AST(std::cout << " integral: " << OS.str());
                    break;
                }
                case TemplateArgument::Declaration:
                    DEBUG_AST(std::cout << " declaration: " << Arg.getAsDecl()->getNameAsString());
                    break;
                case TemplateArgument::Template:
                {
                    TemplateName TN = Arg.getAsTemplate();
                    std::string Result;
                    llvm::raw_string_ostream OS(Result);
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
                }
            }
            prj.modules[mod.name] = mod;
            DEBUG_AST(std::cout << "\n");
        }

        // Iterate all members
        for (Decl *D : RD->decls()) {
            if (auto *FD = dyn_cast<FieldDecl>(D)) {
                llvm::outs() << "  Field: " << FD->getNameAsString() << " : ";
                FD->getType().print(llvm::outs(), RD->getASTContext().getPrintingPolicy());
                llvm::outs() << "\n";
            } else if (auto *VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
                    llvm::outs() << "  Static member: " << VD->getNameAsString() << " : ";
                    VD->getType().print(llvm::outs(), RD->getASTContext().getPrintingPolicy());
                    llvm::outs() << "\n";
                }
            } else if (auto *Nested = dyn_cast<CXXRecordDecl>(D)) {
                llvm::outs() << "  Nested class: " << Nested->getNameAsString() << "\n";
            } else if (auto *EnumD = dyn_cast<EnumDecl>(D)) {
                llvm::outs() << "  Enum: " << EnumD->getNameAsString() << "\n";
            } else if (auto *TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                llvm::outs() << "  Type alias: " << TypeAlias->getNameAsString() << "\n";
            }
        }
    }

    CXXRecordDecl *currentClass = nullptr;

    bool VisitCXXRecordDecl(CXXRecordDecl *RD)
    {
        auto *ModelClass = lookupQualifiedRecord(Context, "cpphdl::Model");
        if (!ModelClass) {
            return true;
        }

        if (!RD || !RD->hasDefinition() || !RD->isDerivedFrom(ModelClass)) {
            return true;
        }

        if (RD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            return true;
        }

        if (currentClass != RD) {
            newModule(RD);
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

static llvm::cl::OptionCategory MyToolCategory("mytool options");

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

    return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}

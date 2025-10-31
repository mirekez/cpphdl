#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"

#include "Debug.h"
#include "Project.h"
#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"

#include <map>

using namespace clang;

cpphdl::Expr exprToExpr(const Stmt *E, ASTContext& Ctx);
bool templateToExpr(QualType QT, cpphdl::Expr& expr, ASTContext& Ctx);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName);

std::map<std::string,CXXRecordDecl*> abstractDefs;

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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
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
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
                return true;
            }

            bool TraverseReturnStmt(ReturnStmt *E)
            {
                VisitReturnStmt(E);
                return true;
            }

            bool VisitReturnStmt(ReturnStmt *RS)
            {
                method.statements.emplace_back(exprToExpr(RS, Ctx));
                DEBUG_AST(std::cout << "\n");
                DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
                return true;
            }

            bool shouldVisitTemplateInstantiations() const { return true; }
        };

        cpphdl::Method method;
        if (MD->getReturnType().getAsString() == "void") {
            method = cpphdl::Method{MD->getNameAsString()};
        }
        else {
            method = cpphdl::Method{MD->getNameAsString(), {cpphdl::Expr{MD->getReturnType().getAsString(), cpphdl::Expr::EXPR_TYPE}}};
        }
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
        DEBUG_AST(std::cout << "\nClass: " << RD->getQualifiedNameAsString() << "\n");
        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        // First extract fields from abstract class
        for (Decl *D : (abstractDefs.find(RD->getQualifiedNameAsString()) != abstractDefs.end() ?
                        abstractDefs[RD->getQualifiedNameAsString()]->decls() : RD->decls())) {
            if (auto *FD = dyn_cast<FieldDecl>(D)) {
                DEBUG_AST(std::cout << "  Field:");

                bool pointer = false;
                QualType QT = FD->getType().getNonReferenceType();
                if (QT->isPointerType()) {
                    QT = QT->getPointeeType();
                    pointer = true;
                    DEBUG_AST(std::cout << " *pointer*");
                }

                cpphdl::Expr arrayExpr;
                bool array = false;
                while (const clang::ArrayType *AT = Context->getAsArrayType(QT)) {
                    if (const auto *CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
                        DEBUG_AST(std::cout << " [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
                        arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_VALUE});
                        arrayExpr.value = "c_array";
                    }
                    else if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
                        DEBUG_AST(std::cout << " [v_array");
                        arrayExpr.sub.push_back(exprToExpr(VAT->getSizeExpr(), *Context));
                        DEBUG_AST(std::cout << "] ");
                        arrayExpr.value = "v_array";
                    }
                    else if (const auto *DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
                        DEBUG_AST(std::cout << " [d_array");
                        arrayExpr.sub.push_back(exprToExpr(DSAT->getSizeExpr(), *Context));
                        DEBUG_AST(std::cout << "] ");
                        arrayExpr.value = "d_array";
                    }

                    arrayExpr.type = cpphdl::Expr::EXPR_ARRAY;
                    QT = AT->getElementType();
                    array = true;
                }

                cpphdl::Expr expr;
                DEBUG_AST(std::cout << " (");
                if (templateToExpr(QT, expr, *Context)) {
                    DEBUG_AST(std::cout << " template) " << expr.value);
                }
                else {
                    QT = QT.getCanonicalType();
                    QT = QT.getDesugaredType(*Context);
                    std::string str = QT.getAsString(Context->getPrintingPolicy());
                    DEBUG_AST(std::cout << str << " type) " << str);
                    expr.value = str;
                    expr.type = cpphdl::Expr::EXPR_TYPE;
                }

                if (array) {
                    arrayExpr.sub.push_back(std::move(expr));
                    expr = std::move(arrayExpr);
                }

                if (pointer || (FD->getNameAsString().length() > 3
                            && (FD->getNameAsString().rfind("_in") == FD->getNameAsString().length()-3
                            || FD->getNameAsString().rfind("_out") == FD->getNameAsString().length()-4))) {
                    DEBUG_AST(std::cout << " {port " << FD->getNameAsString() << "}");

                    mod.ports.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});

                    if (FD->getInClassInitializer()) {
                        DEBUG_AST(std::cout << ", <initializer ");
                        mod.ports.back().initializer = exprToExpr(FD->getInClassInitializer(), *Context);
                        DEBUG_AST(std::cout << FD->getInClassInitializer()->getStmtClassName() << ">");
                        expr.hasInitializer = true;
                    }

                    DEBUG_AST(std::cout << "\n");
                    DEBUG_EXPR(std::cout << "        Expr: " << mod.ports.back().type.debug() << "\n");
                }
                else {
                    auto *ModuleClass = lookupQualifiedRecord(Context, "cpphdl::Module");
                    ASSERT(ModuleClass);

                    auto* CRD = FD->getType()->getAsCXXRecordDecl();

                    QT = QT.getNonReferenceType();
                    QT = QT.getDesugaredType(*Context); // remove typedefs, aliases, etc.
                    QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                    if (const auto *RT = QT->getAs<RecordType>()) {
                        if (cast<CXXRecordDecl>(RT->getDecl())) {
                            CRD = cast<CXXRecordDecl>(RT->getDecl());
                        }
                    }

                    if (auto *TST = FD->getType()->getAs<clang::TemplateSpecializationType>()) {  // check if template is derived from cpphdl::Module
                        clang::TemplateName TN = TST->getTemplateName();
                        if (auto *TD = TN.getAsTemplateDecl()) {
                            if (auto *CTD = llvm::dyn_cast<clang::ClassTemplateDecl>(TD)) {
                                CRD = CTD->getTemplatedDecl();
                            }
                        }
                    }

                    if (CRD && CRD->hasDefinition() && CRD->isDerivedFrom(ModuleClass)) {
                        DEBUG_AST(std::cout << " {member " << FD->getNameAsString() << "}");
                        mod.members.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
                        DEBUG_AST(std::cout << "\n");
                        DEBUG_EXPR(std::cout << "        Expr: " << mod.members.back().type.debug() << "\n");
                    }
                    else {
                        DEBUG_AST(std::cout << " {var " << FD->getNameAsString() << "}");
                        mod.vars.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
                        DEBUG_AST(std::cout << "\n");
                        DEBUG_EXPR(std::cout << "        Expr: " << mod.vars.back().type.debug() << "\n");
                    }
                }
            }
        }

        // Then extract types and methods from specialization
        if (auto *SD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            DEBUG_AST(std::cout << " Module parameters: ");

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
                        DEBUG_AST(std::cout << " type: " << OS.str() << "\n");
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
                        DEBUG_AST(std::cout << "\n");
                        DEBUG_EXPR(std::cout << "        Expr: " << mod.parameters.back().type.debug() << "\n");
                        break;
                    }
                    case TemplateArgument::Declaration:
                        DEBUG_AST(std::cout << " declaration: " << Arg.getAsDecl()->getNameAsString()) << "\n";
                        break;
                    case TemplateArgument::Template:
                    {
                        TemplateName TN = Arg.getAsTemplate();
                        std::string str;
                        llvm::raw_string_ostream OS(str);
                        TN.print(OS, Context->getPrintingPolicy());
                        OS.flush();
                        DEBUG_AST(std::cout << " template: " << OS.str() << "\n");
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
                            DEBUG_AST(std::cout << "\n");
                            DEBUG_EXPR(std::cout << "        Expr: " << exprToExpr(E,*Context).debug() << "\n");
                        }
                        else {
                            DEBUG_AST(std::cout << "\n");
                        }
                        break;
                    }
                    case TemplateArgument::Pack:
                    default:
                        DEBUG_AST(std::cout << " unhandled\n");
                    break;
                }
            }
        }

        for (Decl *D : RD->decls()) {
            if (auto *VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST(std::cout << " (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(std::cout, RD->getASTContext().getPrintingPolicy());
//                    std::cout << "\n";
                }
            } else
            if (auto *Nested = dyn_cast<CXXRecordDecl>(D)) {
                DEBUG_AST(std::cout << "  Nested class: " << Nested->getNameAsString() << "\n");
            } else
            if (auto *EnumD = dyn_cast<EnumDecl>(D)) {
                DEBUG_AST(std::cout << "  Enum: " << EnumD->getNameAsString() << "\n");
            } else
            if (auto *TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                DEBUG_AST(std::cout << "  Type alias: " << TypeAlias->getNameAsString() << "\n");
            } else
            if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(D)) {
                DEBUG_AST(std::cout << "  Method: " << MD->getQualifiedNameAsString() << "\n");
                if (!putMethod(MD, mod)) {
                    return false;
                }
            }
//                if (const auto* SD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Arg.getAsType()/*->getAsCXXRecordDecl()*/)) {
//                    expr1.value = SD->getQualifiedNameAsString();
//                    expr1.type = cpphdl::Expr::EXPR_TEMPLATE;
//                    getParamsFromSourceOrStr
//                    templateToExpr(SD, nullptr, expr1, Ctx);
//                    DEBUG_AST(std::cout << " [template spec]  " << expr1.value);
//                    expr.sub.emplace_back(std::move(expr1));
//                }
        }

        currProject->modules.emplace_back(std::move(mod));
        return true;
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
            abstractDefs[RD->getQualifiedNameAsString()] = RD;
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
    currProject->generate("generated");
    return ret;
}

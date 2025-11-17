#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"

#include "helpers.h"

#include "Debug.h"
#include "Project.h"
#include "Module.h"
#include "Method.h"
#include "Field.h"
#include "Expr.h"
#include "Comb.h"
#include "Struct.h"

#include <map>

using namespace clang;

std::vector<CXXRecordDecl*> abstractDefs;

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

void putMethod(const CXXMethodDecl* MD, Helpers& hlp)
{
    DEBUG_AST(std::cout << "  Method: " << MD->getQualifiedNameAsString() << "\n");
//    if (!MD->hasBody()) {
//        return true;
//    }

    struct LocalVisitor : RecursiveASTVisitor<LocalVisitor>
    {
        Helpers& hlp;
        cpphdl::Method& method;
        LocalVisitor(Helpers& helpers, cpphdl::Method& method) : hlp(helpers), method(method) {}

        bool TraverseBinaryOperator(BinaryOperator* E)
        {
            VisitBinaryOperator(E);
            return true;
        }

        bool VisitBinaryOperator(BinaryOperator* BO)
        {
            method.statements.emplace_back(hlp.exprToExpr(BO));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr* E)
        {
            VisitCXXOperatorCallExpr(E);
            return true;
        }

        bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* OCE)
        {
            method.statements.emplace_back(hlp.exprToExpr(OCE));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseCXXMemberCallExpr(CXXMemberCallExpr* E)
        {
            VisitCXXMemberCallExpr(E);
            return true;
        }

        bool VisitCXXMemberCallExpr(CXXMemberCallExpr* MCE)
        {
            method.statements.emplace_back(hlp.exprToExpr(MCE));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseMemberExpr(MemberExpr* E)
        {
            VisitMemberExpr(E);
            return true;
        }

        bool VisitMemberExpr(MemberExpr* ME)
        {
            method.statements.emplace_back(hlp.exprToExpr(ME));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseForStmt(ForStmt* E)
        {
            VisitForStmt(E);
            return true;
        }

        bool VisitForStmt(ForStmt* FS)
        {
            method.statements.emplace_back(hlp.exprToExpr(FS));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseWhileStmt(WhileStmt* E)
        {
            VisitWhileStmt(E);
            return true;
        }

        bool VisitWhileStmt(WhileStmt* WS)
        {
            method.statements.emplace_back(hlp.exprToExpr(WS));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseIfStmt(IfStmt* E)
        {
            VisitIfStmt(E);
            return true;
        }

        bool VisitIfStmt(IfStmt* IS)
        {
            method.statements.emplace_back(hlp.exprToExpr(IS));
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "        Expr: " << method.statements.back().debug() << "\n");
            return true;
        }

        bool TraverseReturnStmt(ReturnStmt* E)
        {
            VisitReturnStmt(E);
            return true;
        }

        bool VisitReturnStmt(ReturnStmt* RS)
        {
            method.statements.emplace_back(hlp.exprToExpr(RS));
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

    hlp.thisIsModule = true;
    if (MD->getParent()->getQualifiedNameAsString() != hlp.mod.name) {
        hlp.thisIsModule = false;
        QualType QT = MD->getThisType()->getPointeeType();

        cpphdl::Expr expr = hlp.digQT(QT);

        QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(std::cout << "  Param: this (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{"__this", std::move(expr)});
        DEBUG_AST(std::cout << "\n");
        DEBUG_EXPR(std::cout << "    Expr: " << method.parameters.back().type.debug() << "\n");
    }

    for (const ParmVarDecl* Param : MD->parameters()) {
        QualType QT = Param->getType().getNonReferenceType();

        cpphdl::Expr expr = hlp.digQT(QT);

        QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(std::cout << "  Param: " << Param->getNameAsString() << " (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{Param->getNameAsString(), std::move(expr)});
        if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
            auto st = hlp.exportStruct(QT->getAsCXXRecordDecl());
            auto ret = hlp.mod.imports.emplace(st.name);
            if (ret.second) {
                currProject->structs.emplace_back(std::move(st));
            }
        }
        DEBUG_AST(std::cout << "\n");
        DEBUG_EXPR(std::cout << "    Expr: " << method.parameters.back().type.debug() << "\n");
    }

    LocalVisitor(hlp, method).TraverseStmt(const_cast<Stmt*>(MD->getBody()));
    if (MD->getNameAsString() != hlp.mod.name && MD->getNameAsString() != std::string("~") + hlp.mod.name && MD->getNameAsString().find("operator") != 0) {
        hlp.mod.methods.emplace_back(std::move(method));
    }
    DEBUG_AST(std::cout << "\n");
}

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext* Context)
        : Context(Context), SM(Context->getSourceManager()) {}


    bool putModule(CXXRecordDecl* RD)
    {
        DEBUG_AST(std::cout << "\nClass: " << RD->getQualifiedNameAsString() << "\n");

        // We want to have numbers as parameters and types hard-coded in generated names

        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        Helpers hlp(Context, &mod);

        // specialization
        std::vector<cpphdl::Field> params;
        hlp.specializationToParameters(RD, params);
        hlp.addSpecializationName(mod.name, params);
        std::erase_if(params, [](cpphdl::Field& field) { return field.type.type != cpphdl::Expr::EXPR_VALUE; });
        mod.parameters = std::move(params);
        mod.origName = RD->getQualifiedNameAsString();

        for (Decl* D : RD->decls()) {
            if (auto* FD = dyn_cast<FieldDecl>(D)) {
                hlp.putField(FD);
            }
        }

        for (Decl* D : RD->decls()) {
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST(std::cout << " (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(std::cout, RD->getASTContext().getPrintingPolicy());
//                    std::cout << "\n";
                }
            } else
            if ([[maybe_unused]] auto* Nested = dyn_cast<CXXRecordDecl>(D)) {
                DEBUG_AST(std::cout << "  Nested class: " << Nested->getNameAsString() << "\n");
            } else
            if ([[maybe_unused]] auto* EnumD = dyn_cast<EnumDecl>(D)) {
                DEBUG_AST(std::cout << "  Enum: " << EnumD->getNameAsString() << "\n");
            } else
            if ([[maybe_unused]] auto* TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                DEBUG_AST(std::cout << "  Type alias: " << TypeAlias->getNameAsString() << "\n");
            } else
            if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(D)) {  // Method
                if (MD->getQualifiedNameAsString().find("::" + mod.origName) == (size_t)-1
                    && MD->getQualifiedNameAsString().find("::~" + mod.origName) == (size_t)-1
                    && MD->getQualifiedNameAsString().find("::operator=") == (size_t)-1) {
                    putMethod(MD, hlp);
                }
            }
        }

        for (auto* RD : abstractDefs) {
            if (mod.name.find(RD->getQualifiedNameAsString()) == 0) {
                DEBUG_AST(std::cout << "  Applying abstract: " << RD->getQualifiedNameAsString() << " to " << mod.name << "\n");

                for (Decl* D : RD->decls()) {
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        hlp.putField(FD);
                    }
                }
            }
        }

        currProject->modules.emplace_back(std::move(mod));

        return true;
    }

//    CXXRecordDecl* currentClass = nullptr;

    bool VisitCXXRecordDecl(CXXRecordDecl* RD)
    {
        auto* ModuleClass = lookupQualifiedRecord(Context, "cpphdl::Module");
        if (!ModuleClass) {
            return true;
        }

        if (!RD || !RD->hasDefinition() || !RD->isDerivedFrom(ModuleClass)) {
            return true;
        }

        if (RD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            abstractDefs.push_back(RD);
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

    ASTContext* Context;
    const SourceManager &SM;
};

struct MethodConsumer : public ASTConsumer
{
    explicit MethodConsumer(ASTContext* Context) : Visitor(Context) {}

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

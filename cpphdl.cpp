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
#include "Struct.h"

#include <map>

using namespace clang;

cpphdl::Expr exprToExpr(const Stmt* E, ASTContext& Ctx);
bool templateToExpr(cpphdl::Module& mod, QualType QT, cpphdl::Expr& expr, ASTContext& Ctx);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName);
cpphdl::Struct exportStruct(cpphdl::Module& mod, CXXRecordDecl* RD, ASTContext& Ctx);
cpphdl::Expr digQT(cpphdl::Module& mod, QualType& QT, ASTContext& Ctx);
void addSpecializationName(std::string& name, std::vector<cpphdl::Field>& params, bool onlyTypes = true);
bool specializationToParameters(cpphdl::Module& mod, CXXRecordDecl*RD, std::vector<cpphdl::Field>& params, ASTContext& Ctx);

std::vector<CXXRecordDecl*> abstractDefs;

void updateExpr(cpphdl::Expr& expr1, cpphdl::Expr& expr2)
{
    if (expr1.type == expr2.type && expr1.sub.size() == expr2.sub.size()) {
        for (size_t i=0; i < expr1.sub.size(); ++i) {
            updateExpr(expr1.sub[0], expr2.sub[0]);
        }
    }
    if (expr1.type == cpphdl::Expr::EXPR_VALUE && expr2.type != cpphdl::Expr::EXPR_VALUE) {
         expr1 = std::move(expr2);
    }
}

bool putField(cpphdl::Module& mod, FieldDecl* FD, ASTContext* Context)
{
    DEBUG_AST(std::cout << "  Field:");

    bool pointer = false;
    QualType QT = FD->getType().getNonReferenceType();
    if (QT->isPointerType()) {  // while?
        QT = QT->getPointeeType();
        pointer = true;
        DEBUG_AST(std::cout << " *pointer*");
    }

    cpphdl::Expr expr = digQT(mod, QT, *Context);

    if (pointer || (FD->getNameAsString().length() > 3
                && (FD->getNameAsString().rfind("_in") == FD->getNameAsString().length()-3
                || FD->getNameAsString().rfind("_out") == FD->getNameAsString().length()-4))) {
        DEBUG_AST(std::cout << " {port " << FD->getNameAsString() << "}");

        cpphdl::Field* field = 0;
        auto it = std::find_if(mod.ports.begin(), mod.ports.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
        if (it != mod.ports.end()) {
            field = &*it;
            updateExpr(field->type, expr);
        }
        else {
            field = &mod.ports.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
        }

        if (FD->getInClassInitializer()) {
            DEBUG_AST(std::cout << ", <initializer ");
            field->initializer = exprToExpr(FD->getInClassInitializer(), *Context);
            DEBUG_AST(std::cout << FD->getInClassInitializer()->getStmtClassName() << ">");
            expr.hasInitializer = true;
        }

        DEBUG_AST(std::cout << "\n");
        DEBUG_EXPR(std::cout << "    Expr: " << field->type.debug() << "\n");
    }
    else {
        auto* ModuleClass = lookupQualifiedRecord(Context, "cpphdl::Module");
        ASSERT(ModuleClass);

        auto* CRD = /*FD->getType()*/QT->getAsCXXRecordDecl();

        QT = QT.getNonReferenceType();
        QT = QT.getDesugaredType(*Context); // remove typedefs, aliases, etc.
        QT = QT.getCanonicalType();        // ensure you have the actual canonical form

        if (const auto* RT = QT->getAs<RecordType>()) {
            if (cast<CXXRecordDecl>(RT->getDecl())) {
                CRD = cast<CXXRecordDecl>(RT->getDecl());
            }
        }

        if (auto* TST = FD->getType()->getAs<clang::TemplateSpecializationType>()) {
            clang::TemplateName TN = TST->getTemplateName();
            if (auto* TD = TN.getAsTemplateDecl()) {
                if (auto* CTD = llvm::dyn_cast<clang::ClassTemplateDecl>(TD)) {
                    CRD = CTD->getTemplatedDecl();
                }
            }
        }

        if (CRD && CRD->hasDefinition() && CRD->isDerivedFrom(ModuleClass)) {  // check if template is derived from cpphdl::Module
            DEBUG_AST(std::cout << " {member " << FD->getNameAsString() << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(mod.members.begin(), mod.members.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
            if (it != mod.members.end()) {
                field = &*it;
                updateExpr(field->type, expr);
            }
            else {
                field = &mod.members.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
            }
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "    Expr: " << field->type.debug() << "\n");
        }
        else {
            DEBUG_AST(std::cout << " {var " << FD->getNameAsString() << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(mod.vars.begin(), mod.vars.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
            if (it != mod.vars.end()) {
                field = &*it;
                updateExpr(field->type, expr);
            }
            else {
                field = &mod.vars.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
            }
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "    Expr: " << field->type.debug() << "\n");
            if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                auto st = exportStruct(mod, QT->getAsCXXRecordDecl(), *Context);
                auto ret = mod.imports.emplace(st.name);
                if (ret.second) {
                    currProject->structs.emplace_back(std::move(st));
                }
                DEBUG_AST(std::cout << "\n");
            }
        }
    }
    return true;
}

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext* Context)
        : Context(Context), SM(Context->getSourceManager()) {}

    bool putMethod(cpphdl::Module& mod, const CXXMethodDecl* MD)
    {
        DEBUG_AST(std::cout << "  Method: " << MD->getQualifiedNameAsString() << "\n");
//        if (!MD->hasBody()) {
//            return true;
//        }

        struct LocalVisitor : RecursiveASTVisitor<LocalVisitor>
        {
            ASTContext& Ctx;
            cpphdl::Method& method;
            LocalVisitor(ASTContext &Ctx, cpphdl::Method& method) : Ctx(Ctx), method(method) {}

            bool TraverseBinaryOperator(BinaryOperator* E)
            {
                VisitBinaryOperator(E);
                return true;
            }

            bool VisitBinaryOperator(BinaryOperator* BO)
            {
                method.statements.emplace_back(exprToExpr(BO, Ctx));
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
                method.statements.emplace_back(exprToExpr(OCE, Ctx));
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
                method.statements.emplace_back(exprToExpr(MCE, Ctx));
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
                method.statements.emplace_back(exprToExpr(ME, Ctx));
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
                method.statements.emplace_back(exprToExpr(FS, Ctx));
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
                method.statements.emplace_back(exprToExpr(WS, Ctx));
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
                method.statements.emplace_back(exprToExpr(IS, Ctx));
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
        for (const ParmVarDecl* Param : MD->parameters()) {
            QualType QT = Param->getType().getNonReferenceType();;

            cpphdl::Expr expr = digQT(mod, QT, *Context);

            QT = QT.getDesugaredType(*Context); // remove typedefs, aliases, etc.
            QT = QT.getCanonicalType();        // ensure you have the actual canonical form
            DEBUG_AST(std::cout << "  Param: " << Param->getNameAsString() << " (" << QT.getAsString() << ")");

            method.parameters.emplace_back(cpphdl::Field{Param->getNameAsString(), std::move(expr)});
            if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                auto st = exportStruct(mod, QT->getAsCXXRecordDecl(), *Context);
                auto ret = mod.imports.emplace(st.name);
                if (ret.second) {
                    currProject->structs.emplace_back(std::move(st));
                }
            }
            DEBUG_AST(std::cout << "\n");
            DEBUG_EXPR(std::cout << "    Expr: " << method.parameters.back().type.debug() << "\n");
        }

        LocalVisitor(*Context, method).TraverseStmt(const_cast<Stmt*>(MD->getBody()));
        if (MD->getNameAsString() != mod.name && MD->getNameAsString() != std::string("~") + mod.name && MD->getNameAsString().find("operator") != 0) {
            mod.methods.emplace_back(std::move(method));
        }
        DEBUG_AST(std::cout << "\n");
        return true;
    }

    bool putModule(CXXRecordDecl* RD)
    {
        DEBUG_AST(std::cout << "\nClass: " << RD->getQualifiedNameAsString() << "\n");

        // We want to have numbers as parameters and types hard-coded in generated names

        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        // specialization
        std::vector<cpphdl::Field> params;
        specializationToParameters(mod, RD, params, *Context);
        addSpecializationName(mod.name, params);
        std::erase_if(params, [](cpphdl::Field& field) { return field.type.type != cpphdl::Expr::EXPR_VALUE; });
        mod.parameters = std::move(params);
        mod.origName = RD->getQualifiedNameAsString();

        for (Decl* D : RD->decls()) {
            if (auto* FD = dyn_cast<FieldDecl>(D)) {
                putField(mod, FD, Context);
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
                    if (!putMethod(mod, MD)) {
                        return false;
                    }
                }
            }
        }

        currProject->modules.emplace_back(std::move(mod));

        for (auto* RD : abstractDefs) {
            if (currProject->modules.back().name.find(RD->getQualifiedNameAsString()) == 0) {
                DEBUG_AST(std::cout << "  Applying abstract: " << RD->getQualifiedNameAsString() << " to " << currProject->modules.back().name << "\n");

                for (Decl* D : RD->decls()) {
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        putField(currProject->modules.back(), FD, Context);
                    }
                }
            }
        }

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

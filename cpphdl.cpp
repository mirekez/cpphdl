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

void updateExpr(cpphdl::Expr& expr1, cpphdl::Expr& expr2)  // puts expressions instead of values for number parameters
{
    if (expr1.type == expr2.type && expr1.sub.size() == expr2.sub.size()) {
        for (size_t i=0; i < expr1.sub.size(); ++i) {
            updateExpr(expr1.sub[i], expr2.sub[i]);
        }
    }
    if (expr1.type == cpphdl::Expr::EXPR_VALUE && expr2.type != cpphdl::Expr::EXPR_VALUE) {
         expr1 = std::move(expr2);
    }
}

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp)
{
    std::string sname = RD->getQualifiedNameAsString();
    size_t pos;
    while ((pos = sname.find("::")) != (size_t)-1) {
        sname.replace(pos, 2, "__");
    }

    // extracting parameters of the template
    std::vector<cpphdl::Field> params;
    hlp.specializationToParameters(RD, params);
    hlp.addSpecializationName(sname, params, false);

    cpphdl::Struct st{sname, (RD->isUnion() ? cpphdl::Struct::STRUCT_UNION : cpphdl::Struct::STRUCT_STRUCT)};
    st.origName = RD->getQualifiedNameAsString();
    DEBUG_AST(debugIndent++, "@ exportStruct(" << RD->getQualifiedNameAsString() << "):");
    on_return ret_debug([](){ --debugIndent; });

    for (Decl* D : RD->decls()) {
        if (auto* FD = dyn_cast<FieldDecl>(D)) {
            DEBUG_AST(debugIndent, "Field:");

            bool pointer = false;
            QualType QT = FD->getType().getNonReferenceType();
            if (QT->isPointerType()) {
                QT = QT->getPointeeType();
                pointer = true;
                DEBUG_AST1(" *pointer*");
            }

            cpphdl::Expr arrayExpr;
            bool array = false;
            while (const clang::ArrayType* AT = hlp.Ctx.getAsArrayType(QT)) {
                if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
                    DEBUG_AST1(" [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
                    arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_VALUE});
                    arrayExpr.value = "c_array";
                }
                else if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
                    DEBUG_AST1(" [v_array");
                    arrayExpr.sub.push_back(hlp.exprToExpr(VAT->getSizeExpr()));
                    DEBUG_AST1("] ");
                    arrayExpr.value = "v_array";
                }
                else if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
                    DEBUG_AST1(" [d_array");
                    arrayExpr.sub.push_back(hlp.exprToExpr(DSAT->getSizeExpr()));
                    DEBUG_AST1("] ");
                    arrayExpr.value = "d_array";
                }

                arrayExpr.type = cpphdl::Expr::EXPR_ARRAY;
                QT = AT->getElementType();
                array = true;
            }

            cpphdl::Expr expr;
            DEBUG_AST1(" (");
//            if (templateToExpr(QT, expr)) {
//            std::vector<cpphdl::Field> params;
//            if (specializationToParameters(FD, params)) {
//                DEBUG_AST1(" template) " << expr.value);
//            }
//            else {
                QT = QT.getCanonicalType();
                QT = QT.getDesugaredType(hlp.Ctx);
                std::string str = QT.getAsString(hlp.Ctx.getPrintingPolicy());
                DEBUG_AST1(str << " type) " << str);
                expr.value = str;
                expr.type = cpphdl::Expr::EXPR_TYPE;
//            }

            if (array) {
                arrayExpr.sub.emplace_back(std::move(expr));
                expr = std::move(arrayExpr);
            }

            if (!pointer) {

                QT = QT.getNonReferenceType();
                QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
                QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                DEBUG_AST1(" {var " << FD->getNameAsString() << "} " << (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->isAnonymousStructOrUnion()?"ANON":""));
                st.fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)/*, std::move(params)*/});
                if (FD->isBitField()) {
                    DEBUG_AST1(" |bitfield ");
                    st.fields.back().bitwidth = hlp.exprToExpr(FD->getBitWidth());
                    DEBUG_AST1("|");
                }
//                DEBUG_EXPR(debugIndent, " Expr: " << st.fields.back().type.debug(debugIndent));
                if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1 &&
                    QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("std::") == (size_t)-1) {  // we need std containers in structs?
                    auto st1 = exportStruct(QT->getAsCXXRecordDecl(), hlp);

                    if (QT->getAsCXXRecordDecl()->isAnonymousStructOrUnion() || !QT->getAsCXXRecordDecl()->getIdentifier()) {
                        st1.name = FD->getNameAsString();
                        st.fields.back().definition = std::move(st1);
                    }
                    else {
                        auto ret = hlp.mod.imports.emplace(st1.name);
                        if (ret.second) {
                            currProject->structs.emplace_back(std::move(st1));
                        }
                    }
                }

                DEBUG_EXPR(debugIndent, " Expr: " << st.fields.back().expr.debug(debugIndent));
                if (FD->isBitField()) {
                    DEBUG_EXPR(debugIndent, " Expr(bitfield): " << st.fields.back().bitwidth.debug(debugIndent));
                }
            }
        }
        if (VarDecl* VD = dyn_cast<VarDecl>(D)) {
            if (VD->isStaticDataMember() && VD->isConstexpr() && VD->getInit()) {
                DEBUG_AST(debugIndent, "Const(" << VD->getNameAsString() << "): ");
                st.parameters.emplace_back(cpphdl::Field{VD->getNameAsString(), hlp.exprToExpr(VD->getInit())});
                DEBUG_EXPR(debugIndent, " Expr: " << st.parameters.back().expr.debug(debugIndent));
            }
        }
    }

    return st;
}

bool putField(FieldDecl* FD, Helpers& hlp)
{
    DEBUG_AST(debugIndent++, "  putField:");
    on_return ret_debug([](){ --debugIndent; });

    bool pointer = false;
    QualType QT = FD->getType().getNonReferenceType();
    if (QT->isPointerType()) {  // while?
        QT = QT->getPointeeType();
        pointer = true;
        DEBUG_AST1(" *pointer*");
    }

    QT = QT.getDesugaredType(hlp.Ctx);
//!!!    QT = QT.getCanonicalType();
//    auto T = QT.getUnqualifiedType();

    hlp.getStdFunctionType(QT);

    cpphdl::Expr expr = hlp.digQT(QT);

    if (str_ending(FD->getNameAsString(), "_in") || str_ending(FD->getNameAsString(), "_out")) {
        DEBUG_AST1(" {port " << FD->getNameAsString() << "}");

        cpphdl::Field* field = 0;
        auto it = std::find_if(hlp.mod.ports.begin(), hlp.mod.ports.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
        if (it != hlp.mod.ports.end()) {
            field = &*it;
            updateExpr(field->expr, expr);
        }
        else {
            field = &hlp.mod.ports.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
        }

        if (FD->getInClassInitializer()) {
            DEBUG_AST1(", <initializer ");
            field->initializer = hlp.exprToExpr(FD->getInClassInitializer());
            DEBUG_AST1(">");
            expr.hasInitializer = true;
        }

        DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));
    }
    else {
        auto* ModuleClass = lookupQualifiedRecord(&hlp.Ctx, "cpphdl::Module");
        ASSERT(ModuleClass);

        auto* CRD = /*FD->getType()*/QT->getAsCXXRecordDecl();

        QT = QT.getNonReferenceType();
        QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
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
            DEBUG_AST1(" {member " << FD->getNameAsString() << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod.members.begin(), hlp.mod.members.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
            if (it != hlp.mod.members.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            }
            else {
                field = &hlp.mod.members.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
            }
            DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));
        }
        else {
            DEBUG_AST1(" {var " << FD->getNameAsString() << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod.vars.begin(), hlp.mod.vars.end(), [&](auto& elem){ return elem.name == FD->getNameAsString(); } );
            if (it != hlp.mod.vars.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            }
            else {
                field = &hlp.mod.vars.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)});
            }
            DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));
            if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                auto st = exportStruct(QT->getAsCXXRecordDecl(), hlp);
                auto ret = hlp.mod.imports.emplace(st.name);
                if (ret.second) {
                    currProject->structs.emplace_back(std::move(st));
                }
            }
        }
    }
    return true;
}

std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp)
{
    auto* ModuleClass = lookupQualifiedRecord(&hlp.Ctx, "cpphdl::Module");
    ASSERT(ModuleClass);

    if (MD->getQualifiedNameAsString().find("::" + hlp.mod.origName) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::~" + hlp.mod.origName) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::operator=") != (size_t)-1
    || MD->getQualifiedNameAsString().find("cpphdl::") != (size_t)-1
    || MD->getQualifiedNameAsString().find("std::") != (size_t)-1
    || (MD->getParent()->isDerivedFrom(ModuleClass) && hlp.mod.name.find(MD->getParent()->getQualifiedNameAsString())) != 0) {  // module's class method but not current module
        return "";
    }

    DEBUG_AST(debugIndent++, "# putMethod: " << MD->getQualifiedNameAsString());
    on_return ret_debug([](){ --debugIndent; });
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
            DEBUG_AST(debugIndent, "% VisitBinaryOperator");
            method.statements.emplace_back(hlp.exprToExpr(BO));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseCXXOperatorCallExpr(CXXOperatorCallExpr* E)
        {
            VisitCXXOperatorCallExpr(E);
            return true;
        }

        bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* OCE)
        {
            DEBUG_AST(debugIndent, "% VisitCXXOperatorCallExpr");
            method.statements.emplace_back(hlp.exprToExpr(OCE));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseCXXMemberCallExpr(CXXMemberCallExpr* E)
        {
            VisitCXXMemberCallExpr(E);
            return true;
        }

        bool VisitCXXMemberCallExpr(CXXMemberCallExpr* MCE)
        {
            DEBUG_AST(debugIndent, "% VisitCXXMemberCallExpr");
            method.statements.emplace_back(hlp.exprToExpr(MCE));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseMemberExpr(MemberExpr* E)
        {
            VisitMemberExpr(E);
            return true;
        }

        bool VisitMemberExpr(MemberExpr* ME)
        {
            DEBUG_AST(debugIndent, "% VisitMemberExpr");
            method.statements.emplace_back(hlp.exprToExpr(ME));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseCallExpr(CallExpr* E)
        {
            VisitCallExpr(E);
            return true;
        }

        bool VisitCallExpr(clang::CallExpr *CE) {
            DEBUG_AST(debugIndent, "% VisitCallExpr");
            method.statements.emplace_back(hlp.exprToExpr(CE));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseForStmt(ForStmt* E)
        {
            VisitForStmt(E);
            return true;
        }

        bool VisitForStmt(ForStmt* FS)
        {
            DEBUG_AST(debugIndent, "% VisitForStmt");
            method.statements.emplace_back(hlp.exprToExpr(FS));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseWhileStmt(WhileStmt* E)
        {
            VisitWhileStmt(E);
            return true;
        }

        bool VisitWhileStmt(WhileStmt* WS)
        {
            DEBUG_AST(debugIndent, "% VisitWhileStmt");
            method.statements.emplace_back(hlp.exprToExpr(WS));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseIfStmt(IfStmt* E)
        {
            VisitIfStmt(E);
            return true;
        }

        bool VisitIfStmt(IfStmt* IS)
        {
            DEBUG_AST(debugIndent, "% VisitIfStmt");
            method.statements.emplace_back(hlp.exprToExpr(IS));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
            return true;
        }

        bool TraverseReturnStmt(ReturnStmt* E)
        {
            VisitReturnStmt(E);
            return true;
        }

        bool VisitReturnStmt(ReturnStmt* RS)
        {
            DEBUG_AST(debugIndent, "% VisitReturnStmt");
            method.statements.emplace_back(hlp.exprToExpr(RS));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
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

    unsigned savedFlags = hlp.flags;
    if (hlp.mod.name.find(MD->getParent()->getQualifiedNameAsString()) != 0) {  // method of structure, adding first parameter this
        // extracting parameters of the template
        std::vector<cpphdl::Field> params;
        hlp.specializationToParameters(MD->getParent(), params);
        hlp.addSpecializationName(method.name, params, false);

        DEBUG_AST1(" - not Module method: (" << hlp.mod.name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
        hlp.flags |= Helpers::FLAG_EXTERNAL_THIS;
        QualType QT = MD->getThisType()->getPointeeType();

        cpphdl::Expr expr = hlp.digQT(QT);

        QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(debugIndent, "Param this (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{"_this", std::move(expr)});
        DEBUG_EXPR(debugIndent, " Expr: " << method.parameters.back().expr.debug(debugIndent));
    }

    for (const ParmVarDecl* Param : MD->parameters()) {
        QualType QT = Param->getType().getNonReferenceType();

        cpphdl::Expr expr = hlp.digQT(QT);

        QT = QT.getDesugaredType(hlp.Ctx); // remove typedefs, aliases, etc.
        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(debugIndent, "Param: " << Param->getNameAsString() << " (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{Param->getNameAsString(), std::move(expr)});
        if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
            auto st = exportStruct(QT->getAsCXXRecordDecl(), hlp);
            auto ret = hlp.mod.imports.emplace(st.name);
            if (ret.second) {
                currProject->structs.emplace_back(std::move(st));
            }
        }
        DEBUG_EXPR(debugIndent, " Expr: " << method.parameters.back().expr.debug(debugIndent));
    }

    LocalVisitor(hlp, method).TraverseStmt(const_cast<Stmt*>(MD->getBody()));
//    if (MD->getNameAsString() != hlp.mod.name
////     && MD->getNameAsString() != std::string("~") + hlp.mod.name
//     && MD->getNameAsString().find("operator") != 0) {
    std::string ret = method.name;
    auto it = std::find_if(hlp.mod.methods.begin(), hlp.mod.methods.end(), [&](auto& elem){ return elem.name == method.name; } );
    if (it != hlp.mod.methods.end()) {
        if (it->ret.size() == method.ret.size()) {
            for (size_t i=0; i < it->ret.size(); ++i) {
                DEBUG_AST(debugIndent, "updating ret...");
                updateExpr(it->ret[i], method.ret[i]);
            }
        }
        if (it->parameters.size() == method.parameters.size()) {
            for (size_t i=0; i < it->parameters.size(); ++i) {
                DEBUG_AST(debugIndent, "updating parameters...");
                updateExpr(it->parameters[i].expr, method.parameters[i].expr);
                updateExpr(it->parameters[i].initializer, method.parameters[i].initializer);
                updateExpr(it->parameters[i].bitwidth, method.parameters[i].bitwidth);
            }
        }
        if (it->statements.size() == method.statements.size()) {
            for (size_t i=0; i < it->statements.size(); ++i) {
                DEBUG_AST(debugIndent, "updating statements...");
                updateExpr(it->statements[i], method.statements[i]);
            }
        }
    }
    else {
        hlp.mod.methods.emplace_back(std::move(method));
    }
//    }
    hlp.flags = savedFlags;
    return ret;
}

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext* Context)
        : Context(Context)/*, SM(Context->getSourceManager())*/ {}


    void putModule(CXXRecordDecl* RD)
    {
        DEBUG_AST(debugIndent++, "# putModule: " << RD->getQualifiedNameAsString());
        on_return ret_debug([](){ --debugIndent; });

        // We want to have numbers as parameters and types hard-coded in generated names

        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        Helpers hlp(Context, &mod);

        // specialization
        std::vector<cpphdl::Field> params;
        hlp.specializationToParameters(RD, params);
        hlp.addSpecializationName(mod.name, params);
        std::erase_if(params, [](cpphdl::Field& field) { return field.expr.type != cpphdl::Expr::EXPR_VALUE; });
        mod.parameters = std::move(params);
        mod.origName = RD->getQualifiedNameAsString();

        for (Decl* D : RD->decls()) {
            if (auto* FD = dyn_cast<FieldDecl>(D)) {
                putField(FD, hlp);
            }
        }

        for (Decl* D : RD->decls()) {
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST1(" (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(std::cout, RD->getASTContext().getPrintingPolicy());
//                    "\n";
                }
            } else
            if ([[maybe_unused]] auto* Nested = dyn_cast<CXXRecordDecl>(D)) {
                DEBUG_AST(debugIndent, "Nested class: " << Nested->getNameAsString());
            } else
            if ([[maybe_unused]] auto* EnumD = dyn_cast<EnumDecl>(D)) {
                DEBUG_AST(debugIndent, "Enum: " << EnumD->getNameAsString());
            } else
            if ([[maybe_unused]] auto* TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                DEBUG_AST(debugIndent, "Type alias: " << TypeAlias->getNameAsString());
            } else
            if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(D)) {  // Method
                putMethod(MD, hlp);
            }
        }

        for (auto* RD : abstractDefs) {
            if (mod.name.find(RD->getQualifiedNameAsString()) == 0) {
                DEBUG_AST(debugIndent, "Applying abstract: " << RD->getQualifiedNameAsString() << " to " << mod.name);

                for (Decl* D : RD->decls()) {
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        putField(FD, hlp);
                    }
                }
            }
        }

        currProject->modules.emplace_back(std::move(mod));
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
        }
        else {
            putModule(RD);
        }

        return true;
    }

    bool shouldVisitTemplateInstantiations() const { return true; }

    ASTContext* Context;
//    const SourceManager &SM;
    std::vector<CXXRecordDecl*> abstractDefs;
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

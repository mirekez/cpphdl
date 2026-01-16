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

void updateExpr(cpphdl::Expr& expr1, cpphdl::Expr& expr2)  // add correspondent abstract expressions to number parameters
{
    if (expr1.type == expr2.type && expr1.sub.size() == expr2.sub.size()) {
        for (size_t i=0; i < expr1.sub.size(); ++i) {
            updateExpr(expr1.sub[i], expr2.sub[i]);
        }
    }
    if ((expr1.type == cpphdl::Expr::EXPR_NUM || expr1.type == cpphdl::Expr::EXPR_PARAM) && expr2.type != cpphdl::Expr::EXPR_NUM) {
         expr1.sub.emplace_back(std::move(expr2));
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
    if (auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        const TemplateArgumentList& Args = CTSD->getTemplateArgs();
        const TemplateParameterList* Params = CTSD->getSpecializedTemplate()->getTemplateParameters();
        for (unsigned i = 0; i < Args.size(); ++i) {
            auto expr = hlp.ArgToExpr(Args[i], Params->getParam(i)->getNameAsString());
            hlp.genSpecializationTypeName(i == 0, sname, expr);
        }
    }

    cpphdl::Struct st{sname, (RD->isUnion() ? cpphdl::Struct::STRUCT_UNION : cpphdl::Struct::STRUCT_STRUCT)};
    st.origName = RD->getQualifiedNameAsString();
    DEBUG_AST(debugIndent++, "@ exportStruct(" << RD->getQualifiedNameAsString() << "):"); on_return ret_debug([](){ --debugIndent; });

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
                    arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_NUM});
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
//            if (ArgToExpr(FD, params)) {
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
//?                QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                auto* CRD = hlp.resolveCXXRecordDecl(QT);
                DEBUG_AST1(" {var " << FD->getNameAsString() << "} " << (CRD && CRD->isAnonymousStructOrUnion()?"ANON":""));
                st.fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)/*, std::move(params)*/});
                if (FD->isBitField()) {
                    DEBUG_AST1(" |bitfield ");
                    st.fields.back().bitwidth = hlp.exprToExpr(FD->getBitWidth());
                    DEBUG_AST1("|");
                }
//                DEBUG_EXPR(debugIndent, " Expr: " << st.fields.back().type.debug(debugIndent));
                if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 &&
                    CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {  // we need std containers in structs?
                    auto st1 = exportStruct(CRD, hlp);

                    if (CRD->isAnonymousStructOrUnion() || !CRD->getIdentifier()) {
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

void putField(QualType fieldType, const std::string& fieldName, const Expr* initializer, Helpers& hlp)
{
    DEBUG_AST(debugIndent++, "# putField: "); on_return ret_debug([](){ --debugIndent; });
    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
    ASSERT(ModuleClass);

    QualType QT = fieldType;

//    bool pointer = false;
    if (QT->isPointerType()) {  // while?
        QT = QT->getPointeeType();
//        pointer = true;
        DEBUG_AST1(" *pointer*");
    }

    QT = QT.getDesugaredType(hlp.Ctx);
//!!!    QT = QT.getCanonicalType();
//    auto T = QT.getUnqualifiedType();

    auto* CRD = hlp.resolveCXXRecordDecl(QT);
    if (CRD && CRD->getQualifiedNameAsString().find("std::") == (size_t)0
        && CRD->getQualifiedNameAsString().find("std::function") == (size_t)-1) {
        return;  // we dont want any std type to be translated to SV
    }

    bool isMember = false;
    if (CRD && CRD->isDerivedFrom(ModuleClass)) {  // check if template is derived from cpphdl::Module
        isMember = true;
    }
    if (std::find_if(hlp.mod.members.begin(), hlp.mod.members.end(), [&](auto& elem){ return elem.name == fieldName; } ) != hlp.mod.members.end()) {  // we cant see if it's of Module in abstract decl
        isMember = true;
    }

    hlp.skipStdFunctionType(QT);

    cpphdl::Expr expr = hlp.digQT(QT);

    if (str_ending(fieldName, "_in") || str_ending(fieldName, "_out")) {
        DEBUG_AST1(" {port " << fieldName << "}");

        cpphdl::Field* field = 0;
        auto it = std::find_if(hlp.mod.ports.begin(), hlp.mod.ports.end(), [&](auto& elem){ return elem.name == fieldName; } );
        if (it != hlp.mod.ports.end()) {
            field = &*it;
            updateExpr(field->expr, expr);
        }
        else if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
            field = &hlp.mod.ports.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
        }
        else {
            return;
        }

        if (initializer) {
            DEBUG_AST1(", <initializer ");
            field->initializer = hlp.exprToExpr(initializer);
            DEBUG_AST1(">");
            expr.hasInitializer = true;
        }

        DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));
    }
    else {
        auto* CRD = hlp.resolveCXXRecordDecl(QT);

//        if (auto* TST = fieldType->getAs<clang::TemplateSpecializationType>()) {
//            clang::TemplateName TN = TST->getTemplateName();
//            if (auto* TD = TN.getAsTemplateDecl()) {
//                if (auto* CTD = llvm::dyn_cast<clang::ClassTemplateDecl>(TD)) {
//                    CRD = CTD->getTemplatedDecl();
//                }
//            }
//        }

        if (isMember /*|| (CRD && CRD->isDerivedFrom(ModuleClass))*/) {
            DEBUG_AST1(" {member " << fieldName << "}");

            if (expr.type == cpphdl::Expr::EXPR_TEMPLATE) {
                for (auto& param: expr.sub) {
                    if (param.type == cpphdl::Expr::EXPR_NUM) {
                        param.type = cpphdl::Expr::EXPR_PARAM;  // we want members to use expressions in parameters, not numbers
                    }
                }
            }

            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod.members.begin(), hlp.mod.members.end(), [&](auto& elem){ return elem.name == fieldName; } );
            if (it != hlp.mod.members.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            }
            else if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod.members.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));
        }
        else {
            DEBUG_AST1(" {var " << fieldName << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod.vars.begin(), hlp.mod.vars.end(), [&](auto& elem){ return elem.name == fieldName; } );
            if (it != hlp.mod.vars.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            }
            else if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod.vars.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));
            if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0
                    && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                auto st = exportStruct(CRD, hlp);
                auto ret = hlp.mod.imports.emplace(st.name);
                if (ret.second) {
                    currProject->structs.emplace_back(std::move(st));
                }
            }
        }
    }
}

std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp)
{
    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
    ASSERT(ModuleClass);

    const Stmt *Body = MD->getBody();
    if (!Body) {
        return "";
    }

    if (MD->getQualifiedNameAsString().find("::" + hlp.mod.origName) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::~" + hlp.mod.origName) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::operator=") != (size_t)-1
    || MD->getQualifiedNameAsString().find("cpphdl::") != (size_t)-1
    || MD->getQualifiedNameAsString().find("std::") == (size_t)0
    || (MD->getParent()->isDerivedFrom(ModuleClass) && hlp.mod.name.find(MD->getParent()->getQualifiedNameAsString())) != 0) {  // module's class method but not current module
        return "";
    }

    DEBUG_AST(debugIndent++, "# putMethod: " << MD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });
    cpphdl::Method method;
    if (MD->getReturnType().getAsString() == "void") {
        method = cpphdl::Method{MD->getNameAsString()};
    }
    else {
        method = cpphdl::Method{MD->getNameAsString(), {cpphdl::Expr{MD->getReturnType().getAsString(), cpphdl::Expr::EXPR_TYPE}}};
    }

    unsigned savedFlags = hlp.flags;
    if (hlp.mod.origName.find(MD->getParent()->getQualifiedNameAsString()) != 0) {  // method of structure, adding first parameter this
        // extracting parameters of the template
        std::vector<cpphdl::Field> params;
        if (auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(MD->getParent())) {
            const TemplateArgumentList& Args = CTSD->getTemplateArgs();
            const TemplateParameterList* Params = CTSD->getSpecializedTemplate()->getTemplateParameters();
            for (unsigned i = 0; i < Args.size(); ++i) {
                auto expr = hlp.ArgToExpr(Args[i], Params->getParam(i)->getNameAsString());
                hlp.genSpecializationTypeName(i == 0, method.name, expr);
            }
        }

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
//?        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(debugIndent, "Param: " << Param->getNameAsString() << " (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{Param->getNameAsString(), std::move(expr)});
        auto* CRD = hlp.resolveCXXRecordDecl(QT);
        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0
                && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
            auto st = exportStruct(CRD, hlp);
            auto ret = hlp.mod.imports.emplace(st.name);
            if (ret.second) {
                currProject->structs.emplace_back(std::move(st));
            }
        }
        DEBUG_EXPR(debugIndent, " Expr: " << method.parameters.back().expr.debug(debugIndent));
    }

    if (const auto *CS = dyn_cast<CompoundStmt>(MD->getBody())) {
        for (const Stmt *S : CS->body()) {
            method.statements.emplace_back(hlp.exprToExpr(S));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
        }
    }

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
                DEBUG_EXPR(debugIndent, " Expr: " << method.statements[i].debug(debugIndent));
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

    void putClass(CXXRecordDecl* RD, cpphdl::Module& mod)
    {
        DEBUG_AST(debugIndent++, "# putClass: " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });

        Helpers hlp(Context, &mod);

        for (Decl* D : RD->decls()) {
            if (auto* FD = dyn_cast<FieldDecl>(D)) {

                DEBUG_AST(debugIndent++, "# putField: "); on_return ret_debug([](){ --debugIndent; });

                std::string S;
                llvm::raw_string_ostream OS(S);
                FD->getType().print(OS, Context->getPrintingPolicy());
                DEBUG_AST1(std::string("\"")+ S + " " + FD->getNameAsString() + "\"");

                if (const auto *TST = FD->getType()->getAs<TemplateSpecializationType>()) {  // std::tuple support
                    const TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
                    if (TD->getName() == "tuple") {
                        const auto *NS = dyn_cast<NamespaceDecl>(TD->getDeclContext());
                        DEBUG_AST1(std::string(" *tuple*"));
                        if (NS->getName() == "std" || NS->getName() == "__1") {
                            DEBUG_AST1(" *std*");
                            const auto &Args = TST->template_arguments();
                            size_t i = 0;
                            for (const auto &Arg : Args) {
                                if (Arg.getKind() == TemplateArgument::Type || Arg.getKind() == TemplateArgument::Template) {
                                    DEBUG_AST(debugIndent++, "% putField: "); on_return ret_debug([](){ --debugIndent; });
                                    std::string S;
                                    llvm::raw_string_ostream OS(S);
                                    Arg.getAsType().getNonReferenceType().print(OS, Context->getPrintingPolicy());
                                    DEBUG_AST1(std::string("\"")+ S + "\"");
                                    putField(Arg.getAsType(), FD->getNameAsString() + "_tuple_" + std::to_string(i++), nullptr, hlp);
                                }
                            }
                            continue;
                        }
                    }
                }

                putField(FD->getType().getNonReferenceType(), FD->getNameAsString(), FD->getInClassInitializer(), hlp);
            } else
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember()) {
//                    DEBUG_AST1(" (var) " << VD->getNameAsString() << " : ");
//                    VD->getType().print(std::cout, RD->getASTContext().getPrintingPolicy());
//                    "\n";
                }
            } else
            if ([[maybe_unused]] auto* Nested = dyn_cast<CXXRecordDecl>(D)) {
//                DEBUG_AST(debugIndent, "Nested class: " << Nested->getNameAsString());
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
                DEBUG_AST(debugIndent, "Applying abstract: " << RD->getQualifiedNameAsString() << " to " << mod.name);  // get original parameters substitution form abstract, need only for numbers

                for (Decl* D : RD->decls()) {
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        hlp.flags |= Helpers::FLAG_ABSTRACT;
                        putField(FD->getType().getNonReferenceType(), FD->getNameAsString(), FD->getInClassInitializer(), hlp);
                        hlp.flags &= ~Helpers::FLAG_ABSTRACT;
                    }
// else  // no need to update code because of SubstNonTypeTemplateParmExpr replacements
//                    if (auto* VD = dyn_cast<VarDecl>(D)) {
//                        if (VD->isStaticDataMember()) {
////                            DEBUG_AST1(" (var) " << VD->getNameAsString() << " : ");
////                            VD->getType().print(std::cout, RD->getASTContext().getPrintingPolicy());
////                            "\n";
//                        }
//                    } else
//                    if ([[maybe_unused]] auto* Nested = dyn_cast<CXXRecordDecl>(D)) {
////                        DEBUG_AST(debugIndent, "Nested class: " << Nested->getNameAsString());
//                    } else
//                    if ([[maybe_unused]] auto* EnumD = dyn_cast<EnumDecl>(D)) {
//                        DEBUG_AST(debugIndent, "Enum: " << EnumD->getNameAsString());
//                    } else
//                    if ([[maybe_unused]] auto* TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
//                        DEBUG_AST(debugIndent, "Type alias: " << TypeAlias->getNameAsString());
//                    } else
//                    if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(D)) {  // Method
//                        putMethod(MD, hlp);
//                    }
                }
            }
        }
    }

    void putModule(CXXRecordDecl* RD)
    {
        if (RD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {  // we dont create modules for abstract classes
            abstractDefs.push_back(RD);

            for (const auto &Base : RD->bases()) {
                CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
                if (BaseRD && BaseRD->hasDefinition()) {
//                    if (BaseRD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(BaseRD)) {
                        abstractDefs.push_back(BaseRD);
//                    }
                }
            }
            return;
        }

        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        Helpers hlp(Context, &mod);

        std::vector<cpphdl::Field> params;
//        for (auto* RD : abstractDefs) {
//            if (mod.name.find(RD->getQualifiedNameAsString()) == 0) {
//                DEBUG_AST(debugIndent, "Applying abstract template: " << RD->getQualifiedNameAsString() << " to " << mod.name);  // get original parameters substitution form abstract, need only for numbers
                // specialization - we want to have numbers as parameters and types hard-coded in generated names
                if (auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
                    const TemplateArgumentList& Args = CTSD->getTemplateArgs();
                    const TemplateParameterList* Params = CTSD->getSpecializedTemplate()->getTemplateParameters();
                    for (unsigned i = 0; i < Args.size(); ++i) {
                        auto expr = hlp.ArgToExpr(Args[i], Params->getParam(i)->getNameAsString());
                        hlp.genSpecializationTypeName(i == 0, mod.name, expr, true);
                        params.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), std::move(expr)});
                    }
                }
//            }
//        }
        std::erase_if(params, [](cpphdl::Field& field) { return field.expr.type != cpphdl::Expr::EXPR_NUM; });
        mod.parameters = std::move(params);
        mod.origName = RD->getQualifiedNameAsString();

        DEBUG_AST(debugIndent++, "# putModule: " << RD->getQualifiedNameAsString() << "(" << mod.name << ")"); on_return ret_debug([](){ --debugIndent; });

        putClass(RD, mod);

        for (const auto &Base : RD->bases()) {
            CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
            if (BaseRD && BaseRD->hasDefinition()) {
                putClass(BaseRD, mod);
            }
        }

        currProject->modules.emplace_back(std::move(mod));
    }

    bool VisitCXXRecordDecl(CXXRecordDecl* RD)
    {
        Helpers hlp(Context, nullptr);
        if (auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module")) {
            if (RD && RD->hasDefinition() && RD->isDerivedFrom(ModuleClass)) {
                putModule(RD);
            }
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

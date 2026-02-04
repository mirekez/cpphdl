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

void updateExpr(cpphdl::Expr& expr1, const cpphdl::Expr& expr2)  // add correspondent abstract expressions to number parameters
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

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp, cpphdl::Struct* st = nullptr)
{
    DEBUG_AST(debugIndent++, "@ exportStruct " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });
    cpphdl::Struct st_obj = {};
    if (!st) {
        std::string sname = genTypeName(RD->getQualifiedNameAsString());

        // extracting parameters of the template if we see it as template
        hlp.followSpecialization(RD, sname);

        st = &st_obj;
        st->name = sname;
        st->type = (RD->isUnion() ? cpphdl::Struct::STRUCT_UNION : cpphdl::Struct::STRUCT_STRUCT);
        st->origName = RD->getQualifiedNameAsString();
        DEBUG_AST(debugIndent, "@ => " << RD->getQualifiedNameAsString() << " (" + sname + "):");
    }

    bool hasBases = false;
    for (const auto &Base : RD->bases()) {
        CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
        if (BaseRD && BaseRD->hasDefinition()) {  // need to check correct order of struct fields here
            hasBases = true;
            exportStruct(BaseRD, hlp, st);
        }
    }

    bool hasDecls = false;
    for (Decl* D : RD->decls()) {
        if (auto* FD = dyn_cast<FieldDecl>(D)) {
            hasDecls = true;
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
            while (const clang::ArrayType* AT = hlp.ctx->getAsArrayType(QT)) {  // comment this out
                if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
                    DEBUG_AST1(" [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
                    arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_NUM});
                    arrayExpr.value = "c_array";
                } else
                if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
                    DEBUG_AST1(" [v_array");
                    arrayExpr.sub.push_back(hlp.exprToExpr(VAT->getSizeExpr()));
                    DEBUG_AST1("] ");
                    arrayExpr.value = "v_array";
                } else
                if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
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
                QT = QT.getDesugaredType(*hlp.ctx);
                std::string str = QT.getAsString(hlp.ctx->getPrintingPolicy());
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
                QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?                QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                auto* CRD = hlp.resolveCXXRecordDecl(QT);
                DEBUG_AST1(" {var " << FD->getNameAsString() << "} " << (CRD && CRD->isAnonymousStructOrUnion()?"ANON":""));
                st->fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)/*, std::move(params)*/});
                if (FD->isBitField()) {
                    DEBUG_AST1(" |bitfield ");
                    Expr::EvalResult ER;
                    if (FD->getBitWidth()->EvaluateAsInt(ER, *hlp.ctx)) {
                        st->fields.back().bitwidth = cpphdl::Expr{std::to_string((size_t)ER.Val.getInt().getZExtValue()), cpphdl::Expr::EXPR_NUM};
                    } else {
                        st->fields.back().bitwidth = hlp.exprToExpr(FD->getBitWidth());
                    }
                    DEBUG_AST1("|");
                }
//                DEBUG_EXPR(debugIndent, " Expr: " << st->fields.back().type.debug(debugIndent));
                if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                    auto st1 = exportStruct(CRD, hlp);

                    if (CRD->isAnonymousStructOrUnion() || !CRD->getIdentifier()) {
                        st1.name = FD->getNameAsString();
                        st->fields.back().definition = std::move(st1);
                    }
                    else {
                        auto ret = hlp.mod->imports.emplace(st1.name);
                        if (ret.second) {
                            currProject->structs.emplace_back(std::move(st1));
                        }
                    }
                }

                DEBUG_EXPR(debugIndent, " Expr: " << st->fields.back().expr.debug(debugIndent));
                if (FD->isBitField()) {
                    DEBUG_EXPR(debugIndent, " Expr(bitfield): " << st->fields.back().bitwidth.debug(debugIndent));
                }
            }
        }

        if (VarDecl* VD = dyn_cast<VarDecl>(D)) {  // constexprs from structs
            if (VD->isStaticDataMember() && VD->isConstexpr() && VD->getInit()) {
                DEBUG_AST(debugIndent, "Const(" << VD->getNameAsString() << "): ");
                st->parameters.emplace_back(cpphdl::Field{VD->getNameAsString(), hlp.exprToExpr(VD->getInit())});
                DEBUG_EXPR(debugIndent, " Expr: " << st->parameters.back().expr.debug(debugIndent));
            }
        }
    }

    if (!(!hasDecls && hasBases)) {  // if no decls but has bases then it's already aligned
        // adding align marker for two purposes: 1) align structures to 8 bits as in C, 2) put 8 bit placeholder to empty structures
        st->fields.emplace_back(cpphdl::Field{std::string("_align") + std::to_string(st->alignNo), {cpphdl::Expr{"uint8_t", cpphdl::Expr::EXPR_TYPE}}});
        st->fields.back().bitwidth = cpphdl::Expr{"0",cpphdl::Expr::EXPR_NUM};
        ++st->alignNo;
    }

    return *st;
}

void putField(QualType fieldType, std::string fieldName, const Expr* initializer, Helpers& hlp)
{
    DEBUG_AST(debugIndent++, "# putField: "); on_return ret_debug([](){ --debugIndent; });
    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
    ASSERT(ModuleClass && ModuleClass->getDefinition());

    QualType QT = fieldType;

//    bool pointer = false;
    if (QT->isPointerType()) {  // while?
        QT = QT->getPointeeType();
//        pointer = true;
        DEBUG_AST1(" *pointer*");
    }

    QT = QT.getDesugaredType(*hlp.ctx);
//!!!    QT = QT.getCanonicalType();
//    auto T = QT.getUnqualifiedType();

    auto* CRD = hlp.resolveCXXRecordDecl(QT);
    if ((CRD && CRD->getQualifiedNameAsString().find("std::") == (size_t)0
        && CRD->getQualifiedNameAsString().find("std::function") == (size_t)-1
        && CRD->getQualifiedNameAsString().find("cpphdl_function_ref") == (size_t)-1)
        || (CRD && !CRD->getDefinition())) {
        return;  // we dont want any std type to be translated to SV
    }

    bool isMember = false;
    if (CRD && CRD->isDerivedFrom(ModuleClass)) {  // check if template is derived from cpphdl::Module
        isMember = true;
    }
    if (std::find_if(hlp.mod->members.begin(), hlp.mod->members.end(), [&](auto& m){ return m.name == fieldName; } ) != hlp.mod->members.end()) {  // we cant see if it's of Module in abstract decl
        isMember = true;
    }

    hlp.skipStdFunctionType(QT);

    cpphdl::Expr expr = hlp.digQT(QT);

    if (str_ending(fieldName, "_in") || str_ending(fieldName, "_out")) {
        DEBUG_AST1(" {port " << fieldName << "}");

        cpphdl::Field* field = 0;
        auto it = std::find_if(hlp.mod->ports.begin(), hlp.mod->ports.end(), [&](auto& p){ return p.name == fieldName; } );
        if (it != hlp.mod->ports.end()) {
            field = &*it;
            updateExpr(field->expr, expr);
        } else
        if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr (above)
            field = &hlp.mod->ports.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
        }
        else {
            return;
        }

        DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));

        if (initializer) {
            DEBUG_AST1(", <initializer ");
            field->initializer = hlp.exprToExpr(initializer);
            DEBUG_EXPR(debugIndent, " Expr: " << field->initializer.debug(debugIndent));
            DEBUG_AST1(">");
        }

        auto* CRD = hlp.resolveCXXRecordDecl(QT);
        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
            auto st = exportStruct(CRD, hlp);
            auto ret = hlp.mod->imports.emplace(st.name);
            if (ret.second) {
                currProject->structs.emplace_back(std::move(st));
            }
        }
    }
    else {
        if (hlp.mod->origName.find(hlp.parent->getQualifiedNameAsString()) != 0) {  // add base class name, ports dont get this prefix
            fieldName = genTypeName(hlp.parent->getQualifiedNameAsString()) + "___" + fieldName;
        }

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
            auto it = std::find_if(hlp.mod->members.begin(), hlp.mod->members.end(), [&](auto& m){ return m.name == fieldName; } );
            if (it != hlp.mod->members.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            } else
            if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod->members.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));
        }
        else {
            DEBUG_AST1(" {var " << fieldName << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod->vars.begin(), hlp.mod->vars.end(), [&](auto& v){ return v.name == fieldName; } );
            if (it != hlp.mod->vars.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
            } else
            if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod->vars.emplace_back(cpphdl::Field{fieldName, std::move(expr)});
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));

            auto* CRD = hlp.resolveCXXRecordDecl(QT);
            if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                auto st = exportStruct(CRD, hlp);
                auto ret = hlp.mod->imports.emplace(st.name);
                if (ret.second) {
                    currProject->structs.emplace_back(std::move(st));
                }
            }
        }
    }
}

std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp, bool notThis = false)
{
    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
    ASSERT(ModuleClass);

    const Stmt *Body = MD->getBody();
    if (!Body) {
        return "";
    }

    if (MD->getNameAsString() == "_strobe") {  // never need them in SV
        return "";
    }

    if (MD->getQualifiedNameAsString().find("::" + hlp.parent->getNameAsString()) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::~" + hlp.parent->getNameAsString()) != (size_t)-1
    || MD->getQualifiedNameAsString().find("::operator=") != (size_t)-1
    || MD->getQualifiedNameAsString().find("cpphdl::") != (size_t)-1
    || MD->getQualifiedNameAsString().find("std::") == (size_t)0
    || MD->getQualifiedNameAsString().find("*(*)()") != (size_t)-1  // lambda
//    || (MD->getParent()->isDerivedFrom(ModuleClass) && hlp.mod->name.find(MD->getParent()->getQualifiedNameAsString())) != 0  // module's class method but not current module
    ) {
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
    // three types of methods:
    // - module object's methods
    // - base module object's methods
    // - external object's methods (require _this)
    if (hlp.mod->origName.find(MD->getParent()->getQualifiedNameAsString()) != 0) {  // method of base class or external object (not current mod)
        method.name = genTypeName(MD->getParent()->getQualifiedNameAsString()) + "___";
        if (notThis || (hlp.flags&Helpers::FLAG_EXTERNAL_THIS)) {  // method called for var - need specialization
            // extracting parameters of the template
            std::vector<cpphdl::Field> params;
            hlp.followSpecialization(MD->getParent(), method.name, &params);
            DEBUG_AST1(" - not Module method: (" << hlp.mod->name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
            hlp.flags |= Helpers::FLAG_EXTERNAL_THIS;

            QualType QT = MD->getThisType()->getPointeeType();
            cpphdl::Expr expr = hlp.digQT(QT);
            QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?            QT = QT.getCanonicalType();        // ensure you have the actual canonical form
            DEBUG_AST(debugIndent, "Param this (" << QT.getAsString() << ")");
            method.parameters.emplace_back(cpphdl::Field{"_this", std::move(expr)});
            DEBUG_EXPR(debugIndent, " param Expr: " << method.parameters.back().expr.debug(debugIndent));
        }
        else {
            DEBUG_AST1(" - base Module method: (" << hlp.mod->name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
        }
//        method.name += "_";
        method.name += MD->getNameAsString();
    }

    for (const ParmVarDecl* param : MD->parameters()) {
        QualType QT = param->getType().getNonReferenceType();

        cpphdl::Expr expr = hlp.digQT(QT);

        QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?        QT = QT.getCanonicalType();        // ensure you have the actual canonical form
        DEBUG_AST(debugIndent, "Param: " << param->getNameAsString() << " (" << QT.getAsString() << ")");

        method.parameters.emplace_back(cpphdl::Field{param->getNameAsString(), std::move(expr)});
//        auto* CRD = hlp.resolveCXXRecordDecl(QT);
//        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
//            auto st = exportStruct(CRD, hlp);
//            auto ret = hlp.mod->imports.emplace(st.name);
//            if (ret.second) {
//                currProject->structs.emplace_back(std::move(st));
//            }
//        }
        DEBUG_EXPR(debugIndent, "param Expr: " << method.parameters.back().expr.debug(debugIndent));
    }

    if (const auto *CS = dyn_cast<CompoundStmt>(MD->getBody())) {
        for (const Stmt *S : CS->body()) {
            method.statements.emplace_back(hlp.exprToExpr(S));
            DEBUG_EXPR(debugIndent, " Expr: " << method.statements.back().debug(debugIndent));
        }
    }

//    if (MD->getNameAsString() != hlp.mod->name
////     && MD->getNameAsString() != std::string("~") + hlp.mod->name
//     && MD->getNameAsString().find("operator") != 0) {
    std::string ret = method.name;

    auto it = std::find_if(hlp.mod->methods.begin(), hlp.mod->methods.end(), [&](auto& m){ return m.name == method.name; } );
    if (it != hlp.mod->methods.end()) {
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
        hlp.mod->methods.emplace_back(std::move(method));
    }
//    }
    hlp.flags = savedFlags;
    return ret;
}

struct MethodVisitor : public RecursiveASTVisitor<MethodVisitor>
{
    explicit MethodVisitor(ASTContext* context)
        : context(context)/*, SM(context->getSourceManager())*/ {}

    void putClass(const CXXRecordDecl* RD, cpphdl::Module& mod)
    {
        DEBUG_AST(debugIndent++, "# putClass: " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });

        Helpers hlp(context, &mod, RD);

        for (Decl* D : RD->decls()) {
            if (auto* FD = dyn_cast<FieldDecl>(D)) {

                DEBUG_AST(debugIndent++, "# putField: "); on_return ret_debug([](){ --debugIndent; });

                std::string S;
                llvm::raw_string_ostream OS(S);
                FD->getType().print(OS, context->getPrintingPolicy());
                DEBUG_AST1(std::string("\"")+ S + " " + FD->getNameAsString() + "\"");

                // if field is std::tuple - iterate over them
                if (const auto *TST = FD->getType()->getAs<TemplateSpecializationType>()) {  // std::tuple support
                    const TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
                    if (TD->getName() == "tuple") {
                        const auto *NS = dyn_cast<NamespaceDecl>(TD->getDeclContext());
                        DEBUG_AST1(std::string(" *tuple*"));
                        if (NS->getName() == "std" || NS->getName() == "__1") {
                            DEBUG_AST1(" *std*");
                            const auto &args = TST->template_arguments();
                            size_t i = 0;
                            for (const auto &arg : args) {
                                if (arg.getKind() == TemplateArgument::Type || arg.getKind() == TemplateArgument::Template) {
                                    DEBUG_AST(debugIndent++, "% putField: "); on_return ret_debug([](){ --debugIndent; });
                                    std::string S;
                                    llvm::raw_string_ostream OS(S);
                                    arg.getAsType().getNonReferenceType().print(OS, context->getPrintingPolicy());
                                    DEBUG_AST1(std::string("\"")+ S + "\"");
                                    putField(arg.getAsType(), FD->getNameAsString() + "_tuple_" + std::to_string(i++), nullptr, hlp);
                                }
                            }
                            continue;
                        }
                    }
                }

                putField(FD->getType().getNonReferenceType(), FD->getNameAsString(), FD->getInClassInitializer(), hlp);
            } else
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (VD->isStaticDataMember() && VD->isConstexpr()) {
                    if (VD->getInit()) {
                        auto it = std::find_if(hlp.mod->consts.begin(), hlp.mod->consts.end(), [&](auto& c){ return c.name == VD->getNameAsString(); } );
                        if (it != hlp.mod->consts.end()) {
                            updateExpr((*it).expr, hlp.exprToExpr(VD->getInit()));
                        }
                        else {
                            hlp.mod->consts.emplace_back(cpphdl::Field{VD->getNameAsString(), hlp.exprToExpr(VD->getInit())});
                            DEBUG_AST(debugIndent, "constexpr: " << VD->getNameAsString() << "\n");
                        }
                    }
                    continue;
                }

                DEBUG_AST(debugIndent++, "# putFieldStatic: "); on_return ret_debug([](){ --debugIndent; });

                std::string S;
                llvm::raw_string_ostream OS(S);
                VD->getType().print(OS, context->getPrintingPolicy());
                DEBUG_AST1(std::string("\"")+ S + " " + VD->getNameAsString() + "\"");

                // if field is std::tuple - iterate over them
                if (const auto *TST = VD->getType()->getAs<TemplateSpecializationType>()) {  // std::tuple support
                    const TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
                    if (TD->getName() == "tuple") {
                        const auto *NS = dyn_cast<NamespaceDecl>(TD->getDeclContext());
                        DEBUG_AST1(std::string(" *tuple*"));
                        if (NS->getName() == "std" || NS->getName() == "__1") {
                            DEBUG_AST1(" *std*");
                            const auto &args = TST->template_arguments();
                            size_t i = 0;
                            for (const auto &arg : args) {
                                if (arg.getKind() == TemplateArgument::Type || arg.getKind() == TemplateArgument::Template) {
                                    DEBUG_AST(debugIndent++, "% putField: "); on_return ret_debug([](){ --debugIndent; });
                                    std::string S;
                                    llvm::raw_string_ostream OS(S);
                                    arg.getAsType().getNonReferenceType().print(OS, context->getPrintingPolicy());
                                    DEBUG_AST1(std::string("\"")+ S + "\"");
                                    putField(arg.getAsType(), VD->getNameAsString() + "_tuple_" + std::to_string(i++), nullptr, hlp);
                                }
                            }
                            continue;
                        }
                    }
                }

                putField(VD->getType().getNonReferenceType(), VD->getNameAsString(), VD->getInit(), hlp);
            } else
            if ([[maybe_unused]] auto* Nested = dyn_cast<CXXRecordDecl>(D)) {
//                DEBUG_AST(debugIndent, "Nested class: " << Nested->getNameAsString());
            } else
            if ([[maybe_unused]] auto* EnumD = dyn_cast<EnumDecl>(D)) {
                DEBUG_AST(debugIndent, "Enum: " << EnumD->getNameAsString());
            } else
            if ([[maybe_unused]] auto* TypeAlias = dyn_cast<TypeAliasDecl>(D)) {
                DEBUG_AST(debugIndent, "Type alias: " << TypeAlias->getNameAsString());
            }// else
//            if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(D)) {  // Method
//                putMethod(MD, hlp);
//            }
        }

        // when all vars are already in
        for (const CXXMethodDecl *MD : RD->methods()) {
            putMethod(MD, hlp);
        }

        for (auto* aRD : abstractDefs) {  // we take from abstract: 1) initializers for members, 2) template numeric parameters names, 3) numeric template parameter expressions in all code
            if (/*hlp.mod->name*/RD->getQualifiedNameAsString().find(aRD->getQualifiedNameAsString()) == 0) {
                DEBUG_AST(debugIndent, "*** Applying abstract: " << aRD->getQualifiedNameAsString() << " to " << hlp.mod->name);  // get original parameters substitution form abstract, need only for numbers

                for (Decl* D : aRD->decls()) {  // need fields from abstract class to get its port width parametrict expressions (not numbers)
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        hlp.flags |= Helpers::FLAG_ABSTRACT;
                        putField(FD->getType().getNonReferenceType(), FD->getNameAsString(), FD->getInClassInitializer(), hlp);
                        hlp.flags &= ~Helpers::FLAG_ABSTRACT;
                    } else
                    if (auto* VD = dyn_cast<VarDecl>(D)) {
                        if (VD->isStaticDataMember() && !VD->isConstexpr()) {
                            hlp.flags |= Helpers::FLAG_ABSTRACT;
                            putField(VD->getType().getNonReferenceType(), VD->getNameAsString(), VD->getInit(), hlp);
                            hlp.flags &= ~Helpers::FLAG_ABSTRACT;
                        }
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

    void putModule(const CXXRecordDecl* RD)
    {
        if (/*RD->getDescribedClassTemplate() &&*/ !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {  // we dont create modules for abstract classes
            DEBUG_AST(debugIndent++, "# putAbstract: " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });
            abstractDefs.push_back(RD);

//            for (const auto &Base : RD->bases()) {
//                CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
//                if (BaseRD && BaseRD->hasDefinition()) {
////                    if (BaseRD->getDescribedClassTemplate() && !dyn_cast<ClassTemplateSpecializationDecl>(BaseRD)) {
//                        abstractDefs.push_back(BaseRD);
////                    }
//                }
//            }
            if (RD->getDescribedClassTemplate()) {  // it's template, dont use abstract in putClass
                return;
            }
        }


        cpphdl::Module mod{RD->getQualifiedNameAsString()};

        Helpers hlp(context, &mod, RD);

        std::vector<cpphdl::Field> params;
        hlp.followSpecialization(RD, mod.name, &params, true);
        std::erase_if(params, [](cpphdl::Field& field) { return field.expr.type != cpphdl::Expr::EXPR_NUM; });  // dont use numeric parameters in modules names
        mod.parameters = std::move(params);
        mod.origName = RD->getQualifiedNameAsString();

        DEBUG_AST(debugIndent++, "# putModule: " << RD->getQualifiedNameAsString() << "(" << mod.name << ")"); on_return ret_debug([](){ --debugIndent; });

        hlp.forEachBase(RD, [&](const CXXRecordDecl* RD1){
                putClass(RD1, mod);
            });

        currProject->modules.emplace_back(std::move(mod));
    }

    bool VisitCXXRecordDecl(CXXRecordDecl* RD)
    {
        Helpers hlp(context);

        CXXRecordDecl* moduleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
        if (!moduleClass || !moduleClass->getDefinition()) {
            std::cerr << "ERROR: can't find cpphdl Module class definition\n";
            return false;
        }
        if (!RD->getDefinition()) {
            return true;
        }
        const CXXRecordDecl* def = RD->getDefinition();
        bool isModule = false;
        hlp.forEachBase(def, [&](const CXXRecordDecl* RD) {
//                RD = RD->getCanonicalDecl();
                if (RD->getQualifiedNameAsString() == moduleClass->getQualifiedNameAsString()) {
                    isModule = true;
                }
            });

        if (isModule) {
            putModule(def);
        }

        return true;
    }

    bool shouldVisitTemplateInstantiations() const { return true; }

    ASTContext* context;
//    const SourceManager &SM;
    std::vector<const CXXRecordDecl*> abstractDefs;
};

struct MethodConsumer : public ASTConsumer
{
    explicit MethodConsumer(ASTContext* context) : Visitor(context) {}

    void HandleTranslationUnit(ASTContext &context) override
    {
        Visitor.TraverseDecl(context.getTranslationUnitDecl());
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

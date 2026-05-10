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
#include "Struct.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>

using namespace clang;

namespace
{

std::string stripAnnotationValue(std::string text, std::string_view prefix)
{
    text.erase(0, prefix.size());
    size_t end = text.find_last_not_of(" \t\r\n");
    if (end != std::string::npos && text[end] == ';') {
        text.erase(end);
    }
    return text;
}

std::string shellQuote(const std::filesystem::path& path)
{
    std::string quoted = "'";
    for (char ch : path.string()) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::filesystem::path resolveAnnotationPath(const std::string& annotation_path, const CXXRecordDecl* RD, const ASTContext& ctx)
{
    namespace fs = std::filesystem;

    fs::path path(annotation_path);
    if (path.is_absolute() || fs::exists(path)) {
        return path;
    }

    const SourceManager& sm = ctx.getSourceManager();
    std::string source = sm.getFilename(sm.getSpellingLoc(RD->getLocation())).str();
    if (!source.empty()) {
        fs::path from_source = fs::path(source).parent_path() / path;
        if (fs::exists(from_source)) {
            return from_source;
        }
    }

    return path;
}

std::string scriptCommandFromAnnotation(const std::string& annotation_command, const CXXRecordDecl* RD, const ASTContext& ctx)
{
    size_t begin = annotation_command.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    size_t end = annotation_command.find_first_of(" \t\r\n", begin);
    std::string script = annotation_command.substr(begin, end - begin);
    std::string args = end == std::string::npos ? std::string{} : annotation_command.substr(end);

    return shellQuote(resolveAnnotationPath(script, RD, ctx)) + args;
}

std::string readReplacementFromFile(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) {
        std::cerr << "ERROR: failed to open CPPHDL_REPLACEMENT_FILE '" << file.string() << "'\n";
        return {};
    }

    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string readReplacementFromScript(const std::string& command)
{
    std::array<char, 4096> buffer{};
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "ERROR: failed to run CPPHDL_REPLACEMENT_SCRIPT '" << command << "'\n";
        return {};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }

    int status = pclose(pipe);
    if (status != 0) {
        std::cerr << "ERROR: CPPHDL_REPLACEMENT_SCRIPT '" << command << "' failed with status " << status << "\n";
        return {};
    }

    return result;
}

std::string cpphdlReplacementFromAnnotations(const CXXRecordDecl* RD, const ASTContext& ctx)
{
    constexpr std::string_view replacement_prefix = "CPPHDL_REPLACEMENT=";
    constexpr std::string_view script_prefix = "CPPHDL_REPLACEMENT_SCRIPT=";
    constexpr std::string_view file_prefix = "CPPHDL_REPLACEMENT_FILE=";

    for (const Attr* attr : RD->attrs()) {
        if (const auto* ann = dyn_cast<AnnotateAttr>(attr)) {
            std::string text = ann->getAnnotation().str();
            if (text.rfind(replacement_prefix, 0) == 0) {
                return stripAnnotationValue(std::move(text), replacement_prefix);
            }
            if (text.rfind(script_prefix, 0) == 0) {
                std::string script = stripAnnotationValue(std::move(text), script_prefix);
                return readReplacementFromScript(scriptCommandFromAnnotation(script, RD, ctx));
            }
            if (text.rfind(file_prefix, 0) == 0) {
                std::string file = stripAnnotationValue(std::move(text), file_prefix);
                return readReplacementFromFile(resolveAnnotationPath(file, RD, ctx));
            }
        }
    }
    return {};
}

}

void updateExpr(cpphdl::Expr& expr1, const cpphdl::Expr& expr2)  // add correspondent abstract expressions to number parameters
{
    auto isTemplateTypeParam = [](const cpphdl::Expr& expr) {
        if (expr.type != cpphdl::Expr::EXPR_TYPE || expr.value.empty()) {
            return false;
        }
        return std::all_of(expr.value.begin(), expr.value.end(), [](unsigned char ch) {
            return std::isupper(ch) || std::isdigit(ch) || ch == '_';
        });
    };

    if (isTemplateTypeParam(expr1) && !(expr2.type == cpphdl::Expr::EXPR_TYPE && expr2.value == expr1.value)) {
        expr1 = expr2;
        return;
    }

    if (expr1.type == expr2.type && expr1.value == expr2.value && expr1.sub.size() == expr2.sub.size()) {
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
            if (hlp.templateToExpr(QT, expr)) {
                DEBUG_AST1(" template) " << expr.value);
            }
            else {
                QT = QT.getCanonicalType();
                QT = QT.getDesugaredType(*hlp.ctx);
                std::string str = QT.getAsString(hlp.ctx->getPrintingPolicy());
                DEBUG_AST1(str << " type)");
                expr.value = genTypeName(str);
                expr.type = cpphdl::Expr::EXPR_TYPE;
            }

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
                    DEBUG_AST1(" |bitfield");
                    Expr::EvalResult ER;
                    if (FD->getBitWidth()->EvaluateAsInt(ER, *hlp.ctx)) {
                        st->fields.back().bitwidth = cpphdl::Expr{std::to_string((size_t)ER.Val.getInt().getZExtValue()), cpphdl::Expr::EXPR_NUM};
                    } else {
                        st->fields.back().bitwidth = hlp.exprToExpr(FD->getBitWidth());
                    }
                    DEBUG_AST1("|");
                }
//                DEBUG_EXPR(debugIndent, " Expr: " << st->fields.back().type.debug(debugIndent));
                // folded struct
                if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                    if (!CRD->isAnonymousStructOrUnion() && CRD->getIdentifier()) {  // add to imports
                        std::string name = genTypeName(CRD->getQualifiedNameAsString());
                        if (std::find_if(st->imports.begin(), st->imports.end(), [&](auto& imp){ return imp.name == name; }) == st->imports.end()) {
                            st->imports.emplace_back(name);
                        }
                    }
                    auto st1 = exportStruct(CRD, hlp);

                    if (CRD->isAnonymousStructOrUnion() || !CRD->getIdentifier()) {
                        st1.name = FD->getNameAsString();
                        st->fields.back().definition = std::move(st1);
                        DEBUG_AST1(" ANON");
                    }
                    else {
                        if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(), [&](auto& imp){ return imp.name == st1.name; }) == hlp.mod->imports.end()) {
                            hlp.mod->imports.emplace_back(st1.name);
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
                DEBUG_AST(debugIndent, " Const(" << VD->getNameAsString() << "): ");
                st->parameters.emplace_back(cpphdl::Field{VD->getNameAsString(), hlp.exprToExpr(VD->getInit())});
                DEBUG_EXPR(debugIndent, " Expr: " << st->parameters.back().expr.debug(debugIndent));
            }
        }
    }

    if (!(!hasDecls && hasBases) && !RD->isUnion()) {  // if no decls but has bases then it's already aligned, union cant be aligned
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
    auto* InterfaceClass = hlp.lookupQualifiedRecord("cpphdl::Interface");
    ASSERT(InterfaceClass && InterfaceClass->getDefinition());

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

    std::vector<cpphdl::Expr> array_dim;
    while (const ArrayType *AT = hlp.ctx->getAsArrayType(QT)) {
        CRD = hlp.resolveCXXRecordDecl(AT->getElementType());
        QT = AT->getElementType();
        if (const auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
            array_dim.emplace_back(cpphdl::Expr{std::to_string(CAT->getSize().getZExtValue()), cpphdl::Expr::EXPR_NUM});
        }
        if (const auto *DAT = dyn_cast<DependentSizedArrayType>(AT)) {
            array_dim.emplace_back(hlp.exprToExpr(DAT->getSizeExpr()));
        }
    }
    size_t cArrayDims = array_dim.size();
    size_t cppArrayDims = 0;
    while (true) {
        auto* TSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(hlp.resolveCXXRecordDecl(QT));
        if (!TSD || TSD->getSpecializedTemplate()->getQualifiedNameAsString() != "cpphdl::array"
            || TSD->getTemplateArgs().size() < 2) {
            break;
        }

        const TemplateArgument& typeArg = TSD->getTemplateArgs()[0];
        const TemplateArgument& sizeArg = TSD->getTemplateArgs()[1];
        if (typeArg.getKind() != TemplateArgument::Type) {
            break;
        }

        cpphdl::Expr sizeExpr;
        hlp.ArgToExpr(sizeArg, sizeExpr);
        if (!sizeExpr.sub.empty()) {
            array_dim.emplace_back(std::move(sizeExpr.sub.front()));
            ++cppArrayDims;
        }

        QT = typeArg.getAsType().getNonReferenceType();
        QT = QT.getDesugaredType(*hlp.ctx);
        CRD = hlp.resolveCXXRecordDecl(QT);
    }

    if ((CRD && CRD->getQualifiedNameAsString().find("std::") == (size_t)0
        && CRD->getQualifiedNameAsString().find("std_function") == (size_t)-1
        && CRD->getQualifiedNameAsString().find("function_ref") == (size_t)-1)
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

    const std::string qtText = QT.getAsString(hlp.ctx->getPrintingPolicy());
    const bool functionRefCpphdlArray = qtText.rfind("cpphdl::array<", 0) == 0 || qtText.rfind("array<", 0) == 0;

    cpphdl::Expr expr = hlp.digQT(QT);
    const bool packedArray = functionRefCpphdlArray || cppArrayDims != 0;
    size_t packedArrayDims = cppArrayDims;
    if (functionRefCpphdlArray && packedArrayDims == 0) {
        packedArrayDims = array_dim.size() - cArrayDims;
        if (packedArrayDims == 0) {
            packedArrayDims = array_dim.size();
        }
    }

    if (str_ending(fieldName, "_in") || str_ending(fieldName, "_out")) {
        DEBUG_AST1(" {port " << fieldName << "} ");

        auto* CRD = hlp.resolveCXXRecordDecl(QT);

        cpphdl::Field* field = 0;
        auto it = std::find_if(hlp.mod->ports.begin(), hlp.mod->ports.end(), [&](auto& p){ return p.name == fieldName; } );
        if (it != hlp.mod->ports.end()) {  // field already exists
            field = &*it;
            updateExpr(field->expr, expr);
            for (size_t i=0; i < array_dim.size(); ++i) {
                if (field->array.size() > i) {
                    updateExpr(field->array[i], array_dim[i]);
                }
            }
        } else
        if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // specialization (we start from it and then update just some info from abstract)
            if (!CRD || !CRD->isDerivedFrom(InterfaceClass)) {
                field = &hlp.mod->ports.emplace_back(cpphdl::Field{fieldName, std::move(expr), {}, array_dim});  // normal field, not Interface
                field->packedArray = packedArray;
                field->packedArrayDims = packedArrayDims;
            }
        }
        else {  // looks like it's Abstract for the Module, need update all Abstract information about parameters to Interface struct and array size expression
            auto it = std::find_if(hlp.mod->ports.begin(), hlp.mod->ports.end(), [&](auto& p){ return p.name.find(fieldName + "__") == 0; } );
            DEBUG_EXPR1(" ExprInterface: " << expr.debug(debugIndent) << ", " << " array " << (array_dim.size()?array_dim[0].debug(debugIndent):"0"));
            while (it != hlp.mod->ports.end() && it->name.find(fieldName + "__") == 0) {
                field = &*it;
                DEBUG_EXPR(debugIndent, " updating " << field->name << " " << array_dim.size() << "/" << field->array.size() << "... ");
                for (size_t i=0; i < array_dim.size(); ++i) {
                    if (field->array.size() > i) {
                        updateExpr(field->array[i], array_dim[i]);
                    }
                }
                const clang::TemplateSpecializationType *TST = QT->getAs<clang::TemplateSpecializationType>();
                if (TST && TST->getTemplateName().getAsTemplateDecl()) {
                    const clang::TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
                    const clang::TemplateParameterList *params = TD->getTemplateParameters();  // getting port names
                    size_t i=0;
                    for (const clang::NamedDecl *param : *params) {  // looking for parameters of Interface struct used in SubFields
                        DEBUG_EXPR1(" checking param " << param->getNameAsString() << " ");
                        field->expr.traverseIf( [&](auto& e) {
                                if (e.type == cpphdl::Expr::EXPR_VAR && e.value == param->getNameAsString() && expr.sub.size() > i) {
                                    e = expr.sub[i];  // get parameter expression from Interface parameters if one of parameters names is used in fields of the Interface
                                }
                                return false;
                            });
                        ++i;
                    }
                }
                DEBUG_AST(debugIndent, "SubField: " << field->name << ": " << field->expr.debug(debugIndent)
                    << " array " << (field->array.size()?field->array[0].debug(debugIndent):""));
                ++it;
            };
            return;
        }

        if (field) {  // normal field, not Interface struct
            if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {
                field->packedArray = field->packedArray || packedArray;
                field->packedArrayDims = std::max(field->packedArrayDims, packedArrayDims);
            }
            DEBUG_EXPR1(" Expr: " << field->expr.debug(debugIndent));

            if (initializer) {
                DEBUG_AST1(", <initializer ");
                field->initializer = hlp.exprToExpr(initializer);
                DEBUG_EXPR(debugIndent, " Expr: " << field->initializer.debug(debugIndent));
                DEBUG_AST1(">");
            }
        }

        if (CRD && CRD->isDerivedFrom(InterfaceClass)) {  // Interface struct
            // for Interface structs we need Abstract declaration to know expressions of template parameters in subfields (and port cames too)
            ClassTemplateDecl *CTD = dyn_cast<ClassTemplateSpecializationDecl>(CRD)->getSpecializedTemplate();
            CRD = CTD->getTemplatedDecl();
            for (Decl* D : CRD->decls()) {
                if (auto* FD = dyn_cast<FieldDecl>(D)) {
                    QualType QT = FD->getType().getNonReferenceType();
                    hlp.skipStdFunctionType(QT);
                    cpphdl::Expr exprSub = hlp.digQT(QT);
                    std::string ending = FD->getNameAsString();
                    if (str_ending(fieldName, "_out")) {
                        if (str_ending(ending, "_out")) {
                            ending.replace(ending.length() - 4, 4, "_in");
                        } else
                        if (str_ending(ending, "_in")) {
                            ending.replace(ending.length() - 3, 3, "_out");
                        }
                    }
                    if (std::find_if(hlp.mod->ports.begin(), hlp.mod->ports.end(), [&](auto& p){ return p.name == fieldName + "__" + ending; } ) == hlp.mod->ports.end()) {
                        field = &hlp.mod->ports.emplace_back(cpphdl::Field{fieldName + "__" + ending, std::move(exprSub), {}, array_dim});
                        DEBUG_AST(debugIndent, "SubField: " << FD->getNameAsString() << "(" << field->name << ")" << ": " << exprSub.debug(debugIndent)
                            << " array " << (array_dim.size()?array_dim[0].debug(debugIndent):""));
                        // only specialized here
                    }
                }
            }
        }
        else
        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
            auto st = exportStruct(CRD, hlp);
            if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
                hlp.mod->imports.emplace_back(st.name);
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
                for (size_t i=0; i < array_dim.size(); ++i) {
                    if (field->array.size() > i) {
                        updateExpr(field->array[i], array_dim[i]);
                    }
                }
            } else
            if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod->members.emplace_back(cpphdl::Field{fieldName, std::move(expr), {}, array_dim});
                field->packedArray = packedArray;
                field->packedArrayDims = packedArrayDims;
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));
            if (field) {
                if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {
                    field->packedArray = field->packedArray || packedArray;
                    field->packedArrayDims = std::max(field->packedArrayDims, packedArrayDims);
                }
            }
        }
        else {
            DEBUG_AST1(" {var " << fieldName << "}");
            cpphdl::Field* field = 0;
            auto it = std::find_if(hlp.mod->vars.begin(), hlp.mod->vars.end(), [&](auto& v){ return v.name == fieldName; } );
            if (it != hlp.mod->vars.end()) {
                field = &*it;
                updateExpr(field->expr, expr);
                for (size_t i=0; i < array_dim.size(); ++i) {
                    if (field->array.size() > i) {
                        updateExpr(field->array[i], array_dim[i]);
                    }
                }
            } else
            if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {  // dont need abstract declarations, only updateExpr
                field = &hlp.mod->vars.emplace_back(cpphdl::Field{fieldName, std::move(expr), {}, array_dim});
                field->packedArray = packedArray;
                field->packedArrayDims = packedArrayDims;
            }
            else {
                return;
            }
            DEBUG_EXPR(debugIndent, " Expr: " << field->expr.debug(debugIndent));
            if (field) {
                if (!(hlp.flags&Helpers::FLAG_ABSTRACT)) {
                    field->packedArray = field->packedArray || packedArray;
                    field->packedArrayDims = std::max(field->packedArrayDims, packedArrayDims);
                }
            }

            auto* CRD = hlp.resolveCXXRecordDecl(QT);
            if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                auto st = exportStruct(CRD, hlp);
                if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
                    hlp.mod->imports.emplace_back(st.name);
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

    if (MD->getQualifiedNameAsString().find("::" + hlp.parent->getNameAsString()) != (size_t)-1  // constructor
    || MD->getQualifiedNameAsString().find("::~" + hlp.parent->getNameAsString()) != (size_t)-1  // destructor
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
        if ((notThis || (hlp.flags&Helpers::FLAG_EXTERNAL_THIS))) {  // method called for var - need specialization
            if (MD->getNameAsString() == "_work" || MD->getNameAsString() == "_assign") {
                return "";  // we need not work functions from third party classes (not Modules)
            }

            // extracting parameters of the template
            std::vector<cpphdl::Field> params;
            hlp.followSpecialization(MD->getParent(), method.name, &params);
            DEBUG_AST1(" - not Module method: (" << hlp.mod->name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
            hlp.flags |= Helpers::FLAG_EXTERNAL_THIS;

            QualType QT = MD->getThisType()->getPointeeType();

            auto* CRD = hlp.resolveCXXRecordDecl(QT);  // this of method
            if (CRD) {
                auto st = exportStruct(CRD, hlp);
                if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
                    hlp.mod->imports.emplace_back(st.name);
                    currProject->structs.emplace_back(std::move(st));
                }
            }

            cpphdl::Expr expr = hlp.digQT(QT);
            QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?            QT = QT.getCanonicalType();        // ensure you have the actual canonical form
            DEBUG_AST(debugIndent, "Param this (" << QT.getAsString() << ")");
            method.arguments.emplace_back(cpphdl::Field{"_this", std::move(expr)});
            DEBUG_EXPR(debugIndent, " param Expr: " << method.arguments.back().expr.debug(debugIndent));
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

        method.arguments.emplace_back(cpphdl::Field{(param->getNameAsString().length() ? param->getNameAsString() : "unused"), std::move(expr)});
//        auto* CRD = hlp.resolveCXXRecordDecl(QT);
//        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
//            auto st = exportStruct(CRD, hlp);
//            auto ret = hlp.mod->imports.emplace_back(st.name);
//            if (ret.second) {
//                currProject->structs.emplace_back(std::move(st));
//            }
//        }
        DEBUG_EXPR(debugIndent, "param Expr: " << method.arguments.back().expr.debug(debugIndent));
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
/*not using it now
        if (it->ret.size() == method.ret.size()) {
            for (size_t i=0; i < it->ret.size(); ++i) {
                DEBUG_AST(debugIndent, "updating ret...");
                updateExpr(it->ret[i], method.ret[i]);
            }
        }
        if (it->arguments.size() == method.arguments.size()) {
            for (size_t i=0; i < it->arguments.size(); ++i) {
                DEBUG_AST(debugIndent, "updating arguments...");
                updateExpr(it->arguments[i].expr, method.arguments[i].expr);
                updateExpr(it->arguments[i].initializer, method.arguments[i].initializer);
                updateExpr(it->arguments[i].bitwidth, method.arguments[i].bitwidth);
            }
        }
        if (it->statements.size() == method.statements.size()) {
            for (size_t i=0; i < it->statements.size(); ++i) {
                DEBUG_AST(debugIndent, "updating statements...");
                updateExpr(it->statements[i], method.statements[i]);
                DEBUG_EXPR(debugIndent, " Expr: " << method.statements[i].debug(debugIndent));
            }
        }
*/
    }
    else {
        auto it = std::find_if(hlp.mod->methods.begin(), hlp.mod->methods.end(), [&](auto& m){ return m.name == method.name; });
        if (it != hlp.mod->methods.end()) {
            if (it->statements.size() == 0) {
                *it = std::move(method);
            }
        }
        else {
            hlp.mod->methods.emplace_back(std::move(method));
        }
    }

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

        for (Decl* D : RD->getDefinition()->decls()) {
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
                            updateExpr((*it).initializer, hlp.exprToExpr(VD->getInit()));
                        }
                        else {
                            hlp.mod->consts.emplace_back(cpphdl::Field{VD->getNameAsString(), cpphdl::Expr{"parameter", cpphdl::Expr::EXPR_CONST, {hlp.exprToExpr(VD->getInit())}}});
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
                DEBUG_AST(debugIndent, "Type alias: " << TypeAlias->getNameAsString() << ", " << TypeAlias->getUnderlyingType().getCanonicalType().getAsString(hlp.ctx->getPrintingPolicy()));
                if (find_if(mod.aliases.begin(), mod.aliases.end(), [&](auto& a){ return a.name == TypeAlias->getNameAsString(); }) == mod.aliases.end()) {
                    if (TypeAlias->getUnderlyingType().getCanonicalType().getAsString(hlp.ctx->getPrintingPolicy()).find("std::") != 0) {
                        mod.aliases.emplace_back(cpphdl::Field{TypeAlias->getNameAsString(),
                            cpphdl::Expr{genTypeName(TypeAlias->getUnderlyingType().getCanonicalType().getAsString(hlp.ctx->getPrintingPolicy())), cpphdl::Expr::EXPR_TYPE}});
                    }
                }
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
//                    if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(D)) {  // Method     // not used now
//                        putMethod(MD, hlp);
//                    }
                }
            }
        }
    }

    void putModule(const CXXRecordDecl* RD)
    {
        if (/*RD->getDescribedClassTemplate() &&*/ !dyn_cast<ClassTemplateSpecializationDecl>(RD)) {  // we dont create modules for abstract classes
            DEBUG_AST(debugIndent++, "# it's abstract, saving " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });
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
        if (RD->getQualifiedNameAsString() == "cpphdl::Module") {
            return;
        }

        cpphdl::Module* mod = nullptr;
        auto it = std::find_if(currProject->modules.begin(), currProject->modules.end(), [&](auto& mod){ return mod.name == RD->getQualifiedNameAsString(); });
        if (it == currProject->modules.end()) {
            mod = &currProject->modules.emplace_back(cpphdl::Module{RD->getQualifiedNameAsString()});
        }
        else {
            mod = &*it;
        }

        Helpers hlp(context, mod, RD);

        std::vector<cpphdl::Field> params;
        hlp.followSpecialization(RD, mod->name, &params, true);
        std::erase_if(params, [](cpphdl::Field& field) { return field.expr.type != cpphdl::Expr::EXPR_NUM; });  // dont use numeric parameters in modules names
        mod->parameters = std::move(params);
        mod->origName = RD->getQualifiedNameAsString();
        mod->replacement = cpphdlReplacementFromAnnotations(RD, *context);

        DEBUG_AST(debugIndent++, "# putModule: " << RD->getQualifiedNameAsString() << "(" << mod->name << ")"); on_return ret_debug([](){ --debugIndent; });

        hlp.forEachBase(RD, [&](const CXXRecordDecl* RD1){
                putClass(RD1, *mod);
            });
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
    std::vector<const char*> replace;
    bool saw_double_dash = false;
    bool injected_double_dash = false;

    if (argc > 0) {
        replace.push_back(argv[0]);
    }

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "--") == 0) {
            saw_double_dash = true;
            replace.push_back(arg);
            continue;
        }

        if (!saw_double_dash) {
            if (str_ending(arg, ".cpp") || str_ending(arg, ".h") || str_ending(arg, ".cc") || str_ending(arg, ".hpp")) {
                replace.push_back(arg);
            } else {
                if (!injected_double_dash) {
                    replace.push_back("--");
                    injected_double_dash = true;
                }
                replace.push_back(arg);
            }
        } else {
            replace.push_back(arg);
        }
    }

    std::vector<const char*> new_argv = replace;
    new_argv.push_back(nullptr);
    int new_argc = (int)new_argv.size() - 1;

    auto ExpectedParser = tooling::CommonOptionsParser::create(new_argc, (const char**)new_argv.data(), MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    tooling::CommonOptionsParser& Options = ExpectedParser.get();

    tooling::ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

    if (::getenv("CONDA_PREFIX")) {
        Tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster(
            {"-nostdinc",
             "-x", "c++",
             "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/include/c++/v1").str(),
             "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/lib/clang/21/include").str(),
             "-isystem", (llvm::Twine(::getenv("CONDA_PREFIX")) + "/x86_64-conda-linux-gnu/sysroot/usr/include").str(),
             "-std=c++26",
             "-DSYNTHESIS"},
            tooling::ArgumentInsertPosition::BEGIN));
    }
    else {
        Tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster(
            {"-x", "c++",
             "-std=c++26",
             "-DSYNTHESIS"},
            tooling::ArgumentInsertPosition::BEGIN));
    }


    int ret = Tool.run(tooling::newFrontendActionFactory<MyFrontendAction>().get());
    currProject->generate("generated");
    return ret;
}

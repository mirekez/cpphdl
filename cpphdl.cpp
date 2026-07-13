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
#include "Enum.h"
#include "json_output.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;

namespace
{

using AnnotationVars = std::unordered_map<std::string, std::string>;

static bool isCurrentOrBaseRecord(const CXXRecordDecl* current, const CXXRecordDecl* owner)
{
    if (!current || !owner) {
        return false;
    }

    const CXXRecordDecl* currentDef = current->getDefinition();
    const CXXRecordDecl* ownerDef = owner->getDefinition();
    current = currentDef ? currentDef : current;
    owner = ownerDef ? ownerDef : owner;

    if (current == owner ||
        current->getQualifiedNameAsString() == owner->getQualifiedNameAsString()) {
        return true;
    }

    return current->isDerivedFrom(owner);
}

bool evaluatedIntegerExpr(const clang::Expr* expr, ASTContext& ctx, cpphdl::Expr& out)
{
    if (!expr) {
        return false;
    }

    clang::Expr::EvalResult result;
    if (!expr->EvaluateAsInt(result, ctx)) {
        return false;
    }

    llvm::SmallString<32> str;
    const auto& value = result.Val.getInt();
    value.toString(str, 16, value.isSigned());
    std::string literal = "'h" + str.str().str();
    if (value.getBitWidth() > 32) {
        literal = std::to_string(value.getBitWidth()) + literal;
    }
    out = cpphdl::Expr{literal, cpphdl::Expr::EXPR_NUM};
    return true;
}

std::string stripAnnotationValue(std::string text, std::string_view prefix)
{
    text.erase(0, prefix.size());
    size_t end = text.find_last_not_of(" \t\r\n");
    if (end != std::string::npos && text[end] == ';') {
        text.erase(end);
    }
    return text;
}

std::string stripIntegerSuffixes(std::string text)
{
    while (!text.empty()) {
        if (text.size() >= 2 && text.ends_with("LL")) {
            text.resize(text.size() - 2);
        }
        else if (text.ends_with("L") || text.ends_with("U")) {
            text.resize(text.size() - 1);
        }
        else {
            break;
        }
    }
    return text;
}

std::string stripQuotedString(std::string text)
{
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"')
            || (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string printTemplateArgument(const TemplateArgument& arg, const ASTContext& ctx)
{
    std::string text;
    llvm::raw_string_ostream os(text);
    arg.print(ctx.getPrintingPolicy(), os, true);
    os.flush();
    return text;
}

std::string templateArgumentText(const TemplateArgument& arg, const ASTContext& ctx)
{
    switch (arg.getKind()) {
    case TemplateArgument::Integral:
        if (arg.getIntegralType()->isBooleanType()) {
            return arg.getAsIntegral().isZero() ? "false" : "true";
        }
        return stripIntegerSuffixes(printTemplateArgument(arg, ctx));

    case TemplateArgument::Expression: {
        Helpers hlp(const_cast<ASTContext*>(&ctx));
        cpphdl::Expr expr = hlp.exprToExpr(arg.getAsExpr());
        return stripQuotedString(expr.str());
    }

    case TemplateArgument::Declaration:
        if (const auto* VD = dyn_cast_or_null<VarDecl>(arg.getAsDecl())) {
            if (const clang::Expr* init = VD->getInit()) {
                init = init->IgnoreParenImpCasts();
                if (const auto* SL = dyn_cast<StringLiteral>(init)) {
                    return SL->getString().str();
                }
                Helpers hlp(const_cast<ASTContext*>(&ctx));
                cpphdl::Expr expr = hlp.exprToExpr(init);
                return stripQuotedString(expr.str());
            }
        }
        return printTemplateArgument(arg, ctx);

    case TemplateArgument::Type: {
        QualType QT = arg.getAsType().getNonReferenceType();
        std::string text;
        llvm::raw_string_ostream os(text);
        QT.print(os, ctx.getPrintingPolicy());
        os.flush();
        return text;
    }

    default:
        return printTemplateArgument(arg, ctx);
    }
}

AnnotationVars annotationTemplateVariables(const CXXRecordDecl* RD, const ASTContext& ctx)
{
    AnnotationVars vars;
    const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD || !CTSD->getSpecializedTemplate()) {
        return vars;
    }

    const TemplateArgumentList& args = CTSD->getTemplateArgs();
    const TemplateParameterList* params = CTSD->getSpecializedTemplate()->getTemplateParameters();
    unsigned count = std::min<unsigned>(args.size(), params->size());
    for (unsigned i = 0; i < count; ++i) {
        const NamedDecl* param = params->getParam(i);
        if (!param || param->getName().empty()) {
            continue;
        }
        if (args[i].getKind() == TemplateArgument::Pack) {
            continue;
        }
        vars.emplace(param->getNameAsString(), templateArgumentText(args[i], ctx));
    }
    return vars;
}

std::string replaceAnnotationVariables(const std::string& text, const AnnotationVars& vars)
{
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '$') {
            out += text[i];
            continue;
        }

        if (i + 1 < text.size() && text[i + 1] == '$') {
            out += '$';
            ++i;
            continue;
        }

        if (i + 1 < text.size() && text[i + 1] == '(') {
            size_t close = text.find(')', i + 2);
            if (close != std::string::npos) {
                std::string name = text.substr(i + 2, close - (i + 2));
                auto it = vars.find(name);
                if (it != vars.end()) {
                    out += it->second;
                }
                else {
                    out += text.substr(i, close - i + 1);
                }
                i = close;
                continue;
            }
        }

        out += text[i];
    }

    return out;
}

void appendDelimitedIncludeDirs(std::vector<std::string>& args, std::string_view dirs)
{
    size_t begin = 0;
    while (begin <= dirs.size()) {
        size_t end = dirs.find('|', begin);
        if (end == std::string_view::npos) {
            end = dirs.size();
        }
        std::string dir(dirs.substr(begin, end - begin));
        if (!dir.empty() && std::filesystem::exists(dir)) {
            args.push_back("-isystem");
            args.push_back(dir);
        }
        if (end == dirs.size()) {
            break;
        }
        begin = end + 1;
    }
}

std::string shellQuote(const std::string& text)
{
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        }
        else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

void appendCompilerProbeIncludeDirs(std::vector<std::string>& args)
{
    std::string compiler;
#ifdef CPPHDL_CMAKE_CXX_COMPILER
    compiler = CPPHDL_CMAKE_CXX_COMPILER;
#endif
    if (compiler.empty()) {
        if (const char* env_cxx = ::getenv("CXX")) {
            compiler = env_cxx;
        }
    }
    if (compiler.empty()) {
        compiler = "clang++";
    }

    const std::string command = "printf '' | " + shellQuote(compiler) + " -E -x c++ - -v 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) {
        return;
    }

    std::array<char, 4096> buffer{};
    bool in_include_list = false;
    while (std::fgets(buffer.data(), (int)buffer.size(), pipe)) {
        std::string line(buffer.data());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
            line.erase(0, first);
        }
        const size_t last = line.find_last_not_of(" \t");
        if (last != std::string::npos) {
            line.erase(last + 1);
        }
        if (line == "#include <...> search starts here:") {
            in_include_list = true;
            continue;
        }
        if (line == "End of search list.") {
            in_include_list = false;
            continue;
        }
        if (in_include_list && std::filesystem::exists(line)) {
            args.push_back("-isystem");
            args.push_back(line);
        }
    }
    ::pclose(pipe);
}

std::string trimText(std::string text)
{
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::vector<std::string> fieldInlineAnnotations(const FieldDecl* FD, const ASTContext& ctx)
{
    std::vector<std::string> annotations;
    if (!FD) {
        return annotations;
    }

    const SourceManager& sm = ctx.getSourceManager();
    SourceLocation loc = sm.getSpellingLoc(FD->getBeginLoc());
    if (!loc.isValid()) {
        return annotations;
    }

    bool invalid = false;
    llvm::StringRef buffer = sm.getBufferData(sm.getFileID(loc), &invalid);
    if (invalid) {
        return annotations;
    }

    const unsigned targetLine = sm.getSpellingLineNumber(loc);
    if (targetLine <= 1) {
        return annotations;
    }

    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin <= buffer.size()) {
        size_t end = buffer.find('\n', begin);
        if (end == llvm::StringRef::npos) {
            end = buffer.size();
        }
        lines.emplace_back(buffer.substr(begin, end - begin).str());
        if (end == buffer.size()) {
            break;
        }
        begin = end + 1;
    }

    for (int line = (int)targetLine - 2; line >= 0 && line < (int)lines.size(); --line) {
        std::string text = trimText(lines[(size_t)line]);
        if (text.rfind("//", 0) != 0) {
            break;
        }

        text = trimText(text.substr(2));
        if (text.rfind("(*", 0) != 0 || text.size() < 4 || text.find("*)") == std::string::npos) {
            break;
        }
        annotations.push_back(text);
    }

    std::reverse(annotations.begin(), annotations.end());
    return annotations;
}

void appendFieldAnnotations(cpphdl::Field* field, const std::vector<std::string>& annotations)
{
    if (!field) {
        return;
    }
    for (const auto& annotation : annotations) {
        if (std::find(field->annotations.begin(), field->annotations.end(), annotation) == field->annotations.end()) {
            field->annotations.push_back(annotation);
        }
    }
}

std::vector<const CXXRecordDecl*> annotationRecordsFor(const CXXRecordDecl* RD)
{
    std::vector<const CXXRecordDecl*> records;
    records.push_back(RD);
    if (const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        if (const ClassTemplateDecl* TD = CTSD->getSpecializedTemplate()) {
            if (const CXXRecordDecl* templated = TD->getTemplatedDecl()) {
                if (templated != RD) {
                    records.push_back(templated);
                }
            }
        }
    }
    return records;
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

    AnnotationVars vars = annotationTemplateVariables(RD, ctx);
    for (const CXXRecordDecl* annotationRD : annotationRecordsFor(RD)) {
        for (const Attr* attr : annotationRD->attrs()) {
            if (const auto* ann = dyn_cast<AnnotateAttr>(attr)) {
                std::string text = ann->getAnnotation().str();
                if (text.rfind(replacement_prefix, 0) == 0) {
                    std::string replacement = stripAnnotationValue(std::move(text), replacement_prefix);
                    return replaceAnnotationVariables(replacement, vars);
                }
                if (text.rfind(script_prefix, 0) == 0) {
                    std::string script = stripAnnotationValue(std::move(text), script_prefix);
                    script = replaceAnnotationVariables(script, vars);
                    return readReplacementFromScript(scriptCommandFromAnnotation(script, annotationRD, ctx));
                }
                if (text.rfind(file_prefix, 0) == 0) {
                    std::string file = stripAnnotationValue(std::move(text), file_prefix);
                    file = replaceAnnotationVariables(file, vars);
                    return readReplacementFromFile(resolveAnnotationPath(file, annotationRD, ctx));
                }
            }
        }
    }
    return {};
}

bool hasCpphdlReplacementAnnotation(const CXXRecordDecl* RD)
{
    constexpr std::string_view replacement_prefix = "CPPHDL_REPLACEMENT=";
    constexpr std::string_view script_prefix = "CPPHDL_REPLACEMENT_SCRIPT=";
    constexpr std::string_view file_prefix = "CPPHDL_REPLACEMENT_FILE=";

    for (const CXXRecordDecl* annotationRD : annotationRecordsFor(RD)) {
        for (const Attr* attr : annotationRD->attrs()) {
            if (const auto* ann = dyn_cast<AnnotateAttr>(attr)) {
                std::string text = ann->getAnnotation().str();
                if (text.rfind(replacement_prefix, 0) == 0
                    || text.rfind(script_prefix, 0) == 0
                    || text.rfind(file_prefix, 0) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

void addEnumPackageImport(EnumDecl* ED, cpphdl::Struct* st)
{
    if (!ED) {
        return;
    }

    std::string name = genTypeName(ED->getQualifiedNameAsString());
    if (std::find_if(st->imports.begin(), st->imports.end(), [&](auto& imp){ return imp.name == name; }) == st->imports.end()) {
        st->imports.emplace_back(name);
    }

    if (std::find_if(currProject->enums.begin(), currProject->enums.end(), [&](auto& en){ return en.name == name; }) != currProject->enums.end()) {
        return;
    }

    cpphdl::Enum en{name, ED->getQualifiedNameAsString()};
    for (const EnumConstantDecl* ECD : ED->enumerators()) {
        if (ECD->getInitExpr()) {
            en.fields.emplace_back(cpphdl::Field{ECD->getName().str(),
                {std::to_string(ECD->getInitVal().getSExtValue()), cpphdl::Expr::EXPR_NUM}});
        }
        else {
            en.fields.emplace_back(cpphdl::Field{ECD->getName().str()});
        }
    }
    currProject->enums.emplace_back(std::move(en));
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

void updateArrayDim(cpphdl::Expr& concrete, const cpphdl::Expr& abstract)
{
    // Modules are visited as a concrete specialization first and then as their
    // primary template. Keep the primary expression as the single dimension;
    // updateExpr() would retain both concrete and symbolic array dimensions.
    if (concrete.type == cpphdl::Expr::EXPR_NUM && abstract.type != cpphdl::Expr::EXPR_NUM) {
        concrete = abstract;
        return;
    }
    updateExpr(concrete, abstract);
}

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp, cpphdl::Struct* st = nullptr);
static bool cpphdlRecordShouldExportAsStruct(const CXXRecordDecl* RD, Helpers& hlp);

std::unordered_map<std::string, cpphdl::Expr> templateTypeSubstitutions(const CXXRecordDecl* RD, Helpers& hlp)
{
    std::unordered_map<std::string, cpphdl::Expr> result;
    const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD || !CTSD->getSpecializedTemplate()) {
        return result;
    }

    const TemplateArgumentList& args = CTSD->getTemplateArgs();
    const TemplateParameterList* params = CTSD->getSpecializedTemplate()->getTemplateParameters();
    const unsigned n = std::min<unsigned>(args.size(), params->size());
    for (unsigned i = 0; i < n; ++i) {
        const auto* typeParam = dyn_cast<TemplateTypeParmDecl>(params->getParam(i));
        if (!typeParam || args[i].getKind() != TemplateArgument::Type) {
            continue;
        }

        cpphdl::Expr tmp;
        hlp.ArgToExpr(args[i], tmp, false);
        if (!tmp.sub.empty()) {
            QualType QT = args[i].getAsType().getNonReferenceType();
            CXXRecordDecl* CRD = hlp.resolveCXXRecordDecl(QT);
            if (cpphdlRecordShouldExportAsStruct(CRD, hlp)) {
                auto st = exportStruct(CRD, hlp);
                if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(),
                        [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
                    hlp.mod->imports.emplace_back(st.name);
                    currProject->structs.emplace_back(std::move(st));
                }
            }
            result.emplace(typeParam->getNameAsString(), std::move(tmp.sub.front()));
        }
    }
    return result;
}

std::unordered_map<std::string, cpphdl::Expr> templateTypeConstexprSubstitutions(const CXXRecordDecl* RD, Helpers& hlp)
{
    // Type template arguments are fixed for a module specialization. Resolve
    // their static constexpr fields so generated modules do not reference a
    // package that is intentionally not emitted for value-less helper types.
    std::unordered_map<std::string, cpphdl::Expr> result;
    const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD || !CTSD->getSpecializedTemplate()) {
        return result;
    }

    const TemplateArgumentList& args = CTSD->getTemplateArgs();
    const TemplateParameterList* params = CTSD->getSpecializedTemplate()->getTemplateParameters();
    const unsigned n = std::min<unsigned>(args.size(), params->size());
    for (unsigned i = 0; i < n; ++i) {
        const auto* typeParam = dyn_cast<TemplateTypeParmDecl>(params->getParam(i));
        if (!typeParam || args[i].getKind() != TemplateArgument::Type) {
            continue;
        }

        QualType QT = args[i].getAsType().getNonReferenceType();
        CXXRecordDecl* CRD = hlp.resolveCXXRecordDecl(QT);
        if (!CRD || !CRD->hasDefinition()) {
            continue;
        }

        const std::string paramPkg = typeParam->getNameAsString() + "_pkg::";
        std::string concreteName = genTypeName(CRD->getQualifiedNameAsString());
        hlp.followSpecialization(CRD, concreteName);
        const std::string concretePkg = concreteName + "_pkg::";

        for (Decl* D : CRD->getDefinition()->decls()) {
            const auto* VD = dyn_cast<VarDecl>(D);
            if (!VD || !VD->isStaticDataMember() || !VD->isConstexpr() || !VD->getInit()) {
                continue;
            }
            cpphdl::Expr value;
            if (!evaluatedIntegerExpr(VD->getInit(), *hlp.ctx, value)) {
                continue;
            }
            result.emplace(paramPkg + VD->getNameAsString(), value);
            result.emplace(concretePkg + VD->getNameAsString(), std::move(value));
        }
    }
    return result;
}

std::unordered_map<std::string, cpphdl::Expr> templateTypeSubstitutionsForRecord(const CXXRecordDecl* RD, Helpers& hlp)
{
    std::unordered_map<std::string, cpphdl::Expr> result;
    const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD || !CTSD->getSpecializedTemplate()) {
        return result;
    }

    const TemplateArgumentList& args = CTSD->getTemplateArgs();
    const TemplateParameterList* params = CTSD->getSpecializedTemplate()->getTemplateParameters();
    const unsigned n = std::min<unsigned>(args.size(), params->size());
    for (unsigned i = 0; i < n; ++i) {
        const NamedDecl* param = params->getParam(i);
        if (!param || param->getName().empty()) {
            continue;
        }

        cpphdl::Expr tmp;
        hlp.ArgToExpr(args[i], tmp, false);
        if (!tmp.sub.empty()) {
            result.emplace(param->getNameAsString(), std::move(tmp.sub.front()));
        }
    }
    return result;
}

void appendTemplateTypeSpecializationName(std::string& name, const CXXRecordDecl* RD, Helpers& hlp)
{
    const ClassTemplateDecl* CTD = nullptr;
    if (const auto* CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(RD)) {
        CTD = CTSD->getSpecializedTemplate();
    }
    else if (RD) {
        CTD = RD->getDescribedClassTemplate();
    }
    if (!CTD) {
        return;
    }

    const auto substitutions = templateTypeSubstitutions(hlp.specializationParent, hlp);
    if (substitutions.empty()) {
        return;
    }

    bool first = true;
    for (const NamedDecl* param : *CTD->getTemplateParameters()) {
        const auto* typeParam = dyn_cast<TemplateTypeParmDecl>(param);
        if (!typeParam) {
            continue;
        }
        auto it = substitutions.find(typeParam->getNameAsString());
        if (it == substitutions.end()) {
            continue;
        }
        if (!first) {
            name += "_";
        }
        cpphdl::Expr arg = it->second;
        name += genTypeName(arg.str());
        first = false;
    }
}

void applyTemplateTypeSubstitutions(cpphdl::Expr& expr, const std::unordered_map<std::string, cpphdl::Expr>& substitutions)
{
    if (substitutions.empty()) {
        return;
    }

    if ((expr.type == cpphdl::Expr::EXPR_TYPE || expr.type == cpphdl::Expr::EXPR_VAR)
        && substitutions.contains(expr.value)) {
        expr = substitutions.at(expr.value);
    }
    else if (expr.type == cpphdl::Expr::EXPR_VAR) {
        for (const auto& [name, replacement] : substitutions) {
            if (replacement.type != cpphdl::Expr::EXPR_TYPE) {
                continue;
            }
            cpphdl::Expr concreteExpr = replacement;
            const std::string concrete = concreteExpr.str();
            const std::string pkgPrefix = name + "_pkg::";
            const std::string scopePrefix = name + "::";
            if (expr.value.rfind(pkgPrefix, 0) == 0) {
                expr.value = concrete + "_pkg::" + expr.value.substr(pkgPrefix.size());
                break;
            }
            if (expr.value.rfind(scopePrefix, 0) == 0) {
                expr.value = concrete + "_pkg::" + expr.value.substr(scopePrefix.size());
                break;
            }
        }
    }
    else if (expr.type == cpphdl::Expr::EXPR_TEMPLATE) {
        if (auto it = substitutions.find(expr.value); it != substitutions.end()) {
            expr = it->second;
        }
    }

    for (auto& sub : expr.sub) {
        applyTemplateTypeSubstitutions(sub, substitutions);
    }
}

void replacePackageOwner(cpphdl::Expr& expr, const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) {
        return;
    }

    const std::string fromPrefix = from + "_pkg::";
    const std::string toPrefix = to + "_pkg::";
    expr.traverseIf([&](cpphdl::Expr& e) {
        if (e.type == cpphdl::Expr::EXPR_VAR && e.value.rfind(fromPrefix, 0) == 0) {
            e.value = toPrefix + e.value.substr(fromPrefix.size());
        }
        return false;
    });
}

void localizeKnownModuleConstRefs(cpphdl::Expr& expr, const cpphdl::Module& mod)
{
    expr.traverseIf([&](cpphdl::Expr& e) {
        if (e.type != cpphdl::Expr::EXPR_VAR) {
            return false;
        }
        const size_t scope = e.value.rfind("_pkg::");
        if (scope == std::string::npos) {
            return false;
        }
        const std::string constName = e.value.substr(scope + 6);
        if (std::find_if(mod.consts.begin(), mod.consts.end(),
                [&](const cpphdl::Field& c){ return c.name == constName; }) != mod.consts.end()) {
            e.value = constName;
        }
        return false;
    });
}

void rewriteSelfConstReferenceToTemplateParameter(cpphdl::Expr& expr, const std::string& constName, const cpphdl::Module& mod)
{
    if (expr.type != cpphdl::Expr::EXPR_VAR || expr.value != constName) {
        return;
    }

    const std::string templateParamName = constName + "_";
    if (std::find_if(mod.parameters.begin(), mod.parameters.end(),
            [&](const cpphdl::Field& param){ return param.name == templateParamName; }) != mod.parameters.end()) {
        // Re-exported base geometry constants often use CACHE_SIZE = Base::CACHE_SIZE
        // while the module template parameter is CACHE_SIZE_.  Localizing the
        // base reference to CACHE_SIZE would create a circular SV parameter.
        expr.value = templateParamName;
    }
}

bool structHasAnonymousAggregateWrapper(const std::string& typeName)
{
    auto it = std::find_if(currProject->structs.begin(), currProject->structs.end(),
        [&](const cpphdl::Struct& st) { return st.name == typeName; });
    if (it == currProject->structs.end()) {
        return false;
    }

    return std::any_of(it->fields.begin(), it->fields.end(), [](const cpphdl::Field& field) {
        return field.name.empty() && field.definition.type != cpphdl::Struct::STRUCT_EMPTY;
    });
}

static bool cpphdlRecordShouldExportAsStruct(const CXXRecordDecl* RD, Helpers& hlp)
{
    if (!RD || !RD->hasDefinition()) {
        return false;
    }
    if (RD->getQualifiedNameAsString().find("cpphdl::") == 0 ||
        RD->getQualifiedNameAsString().find("std::") == 0) {
        return false;
    }

    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
    if (ModuleClass && RD->isDerivedFrom(ModuleClass)) {
        return false;
    }
    if (isCurrentOrBaseRecord(hlp.parent, RD)) {
        return false;
    }

    // Empty user structs still need SV packages when used through template type
    // parameters; exportStruct() adds the pad bit that makes them legal SV.
    return true;
}

void fixAnonymousLocalMemberAccess(cpphdl::Expr& expr, std::unordered_map<std::string, bool>& localAnonTypes)
{
    if (expr.type == cpphdl::Expr::EXPR_MEMBER && expr.sub.size() == 1
        && !(expr.flags & cpphdl::Expr::FLAG_ANON)
        && expr.value != "_"
        && expr.value != ""
        && expr.sub[0].type == cpphdl::Expr::EXPR_VAR
        && localAnonTypes.contains(expr.sub[0].value)) {
        cpphdl::Expr base = std::move(expr.sub[0]);
        expr.sub[0] = cpphdl::Expr{"", cpphdl::Expr::EXPR_MEMBER, {std::move(base)}, cpphdl::Expr::FLAG_ANON};
    }

    for (auto& sub : expr.sub) {
        fixAnonymousLocalMemberAccess(sub, localAnonTypes);
    }
}

void fixAnonymousLocalMemberAccesses(cpphdl::Method& method, const std::unordered_map<std::string, cpphdl::Expr>& substitutions)
{
    std::unordered_set<std::string> substitutedTypeNames;
    for (const auto& [_, expr] : substitutions) {
        if (expr.type == cpphdl::Expr::EXPR_TYPE) {
            substitutedTypeNames.insert(expr.value);
        }
    }

    std::unordered_map<std::string, bool> localAnonTypes;
    for (auto& arg : method.arguments) {
        if (arg.expr.type == cpphdl::Expr::EXPR_TYPE
            && substitutedTypeNames.contains(arg.expr.value)
            && structHasAnonymousAggregateWrapper(arg.expr.value)) {
            localAnonTypes.emplace(arg.name, true);
        }
    }

    for (auto& statement : method.statements) {
        statement.traverseIf([&](cpphdl::Expr& expr) {
            if (expr.type == cpphdl::Expr::EXPR_DECL && expr.sub.size()
                && expr.sub[0].type == cpphdl::Expr::EXPR_TYPE
                && substitutedTypeNames.contains(expr.sub[0].value)
                && structHasAnonymousAggregateWrapper(expr.sub[0].value)) {
                localAnonTypes.emplace(expr.value, true);
            }
            return false;
        });
        fixAnonymousLocalMemberAccess(statement, localAnonTypes);
    }
}

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp, cpphdl::Struct* st)
{
    DEBUG_AST(debugIndent++, "@ exportStruct " << RD->getQualifiedNameAsString()); on_return ret_debug([](){ --debugIndent; });
    cpphdl::Struct st_obj = {};
    const auto typeSubstitutions = templateTypeSubstitutionsForRecord(RD, hlp);
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

    auto putStructConstexpr = [&](const VarDecl* VD) {
        if (!VD->isStaticDataMember() || !VD->isConstexpr() || !VD->getInit()) {
            return;
        }
        if (std::find_if(st->parameters.begin(), st->parameters.end(),
                [&](auto& p){ return p.name == VD->getNameAsString(); }) != st->parameters.end()) {
            return;
        }

        DEBUG_AST(debugIndent, " Const(" << VD->getNameAsString() << "): ");
        cpphdl::Expr init;
        if (!evaluatedIntegerExpr(VD->getInit(), *hlp.ctx, init)) {
            init = hlp.exprToExpr(VD->getInit());
        }
        applyTemplateTypeSubstitutions(init, typeSubstitutions);
        const auto typeConstexprSubstitutions = templateTypeConstexprSubstitutions(RD, hlp);
        applyTemplateTypeSubstitutions(init, typeConstexprSubstitutions);
        st->parameters.emplace_back(cpphdl::Field{VD->getNameAsString(), std::move(init)});
        DEBUG_EXPR(debugIndent, " Expr: " << st->parameters.back().expr.debug(debugIndent));
    };

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
            applyTemplateTypeSubstitutions(expr, typeSubstitutions);

            if (!pointer) {

                QT = QT.getNonReferenceType();
                QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?                QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                auto* CRD = hlp.resolveCXXRecordDecl(QT);
                if (const auto* ET = QT->getAs<EnumType>()) {
                    addEnumPackageImport(ET->getDecl(), st);
                }
                DEBUG_AST1(" {var " << FD->getNameAsString() << "} " << (CRD && CRD->isAnonymousStructOrUnion()?"ANON":""));
                st->fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)/*, std::move(params)*/});
                if (FD->isBitField()) {
                    DEBUG_AST1(" |bitfield");
                    Expr::EvalResult ER;
                    if (FD->getBitWidth()->EvaluateAsInt(ER, *hlp.ctx)) {
                        st->fields.back().bitwidth = cpphdl::Expr{std::to_string((size_t)ER.Val.getInt().getZExtValue()), cpphdl::Expr::EXPR_NUM};
                    } else {
                        st->fields.back().bitwidth = hlp.exprToExpr(FD->getBitWidth());
                        applyTemplateTypeSubstitutions(st->fields.back().bitwidth, typeSubstitutions);
                    }
                    DEBUG_AST1("|");
                }
//                DEBUG_EXPR(debugIndent, " Expr: " << st->fields.back().type.debug(debugIndent));
                // folded struct
                if (cpphdlRecordShouldExportAsStruct(CRD, hlp)) {
                    auto st1 = exportStruct(CRD, hlp);

                    if (CRD->isAnonymousStructOrUnion() || !CRD->getIdentifier()) {
                        st1.name = FD->getNameAsString();
                        st->fields.back().definition = std::move(st1);
                        DEBUG_AST1(" ANON");
                    }
                    else {
                        // Template struct fields must import the concrete specialization package, not the primary template name.
                        if (std::find_if(st->imports.begin(), st->imports.end(), [&](auto& imp){ return imp.name == st1.name; }) == st->imports.end()) {
                            st->imports.emplace_back(st1.name);
                        }
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
            putStructConstexpr(VD);
        }
    }

    if (const auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        if (const CXXRecordDecl* primary = CTSD->getSpecializedTemplate()->getTemplatedDecl()) {
            for (Decl* D : primary->decls()) {
                if (VarDecl* VD = dyn_cast<VarDecl>(D)) {
                    putStructConstexpr(VD);
                }
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

void putField(QualType fieldType, std::string fieldName, const Expr* initializer, Helpers& hlp, const FieldDecl* sourceField = nullptr)
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

        const TemplateArgument& sizeArg = TSD->getTemplateArgs()[0];
        const TemplateArgument& typeArg = TSD->getTemplateArgs()[1];
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
    // Abstract dependent base members may not resolve to a concrete Module
    // type. Match the name already installed by the concrete base, including
    // the base prefix used inside a derived module.
    std::string memberLookupName = fieldName;
    if (hlp.mod->origName.find(hlp.parent->getQualifiedNameAsString()) != 0) {
        memberLookupName = genTypeName(hlp.parent->getQualifiedNameAsString()) + "___" + fieldName;
    }
    if (std::find_if(hlp.mod->members.begin(), hlp.mod->members.end(),
        [&](auto& m){ return m.name == fieldName || m.name == memberLookupName; }) != hlp.mod->members.end()) {  // we cant see if it's of Module in abstract decl
        isMember = true;
    }

    hlp.skipStdFunctionType(QT);

    const std::string qtText = QT.getAsString(hlp.ctx->getPrintingPolicy());
    const bool functionRefCpphdlArray = qtText.rfind("cpphdl::array<", 0) == 0 || qtText.rfind("array<", 0) == 0;

    cpphdl::Expr expr = hlp.digQT(QT);
    // Dependent cpphdl::array specializations are not ClassTemplateSpecializationDecls,
    // so the type loop above cannot separate their dimensions from the element type.
    // Normalize them here to the same representation used by concrete specializations.
    while (expr.type == cpphdl::Expr::EXPR_TEMPLATE && expr.value == "cpphdl_array" && expr.sub.size() >= 2) {
        array_dim.emplace_back(std::move(expr.sub[0]));
        cpphdl::Expr element = std::move(expr.sub[1]);
        expr = std::move(element);
        ++cppArrayDims;
    }
    const bool packedArray = functionRefCpphdlArray || cppArrayDims != 0;
    size_t packedArrayDims = cppArrayDims;
    if (functionRefCpphdlArray && packedArrayDims == 0) {
        packedArrayDims = array_dim.size() - cArrayDims;
        if (packedArrayDims == 0) {
            packedArrayDims = array_dim.size();
        }
    }

    const auto typeSubstitutions = templateTypeSubstitutions(hlp.parent, hlp);
    applyTemplateTypeSubstitutions(expr, typeSubstitutions);
    for (auto& dim : array_dim) {
        applyTemplateTypeSubstitutions(dim, typeSubstitutions);
    }
    const auto inlineAnnotations = fieldInlineAnnotations(sourceField, *hlp.ctx);

    const bool isInterfaceField = CRD && CRD->isDerivedFrom(InterfaceClass);
    if (str_ending(fieldName, "_in") || str_ending(fieldName, "_out") || isInterfaceField) {
        DEBUG_AST1(" {port " << fieldName << "} ");

        auto* CRD = hlp.resolveCXXRecordDecl(QT);

        cpphdl::Field* field = 0;
        auto it = std::find_if(hlp.mod->ports.begin(), hlp.mod->ports.end(), [&](auto& p){ return p.name == fieldName; } );
        if (it != hlp.mod->ports.end()) {  // field already exists
            field = &*it;
            updateExpr(field->expr, expr);
            for (size_t i=0; i < array_dim.size(); ++i) {
                if (field->array.size() > i) {
                    updateArrayDim(field->array[i], array_dim[i]);
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
                        updateArrayDim(field->array[i], array_dim[i]);
                    }
                }
                const clang::TemplateSpecializationType *TST = QT->getAs<clang::TemplateSpecializationType>();
                if (TST && TST->getTemplateName().getAsTemplateDecl()) {
                    const clang::TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
                    const clang::TemplateParameterList *params = TD->getTemplateParameters();  // getting port names
                    // Rebuild the flattened sub-port from the Interface primary template;
                    // the existing field contains only the first concrete specialization.
                    const auto* classTemplate = dyn_cast<ClassTemplateDecl>(TD);
                    const CXXRecordDecl* primary = classTemplate ? classTemplate->getTemplatedDecl() : nullptr;
                    if (primary) {
                        for (Decl* D : primary->decls()) {
                            auto* FD = dyn_cast<FieldDecl>(D);
                            if (!FD) {
                                continue;
                            }
                            std::string ending = FD->getNameAsString();
                            if (str_ending(fieldName, "_out")) {
                                if (str_ending(ending, "_out")) {
                                    ending.replace(ending.length() - 4, 4, "_in");
                                } else if (str_ending(ending, "_in")) {
                                    ending.replace(ending.length() - 3, 3, "_out");
                                }
                            }
                            if (field->name != fieldName + "__" + ending) {
                                continue;
                            }

                            QualType subQT = FD->getType().getNonReferenceType();
                            hlp.skipStdFunctionType(subQT);
                            cpphdl::Expr abstractSub = hlp.digQT(subQT);
                            size_t i = 0;
                            for (const clang::NamedDecl *param : *params) {
                                DEBUG_EXPR1(" checking param " << param->getNameAsString() << " ");
                                abstractSub.traverseIf([&](auto& e) {
                                    if (e.type == cpphdl::Expr::EXPR_VAR
                                        && e.value == param->getNameAsString()
                                        && expr.sub.size() > i) {
                                        e = expr.sub[i];
                                    }
                                    return false;
                                });
                                ++i;
                            }
                            updateExpr(field->expr, abstractSub);
                            break;
                        }
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
                applyTemplateTypeSubstitutions(field->initializer, typeSubstitutions);
                DEBUG_EXPR(debugIndent, " Expr: " << field->initializer.debug(debugIndent));
                DEBUG_AST1(">");
            }
        }

        if (CRD && CRD->isDerivedFrom(InterfaceClass)) {  // Interface struct
            // for Interface structs we need Abstract declaration to know expressions of template parameters in subfields (and port cames too)
            // Non-template interfaces have no specialization node; flatten their
            // concrete declaration directly instead of dereferencing a null cast.
            auto* interfaceSpecialization = dyn_cast<ClassTemplateSpecializationDecl>(CRD);
            ClassTemplateDecl *CTD = interfaceSpecialization ? interfaceSpecialization->getSpecializedTemplate() : nullptr;
            const clang::TemplateParameterList *interfaceParams = CTD ? CTD->getTemplateParameters() : nullptr;
            if (CTD) {
                CRD = CTD->getTemplatedDecl();
            }
            for (Decl* D : CRD->decls()) {
                if (auto* FD = dyn_cast<FieldDecl>(D)) {
                    QualType QT = FD->getType().getNonReferenceType();
                    hlp.skipStdFunctionType(QT);
                    cpphdl::Expr exprSub = hlp.digQT(QT);
                    if (interfaceParams) {
                        size_t i = 0;
                        for (const clang::NamedDecl *param : *interfaceParams) {
                            // Interface ports are cloned from the primary template,
                            // so replace ADDR_WIDTH/DATA_WIDTH/etc with the
                            // concrete Axi4If<...> actuals while creating them.
                            exprSub.traverseIf([&](auto& e) {
                                if (e.type == cpphdl::Expr::EXPR_VAR
                                    && e.value == param->getNameAsString()
                                    && expr.sub.size() > i) {
                                    e = expr.sub[i];
                                }
                                return false;
                            });
                            ++i;
                        }
                    }
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
        if (cpphdlRecordShouldExportAsStruct(CRD, hlp)) {
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
                        updateArrayDim(field->array[i], array_dim[i]);
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
                        updateArrayDim(field->array[i], array_dim[i]);
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
                    appendFieldAnnotations(field, inlineAnnotations);
                }
            }

            auto* CRD = hlp.resolveCXXRecordDecl(QT);
            if (cpphdlRecordShouldExportAsStruct(CRD, hlp)) {
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

    if (MD->getParent()->isDerivedFrom(ModuleClass)) {
        auto it = std::find_if(currProject->modules.begin(), currProject->modules.end(), [&](auto& mod) {
            return mod.origName == MD->getParent()->getQualifiedNameAsString();
        });
        if (((it != currProject->modules.end() && !it->replacement.empty())
                || hasCpphdlReplacementAnnotation(MD->getParent()))
            && hlp.mod->origName != MD->getParent()->getQualifiedNameAsString()) {
            return "";
        }
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
        QualType QT = MD->getReturnType().getNonReferenceType();
        const std::string returnType = QT.getAsString(hlp.ctx->getPrintingPolicy());
        const std::string canonicalReturnType = QT.getCanonicalType().getAsString(hlp.ctx->getPrintingPolicy());
        if (returnType == "std::string" || returnType == "string" ||
            canonicalReturnType.find("std::basic_string") != std::string::npos ||
            canonicalReturnType.find("basic_string") != std::string::npos) {
            method = cpphdl::Method{MD->getNameAsString(), {cpphdl::Expr{"std::string", cpphdl::Expr::EXPR_TYPE}}};
        }
        else {
            method = cpphdl::Method{MD->getNameAsString(), {hlp.digQT(QT)}};
        }
    }

    unsigned savedFlags = hlp.flags;
    std::string externalThisTypeName;
    // three types of methods:
    // - module object's methods
    // - base module object's methods
    // - external object's methods (require _this)
    if (hlp.mod->origName.find(MD->getParent()->getQualifiedNameAsString()) != 0) {  // method of base class or external object (not current mod)
        std::string parentName = genTypeName(MD->getParent()->getQualifiedNameAsString());
        if ((notThis || (hlp.flags&Helpers::FLAG_EXTERNAL_THIS))) {  // method called for var - need specialization
            if (MD->getNameAsString() == "_work" || MD->getNameAsString() == "_assign") {
                return "";  // we need not work functions from third party classes (not Modules)
            }

            // extracting parameters of the template
            std::vector<cpphdl::Field> params;
            hlp.followSpecialization(MD->getParent(), parentName, &params);
            // A concrete helper already contributes its fixed type arguments
            // through followSpecialization(); only a primary template needs the
            // enclosing module's type substitution appended to its name.
            if (!dyn_cast<ClassTemplateSpecializationDecl>(MD->getParent())) {
                appendTemplateTypeSpecializationName(parentName, MD->getParent(), hlp);
            }
            externalThisTypeName = parentName;
            DEBUG_AST1(" - not Module method: (" << hlp.mod->name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
            hlp.flags |= Helpers::FLAG_EXTERNAL_THIS;

            QualType QT = MD->getThisType()->getPointeeType();

            auto* CRD = hlp.resolveCXXRecordDecl(QT);  // this of method
            if (cpphdlRecordShouldExportAsStruct(CRD, hlp)) {
                auto st = exportStruct(CRD, hlp);
                if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
                    hlp.mod->imports.emplace_back(st.name);
                    currProject->structs.emplace_back(std::move(st));
                }
            }

            cpphdl::Expr expr = externalThisTypeName.empty()
                ? hlp.digQT(QT)
                : cpphdl::Expr{externalThisTypeName, cpphdl::Expr::EXPR_TYPE};
            QT = QT.getDesugaredType(*hlp.ctx); // remove typedefs, aliases, etc.
//?            QT = QT.getCanonicalType();        // ensure you have the actual canonical form
            DEBUG_AST(debugIndent, "Param this (" << QT.getAsString() << ")");
            method.arguments.emplace_back(cpphdl::Field{"_this", std::move(expr)});
            DEBUG_EXPR(debugIndent, " param Expr: " << method.arguments.back().expr.debug(debugIndent));
        }
        else {
            if (MD->isStatic()) {
                parentName = genTypeName(MD->getParent()->getQualifiedNameAsString());
                hlp.followSpecialization(MD->getParent(), parentName);
                if (!dyn_cast<ClassTemplateSpecializationDecl>(MD->getParent())) {
                    appendTemplateTypeSpecializationName(parentName, MD->getParent(), hlp);
                }
            }
            else {
                parentName = genTypeName(MD->getParent()->getNameAsString());
                appendTemplateTypeSpecializationName(parentName, MD->getParent(), hlp);
            }
            DEBUG_AST1(" - base Module method: (" << hlp.mod->name << " " << MD->getParent()->getQualifiedNameAsString() << ")");
        }
        method.name = parentName + "___";
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

    const auto typeSubstitutions = templateTypeSubstitutions(hlp.specializationParent, hlp);
    for (auto& ret : method.ret) {
        applyTemplateTypeSubstitutions(ret, typeSubstitutions);
    }
    for (auto& arg : method.arguments) {
        applyTemplateTypeSubstitutions(arg.expr, typeSubstitutions);
        applyTemplateTypeSubstitutions(arg.initializer, typeSubstitutions);
        applyTemplateTypeSubstitutions(arg.bitwidth, typeSubstitutions);
    }
    for (auto& statement : method.statements) {
        applyTemplateTypeSubstitutions(statement, typeSubstitutions);
    }
    if (!externalThisTypeName.empty()) {
        const std::string primaryThisTypeName = genTypeName(MD->getParent()->getQualifiedNameAsString());
        for (auto& ret : method.ret) {
            replacePackageOwner(ret, primaryThisTypeName, externalThisTypeName);
        }
        for (auto& arg : method.arguments) {
            replacePackageOwner(arg.expr, primaryThisTypeName, externalThisTypeName);
            replacePackageOwner(arg.initializer, primaryThisTypeName, externalThisTypeName);
            replacePackageOwner(arg.bitwidth, primaryThisTypeName, externalThisTypeName);
        }
        for (auto& statement : method.statements) {
            replacePackageOwner(statement, primaryThisTypeName, externalThisTypeName);
        }
    }
    fixAnonymousLocalMemberAccesses(method, typeSubstitutions);

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

        const CXXRecordDecl* definition = RD->getDefinition();
        if (dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
            definition = RD;
        }

        auto putStaticConstexpr = [&](const VarDecl* VD) {
            if (!VD->isStaticDataMember() || !VD->isConstexpr() || !VD->getInit()) {
                return false;
            }

            cpphdl::Expr init;
            if (!evaluatedIntegerExpr(VD->getInit(), *hlp.ctx, init)) {
                init = hlp.exprToExpr(VD->getInit());
            }
            const auto typeSubstitutions = templateTypeSubstitutions(hlp.parent, hlp);
            applyTemplateTypeSubstitutions(init, typeSubstitutions);
            const auto typeConstexprSubstitutions = templateTypeConstexprSubstitutions(hlp.parent, hlp);
            applyTemplateTypeSubstitutions(init, typeConstexprSubstitutions);

            auto it = std::find_if(hlp.mod->consts.begin(), hlp.mod->consts.end(),
                [&](auto& c){ return c.name == VD->getNameAsString(); } );
            if (it != hlp.mod->consts.end()) {
                if (it->expr.sub.empty()) {
                    localizeKnownModuleConstRefs(init, *hlp.mod);
                    rewriteSelfConstReferenceToTemplateParameter(init, VD->getNameAsString(), *hlp.mod);
                    it->expr.sub.emplace_back(std::move(init));
                }
                else {
                    localizeKnownModuleConstRefs(init, *hlp.mod);
                    rewriteSelfConstReferenceToTemplateParameter(init, VD->getNameAsString(), *hlp.mod);
                    updateExpr(it->expr.sub[0], init);
                    localizeKnownModuleConstRefs(it->expr.sub[0], *hlp.mod);
                    rewriteSelfConstReferenceToTemplateParameter(it->expr.sub[0], VD->getNameAsString(), *hlp.mod);
                }
            }
            else {
                localizeKnownModuleConstRefs(init, *hlp.mod);
                rewriteSelfConstReferenceToTemplateParameter(init, VD->getNameAsString(), *hlp.mod);
                hlp.mod->consts.emplace_back(cpphdl::Field{VD->getNameAsString(),
                    cpphdl::Expr{"parameter", cpphdl::Expr::EXPR_CONST, {std::move(init)}}});
                DEBUG_AST(debugIndent, "constexpr: " << VD->getNameAsString() << "\n");
            }
            return true;
        };

        for (Decl* D : definition->decls()) {
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

                putField(FD->getType().getNonReferenceType(), FD->getNameAsString(), FD->getInClassInitializer(), hlp, FD);
            } else
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (putStaticConstexpr(VD)) {
                    continue;
                }
                if (VD->isStaticDataMember() && VD->isConstexpr()) {
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
                    QualType aliasQT = TypeAlias->getUnderlyingType().getNonReferenceType();
                    CXXRecordDecl* aliasRD = hlp.resolveCXXRecordDecl(aliasQT);
                    auto* ModuleClass = hlp.lookupQualifiedRecord("cpphdl::Module");
                    const bool aliasIsClassOnly =
                        aliasRD
                        && (!cpphdlRecordHasValueFields(aliasRD)
                            || (ModuleClass && aliasRD->isDerivedFrom(ModuleClass)));
                    if (!aliasIsClassOnly
                        && TypeAlias->getUnderlyingType().getCanonicalType().getAsString(hlp.ctx->getPrintingPolicy()).find("std::") != 0) {
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
        for (const CXXMethodDecl *MD : definition->methods()) {
            putMethod(MD, hlp);
        }

        for (auto* aRD : abstractDefs) {  // we take from abstract: 1) initializers for members, 2) template numeric parameters names, 3) numeric template parameter expressions in all code
            if (/*hlp.mod->name*/RD->getQualifiedNameAsString().find(aRD->getQualifiedNameAsString()) == 0) {
                DEBUG_AST(debugIndent, "*** Applying abstract: " << aRD->getQualifiedNameAsString() << " to " << hlp.mod->name);  // get original parameters substitution form abstract, need only for numbers

                // Parse primary-template fields in their own dependent context.
                // Reusing the concrete specialization helper substitutes the
                // first actual (for example WAYS=1) back into inherited array
                // dimensions and freezes an otherwise parameterized module.
                Helpers abstractHlp(context, &mod, aRD);
                // Keep numeric expressions dependent on the primary template,
                // while nested method calls still resolve its fixed type arguments
                // from the concrete module specialization being assembled.
                abstractHlp.specializationParent = RD;

                for (Decl* D : aRD->decls()) {  // need fields from abstract class to get its port width parametrict expressions (not numbers)
                    if (auto* FD = dyn_cast<FieldDecl>(D)) {
                        abstractHlp.flags |= Helpers::FLAG_ABSTRACT;
                        putField(FD->getType().getNonReferenceType(), FD->getNameAsString(),
                            FD->getInClassInitializer(), abstractHlp, FD);
                        abstractHlp.flags &= ~Helpers::FLAG_ABSTRACT;
                    } else
                    if (auto* VD = dyn_cast<VarDecl>(D)) {
                        if (putStaticConstexpr(VD)) {
                            continue;
                        }
                        if (VD->isStaticDataMember() && VD->isConstexpr()) {
                            continue;
                        }
                        if (VD->isStaticDataMember() && !VD->isConstexpr()) {
                            abstractHlp.flags |= Helpers::FLAG_ABSTRACT;
                            putField(VD->getType().getNonReferenceType(), VD->getNameAsString(),
                                VD->getInit(), abstractHlp);
                            abstractHlp.flags &= ~Helpers::FLAG_ABSTRACT;
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
    bool BeginSourceFileAction(CompilerInstance &CI) override
    {
        // Fetched Clang 21 can report failed include-search candidates here
        // even when a later search directory opens the header successfully.
        CI.getDiagnostics().setSeverity(
            clang::diag::err_cannot_open_file,
            clang::diag::Severity::Ignored,
            clang::SourceLocation());
        return true;
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override
    {
        return std::make_unique<MethodConsumer>(&CI.getASTContext());
    }
};

int main(int argc, const char **argv)
{
    std::vector<const char*> replace;
    std::deque<std::string> owned_args;
    std::string generated_dir = "generated";
    std::string json_output;
    // JSON extraction previously forced SYNTHESIS and hid test-only modules.
    // Callers need to select whether preprocessing follows synthesis guards.
    // Keep synthesis as the default while recording an explicit opt-out here.
    bool add_synthesis_flag = true;
    bool saw_double_dash = false;
    bool injected_double_dash = false;
    std::filesystem::path cpphdl_source_include;

#ifdef CPPHDL_SOURCE_DIR
    cpphdl_source_include = std::filesystem::path(CPPHDL_SOURCE_DIR) / "include";
#endif

    if (argc > 0) {
        replace.push_back(argv[0]);
    }

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (!saw_double_dash && std::strcmp(arg, "--debug") == 0) {
            cpphdlDebugEnabled = true;
            continue;
        }

        // This option belongs to cpphdl rather than the forwarded compiler args.
        // Consuming it before "--" prevents Clang from seeing an unknown option.
        // It disables only cpphdl's implicit SYNTHESIS definition.
        if (!saw_double_dash && std::strcmp(arg, "--no-synthesis-flag") == 0) {
            add_synthesis_flag = false;
            continue;
        }

        if (!saw_double_dash && std::strcmp(arg, "--generated-dir") == 0) {
            if (i + 1 >= argc) {
                llvm::errs() << "--generated-dir requires a directory argument\n";
                return 1;
            }
            generated_dir = argv[++i];
            continue;
        }

        if (!saw_double_dash && std::strncmp(arg, "--generated-dir=", 16) == 0) {
            generated_dir = arg + 16;
            if (generated_dir.empty()) {
                llvm::errs() << "--generated-dir requires a non-empty directory argument\n";
                return 1;
            }
            continue;
        }

        if (!saw_double_dash && std::strcmp(arg, "--json-output") == 0) {
            if (i + 1 >= argc) {
                llvm::errs() << "--json-output requires a file name argument\n";
                return 1;
            }
            json_output = argv[++i];
            continue;
        }

        if (!saw_double_dash && std::strncmp(arg, "--json-output=", 14) == 0) {
            json_output = arg + 14;
            if (json_output.empty()) {
                llvm::errs() << "--json-output requires a non-empty file name argument\n";
                return 1;
            }
            continue;
        }

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

    std::vector<std::string> cpphdl_include_args;
#ifdef CPPHDL_SOURCE_DIR
    if (std::filesystem::exists(cpphdl_source_include / "cpphdl.h")) {
        cpphdl_include_args.push_back("-isystem");
        cpphdl_include_args.push_back(cpphdl_source_include.string());
    }
#endif
#ifdef CPPHDL_CXX_IMPLICIT_INCLUDE_DIRS
    appendDelimitedIncludeDirs(cpphdl_include_args, CPPHDL_CXX_IMPLICIT_INCLUDE_DIRS);
#endif
    appendCompilerProbeIncludeDirs(cpphdl_include_args);

    // The old unconditional define made non-synthesis source invisible to JSON.
    // Preserve the established command-line behavior unless opted out above.
    // Build the common arguments first and append the define conditionally.
    std::vector<std::string> args{
        "-x", "c++",
        "-std=c++26"};
    if (add_synthesis_flag) {
        args.push_back("-DSYNTHESIS");
    }
    args.insert(args.end(), cpphdl_include_args.begin(), cpphdl_include_args.end());

    if (!saw_double_dash && !injected_double_dash) {
        replace.push_back("--");
    }
    for (const auto& arg : args) {
        owned_args.push_back(arg);
        replace.push_back(owned_args.back().c_str());
    }

    std::vector<const char*> new_argv = replace;
    new_argv.push_back(nullptr);
    int new_argc = (int)new_argv.size() - 1;

    if (cpphdlDebugEnabled) {
        llvm::errs() << "CppHDL clang args:";
        for (const auto& arg : args) {
            llvm::errs() << " " << arg;
        }
        llvm::errs() << "\n";
    }

    auto ExpectedParser = tooling::CommonOptionsParser::create(new_argc, (const char**)new_argv.data(), MyToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    tooling::CommonOptionsParser& Options = ExpectedParser.get();

    tooling::ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());

    int ret = Tool.run(tooling::newFrontendActionFactory<MyFrontendAction>().get());
    currProject->generate(generated_dir);
    if (!json_output.empty() && !cpphdl::writeJsonOutput(*currProject, json_output)) {
        return 1;
    }
    return ret;
}

#include "FunctionOverrides.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include <stdexcept>

namespace cpphdl::hls {
using namespace clang;
namespace {
std::string nameOf(const FunctionDecl* fn) {
    std::string name = fn->getNameAsString();
    for (auto* parent = fn->getDeclContext(); parent; parent = parent->getParent()) {
        if (auto* ns = dyn_cast<NamespaceDecl>(parent)) {
            if (!ns->isInline() && !ns->isAnonymousNamespace()) name = ns->getNameAsString() + "::" + name;
        } else if (auto* record = dyn_cast<RecordDecl>(parent)) name = record->getNameAsString() + "::" + name;
    }
    return name;
}
}
FunctionOverrides::FunctionOverrides(ASTContext& ctx) : context(ctx) { collect(ctx.getTranslationUnitDecl()); }
void FunctionOverrides::collect(DeclContext* scope) {
    for (auto* declaration : scope->decls()) {
        if (auto* ns = dyn_cast<NamespaceDecl>(declaration)) collect(ns);
        else if (auto* linkage = dyn_cast<LinkageSpecDecl>(declaration)) collect(linkage);
        auto* fn = dyn_cast<FunctionDecl>(declaration);
        if (auto* templ = dyn_cast<FunctionTemplateDecl>(declaration)) fn = templ->getTemplatedDecl();
        if (!fn) continue;
        for (const auto* attr : fn->specific_attrs<AnnotateAttr>()) {
            auto annotation = attr->getAnnotation();
            bool raw = annotation.consume_front("CPPHDL_HLS_OVERRIDE_BITS=");
            if (!raw && !annotation.consume_front("CPPHDL_HLS_OVERRIDE=")) continue;
            if (!fn->doesThisDeclarationHaveABody()) {
                if (!fn->hasBody()) throw std::runtime_error("HLS override has no body: " + fn->getQualifiedNameAsString());
                continue;
            }
            if (annotation.empty() || fn->isVariadic() || fn->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
                throw std::runtime_error("HLS override requires a named target and a concrete non-variadic function");
            auto& list = entries[annotation.str()];
            for (const auto& entry : list)
                if (context.hasSameType(entry.function->getType(), fn->getType()) && entry.function != fn)
                    throw std::runtime_error("duplicate HLS override: " + annotation.str());
            list.push_back({fn, raw});
        }
    }
}
FunctionDecl* FunctionOverrides::find(const std::string& name, QualType result, const std::vector<QualType>& args) {
    auto it = entries.find(name);
    if (it == entries.end()) return nullptr;
    FunctionDecl* found = nullptr;
    for (const auto& entry : it->second) {
        auto* fn = entry.function;
        auto matches = [&](QualType original, QualType substitute) {
            if (context.hasSameType(original, substitute)) return true;
            return entry.rawBits && original->isFloatingType() && substitute->isUnsignedIntegerType() &&
                context.getTypeSize(original) == context.getTypeSize(substitute);
        };
        if (fn->getNumParams() != args.size() || !matches(result, fn->getReturnType())) continue;
        bool match = true;
        for (unsigned i = 0; i < args.size(); ++i) match &= matches(args[i], fn->getParamDecl(i)->getType());
        if (!match) continue;
        if (found) throw std::runtime_error("ambiguous HLS override: " + name);
        found = fn;
    }
    if (!found) throw std::runtime_error("HLS override signature mismatch: " + name);
    return found;
}
FunctionDecl* FunctionOverrides::find(FunctionDecl* fn, const std::vector<QualType>& actualTypes) {
    auto name = nameOf(fn);
    if (!entries.count(name)) return nullptr;
    if (fn->isVariadic() && !fn->getBuiltinID())
        throw std::runtime_error("variadic HLS override target is not supported: " + name);
    if (auto* method = dyn_cast<CXXMethodDecl>(fn); method && !method->isStatic())
        throw std::runtime_error("HLS override of nonstatic member functions is not supported: " + name);
    std::vector<QualType> args;
    for (auto* param : fn->parameters()) args.push_back(param->getType());
    // Type-generic Clang builtins can have no prototype parameters. Match the
    // concrete call, not that placeholder declaration.
    if (fn->getBuiltinID() && (fn->isVariadic() || args.size() != actualTypes.size())) args = actualTypes;
    return find(name, fn->getReturnType(), args);
}
FunctionDecl* FunctionOverrides::operation(const std::string& name, QualType result, const std::vector<QualType>& args) {
    auto* fn = find(name, result, args);
    if (!fn) throw std::runtime_error("floating-point HLS operation requires an explicit override: " + name);
    return fn;
}
bool FunctionOverrides::hasFloatingHooks() const {
    for (const auto& item : entries)
        for (const auto& entry : item.second) if (entry.rawBits) return true;
    return false;
}
}

#include "StdContainers.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <charconv>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>

namespace cpphdl::hls {
namespace {
llvm::json::Array units;

bool inStd(const clang::DeclContext* scope)
{
    for (; scope; scope = scope->getParent())
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(scope))
            if (ns->isStdNamespace()) return true;
    return false;
}

bool isModule(const clang::CXXRecordDecl* record)
{
    if (!record) return false;
    if (record->getQualifiedNameAsString() == "cpphdl::Module") return true;
    if (!record->hasDefinition()) return false;
    for (const auto& base : record->getDefinition()->bases())
        if (isModule(base.getType()->getAsCXXRecordDecl())) return true;
    return false;
}

const clang::CXXRecordDecl* containerRecord(clang::QualType type)
{
    type = type.getNonReferenceType();
    if (type->isPointerType()) type = type->getPointeeType();
    const auto* record = type->getAsCXXRecordDecl();
    if (!record || !inStd(record->getDeclContext())) return nullptr;
    const std::string name = record->getNameAsString();
    return name == "vector" || name == "inplace_vector" || name == "list" ||
        name == "map" || name == "multimap" || name == "unordered_map" ? record : nullptr;
}

std::string location(clang::SourceLocation loc, const clang::ASTContext& context)
{
    return loc.printToString(context.getSourceManager());
}

std::string functionName(const clang::FunctionDecl* fn, clang::ASTContext& context)
{
    std::string name;
    llvm::raw_string_ostream stream(name);
    fn->getNameForDiagnostic(stream, context.getPrintingPolicy(), true);
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fn)) {
        name = context.getRecordType(method->getParent()).getAsString() + "::" + name;
    }
    return name + " : " + fn->getType().getAsString();
}

struct Roots : clang::RecursiveASTVisitor<Roots>
{
    std::vector<const clang::FieldDecl*> fields;
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitFieldDecl(clang::FieldDecl* field)
    {
        const auto* owner = llvm::dyn_cast<clang::CXXRecordDecl>(field->getParent());
        if (owner && !owner->isDependentContext() && isModule(owner) && containerRecord(field->getType()))
            fields.push_back(field);
        return true;
    }
};

struct Calls : clang::RecursiveASTVisitor<Calls>
{
    std::vector<clang::FunctionDecl*> callees;
    std::vector<clang::QualType> types;
    unsigned indirect = 0, allocations = 0, deallocations = 0, throws = 0;
    // Type queries are not runtime calls. Instantiating declval from noexcept
    // or decltype is ill-formed and invents dependencies absent from the program.
    bool TraverseCXXNoexceptExpr(clang::CXXNoexceptExpr*) { return true; }
    bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr*) { return true; }
    bool TraverseType(clang::QualType) { return true; }
    bool TraverseTypeLoc(clang::TypeLoc) { return true; }
    bool VisitCallExpr(clang::CallExpr* call)
    {
        if (auto* fn = call->getDirectCallee()) callees.push_back(fn);
        else ++indirect;
        return true;
    }
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expr)
    {
        callees.push_back(expr->getConstructor());
        types.push_back(expr->getType());
        return true;
    }
    bool VisitMemberExpr(clang::MemberExpr* expr)
    {
        if (const auto* field = llvm::dyn_cast<clang::FieldDecl>(expr->getMemberDecl()))
            types.push_back(field->getType());
        types.push_back(expr->getBase()->getType());
        return true;
    }
    bool VisitCXXNewExpr(clang::CXXNewExpr* expr)
    {
        ++allocations;
        if (auto* fn = expr->getOperatorNew()) callees.push_back(fn);
        types.push_back(expr->getAllocatedType());
        return true;
    }
    bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* expr)
    {
        ++deallocations;
        if (auto* fn = expr->getOperatorDelete()) callees.push_back(fn);
        if (const auto* record = expr->getDestroyedType()->getAsCXXRecordDecl())
            if (record->getDestructor()) callees.push_back(record->getDestructor());
        return true;
    }
    bool VisitCXXThrowExpr(clang::CXXThrowExpr*) { ++throws; return true; }
};

struct Analysis
{
    clang::ASTContext& context;
    clang::Sema& sema;
    std::deque<clang::FunctionDecl*> queue;
    std::set<const clang::FunctionDecl*> seen;
    std::set<const clang::CXXRecordDecl*> records;
    llvm::json::Array methods, layouts, issues;
    static constexpr unsigned maxMethods = 2048;

    void type(clang::QualType qt)
    {
        if (qt.isNull() || qt->isDependentType()) return;
        qt = qt.getNonReferenceType();
        while (qt->isPointerType()) qt = qt->getPointeeType();
        if (const auto* array = context.getAsArrayType(qt)) { type(array->getElementType()); return; }
        const auto* record = qt->getAsCXXRecordDecl();
        if (!record || !record->getDefinition()) return;
        record = record->getDefinition();
        if (!records.insert(record->getCanonicalDecl()).second) return;
        llvm::json::Array fields, bases;
        for (const auto* field : record->fields()) {
            fields.push_back(llvm::json::Object{{"name", field->getNameAsString()},
                {"type", field->getType().getAsString()}, {"pointer", field->getType()->isPointerType()}});
            type(field->getType());
        }
        for (const auto& base : record->bases()) {
            bases.push_back(base.getType().getAsString());
            type(base.getType());
        }
        layouts.push_back(llvm::json::Object{{"type", context.getRecordType(record).getAsString()},
            {"fields", std::move(fields)}, {"bases", std::move(bases)}});
    }

    void add(clang::FunctionDecl* fn)
    {
        if (!fn || fn->isDependentContext() || fn->getType()->isDependentType()) return;
        if (seen.insert(fn->getCanonicalDecl()).second) queue.push_back(fn);
    }

    void collect()
    {
        while (!queue.empty() && methods.size() < maxMethods) {
            auto* fn = queue.front(); queue.pop_front();
            // Instantiate only reachable specializations, never every STL member.
            if (!fn->hasBody() && fn->getTemplateInstantiationPattern())
                sema.InstantiateFunctionDefinition(fn->getLocation(), fn, false, false, true);
            const clang::FunctionDecl* definition = nullptr;
            const auto* body = fn->getBody(definition);
            const std::string id = functionName(fn, context);
            llvm::json::Object node{{"id", id}, {"source", location(fn->getLocation(), context)},
                {"has_body", body != nullptr}, {"builtin", fn->getBuiltinID() != 0}};
            if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fn))
                type(context.getRecordType(method->getParent()));
            for (auto* param : fn->parameters()) type(param->getType());
            type(fn->getReturnType());
            llvm::json::Array edges;
            if (body) {
                Calls calls;
                calls.TraverseStmt(const_cast<clang::Stmt*>(body));
                for (auto* callee : calls.callees) {
                    edges.push_back(functionName(callee, context));
                    add(callee);
                }
                for (const auto& qt : calls.types) type(qt);
                std::string printed;
                llvm::raw_string_ostream stream(printed);
                body->printPretty(stream, nullptr, context.getPrintingPolicy());
                node["body"] = printed;
                node["allocations"] = calls.allocations;
                node["deallocations"] = calls.deallocations;
                node["throws"] = calls.throws;
                node["indirect_calls"] = calls.indirect;
            } else if (!fn->getBuiltinID() && !fn->isDefaulted()) {
                issues.push_back(llvm::json::Object{{"code", "missing_body"}, {"method", id},
                    {"source", location(fn->getLocation(), context)}});
            }
            node["calls"] = std::move(edges);
            methods.push_back(std::move(node));
        }
        if (!queue.empty()) issues.push_back(llvm::json::Object{{"code", "analysis_limit"}, {"limit", maxMethods}});
    }
};
}

bool inspectStdContainers(clang::ASTContext& context, clang::Sema& sema)
{
    Roots roots;
    roots.TraverseDecl(context.getTranslationUnitDecl());
    if (roots.fields.empty()) return true;
    Analysis analysis{context, sema};
    llvm::json::Array objects;
    std::set<const clang::CXXRecordDecl*> owners;
    for (const auto* field : roots.fields) {
        const auto* owner = llvm::cast<clang::CXXRecordDecl>(field->getParent());
        const bool heap = field->getType()->isPointerType();
        const std::string name = owner->getQualifiedNameAsString() + "::" + field->getNameAsString();
        unsigned capacity = 0;
        for (const auto* attr : field->specific_attrs<clang::AnnotateAttr>()) {
            const std::string text = attr->getAnnotation().str();
            const std::string prefix = "CPPHDL_HLS_CAPACITY=";
            if (text.compare(0, prefix.size(), prefix) != 0) continue;
            const auto result = std::from_chars(text.data() + prefix.size(), text.data() + text.size(), capacity);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) capacity = 0;
        }
        if (!capacity) analysis.issues.push_back(llvm::json::Object{{"code", "missing_capacity"}, {"object", name}});
        const bool ownsHeap = heap && field->hasInClassInitializer() &&
            llvm::isa<clang::CXXNewExpr>(field->getInClassInitializer()->IgnoreParenImpCasts());
        if (heap && !ownsHeap)
            analysis.issues.push_back(llvm::json::Object{{"code", "unproven_object_ownership"}, {"object", name}});
        objects.push_back(llvm::json::Object{{"name", name}, {"type", field->getType().getAsString()},
            {"storage", heap ? "raw_port" : "registers"}, {"capacity", capacity},
            {"source", location(field->getLocation(), context)}, {"heap_owner_proven", ownsHeap}});
        analysis.type(field->getType());
        if (owners.insert(owner).second) {
            for (auto* method : owner->methods()) {
                if (!method->hasBody()) continue;
                Calls calls;
                calls.TraverseStmt(method->getBody());
                for (auto* fn : calls.callees)
                    if (inStd(fn->getDeclContext())) analysis.add(fn);
            }
        }
        if (field->hasInClassInitializer()) {
            Calls calls;
            calls.TraverseStmt(field->getInClassInitializer());
            for (auto* fn : calls.callees) analysis.add(fn);
        }
    }
    analysis.collect();
    analysis.issues.push_back(llvm::json::Object{{"code", "storage_lowering_unimplemented"},
        {"detail", "Real STL object layout, allocator calls, pointer aliases and memory-dependent control flow require bounded storage lowering and scheduling. No container replacement is permitted."}});
    llvm::errs() << "HLS error: actual std::container methods were collected (" << analysis.methods.size()
                 << " functions); bounded object/pointer storage lowering and memory scheduling are not implemented.\n"
                 << "HLS: no RTL emitted; see hls-analysis.json in the generated directory.\n";
    units.push_back(llvm::json::Object{{"objects", std::move(objects)}, {"methods", std::move(analysis.methods)},
        {"records", std::move(analysis.layouts)}, {"issues", std::move(analysis.issues)}});
    return false;
}

bool writeStdContainerAnalysis(const std::string& directory)
{
    if (units.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) { llvm::errs() << "HLS: cannot create analysis directory: " << ec.message() << '\n'; return false; }
    std::ofstream file(std::filesystem::path(directory) / "hls-analysis.json");
    std::string text;
    llvm::raw_string_ostream stream(text);
    stream << llvm::formatv("{0:2}", llvm::json::Value(llvm::json::Object{
        {"schema", 1}, {"status", "unsupported_rtl"}, {"translation_units", std::move(units)}}));
    file << text << '\n';
    if (!file) { llvm::errs() << "HLS: cannot write analysis report\n"; return false; }
    return true;
}
}

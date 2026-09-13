#include "LifecycleChecks.h"
#include "Project.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
using namespace clang;

bool isProcess(const std::string& name, const std::string& phase)
{
    if (name == phase || name == phase + "_neg") {
        return true;
    }
    for (const auto& clock : currProject->clocks) {
        if (name == phase + "_" + clock.name || name == phase + "_neg_" + clock.name) {
            return true;
        }
    }
    return false;
}

bool isModule(const CXXRecordDecl* record)
{
    if (!record || !(record = record->getDefinition())) {
        return false;
    }
    if (record->getQualifiedNameAsString() == "cpphdl::Module") {
        return true;
    }
    for (const auto& base : record->bases()) {
        if (isModule(base.getType()->getAsCXXRecordDecl())) {
            return true;
        }
    }
    return false;
}

using Methods = std::map<std::string, const CXXMethodDecl*>;

void collectMembers(const CXXRecordDecl* record, Methods& methods,
    std::vector<const FieldDecl*>& fields, std::set<const CXXRecordDecl*>& visited)
{
    if (!record || !(record = record->getDefinition())
        || !visited.insert(record).second
        || record->getQualifiedNameAsString() == "cpphdl::Module") {
        return;
    }
    // Derived declarations hide base methods; explicitly qualified base calls
    // are followed separately when examining the method body.
    for (const auto* method : record->methods()) {
        methods.emplace(method->getNameAsString(), method);
    }
    for (const auto* field : record->fields()) {
        fields.push_back(field);
    }
    for (const auto& base : record->bases()) {
        collectMembers(base.getType()->getAsCXXRecordDecl(), methods, fields, visited);
    }
}

const Expr* strip(const Expr* expr)
{
    while (expr) {
        expr = expr->IgnoreParenImpCasts();
        if (const auto* cast = dyn_cast<CastExpr>(expr)) {
            expr = cast->getSubExpr();
        } else {
            break;
        }
    }
    return expr;
}

bool isThis(const Expr* expr)
{
    expr = strip(expr);
    if (const auto* unary = dyn_cast_or_null<UnaryOperator>(expr)) {
        return unary->getOpcode() == UO_Deref && isThis(unary->getSubExpr());
    }
    return isa_and_nonnull<CXXThisExpr>(expr);
}

const FieldDecl* receiverField(const Expr* expr)
{
    expr = strip(expr);
    if (const auto* index = dyn_cast_or_null<ArraySubscriptExpr>(expr)) {
        return receiverField(index->getBase());
    }
    if (const auto* index = dyn_cast_or_null<CXXOperatorCallExpr>(expr)) {
        if (index->getOperator() == OO_Subscript && index->getNumArgs()) {
            return receiverField(index->getArg(0));
        }
    }
    if (const auto* member = dyn_cast_or_null<MemberExpr>(expr)) {
        if (isThis(member->getBase())) {
            return dyn_cast<FieldDecl>(member->getMemberDecl());
        }
    }
    return nullptr;
}

using Calls = std::map<const FieldDecl*, std::set<std::string>>;

class ProcessCalls {
public:
    ProcessCalls(ASTContext& context, Sema& sema) : context(context), sema(sema) {}

    void method(const CXXMethodDecl* decl)
    {
        if (decl && visited.insert(decl->getCanonicalDecl()).second) {
            // An unused method of a template base can have a definition only
            // in the pattern. Inspect its concrete body, not an empty stub.
            if (!decl->getBody() && decl->getTemplateInstantiationPattern()) {
                sema.InstantiateFunctionDefinition(decl->getLocation(),
                    const_cast<CXXMethodDecl*>(decl), true);
            }
            statement(decl->getBody());
        }
    }

    Calls calls;

private:
    void statement(const Stmt* stmt)
    {
        if (!stmt || isa<LambdaExpr>(stmt)) {
            return;
        }
        // A call in an explicitly disabled branch does not satisfy the check.
        if (const auto* branch = dyn_cast<IfStmt>(stmt)) {
            bool condition;
            if (!branch->getCond()->isValueDependent()
                && branch->getCond()->EvaluateAsBooleanCondition(condition, context)) {
                statement(branch->getInit());
                statement(branch->getCond());
                statement(condition ? branch->getThen() : branch->getElse());
                return;
            }
        }
        if (const auto* call = dyn_cast<CallExpr>(stmt)) {
            const auto* decl = dyn_cast_or_null<CXXMethodDecl>(call->getDirectCallee());
            const auto* member = dyn_cast_or_null<MemberExpr>(strip(call->getCallee()));
            if (decl && member) {
                const Expr* object = member->getBase();
                if (const auto* field = receiverField(object)) {
                    calls[field].insert(decl->getNameAsString());
                } else if (isThis(object)) {
                    method(decl);
                }
            }
        }
        for (const auto* child : stmt->children()) {
            statement(child);
        }
    }

    ASTContext& context;
    Sema& sema;
    std::set<const CXXMethodDecl*> visited;
};

const CXXRecordDecl* elementRecord(QualType type, ASTContext& context)
{
    while (const auto* array = context.getAsArrayType(type)) {
        type = array->getElementType();
    }
    const auto* record = type->getAsCXXRecordDecl();
    const auto* specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(record);
    if (specialization
        && specialization->getSpecializedTemplate()->getQualifiedNameAsString() == "cpphdl::array") {
        const auto& args = specialization->getTemplateArgs();
        if (args.size() > 1 && args[1].getKind() == TemplateArgument::Type) {
            return elementRecord(args[1].getAsType(), context);
        }
    }
    return record;
}

void report(const CXXRecordDecl* record, const FieldDecl* field,
    const std::string& category, const std::string& call, const std::string& phase,
    ASTContext& context)
{
    const std::string heading = "MISSED CALL FOUND";
    const std::string location = "WARNING: " + context.getRecordType(record).getAsString()
        + " at " + field->getLocation().printToString(context.getSourceManager());
    const std::string detail = "Missing " + category + ": " + field->getNameAsString()
        + "." + call + "() from " + phase + " (or its clock/edge variant)";
    const size_t width = std::max({size_t(68), location.size() + 6, detail.size() + 6});
    const size_t left = (width - heading.size() - 4) / 2;
    const size_t right = width - heading.size() - 4 - left;
    std::cerr << "/" << std::string(left, '*') << " " << heading << " "
              << std::string(right, '*') << "/\n";
    for (const auto& line : {location, detail}) {
        std::cerr << "/* " << line << " " << std::string(width - line.size() - 5, '*') << "/\n";
    }
    std::cerr << "/" << std::string(width - 2, '*') << "/\n";
}

void check(const CXXRecordDecl* record, ASTContext& context, Sema& sema)
{
    Methods methods;
    std::vector<const FieldDecl*> fields;
    std::set<const CXXRecordDecl*> visited;
    collectMembers(record, methods, fields, visited);
    ProcessCalls work(context, sema), strobe(context, sema);
    for (const auto& [name, decl] : methods) {
        if (isProcess(name, "_work")) {
            work.method(decl);
        }
        if (isProcess(name, "_strobe")) {
            strobe.method(decl);
        }
    }
    for (const auto* field : fields) {
        const auto* type = elementRecord(field->getType(), context);
        if (!type) {
            continue;
        }
        const std::string name = type->getQualifiedNameAsString();
        if (name == "cpphdl::reg" || name == "cpphdl::memory") {
            const std::string call = name == "cpphdl::reg" ? "strobe" : "apply";
            if (!strobe.calls[field].count(call)) {
                report(record, field, call == "strobe" ? "register strobe" : "memory apply",
                    call, "_strobe()", context);
            }
        } else if (isModule(type)) {
            Methods childMethods;
            std::vector<const FieldDecl*> childFields;
            std::set<const CXXRecordDecl*> childVisited;
            collectMembers(type, childMethods, childFields, childVisited);
            for (const auto& [childName, decl] : childMethods) {
                if (isProcess(childName, "_work") && !work.calls[field].count(childName)) {
                    report(record, field, "child work", childName, "_work()", context);
                }
                if (isProcess(childName, "_strobe") && !strobe.calls[field].count(childName)) {
                    report(record, field, "child strobe", childName, "_strobe()", context);
                }
            }
        }
    }
}

class ModuleChecks : public RecursiveASTVisitor<ModuleChecks> {
public:
    ModuleChecks(ASTContext& context, Sema& sema) : context(context), sema(sema) {}
    bool shouldVisitTemplateInstantiations() const { return true; }

    bool VisitCXXRecordDecl(CXXRecordDecl* record)
    {
        if (record->isThisDeclarationADefinition() && !record->isDependentContext()
            && isModule(record) && visited.insert(record->getCanonicalDecl()).second) {
            check(record, context, sema);
        }
        return true;
    }

private:
    ASTContext& context;
    Sema& sema;
    std::unordered_set<const CXXRecordDecl*> visited;
};
} // namespace

void checkModuleLifecycleCalls(clang::ASTContext& context, clang::Sema& sema)
{
    ModuleChecks(context, sema).TraverseDecl(context.getTranslationUnitDecl());
}

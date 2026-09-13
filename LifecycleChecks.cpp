#include "LifecycleChecks.h"
#include "Project.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"

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

const Stmt* methodBody(const CXXMethodDecl* decl)
{
    if (const auto* body = decl->getBody()) {
        return body;
    }
    const auto* pattern = decl->getTemplateInstantiationPattern();
    return pattern ? pattern->getBody() : nullptr;
}

const TagDecl* recordPattern(const RecordDecl* record)
{
    if (const auto* cxxRecord = dyn_cast<CXXRecordDecl>(record)) {
        if (const auto* pattern = cxxRecord->getTemplateInstantiationPattern()) {
            record = pattern;
        }
    }
    return record->getCanonicalDecl();
}

class ProcessCalls {
public:
    ProcessCalls(ASTContext& context, const Methods& methods,
        const std::vector<const FieldDecl*>& fields)
        : context(context), methods(methods), fields(fields) {}

    void method(const CXXMethodDecl* decl)
    {
        if (decl && visited.insert(decl->getCanonicalDecl()).second) {
            // Never instantiate here: parsing is over and Sema's scopes may
            // already be gone. Unused template methods still have a pattern.
            statement(methodBody(decl));
        }
    }

    Calls calls;

private:
    const FieldDecl* concreteField(const FieldDecl* field) const
    {
        if (!field || !field->getParent()->isDependentContext()) {
            return field;
        }
        for (const auto* candidate : fields) {
            if (candidate->getName() == field->getName()
                && recordPattern(candidate->getParent()) == recordPattern(field->getParent())) {
                return candidate;
            }
        }
        return field;
    }

    void memberCall(const Expr* object, const std::string& name,
        const CXXMethodDecl* decl, NestedNameSpecifier* qualifier = nullptr)
    {
        if (const auto* field = receiverField(object)) {
            calls[concreteField(field)].insert(name);
        } else if (!object || isThis(object)) {
            if (decl) {
                method(decl);
            } else if (qualifier && qualifier->getAsType()) {
                // A qualified Base<T>::helper() in an uninstantiated body.
                const auto* type = qualifier->getAsType();
                const auto* record = type->getAsCXXRecordDecl();
                if (!record) {
                    if (const auto* specialization = type->getAs<TemplateSpecializationType>()) {
                        if (const auto* templ = dyn_cast_or_null<ClassTemplateDecl>(
                                specialization->getTemplateName().getAsTemplateDecl())) {
                            record = templ->getTemplatedDecl();
                        }
                    }
                }
                if (record) {
                    Methods baseMethods;
                    std::vector<const FieldDecl*> baseFields;
                    std::set<const CXXRecordDecl*> baseVisited;
                    collectMembers(record, baseMethods, baseFields, baseVisited);
                    if (auto it = baseMethods.find(name); it != baseMethods.end()) {
                        method(it->second);
                    }
                }
            } else if (auto it = methods.find(name); it != methods.end()) {
                method(it->second);
            }
        }
    }

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
                memberCall(member->getBase(), decl->getNameAsString(), decl);
            } else if (const auto* dependent = dyn_cast_or_null<CXXDependentScopeMemberExpr>(
                    strip(call->getCallee()))) {
                memberCall(dependent->isImplicitAccess() ? nullptr : dependent->getBase(),
                    dependent->getMember().getAsString(), nullptr, dependent->getQualifier());
            } else if (const auto* dependent = dyn_cast_or_null<DependentScopeDeclRefExpr>(
                    strip(call->getCallee()))) {
                memberCall(nullptr, dependent->getDeclName().getAsString(), nullptr,
                    dependent->getQualifier());
            }
        }
        for (const auto* child : stmt->children()) {
            statement(child);
        }
    }

    ASTContext& context;
    const Methods& methods;
    const std::vector<const FieldDecl*>& fields;
    std::set<const CXXMethodDecl*> visited;
};

bool hasLifecycleEffect(const Stmt* stmt, ASTContext& context,
    std::set<const CXXMethodDecl*>& active)
{
    if (!stmt || isa<NullStmt>(stmt)) {
        return false;
    }
    if (const auto* block = dyn_cast<CompoundStmt>(stmt)) {
        for (const auto* child : block->body()) {
            if (hasLifecycleEffect(child, context, active)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* ret = dyn_cast<ReturnStmt>(stmt)) {
        return ret->getRetValue() != nullptr;
    }
    if (const auto* branch = dyn_cast<IfStmt>(stmt)) {
        bool value;
        if (!branch->getCond()->isValueDependent()
            && branch->getCond()->EvaluateAsBooleanCondition(value, context)) {
            return hasLifecycleEffect(branch->getInit(), context, active)
                || hasLifecycleEffect(value ? branch->getThen() : branch->getElse(), context, active);
        }
    }
    if (const auto* call = dyn_cast<CXXMemberCallExpr>(stmt)) {
        const auto* decl = call->getMethodDecl();
        if (decl && methodBody(decl) && active.insert(decl->getCanonicalDecl()).second) {
            const bool effect = hasLifecycleEffect(methodBody(decl), context, active);
            active.erase(decl->getCanonicalDecl());
            if (effect || call->getImplicitObjectArgument()->HasSideEffects(context)) {
                return true;
            }
            for (const auto* arg : call->arguments()) {
                if (arg->HasSideEffects(context)) {
                    return true;
                }
            }
            return false;
        }
    }
    // Only suppress provably empty processes, not unknown or external calls.
    return true;
}

bool requiresLifecycleCall(const CXXMethodDecl* decl, ASTContext& context)
{
    const auto* body = methodBody(decl);
    std::set<const CXXMethodDecl*> active{decl->getCanonicalDecl()};
    return !body || hasLifecycleEffect(body, context, active);
}

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

void check(const CXXRecordDecl* record, ASTContext& context)
{
    Methods methods;
    std::vector<const FieldDecl*> fields;
    std::set<const CXXRecordDecl*> visited;
    collectMembers(record, methods, fields, visited);
    ProcessCalls work(context, methods, fields), strobe(context, methods, fields);
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
                if ((!isProcess(childName, "_work") && !isProcess(childName, "_strobe"))
                    || !requiresLifecycleCall(decl, context)) {
                    continue;
                }
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
    explicit ModuleChecks(ASTContext& context) : context(context) {}
    bool shouldVisitTemplateInstantiations() const { return true; }

    bool VisitCXXRecordDecl(CXXRecordDecl* record)
    {
        if (record->isThisDeclarationADefinition() && !record->isDependentContext()
            && isModule(record) && visited.insert(record->getCanonicalDecl()).second) {
            records.push_back(record);
            for (const auto& base : record->bases()) {
                if (const auto* baseRecord = base.getType()->getAsCXXRecordDecl()) {
                    if (const auto* definition = baseRecord->getDefinition()) {
                        moduleBases.insert(definition->getCanonicalDecl());
                    }
                }
            }
        }
        return true;
    }

    bool VisitFieldDecl(FieldDecl* field)
    {
        if (const auto* record = elementRecord(field->getType(), context)) {
            if (isModule(record)) {
                if (const auto* definition = record->getDefinition()) {
                    composedModules.insert(definition->getCanonicalDecl());
                }
            }
        }
        return true;
    }

    void checkLeafModules()
    {
        // A module base can deliberately leave its lifecycle to a derived
        // implementation layer.  Checking every intermediate record reports
        // those inherited fields repeatedly even though the generated leaf
        // module contains the required calls.  Any omission still appears on
        // the most-derived module, where all inherited fields are collected.
        // A base that is also used as a child remains independently checked.
        for (const auto* record : records) {
            const auto* canonical = record->getCanonicalDecl();
            if (!moduleBases.count(canonical) || composedModules.count(canonical)) {
                check(record, context);
            }
        }
    }

private:
    ASTContext& context;
    std::unordered_set<const CXXRecordDecl*> visited;
    std::unordered_set<const CXXRecordDecl*> moduleBases;
    std::unordered_set<const CXXRecordDecl*> composedModules;
    std::vector<const CXXRecordDecl*> records;
};
} // namespace

void checkModuleLifecycleCalls(clang::ASTContext& context)
{
    ModuleChecks checks(context);
    checks.TraverseDecl(context.getTranslationUnitDecl());
    checks.checkLeafModules();
}

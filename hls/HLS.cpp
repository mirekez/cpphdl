#include "HLS.h"
#include "StdContainers.h"
#include "AstClocked.h"
#include "../Project.h"
#include "../Module.h"
#include "../Method.h"
#include "../Field.h"
#include "../Expr.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string_view>

namespace cpphdl::hls {
namespace {
bool enabled = false;
bool failed = false;
using Key = std::pair<std::string, std::string>;
std::set<Key> active;
std::map<Key, unsigned> limits;

void error(const std::string& text)
{
    std::cerr << "HLS error: " << text << '\n';
    failed = true;
}

bool localCall(const Expr& expr)
{
    return expr.type == Expr::EXPR_CALL ||
        (expr.type == Expr::EXPR_MEMBERCALL &&
         !(expr.flags & Expr::FLAG_MODULE_INSTANCE_METHOD) &&
         !expr.sub.empty() && (expr.sub[0].type == Expr::EXPR_NONE ||
                               expr.sub[0].value == "_this"));
}

void visit(Expr& expr, const std::function<void(Expr&)>& fn)
{
    fn(expr);
    for (auto& sub : expr.sub) visit(sub, fn);
}

std::string numbered(const std::string& name, unsigned depth)
{
    return name + "_hls_" + std::to_string(depth);
}

bool sameType(const Expr& a, const Expr& b)
{
    if (a.type != b.type || a.value != b.value || a.sub.size() != b.sub.size()) return false;
    for (size_t i = 0; i < a.sub.size(); ++i)
        if (!sameType(a.sub[i], b.sub[i])) return false;
    return true;
}
}

void enable() { enabled = true; }

bool prepare(clang::ASTContext& context, clang::Sema& sema)
{
    if (!enabled) return true;
    prepareClocked(context, sema);
    if (!inspectStdContainers(context, sema)) {
        failed = true;
        return false;
    }
    return true;
}

bool writeAnalysis(const std::string& directory)
{
    return writeStdContainerAnalysis(directory);
}

bool enterMethod(Module& module, const std::string& name,
                 const clang::CXXMethodDecl& declaration)
{
    const Key key{module.name, name};
    if (active.count(key)) {
        if (!enabled) error(module.name + "::" + name + " is recursive; use --hls with a bounded recursion contract");
        return false;
    }
    active.insert(key);
    if (enabled) {
        unsigned limit = 10;
        for (const auto* attr : declaration.specific_attrs<clang::AnnotateAttr>()) {
            const std::string annotation = attr->getAnnotation().str();
            const std::string_view prefix = "CPPHDL_HLS_MAX_RECURSION=";
            if (annotation.compare(0, prefix.size(), prefix) != 0) continue;
            const char* begin = annotation.data() + prefix.size();
            const char* end = annotation.data() + annotation.size();
            const auto parsed = std::from_chars(begin, end, limit);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !limit || limit > 64) {
                error(module.name + "::" + name + ": MAX_RECURSION must be between 1 and 64");
            }
        }
        limits[key] = limit;
    }
    return true;
}

void leaveMethod(Module& module, const std::string& name)
{
    active.erase({module.name, name});
}

bool lower(Project& project)
{
    if (!clockedSucceeded()) return false;
    if (failed || !enabled) return !failed;
    for (auto& module : project.modules) {
        std::map<std::string, size_t> methods;
        for (size_t i = 0; i < module.methods.size(); ++i) methods[module.methods[i].name] = i;
        const size_t count = module.methods.size();
        std::vector<std::set<size_t>> reach(count);
        for (size_t i = 0; i < count; ++i) {
            for (auto& stmt : module.methods[i].statements) visit(stmt, [&](Expr& expr) {
                if (localCall(expr) && methods.count(expr.value)) reach[i].insert(methods.at(expr.value));
            });
        }
        // Transitive closure also identifies mutually recursive components.
        for (size_t k = 0; k < count; ++k)
            for (size_t i = 0; i < count; ++i)
                if (reach[i].count(k)) reach[i].insert(reach[k].begin(), reach[k].end());
        std::map<std::string, unsigned> recursive;
        for (size_t i = 0; i < count; ++i) {
            if (!reach[i].count(i)) continue;
            const auto& method = module.methods[i];
            const unsigned limit = limits.at({module.name, method.name});
            recursive[method.name] = limit;
            const auto fallback = methods.find(method.name + "_limit");
            if (fallback == methods.end()) {
                error(module.name + "::" + method.name + " requires a nonrecursive " + method.name + "_limit method");
                continue;
            }
            const auto& terminal = module.methods[fallback->second];
            bool signature = terminal.ret.size() == method.ret.size() && terminal.arguments.size() == method.arguments.size();
            for (size_t j = 0; signature && j < method.ret.size(); ++j)
                signature = sameType(method.ret[j], terminal.ret[j]);
            for (size_t j = 0; signature && j < method.arguments.size(); ++j)
                signature = sameType(method.arguments[j].expr, terminal.arguments[j].expr);
            if (!signature) error(module.name + "::" + terminal.name + " must have the same signature as " + method.name);
            for (size_t j : reach[fallback->second])
                if (reach[j].count(j)) error(module.name + "::" + terminal.name + " must not call recursive methods");
            for (size_t j : reach[i]) {
                if (reach[j].count(i) && limits.at({module.name, module.methods[j].name}) != limit)
                    error(module.name + ": mutually recursive methods must use the same MAX_RECURSION");
            }
        }
        if (failed) return false;
        std::vector<Method> result;
        for (size_t i = 0; i < count; ++i) {
            const auto& original = module.methods[i];
            const bool unfold = recursive.count(original.name);
            const unsigned copies = unfold ? recursive.at(original.name) : 1;
            if (unfold) {
                // Keep the public entry name for calls through child module instances.
                Method entry = original;
                Expr call{numbered(original.name, 0), Expr::EXPR_CALL};
                for (const auto& arg : original.arguments)
                    call.sub.push_back(Expr{arg.name, Expr::EXPR_VAR});
                entry.statements = original.ret.empty() ? std::vector<Expr>{call}
                    : std::vector<Expr>{Expr{"", Expr::EXPR_RETURN, {call}}};
                result.push_back(std::move(entry));
            }
            for (unsigned depth = 0; depth < copies; ++depth) {
                Method copy = original;
                if (unfold) copy.name = numbered(original.name, depth);
                if (methods.count(copy.name) && copy.name != original.name) {
                    error(module.name + ": generated method name collision: " + copy.name);
                    return false;
                }
                for (auto& stmt : copy.statements) visit(stmt, [&](Expr& expr) {
                    if (!localCall(expr) || !recursive.count(expr.value)) return;
                    const size_t target = methods.at(expr.value);
                    const bool sameComponent = unfold && reach[target].count(i) && reach[i].count(target);
                    expr.value = sameComponent && depth + 1 == copies ? expr.value + "_limit"
                        : numbered(expr.value, sameComponent ? depth + 1 : 0);
                });
                result.push_back(std::move(copy));
            }
        }
        module.methods = std::move(result);
        for (const auto& [name, limit] : recursive)
            std::cout << "HLS: " << module.name << "::" << name << " unfolded into " << limit << " functions\n";
    }
    return !failed;
}
}

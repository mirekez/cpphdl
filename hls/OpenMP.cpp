#include "OpenMP.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include <functional>
#include <set>
#include <map>
#include <stdexcept>

namespace cpphdl::hls {
using namespace clang;
namespace {
[[noreturn]] void unsupported(const std::string& message) {
    throw std::runtime_error("OpenMP HLS: " + message);
}
const Expr* strip(const Expr* expr) { return expr->IgnoreParenImpCasts(); }
const VarDecl* variable(const Expr* expr) {
    auto* ref = dyn_cast<DeclRefExpr>(strip(expr));
    if (!ref || !isa<VarDecl>(ref->getDecl())) unsupported("expected a scalar variable");
    return cast<VarDecl>(ref->getDecl());
}
int64_t constant(ASTContext& ctx, const Expr* expr) {
    Expr::EvalResult result;
    if (!expr || expr->HasSideEffects(ctx) || !expr->EvaluateAsInt(result, ctx) ||
        !result.Val.getInt().isSignedIntN(63))
        unsupported("loop bounds, strides and collapse count must be constant integers");
    return result.Val.getInt().getSExtValue();
}
OpenMPLoop loopPlan(ASTContext& ctx, const ForStmt* loop) {
    auto* decl = dyn_cast_or_null<DeclStmt>(loop->getInit());
    if (!decl || !decl->isSingleDecl() || !isa<VarDecl>(decl->getSingleDecl()))
        unsupported("declare the induction variable in the for initializer");
    auto* var = cast<VarDecl>(decl->getSingleDecl());
    if (!var->getType()->isIntegerType() || ctx.getTypeSize(var->getType()) > 32)
        unsupported("induction variables must be integers of at most 32 bits");
    int64_t start = constant(ctx, var->getInit());
    auto* cond = dyn_cast_or_null<BinaryOperator>(loop->getCond()->IgnoreParenImpCasts());
    if (!cond || variable(cond->getLHS()) != var ||
        (cond->getOpcode() != BO_LT && cond->getOpcode() != BO_LE &&
         cond->getOpcode() != BO_GT && cond->getOpcode() != BO_GE))
        unsupported("expected induction-variable <, <=, > or >= constant");
    int64_t bound = constant(ctx, cond->getRHS()), step = 0;
    auto* inc = strip(loop->getInc());
    if (auto* unary = dyn_cast<UnaryOperator>(inc)) {
        if (variable(unary->getSubExpr()) != var || !unary->isIncrementDecrementOp())
            unsupported("unsupported loop stride");
        step = unary->isIncrementOp() ? 1 : -1;
    } else if (auto* binary = dyn_cast<CompoundAssignOperator>(inc)) {
        if (variable(binary->getLHS()) != var ||
            (binary->getOpcode() != BO_AddAssign && binary->getOpcode() != BO_SubAssign))
            unsupported("unsupported loop stride");
        step = constant(ctx, binary->getRHS());
        if (binary->getOpcode() == BO_SubAssign) step = -step;
    } else unsupported("unsupported loop stride");
    bool ascending = cond->getOpcode() == BO_LT || cond->getOpcode() == BO_LE;
    if (!step || (step > 0) != ascending) unsupported("stride does not approach the loop bound");
    OpenMPLoop result{var, {}};
    auto active = [&](int64_t i) {
        switch (cond->getOpcode()) {
        case BO_LT: return i < bound;
        case BO_LE: return i <= bound;
        case BO_GT: return i > bound;
        default: return i >= bound;
        }
    };
    unsigned width = ctx.getTypeSize(var->getType());
    int64_t minimum = var->getType()->isSignedIntegerType() ? -(int64_t(1) << (width - 1)) : 0;
    int64_t maximum = var->getType()->isSignedIntegerType() ? (int64_t(1) << (width - 1)) - 1 : (int64_t(1) << width) - 1;
    for (int64_t i = start; active(i); i += step) {
        if (i < minimum || i > maximum || step > maximum - i || step < minimum - i)
            unsupported("induction arithmetic overflows its source type");
        if (result.values.size() == 256) unsupported("parallel loop exceeds 256 hardware lanes");
        result.values.push_back(i);
    }
    return result;
}
void validateBody(ASTContext& ctx, const OpenMPPlan& plan) {
    std::set<const ValueDecl*> local, independent;
    for (auto* var : plan.privates) independent.insert(var);
    for (auto* var : plan.firstprivates) independent.insert(var);
    for (auto& reduction : plan.reductions) independent.insert(reduction.variable);
    std::set<const VarDecl*> indices;
    for (auto& loop : plan.loops) indices.insert(loop.variable);
    std::function<void(const Stmt*)> declarations = [&](const Stmt* stmt) {
        if (!stmt) return;
        if (auto* ds = dyn_cast<DeclStmt>(stmt)) for (auto* decl : ds->decls()) {
            auto* var = dyn_cast<VarDecl>(decl);
            if (!var || var->isStaticLocal() || var->getType()->isReferenceType() || var->getType()->isPointerType())
                unsupported("lane locals must be automatic values, not aliases or pointers");
            local.insert(var);
        }
        for (auto* child : stmt->children()) declarations(child);
    };
    declarations(plan.body);
    struct ArrayAccess { const ValueDecl* root; std::string signature; };
    auto access = [&](const Expr* expr, bool disjoint = false) {
        ArrayAccess result{};
        std::set<const VarDecl*> used;
        expr = strip(expr);
        while (auto* sub = dyn_cast<ArraySubscriptExpr>(expr)) {
            auto* index = strip(sub->getIdx());
            auto* array = ctx.getAsConstantArrayType(strip(sub->getBase())->getType());
            if (!array) unsupported("parallel memory accesses require fixed arrays, not pointers");
            uint64_t extent = array->getSize().getLimitedValue();
            auto* ref = dyn_cast<DeclRefExpr>(index);
            auto* indexVar = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
            if (indexVar && indices.count(indexVar)) {
                auto* var = cast<VarDecl>(ref->getDecl());
                for (const auto& loop : plan.loops) if (loop.variable == var)
                    for (auto i : loop.values) if (i < 0 || uint64_t(i) >= extent)
                        unsupported("parallel array index is outside the fixed array");
                used.insert(var);
                result.signature += ":" + var->getNameAsString();
            } else {
                auto i = constant(ctx, index);
                if (i < 0 || uint64_t(i) >= extent) unsupported("parallel array index is outside the fixed array");
                result.signature += ":constant" + std::to_string(i);
            }
            expr = strip(sub->getBase());
        }
        if (auto* ref = dyn_cast<DeclRefExpr>(expr)) result.root = ref->getDecl();
        else if (auto* member = dyn_cast<MemberExpr>(expr)) result.root = member->getMemberDecl();
        if (!result.root || !result.root->getType()->isArrayType())
            unsupported("parallel memory accesses require fixed arrays, not pointers");
        if (disjoint && used != indices) unsupported("array writes must use every induction variable as a direct index");
        return result;
    };
    std::map<const ValueDecl*, std::string> written;
    auto write = [&](const Expr* expr) {
        expr = strip(expr);
        if (auto* ref = dyn_cast<DeclRefExpr>(expr)) {
            if (!local.count(ref->getDecl()) && !independent.count(ref->getDecl()))
                unsupported("shared scalar write needs a reduction or private clause");
        } else if (isa<ArraySubscriptExpr>(expr)) {
            auto item = access(expr, true);
            if (!local.count(item.root)) {
                auto [found, inserted] = written.emplace(item.root, item.signature);
                if (!inserted && found->second != item.signature)
                    unsupported("cannot prove disjoint array writes");
            }
        } else unsupported("unsupported parallel write target");
    };
    std::function<void(const Stmt*)> check = [&](const Stmt* stmt) {
        if (!stmt) return;
        if (isa<CallExpr>(stmt) || isa<CXXConstructExpr>(stmt) || isa<ReturnStmt>(stmt) ||
            isa<GotoStmt>(stmt) || isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) ||
            isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt) || isa<OMPExecutableDirective>(stmt))
            unsupported("lane bodies currently require straight-line arithmetic/branches without calls or nested control loops");
        if (auto* binary = dyn_cast<BinaryOperator>(stmt); binary && binary->isAssignmentOp()) write(binary->getLHS());
        if (auto* unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp()) write(unary->getSubExpr());
            if (unary->getOpcode() == UO_AddrOf || unary->getOpcode() == UO_Deref)
                unsupported("address escapes and pointer dereferences are not parallel lane operations");
        }
        for (auto* child : stmt->children()) check(child);
    };
    check(plan.body);
    // OpenMP private/firstprivate values belong to host threads, which may
    // process several iterations. Hardware lanes must not depend on that
    // assignment: reject a private read before a lane-local definite write.
    std::set<const ValueDecl*> tracked(plan.privates.begin(), plan.privates.end());
    tracked.insert(plan.firstprivates.begin(), plan.firstprivates.end());
    std::set<const ValueDecl*> mutated;
    std::function<void(const Stmt*)> mutations = [&](const Stmt* stmt) {
        if (!stmt) return;
        const Expr* target = nullptr;
        if (auto* b = dyn_cast<BinaryOperator>(stmt); b && b->isAssignmentOp()) target = b->getLHS();
        if (auto* u = dyn_cast<UnaryOperator>(stmt); u && u->isIncrementDecrementOp()) target = u->getSubExpr();
        if (target) if (auto* ref = dyn_cast<DeclRefExpr>(strip(target))) mutated.insert(ref->getDecl());
        for (auto* child : stmt->children()) mutations(child);
    };
    mutations(plan.body);
    using Defined = std::set<const ValueDecl*>;
    std::function<void(const Stmt*, Defined&)> definite = [&](const Stmt* stmt, Defined& defined) {
        if (!stmt) return;
        if (auto* branch = dyn_cast<IfStmt>(stmt)) {
            definite(branch->getInit(), defined);
            definite(branch->getConditionVariableDeclStmt(), defined);
            definite(branch->getCond(), defined);
            auto yes = defined, no = defined;
            definite(branch->getThen(), yes); definite(branch->getElse(), no);
            for (auto* var : yes) if (no.count(var)) defined.insert(var);
            return;
        }
        if (auto* b = dyn_cast<BinaryOperator>(stmt); b && b->isAssignmentOp()) {
            definite(b->getRHS(), defined);
            auto* ref = dyn_cast<DeclRefExpr>(strip(b->getLHS()));
            if (ref && tracked.count(ref->getDecl())) {
                if (b->isCompoundAssignmentOp() && !defined.count(ref->getDecl()))
                    unsupported("private/firstprivate value carries state between host iterations");
                defined.insert(ref->getDecl()); return;
            }
        }
        if (auto* ref = dyn_cast<DeclRefExpr>(stmt); ref && tracked.count(ref->getDecl()) && !defined.count(ref->getDecl()))
            unsupported("private/firstprivate value must be written in each lane before reading");
        // Conditional and short-circuit expressions cannot establish a write
        // on every path, even if one arm happens to initialize a private value.
        if (auto* conditional = dyn_cast<ConditionalOperator>(stmt)) {
            definite(conditional->getCond(), defined);
            auto yes = defined, no = defined;
            definite(conditional->getTrueExpr(), yes); definite(conditional->getFalseExpr(), no);
            for (auto* var : yes) if (no.count(var)) defined.insert(var);
            return;
        }
        if (auto* b = dyn_cast<BinaryOperator>(stmt); b && (b->getOpcode() == BO_LAnd || b->getOpcode() == BO_LOr)) {
            definite(b->getLHS(), defined); auto optional = defined;
            definite(b->getRHS(), optional); return;
        }
        for (auto* child : stmt->children()) definite(child, defined);
    };
    Defined defined;
    for (auto* var : plan.firstprivates) if (!mutated.count(var)) defined.insert(var);
    definite(plan.body, defined);
    std::function<void(const Stmt*)> reads = [&](const Stmt* stmt) {
        if (!stmt) return;
        if (auto* sub = dyn_cast<ArraySubscriptExpr>(stmt)) {
            auto item = access(sub);
            if (written.count(item.root) && written.at(item.root) != item.signature)
                unsupported("cross-lane read/write dependence");
            // The inner subscripts of a multidimensional access are not separate accesses.
            reads(sub->getIdx()); return;
        }
        for (auto* child : stmt->children()) reads(child);
    };
    reads(plan.body);
}
}
OpenMPPlan planOpenMP(ASTContext& ctx, const OMPExecutableDirective* directive) {
    using namespace llvm::omp;
    auto kind = directive->getDirectiveKind();
    if (kind != OMPD_parallel_for && kind != OMPD_parallel_for_simd && kind != OMPD_simd)
        unsupported("supported directives are parallel for, parallel for simd and simd");
    OpenMPPlan plan;
    unsigned collapsed = 1;
    auto append = [](auto* clause, auto& list) {
        for (auto* expr : llvm::make_range(clause->varlist_begin(), clause->varlist_end())) {
            auto* var = variable(expr);
            if (!var->getType()->isIntegerType()) unsupported("private/reduction variables must be scalar integers");
            list.push_back(var);
        }
    };
    for (auto* clause : directive->clauses()) {
        if (auto* c = dyn_cast<OMPPrivateClause>(clause)) append(c, plan.privates);
        else if (auto* c = dyn_cast<OMPFirstprivateClause>(clause)) append(c, plan.firstprivates);
        else if (auto* c = dyn_cast<OMPCollapseClause>(clause)) {
            int64_t count = constant(ctx, c->getNumForLoops());
            if (count < 1 || count > 3) unsupported("collapse count must be in 1..3");
            collapsed = unsigned(count);
        } else if (auto* c = dyn_cast<OMPReductionClause>(clause)) {
            if (c->getModifier() != OMPC_REDUCTION_unknown) unsupported("reduction modifiers are unsupported");
            auto name = c->getNameInfo().getName();
            std::string op, identity;
            if (name.getNameKind() == DeclarationName::CXXOperatorName) {
                switch (name.getCXXOverloadedOperator()) {
                case OO_Plus: op = "+"; break;
                case OO_Star: op = "*"; break;
                case OO_Amp: op = "&"; break;
                case OO_Pipe: op = "|"; break;
                case OO_Caret: op = "^"; break;
                default: break;
                }
            }
            if (op == "+" || op == "|" || op == "^") identity = "0";
            else if (op == "*") identity = "1";
            else if (op == "&") identity = "'1";
            else unsupported("supported integer reductions are +, *, &, | and ^");
            for (auto* expr : llvm::make_range(c->varlist_begin(), c->varlist_end())) {
                auto* var = variable(expr);
                if (!var->getType()->isUnsignedIntegerType() || ctx.getTypeSize(var->getType()) > 64)
                    unsupported("reductions require unsigned integers of at most 64 bits");
                plan.reductions.push_back({var, op, identity});
            }
        } else if (auto* c = dyn_cast<OMPScheduleClause>(clause)) {
            if (c->getScheduleKind() != OMPC_SCHEDULE_static) unsupported("hardware lanes support only schedule(static)");
            if (c->getChunkSize() && constant(ctx, c->getChunkSize()) < 1)
                unsupported("static schedule chunks must be positive constant integers");
        } else if (auto* c = dyn_cast<OMPDefaultClause>(clause)) {
            if (c->getDefaultKind() != OMP_DEFAULT_shared && c->getDefaultKind() != OMP_DEFAULT_none)
                unsupported("supported defaults are shared and none; declare private variables explicitly");
        } else if (!isa<OMPSharedClause>(clause))
            unsupported("unsupported clause on parallel loop");
    }
    const Stmt* body = directive->getRawStmt();
    for (unsigned depth = 0; depth < collapsed; ++depth) {
        while (auto* compound = dyn_cast<CompoundStmt>(body)) {
            if (compound->size() != 1) unsupported("collapse requires perfectly nested loops");
            body = *compound->body_begin();
        }
        auto* loop = dyn_cast<ForStmt>(body);
        if (!loop) unsupported("expected a canonical for loop");
        plan.loops.push_back(loopPlan(ctx, loop));
        if (plan.loops.back().values.size() && plan.lanes > 256 / plan.loops.back().values.size())
            unsupported("parallel loop exceeds 256 hardware lanes");
        plan.lanes *= plan.loops.back().values.size();
        body = loop->getBody();
    }
    plan.body = body;
    validateBody(ctx, plan);
    return plan;
}
}

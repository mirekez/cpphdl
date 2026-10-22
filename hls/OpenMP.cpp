#include "OpenMP.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include <functional>
#include <set>
#include <map>
#include <stdexcept>
#include <limits>
#include "clang/AST/DeclCXX.h"

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
    auto* cond = loop->getCond() ? dyn_cast<BinaryOperator>(loop->getCond()->IgnoreParenImpCasts()) : nullptr;
    if (!cond || variable(cond->getLHS()) != var ||
        (cond->getOpcode() != BO_LT && cond->getOpcode() != BO_LE &&
         cond->getOpcode() != BO_GT && cond->getOpcode() != BO_GE))
        unsupported("expected induction-variable <, <=, > or >= constant");
    int64_t bound = constant(ctx, cond->getRHS()), step = 0;
    if (!loop->getInc()) unsupported("unsupported loop stride");
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
        if (result.values.size() == OpenMPMaxLanes) unsupported("parallel loop exceeds 4096 hardware lanes");
        result.values.push_back(i);
    }
    return result;
}
void validateBody(ASTContext& ctx, const OpenMPPlan& plan, const OpenMPCallResolver& resolver, const StaticIntegers& inherited) {
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
    // Calls are allowed only after a transitive scalar-value purity check.
    std::set<const FunctionDecl*> checking, checked;
    std::function<void(const CallExpr*)> pureCall;
    pureCall = [&](const CallExpr* call) {
        auto* fn = resolver ? resolver(call) : call->getDirectCallee();
        if (!fn) unsupported("pure helpers require a direct call with a visible body");
        if (auto* method = dyn_cast<CXXMethodDecl>(fn); method && !method->isStatic())
            unsupported("pure helpers must be free or static value functions");
        auto scalar = [](QualType type) { return !type.isVolatileQualified() &&
            (type->isIntegerType() || type->isRealFloatingType()); };
        if (!scalar(fn->getReturnType())) unsupported("pure helpers require an arithmetic value result");
        std::set<const ValueDecl*> own;
        for (auto* param : fn->parameters()) {
            if (!scalar(param->getType())) unsupported("pure helpers require arithmetic value parameters, not aliases");
            own.insert(param);
        }
        auto* canonical = fn->getCanonicalDecl();
        if (checked.count(canonical)) return;
        if (!checking.insert(canonical).second) unsupported("recursive pure helpers are not parallel lane operations");
        auto* body = fn->getBody();
        if (!body) unsupported("pure helpers require a visible instantiated body");
        std::function<void(const Stmt*)> collect = [&](const Stmt* stmt) {
            if (!stmt) return;
            if (auto* ds = dyn_cast<DeclStmt>(stmt)) for (auto* declaration : ds->decls()) {
                auto* var = dyn_cast<VarDecl>(declaration);
                if (!var || var->isStaticLocal() || !scalar(var->getType()))
                    unsupported("pure helpers require automatic arithmetic locals");
                own.insert(var);
            }
            for (auto* child : stmt->children()) collect(child);
        };
        collect(body);
        std::function<void(const Stmt*)> check = [&](const Stmt* stmt) {
            if (!stmt) return;
            if (auto* call = dyn_cast<CallExpr>(stmt)) pureCall(call);
            if (auto* loop = dyn_cast<ForStmt>(stmt)) (void)loopPlan(ctx, loop);
            if (isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) || isa<GotoStmt>(stmt) ||
                isa<CXXConstructExpr>(stmt) || isa<CXXNewExpr>(stmt) || isa<CXXDeleteExpr>(stmt) ||
                isa<CXXThrowExpr>(stmt) || isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt) ||
                isa<OMPExecutableDirective>(stmt)) unsupported("pure helpers require bounded arithmetic control flow");
            if (auto* ref = dyn_cast<DeclRefExpr>(stmt)) if (auto* var = dyn_cast<VarDecl>(ref->getDecl()))
                if (!own.count(var) && !var->isConstexpr()) unsupported("pure helper reads a mutable or nonconstant global");
            const Expr* target = nullptr;
            if (auto* b = dyn_cast<BinaryOperator>(stmt); b && b->isAssignmentOp()) target = b->getLHS();
            if (auto* u = dyn_cast<UnaryOperator>(stmt)) {
                if (u->isIncrementDecrementOp()) target = u->getSubExpr();
                if (u->getOpcode()==UO_AddrOf || u->getOpcode()==UO_Deref)
                    unsupported("pure helpers cannot escape addresses or dereference pointers");
            }
            if (target) {
                auto* ref = dyn_cast<DeclRefExpr>(strip(target));
                if (!ref || !own.count(ref->getDecl())) unsupported("pure helper writes outside its value locals");
            }
            for (auto* child : stmt->children()) check(child);
        };
        check(body);checking.erase(canonical);checked.insert(canonical);
    };
    std::function<void(const Stmt*)> check = [&](const Stmt* stmt) {
        if (!stmt) return;
        if (auto* call = dyn_cast<CallExpr>(stmt)) pureCall(call);
        if (auto* loop = dyn_cast<ForStmt>(stmt)) (void)loopPlan(ctx, loop);
        if (isa<CXXConstructExpr>(stmt) || isa<ReturnStmt>(stmt) || isa<GotoStmt>(stmt) ||
            isa<WhileStmt>(stmt) || isa<DoStmt>(stmt) || isa<BreakStmt>(stmt) ||
            isa<ContinueStmt>(stmt) || isa<OMPExecutableDirective>(stmt))
            unsupported("lane bodies require bounded arithmetic/branches and pure scalar helpers");
        if (auto* unary = dyn_cast<UnaryOperator>(stmt))
            if (unary->getOpcode()==UO_AddrOf || unary->getOpcode()==UO_Deref)
                unsupported("address escapes and pointer dereferences are not parallel lane operations");
        for (auto* child : stmt->children()) check(child);
    };
    check(plan.body);
    // Enumerate bounded affine accesses using the actual lane coordinates and
    // constant guards. Ownership checks are O(accesses log accesses), not an
    // all-pairs comparison. Read-only neighbor snapshots are legal; in-place
    // cross-lane recurrences remain races and are rejected.
    using Address = std::vector<int64_t>;
    using Root = std::vector<const ValueDecl*>;
    std::map<Root, std::map<Address, unsigned>> writers;
    struct Read { Root root; Address address; unsigned lane; };
    std::vector<Read> reads;
    auto access = [&](const Expr* expr, const StaticIntegers& values) {
        Root root;
        Address address;
        expr = strip(expr);
        while (auto* sub = dyn_cast<ArraySubscriptExpr>(expr)) {
            auto* array = ctx.getAsConstantArrayType(strip(sub->getBase())->getType());
            if (!array) unsupported("parallel memory accesses require fixed arrays, not pointers");
            auto i = staticInteger(ctx, sub->getIdx(), values);
            if (!i) unsupported("parallel array indices must be bounded static affine expressions");
            if (*i < 0 || uint64_t(*i) >= array->getSize().getLimitedValue())
                unsupported("parallel array index is outside the fixed array");
            address.push_back(*i);expr = strip(sub->getBase());
        }
        while (auto* member = dyn_cast<MemberExpr>(expr)) {
            root.push_back(member->getMemberDecl());expr=strip(member->getBase());
        }
        if(auto* ref=dyn_cast<DeclRefExpr>(expr)) root.push_back(ref->getDecl());
        else if(isa<CXXThisExpr>(expr)) root.push_back(nullptr);
        else unsupported("parallel array roots require distinct fixed value objects");
        if(root.empty() || !root.front() || !root.front()->getType().getNonReferenceType()->isArrayType())
            unsupported("parallel memory accesses require fixed arrays, not pointers");
        return std::make_pair(root, address);
    };
    StaticIntegers values = inherited;
    unsigned lane = 0;
    std::function<void(const Stmt*)> memory;
    auto write = [&](const Expr* target, bool read) {
        target = strip(target);
        if (auto* ref = dyn_cast<DeclRefExpr>(target)) {
            if (indices.count(dyn_cast<VarDecl>(ref->getDecl()))) unsupported("parallel induction variables cannot be modified in a lane");
            if (!local.count(ref->getDecl()) && !independent.count(ref->getDecl()))
                unsupported("shared scalar write needs a reduction or private clause");
        } else if (auto* sub = dyn_cast<ArraySubscriptExpr>(target)) {
            auto [root,address] = access(sub,values);
            if (!local.count(root.back())) {
                auto [owner,fresh] = writers[root].emplace(address,lane);
                if (!fresh && owner->second!=lane) unsupported("cannot prove disjoint array writes");
                if (read) reads.push_back({root,address,lane});
            }
        } else unsupported("unsupported parallel write target");
    };
    memory = [&](const Stmt* stmt) {
        if (!stmt) return;
        if (auto* branch = dyn_cast<IfStmt>(stmt)) {
            memory(branch->getInit());memory(branch->getConditionVariableDeclStmt());memory(branch->getCond());
            auto known=staticInteger(ctx,branch->getCond(),values);
            if (!known || *known) memory(branch->getThen());
            if (!known || !*known) memory(branch->getElse());
            return;
        }
        if (auto* conditional = dyn_cast<ConditionalOperator>(stmt)) {
            memory(conditional->getCond());auto known=staticInteger(ctx,conditional->getCond(),values);
            if (!known || *known) memory(conditional->getTrueExpr());
            if (!known || !*known) memory(conditional->getFalseExpr());return;
        }
        if (auto* loop = dyn_cast<ForStmt>(stmt)) {
            auto nested=loopPlan(ctx,loop);
            for(auto i:nested.values) {values[nested.variable]=i;memory(loop->getBody());}
            values.erase(nested.variable);return;
        }
        if (auto* binary=dyn_cast<BinaryOperator>(stmt)) {
            if (binary->isAssignmentOp()) {memory(binary->getRHS());write(binary->getLHS(),binary->isCompoundAssignmentOp());return;}
            if (binary->getOpcode()==BO_LAnd || binary->getOpcode()==BO_LOr) {
                memory(binary->getLHS());auto known=staticInteger(ctx,binary->getLHS(),values);
                if (!known || (*known!=0)==(binary->getOpcode()==BO_LAnd)) memory(binary->getRHS());return;
            }
        }
        if (auto* unary=dyn_cast<UnaryOperator>(stmt); unary && unary->isIncrementDecrementOp()) {write(unary->getSubExpr(),true);return;}
        if (auto* sub=dyn_cast<ArraySubscriptExpr>(stmt)) {
            auto [root,address]=access(sub,values);
            if (!local.count(root.back())) reads.push_back({root,address,lane});
            return;
        }
        for(auto* child:stmt->children()) memory(child);
    };
    std::function<void(unsigned)> lanes = [&](unsigned depth) {
        if(depth<plan.loops.size()) {
            auto& loop=plan.loops[depth];
            for(auto i:loop.values) {values[loop.variable]=i;lanes(depth+1);}
            values.erase(loop.variable);return;
        }
        memory(plan.body);++lane;
    };
    lanes(0);
    for(const auto& read:reads) {
        auto arrays=writers.find(read.root);if(arrays==writers.end()) continue;
        auto owner=arrays->second.find(read.address);
        if(owner!=arrays->second.end() && owner->second!=read.lane) unsupported("cross-lane read/write dependence");
    }
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

}
}
OpenMPLoop planStaticLoop(ASTContext& ctx,const ForStmt* loop) { return loopPlan(ctx,loop); }
std::optional<int64_t> staticInteger(ASTContext& ctx,const Expr* input,const StaticIntegers& values) {
    if (!input) return {};
    auto* expr=input->IgnoreParens();
    // A known result does not make evaluating the expression optional.
    if (expr->HasSideEffects(ctx)) return {};
    if (auto* ref=dyn_cast<DeclRefExpr>(expr)) {
        if(auto* var=dyn_cast<VarDecl>(ref->getDecl())) {auto found=values.find(var);if(found!=values.end()) return found->second;}
    }
    Expr::EvalResult result;
    if (!expr->HasSideEffects(ctx) && expr->EvaluateAsInt(result,ctx) && result.Val.getInt().isSignedIntN(63))
        return result.Val.getInt().getSExtValue();
    auto normalize=[&](int64_t value)->std::optional<int64_t> {
        if(!expr->getType()->isIntegerType()) return {};
        unsigned width=ctx.getTypeSize(expr->getType());
        if(width>64) return {};
        if(width==1) return value!=0;
        if(width<64) {
            uint64_t mask=(uint64_t(1)<<width)-1;uint64_t raw=uint64_t(value)&mask;
            if(expr->getType()->isSignedIntegerType() && (raw&(uint64_t(1)<<(width-1)))) raw|=~mask;
            return int64_t(raw);
        }
        // StaticIntegers cannot represent an unsigned value above INT64_MAX.
        // Leave its comparison/arithmetic to normal typed lowering.
        if (expr->getType()->isUnsignedIntegerType() && value < 0) return {};
        return value;
    };
    if(auto* cast=dyn_cast<CastExpr>(expr)) {auto v=staticInteger(ctx,cast->getSubExpr(),values);return v?normalize(*v):v;}
    if(auto* unary=dyn_cast<UnaryOperator>(expr)) {
        auto v=staticInteger(ctx,unary->getSubExpr(),values);if(!v) return {};
        switch(unary->getOpcode()) {case UO_Plus:return normalize(*v);case UO_Minus:if(*v!=std::numeric_limits<int64_t>::min())return normalize(-*v);return {};
            case UO_LNot:return !*v;case UO_Not:return normalize(~*v);default:return {};}
    }
    if(auto* cond=dyn_cast<ConditionalOperator>(expr)) {
        auto test=staticInteger(ctx,cond->getCond(),values);
        return test?staticInteger(ctx,*test?cond->getTrueExpr():cond->getFalseExpr(),values):std::optional<int64_t>{};
    }
    if(auto* binary=dyn_cast<BinaryOperator>(expr)) {
        auto a=staticInteger(ctx,binary->getLHS(),values),b=staticInteger(ctx,binary->getRHS(),values);
        if(binary->getOpcode()==BO_LAnd) {if((a && !*a)||(b && !*b))return 0;if(a&&b)return 1;return {};}
        if(binary->getOpcode()==BO_LOr) {if((a && *a)||(b && *b))return 1;if(a&&b)return 0;return {};}
        if(!a||!b) return {};
        __int128 wide=0;
        switch(binary->getOpcode()) {
        case BO_Add:wide=__int128(*a)+*b;break;case BO_Sub:wide=__int128(*a)-*b;break;case BO_Mul:wide=__int128(*a)* *b;break;
        case BO_Div:if(!*b)return {};wide=__int128(*a)/ *b;break;case BO_Rem:if(!*b)return {};wide=__int128(*a)% *b;break;
        case BO_Shl:if(*b<0||*b>=63||*a<0)return {};wide=__int128(*a)<<*b;break;
        case BO_Shr:if(*b<0||*b>=64)return {};return normalize(*a>>*b);
        case BO_And:return normalize(*a&*b);case BO_Or:return normalize(*a|*b);case BO_Xor:return normalize(*a^*b);
        case BO_LT:return *a<*b;case BO_LE:return *a<=*b;case BO_GT:return *a>*b;case BO_GE:return *a>=*b;
        case BO_EQ:return *a==*b;case BO_NE:return *a!=*b;default:return {};
        }
        if(wide<std::numeric_limits<int64_t>::min() || wide>std::numeric_limits<int64_t>::max())return {};
        return normalize(int64_t(wide));
    }
    return {};
}
OpenMPPlan planOpenMP(ASTContext& ctx, const OMPExecutableDirective* directive, const OpenMPCallResolver& resolver, const StaticIntegers& inherited) {
    using namespace llvm::omp;
    auto kind = directive->getDirectiveKind();
    if (kind != OMPD_parallel_for && kind != OMPD_parallel_for_simd && kind != OMPD_simd)
        unsupported("supported directives are parallel for, parallel for simd and simd");
    OpenMPPlan plan;
    unsigned collapsed = 1;
    auto append = [](auto* clause, auto& list) {
        for (auto* expr : llvm::make_range(clause->varlist_begin(), clause->varlist_end())) {
            auto* var = variable(expr);
            if (!var->getType()->isIntegerType() && !(var->getType()->isRealFloatingType() && var->getType()->isSpecificBuiltinType(BuiltinType::Float)))
                unsupported("private variables must be scalar integers or FP32");
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
                bool fp32=var->getType()->isSpecificBuiltinType(BuiltinType::Float);
                if (fp32 && op!="+" && op!="*") unsupported("FP32 reductions support + and *");
                if (!fp32 && (!var->getType()->isUnsignedIntegerType() || ctx.getTypeSize(var->getType())>64))
                    unsupported("reductions require unsigned integers of at most 64 bits or FP32");
                plan.reductions.push_back({var,op,fp32 && op=="*" ? "32'h3f800000" : identity});
            }
        } else if (auto* c = dyn_cast<OMPScheduleClause>(clause)) {
            if (c->getScheduleKind() != OMPC_SCHEDULE_static) unsupported("hardware lanes support only schedule(static)");
            if (c->getChunkSize() && constant(ctx, c->getChunkSize()) < 1)
                unsupported("static schedule chunks must be positive constant integers");
        } else if (auto* c = dyn_cast<OMPDefaultClause>(clause)) {
            if (c->getDefaultKind() != OMP_DEFAULT_shared && c->getDefaultKind() != OMP_DEFAULT_none)
                unsupported("supported defaults are shared and none; declare private variables explicitly");
        } else if (auto* c = dyn_cast<OMPIfClause>(clause)) {
            // OpenMP if controls host team creation, not whether loop work
            // executes. Constant team policy never changes hardware lanes.
            (void)constant(ctx, c->getCondition());
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
        if (plan.loops.back().values.size() && plan.lanes > OpenMPMaxLanes / plan.loops.back().values.size())
            unsupported("parallel loop exceeds 4096 hardware lanes");
        plan.lanes *= plan.loops.back().values.size();
        body = loop->getBody();
    }
    plan.body = body;
    validateBody(ctx, plan, resolver, inherited);
    return plan;
}
}

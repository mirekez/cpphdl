#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ParentMapContext.h"

#include "helpers.h"

#include "Project.h"
#include "Module.h"
#include "Debug.h"
#include "Expr.h"
#include "Struct.h"
#include "Enum.h"
#include "Field.h"

#include <algorithm>
#include <cctype>
#include <iostream>

unsigned debugIndent = 0;
bool cpphdlDebugEnabled = false;

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp, cpphdl::Struct* st = nullptr);
std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp, bool notThis = false);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* ctx, llvm::StringRef QualifiedName);
void addEnumPackageImport(EnumDecl* ED, std::vector<cpphdl::Import>& imports)
{
    if (!ED) return;
    const std::string name = genTypeName(ED->getQualifiedNameAsString());
    if (std::find_if(imports.begin(), imports.end(), [&](auto& imp){ return imp.name == name; }) == imports.end())
        imports.emplace_back(name);
    if (std::find_if(currProject->enums.begin(), currProject->enums.end(), [&](auto& en){ return en.name == name; }) != currProject->enums.end())
        return;
    cpphdl::Enum en{name, ED->getQualifiedNameAsString()};
    QualType integerType = ED->getIntegerType();
    if (!integerType.isNull()) {
        en.bitWidth = ED->getASTContext().getTypeSize(integerType);
        en.isSigned = integerType->isSignedIntegerType();
    }
    for (const EnumConstantDecl* ECD : ED->enumerators()) {
        if (ECD->getInitExpr()) {
            en.fields.emplace_back(cpphdl::Field{ECD->getName().str(),
                {std::to_string(ECD->getInitVal().getSExtValue()), cpphdl::Expr::EXPR_NUM}});
        } else {
            en.fields.emplace_back(cpphdl::Field{ECD->getName().str()});
        }
    }
    currProject->enums.emplace_back(std::move(en));
}
//const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext* ctx);

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

// Conservative suffix pruning: these exits all leave the current case.
// Do not look through nested switches/loops: they consume their own breaks.
// Emitting an unreachable suffix is harmless; dropping a reachable one is not.
static bool statementTerminatesSwitchCase(const Stmt* statement)
{
    if (!statement) return false;
    if (isa<BreakStmt>(statement) || isa<ReturnStmt>(statement) || isa<ContinueStmt>(statement))
        return true;
    if (const auto* attributed = dyn_cast<AttributedStmt>(statement))
        return statementTerminatesSwitchCase(attributed->getSubStmt());
    if (const auto* compound = dyn_cast<CompoundStmt>(statement)) {
        for (const Stmt* child : compound->body())
            if (statementTerminatesSwitchCase(child)) return true;
    }
    if (const auto* ifStmt = dyn_cast<IfStmt>(statement))
        return ifStmt->getElse() && statementTerminatesSwitchCase(ifStmt->getThen()) &&
            statementTerminatesSwitchCase(ifStmt->getElse());
    return false;
}

// A trailing break is already implemented by the SV case boundary. Remove it
// without looking through a nested loop/switch. Ordinary switches then need
// neither a named block nor disable statements in synthesizable always blocks.
static void removeTrailingSwitchBreak(cpphdl::Expr& expression, const std::string& target)
{
    using E = cpphdl::Expr;
    if (expression.type == E::EXPR_BREAK && expression.value == target) {
        expression = E{};
    } else if (expression.type == E::EXPR_BODY) {
        for (auto it = expression.sub.rbegin(); it != expression.sub.rend(); ++it) {
            if (it->type == E::EXPR_NONE) continue;
            removeTrailingSwitchBreak(*it, target);
            break;
        }
    } else if (expression.type == E::EXPR_IF) {
        for (size_t index = 1; index < expression.sub.size(); ++index)
            removeTrailingSwitchBreak(expression.sub[index], target);
    }
}

static bool cpphdlRecordHasValueFieldsImpl(const CXXRecordDecl* RD, std::unordered_set<const CXXRecordDecl*>& visited)
{
    if (!RD) {
        return false;
    }

    const CXXRecordDecl* def = RD->getDefinition();
    // An alias can mention a specialization without instantiating its body.
    // Clang's bases() requires definition data even when there are no fields.
    if (!def) return false;
    RD = def;
    if (!visited.insert(RD).second) {
        return false;
    }

    for (const Decl* D : RD->decls()) {
        if (isa<FieldDecl>(D)) {
            return true;
        }
    }

    for (const CXXBaseSpecifier& base : RD->bases()) {
        if (const CXXRecordDecl* baseRD = base.getType()->getAsCXXRecordDecl()) {
            if (cpphdlRecordHasValueFieldsImpl(baseRD, visited)) {
                return true;
            }
        }
    }

    return false;
}

static std::string qualifierLocalName(std::string qualifier)
{
    if (size_t pos = qualifier.find('<'); pos != std::string::npos) {
        qualifier.resize(pos);
    }
    if (size_t pos = qualifier.rfind("::"); pos != std::string::npos) {
        qualifier.erase(0, pos + 2);
    }
    return genTypeName(qualifier);
}

static bool recordOrBaseMatchesQualifier(const CXXRecordDecl* record, const std::string& qualifier)
{
    if (!record || qualifier.empty()) {
        return false;
    }

    std::string recordName = record->getQualifiedNameAsString();
    str_replace(recordName, "::", "_");
    if (qualifier == recordName || qualifierLocalName(qualifier) == qualifierLocalName(record->getQualifiedNameAsString())) {
        return true;
    }

    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(record)) {
        const std::string primary = spec->getSpecializedTemplate()->getQualifiedNameAsString();
        if (qualifierLocalName(qualifier) == qualifierLocalName(primary)) {
            return true;
        }
    }

    for (const auto& base : record->bases()) {
        if (recordOrBaseMatchesQualifier(base.getType()->getAsCXXRecordDecl(), qualifier)) {
            return true;
        }
    }
    return false;
}

static bool qualifierIsLocalModuleContext(const std::string& qualifier, const CXXRecordDecl* parent, const cpphdl::Module* mod)
{
    if (recordOrBaseMatchesQualifier(parent, qualifier)) {
        return true;
    }
    if (parent) {
        for (const Decl* decl : parent->decls()) {
            if (const auto* alias = dyn_cast<TypeAliasDecl>(decl)) {
                if (alias->getNameAsString() == qualifier || alias->getNameAsString() == qualifierLocalName(qualifier)) {
                    return true;
                }
            }
        }
    }
    if (mod && std::any_of(mod->aliases.begin(), mod->aliases.end(), [&](const cpphdl::Field& alias) {
            return alias.name == qualifier || alias.name == qualifierLocalName(qualifier);
        })) {
        return true;
    }
    return false;
}

bool cpphdlRecordHasValueFields(const CXXRecordDecl* RD)
{
    std::unordered_set<const CXXRecordDecl*> visited;
    return cpphdlRecordHasValueFieldsImpl(RD, visited);
}

static bool cpphdlRecordIsOrDerivesFromModule(const CXXRecordDecl* RD, std::unordered_set<const CXXRecordDecl*>& visited)
{
    if (!RD) {
        return false;
    }
    const CXXRecordDecl* def = RD->getDefinition();
    RD = def ? def : RD;
    if (!visited.insert(RD).second) {
        return false;
    }
    if (RD->getQualifiedNameAsString() == "cpphdl::Module") {
        return true;
    }
    for (const CXXBaseSpecifier& base : RD->bases()) {
        if (cpphdlRecordIsOrDerivesFromModule(base.getType()->getAsCXXRecordDecl(), visited)) {
            return true;
        }
    }
    return false;
}

static bool cpphdlRecordShouldExportAsStruct(const CXXRecordDecl* RD)
{
    if (!RD || !RD->hasDefinition() || RD->isLambda()) {
        return false;
    }
    if (RD->getQualifiedNameAsString().find("cpphdl::") == 0 ||
        RD->getQualifiedNameAsString().find("std::") == 0) {
        return false;
    }

    std::unordered_set<const CXXRecordDecl*> visited;
    if (cpphdlRecordIsOrDerivesFromModule(RD, visited)) {
        return false;
    }

    // Empty user structs still need SV packages when they participate in
    // template-specialized aggregate types. exportStruct() gives them a pad bit.
    return true;
}

static bool cpphdlRecordShouldExportAsStruct(const CXXRecordDecl* RD, Helpers& hlp)
{
    return cpphdlRecordShouldExportAsStruct(RD) && !isCurrentOrBaseRecord(hlp.parent, RD);
}

static void importStructForStaticMethodOwner(const CXXMethodDecl* method, Helpers& hlp)
{
    if (!method || !hlp.mod) {
        return;
    }

    CXXRecordDecl* parent = const_cast<CXXRecordDecl*>(method->getParent());
    if (!parent || !parent->hasDefinition() ||
        parent->getQualifiedNameAsString().find("cpphdl::") == 0 ||
        parent->getQualifiedNameAsString().find("std::") == 0) {
        return;
    }

    if (auto* moduleClass = hlp.lookupQualifiedRecord("cpphdl::Module")) {
        if (parent->isDerivedFrom(moduleClass)) {
            return;
        }
    }
    if (isCurrentOrBaseRecord(hlp.parent, parent)) {
        return;
    }

    auto importRecord = [&](CXXRecordDecl* record) {
        if (!cpphdlRecordShouldExportAsStruct(record, hlp)) {
            return;
        }
        auto st = exportStruct(record, hlp);
        if (std::find_if(hlp.mod->imports.begin(), hlp.mod->imports.end(),
                [&](auto& imp){ return imp.name == st.name; }) == hlp.mod->imports.end()) {
            hlp.mod->imports.emplace_back(st.name);
            currProject->structs.emplace_back(std::move(st));
        }
    };

    importRecord(parent);

    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(parent)) {
        for (const auto& arg : spec->getTemplateArgs().asArray()) {
            if (arg.getKind() != TemplateArgument::Type) {
                continue;
            }
            QualType qt = arg.getAsType().getNonReferenceType();
            importRecord(hlp.resolveCXXRecordDecl(qt));
        }
    }

    // Static helper methods are emitted into the caller module by putMethod(),
    // but their bodies can still reference static constexpr fields through the
    // owner package, for example Helper_pkg::OFFSET.
}

static bool isCombFuncName(const std::string& name)
{
    return cpphdl_is_comb_func_name(name);
}

static std::string flattenedCombSignalName(const cpphdl::Module* mod, const std::string& combFuncName)
{
    if (!mod) {
        return "";
    }

    std::string signal = combFuncName;
    if (isCombFuncName(signal)) {
        signal = cpphdl_comb_func_signal_name(signal);
    }
    else if (!str_ending(signal, "_comb")) {
        return "";
    }

    size_t pos = signal.rfind("___");
    if (pos != std::string::npos) {
        signal = signal.substr(pos + 3);
    }

    for (const auto& var : mod->vars) {
        std::string suffix = std::string("___") + signal;
        if (var.name == signal || str_ending(var.name, suffix.c_str())) {
            return var.name;
        }
    }

    return "";
}

static const ClassTemplateDecl* findClassTemplateDecl(DeclContext* dc, llvm::StringRef name)
{
    if (!dc) {
        return nullptr;
    }

    for (Decl* decl : dc->decls()) {
        if (const auto* ctd = dyn_cast<ClassTemplateDecl>(decl)) {
            if (ctd->getName() == name) {
                return ctd;
            }
        }
        if (auto* ns = dyn_cast<NamespaceDecl>(decl)) {
            if (const auto* found = findClassTemplateDecl(ns, name)) {
                return found;
            }
        }
    }
    return nullptr;
}

static std::string dependentQualifierText(const DependentScopeDeclRefExpr* expr, const PrintingPolicy& policy)
{
    std::string text;
    llvm::raw_string_ostream os(text);
    if (expr->getQualifier()) {
        expr->getQualifier()->print(os, policy);
    }
    os.flush();
    while (str_ending(text, "::")) {
        text.resize(text.size() - 2);
    }
    return text;
}

std::string Helpers::castTypeName(QualType QT)
{
    // Use Clang's target ABI, not sanitized C++ spelling (e.g. unsigned
    // long long used to fall through to a 32-bit unsigned cast).
    QT = QT.getNonReferenceType();
    if (QT->isBooleanType()) return "bool";
    if (QT->isIntegralOrEnumerationType()) {
        return std::string(QT->isSignedIntegerOrEnumerationType() ? "cpphdl_i" : "cpphdl_u")
            + std::to_string(ctx->getTypeSize(QT));
    }
    std::string sugared = QT.getAsString(ctx->getPrintingPolicy());
    std::string canonical = QT.getCanonicalType().getAsString(ctx->getPrintingPolicy());
    // A literal width may itself carry a C++ promotion (64'h10). Normalize
    // only literal spellings; symbolic module widths must stay symbolic.
    auto normalizeWidth = [](std::string text) {
        const auto hex = text.find("'h");
        if (hex != std::string::npos
            && text.substr(0, hex).find_first_not_of("0123456789") == std::string::npos) {
            uint64_t width;
            if (!llvm::StringRef(text).drop_front(hex + 2).getAsInteger(16, width))
                return std::to_string(width);
        }
        return text;
    };

    auto isCpphdlSizedType = [](const std::string& name) {
        return name.find("cpphdl::u<") != std::string::npos ||
               name.find("cpphdl::i<") != std::string::npos ||
               name.find("cpphdl::logic<") != std::string::npos ||
               name.find("u<") == 0 ||
               name.find("i<") == 0 ||
               name.find("logic<") == 0;
    };
    auto widthUsesOnlyModuleNames = [&](const std::string& width) {
        for (size_t pos = 0; pos < width.size();) {
            if (!(std::isalpha(static_cast<unsigned char>(width[pos])) || width[pos] == '_')) {
                ++pos;
                continue;
            }
            size_t begin = pos;
            while (pos < width.size() && (std::isalnum(static_cast<unsigned char>(width[pos])) || width[pos] == '_')) {
                ++pos;
            }
            if (begin > 0 && width[begin - 1] == '$') {
                continue;
            }
            std::string token = width.substr(begin, pos - begin);
            // These are SV cast/literal syntax, not free identifiers in the
            // width. Integral promotions can wrap a module parameter in them.
            if (token == "signed" || token == "unsigned"
                || (begin > 0 && width[begin - 1] == '\''
                    && (token[0] == 'h' || token[0] == 'b' || token[0] == 'o' || token[0] == 'd'))) {
                continue;
            }
            auto isKnown = [&](const cpphdl::Field& field) { return field.name == token; };
            bool known = mod &&
                         (std::find_if(mod->parameters.begin(), mod->parameters.end(), isKnown) != mod->parameters.end() ||
                          std::find_if(mod->consts.begin(), mod->consts.end(), isKnown) != mod->consts.end());
            if (!known) {
                return false;
            }
        }
        return true;
    };

    // Keep the sugared cpphdl type for dependent widths like u<clog2(ENTRIES)>.
    // The canonical type can flatten that to cpphdl::u<N>, which loses the
    // SystemVerilog parameter expression and generated an invalid clog2ENTRIES.
    if (isCpphdlSizedType(sugared) || isCpphdlSizedType(canonical)) {
        if (const auto* TST = QT->getAs<TemplateSpecializationType>()) {
            if (const TemplateDecl* TD = TST->getTemplateName().getAsTemplateDecl()) {
                std::string name = genTypeName(TD->getQualifiedNameAsString());
                if (name == "cpphdl_u" || name == "cpphdl_i" || name == "cpphdl_logic") {
                    cpphdl::Expr width;
                    auto args = TST->template_arguments();
                    if (!args.empty()) {
                        ArgToExpr(args[0], width, false);
                    }
                    if (!width.sub.empty()) {
                        std::string width_text = normalizeWidth(width.sub[0].str());
                        if (widthUsesOnlyModuleNames(width_text)) {
                            return name + width_text;
                        }
                    }
                }
            }
        }
        QualType cast_qt = QT.getNonReferenceType();
        cpphdl::Expr type_expr;
        if (templateToExpr(cast_qt, type_expr) && type_expr.value.find("cpphdl_") == 0 && !type_expr.sub.empty()) {
            return type_expr.value + normalizeWidth(type_expr.sub[0].str());
        }
    }

    std::string name = isCpphdlSizedType(sugared) ? sugared : canonical;
    if (name.find("u<") == 0 || name.find("i<") == 0 || name.find("logic<") == 0) {
        name = "cpphdl::" + name;
    }
    return genTypeName(name);
}

static cpphdl::Expr unsupportedLambda(ASTContext& ctx, SourceLocation location)
{
    auto& diagnostics = ctx.getDiagnostics();
    const unsigned id = diagnostics.getCustomDiagID(DiagnosticsEngine::Error,
        "cpphdl: local lambda objects and lambda calls are not supported in RTL conversion; "
        "use a named helper method or inline the operations");
    diagnostics.Report(location, id);
    return cpphdl::Expr{};
}

// Only prove unsignedness for expressions whose RTL representation is known.
// Library conversion operators can disappear during lowering, so their C++
// return type alone is not evidence about the generated signal's signedness.
static bool naturallyUnsigned(const clang::Expr* expr)
{
    expr = expr->IgnoreParens();
    if (isa<IntegerLiteral>(expr) || isa<CXXBoolLiteralExpr>(expr)) return true;
    if (const auto* cast = dyn_cast<CastExpr>(expr)) {
        if (cast->getCastKind() == CK_IntegralCast || cast->getCastKind() == CK_IntegralToBoolean)
            return cast->getType()->isUnsignedIntegerType() || cast->getType()->isBooleanType();
        if (cast->getCastKind() == CK_LValueToRValue || cast->getCastKind() == CK_NoOp)
            return naturallyUnsigned(cast->getSubExpr());
        return false;
    }
    if (const auto* ref = dyn_cast<DeclRefExpr>(expr)) {
        const auto* var = dyn_cast<VarDecl>(ref->getDecl());
        return var && !var->isConstexpr() && var->getType()->isUnsignedIntegerType();
    }
    if (const auto* ref = dyn_cast<MemberExpr>(expr))
        return isa<FieldDecl>(ref->getMemberDecl()) && ref->getType()->isUnsignedIntegerType();
    if (const auto* binary = dyn_cast<BinaryOperator>(expr)) {
        if (binary->isComparisonOp() || binary->isLogicalOp()) return true;
        if (binary->isAssignmentOp()) return naturallyUnsigned(binary->getLHS());
        if (binary->isShiftOp()) return naturallyUnsigned(binary->getLHS());
        if (binary->isAdditiveOp() || binary->isMultiplicativeOp() || binary->isBitwiseOp())
            return naturallyUnsigned(binary->getLHS()) || naturallyUnsigned(binary->getRHS());
        return false;
    }
    if (const auto* unary = dyn_cast<UnaryOperator>(expr))
        return unary->getOpcode() == UO_LNot || naturallyUnsigned(unary->getSubExpr());
    return false;
}

cpphdl::Expr Helpers::valueCast(QualType target, const clang::Expr* operand)
{
    auto lowered = exprToExpr(operand);
    const auto source = operand->getType();
    const auto targetName = castTypeName(target);
    bool unsignedOperand = naturallyUnsigned(operand)
        || (lowered.type == cpphdl::Expr::EXPR_CAST
            && (lowered.value.rfind("cpphdl_u", 0) == 0 || lowered.value.rfind("cpphdl_logic", 0) == 0));
    // A conversion is already self-sized. Do not wrap it again when a
    // comparison or an outer C++ cast requests exactly the same type.
    if (lowered.type == cpphdl::Expr::EXPR_CAST && lowered.value == targetName)
        return lowered;
    const auto* plain = operand->IgnoreParenImpCasts();
    const auto* ref = dyn_cast<DeclRefExpr>(plain);
    const auto* var = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
    const auto* member = dyn_cast<MemberExpr>(plain);
    const bool typedVariable = (var && !var->isConstexpr())
        || (member && isa<FieldDecl>(member->getMemberDecl()));
    if (source->isIntegralOrEnumerationType() && !source->isEnumeralType()
        && ctx->hasSameUnqualifiedType(source, target)
        && typedVariable
        && ctx->hasSameUnqualifiedType(plain->getType(), source)) {
        // Ordinary typed variables already have the C++ width and sign in
        // RTL. Unlike arithmetic, reading one cannot acquire a wider context.
        return lowered;
    }
    if (target->isIntegerType() && !target->isBooleanType()
        && isa<IntegerLiteral>(plain)
        && lowered.type == cpphdl::Expr::EXPR_NUM && lowered.sub.empty()) {
        // Emit a typed literal, not a chain of casts around an unsigned hex
        // literal. Only fold literals, never symbolic module parameters.
        clang::Expr::EvalResult result;
        if (operand->EvaluateAsInt(result, *ctx)) {
            auto bits = result.Val.getInt().extOrTrunc(ctx->getTypeSize(target));
            llvm::SmallString<32> text;
            bits.toString(text, 16, false);
            return cpphdl::Expr{std::to_string(ctx->getTypeSize(target))
                + (target->isSignedIntegerType() ? "'sh" : "'h") + text.str().str(),
                cpphdl::Expr::EXPR_NUM};
        }
    }
    // SV propagates a widening size cast down into arithmetic. C++ first
    // evaluates at the operand's width (including unsigned wrap), then converts.
    // Preserve that boundary before widening, including bit-vector constructors.
    const cpphdl::Expr* sized = &lowered;
    while (sized->type == cpphdl::Expr::EXPR_PAREN && sized->sub.size() == 1)
        sized = &sized->sub[0];
    if ((sized->type == cpphdl::Expr::EXPR_BINARY
            || sized->type == cpphdl::Expr::EXPR_UNARY
            || sized->type == cpphdl::Expr::EXPR_COND)
        && source->isIntegralOrEnumerationType() && !source->isBooleanType()
        && (!target->isIntegralOrEnumerationType() || ctx->getTypeSize(source) < ctx->getTypeSize(target))) {
        lowered = cpphdl::Expr{castTypeName(source), cpphdl::Expr::EXPR_CAST, {std::move(lowered)}};
        lowered.castKeepsUnsigned = unsignedOperand && source->isUnsignedIntegerType();
        unsignedOperand = source->isUnsignedIntegerType();
    }
    if (const auto* type = target->getAs<EnumType>()) {
        if (mod) addEnumPackageImport(type->getDecl(), mod->imports);
        return cpphdl::Expr{"svtype:" + genTypeName(type->getDecl()->getQualifiedNameAsString()),
            cpphdl::Expr::EXPR_CAST, {std::move(lowered)}};
    }
    cpphdl::Expr result{targetName, cpphdl::Expr::EXPR_CAST, {std::move(lowered)}};
    result.castKeepsUnsigned = unsignedOperand;
    return result;
}

cpphdl::Expr Helpers::bitIndexToExpr(const clang::Expr* operand)
{
    // bits() accepts size_t, but an SV select index is self-determined: no
    // enclosing data width can widen its arithmetic. Drop only the final
    // implicit widening to size_t, not explicit/narrowing casts or conversions
    // inside the index expression. All valid C++ bits() indices are nonnegative
    // and in range (the library asserts this), so this widening changes none.
    const auto* cast = dyn_cast<ImplicitCastExpr>(operand->IgnoreParens());
    if (cast && cast->getCastKind() == CK_IntegralCast
        && ctx->hasSameUnqualifiedType(cast->getType(), ctx->getSizeType())
        && cast->getSubExpr()->getType()->isIntegerType()
        && ctx->getTypeSize(cast->getSubExpr()->getType()) <= ctx->getTypeSize(cast->getType())) {
        return exprToExpr(cast->getSubExpr());
    }
    return exprToExpr(operand);
}

cpphdl::Expr Helpers::exprToExpr(const Stmt* E)
{
    const SourceManager &SM = ctx->getSourceManager();
    const LangOptions LangOpts = ctx->getLangOpts();
    SourceLocation StartLoc = E->getBeginLoc();
    SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);
    if (StartLoc.isMacroID()) {
        StartLoc = SM.getSpellingLoc(StartLoc);
        EndLoc   = SM.getSpellingLoc(EndLoc);
    }
    CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);
    DEBUG_AST(debugIndent++, " exprToExpr(" << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "): {"); on_return ret_debug([](){ --debugIndent; });
    on_return ret_debug1([](){ DEBUG_AST1("}"); });

    // General lambda invocation is not a port read: its parameters and
    // captures need proper call lowering. Catch both operator-call syntax and
    // explicit .operator() calls before either loses its argument binding.
    // Do not reject lambda conversion operators used by _ASSIGN bindings.
    if (const auto* call = dyn_cast<CallExpr>(E)) {
        const auto* method = dyn_cast_or_null<CXXMethodDecl>(call->getDirectCallee());
        if (method && method->getParent()->isLambda() &&
            (method->getOverloadedOperator() == OO_Call || method->isLambdaStaticInvoker())) {
            return unsupportedLambda(*ctx, E->getBeginLoc());
        }
    }

    if (auto* FS = dyn_cast<ForStmt>(E)) {
        DEBUG_AST1(" ForStmt");
        breakTargets.emplace_back();
        on_return popBreakTarget([&](){ breakTargets.pop_back(); });

        cpphdl::Expr expr = cpphdl::Expr{"for", cpphdl::Expr::EXPR_FOR};

        // The RTL emitter addresses init/condition/increment/body by position.
        // Omitting a C++ clause must not shift the remaining expressions.
        expr.sub.push_back(FS->getInit() ? exprToExpr(FS->getInit()) : cpphdl::Expr{});
        expr.sub.push_back(FS->getCond() ? exprToExpr(FS->getCond())
                                       : cpphdl::Expr{"1", cpphdl::Expr::EXPR_NUM});
        expr.sub.push_back(FS->getInc() ? exprToExpr(FS->getInc()) : cpphdl::Expr{});

        if (FS->getBody()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(FS->getBody())) {
                for (auto* S : CS->body()) {
                     expr1.sub.push_back(exprToExpr(S));
                }
            } else {
                expr1.sub.push_back(exprToExpr(FS->getBody()));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        return expr;
    }
    if (auto* WS = dyn_cast<WhileStmt>(E)) {
        DEBUG_AST1(" WhileStmt");
        breakTargets.emplace_back();
        on_return popBreakTarget([&](){ breakTargets.pop_back(); });

        cpphdl::Expr expr = cpphdl::Expr{"while", cpphdl::Expr::EXPR_WHILE};

        if (WS->getCond()) {
            expr.sub.push_back(exprToExpr(WS->getCond()));
        }

        if (WS->getBody()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(WS->getBody())) {
                for (auto* S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S));
                }
            } else {
                expr1.sub.push_back(exprToExpr(WS->getBody()));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        return expr;
    }
    if (auto* IS = dyn_cast<IfStmt>(E)) {
        DEBUG_AST1(" IfStmt");

        cpphdl::Expr expr = cpphdl::Expr{"if", cpphdl::Expr::EXPR_IF};

        if (IS->getCond()) {
            expr.sub.push_back(exprToExpr(IS->getCond()));
        }

        if (IS->getThen()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"then", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(IS->getThen())) {
                for (auto* S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S));
                }
            } else {
                expr1.sub.push_back(exprToExpr(IS->getThen()));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        if (IS->getElse()) {
            cpphdl::Expr expr2 = cpphdl::Expr{"else", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(IS->getElse())) {
                for (auto* S : CS->body()) {
                    expr2.sub.push_back(exprToExpr(S));
                }
            } else {
                expr2.sub.push_back(exprToExpr(IS->getElse()));
            }
            expr.sub.emplace_back(std::move(expr2));
        }

        return expr;
    }
    if (auto* SS = dyn_cast<SwitchStmt>(E)) {
        // SV case does not fall through. Give every entry label the C++ suffix
        // it can execute, and translate switch breaks to a named-block exit.
        // Nested loops still own their native break/continue statements.
        auto name = "__cpphdl_switch_" + std::to_string(++switchSerial);
        breakTargets.push_back(name);
        on_return popBreakTarget([&](){ breakTargets.pop_back(); });
        cpphdl::Expr selection{name, cpphdl::Expr::EXPR_SWITCH, {exprToExpr(SS->getCond())}};
        struct Arm { std::string label; std::vector<const Stmt*> statements; };
        std::vector<Arm> arms;
        auto compound = dyn_cast<CompoundStmt>(SS->getBody());
        if (!compound) {
            auto id = ctx->getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error,
                "cpphdl: switch requires a compound body");
            ctx->getDiagnostics().Report(SS->getBeginLoc(), id);
            return {};
        }
        for (const Stmt* child : compound->body()) {
            while (auto label = dyn_cast<SwitchCase>(child)) {
                std::string text = "default";
                if (const auto* item = dyn_cast<CaseStmt>(label)) {
                    text = exprToExpr(item->getLHS()).str();
                    if (item->getRHS())
                        text = "[" + text + ":" + exprToExpr(item->getRHS()).str() + "]";
                }
                arms.push_back({text, {}});
                child = label->getSubStmt();
            }
            if (!arms.empty()) arms.back().statements.push_back(child);
        }
        for (size_t entry = 0; entry < arms.size(); ++entry) {
            cpphdl::Expr arm{arms[entry].label, cpphdl::Expr::EXPR_BODY};
            bool terminated = false;
            for (size_t index = entry; index < arms.size() && !terminated; ++index) {
                for (auto child : arms[index].statements) {
                    arm.sub.push_back(exprToExpr(child));
                    if (statementTerminatesSwitchCase(child)) { terminated = true; break; }
                }
            }
            removeTrailingSwitchBreak(arm, name);
            selection.sub.push_back(std::move(arm));
        }
        bool needsExit = false;
        selection.traverseIf([&](cpphdl::Expr& expression) {
            if (expression.type == cpphdl::Expr::EXPR_BREAK && expression.value == name)
                needsExit = true;
            return false;
        });
        if (!needsExit) selection.value = "switch";
        cpphdl::Expr result{"switch scope", cpphdl::Expr::EXPR_BODY};
        if (SS->getInit()) result.sub.push_back(exprToExpr(SS->getInit()));
        if (SS->getConditionVariableDeclStmt())
            result.sub.push_back(exprToExpr(SS->getConditionVariableDeclStmt()));
        result.sub.push_back(std::move(selection));
        return result;
    }
    if (/*auto* CS =*/ dyn_cast<CaseStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};
    }
    if (isa<BreakStmt>(E)) {
        return cpphdl::Expr{breakTargets.empty() ? "" : breakTargets.back(), cpphdl::Expr::EXPR_BREAK};
    }
    if (isa<ContinueStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_CONTINUE};
    }
    if (const auto* attributed = dyn_cast<AttributedStmt>(E)) {
        return exprToExpr(attributed->getSubStmt());
    }
    if (auto* CS = dyn_cast<CompoundStmt>(E)) {
        DEBUG_AST1(" CompoundStmt");

        cpphdl::Expr expr = cpphdl::Expr{"compound", cpphdl::Expr::EXPR_BODY};
        for (auto* S : CS->body()) {
            expr.sub.push_back(exprToExpr(S));
        }
        return expr;
    }
    if (auto* RS = dyn_cast<ReturnStmt>(E)) {
        DEBUG_AST1(" ReturnStmt");
        if (RS->getRetValue()) {
            return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN, {exprToExpr(RS->getRetValue())}};
        }
        return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN};
    }
    if (dyn_cast<NullStmt>(E)) {
        DEBUG_AST1(" NullStmt");

        cpphdl::Expr expr = cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};
        return expr;
    }
    if (auto* DS = dyn_cast<DeclStmt>(E)) {
        DEBUG_AST1(" DeclStmt");
        auto body = cpphdl::Expr{"", cpphdl::Expr::EXPR_BODY};
        for (Decl* D : DS->decls()) {
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                // Closure fields can be unnamed references. They are not
                // hardware state, and exporting them as structs is invalid.
                // Check reference-bound closures too, before reference locals
                // are discarded below. Port-binding lambdas never take this
                // local-closure declaration path.
                const auto* record = VD->getType().getNonReferenceType()->getAsCXXRecordDecl();
                if (record && record->isLambda()) {
                    unsupportedLambda(*ctx, VD->getLocation());
                    continue;
                }
                if (VD->getType()->isReferenceType()) {  // any reference declaration
                    // ignore
                }
                else {  // real declaration
                    auto expr = cpphdl::Expr{VD->getName().str(), cpphdl::Expr::EXPR_DECL};

                    DEBUG_AST1(" VarDecl(" << VD->getName().str() << ")");
                    QualType QT = VD->getType();

                    if (QT->isPointerType()) {  // while?
                        QT = QT->getPointeeType();
                        DEBUG_AST1(" *pointer*");
                    }

                    // Keep alias/template sugar for digQT: desugaring here
                    // bakes a module parameter's default into local widths.
                    auto* CRD = resolveCXXRecordDecl(QT);
                    const bool stdString = CRD &&
                        CRD->getQualifiedNameAsString().find("std::basic_string") == 0;
                    if (CRD && CRD->getQualifiedNameAsString().find("std::") == 0 && !stdString) {
                        continue;  // unsupported standard-library locals have no SV declaration
                    }

                    // std::string is a native SystemVerilog string and must remain
                    // visible when it carries an intermediate formatting result.
                    const auto sizedType = CRD ? castTypeName(QT) : std::string{};
                    const bool bitVector = sizedType.rfind("cpphdl_logic", 0) == 0
                        || sizedType.rfind("cpphdl_u", 0) == 0 || sizedType.rfind("cpphdl_i", 0) == 0;
                    expr.sub.emplace_back(stdString
                        ? cpphdl::Expr{"std::string", cpphdl::Expr::EXPR_TYPE}
                        : bitVector ? cpphdl::Expr{sizedType, cpphdl::Expr::EXPR_TYPE}
                        : digQT(QT));
                    if (VD->getInit()) {
                        const clang::Expr* init = VD->getInit()->IgnoreImplicit();
                        const auto* constructor = dyn_cast<CXXConstructExpr>(init);
                        // Clang represents `u<4> value;` and other class-type
                        // declarations as a zero-argument callinit even though
                        // the source has no initializer. Do not turn that
                        // implicit constructor node into an RTL zero assignment.
                        // Explicit `{}` is ListInit and remains a real zeroing
                        // assignment, as required by its C++ semantics.
                        const bool implicitDefaultInit = VD->getInitStyle() == VarDecl::CallInit
                            && constructor && constructor->getNumArgs() == 0;
                        if (!implicitDefaultInit) {
                            expr.sub.emplace_back(exprToExpr(VD->getInit()));
                        }
                    }

                    CRD = resolveCXXRecordDecl(QT);
                    if (!stdString && cpphdlRecordShouldExportAsStruct(CRD, *this)
                        && CRD->getQualifiedNameAsString().find("IO_FILE") == (size_t)-1) {
                        auto st = exportStruct(CRD, *this);
                        if (std::find_if(mod->imports.begin(), mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == mod->imports.end()) {
                            mod->imports.emplace_back(st.name);
                            currProject->structs.emplace_back(std::move(st));
                        }
                    }

                    body.sub.emplace_back(std::move(expr));
                }
            }
        }
        return body;
    }
    if (auto* UO = dyn_cast<UnaryOperator>(E)) {
        DEBUG_AST1(" UnaryOperator");
        auto expr = exprToExpr(UO->getSubExpr());
        QualType LQT = UO->getSubExpr()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (UO->getOpcodeStr(UO->getOpcode()) == "*" && LQT->isPointerType()) {  // convert pointer add into index
            std::string typeSize;
            if (const CXXRecordDecl* RD = LQT->getPointeeType().getNonReferenceType()->getAsCXXRecordDecl()) {
                typeSize = std::string("$bits(") + genTypeName(RD->getQualifiedNameAsString()) + ")";
            }
            else {
                typeSize = std::string("$bits(") + genTypeName(LQT->getPointeeType().getNonReferenceType().getDesugaredType(*ctx).getAsString(ctx->getPrintingPolicy())) + ")";
            }
            bool found = false;
            expr.traverseIf( [&](auto& e) {  // we support only one substitution in pack
                    if (e.type == cpphdl::Expr::EXPR_INDEX) {
                        e.value = std::string("*8 +: ") + typeSize;
                        found = true;
                        return true;
                    }
                    return false;
                });
            if (found) {
                return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {std::move(expr)}};
            } else {
                return cpphdl::Expr{typeSize, cpphdl::Expr::EXPR_DEREF, {std::move(expr)}};
            }
        }
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {std::move(expr)}};
    }
    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST1(" BinaryOperator(" << BO->getOpcodeStr().data() << ")");
        // Based SV literals are unsigned. For sign-sensitive operators, use
        // Clang's post-promotion operand types rather than letting a literal
        // silently turn a signed comparison, division or right shift unsigned.
        if (BO->isComparisonOp() || BO->getOpcode() == BO_Div
            || BO->getOpcode() == BO_Rem || BO->getOpcode() == BO_Shr) {
            auto operand = [&](const clang::Expr* arg) {
                const auto* plain = arg->IgnoreParenImpCasts();
                const auto* ref = dyn_cast<DeclRefExpr>(plain);
                const auto* member = dyn_cast<MemberExpr>(plain);
                const auto* decl = ref ? ref->getDecl() : member ? member->getMemberDecl() : nullptr;
                const auto* var = dyn_cast_or_null<VarDecl>(decl);
                // SV module parameters/localparams are emitted without an
                // explicit C++ type. Unlike ordinary variables, their width
                // and signedness must still be supplied to these operators.
                const bool parameter = isa_and_nonnull<NonTypeTemplateParmDecl>(decl)
                    || (var && var->isConstexpr());
                return (arg->getType()->isSignedIntegerOrEnumerationType()
                    || (parameter && arg->getType()->isIntegerType()))
                    ? valueCast(arg->getType(), arg) : exprToExpr(arg);
            };
            return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY,
                {operand(BO->getLHS()), operand(BO->getRHS())}};
        }
        if (BO->getOpcode() == BO_Assign && BO->getLHS()->getType()->isIntegerType()
            && !BO->getLHS()->getType()->isBooleanType()) {
            auto rhs = exprToExpr(BO->getRHS());
            // Assignment supplies exactly this conversion already. Retain
            // inner casts that constrain intermediate arithmetic widths.
            const auto target = castTypeName(BO->getLHS()->getType());
            while (rhs.type == cpphdl::Expr::EXPR_CAST && rhs.value == target) {
                auto operand = std::move(rhs.sub[0]);
                rhs = std::move(operand);
            }
            return cpphdl::Expr{"=", cpphdl::Expr::EXPR_BINARY,
                {exprToExpr(BO->getLHS()), std::move(rhs)}};
        }
//        QualType LQT = BO->getLHS()->IgnoreParenImpCasts()->getType().getNonReferenceType();
//        if (BO->getOpcodeStr() == "+" && LQT->isPointerType() && !(flag&&FLAG_POINTER_BASE)) {  // convert pointer add into index
//            if (dyn_cast<BinaryOperator>(BO->getRHS())) {
//            }
//            return cpphdl::Expr{std::string("*8 +:") + std::to_string(ctx->getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
//                                   cpphdl::Expr::EXPR_INDEX, {exprToExpr(BO->getLHS()),exprToExpr(BO->getRHS())}};
//        }
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS()),exprToExpr(BO->getRHS())}};
    }
    if (auto* CAO = dyn_cast<CompoundAssignOperator>(E)) {
        DEBUG_AST1(" CompoundAssignOperator(" << CAO->getOpcodeStr().data() << ")");
        return cpphdl::Expr{CAO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(CAO->getLHS()),exprToExpr(CAO->getRHS())}};
    }
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST1(" DeclRefExpr(" << DRE->getNameInfo().getAsString() << ")");

        const ValueDecl *VD = DRE->getDecl();
        const auto *Var = dyn_cast_or_null<VarDecl>(VD);
        if (Var && DRE->getDecl()->getType()->isReferenceType() && Var->hasInit()) {  // check if it's reference aka symlink to another var
            DEBUG_AST1(" REF");
            return exprToExpr(Var->getInit());
        }

        bool isPack = false;
        if (const ParmVarDecl* PVD = dyn_cast_or_null<ParmVarDecl>(Var)) {
             if (PVD && PVD->isParameterPack()) {
                 DEBUG_AST1(" PACK");
                 isPack = true;
             }
        }

        const CXXRecordDecl* owner = nullptr;
        if (VD) {
            const DeclContext *DC = VD->getDeclContext();  // find owner class
            while (DC) {
                if ((owner = dyn_cast<CXXRecordDecl>(DC))) {
                    if (!owner->isLambda())
                        break;
                }
                DC = DC->getParent();
            }
        }

        std::string name = VD->getNameAsString();
        if (const auto *ECD = dyn_cast<EnumConstantDecl>(VD)) {  // make enum pkg
            if (const auto *ED = dyn_cast<EnumDecl>(ECD->getDeclContext())) {
                DEBUG_AST1(" EnumName: " << ED->getQualifiedNameAsString());

                auto en = cpphdl::Enum{genTypeName(ED->getQualifiedNameAsString()), ED->getQualifiedNameAsString()};
                QualType integerType = ED->getIntegerType();
                if (!integerType.isNull()) {
                    en.bitWidth = ED->getASTContext().getTypeSize(integerType);
                    en.isSigned = integerType->isSignedIntegerType();
                }

                for (const EnumConstantDecl *ECD : ED->enumerators()) {
                    if (ECD->getInitExpr()) {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str(), {exprToExpr(ECD->getInitExpr())}});
                    }
                    else {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str()});
                    }
                }

                name = en.name + "_pkg::" + name;

                if (std::find_if(mod->imports.begin(), mod->imports.end(), [&](auto& imp){ return imp.name == en.name; }) == mod->imports.end()) {
                    mod->imports.emplace_back(en.name);
                    currProject->enums.emplace_back(std::move(en));
                }
            }
        } else
        if (owner && Var && Var->isConstexpr()
            && (flags&FLAG_EXTERNAL_THIS
                || (mod && !isCurrentOrBaseRecord(parent, owner)
                    && owner->getQualifiedNameAsString().find("cpphdl::") != 0
                    && owner->getQualifiedNameAsString().find("std::") != 0))) {  // make name for pkg constexpr parameter access
            std::string sname = owner->getQualifiedNameAsString();
            str_replace(sname, "::", "_");
            // extracting parameters of the template
            followSpecialization(owner, sname);
            auto* ModuleClass = lookupQualifiedRecord("cpphdl::Module");
            if (ModuleClass && owner->isDerivedFrom(ModuleClass)) {
                // Static methods use DeclRefExpr for unqualified constants.
                // Request the module package here just as MemberExpr does for
                // explicitly qualified external constant access.
                currProject->modulePackages.insert(sname);
                if (std::find_if(mod->imports.begin(), mod->imports.end(),
                        [&](const auto& imp){ return imp.name == sname; }) == mod->imports.end()) {
                    mod->imports.emplace_back(sname);
                }
            }
            name = sname + "_pkg::" + name;
        } else
        if (owner && Var && mod->origName.find(owner->getQualifiedNameAsString()) != 0 && owner->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1
            && !Var->isLocalVarDeclOrParm()/* && !Var->isStaticLocal()*/ && !Var->isConstexpr()
            && !str_ending(name, "_in") && !str_ending(name, "_out")) {  // add base class name, ports dont get this prefix
            name = genTypeName(owner->getQualifiedNameAsString()) + "___" + name;
        }
        if (isPack) {
            name = "";//genTypeName(owner->getQualifiedNameAsString()) + "___";
        }

        if (!dyn_cast<MemberExpr>(E)) {
            return cpphdl::Expr{name, isPack ? cpphdl::Expr::EXPR_PACK : cpphdl::Expr::EXPR_VAR};
        }
    }
    if (auto* IL = dyn_cast<IntegerLiteral>(E)) {
        llvm::SmallString<32> Str;
        const auto& value = IL->getValue();
        value.toString(Str, 16, !IL->getType()->isUnsignedIntegerType());
        std::string literal = "'h" + Str.str().str();
        if (value.getBitWidth() > 32) {
            literal = std::to_string(value.getBitWidth()) + literal;
        }
        DEBUG_AST1(" IntegerLiteral(" << literal << ")");
        return cpphdl::Expr{literal, cpphdl::Expr::EXPR_NUM};
    }
    if (auto* OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
        DEBUG_AST1(" CXXOperatorCallExpr");
        cpphdl::Expr call = cpphdl::Expr{getOperatorSpelling(OCE->getOperator()), cpphdl::Expr::EXPR_OPERATORCALL};
        for (unsigned i = 0; i < OCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(OCE->getArg(i)));
        }
        if (OCE->getOperator() == OO_Tilde) {
            const auto resultType = castTypeName(OCE->getArg(0)->getType());
            if (resultType.rfind("cpphdl_logic", 0) == 0 || resultType.rfind("cpphdl_u", 0) == 0) {
                // CppHDL complements only the bit-vector's declared bits.
                // SV would propagate a wider assignment through ~ and invert
                // the extension bits too. Derive the width from the SV operand
                // so instantiated C++ default widths cannot replace parameters.
                return cpphdl::Expr{"cpphdl_bitnot", cpphdl::Expr::EXPR_CAST, {std::move(call.sub[0])}};
            }
        }
        if (OCE->getOperator() == OO_Equal && OCE->getNumArgs() >= 2) {
            QualType targetType = OCE->getArg(0)->getType().getNonReferenceType();
            if (skipStdFunctionType(targetType) && targetType->isBooleanType()) {
                cpphdl::coerceReturnToBool(call.sub[1]);
            }
            // Fixed-width bit-vector assignment, like scalar assignment,
            // supplies a final narrowing conversion. Keep any inner arithmetic
            // barrier; only the outer conversion is redundant here.
            auto width = [](const std::string& type) -> unsigned {
                for (const std::string prefix : {"cpphdl_u", "cpphdl_i", "cpphdl_logic"}) {
                    if (type.rfind(prefix, 0) != 0) continue;
                    const auto digits = type.substr(prefix.size());
                    if (digits.empty() || digits.size() > 6
                        || digits.find_first_not_of("0123456789") != std::string::npos) return 0;
                    return std::stoul(digits);
                }
                return 0;
            };
            const auto targetWidth = width(castTypeName(targetType));
            if (targetType->isRecordType() && targetWidth
                && call.sub[1].type == cpphdl::Expr::EXPR_CAST
                && width(call.sub[1].value) >= targetWidth) {
                auto operand = std::move(call.sub[1].sub[0]);
                call.sub[1] = std::move(operand);
            }
        }
        return call;
    }
    if (auto* MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        DEBUG_AST1(" CXXMemberCallExpr(" << (MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():"") << ")");
        cpphdl::Expr call = cpphdl::Expr{(MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_MEMBERCALL};

        bool notThis = false;
        bool moduleInstanceMethod = false;
        if (auto* ME = dyn_cast<MemberExpr>(MCE->getCallee())) {
            const clang::Expr* base = ME->getBase()->IgnoreParenImpCasts();
            if (const auto* baseMember = dyn_cast<MemberExpr>(base)) {
                if (const auto* field = dyn_cast<FieldDecl>(baseMember->getMemberDecl())) {
                    auto* ModuleClass = lookupQualifiedRecord("cpphdl::Module");
                    CXXRecordDecl* fieldType = resolveCXXRecordDecl(field->getType().getNonReferenceType());
                    const std::string methodName = MCE->getDirectCallee()
                        ? MCE->getDirectCallee()->getNameAsString() : "";
                    moduleInstanceMethod = ModuleClass && fieldType && fieldType->isDerivedFrom(ModuleClass)
                        && methodName != "_work" && methodName != "_assign" && methodName != "_strobe";
                }
            }
            auto expr = exprToExpr(ME->getBase());

//            if (auto* DRE = dyn_cast<DeclRefExpr>(ME->getBase())) {
//                if (const ValueDecl *VD = DRE->getDecl()) {  // check if it's reference aka symlink to another var
//                    if (VD->getType()->isReferenceType()) {
//                        DEBUG_AST1(" REF");
//                        call.sub.emplace_back(exprToExpr(ME->getBase()));
//                        return call;
//                    }
//                }
//            }

            if (expr.type != cpphdl::Expr::EXPR_NONE  // base object is not "this" and not member, marking ingerited blocks as EXTERNAL
                && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& member){ return member.name == expr.value; }) == mod->members.end()) {
                notThis = true;
                DEBUG_AST1(" NOTTHIS");
            }

            if ((flags&FLAG_EXTERNAL_THIS) && expr.type == cpphdl::Expr::EXPR_NONE) {  // member is current object (CallExpr sets EXPR_NONE for local)
                DEBUG_AST1(" EXTERNAL");
                call.sub.push_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
            }
            else {  // member is a third object
                call.sub.push_back(std::move(expr)/*cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase())}}*/);
            }
        }
        for (unsigned i = 0; i < MCE->getNumArgs(); ++i) {
            call.sub.push_back(call.value == "bits"
                ? bitIndexToExpr(MCE->getArg(i)) : exprToExpr(MCE->getArg(i)));
        }

        const CXXMethodDecl* MD = MCE->getMethodDecl();
        if (!MD) {
            MD = dyn_cast_or_null<CXXMethodDecl>(MCE->getDirectCallee());
        }
        if (MD) {
            if (moduleInstanceMethod) {
                // Keep a real function call on the generated module instance.
                // Its function body stays in the child module and can use the
                // child's parameters and state without an invalid _this type.
                call.flags |= cpphdl::Expr::FLAG_MODULE_INSTANCE_METHOD;
            }
            else if (call.sub.size()  // we need not call members - they are accessible through ports wires
                && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& member){ return member.name == call.sub[0].value; }) == mod->members.end()) {
                auto newName = putMethod(MD, *this, notThis);
                DEBUG_AST1(" Called method( " << MD->getQualifiedNameAsString() << " => " << newName << ")");
                if (newName.length()) {
                    call.value = newName;
                }
            }
        }

        return call;
    }
    if (auto* ME = dyn_cast<MemberExpr>(E)) {
        const CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(ME->getMemberDecl()->getDeclContext());
        auto* InterfaceClass = lookupQualifiedRecord("cpphdl::Interface");
        ASSERT(InterfaceClass && InterfaceClass->getDefinition());
        bool interface = CRD->isDerivedFrom(InterfaceClass);

        DEBUG_AST1(" MemberExpr(" << CRD->getQualifiedNameAsString() << "::" << ME->getMemberDecl()->getNameAsString() << ")");

        auto expr = exprToExpr(ME->getBase()->IgnoreParenImpCasts());

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL ");
            if (expr.type == cpphdl::Expr::EXPR_NONE) {
                expr.type = cpphdl::Expr::EXPR_VAR;
                expr.value = "_this";
            }
        }

        const auto* Var = dyn_cast<VarDecl>(ME->getMemberDecl());
        bool ignoreBase = false;
        std::string name = ME->getMemberDecl()->getNameAsString();
        bool anon = false;
        if (/*const auto* FD =*/ dyn_cast<FieldDecl>(ME->getMemberDecl()) && CRD->isAnonymousStructOrUnion()) {  // replacing anon with '_'
            DEBUG_AST1(" ANON");
            anon = true;
        } else
        if (Var && Var->isConstexpr() && (flags&FLAG_EXTERNAL_THIS) && !isCurrentOrBaseRecord(parent, CRD)) {  // make name for pkg constexpr parameter access
            DEBUG_AST1(" PKG");
            std::string sname = CRD->getQualifiedNameAsString();
            str_replace(sname, "::", "_");
            // extracting parameters of the template
            followSpecialization(CRD, sname);
            auto* ModuleClass = lookupQualifiedRecord("cpphdl::Module");
            if (ModuleClass && CRD->isDerivedFrom(ModuleClass)) {
                // A Module is not a struct package by default. Record the
                // package demand created by this external constexpr access and
                // import it in the consumer so SV build tools see the dependency.
                currProject->modulePackages.insert(sname);
                if (std::find_if(mod->imports.begin(), mod->imports.end(),
                        [&](const auto& imp){ return imp.name == sname; }) == mod->imports.end()) {
                    mod->imports.emplace_back(sname);
                }
            }
            name = sname + "_pkg::" + name;
            ignoreBase = true;
        } else
        if (mod->origName.find(CRD->getQualifiedNameAsString()) != 0 && CRD->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1
            && expr.type == cpphdl::Expr::EXPR_NONE && !str_ending(name, "_in") && !str_ending(name, "_out") ) {  // add base class name, ports dont get this prefix
            name = genTypeName(CRD->getQualifiedNameAsString()) + "___" + name;
        }

        return cpphdl::Expr{name, cpphdl::Expr::EXPR_MEMBER, {expr},
                (anon?cpphdl::Expr::FLAG_ANON:0U) | ((flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_USETHIS:0U) | (ignoreBase?cpphdl::Expr::FLAG_NOBASE:0U),
                interface};
    }
    if (auto* CDSME = dyn_cast<CXXDependentScopeMemberExpr>(E)) {
        DEBUG_AST1(" CXXDependentScopeMemberExpr(" << CDSME->getMemberNameInfo().getAsString() << ")");

        auto expr = exprToExpr(CDSME->getBase());

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL ");
            if (expr.type == cpphdl::Expr::EXPR_NONE) {
                expr.type = cpphdl::Expr::EXPR_VAR;
                expr.value = "_this";
            }
        }

        return cpphdl::Expr{CDSME->getMemberNameInfo().getAsString(), cpphdl::Expr::EXPR_MEMBER, {expr}, 0,
            resolveInterfaceRecordDecl(CDSME->getBase()->getType()) != nullptr};
    }
    if (auto* LE = dyn_cast<LambdaExpr>(E)) {
        DEBUG_AST1(" LambdaExpr");

        cpphdl::Expr expr = cpphdl::Expr{"lambda", cpphdl::Expr::EXPR_BODY};
        const CompoundStmt *body = cast<CompoundStmt>(LE->getBody());
        for (auto* S : body->body()) {
            expr.sub.emplace_back(exprToExpr(S));
        }
        return expr;
    }
    if (auto* CE = dyn_cast<CallExpr>(E)) {
        cpphdl::Expr call = cpphdl::Expr{"unknown", cpphdl::Expr::EXPR_CALL};
        const clang::Expr* callee = CE->getCallee()->IgnoreParenImpCasts();
        DEBUG_AST1(" CallExpr(" << callee->getStmtClassName() << ", "
                   << callee->getType().getAsString(ctx->getPrintingPolicy())
                   << ")");

        // Ports are represented by zero-argument function/function_ref
        // objects.  Their invocation is just a read of the generated signal.
        // Handle arbitrary callee expressions here (notably dynamically
        // indexed unpacked port arrays such as port[i]()) rather than falling
        // through to an invalid unknown() call.
        QualType calleeType = callee->getType().getNonReferenceType();
        if (const auto* arrayElement = dyn_cast<ArraySubscriptExpr>(callee)) {
            QualType arrayType =
                arrayElement->getBase()->IgnoreParenImpCasts()->getType();
            if (const auto* type = ctx->getAsArrayType(arrayType)) {
                calleeType = type->getElementType().getNonReferenceType();
            }
        }
        if (CE->getNumArgs() == 0 && skipStdFunctionType(calleeType)) {
            return exprToExpr(callee);
        }

        //////////// this code is for std::tuple and should be refactored to separated source file //

        if (const auto* DSDRE = llvm::dyn_cast<clang::DependentScopeDeclRefExpr>(callee)) {
            DEBUG_AST1(" DSDRE(" << DSDRE->getDeclName().getAsString() << ")");
            const std::string qualifier = dependentQualifierText(DSDRE, ctx->getPrintingPolicy());
            std::string templateName = qualifier;
            if (size_t pos = templateName.find('<'); pos != std::string::npos) {
                templateName.resize(pos);
            }
            if (size_t pos = templateName.rfind("::"); pos != std::string::npos) {
                templateName.erase(0, pos + 2);
            }

            call.value = genTypeName(qualifier) + "___" + DSDRE->getDeclName().getAsString();
            if (const ClassTemplateDecl* ctd = findClassTemplateDecl(ctx->getTranslationUnitDecl(), templateName)) {
                if (const CXXRecordDecl* templated = ctd->getTemplatedDecl()) {
                    for (const CXXMethodDecl* method : templated->methods()) {
                        if (method->getNameAsString() == DSDRE->getDeclName().getAsString()) {
                            auto newName = putMethod(method, *this);
                            if (!newName.empty()) {
                                call.value = newName;
                            }
                            break;
                        }
                    }
                }
            }
        }

        if (const auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(callee)) { // we do it only for std::apply
            const clang::FunctionDecl* function = llvm::dyn_cast<clang::FunctionDecl>(DRE->getDecl());
            DEBUG_AST1(" DRE(" << function->getQualifiedNameAsString() << ")");
            call.value = function->getNameAsString();
            if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
                importStructForStaticMethodOwner(method, *this);
                auto newName = putMethod(method, *this);
                DEBUG_AST1(" Called static method( " << method->getQualifiedNameAsString() << " => " << newName << ")");
                std::string combSignal = flattenedCombSignalName(mod, call.value);
                if (combSignal.empty()) {
                    combSignal = flattenedCombSignalName(mod, newName);
                }
                if (!combSignal.empty()) {
                    call.value = combSignal + "_func";
                }
                else if (newName.length() && !isCombFuncName(method->getNameAsString())) {
                    call.value = newName;
                }
            }

            if (function->getQualifiedNameAsString() == "std::apply") {
                DEBUG_AST1(" APPLY");
                const Expr* tupleExpr = CE->getArg(1)->IgnoreParenImpCasts();  // getting arg1 of std::apply
                QualType QT = tupleExpr->getType();
                QT = QT.getNonReferenceType();
                QT = QT.getUnqualifiedType();
                if (const auto* TST = QT->getAs<TemplateSpecializationType>()) {
                    if (const TemplateDecl* TD = TST->getTemplateName().getAsTemplateDecl()) {
                        if (TD->getQualifiedNameAsString() == "std::tuple") {
                            auto apply = cpphdl::Expr{"apply", cpphdl::Expr::EXPR_BODY};
                            auto expr = exprToExpr(CE->getArg(0)->IgnoreParenImpCasts());  // getting arg0 lambda of std::apply
                            apply.sub.push_back(expr);  // adding whole lambda, making copy, not moving
                            cpphdl::Expr* placeToInsertPattern = nullptr;
                            apply.traverseIf( [&](auto& e) {  // looking for CXXFoldExpr in lambda
                                    if (e.value == "CXXFoldExpr") {
                                        e.sub.clear();
                                        placeToInsertPattern = &e;
                                        return true;
                                    }
                                    return false;
                                });

                            size_t i=0;
                            for (const auto& arg : TST->template_arguments()) {  // for each argument of Pack, prepare right pattern in lambda
                                auto expr1 = expr;  // we will modify a copy
                                expr1.traverseIf( [&](auto& e) {
                                            // implace right name instead of pack argument (we support only one substitution in pack)
                                            if (e.type == cpphdl::Expr::EXPR_PACK) {
                                                e.value += exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i);
                                            }

                                            // replace typename in using TYPE = typename decltype(pack_element_type) like "+: $bits(typename std::remove_reference_t<decltype(stage)>::STATE)/8"
                                            size_t pos = -1;
                                            if ((pos = e.value.find("decltype")) != (size_t)-1 && e.value.find("(", pos) != (size_t)-1) {  // we do it only to extract decltype() from std::apply
                                                if ((pos = e.value.rfind("::")) != (size_t)-1 && arg.getKind() == TemplateArgument::Type) {
                                                    std::string name = e.value.substr(pos+2, e.value.rfind(")") != (size_t)-1 ? e.value.rfind(")")-pos-2 : -1);  // like "STATE"
                                                    std::string type;
                                                    QualType QT1 = arg.getAsType().getNonReferenceType();
                                                    if (const CXXRecordDecl* RD = QT1->getAsCXXRecordDecl()) {
                                                        forEachBase(RD, [&](const CXXRecordDecl* RD) {  // looking for type like "STATE" through all base classes
                                                                for (const Decl *D : RD->decls()) {
                                                                    if (auto *Alias = dyn_cast<TypeAliasDecl>(D)) {
                                                                        if (Alias->getName() == name) {
                                                                            QualType QT2 = Alias->getUnderlyingType();
                                                                            type = genTypeName(QT2.getAsString(ctx->getPrintingPolicy()));
                                                                        }
                                                                    } else if (auto *TD = dyn_cast<TypeDecl>(D)) {
                                                                        if (TD->getName() == name) {
                                                                            QualType QT2 = ctx->getTypeDeclType(TD);
                                                                            type = genTypeName(QT2.getAsString(ctx->getPrintingPolicy()));
                                                                        }
                                                                    }
                                                                }
                                                        });
                                                    }
                                                    // we're trying to parse line "typename std::remove_reference_t<decltype(stage)>::STATE" which is not splitted by Clang AST (considered as atomic)
                                                    size_t pos1 = e.value.find("typename");
                                                    size_t pos2 = e.value.find(name, pos1);
                                                    if (pos1 != (size_t)-1) {
                                                        e.value.replace(pos1, pos2 != (size_t)-1 ? pos2 - pos1 + name.length() : -1, type.length() ? type + "_pkg::" + type: (exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i)));
                                                    }
                                                    else {
                                                        e.value = type.length() ? type + "_pkg::" + type: (exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i));
                                                    }
                                                } else {
                                                    size_t pos1 = e.value.find("typename");
                                                    if (pos1 != (size_t)-1) {
                                                        e.value.replace(pos1, -1, exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i));
                                                    }
                                                    else {
                                                        e.value = exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i);
                                                    }
                                                }
                                            }
                                            return false;
                                        });
                                expr1.traverseIf( [&](auto& e) {  // looking for CXXFoldExpr in lambda
                                        // instantiating block of code, must me only CXXFoldExpr pack per std::apply lambda
                                        if (e.value == "CXXFoldExpr") {
                                            if (placeToInsertPattern) {
                                                placeToInsertPattern->sub.emplace_back(e);
                                            }
                                            return true;
                                        }
                                        return false;
                                    });
                                ++i;
                            }
                            return apply;
                        }
                    }
                }
//                if (const auto* DRE = dyn_cast<DeclRefExpr>(DRE->getBase()->IgnoreParenImpCasts())) {
//                    std::cout << "Pack variable: " << DRE->getDecl()->getNameAsString() << "\n";
//                }
            }
        }

        /////////////////////////////////////////////////////////////////////////////////////////////


        if (const auto* ME = llvm::dyn_cast<clang::MemberExpr>(callee)) {
            const ValueDecl *VD = ME->getMemberDecl();
            const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(ME->getMemberDecl());
            cpphdl::Expr baseExpr{"", cpphdl::Expr::EXPR_NONE};
            bool notThis = false;
            if (method && !ME->isImplicitAccess()) {
                baseExpr = exprToExpr(ME->getBase());
                if (baseExpr.type != cpphdl::Expr::EXPR_NONE
                    && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& member){ return member.name == baseExpr.value; }) == mod->members.end()) {
                    notThis = true;
                }
            }
            if (method) {
                DEBUG_AST1(" ME(" << method->getQualifiedNameAsString() << ")");
                call.value = method->getNameAsString();
            } else
            if (const clang::FieldDecl* field = llvm::dyn_cast<clang::FieldDecl>(ME->getMemberDecl())) {
                DEBUG_AST1(" ME(" << field->getNameAsString() << ")");
                call.value = field->getNameAsString();
            }
            else {
                DEBUG_AST1(" ME(" << VD->getDeclKindName() << ")");
                call.value = VD->getDeclKindName();
            }

//            if (call.sub.size()  // we need not call members - they are accessible through ports wires
//                && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& member){ return member.name == call.sub[0].value; }) == mod->members.end()) {
            if (method) {
                importStructForStaticMethodOwner(method, *this);
                auto newName = putMethod(method, *this, notThis);
                DEBUG_AST1(" Called method( " << method->getQualifiedNameAsString() << " => " << newName << ")");
                std::string combSignal = flattenedCombSignalName(mod, call.value);
                if (combSignal.empty()) {
                    combSignal = flattenedCombSignalName(mod, newName);
                }
                if (!combSignal.empty()) {
                    call.value = combSignal + "_func";
                }
                else if (newName.length() && !isCombFuncName(method->getNameAsString())) {
                    call.value = newName;
                }
            }

            if (method && method->isLambdaStaticInvoker()) {
                const auto* RD = method->getParent();
                if (RD && RD->isLambda()) {
                    DEBUG_AST1(" CallExpr(LambdaExpr) not supported");
                }
            }

            call.type = cpphdl::Expr::EXPR_MEMBERCALL;
            call.sub.emplace_back(std::move(baseExpr));
        }

        if (const auto* UME = llvm::dyn_cast<clang::UnresolvedMemberExpr>(callee)) {
            DEBUG_AST1(" UME(" << UME->getMemberName().getAsString() << ")");
            call.value = UME->getMemberName().getAsString();
            call.type = cpphdl::Expr::EXPR_MEMBERCALL;
//            member.sub.emplace_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
//            member.type = cpphdl::Expr::EXPR_MEMBER;
            call.sub.emplace_back(cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE});
        }

        if (const auto* LE = llvm::dyn_cast<clang::LambdaExpr>(callee)) {
            DEBUG_AST1(" LE()");
            return exprToExpr(LE);
        }

        if (const auto* ULE = llvm::dyn_cast<clang::UnresolvedLookupExpr>(callee)) {
            DEBUG_AST1(" ULE(" << ULE->getNameInfo().getAsString() << ")");
            call.value = ULE->getNameInfo().getAsString();
        }

        if (const auto* DSME = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(callee)) {  // we do this only to get pack inside std::apply
            if (CE->getNumArgs() == 0) {
                if (auto* interface = resolveInterfaceRecordDecl(DSME->getBase()->getType())) {
                    for (const auto* field : interface->fields()) {
                        if (field->getName() == DSME->getMemberNameInfo().getAsString()) {
                            QualType type = field->getType();
                            if (skipStdFunctionType(type)) {
                                return exprToExpr(DSME);
                            }
                        }
                    }
                }
            }
            DEBUG_AST1(" DSME(" << DSME->getMemberNameInfo().getAsString() << ")");
            call.value = DSME->getMemberNameInfo().getAsString();
            call.type = cpphdl::Expr::EXPR_MEMBERCALL;
            auto member = exprToExpr(DSME->getBase());
//            member.sub.emplace_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
//            member.type = cpphdl::Expr::EXPR_MEMBER;
            bool notThis = member.type != cpphdl::Expr::EXPR_NONE
                && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& m){ return m.name == member.value; }) == mod->members.end();
            call.sub.emplace_back(member);
            const auto* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(CE->getDirectCallee());
            if (!method) {
                QualType baseQT = DSME->getBase()->getType().getNonReferenceType();
                CXXRecordDecl* baseRD = resolveCXXRecordDecl(baseQT);
                if (const auto* TSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(baseRD)) {
                    baseRD = TSD->getSpecializedTemplate()->getTemplatedDecl();
                }
                if (!baseRD) {
                    std::string baseTypeName = baseQT.getAsString(ctx->getPrintingPolicy());
                    if (size_t pos = baseTypeName.find('<'); pos != std::string::npos) {
                        baseTypeName.resize(pos);
                    }
                    if (size_t pos = baseTypeName.rfind("::"); pos != std::string::npos) {
                        baseTypeName.erase(0, pos + 2);
                    }
                    if (const ClassTemplateDecl* ctd = findClassTemplateDecl(ctx->getTranslationUnitDecl(), baseTypeName)) {
                        baseRD = ctd->getTemplatedDecl();
                    }
                }
                if (baseRD) {
                    const std::string methodName = DSME->getMemberNameInfo().getAsString();
                    for (const CXXMethodDecl* candidate : baseRD->methods()) {
                        if (candidate->getNameAsString() == methodName) {
                            method = candidate;
                            break;
                        }
                    }
                }
            }
            if (method) {
                auto newName = putMethod(method, *this, notThis);
                DEBUG_AST1(" Called dependent method( " << method->getQualifiedNameAsString() << " => " << newName << ")");
                std::string combSignal = flattenedCombSignalName(mod, call.value);
                if (combSignal.empty()) {
                    combSignal = flattenedCombSignalName(mod, newName);
                }
                if (!combSignal.empty()) {
                    call.value = combSignal + "_func";
                }
                else if (newName.length() && !isCombFuncName(method->getNameAsString())) {
                    call.value = newName;
                }
            }
        }

        for (auto* arg : CE->arguments()) {
            call.sub.push_back(call.type == cpphdl::Expr::EXPR_MEMBERCALL && call.value == "bits"
                ? bitIndexToExpr(arg) : exprToExpr(arg));
        }

/*        if (const auto *DRE = dyn_cast<DeclRefExpr>(Callee)) {
            if (const auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl())) {
                if (const auto *FTSI = FD->getTemplateSpecializationInfo()) {
                    const TemplateArgumentList *args = FTSI->TemplateArguments;
                    if (const TemplateArgumentList *args = FTSI->TemplateArguments) {
                        for (const TemplateArgument &arg : args->asArray()) {
                        }
                    }
                }
            }
        }
*/
        cpphdl::Expr templ = cpphdl::Expr{(CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_TEMPLATE};
        if (const auto *DRE = dyn_cast<DeclRefExpr>(callee->IgnoreParenImpCasts())) {  // template parameters for call (needed by std::apply()
            if (DRE->hasExplicitTemplateArgs()) {
                const TemplateArgumentLoc *args = DRE->getTemplateArgs();
                for (unsigned i = 0; i < DRE->getNumTemplateArgs(); ++i) {
                    DEBUG_AST(debugIndent++, "# Arg "); on_return ret_debug([](){ --debugIndent; });
                    cpphdl::Expr tmp;
                    ArgToExpr(args[i].getArgument(), tmp);
                    for (auto& expr : tmp.sub) {
                        templ.sub.emplace_back(std::move(expr));
                    }
                }
                templ.sub.push_back(call);
                call = templ;  // swap them to make it work
            }
        }

        const CXXRecordDecl* CRD = nullptr;
        if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(CE->getDirectCallee())) {
            const CXXMethodDecl *CanonicalMD = MD->getCanonicalDecl();
            CRD = CanonicalMD->getParent();
        }

        if (CRD && mod->origName.find(CRD->getQualifiedNameAsString()) != 0 && CRD->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1
            && call.value.find("___") == std::string::npos
            && !str_ending(call.value, "_in") && !str_ending(call.value, "_out") ) {  // add base class name, ports dont get this prefix
            std::string combSignal = flattenedCombSignalName(mod, call.value);
            if (!combSignal.empty()) {
                call.value = combSignal + "_func";
            }
            else {
                call.value = genTypeName(CRD->getQualifiedNameAsString()) + "___" + call.value;
            }
        }

        return call;
    }
    if (auto* CE = dyn_cast<CXXUnresolvedConstructExpr>(E)) {
        const std::string type = castTypeName(CE->getType());
        if (CE->getNumArgs() == 1 && (type.find("cpphdl_u") == 0
            || type.find("cpphdl_i") == 0 || type.find("cpphdl_logic") == 0)) {
            return valueCast(CE->getType(), CE->getArg(0));
        }
    }
    if (auto* CE = dyn_cast<CXXConstructExpr>(E)) {
        const CXXConstructorDecl* CtorDecl = CE->getConstructor();
        if (CtorDecl) {
            std::string str;
            llvm::raw_string_ostream OS(str);
            CtorDecl->printQualifiedName(OS);
            OS.flush();

            DEBUG_AST1(" CXXConstructExpr(" << str << ")");

//            cpphdl::Expr call = cpphdl::Expr{str, cpphdl::Expr::EXPR_CALL};
//            for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
//                call.sub.push_back(exprToExpr(CE->getArg(i)));
//            }
//            return call;
            if (CE->getNumArgs()) {
                std::string canonical_type = CE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy());
                if (str.find("cpphdl::cat<") == 0 || canonical_type.find("cpphdl::cat<") != std::string::npos) {
                    cpphdl::Expr expr{"cat", cpphdl::Expr::EXPR_CAT};
                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                        expr.sub.emplace_back(exprToExpr(CE->getArg(i)));
                    }
                    return expr;
                }
                if (str.find("std::basic_format_string") == 0) {
                    return cpphdl::Expr{str, cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getArg(0))}};  // we use it to determine std::print
                }
                std::string cast_type = castTypeName(CE->getType());
                if (cast_type.find("cpphdl_u") == 0 || cast_type.find("cpphdl_i") == 0 || cast_type.find("cpphdl_logic") == 0 ||
                    str.find("cpphdl::u<") != std::string::npos || str.find("cpphdl::i<") != std::string::npos ||
                    str.find("cpphdl::logic<") != std::string::npos) {
                    if (CtorDecl->isCopyOrMoveConstructor())
                        return exprToExpr(CE->getArg(0));
                    return valueCast(CE->getType(), CE->getArg(0));
                }
                return /*cpphdl::Expr{str, cpphdl::Expr::EXPR_CAST, {*/exprToExpr(CE->getArg(0))/*}}*/;
            }
            else {
                std::string cast_type = castTypeName(CE->getType());
                std::string canonical_type = CE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy());
                if (cast_type.find("cpphdl_u") == 0 || cast_type.find("cpphdl_i") == 0 || cast_type.find("cpphdl_logic") == 0 ||
                    str.find("cpphdl::u<") != std::string::npos || str.find("cpphdl::i<") != std::string::npos ||
                    str.find("cpphdl::logic<") != std::string::npos || canonical_type.find("cpphdl::u<") != std::string::npos ||
                    canonical_type.find("cpphdl::i<") != std::string::npos || canonical_type.find("cpphdl::logic<") != std::string::npos) {
                    // Scalar CppHDL value-initialization, for example logic<1>{},
                    // is a real zero assignment in RTL, not an absent expression.
                    return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
                }
                return cpphdl::Expr{str, cpphdl::Expr::EXPR_NONE};
            }
        }
    }
    if (auto* CL = dyn_cast<CharacterLiteral>(E)) {
        DEBUG_AST1(" CharacterLiteral");
        return cpphdl::Expr{std::to_string(CL->getValue()), cpphdl::Expr::EXPR_NUM};
    }
    if (auto* CLE = dyn_cast<CompoundLiteralExpr>(E)) {
        DEBUG_AST1(" CompoundLiteralExpr");
        return exprToExpr(CLE->getInitializer());
    }
    if (auto* SL = dyn_cast<StringLiteral>(E)) {
        DEBUG_AST1(" StringLiteral");

        clang::SourceRange Range = SL->getSourceRange();
        clang::LangOptions LO = ctx->getLangOpts();

        llvm::StringRef RawText = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(Range),
            ctx->getSourceManager(),
            LO);

        return cpphdl::Expr{RawText.str(), cpphdl::Expr::EXPR_STRING};
    }
    if (auto* BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
        DEBUG_AST1(" CXXBoolLiteralExpr");
        return cpphdl::Expr{BLE->getValue()?"1":"0", cpphdl::Expr::EXPR_NUM};
    }
    if (/*auto* ME =*/dyn_cast<CXXThisExpr>(E)) {
        DEBUG_AST1(" CXXThisExpr");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};  // we never use this directly
    }
    if (auto* CO = dyn_cast<ConditionalOperator>(E)) {
        DEBUG_AST1(" ConditionalOperator");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_COND, {exprToExpr(CO->getCond()),exprToExpr(CO->getTrueExpr()),exprToExpr(CO->getFalseExpr())}};
    }
    if (auto* ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        DEBUG_AST1(" ArraySubscriptExpr");
        QualType LQT = ASE->getLHS()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (LQT->isPointerType()) {  // convert pointer add into index
            return cpphdl::Expr{std::string("*8 +:") + std::to_string(ctx->getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
                                   cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getLHS()),exprToExpr(ASE->getRHS())}};
        }
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getLHS()),exprToExpr(ASE->getRHS())}};
    }
    if (auto* FCE = dyn_cast<ImplicitCastExpr>(E)) {
        DEBUG_AST1(" ImplicitCastExpr");
        if (FCE->getCastKind() == CK_FloatingToIntegral || FCE->getCastKind() == CK_IntegralToFloating
            || FCE->getCastKind() == CK_FloatingCast || FCE->getCastKind() == CK_FloatingToBoolean
            || FCE->getCastKind() == CK_MemberPointerToBoolean || FCE->getType()->isMemberPointerType()) {
            // No RTL lowering for this conversion: retain the operand rather
            // than reject it or drop its side effects.
            return exprToExpr(FCE->getSubExpr());
        }
        if (FCE->getType()->isBooleanType()
            && !FCE->getSubExpr()->getType()->isBooleanType()) {
            return cpphdl::Expr{"bool", cpphdl::Expr::EXPR_CAST,
                {exprToExpr(FCE->getSubExpr())}};
        }
        if (FCE->getCastKind() == CK_IntegralCast) {
            return valueCast(FCE->getType(), FCE->getSubExpr());
        }
        return /*cpphdl::Expr{"implicit_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(FCE->getSubExpr())/*}}*/;
    }
    if (const auto* cast = dyn_cast<ExplicitCastExpr>(E)) {
        // All explicit C++ spellings share semantic cast kinds. A value cast must
        // remain self-sized even when its enclosing SV expression is wider.
        const auto target = cast->getType();
        const auto source = cast->getSubExpr()->getType();
        const auto kind = cast->getCastKind();
        if (target->isVoidType() && !cast->getSubExpr()->HasSideEffects(*ctx)) {
            // `(void)value` is not an SV expression statement, and requires
            // no hardware. Do not drop calls or assignments with side effects.
            return cpphdl::Expr{};
        }
        if (kind == CK_Dynamic || kind == CK_PointerToIntegral || kind == CK_IntegralToPointer
            || kind == CK_MemberPointerToBoolean || target->isMemberPointerType()
            || target->isRealFloatingType() || source->isRealFloatingType()) {
            return exprToExpr(cast->getSubExpr());
        }
        // Qualifier/reference and hierarchy casts select the same hardware
        // object. Keep it an lvalue: a sized SV value cast cannot be assigned.
        if (cast->isGLValue() || target->isPointerType() || target->isVoidType()) {
            // This also covers (Struct&) and reinterpret_cast<Struct&> views.
            // Do not turn a reference into a packed-value cast or temporary.
            // SV does not allow a parenthesized assignment as a standalone
            // statement. A discarded C++ value still performs its side effects.
            return exprToExpr(target->isVoidType()
                ? cast->getSubExpr()->IgnoreParens() : cast->getSubExpr());
        }
        return valueCast(target, cast->getSubExpr());
    }
    if (auto* MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
        DEBUG_AST1(" MaterializeTemporaryExpr");
        return /*cpphdl::Expr{"MaterializeTemporaryExpr", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(MTE->getSubExpr())/*}}*/;
    }
    if (auto* EWC = dyn_cast<ExprWithCleanups>(E)) {
        DEBUG_AST1(" ExprWithCleanups");
        return /*cpphdl::Expr{"ExprWithCleanups", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(EWC->getSubExpr())/*}}*/;
    }
    if (auto* CE = dyn_cast<ConstantExpr>(E)) {
        DEBUG_AST1(" ConstantExpr");
        return /*cpphdl::Expr{"ConstantExpr", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(CE->getSubExpr())/*}}*/;
    }
    if (auto* BTE = dyn_cast<CXXBindTemporaryExpr>(E)) {
        DEBUG_AST1(" CXXBindTemporaryExpr");
        return /*cpphdl::Expr{"CXXBindTemporaryExpr", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(BTE->getSubExpr())/*}}*/;
    }
    if (/*auto* CE = */dyn_cast<CXXNullPtrLiteralExpr>(E)) {
        DEBUG_AST1(" CXXNullPtrLiteralExpr");
        return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
    }
    if (auto* ILE = dyn_cast<InitListExpr>(E)) {
        DEBUG_AST1(" InitListExpr");
        // The semantic form includes omitted fields and default member values.
        if (ILE->isSyntacticForm() && ILE->getSemanticForm())
            ILE = ILE->getSemanticForm();
        if (!ILE->getNumInits()) {
            return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
        }
        auto* record = ILE->getType()->getAsCXXRecordDecl();
        if (record && record->isUnion()) {
            // A packed union adds no bits or aggregate layer to its active
            // member. In particular, wrapping a scalar raw value in an SV
            // pattern instead initializes the first (reversed) union member.
            return exprToExpr(ILE->getInit(0));
        }
        auto expr = cpphdl::Expr{"init", cpphdl::Expr::EXPR_INIT};
        unsigned index = 0;
        for (const Expr *Init : ILE->inits()) {
            auto child = exprToExpr(Init);
            if (record && index < record->getNumBases()
                && child.type == cpphdl::Expr::EXPR_INIT && child.value == "init") {
                // exportStruct flattens base-class fields into the derived
                // type; only those initializer layers must be flattened too.
                for (auto& field : child.sub) expr.sub.emplace_back(std::move(field));
            } else {
                expr.sub.emplace_back(std::move(child));
            }
            ++index;
        }
        return expr;
    }
    if (auto* UETTE = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        DEBUG_AST1(" UnaryExprOrTypeTraitExpr");

        std::string op;
        switch (UETTE->getKind()) {
            case UETT_SizeOf:   op = "sizeof"; break;
            case UETT_AlignOf:  op = "alignof"; break;
            case UETT_VecStep:  op = "vecstep"; break;
            case UETT_PreferredAlignOf: op = "preferred_alignof"; break;
            case UETT_OpenMPRequiredSimdAlign: op = "omp required simd align"; break;
            default: op = "unknown_trait"; break;
        }

        if (UETTE->getKind() == UETT_SizeOf && mod) {
            // sizeof is an unevaluated type dependency, even when spelled
            // sizeof(expr). No variable/port declaration need import this
            // record elsewhere. Use the exported specialization identity,
            // not the spelling of a cv-qualified type or alias.
            auto* record = resolveCXXRecordDecl(UETTE->getTypeOfArgument());
            if (cpphdlRecordShouldExportAsStruct(record, *this)) {
                auto st = exportStruct(record, *this);
                const auto name = st.name;
                if (std::none_of(mod->imports.begin(), mod->imports.end(),
                        [&](const auto& imp) { return imp.name == name; })) {
                    mod->imports.emplace_back(name);
                    currProject->structs.emplace_back(std::move(st));
                }
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT,
                    {cpphdl::Expr{name, cpphdl::Expr::EXPR_TYPE}}};
            }
        }

        if (UETTE->isArgumentType()) {
            auto QT = UETTE->getArgumentType().getNonReferenceType().getDesugaredType(*ctx);
            if (!QT->getAs<RecordType>()) {  // cant know the type - will try to replace it later
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {cpphdl::Expr{genTypeName(QT.getAsString()),cpphdl::Expr::EXPR_TYPE}}};
            }
            return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {cpphdl::Expr{genTypeName(QT.getAsString()),cpphdl::Expr::EXPR_TYPE}}};
        } else {
            if (UETTE->getArgumentExpr()) {
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {exprToExpr(UETTE->getArgumentExpr())}};
            }
        }
    }
    if (auto* PE = dyn_cast<ParenExpr>(E)) {
        DEBUG_AST1(" ParenExpr");
        return cpphdl::Expr{"paren", cpphdl::Expr::EXPR_PAREN, {exprToExpr(PE->getSubExpr())}};
    }
    if (auto* SNTTPE = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {  // all places in code where template parameter is used
        DEBUG_AST1(" SubstNonTypeTemplateParmExpr");

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL");
        }
        auto replacement = exprToExpr(SNTTPE->getReplacement());
        return cpphdl::Expr{replacement.value, cpphdl::Expr::EXPR_PARAM, {cpphdl::Expr{SNTTPE->getParameter()->getName().str(), cpphdl::Expr::EXPR_VAR}},
            ((flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_SPECVAL:0U)};  // FLAG_SPECVAL used in structures definitions when we need numbers, not expressions
    }
    if (auto* CFE = dyn_cast<CXXFoldExpr>(E)) {
        DEBUG_AST1(" CXXFoldExpr");
        auto pattern = CFE->getPattern()->IgnoreParenImpCasts();
        auto expr = exprToExpr(pattern);
//        if (expr.type == cpphdl::Expr::EXPR_PAREN && expr.sub.size() > 0) {
//            expr = expr.sub[0];
//        }

        return cpphdl::Expr{"CXXFoldExpr", cpphdl::Expr::EXPR_BODY, {expr}};
    }
    if (auto* DIE = dyn_cast<CXXDefaultInitExpr>(E)) {
        return exprToExpr(DIE->getExpr());
    }
    if (isa<ImplicitValueInitExpr>(E) || isa<CXXScalarValueInitExpr>(E)) {
        // A value-initialized field is present and zero, not a missing field.
        return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
    }
    if (auto* SOPE = dyn_cast<SizeOfPackExpr>(E)) {
        if (!SOPE->isValueDependent() && !SOPE->isTypeDependent()) {
            auto len = SOPE->getPackLength();
            DEBUG_AST1(" SizeOfPackExpr: " << len);
            return cpphdl::Expr{std::to_string(len), cpphdl::Expr::EXPR_NUM};
        }
        DEBUG_AST1(" SizeOfPackExpr: " << -1);
        return cpphdl::Expr{"-1", cpphdl::Expr::EXPR_NUM};
    }
    if (auto* DSDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST1(" DependentScopeDeclRefExpr");
        const std::string qualifier = dependentQualifierText(DSDRE, ctx->getPrintingPolicy());
        if (!qualifier.empty()) {
            if (qualifierIsLocalModuleContext(qualifier, parent, mod)) {
                return cpphdl::Expr{DSDRE->getDeclName().getAsString(), cpphdl::Expr::EXPR_VAR};
            }
            return cpphdl::Expr{genTypeName(qualifier) + "_pkg::" + DSDRE->getDeclName().getAsString(), cpphdl::Expr::EXPR_VAR};
        }
        return cpphdl::Expr{DSDRE->getDeclName().getAsString(), cpphdl::Expr::EXPR_VAR};
    }
/*
    if (auto* FL = dyn_cast<FloatingLiteral>(E)) {
        DEBUG_AST1(" FloatingLiteral");
//        return cpphdl::Expr{std::to_string(FL->getValue()), cpphdl::Expr::EXPR_NUM};
    }
    if (auto* ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
        DEBUG_AST1(" UnresolvedLookupExpr");
    }
    if (auto* DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST1(" DesignatedInitExpr");
    }
    if (auto* TOE = dyn_cast<CXXTemporaryObjectExpr>(E)) {
        DEBUG_AST1(" CXXTemporaryObjectExpr");
    }
    if (auto* BCO = dyn_cast<BinaryConditionalOperator>(E)) {
        DEBUG_AST1(" BinaryConditionalOperator");
    }
*/
    if (auto* SILE = dyn_cast<CXXStdInitializerListExpr>(E)) {
        DEBUG_AST1(" CXXStdInitializerListExpr");
        auto expr = exprToExpr(SILE->getSubExpr());
        if (expr.type == cpphdl::Expr::EXPR_INIT) {
            if (expr.sub.empty()) {
                return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
            }
            if (expr.sub.size() == 1) {
                return expr.sub[0];
            }
        }
        return expr;
    }
/*
    if (auto* DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST1(" DesignatedInitExpr");
    }
    if (auto* OOE = dyn_cast<OffsetOfExpr>(E)) {
        DEBUG_AST1(" OffsetOfExpr");
    }
    if (auto* FE = dyn_cast<FullExpr>(E)) {
        DEBUG_AST1(" FullExpr");
    }
*/
    else {
        DEBUG_AST1(" unknown: " << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "(" << E->getStmtClassName() << ")");

        return cpphdl::Expr{std::string(Lexer::getSourceText(Range, SM, LangOpts)) + "(" + E->getStmtClassName() + ")", cpphdl::Expr::EXPR_UNKNOWN};
    }
    ASSERT(0);
    return cpphdl::Expr{"", cpphdl::Expr::EXPR_UNKNOWN};
}

void Helpers::ArgToExpr(const TemplateArgument& arg, cpphdl::Expr& expr, bool specialization)
{
    std::string str;
    llvm::raw_string_ostream OS(str);
    arg.print(ctx->getPrintingPolicy(), OS, true);
    OS.flush();

    if (arg.getKind() == TemplateArgument::Expression) {
        DEBUG_AST1(" (expression");
        expr.sub.emplace_back(exprToExpr(arg.getAsExpr()));
        DEBUG_AST1(" ),");
    } else
    if (arg.getKind() == TemplateArgument::Pack) {
        DEBUG_AST1(" (pack" << str);
        for (const TemplateArgument &arg1 : arg.getPackAsArray()) {
            DEBUG_AST(debugIndent++, "# pArg "); on_return ret_debug([](){ --debugIndent; });
            ArgToExpr(arg1, expr);
        }
        DEBUG_AST1(" ),");
    } else
    if (arg.getKind() == TemplateArgument::Type) {
        QualType QT = arg.getAsType().getNonReferenceType();//specialization ? /*TSD->getTemplateArgs()[i]*/arg.getAsType().getNonReferenceType() : arg.getAsType().getNonReferenceType();
        DEBUG_AST1(" (type");

        cpphdl::Expr expr1 = digQT(QT);
        expr.sub.emplace_back(std::move(expr1));

        auto* CRD = resolveCXXRecordDecl(QT);  // types embedded in any templates
        if (specialization && cpphdlRecordShouldExportAsStruct(CRD, *this)) {
            auto st = exportStruct(CRD, *this);
            if (std::find_if(mod->imports.begin(), mod->imports.end(), [&](auto& imp){ return imp.name == st.name; }) == mod->imports.end()) {
                auto ret = mod->imports.emplace_back(st.name);
                currProject->structs.emplace_back(std::move(st));
            }
        }
        DEBUG_AST1("), ");
    } else
    if (arg.getKind() == TemplateArgument::Template) {
        cpphdl::Expr expr1;
        DEBUG_AST1(" (template" << str << " ),");
    } else
    if (arg.getKind() == TemplateArgument::Integral) {
        if (str_ending(str, "LL")) {
            str = str.replace(str.rfind("LL"), 2, "");
        }
        if (str_ending(str, "L")) {
            str = str.replace(str.rfind("L"), 1, "");
        }
        if (str_ending(str, "U")) {
            str = str.replace(str.rfind("U"), 1, "");
        }
        cpphdl::Expr expr1 = cpphdl::Expr{arg.getIntegralType()->isBooleanType() ? (arg.getAsIntegral().isZero() ? "false" : "true") : str, cpphdl::Expr::EXPR_NUM};
        expr.sub.emplace_back(std::move(expr1));
        DEBUG_AST1("(integral " << str << "),");
    } else
    if (arg.getKind() == TemplateArgument::Declaration) {
        // Declaration non-type template args, such as string tag variables,
        // participate in specialization identity even though they are not SV
        // module parameters.
        if (const auto* ND = dyn_cast_or_null<NamedDecl>(arg.getAsDecl())) {
            cpphdl::Expr expr1{genTypeName(ND->getNameAsString()), cpphdl::Expr::EXPR_VAR};
            if (const auto* VD = dyn_cast<VarDecl>(ND)) {
                if (const clang::Expr* init = VD->getInit()) {
                    init = init->IgnoreParenImpCasts();
                    if (const auto* SL = dyn_cast<StringLiteral>(init)) {
                        expr1 = cpphdl::Expr{SL->getString().str(), cpphdl::Expr::EXPR_STRING};
                    }
                }
            }
            expr.sub.emplace_back(std::move(expr1));
        }
        DEBUG_AST1("(decl " << str << "),");
    } else {
        DEBUG_AST1("(unhandled " << str << "),");
    }
}

bool Helpers::templateToExpr(QualType QT, cpphdl::Expr& expr)
{
    std::string str;
    llvm::raw_string_ostream OS(str);
    QT.print(OS, ctx->getPrintingPolicy());
    OS.flush();
    DEBUG_AST1(/*debugIndent++, */" templateToExpr: " << str);//    on_return ret_debug([](){ --debugIndent; });

    auto* CRD = resolveCXXRecordDecl(QT);
    auto* TSD = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(CRD ? CRD->getDefinition() : CRD);
    auto* TST = QT->getAs<TemplateSpecializationType>();
    if (TSD || TST) {
        if (TSD) {
            DEBUG_AST1(" TSD");
        }

        expr.type = cpphdl::Expr::EXPR_TEMPLATE;
        expr.value = genTypeName(TSD ? TSD->getSpecializedTemplate()->getQualifiedNameAsString()
                         : TST->getTemplateName().getAsTemplateDecl()->getQualifiedNameAsString());

        [[maybe_unused]] size_t i=0;
        for (const auto &arg : (TSD ? ArrayRef<TemplateArgument>(TSD->getTemplateArgs().asArray()) : TST->template_arguments())) {
            DEBUG_AST(debugIndent++, "# Arg " << (TSD ? TSD->getSpecializedTemplate()->getTemplateParameters()->getParam(i)->getNameAsString() : "?")); on_return ret_debug([](){ --debugIndent; });
            ArgToExpr(arg, expr, TSD != nullptr);
            ++i;
        }

        expr.sub.push_back(cpphdl::Expr{expr.value, cpphdl::Expr::EXPR_TYPE});  // subject to call
        return true;
    }
    return false;
}

cpphdl::Expr Helpers::digQT(QualType& QT)
{
    cpphdl::Expr arrayExpr;
    bool array = false;
    while (const clang::ArrayType* AT = ctx->getAsArrayType(QT)) {
        if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
            DEBUG_AST1(" [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
            arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_NUM});
            arrayExpr.value = "c_array";
        } else
        if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
            DEBUG_AST1(" [v_array");
            arrayExpr.sub.push_back(exprToExpr(VAT->getSizeExpr()));
            DEBUG_AST1("] ");
            arrayExpr.value = "v_array";
        } else
        if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
            DEBUG_AST1(" [d_array");
            arrayExpr.sub.push_back(exprToExpr(DSAT->getSizeExpr()));
            DEBUG_AST1("] ");
            arrayExpr.value = "d_array";
        }

        arrayExpr.type = cpphdl::Expr::EXPR_ARRAY;
        QT = AT->getElementType();
        array = true;
    }

    cpphdl::Expr expr;
    DEBUG_AST1(" (");
    if (templateToExpr(QT, expr)) {
        DEBUG_AST1(" TEMPLATE) ");
    }
    else {
        QT = QT.getDesugaredType(*ctx);
//?        QT = QT.getCanonicalType();
        std::string str = QT.getAsString(ctx->getPrintingPolicy());
        DEBUG_AST1(" TYPE) ");
        if (QT->isIntegerType() && !QT->isBooleanType()) {
            const auto width = ctx->getTypeSize(QT);
            // Keep the established declaration spelling for ordinary widths,
            // while using the ABI's actual width/sign for char, wchar_t, etc.
            expr.value = (width == 8 || width == 16 || width == 32 || width == 64)
                ? std::string(QT->isSignedIntegerType() ? "int" : "uint") + std::to_string(width) + "_t"
                : castTypeName(QT);
        } else {
            expr.value = genTypeName(str);
            if (const auto* type = QT->getAs<EnumType>(); type && mod)
                addEnumPackageImport(type->getDecl(), mod->imports);
        }
        expr.type = cpphdl::Expr::EXPR_TYPE;
    }

    if (array) {
        arrayExpr.sub.push_back(std::move(expr));
        expr = std::move(arrayExpr);
    }
    return expr;
}

void Helpers::followSpecialization(const CXXRecordDecl* RD, std::string& name, std::vector<cpphdl::Field>* params, bool onlyTypes)
{
    if (auto* CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        const TemplateArgumentList& args = CTSD->getTemplateArgs();
        const TemplateParameterList* Params = CTSD->getSpecializedTemplate()->getTemplateParameters();
        DEBUG_AST1(" followSpecialization: ");
        bool first = true;
        for (unsigned i = 0; i < args.size(); ++i) {
            if (args[i].getKind() == TemplateArgument::Pack) {
                for (const TemplateArgument &arg : args[i].getPackAsArray()) {
                    DEBUG_AST(debugIndent++, "# pArg " << Params->getParam(i)->getNameAsString() << ": "); on_return ret_debug([](){ --debugIndent; });
                    cpphdl::Expr tmp;
                    // Specialization names only need the type expression. Exporting a
                    // type argument here can recursively re-enter exportStruct().
                    ArgToExpr(arg, tmp, false);
                    for (auto& expr: tmp.sub) {
                        if (genSpecializationTypeName(first, name, expr, onlyTypes)) {
                            first = false;
                        }
                        if (params) {
                            params->emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), std::move(expr)});
                        }
                    }
                }
                continue;
            }
            DEBUG_AST(debugIndent++, "# Arg " << Params->getParam(i)->getNameAsString() << ": "); on_return ret_debug([](){ --debugIndent; });
            cpphdl::Expr tmp;
            // Specialization names only need the type expression. Exporting a
            // type argument here can recursively re-enter exportStruct().
            ArgToExpr(args[i], tmp, false);
            for (auto& expr: tmp.sub) {
                if (genSpecializationTypeName(first, name, expr, onlyTypes)) {
                    first = false;
                }
                if (params) {
                    cpphdl::Field field{Params->getParam(i)->getNameAsString(), std::move(expr)};
                    if (const auto* NTTP = dyn_cast<NonTypeTemplateParmDecl>(Params->getParam(i));
                        NTTP && NTTP->hasDefaultArgument()) {
                        cpphdl::Expr defaultExpr;
                        ArgToExpr(NTTP->getDefaultArgument().getArgument(), defaultExpr, false);
                        if (!defaultExpr.sub.empty()) {
                            field.initializer = std::move(defaultExpr.sub.front());
                        }
                    }
                    params->emplace_back(std::move(field));
                }
            }
        }
    }
}
/*
const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext* ctx)
{
    DynTypedNode Node = DynTypedNode::create(*DRE);

    while (true) {
        auto parents = ctx->getParents(Node);

        if (parents.empty())
            return nullptr;

        const DynTypedNode &P = parents[0];

        if (const Decl *D = P.get<Decl>()) {
            if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
                return MD->getParent();

            if (const auto *FD = dyn_cast<FieldDecl>(D))
                return dyn_cast<CXXRecordDecl>(FD->getParent());

            if (const auto *RD = dyn_cast<CXXRecordDecl>(D))
                return RD;

            Node = P;
            continue;
        }

        if (P.get<Stmt>()) {
            Node = P;
            continue;
        }

        return nullptr;
    }
}
*/
bool Helpers::genSpecializationTypeName(bool first, std::string& name, cpphdl::Expr& param, bool onlyTypes)
{
    if (!onlyTypes || param.type != cpphdl::Expr::EXPR_NUM) {
        if (!first) {
            name += "_";
        }
        name += param.specializationName();
    }
    str_replace(name, "::", "_");
    return !onlyTypes || param.type != cpphdl::Expr::EXPR_NUM;
}

bool Helpers::skipStdFunctionType(QualType& QT)
{
    if (const auto *Record = QT->getAs<RecordType>()) {
        if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(Record->getDecl())) {
            if (const TemplateDecl *TD = Spec->getSpecializedTemplate()) {
                if (const auto *II = TD->getIdentifier()) {
                    if (II->getName() == "function") {
                        DEBUG_AST1(" *function*");
                        const DeclContext *DC = TD->getDeclContext();
                        if (DC->isNamespace()) {
                            if (const auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                                if (NS->getName() == "std" || NS->getName() == "__1") {
                                    DEBUG_AST1(" *std*");
                                    const TemplateArgument &arg = Spec->getTemplateArgs().get(0);
                                    if (arg.getKind() == TemplateArgument::Type) {
                                        QualType FuncTypeQT = arg.getAsType();
                                        if (const FunctionType *FT = FuncTypeQT->getAs<FunctionType>()) {
                                            QT = FT->getReturnType();
                                            DEBUG_AST1(" (" << QT.getAsString() << ") ");
                                            return true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (II->getName() == "function_ref") {
                        DEBUG_AST1(" *function_ref*");
                        const DeclContext *DC = TD->getDeclContext();
                        if (DC->isNamespace()) {
                            if (const auto *NS = dyn_cast<NamespaceDecl>(DC)) {
                                if (NS->getName() == "cpphdl" || NS->getName() == "__1") {
                                    DEBUG_AST1(" *cpphdl*");
                                    const TemplateArgument &arg = Spec->getTemplateArgs().get(0);
                                    if (arg.getKind() == TemplateArgument::Type) {
                                        QT = arg.getAsType();
                                        DEBUG_AST1(" (" << QT.getAsString() << ") ");
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (const auto *TST = QT->getAs<TemplateSpecializationType>()) {
        const TemplateName TN = TST->getTemplateName();
        if (const TemplateDecl *TD = TN.getAsTemplateDecl()) {
            if (TD->getName() == "function") {
                DEBUG_AST1(" *function*");
                if (const auto *NS = dyn_cast<NamespaceDecl>(TD->getDeclContext())) {
                    if (NS->getName() == "std" || NS->getName() == "__1") {
                        DEBUG_AST1(" *std*");
                        auto& arg = TST->template_arguments()[0];
                                    if (arg.getKind() == TemplateArgument::Type) {
                                        QualType FuncTypeQT = arg.getAsType();
                                        if (const FunctionType *FT = FuncTypeQT->getAs<FunctionType>()) {
                                            QT = FT->getReturnType();
                                            DEBUG_AST1(" (" << QT.getAsString() << ") ");
                                            return true;
                                        }
                                    }
                    }
                }
            }
            if (TD->getName() == "function_ref") {
                DEBUG_AST1(" *function_ref*");
                if (const auto *NS = dyn_cast<NamespaceDecl>(TD->getDeclContext())) {
                    if (NS->getName() == "cpphdl") {
                        DEBUG_AST1(" *cpphdl*");
                        auto& arg = TST->template_arguments()[0];
                                    if (arg.getKind() == TemplateArgument::Type) {
                                        QT = arg.getAsType();
                                        DEBUG_AST1(" (" << QT.getAsString() << ") ");
                                        return true;
                                    }
                    }
                }
            }
        }
    }
    return false;
}

CXXRecordDecl* Helpers::resolveCXXRecordDecl(QualType QT)
{
    auto* CRD = QT->getAsCXXRecordDecl();

    QT = QT.getNonReferenceType();
    QT = QT.getDesugaredType(*ctx); // remove typedefs, aliases, etc.

    if (const auto* RT = QT->getAs<RecordType>()) {
        if (cast<CXXRecordDecl>(RT->getDecl())) {
            CRD = cast<CXXRecordDecl>(RT->getDecl());
        }
    }
    return CRD;
}

CXXRecordDecl* Helpers::resolveInterfaceRecordDecl(QualType QT)
{
    QT = QT.getNonReferenceType().getDesugaredType(*ctx);
    auto* record = resolveCXXRecordDecl(QT);
    if (!record) {
        if (const auto* type = QT->getAs<TemplateSpecializationType>()) {
            if (const auto* templ = dyn_cast_or_null<ClassTemplateDecl>(type->getTemplateName().getAsTemplateDecl())) {
                record = templ->getTemplatedDecl();
            }
        }
    }
    auto* interface = lookupQualifiedRecord("cpphdl::Interface");
    return record && record->getDefinition() && interface && record->isDerivedFrom(interface)
        ? record->getDefinition() : nullptr;
}

NamedDecl* Helpers::lookupInContext(DeclContext *DC, IdentifierInfo *Id)
{
    auto Result = DC->lookup(Id);
    if (!Result.empty()) {
        return Result.front();
    }

    for (auto *D : DC->decls()) {
        if (auto *NS = dyn_cast<NamespaceDecl>(D)) {
            if (NS->isInline()) {
                if (auto *ND = lookupInContext(NS, Id))
                    return ND;
            }
        }
    }
    return nullptr;
}

CXXRecordDecl* Helpers::lookupQualifiedRecord(llvm::StringRef QualifiedName)
{
    SmallVector<StringRef, 4> Parts;
    QualifiedName.split(Parts, "::");

    DeclContext *DC = ctx->getTranslationUnitDecl();

    for (unsigned i = 0; i < Parts.size(); ++i) {
        IdentifierInfo &Id = ctx->Idents.get(Parts[i]);

        NamedDecl *ND = lookupInContext(DC, &Id);
        if (!ND) {
            return nullptr;
        }

        if (i + 1 < Parts.size()) {
            if (auto *NS = dyn_cast<NamespaceDecl>(ND)) {
                DC = NS;
                continue;
            }
            if (auto *RD = dyn_cast<CXXRecordDecl>(ND)) {
                DC = RD;
                continue;
            }
            return nullptr;
        }

        if (auto *RD = dyn_cast<CXXRecordDecl>(ND)) {
            if (auto *Def = RD->getDefinition()) {
                return Def;//->getCanonicalDecl();
            }
            return RD;//->getCanonicalDecl();
        }
        return nullptr;
    }
    return nullptr;
}

void Helpers::forEachBase(const CXXRecordDecl *RD, const std::function<void(const CXXRecordDecl *)>& func, std::unordered_set<const CXXRecordDecl*>* visited)
{
    std::unordered_set<const CXXRecordDecl*> set;
    if (!visited) {
        visited = &set;
    }

    func(RD);

    RD = RD->getDefinition();
    if (!RD) {
        return;
    }
//    RD = RD->getCanonicalDecl();

    if (!visited->insert(RD).second) {
        return;
    }

    for (const CXXBaseSpecifier &Base : RD->bases()) {
        QualType QT = Base.getType();

        if (const auto *RT = QT->getAs<RecordType>()) {  // classes
            const auto *BaseRD = dyn_cast<CXXRecordDecl>(RT->getDecl());

            if (!BaseRD) {
                continue;
            }

//            BaseRD = BaseRD->getCanonicalDecl();
            func(BaseRD);
            forEachBase(BaseRD, func, visited);
            continue;
        }

        if (QT->isDependentType()) {  // templates  (hey, Clang team, template classes are classes too, do you hear me??)
            if (const auto *TST = QT->getAs<TemplateSpecializationType>()) {
                if (const TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl()) {
                    if (const auto *CTD = dyn_cast<ClassTemplateDecl>(TD)) {
                        const CXXRecordDecl *TemplRD = CTD->getTemplatedDecl()->getCanonicalDecl();

                        func(TemplRD);
                        forEachBase(TemplRD, func, visited);
                    }
                }
            }
        }
    }
}

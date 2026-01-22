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

#include <iostream>

unsigned debugIndent = 0;

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp, cpphdl::Struct* st = nullptr);
std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* ctx, llvm::StringRef QualifiedName);
const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext &ctx);

cpphdl::Expr Helpers::exprToExpr(const Stmt* E)
{
    SourceManager &SM = ctx.getSourceManager();
    LangOptions LangOpts = ctx.getLangOpts();
    SourceLocation StartLoc = E->getBeginLoc();
    SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);
    if (StartLoc.isMacroID()) {
        StartLoc = SM.getSpellingLoc(StartLoc);
        EndLoc   = SM.getSpellingLoc(EndLoc);
    }
    CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);
    DEBUG_AST(debugIndent++, " exprToExpr(" << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "): {"); on_return ret_debug([](){ --debugIndent; });
    on_return ret_debug1([](){ DEBUG_AST1("}"); });

    if (auto* FS = dyn_cast<ForStmt>(E)) {
        DEBUG_AST1(" ForStmt");

        cpphdl::Expr expr = cpphdl::Expr{"for", cpphdl::Expr::EXPR_FOR};

        if (FS->getInit()) {
            expr.sub.push_back(exprToExpr(FS->getInit()));
        }
        if (FS->getCond()) {
            expr.sub.push_back(exprToExpr(FS->getCond()));
        }
        if (FS->getInc()) {
            expr.sub.push_back(exprToExpr(FS->getInc()));
        }

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
        cpphdl::Expr expr = cpphdl::Expr{"switch", cpphdl::Expr::EXPR_SWITCH, {exprToExpr(SS->getCond())}};
        for (const SwitchCase *SC = SS->getSwitchCaseList(); SC; SC = SC->getNextSwitchCase()) {
            if (isa<SwitchCase>(SC->getSubStmt()) || isa<DefaultStmt>(SC->getSubStmt())) {
                const SourceManager &SM = ctx.getSourceManager();
                PresumedLoc loc = SM.getPresumedLoc(SS->getSwitchLoc());
                std::cerr << "WARNING: case is not terminated by break or return, " << loc.getFilename() << ":" << loc.getLine() << "\n";
            }
            if (auto *CS = dyn_cast<CaseStmt>(SC)) {
                cpphdl::Expr LHS = exprToExpr(CS->getLHS());
                cpphdl::Expr RHS = exprToExpr(CS->getSubStmt());
                expr.sub.emplace_back(cpphdl::Expr{LHS.str(), cpphdl::Expr::EXPR_BODY, {std::move(RHS)}});  // the only one place we call str() in Clang part
            } else
            if (isa<DefaultStmt>(SC)) {
                cpphdl::Expr RHS = exprToExpr(SC->getSubStmt());
                expr.sub.emplace_back(cpphdl::Expr{"default", cpphdl::Expr::EXPR_BODY, {std::move(RHS)}});
            }
        }
        return expr;
    }
    if (/*auto* CS =*/ dyn_cast<CaseStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_EMPTY};
    }
    if (/*auto* BS =*/ dyn_cast<BreakStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_EMPTY};
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

        cpphdl::Expr expr = cpphdl::Expr{"", cpphdl::Expr::EXPR_EMPTY};
        return expr;
    }
    if (auto* DS = dyn_cast<DeclStmt>(E)) {
        DEBUG_AST1(" DeclStmt");
        auto expr = cpphdl::Expr{"decl", cpphdl::Expr::EXPR_BODY};
        for (Decl* D : DS->decls()) {
            if (auto* VD = dyn_cast<VarDecl>(D)) {
                if (VD->getType()->isReferenceType()) {
                    // ignore
                }
                else {
                    DEBUG_AST1(" VarDecl(" << VD->getName().str() << ")");
                    if (VD->getInit()) {
                        expr.sub.push_back(cpphdl::Expr{VD->getName().str(), cpphdl::Expr::EXPR_DECLARE,
                                            {cpphdl::Expr{VD->getType().getAsString(),cpphdl::Expr::EXPR_TYPE}, exprToExpr(VD->getInit())}});
                    }
                    else {
                        expr.sub.push_back(cpphdl::Expr{VD->getName().str(), cpphdl::Expr::EXPR_DECLARE,
                                            {cpphdl::Expr{VD->getType().getAsString(),cpphdl::Expr::EXPR_TYPE}}});
                    }

                    auto* CRD = resolveCXXRecordDecl(VD->getType());
                    if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
                        auto st = exportStruct(CRD, *this);
                        auto ret = mod.imports.emplace(st.name);
                        if (ret.second) {
                            currProject->structs.emplace_back(std::move(st));
                        }
                    }
                }
            }
        }
        return expr;
    }
    if (auto* UO = dyn_cast<UnaryOperator>(E)) {
        DEBUG_AST1(" UnaryOperator");
        auto expr = exprToExpr(UO->getSubExpr());
        QualType LQT = UO->getSubExpr()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (UO->getOpcodeStr(UO->getOpcode()) == "*" && LQT->isPointerType()) {  // convert pointer add into index
            std::string typeSize;
            if (const CXXRecordDecl* RD = LQT->getPointeeType().getNonReferenceType()->getAsCXXRecordDecl()) {
                typeSize = std::string("$bits(") + RD->getQualifiedNameAsString() + ")/8";
            }
            else {
                typeSize = std::string("$bits(") + LQT->getPointeeType().getNonReferenceType().getDesugaredType(ctx).getAsString(ctx.getPrintingPolicy()) + ")/8";
            }
            bool found = false;
            expr.traverseIf( [&](auto& e) {  // we support only one substitution in pack
                    if (e.type == cpphdl::Expr::EXPR_INDEX) {
                        e.value = std::string("+: ") + typeSize;
                        found = true;
                        return true;
                    }
                    return false;
                });
            if (found) {
                return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {expr}};
            } else {
                return cpphdl::Expr{std::string("+: ") + typeSize,
                                   cpphdl::Expr::EXPR_INDEX, {expr, cpphdl::Expr{"0",cpphdl::Expr::EXPR_NUM}}};
            }
        }
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {expr}};
    }
    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST1(" BinaryOperator(" << BO->getOpcodeStr().data() << ")");
        QualType LQT = BO->getLHS()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (BO->getOpcodeStr() == "+" && LQT->isPointerType()) {  // convert pointer add into index
            return cpphdl::Expr{std::string("+:") + std::to_string(ctx.getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
                                   cpphdl::Expr::EXPR_INDEX, {exprToExpr(BO->getLHS()),exprToExpr(BO->getRHS())}};
        }
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS()),exprToExpr(BO->getRHS())}};
    }
    if (auto* CAO = dyn_cast<CompoundAssignOperator>(E)) {
        DEBUG_AST1(" CompoundAssignOperator(" << CAO->getOpcodeStr().data() << ")");
        return cpphdl::Expr{CAO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(CAO->getLHS()),exprToExpr(CAO->getRHS())}};
    }
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST1(" DeclRefExpr(" << DRE->getNameInfo().getAsString() << ")");

        const auto* parent = getParentClassOfExpr(DRE, ctx);
        std::string prefix;
        if (const auto *ECD = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {  // make enum pkg
            if (const auto *ED = dyn_cast<EnumDecl>(ECD->getDeclContext())) {
                std::cout << " EnumName: " << ED->getQualifiedNameAsString() << "\n";

                auto en = cpphdl::Enum{genTypeName(ED->getQualifiedNameAsString()), ED->getQualifiedNameAsString()};

                 for (const EnumConstantDecl *ECD : ED->enumerators()) {
                    if (ECD->getInitExpr()) {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str(), {exprToExpr(ECD->getInitExpr())}});
                    }
                    else {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str()});
                    }
                }

                prefix += en.name + "_pkg::";

                auto ret = mod.imports.emplace(en.name);
                if (ret.second) {
                    currProject->enums.emplace_back(std::move(en));
                }
            }
        } else
        if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (parent && VD->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
                std::string sname = parent->getQualifiedNameAsString();
                str_replace(sname, "::", "_");
                // extracting parameters of the template
                followSpecialization(parent, sname);
                prefix = sname + "_pkg::";
            }
        }

        bool isPack = false;
        if (const ParmVarDecl* PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
             if (PVD && PVD->isParameterPack()) {
                 isPack = true;
                 prefix += "_substitution_";
             }
        }

        return cpphdl::Expr{prefix + DRE->getDecl()->getNameAsString(), isPack ? cpphdl::Expr::EXPR_PACK : cpphdl::Expr::EXPR_VAR};
    }
    if (auto* IL = dyn_cast<IntegerLiteral>(E)) {
        DEBUG_AST1(" IntegerLiteral(" << std::to_string(IL->getValue().getSExtValue()) << ")");
        return cpphdl::Expr{std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_NUM};
    }
    if (auto* OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
        DEBUG_AST1(" CXXOperatorCallExpr");
        cpphdl::Expr call = cpphdl::Expr{getOperatorSpelling(OCE->getOperator()), cpphdl::Expr::EXPR_OPERATORCALL};
        for (unsigned i = 0; i < OCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(OCE->getArg(i)));
        }
        return call;
    }
    if (auto* MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        DEBUG_AST1(" CXXMemberCallExpr(" << (MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():"") << ")");
        cpphdl::Expr call = cpphdl::Expr{(MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_MEMBERCALL};

        if (auto* ME = dyn_cast<MemberExpr>(MCE->getCallee())) {
             call.sub.push_back(exprToExpr(ME->getBase())/*cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase())}}*/);
        }
        for (unsigned i = 0; i < MCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(MCE->getArg(i)));
        }

        if (auto *MD = MCE->getMethodDecl()) {
            DEBUG_AST1(" Called method( " << MD->getQualifiedNameAsString() << ")");
            auto newName = putMethod(MD, *this);
            if (newName.length()) {
                call.value = newName;
            }
        }

        return call;
    }
    if (auto* ME = dyn_cast<MemberExpr>(E)) {
        DEBUG_AST1(" MemberExpr(" << ME->getMemberDecl()->getNameAsString() << ")");
        bool anon = false;
        const CXXRecordDecl *parent = dyn_cast<CXXRecordDecl>(ME->getMemberDecl()->getDeclContext());
        if (/*const auto* FD =*/ dyn_cast<FieldDecl>(ME->getMemberDecl())) {
//            parent = FD->getParent();
            if (parent && parent->isAnonymousStructOrUnion()) {
                anon = true;
                DEBUG_AST1(" ANON");
            }
        }

        bool ignoreBase = false;
        std::string prefix;
        if (const auto* VD = dyn_cast<VarDecl>(ME->getMemberDecl())) {
            if (parent && VD->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
                std::string sname = parent->getQualifiedNameAsString();
                str_replace(sname, "::", "_");
                // extracting parameters of the template
                followSpecialization(parent, sname);
                prefix = sname + "_pkg::";
                ignoreBase = true;
            }
        }

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL");
        }

//        std::string ename = ME->getMemberDecl()->getNameAsString();
//        for (auto parent : parents) {
//            if (refs[parent].find(ename) != refs[parent].end()) {
////                ename = refs[parent][ename];
//            }
//        }

        const Expr *base = ME->getBase()->IgnoreParenImpCasts();  // get init
        if (const auto *DRE = dyn_cast<DeclRefExpr>(base)) {
            if (const ValueDecl *VD = DRE->getDecl()) {
                if (const auto *Var = dyn_cast<VarDecl>(VD)) {
                    if (Var->hasInit()) {
                        base = Var->getInit();
                    }
                }
            }
        }

        return cpphdl::Expr{prefix + ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(base)},
                (anon?cpphdl::Expr::FLAG_ANON:0U) | ((flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_USETHIS:0U) | (ignoreBase?cpphdl::Expr::FLAG_NOBASE:0U)};
    }
    if (auto* CDSME = dyn_cast<CXXDependentScopeMemberExpr>(E)) {
        DEBUG_AST1(" CXXDependentScopeMemberExpr(" << CDSME->getMemberNameInfo().getAsString() << ")");

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL");
        }

        return cpphdl::Expr{CDSME->getMemberNameInfo().getAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(CDSME->getBase())},
                (flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_USETHIS:0U};
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
        DEBUG_AST1(" CallExpr");
        cpphdl::Expr call = cpphdl::Expr{"unknown", cpphdl::Expr::EXPR_CALL};
        const clang::Expr* callee = CE->getCallee()->IgnoreParenImpCasts();

        if (const auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(callee)) { // we do it only for std::apply
            const clang::FunctionDecl* function = llvm::dyn_cast<clang::FunctionDecl>(DRE->getDecl());
            DEBUG_AST1(" DRE(" << function->getQualifiedNameAsString() << ")");
            call.value = function->getNameAsString();

            if (function->getQualifiedNameAsString() == "std::apply") {
                const Expr* tupleExpr = CE->getArg(1)->IgnoreParenImpCasts();
                QualType QT = tupleExpr->getType();
                QT = QT.getNonReferenceType();
                QT = QT.getUnqualifiedType();
                if (const auto* TST = QT->getAs<TemplateSpecializationType>()) {
                    if (const TemplateDecl* TD = TST->getTemplateName().getAsTemplateDecl()) {
                        if (TD->getQualifiedNameAsString() == "std::tuple") {
                            auto apply = cpphdl::Expr{"apply", cpphdl::Expr::EXPR_BODY};
                            auto expr = exprToExpr(CE->getArg(0)->IgnoreParenImpCasts());
                            size_t i=0;
                            for (const auto& arg : TST->template_arguments()) {
                                expr.traverseIf( [&](auto& e) {  // we support only one substitution in pack
                                            if (e.type == cpphdl::Expr::EXPR_PACK) {
                                                e.value = exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i);
                                            }
                                            size_t pos = -1;
                                            if ((pos = e.value.find("decltype")) != (size_t)-1 && e.value.find("(", pos) != (size_t)-1) {  // we do it only to extract decltype() from std::apply
                                                if ((pos = e.value.rfind("::")) != (size_t)-1 && arg.getKind() == TemplateArgument::Type) {
                                                    std::string name = e.value.substr(pos+2, e.value.rfind(")") != (size_t)-1 ? e.value.rfind(")")-pos-2 : -1);
                                                    std::string type;
                                                    QualType QT1 = arg.getAsType().getNonReferenceType();
                                                    if (const CXXRecordDecl* RD = QT1->getAsCXXRecordDecl()) {
                                                        forEachBase(RD, [&](const CXXRecordDecl* RD) {
                                                                for (const Decl *D : RD->decls()) {
                                                                    if (auto *Alias = dyn_cast<TypeAliasDecl>(D)) {
                                                                        if (Alias->getName() == name) {
                                                                            QualType QT1 = Alias->getUnderlyingType();
                                                                            type = genTypeName(QT1.getAsString(ctx.getPrintingPolicy()));
                                                                        }
                                                                    } else if (auto *TD = dyn_cast<TypeDecl>(D)) {
                                                                        if (TD->getName() == name) {
                                                                            QualType QT1 = ctx.getTypeDeclType(TD);
                                                                            type = genTypeName(QT1.getAsString(ctx.getPrintingPolicy()));
                                                                        }
                                                                    }
                                                                }
                                                        });
                                                    }
                                                    size_t pos1 = e.value.find("typename");
                                                    if (pos1 != (size_t)-1) {
                                                        e.value.replace(pos1, e.value.rfind(")") != (size_t)-1 ? e.value.rfind(")")-pos1 : -1, type.length() ? type + "_pkg::" + type : (exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i)));
                                                    }
                                                    else {
                                                        e.value = type.length() ? type + "_pkg::" + type : (exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i));
                                                    }
                                                } else {
                                                    size_t pos1 = e.value.find("typename");
                                                    if (pos1 != (size_t)-1) {
                                                        e.value.replace(pos1, e.value.rfind(")") != (size_t)-1 ? e.value.rfind(")")-pos1 : -1, exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i));
                                                    }
                                                    else {
                                                        e.value = exprToExpr(tupleExpr).value + "_tuple_" + std::to_string(i);
                                                    }
                                                }
                                            }
                                            return false;
                                        });
                                apply.sub.emplace_back(expr);
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

        if (const auto* ME = llvm::dyn_cast<clang::MemberExpr>(callee)) {
            const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(ME->getMemberDecl());
            DEBUG_AST1(" ME(" << method->getQualifiedNameAsString() << ")");
            call.value = method->getNameAsString();

            if (method && method->isLambdaStaticInvoker()) {
                const auto* RD = method->getParent();
                if (RD && RD->isLambda()) {
                    DEBUG_AST1(" CallExpr LE() not supported");
                }
            }
        }

        if (const auto* LE = llvm::dyn_cast<clang::LambdaExpr>(callee)) {
            DEBUG_AST1(" LE()");
            return exprToExpr(LE);
        }

        if (/*const auto* ULE = */llvm::dyn_cast<clang::UnresolvedLookupExpr>(callee)) {
            DEBUG_AST1(" ULE()");
        }

        if (const auto* DSME = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(callee)) {  // we do this only to get pack inside std::apply
            DEBUG_AST1(" DSME(" << DSME->getMemberNameInfo().getAsString() << ")");
            call.value = DSME->getMemberNameInfo().getAsString();
            call.type = cpphdl::Expr::EXPR_MEMBERCALL;
            auto member = exprToExpr(DSME->getBase());
            member.sub.emplace_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
//            member.type = cpphdl::Expr::EXPR_MEMBER;
            call.sub.emplace_back(member);
        }

        for (auto* arg : CE->arguments()) {
            call.sub.push_back(exprToExpr(arg));
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
        if (const auto *DRE = dyn_cast<DeclRefExpr>(callee->IgnoreParenImpCasts())) {  // template parameters
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
                call = templ;
            }
        }
/*
        if (const auto *ME = dyn_cast<MemberExpr>(Callee)) {
            if (ME->hasExplicitTemplateArgs()) {
                const TemplateArgumentLoc *args = ME->getTemplateArgs();
                for (unsigned i = 0; i < ME->getNumTemplateArgs(); ++i) {
                    const TemplateArgument &arg = args[i].getArgument();
                }
            }

            if (const auto *MD = dyn_cast<CXXMethodDecl>(ME->getMemberDecl())) {
                if (const auto *FTSI = MD->getTemplateSpecializationInfo()) {
                    const TemplateArgumentList *args = FTSI->TemplateArguments;
                    for (const TemplateArgument &arg : args->asArray()) {
                    }
                }
            }
        }

        const FunctionDecl *FD = CE->getDirectCallee();
        if (const auto *FTSI = FD->getTemplateSpecializationInfo()) {
            const TemplateArgumentList *args = FTSI->TemplateArguments;
            for (const TemplateArgument &arg : args->asArray()) {
            }
        }
*/
        return call;
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
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getArg(0))}};
            }
            else {
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST};
            }
        }
    }
    if (auto* CL = dyn_cast<CharacterLiteral>(E)) {
        DEBUG_AST1(" CharacterLiteral");
        return cpphdl::Expr{std::to_string(CL->getValue()), cpphdl::Expr::EXPR_NUM};
    }
    if (auto* CLE = dyn_cast<CompoundLiteralExpr>(E)) {
        DEBUG_AST1(" CompoundLiteralExpr");
        return cpphdl::Expr{CLE->getType().getAsString(), cpphdl::Expr::EXPR_INIT, {{exprToExpr(CLE->getInitializer())}}};
    }
    if (auto* SL = dyn_cast<StringLiteral>(E)) {
        DEBUG_AST1(" StringLiteral");

        clang::SourceRange Range = SL->getSourceRange();
        clang::LangOptions LO = ctx.getLangOpts();

        llvm::StringRef RawText = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(Range),
            ctx.getSourceManager(),
            LO);

        return cpphdl::Expr{RawText.str(), cpphdl::Expr::EXPR_STRING};
    }
    if (auto* BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
        DEBUG_AST1(" CXXBoolLiteralExpr");
        return cpphdl::Expr{BLE->getValue()?"1":"0", cpphdl::Expr::EXPR_NUM};
    }
    if (/*auto* ME =*/dyn_cast<CXXThisExpr>(E)) {
        DEBUG_AST1(" CXXThisExpr");
        return cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR};
    }
    if (auto* CO = dyn_cast<ConditionalOperator>(E)) {
        DEBUG_AST1(" ConditionalOperator");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_COND, {exprToExpr(CO->getCond()),exprToExpr(CO->getTrueExpr()),exprToExpr(CO->getFalseExpr())}};
    }
    if (auto* ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        DEBUG_AST1(" ArraySubscriptExpr");
        QualType LQT = ASE->getBase()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (LQT->isPointerType()) {  // convert pointer add into index
            return cpphdl::Expr{std::string("+:") + std::to_string(ctx.getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
                                   cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase()),exprToExpr(ASE->getIdx())}};
        }
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase()),exprToExpr(ASE->getIdx())}};
    }
    if (auto* FCE = dyn_cast<ImplicitCastExpr>(E)) {
        DEBUG_AST1(" ImplicitCastExpr");
        return cpphdl::Expr{"implicit_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr())}};
    }
    if (auto* FCE = dyn_cast<CXXFunctionalCastExpr>(E)) {
        DEBUG_AST1(" CXXFunctionalCastExpr");
        return cpphdl::Expr{"functional_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr())}};
    }
    if (auto* SCE = dyn_cast<CXXStaticCastExpr>(E)) {
        DEBUG_AST1(" CXXStaticCastExpr");
        return cpphdl::Expr{"static_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr())}};
    }
    if (auto* DCE = dyn_cast<CXXDynamicCastExpr>(E)) {
        DEBUG_AST1(" CXXDynamicCastExpr");
        return cpphdl::Expr{"dynamic_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(DCE->getSubExpr())}};
    }
    if (auto* RCE = dyn_cast<CXXReinterpretCastExpr>(E)) {
        DEBUG_AST1(" CXXReinterpretCastExpr");
        return cpphdl::Expr{"reinterpret_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(RCE->getSubExpr())}};
    }
    if (auto* CCE = dyn_cast<CXXConstCastExpr>(E)) {
        DEBUG_AST1(" CXXConstCastExpr");
        return cpphdl::Expr{"const_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(CCE->getSubExpr())}};
    }
    if (auto* SCE = dyn_cast<CStyleCastExpr>(E)) {
        DEBUG_AST1(" CStyleCastExpr");
        return cpphdl::Expr{"cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr())}};
    }
    if (auto* MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
        DEBUG_AST1(" MaterializeTemporaryExpr");
        return /*cpphdl::Expr{"MaterializeTemporaryExpr", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(MTE->getSubExpr())/*}}*/;
    }
    if (auto* EWC = dyn_cast<ExprWithCleanups>(E)) {
        DEBUG_AST1(" ExprWithCleanups");
        return cpphdl::Expr{"ExprWithCleanups", cpphdl::Expr::EXPR_CAST, {exprToExpr(EWC->getSubExpr())}};
    }
    if (auto* CE = dyn_cast<ConstantExpr>(E)) {
        DEBUG_AST1(" ConstantExpr");
        return cpphdl::Expr{"ConstantExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getSubExpr())}};
    }
    if (auto* BTE = dyn_cast<CXXBindTemporaryExpr>(E)) {
        DEBUG_AST1(" CXXBindTemporaryExpr");
        return cpphdl::Expr{"CXXBindTemporaryExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(BTE->getSubExpr())}};
    }
    if (/*auto* CE = */dyn_cast<CXXNullPtrLiteralExpr>(E)) {
        DEBUG_AST1(" CXXNullPtrLiteralExpr");
        return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
    }
    if (auto* ILE = dyn_cast<InitListExpr>(E)) {
        DEBUG_AST1(" InitListExpr");
        if (!ILE->getNumInits()) {
            return cpphdl::Expr{"0", cpphdl::Expr::EXPR_NUM};
        }
        auto expr = cpphdl::Expr{"init", cpphdl::Expr::EXPR_INIT};
        for (const Expr *Init : ILE->inits()) {
            expr.sub.emplace_back(exprToExpr(Init));
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

        if (UETTE->isArgumentType()) {
            auto QT = UETTE->getArgumentType().getNonReferenceType().getDesugaredType(ctx);
            if (!QT->getAs<RecordType>()) {  // cant know the type - will try to replace it later
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {cpphdl::Expr{QT.getAsString(),cpphdl::Expr::EXPR_TYPE}}};
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

        return cpphdl::Expr{"fold", cpphdl::Expr::EXPR_BODY, {expr}};
    }
    if(/*auto* IVIE = */dyn_cast<ImplicitValueInitExpr>(E)) {
        return cpphdl::Expr{"ImplicitValueInitExpr", cpphdl::Expr::EXPR_EMPTY};
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
    if (auto* SILE = dyn_cast<CXXStdInitializerListExpr>(E)) {
        DEBUG_AST1(" CXXStdInitializerListExpr");
    }
    if (auto* DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST1(" DesignatedInitExpr");
    }
    if (auto* DSDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST1(" DependentScopeDeclRefExpr");
    }
    if (auto* DDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST1(" DependentScopeDeclRefExpr");
    }
    if (auto* SOPE = dyn_cast<SizeOfPackExpr>(E)) {
        DEBUG_AST1(" SizeOfPackExpr");
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
    arg.print(ctx.getPrintingPolicy(), OS, true);
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

        auto* CRD = resolveCXXRecordDecl(QT);
        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
            auto st = exportStruct(CRD, *this);
            auto ret = mod.imports.emplace(st.name);
            if (ret.second) {
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
        if (str.length() > 2 && str_ending(str, "UL")) {
            str = str.replace(str.rfind("UL"), 2, "");
        }
        cpphdl::Expr expr1 = cpphdl::Expr{arg.getIntegralType()->isBooleanType() ? (arg.getAsIntegral().isZero() ? "false" : "true") : str, cpphdl::Expr::EXPR_NUM};
        expr.sub.emplace_back(std::move(expr1));
        DEBUG_AST1("(integral " << str << "),");
    } else
    if (arg.getKind() == TemplateArgument::Declaration) {
        DEBUG_AST1("(decl " << str << "),");
    } else {
        DEBUG_AST1("(unhandled " << str << "),");
    }
}

bool Helpers::templateToExpr(QualType QT, cpphdl::Expr& expr)
{
    std::string str;
    llvm::raw_string_ostream OS(str);
    QT.print(OS, ctx.getPrintingPolicy());
    OS.flush();
    DEBUG_AST1(/*debugIndent++, */" templateToExpr: " << str);//    on_return ret_debug([](){ --debugIndent; });

    auto* CRD = resolveCXXRecordDecl(QT);
    auto* TSD = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(CRD);
    auto* TST = QT->getAs<TemplateSpecializationType>();
    if (TSD || TST) {
        if (TSD) {
            DEBUG_AST1(" TSD");
        }

        expr.type = cpphdl::Expr::EXPR_TEMPLATE;
        expr.value = genTypeName(TSD ? TSD->getSpecializedTemplate()->getQualifiedNameAsString()
                         : TST->getTemplateName().getAsTemplateDecl()->getQualifiedNameAsString());

        for (const auto &arg : (TSD ? ArrayRef<TemplateArgument>(TSD->getTemplateArgs().asArray()) : TST->template_arguments())) {
            DEBUG_AST(debugIndent++, "# Arg "); on_return ret_debug([](){ --debugIndent; });
            ArgToExpr(arg, expr, TSD != nullptr);
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
    while (const clang::ArrayType* AT = ctx.getAsArrayType(QT)) {
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
        QT = QT.getDesugaredType(ctx);
//?        QT = QT.getCanonicalType();
        std::string str = QT.getAsString(ctx.getPrintingPolicy());
        DEBUG_AST1(str << " TYPE) ");
        expr.value = genTypeName(str);
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
                    ArgToExpr(arg, tmp);
                    for (auto& expr: tmp.sub) {
                        genSpecializationTypeName(first, name, expr, onlyTypes);
                        first = false;
                        if (params) {
                            params->emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), std::move(expr)});
                        }
                    }
                }
                continue;
            }
            DEBUG_AST(debugIndent++, "# Arg " << Params->getParam(i)->getNameAsString() << ": "); on_return ret_debug([](){ --debugIndent; });
            cpphdl::Expr tmp;
            ArgToExpr(args[i], tmp);
            for (auto& expr: tmp.sub) {
                genSpecializationTypeName(first, name, expr, onlyTypes);
                first = false;
                if (params) {
                    params->emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), std::move(expr)});
                }
            }
        }
    }
}

const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext &ctx)
{
    DynTypedNode Node = DynTypedNode::create(*DRE);

    while (true) {
        auto Parents = ctx.getParents(Node);

        if (Parents.empty())
            return nullptr;

        const DynTypedNode &P = Parents[0];

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

void Helpers::genSpecializationTypeName(bool first, std::string& name, cpphdl::Expr& param, bool onlyTypes)
{
    if (!onlyTypes || param.type != cpphdl::Expr::EXPR_NUM) {
        std::string str = param.str();
        if (!first) {
            name += "_";
        }
        name += genTypeName(str);
    }
    str_replace(name, "::", "_");
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
                    if (NS->getName() != "std") {
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
        }
    }
    return false;
}

CXXRecordDecl* Helpers::resolveCXXRecordDecl(QualType QT)
{
    auto* CRD = QT->getAsCXXRecordDecl();

    QT = QT.getNonReferenceType();
    QT = QT.getDesugaredType(ctx); // remove typedefs, aliases, etc.
//?    QT = QT.getCanonicalType();        // can use it only on specialized template - add checks later?

    if (const auto* RT = QT->getAs<RecordType>()) {
        if (cast<CXXRecordDecl>(RT->getDecl())) {
            CRD = cast<CXXRecordDecl>(RT->getDecl());
        }
    }
    return CRD;
}

CXXRecordDecl* Helpers::lookupQualifiedRecord(llvm::StringRef QualifiedName)
{
    SmallVector<StringRef, 4> Parts;
    QualifiedName.split(Parts, "::");

    DeclContext* DC = ctx.getTranslationUnitDecl();

    for (unsigned i = 0; i < Parts.size(); ++i) {
        IdentifierInfo& Id = ctx.Idents.get(Parts[i]);
        auto strs = DC->lookup(&Id);

        if (strs.empty()) {
            return nullptr;
        }

        NamedDecl* ND = strs.front();

        if (i < Parts.size()-1) {
            if (auto* NS = dyn_cast<NamespaceDecl>(ND)) {
                DC = NS;
            }
            else {
                return nullptr;
            }
        } else {
            if (auto* CXX = dyn_cast<CXXRecordDecl>(ND)) {
                return CXX;
            }
            return nullptr;
        }
    }
    return nullptr;
}

void Helpers::forEachBase(const CXXRecordDecl* RD, std::function<void(const CXXRecordDecl*RD)> func)
{
    func(RD);
    for (const CXXBaseSpecifier &Base : RD->bases()) {
        const CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
        if (BaseRD) {
            forEachBase(BaseRD, func);
        }
    }
}

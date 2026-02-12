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
std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp, bool notThis = false);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* ctx, llvm::StringRef QualifiedName);
//const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext* ctx);

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

        cpphdl::Expr body;
        bool wasBreak = true;
        for (const Stmt *S : dyn_cast<CompoundStmt>(SS->getBody())->body()) {
            if (const auto* CS = dyn_cast<CaseStmt>(S)) {
                if (!wasBreak) {
                    expr.sub.emplace_back(std::move(body));

                    const SourceManager &SM = ctx->getSourceManager();
                    PresumedLoc loc = SM.getPresumedLoc(SS->getSwitchLoc());
                    std::cerr << "WARNING: case is not terminated by break or return, " << loc.getFilename() << ":" << loc.getLine() << "\n";
                }

                body = cpphdl::Expr{exprToExpr(CS->getLHS()).str(), cpphdl::Expr::EXPR_BODY};  // the only one place we call str() in Clang part (to make a string value)
                if (CS->getRHS()) {
                    body.value = std::string("[") + exprToExpr(CS->getLHS()).str() + ":" + exprToExpr(CS->getRHS()).str() + "]";  // the only one place we call str() in Clang part (to make a string value)
                }
                body.sub.emplace_back(exprToExpr(CS->getSubStmt()));
                wasBreak = false;
            } else
            if (dyn_cast<DefaultStmt>(S)) {
                if (!wasBreak) {
                    expr.sub.emplace_back(std::move(body));

                    const SourceManager &SM = ctx->getSourceManager();
                    PresumedLoc loc = SM.getPresumedLoc(SS->getSwitchLoc());
                    std::cerr << "WARNING: case is not terminated by break or return, " << loc.getFilename() << ":" << loc.getLine() << "\n";
                }
                body = cpphdl::Expr{"default", cpphdl::Expr::EXPR_BODY};
                wasBreak = false;
            } else
            if (dyn_cast<BreakStmt>(S)) {
                expr.sub.emplace_back(std::move(body));
                wasBreak = true;
            } else
            if (dyn_cast<ReturnStmt>(S)) {
                body.sub.emplace_back(exprToExpr(S));
                expr.sub.emplace_back(std::move(body));
                wasBreak = true;
            }
            else {
                body.sub.emplace_back(exprToExpr(S));
            }
        }
        if (!wasBreak) {
            expr.sub.emplace_back(std::move(body));
        }

        return expr;
    }
    if (/*auto* CS =*/ dyn_cast<CaseStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};
    }
    if (/*auto* BS =*/ dyn_cast<BreakStmt>(E)) {
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};
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

                    QT = QT.getDesugaredType(*ctx);
                    auto* CRD = resolveCXXRecordDecl(QT);
                    if (CRD && (CRD->getQualifiedNameAsString().find("std::") == (size_t)0)) {
                        return cpphdl::Expr{"", cpphdl::Expr::EXPR_NONE};  // we dont want any std type to be translated to SV
                    }

                    expr.sub.emplace_back(digQT(QT));
                    if (VD->getInit()) {
                        expr.sub.emplace_back(exprToExpr(VD->getInit()));
                    }

                    CRD = resolveCXXRecordDecl(QT);
                    if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0
                        && CRD->getQualifiedNameAsString().find("IO_FILE") == (size_t)-1) {
                        auto st = exportStruct(CRD, *this);
                        auto ret = mod->imports.emplace(st.name);
                        if (ret.second) {
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
                typeSize = std::string("$bits(") + RD->getQualifiedNameAsString() + ")";
            }
            else {
                typeSize = std::string("$bits(") + LQT->getPointeeType().getNonReferenceType().getDesugaredType(*ctx).getAsString(ctx->getPrintingPolicy()) + ")";
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
                return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {expr}};
            } else {
                return cpphdl::Expr{std::string("*8 +: ") + typeSize,
                                   cpphdl::Expr::EXPR_INDEX, {expr, cpphdl::Expr{"0",cpphdl::Expr::EXPR_NUM}}};
            }
        }
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {expr}};
    }
    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST1(" BinaryOperator(" << BO->getOpcodeStr().data() << ")");
        QualType LQT = BO->getLHS()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (BO->getOpcodeStr() == "+" && LQT->isPointerType()) {  // convert pointer add into index
            return cpphdl::Expr{std::string("*8 +:") + std::to_string(ctx->getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
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

        const ValueDecl *VD = DRE->getDecl();
        const auto *Var = dyn_cast_or_null<VarDecl>(VD);
        if (Var && DRE->getDecl()->getType()->isReferenceType() && Var->hasInit()) {  // check if it's reference aka symlink to another var
            DEBUG_AST1(" REF");
            return exprToExpr(Var->getInit());
        }

        bool isPack = false;
        if (const ParmVarDecl* PVD = dyn_cast<ParmVarDecl>(Var)) {
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

                for (const EnumConstantDecl *ECD : ED->enumerators()) {
                    if (ECD->getInitExpr()) {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str(), {exprToExpr(ECD->getInitExpr())}});
                    }
                    else {
                        en.fields.emplace_back(cpphdl::Field{ECD->getName().str()});
                    }
                }

                name = en.name + "_pkg::" + name;

                auto ret = mod->imports.emplace(en.name);
                if (ret.second) {
                    currProject->enums.emplace_back(std::move(en));
                }
            }
        } else
        if (owner && Var && Var->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
            std::string sname = owner->getQualifiedNameAsString();
            str_replace(sname, "::", "_");
            // extracting parameters of the template
            followSpecialization(owner, sname);
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
        DEBUG_AST1(" IntegerLiteral(" << (IL->getType()->isUnsignedIntegerType() ? std::to_string(IL->getValue().getZExtValue()) : std::to_string(IL->getValue().getSExtValue())) << ")");
        return cpphdl::Expr{IL->getType()->isUnsignedIntegerType() ? std::to_string(IL->getValue().getZExtValue()) : std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_NUM};
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

        bool notThis = false;
        if (auto* ME = dyn_cast<MemberExpr>(MCE->getCallee())) {
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

            if ((flags&FLAG_EXTERNAL_THIS)) {  // already EXTERNAL
                DEBUG_AST1(" EXTERNAL");
                call.sub.push_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
            }
            else {
                call.sub.push_back(std::move(expr)/*cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase())}}*/);
            }
        }
        for (unsigned i = 0; i < MCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(MCE->getArg(i)));
        }

        if (auto *MD = MCE->getMethodDecl()) {
            if (call.sub.size()  // we need not call members - they are accessible through ports wires
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
        bool anon = false;
        const CXXRecordDecl *owner = dyn_cast<CXXRecordDecl>(ME->getMemberDecl()->getDeclContext());
        DEBUG_AST1(" MemberExpr(" << owner->getQualifiedNameAsString() << "::" << ME->getMemberDecl()->getNameAsString() << ")");

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
        if (/*const auto* FD =*/ dyn_cast<FieldDecl>(ME->getMemberDecl()) && owner->isAnonymousStructOrUnion()) {  // replacing anon with '_'
            DEBUG_AST1(" ANON");
            anon = true;
        } else
        if (Var && Var->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
            DEBUG_AST1(" PKG");
            std::string sname = owner->getQualifiedNameAsString();
            str_replace(sname, "::", "_");
            // extracting parameters of the template
            followSpecialization(owner, sname);
            name = sname + "_pkg::" + name;
            ignoreBase = true;
        } else
        if (mod->origName.find(owner->getQualifiedNameAsString()) != 0 && owner->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1
            && expr.type == cpphdl::Expr::EXPR_NONE && !str_ending(name, "_in") && !str_ending(name, "_out") ) {  // add base class name, ports dont get this prefix
            name = genTypeName(owner->getQualifiedNameAsString()) + "___" + name;
        }

        return cpphdl::Expr{name, cpphdl::Expr::EXPR_MEMBER, {expr},
                (anon?cpphdl::Expr::FLAG_ANON:0U) | ((flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_USETHIS:0U) | (ignoreBase?cpphdl::Expr::FLAG_NOBASE:0U)};
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

        return cpphdl::Expr{CDSME->getMemberNameInfo().getAsString(), cpphdl::Expr::EXPR_MEMBER, {expr}};
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
        DEBUG_AST1(" CallExpr(" << callee->getStmtClassName() << ")");

        //////////// this code is for std::tuple and should be refactored to separated source file //

        if (const auto* DRE = llvm::dyn_cast<clang::DeclRefExpr>(callee)) { // we do it only for std::apply
            const clang::FunctionDecl* function = llvm::dyn_cast<clang::FunctionDecl>(DRE->getDecl());
            DEBUG_AST1(" DRE(" << function->getQualifiedNameAsString() << ")");
            call.value = function->getNameAsString();

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
            const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(ME->getMemberDecl());
            DEBUG_AST1(" ME(" << (method ? method->getQualifiedNameAsString() : "null") << ")");
            call.value = method ? method->getNameAsString() : "null";

//            if (call.sub.size()  // we need not call members - they are accessible through ports wires
//                && std::find_if(mod->members.begin(), mod->members.end(), [&](auto& member){ return member.name == call.sub[0].value; }) == mod->members.end()) {
            if (method) {
                auto newName = putMethod(method, *this);
                DEBUG_AST1(" Called method( " << method->getQualifiedNameAsString() << " => " << newName << ")");
                if (newName.length()) {
                    call.value = newName;
                }
            }

            if (method && method->isLambdaStaticInvoker()) {
                const auto* RD = method->getParent();
                if (RD && RD->isLambda()) {
                    DEBUG_AST1(" CallExpr LE() not supported");
                }
            }
        }

        if (const auto* UME = llvm::dyn_cast<clang::UnresolvedMemberExpr>(callee)) {
            DEBUG_AST1(" UME(" << UME->getMemberName().getAsString() << ")");
            call.value = UME->getMemberName().getAsString();
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
            DEBUG_AST1(" DSME(" << DSME->getMemberNameInfo().getAsString() << ")");
            call.value = DSME->getMemberNameInfo().getAsString();
            call.type = cpphdl::Expr::EXPR_MEMBERCALL;
            auto member = exprToExpr(DSME->getBase());
//            member.sub.emplace_back(cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VAR});
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

        const CXXRecordDecl* owner = nullptr;
        if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(CE->getDirectCallee())) {
            const CXXMethodDecl *CanonicalMD = MD->getCanonicalDecl();
            owner = CanonicalMD->getParent();
        }

        if (owner && mod->origName.find(owner->getQualifiedNameAsString()) != 0 && owner->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1
            && !str_ending(call.value, "_in") && !str_ending(call.value, "_out") ) {  // add base class name, ports dont get this prefix
            call.value = genTypeName(owner->getQualifiedNameAsString()) + "___" + call.value;
        }

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
                if (str.find("std::basic_format_string") == 0) {
                    return cpphdl::Expr{str, cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getArg(0))}};  // we use it to determine std::print
                }
                return /*cpphdl::Expr{str, cpphdl::Expr::EXPR_CAST, {*/exprToExpr(CE->getArg(0))/*}}*/;
            }
            else {
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
        return cpphdl::Expr{CLE->getType().getAsString(), cpphdl::Expr::EXPR_INIT, {{exprToExpr(CLE->getInitializer())}}};
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
        QualType LQT = ASE->getBase()->IgnoreParenImpCasts()->getType().getNonReferenceType();
        if (LQT->isPointerType()) {  // convert pointer add into index
            return cpphdl::Expr{std::string("*8 +:") + std::to_string(ctx->getTypeSizeInChars(LQT->getPointeeType()).getQuantity()*8),
                                   cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase()),exprToExpr(ASE->getIdx())}};
        }
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase()),exprToExpr(ASE->getIdx())}};
    }
    if (auto* FCE = dyn_cast<ImplicitCastExpr>(E)) {
        DEBUG_AST1(" ImplicitCastExpr");
        return /*cpphdl::Expr{"implicit_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(FCE->getSubExpr())/*}}*/;
    }
    if (auto* FCE = dyn_cast<CXXFunctionalCastExpr>(E)) {
        DEBUG_AST1(" CXXFunctionalCastExpr(" << FCE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy()) << ")");
        return cpphdl::Expr{genTypeName(FCE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy())), cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr())}};
    }
    if (auto* SCE = dyn_cast<CXXStaticCastExpr>(E)) {
        DEBUG_AST1(" CXXStaticCastExpr");
        return /*cpphdl::Expr{"static_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(SCE->getSubExpr())/*}}*/;
    }
    if (auto* DCE = dyn_cast<CXXDynamicCastExpr>(E)) {
        DEBUG_AST1(" CXXDynamicCastExpr");
        return /*cpphdl::Expr{"dynamic_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(DCE->getSubExpr())/*}}*/;
    }
    if (auto* RCE = dyn_cast<CXXReinterpretCastExpr>(E)) {
        DEBUG_AST1(" CXXReinterpretCastExpr");
        return /*cpphdl::Expr{"reinterpret_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(RCE->getSubExpr())/*}}*/;
    }
    if (auto* CCE = dyn_cast<CXXConstCastExpr>(E)) {
        DEBUG_AST1(" CXXConstCastExpr");
        return /*cpphdl::Expr{"const_cast", cpphdl::Expr::EXPR_CAST, {*/exprToExpr(CCE->getSubExpr())/*}}*/;
    }
    if (auto* SCE = dyn_cast<CStyleCastExpr>(E)) {
        DEBUG_AST1(" CStyleCastExpr (" + SCE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy()) + ")");
        if (SCE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy()).find("__remove_reference_t") == 0) {
            return exprToExpr(SCE->getSubExpr());
        }
        return cpphdl::Expr{genTypeName(SCE->getType().getCanonicalType().getAsString(ctx->getPrintingPolicy())), cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr())}};
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
            auto QT = UETTE->getArgumentType().getNonReferenceType().getDesugaredType(*ctx);
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

        return cpphdl::Expr{"CXXFoldExpr", cpphdl::Expr::EXPR_BODY, {expr}};
    }
    if (/*auto* IVIE = */dyn_cast<ImplicitValueInitExpr>(E)) {
        return cpphdl::Expr{"ImplicitValueInitExpr", cpphdl::Expr::EXPR_NONE};
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

        auto* CRD = resolveCXXRecordDecl(QT);
        if (CRD && CRD->getQualifiedNameAsString().find("cpphdl::") != (size_t)0 && CRD->getQualifiedNameAsString().find("std::") != (size_t)0) {
            auto st = exportStruct(CRD, *this);
            auto ret = mod->imports.emplace(st.name);
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
    QT.print(OS, ctx->getPrintingPolicy());
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

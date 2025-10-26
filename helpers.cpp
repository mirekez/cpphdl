#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"

#include "Expr.h"
#include "Debug.h"

#include <iostream>

using namespace clang;

//inline bool getParamsFromSourceOrStr(FieldDecl* FD, std::string str, const ASTContext &Ctx, std::vector<std::string>& params);

cpphdl::Expr exprToExpr(const Stmt *E, ASTContext& Ctx)
{
    SourceManager &SM = Ctx.getSourceManager();
    LangOptions LangOpts = Ctx.getLangOpts();
    SourceLocation StartLoc = E->getBeginLoc();
    SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);
    CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);
    DEBUG_AST(std::cout << " exprToExpr(" << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "): ");

    if (auto *FS = dyn_cast<ForStmt>(E)) {
        DEBUG_AST(std::cout << " ForStmt ");

        cpphdl::Expr expr = cpphdl::Expr{"for", cpphdl::Expr::EXPR_FOR};

        if (FS->getInit()) {
            expr.sub.push_back(exprToExpr(FS->getInit(), Ctx));
        }
        if (FS->getCond()) {
            expr.sub.push_back(exprToExpr(FS->getCond(), Ctx));
        }
        if (FS->getInc()) {
            expr.sub.push_back(exprToExpr(FS->getInc(), Ctx));
        }

        if (FS->getBody()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
            if (auto *CS = dyn_cast<CompoundStmt>(FS->getBody())) {
                    for (auto *S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(FS->getBody(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }


        return expr;
    }

    if (auto *WS = dyn_cast<WhileStmt>(E)) {
        DEBUG_AST(std::cout << " WhileStmt ");

        cpphdl::Expr expr = cpphdl::Expr{"while", cpphdl::Expr::EXPR_WHILE};

        if (WS->getCond()) {
            expr.sub.push_back(exprToExpr(WS->getCond(), Ctx));
        }

        if (WS->getBody()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
            if (auto *CS = dyn_cast<CompoundStmt>(WS->getBody())) {
                for (auto *S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(WS->getBody(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        return expr;
    }

    if (auto *IS = dyn_cast<IfStmt>(E)) {
        DEBUG_AST(std::cout << " IfStmt ");

        cpphdl::Expr expr = cpphdl::Expr{"if", cpphdl::Expr::EXPR_IF};

        if (IS->getCond()) {
            expr.sub.push_back(exprToExpr(IS->getCond(), Ctx));
        }

        if (IS->getThen()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"then", cpphdl::Expr::EXPR_BODY};
            if (auto *CS = dyn_cast<CompoundStmt>(IS->getThen())) {
                for (auto *S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(IS->getThen(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        if (IS->getElse()) {
            cpphdl::Expr expr2 = cpphdl::Expr{"else", cpphdl::Expr::EXPR_BODY};
            if (auto *CS = dyn_cast<CompoundStmt>(IS->getElse())) {
                for (auto *S : CS->body()) {
                    expr2.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr2.sub.push_back(exprToExpr(IS->getElse(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr2));
        }

        return expr;
    }

//    E = E->IgnoreParenImpCasts();  //?

    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST(std::cout << "BinaryOperator, " << BO->getOpcodeStr().data());
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS(),Ctx),exprToExpr(BO->getRHS(),Ctx)}};
    }
    if (auto *CAO = dyn_cast<CompoundAssignOperator>(E)) {
        DEBUG_AST(std::cout << "CompoundAssignOperator, " << CAO->getOpcodeStr().data());
        return cpphdl::Expr{CAO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(CAO->getLHS(),Ctx),exprToExpr(CAO->getRHS(),Ctx)}};
    }
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST(std::cout << "DeclRefExpr, " << DRE->getNameInfo().getAsString());
        return cpphdl::Expr{DRE->getNameInfo().getAsString(), cpphdl::Expr::EXPR_DECLARE};
    }
    if (auto *IL = dyn_cast<IntegerLiteral>(E)) {
        DEBUG_AST(std::cout << "IntegerLiteral, " << std::to_string(IL->getValue().getSExtValue()));
        return cpphdl::Expr{std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
        DEBUG_AST(std::cout << "CXXOperatorCallExpr, ");
        cpphdl::Expr call = cpphdl::Expr{getOperatorSpelling(OCE->getOperator()), cpphdl::Expr::EXPR_CALL};
        for (unsigned i = 0; i < OCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(OCE->getArg(i), Ctx));
        }
        return call;
    }
    if (auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        DEBUG_AST(std::cout << "CXXMemberCallExpr, ");
        if (MCE->getNumArgs() == 0) {
            if (auto *ME = dyn_cast<MemberExpr>(MCE->getCallee())) {
                return cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBERCALL, {exprToExpr(ME->getBase(), Ctx)}};
            }
        }

        cpphdl::Expr call = cpphdl::Expr{(MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_CALL};
        for (unsigned i = 0; i < MCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(MCE->getArg(i), Ctx));
        }
        return call;
    }
    if (auto *ME = dyn_cast<MemberExpr>(E)) {
        DEBUG_AST(std::cout << "MemberExpr, ");
        return cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase(), Ctx)}};
    }
    if (auto *CE = dyn_cast<CallExpr>(E)) {
        DEBUG_AST(std::cout << "CallExpr, " << (CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():""));

        cpphdl::Expr call = cpphdl::Expr{(CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_CALL};
        for (auto *arg : CE->arguments()) {
            call.sub.push_back(exprToExpr(arg, Ctx));
        }
        return call;
    }
    if (auto *CL = dyn_cast<CharacterLiteral>(E)) {
        DEBUG_AST(std::cout << "CharacterLiteral, ");
        return cpphdl::Expr{std::to_string(CL->getValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto *CLE = dyn_cast<CompoundLiteralExpr>(E)) {
        DEBUG_AST(std::cout << "CompoundLiteralExpr, ");
        return cpphdl::Expr{CLE->getType().getAsString(), cpphdl::Expr::EXPR_INIT, {{exprToExpr(CLE->getInitializer(), Ctx)}}};
    }
    if (auto *SL = dyn_cast<StringLiteral>(E)) {
        DEBUG_AST(std::cout << "StringLiteral, ");
        return cpphdl::Expr{SL->getString().str(), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto *BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
        DEBUG_AST(std::cout << "CXXBoolLiteralExpr, ");
        return cpphdl::Expr{BLE->getValue()?"1":"0", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST(std::cout << "DeclRefExpr, ");
        return cpphdl::Expr{DRE->getDecl()->getNameAsString(), cpphdl::Expr::EXPR_VALUE};
    }
    if (/*auto *ME = */dyn_cast<CXXThisExpr>(E)) {
        DEBUG_AST(std::cout << "CXXThisExpr, ");
        return cpphdl::Expr{"this", cpphdl::Expr::EXPR_CONST};
    }
    if (auto *UO = dyn_cast<UnaryOperator>(E)) {
        DEBUG_AST(std::cout << "UnaryOperator, ");
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {exprToExpr(UO->getSubExpr(),Ctx)}};
    }
    if (auto *CO = dyn_cast<ConditionalOperator>(E)) {
        DEBUG_AST(std::cout << "ConditionalOperator, ");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_COND, {exprToExpr(CO->getCond(),Ctx),exprToExpr(CO->getTrueExpr(),Ctx),exprToExpr(CO->getFalseExpr(),Ctx)}};
    }
    if (auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        DEBUG_AST(std::cout << "ArraySubscriptExpr, ");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase(),Ctx),exprToExpr(ASE->getIdx(),Ctx)}};
    }
    if (auto *FCE = dyn_cast<ImplicitCastExpr>(E)) {
        DEBUG_AST(std::cout << "ImplicitCastExpr, ");
        return cpphdl::Expr{"implicit_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr(), Ctx)}};
    }
    if (auto *FCE = dyn_cast<CXXFunctionalCastExpr>(E)) {
        DEBUG_AST(std::cout << "CXXFunctionalCastExpr, ");
        return cpphdl::Expr{"functional_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr(), Ctx)}};
    }
    if (auto *SCE = dyn_cast<CXXStaticCastExpr>(E)) {
        DEBUG_AST(std::cout << "CXXStaticCastExpr, ");
        return cpphdl::Expr{"static_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr(), Ctx)}};
    }
    if (auto *DCE = dyn_cast<CXXDynamicCastExpr>(E)) {
        DEBUG_AST(std::cout << "CXXDynamicCastExpr, ");
        return cpphdl::Expr{"dynamic_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(DCE->getSubExpr(), Ctx)}};
    }
    if (auto *RCE = dyn_cast<CXXReinterpretCastExpr>(E)) {
        DEBUG_AST(std::cout << "CXXReinterpretCastExpr, ");
        return cpphdl::Expr{"reinterpret_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(RCE->getSubExpr(), Ctx)}};
    }
    if (auto *CCE = dyn_cast<CXXConstCastExpr>(E)) {
        DEBUG_AST(std::cout << "CXXConstCastExpr, ");
        return cpphdl::Expr{"const_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(CCE->getSubExpr(), Ctx)}};
    }
    if (auto *SCE = dyn_cast<CStyleCastExpr>(E)) {
        DEBUG_AST(std::cout << "CStyleCastExpr, ");
        return cpphdl::Expr{"cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr(), Ctx)}};
    }
    if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
        DEBUG_AST(std::cout << "MaterializeTemporaryExpr, ");
        return cpphdl::Expr{"MaterializeTemporaryExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(MTE->getSubExpr(), Ctx)}};
    }
    if (auto *EWC = dyn_cast<ExprWithCleanups>(E)) {
        DEBUG_AST(std::cout << "ExprWithCleanups");
        return cpphdl::Expr{"ExprWithCleanups", cpphdl::Expr::EXPR_CAST, {exprToExpr(EWC->getSubExpr(), Ctx)}};
    }
    if (auto *CE = dyn_cast<ConstantExpr>(E)) {
        DEBUG_AST(std::cout << "ConstantExpr");
        return cpphdl::Expr{"ConstantExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getSubExpr(), Ctx)}};
    }
    if (/*auto *CE = */dyn_cast<CXXNullPtrLiteralExpr>(E)) {
        DEBUG_AST(std::cout << "CXXNullPtrLiteralExpr");
        return cpphdl::Expr{"nullptr", cpphdl::Expr::EXPR_CONST};
    }
    if (auto *UETTE = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        DEBUG_AST(std::cout << "UnaryExprOrTypeTraitExpr");

        std::string op;
        switch (UETTE->getKind()) {
            case UETT_SizeOf:   op = "sizeof"; break;
            case UETT_AlignOf:  op = "alignof"; break;
            case UETT_VecStep:  op = "vecstep"; break;
            case UETT_PreferredAlignOf: op = "preferred_alignof"; break;
            case UETT_OpenMPRequiredSimdAlign:
                op = "omp required simd align"; break;
            default: op = "unknown_trait"; break;
        }

        if (UETTE->isArgumentType()) {
            return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {cpphdl::Expr{UETTE->getArgumentType().getAsString(),cpphdl::Expr::EXPR_TYPE}}};
        } else {
            if (UETTE->getArgumentExpr()) {
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {exprToExpr(UETTE->getArgumentExpr(), Ctx)}};
            }
        }
    }
    if (auto *PE = dyn_cast<ParenExpr>(E)) {
        DEBUG_AST(std::cout << "ParenExpr");
        return cpphdl::Expr{"paren", cpphdl::Expr::EXPR_PAREN, {exprToExpr(PE->getSubExpr(), Ctx)}};
    }
    if (auto *SNTTPE = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
        DEBUG_AST(std::cout << "SubstNonTypeTemplateParmExpr");
        return cpphdl::Expr{SNTTPE->getParameter()->getName().str(), cpphdl::Expr::EXPR_PARAM, {exprToExpr(SNTTPE->getReplacement(), Ctx)}};
    }
    if (auto *RS = dyn_cast<ReturnStmt>(E)) {
        DEBUG_AST(std::cout << "ReturnStmt");
        if (RS->getRetValue()) {
            return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN, {exprToExpr(RS->getRetValue(), Ctx)}};
        }
        return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN};
    }
/*
    if (auto *FL = dyn_cast<FloatingLiteral>(E)) {
        DEBUG_AST(std::cout << "FloatingLiteral");
//        return cpphdl::Expr{std::to_string(FL->getValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
        DEBUG_AST(std::cout << "UnresolvedLookupExpr");
    }
    if (auto *DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST(std::cout << "DesignatedInitExpr");
    }
    if (auto *TOE = dyn_cast<CXXTemporaryObjectExpr>(E)) {
        DEBUG_AST(std::cout << "CXXTemporaryObjectExpr");
    }
    if (auto *CE = dyn_cast<CXXConstructExpr>(E)) {
        DEBUG_AST(std::cout << "CXXConstructExpr");
    }
    if (auto *BCO = dyn_cast<BinaryConditionalOperator>(E)) {
        DEBUG_AST(std::cout << "BinaryConditionalOperator");
    }
    if (auto *ILE = dyn_cast<InitListExpr>(E)) {
        DEBUG_AST(std::cout << "InitListExpr");
    }
    if (auto *SILE = dyn_cast<CXXStdInitializerListExpr>(E)) {
        DEBUG_AST(std::cout << "CXXStdInitializerListExpr");
    }
    if (auto *DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST(std::cout << "DesignatedInitExpr");
    }
    if (auto *DSDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST(std::cout << "DependentScopeDeclRefExpr");
    }
    if (auto *DSME = dyn_cast<CXXDependentScopeMemberExpr>(E)) {
        DEBUG_AST(std::cout << "CXXDependentScopeMemberExpr");
    }
    if (auto *DDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST(std::cout << "DependentScopeDeclRefExpr");
    }
    if (auto *SOPE = dyn_cast<SizeOfPackExpr>(E)) {
        DEBUG_AST(std::cout << "SizeOfPackExpr");
    }
    if (auto *OOE = dyn_cast<OffsetOfExpr>(E)) {
        DEBUG_AST(std::cout << "OffsetOfExpr");
    }
    if (auto *BTE = dyn_cast<CXXBindTemporaryExpr>(E)) {
        DEBUG_AST(std::cout << "CXXBindTemporaryExpr");
    }
    if (auto *FE = dyn_cast<FullExpr>(E)) {
        DEBUG_AST(std::cout << "FullExpr");
    }
*/
    else {
        DEBUG_AST(std::cout << "unknown: " << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "(" << E->getStmtClassName() << ")");

        return cpphdl::Expr{std::string(Lexer::getSourceText(Range, SM, LangOpts)) + "(" + E->getStmtClassName() + ")", cpphdl::Expr::EXPR_UNKNOWN};
    }
    ASSERT(0);
    return cpphdl::Expr{"", cpphdl::Expr::EXPR_UNKNOWN};
}

bool templateToExpr(QualType QT, cpphdl::Expr& expr, ASTContext& Ctx)
{
//    if (const auto *ET = llvm::dyn_cast<ElaboratedType>(QT)) {
//        QT = ET->getNamedType();
//    }
    cpphdl::Expr expr1;
    if (const auto* TST = QT->getAs<TemplateSpecializationType>()) {

        expr.type = cpphdl::Expr::EXPR_TEMPLATE;
        expr.value = TST->getTemplateName().getAsTemplateDecl()->getQualifiedNameAsString();

        for (const auto &Arg : TST->template_arguments()) {
            std::string str;
            llvm::raw_string_ostream OS(str);
            Arg.print(Ctx.getPrintingPolicy(), OS, true);
            OS.flush();

            if (Arg.getKind() == TemplateArgument::Expression) {
                expr.sub.push_back(exprToExpr(Arg.getAsExpr(), Ctx));
                DEBUG_AST(std::cout << "(expression),");
            } else
            if (Arg.getKind() == TemplateArgument::Type || Arg.getKind() == TemplateArgument::Template) {
                QualType QT = Arg.getAsType().getNonReferenceType();
                if (templateToExpr(QT, expr1, Ctx)) {
                    DEBUG_AST(std::cout << "(template) " << expr1.value);
                }
                else {
                    QT = QT.getCanonicalType();
                    QT = QT.getDesugaredType(Ctx);
                    str = QT.getAsString(Ctx.getPrintingPolicy());
                    DEBUG_AST(std::cout << "(type) " << str << ",");
                    expr1.value = str;
                    expr1.type = cpphdl::Expr::EXPR_TYPE;
                }
                expr.sub.emplace_back(std::move(expr1));
            } else
            if (Arg.getKind() == TemplateArgument::Integral) {
/*                if (params && params->size() > i) {
                    DEBUG_AST(std::cout << "(" << (*params)[i] << ")");
                    expr1.value = (*params)[i];
                    expr1.sub.push_back(cpphdl::Expr{str, cpphdl::Expr::EXPR_VALUE});
                }
                else {
                }*/
                expr1.value = str;
                expr1.type = cpphdl::Expr::EXPR_VALUE;
                expr.sub.emplace_back(std::move(expr1));
                DEBUG_AST(std::cout << "(integral) " << str << ",");
            } else
            if (Arg.getKind() == TemplateArgument::Declaration) {
                expr1.value = str;
                DEBUG_AST(std::cout << "(decl) " << str << ",");
            } else {
                expr1.value = str;
                DEBUG_AST(std::cout << "(unhandled) " << str << ",");
            }
        }
        return true;
    }
    return false;
}

/*inline bool getParamsFromSourceOrStr(FieldDecl* FD, std::string str, const ASTContext &Ctx, std::vector<std::string>& params)
{
    if (FD) {
        clang::SourceRange SR = FD->getSourceRange();
        str = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(SR),
            Ctx.getSourceManager(),
            Ctx.getLangOpts()).str();
    }

    auto lt = str.find('<');
    if (lt == (size_t)-1) {
        return false;
    }
    str = str.substr(lt+1);
    unsigned openCnt = 0;
    bool quiot1 = false;
    bool quiot2 = false;
    bool escape = false;

    size_t pos = 0;
    while ((pos = str.find_first_of("<>\"'\\,", pos)) != (size_t)-1) {
        escape = false;
        if (str[pos] == ',') {
            if (quiot1 || quiot2) {
            } else
            if (openCnt == 0) {
                if (pos > 0) {
                    params.push_back(str.substr(0, pos));
                }
                str = str.substr(pos+1);
                pos = 0;
            }
        } else
        if (str[pos] == '>') {
            if (quiot1 || quiot2) {
            }
            else {
                if (openCnt == 0) {
                    if (pos > 0) {
                        params.push_back(str.substr(0, pos));
                    }
                    return true;
                }
                --openCnt;
            }
        } else
        if (str[pos] == '<') {
            if (!quiot1 && !quiot2) {
                ++openCnt;
            }
        } else
        if (str[pos] == '\'') {
            if (quiot2 || escape) {
            } else
            if (quiot1) {
                quiot1 = false;
            }
            else {
                quiot1 = true;
            }
        } else
        if (str[pos] == '"') {
            if (quiot1 || escape) {
            } else
            if (quiot2) {
                quiot2 = false;
            }
            else {
                quiot2 = true;
            }
        } else
        if (str[pos] == '\\') {
            escape = true;
        }
        ++pos;
    }
    return false;
}
*/
CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName)
{
    SmallVector<StringRef, 4> Parts;
    QualifiedName.split(Parts, "::");

    DeclContext* DC = Ctx->getTranslationUnitDecl();

    for (unsigned i = 0; i < Parts.size(); ++i) {
        IdentifierInfo& Id = Ctx->Idents.get(Parts[i]);
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

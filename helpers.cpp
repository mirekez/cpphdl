#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"

#include "Project.h"
#include "Module.h"
#include "Debug.h"
#include "Expr.h"
#include "Struct.h"
#include "Field.h"

#include <iostream>

using namespace clang;

cpphdl::Struct exportStruct(cpphdl::Module& mod, CXXRecordDecl* RD, ASTContext& Ctx);
cpphdl::Expr digQT(cpphdl::Module& mod, QualType& QT, ASTContext& Ctx);

//inline bool getParamsFromSourceOrStr(FieldDecl* FD, std::string str, const ASTContext &Ctx, std::vector<std::string>& params);

cpphdl::Expr exprToExpr(const Stmt* E, ASTContext& Ctx)
{
    SourceManager &SM = Ctx.getSourceManager();
    LangOptions LangOpts = Ctx.getLangOpts();
    SourceLocation StartLoc = E->getBeginLoc();
    SourceLocation EndLoc   = Lexer::getLocForEndOfToken(E->getEndLoc(), 0, SM, LangOpts);
    if (StartLoc.isMacroID()) {
        StartLoc = SM.getSpellingLoc(StartLoc);
        EndLoc   = SM.getSpellingLoc(EndLoc);
    }
    CharSourceRange Range = CharSourceRange::getCharRange(StartLoc, EndLoc);
    DEBUG_AST(std::cout << " exprToExpr(" << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "): ");

    if (auto* FS = dyn_cast<ForStmt>(E)) {
        DEBUG_AST(std::cout << " ForStmt");

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
            if (auto* CS = dyn_cast<CompoundStmt>(FS->getBody())) {
                    for (auto* S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(FS->getBody(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        return expr;
    }

    if (auto* WS = dyn_cast<WhileStmt>(E)) {
        DEBUG_AST(std::cout << " WhileStmt");

        cpphdl::Expr expr = cpphdl::Expr{"while", cpphdl::Expr::EXPR_WHILE};

        if (WS->getCond()) {
            expr.sub.push_back(exprToExpr(WS->getCond(), Ctx));
        }

        if (WS->getBody()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"body", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(WS->getBody())) {
                for (auto* S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(WS->getBody(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        return expr;
    }

    if (auto* IS = dyn_cast<IfStmt>(E)) {
        DEBUG_AST(std::cout << " IfStmt");

        cpphdl::Expr expr = cpphdl::Expr{"if", cpphdl::Expr::EXPR_IF};

        if (IS->getCond()) {
            expr.sub.push_back(exprToExpr(IS->getCond(), Ctx));
        }

        if (IS->getThen()) {
            cpphdl::Expr expr1 = cpphdl::Expr{"then", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(IS->getThen())) {
                for (auto* S : CS->body()) {
                    expr1.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr1.sub.push_back(exprToExpr(IS->getThen(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr1));
        }

        if (IS->getElse()) {
            cpphdl::Expr expr2 = cpphdl::Expr{"else", cpphdl::Expr::EXPR_BODY};
            if (auto* CS = dyn_cast<CompoundStmt>(IS->getElse())) {
                for (auto* S : CS->body()) {
                    expr2.sub.push_back(exprToExpr(S, Ctx));
                }
            } else {
                expr2.sub.push_back(exprToExpr(IS->getElse(), Ctx));
            }
            expr.sub.emplace_back(std::move(expr2));
        }

        return expr;
    }

    if (auto* CS = dyn_cast<CompoundStmt>(E)) {
        DEBUG_AST(std::cout << " CompoundStmt");

        cpphdl::Expr expr = cpphdl::Expr{"compound", cpphdl::Expr::EXPR_BODY};
        for (auto* S : CS->body()) {
            expr.sub.push_back(exprToExpr(S, Ctx));
        }
        return expr;
    }

    if (dyn_cast<NullStmt>(E)) {
        DEBUG_AST(std::cout << " NullStmt");

        cpphdl::Expr expr = cpphdl::Expr{"", cpphdl::Expr::EXPR_VALUE};
        return expr;
    }

//    E = E->IgnoreParenImpCasts();  //?

    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST(std::cout << " BinaryOperator " << BO->getOpcodeStr().data() << ",");
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS(),Ctx),exprToExpr(BO->getRHS(),Ctx)}};
    }
    if (auto* CAO = dyn_cast<CompoundAssignOperator>(E)) {
        DEBUG_AST(std::cout << " CompoundAssignOperator " << CAO->getOpcodeStr().data() << ",");
        return cpphdl::Expr{CAO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(CAO->getLHS(),Ctx),exprToExpr(CAO->getRHS(),Ctx)}};
    }
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST(std::cout << " DeclRefExpr " << DRE->getNameInfo().getAsString() << ",");
        return cpphdl::Expr{DRE->getNameInfo().getAsString(), cpphdl::Expr::EXPR_VAR};
    }
    if (auto* IL = dyn_cast<IntegerLiteral>(E)) {
        DEBUG_AST(std::cout << " IntegerLiteral " << std::to_string(IL->getValue().getSExtValue()) << ",");
        return cpphdl::Expr{std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
        DEBUG_AST(std::cout << " CXXOperatorCallExpr");
        cpphdl::Expr call = cpphdl::Expr{getOperatorSpelling(OCE->getOperator()), cpphdl::Expr::EXPR_OPERATORCALL};
        for (unsigned i = 0; i < OCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(OCE->getArg(i), Ctx));
        }
        return call;
    }
    if (auto* MCE = dyn_cast<CXXMemberCallExpr>(E)) {
        DEBUG_AST(std::cout << " CXXMemberCallExpr");
        cpphdl::Expr call = cpphdl::Expr{(MCE->getDirectCallee()?MCE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_MEMBERCALL};
        if (auto* ME = dyn_cast<MemberExpr>(MCE->getCallee())) {
             call.sub.push_back(cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase(), Ctx)}});
        }
        for (unsigned i = 0; i < MCE->getNumArgs(); ++i) {
            call.sub.push_back(exprToExpr(MCE->getArg(i), Ctx));
        }
        return call;
    }
    if (auto* ME = dyn_cast<MemberExpr>(E)) {
        DEBUG_AST(std::cout << " MemberExpr");

        bool anon = false;
        const FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (FD) {
            const RecordDecl *Parent = FD->getParent();
            if (Parent && Parent->isAnonymousStructOrUnion()) {
                anon = true;
                DEBUG_AST(std::cout << " ANON");
            }
        }

        return cpphdl::Expr{ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase(), Ctx)}, anon};
    }
    if (auto* ME = dyn_cast<CXXDependentScopeMemberExpr>(E)) {
        DEBUG_AST(std::cout << " MemberExpr");
        return cpphdl::Expr{ME->getMember().getAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase(), Ctx)}};
    }
    if (auto* CE = dyn_cast<CallExpr>(E)) {
        DEBUG_AST(std::cout << " CallExpr" << (CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():"") << ",");

        cpphdl::Expr call = cpphdl::Expr{(CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_CALL};
        for (auto* arg : CE->arguments()) {
            call.sub.push_back(exprToExpr(arg, Ctx));
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

            DEBUG_AST(std::cout << " CXXConstructExpr: " << str << ",");

//            cpphdl::Expr call = cpphdl::Expr{str, cpphdl::Expr::EXPR_CALL};
//            for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
//                call.sub.push_back(exprToExpr(CE->getArg(i), Ctx));
//            }
//            return call;
            if (CE->getNumArgs()) {
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getArg(0), Ctx)}};
            }
            else {
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST};
            }
        }
    }
    if (auto* CL = dyn_cast<CharacterLiteral>(E)) {
        DEBUG_AST(std::cout << " CharacterLiteral");
        return cpphdl::Expr{std::to_string(CL->getValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* CLE = dyn_cast<CompoundLiteralExpr>(E)) {
        DEBUG_AST(std::cout << " CompoundLiteralExpr");
        return cpphdl::Expr{CLE->getType().getAsString(), cpphdl::Expr::EXPR_INIT, {{exprToExpr(CLE->getInitializer(), Ctx)}}};
    }
    if (auto* SL = dyn_cast<StringLiteral>(E)) {
        DEBUG_AST(std::cout << " StringLiteral");

        clang::SourceRange Range = SL->getSourceRange();
        clang::LangOptions LO = Ctx.getLangOpts();

        llvm::StringRef RawText = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(Range),
            Ctx.getSourceManager(),
            LO);

        return cpphdl::Expr{RawText.str(), cpphdl::Expr::EXPR_STRING};
    }
    if (auto* BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
        DEBUG_AST(std::cout << " CXXBoolLiteralExpr");
        return cpphdl::Expr{BLE->getValue()?"1":"0", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST(std::cout << " DeclRefExpr");
        return cpphdl::Expr{DRE->getDecl()->getNameAsString(), cpphdl::Expr::EXPR_VALUE};
    }
    if (/*auto* ME =*/dyn_cast<CXXThisExpr>(E)) {
        DEBUG_AST(std::cout << " CXXThisExpr");
        return cpphdl::Expr{"this", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* UO = dyn_cast<UnaryOperator>(E)) {
        DEBUG_AST(std::cout << " UnaryOperator");
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {exprToExpr(UO->getSubExpr(),Ctx)}};
    }
    if (auto* CO = dyn_cast<ConditionalOperator>(E)) {
        DEBUG_AST(std::cout << " ConditionalOperator");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_COND, {exprToExpr(CO->getCond(),Ctx),exprToExpr(CO->getTrueExpr(),Ctx),exprToExpr(CO->getFalseExpr(),Ctx)}};
    }
    if (auto* ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        DEBUG_AST(std::cout << " ArraySubscriptExpr");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_INDEX, {exprToExpr(ASE->getBase(),Ctx),exprToExpr(ASE->getIdx(),Ctx)}};
    }
    if (auto* FCE = dyn_cast<ImplicitCastExpr>(E)) {
        DEBUG_AST(std::cout << " ImplicitCastExpr");
        return cpphdl::Expr{"implicit_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr(), Ctx)}};
    }
    if (auto* FCE = dyn_cast<CXXFunctionalCastExpr>(E)) {
        DEBUG_AST(std::cout << " CXXFunctionalCastExpr");
        return cpphdl::Expr{"functional_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(FCE->getSubExpr(), Ctx)}};
    }
    if (auto* SCE = dyn_cast<CXXStaticCastExpr>(E)) {
        DEBUG_AST(std::cout << " CXXStaticCastExpr");
        return cpphdl::Expr{"static_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr(), Ctx)}};
    }
    if (auto* DCE = dyn_cast<CXXDynamicCastExpr>(E)) {
        DEBUG_AST(std::cout << " CXXDynamicCastExpr");
        return cpphdl::Expr{"dynamic_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(DCE->getSubExpr(), Ctx)}};
    }
    if (auto* RCE = dyn_cast<CXXReinterpretCastExpr>(E)) {
        DEBUG_AST(std::cout << " CXXReinterpretCastExpr");
        return cpphdl::Expr{"reinterpret_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(RCE->getSubExpr(), Ctx)}};
    }
    if (auto* CCE = dyn_cast<CXXConstCastExpr>(E)) {
        DEBUG_AST(std::cout << " CXXConstCastExpr");
        return cpphdl::Expr{"const_cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(CCE->getSubExpr(), Ctx)}};
    }
    if (auto* SCE = dyn_cast<CStyleCastExpr>(E)) {
        DEBUG_AST(std::cout << " CStyleCastExpr");
        return cpphdl::Expr{"cast", cpphdl::Expr::EXPR_CAST, {exprToExpr(SCE->getSubExpr(), Ctx)}};
    }
    if (auto* MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
        DEBUG_AST(std::cout << " MaterializeTemporaryExpr");
        return cpphdl::Expr{"MaterializeTemporaryExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(MTE->getSubExpr(), Ctx)}};
    }
    if (auto* EWC = dyn_cast<ExprWithCleanups>(E)) {
        DEBUG_AST(std::cout << " ExprWithCleanups");
        return cpphdl::Expr{"ExprWithCleanups", cpphdl::Expr::EXPR_CAST, {exprToExpr(EWC->getSubExpr(), Ctx)}};
    }
    if (auto* CE = dyn_cast<ConstantExpr>(E)) {
        DEBUG_AST(std::cout << " ConstantExpr");
        return cpphdl::Expr{"ConstantExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getSubExpr(), Ctx)}};
    }
    if (auto* BTE = dyn_cast<CXXBindTemporaryExpr>(E)) {
        DEBUG_AST(std::cout << " CXXBindTemporaryExpr");
        return cpphdl::Expr{"CXXBindTemporaryExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(BTE->getSubExpr(), Ctx)}};
    }
    if (/*auto* CE = */dyn_cast<CXXNullPtrLiteralExpr>(E)) {
        DEBUG_AST(std::cout << " CXXNullPtrLiteralExpr");
        return cpphdl::Expr{"nullptr", cpphdl::Expr::EXPR_VALUE};
    }
    if (/*auto* CE = */dyn_cast<InitListExpr>(E)) {
        DEBUG_AST(std::cout << " InitListExpr");
        return cpphdl::Expr{"0", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* UETTE = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        DEBUG_AST(std::cout << " UnaryExprOrTypeTraitExpr");

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
    if (auto* PE = dyn_cast<ParenExpr>(E)) {
        DEBUG_AST(std::cout << " ParenExpr");
        return cpphdl::Expr{"paren", cpphdl::Expr::EXPR_PAREN, {exprToExpr(PE->getSubExpr(), Ctx)}};
    }
    if (auto* SNTTPE = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
        DEBUG_AST(std::cout << " SubstNonTypeTemplateParmExpr");
        return cpphdl::Expr{SNTTPE->getParameter()->getName().str(), cpphdl::Expr::EXPR_PARAM, {exprToExpr(SNTTPE->getReplacement(), Ctx)}};
    }
    if (auto* RS = dyn_cast<ReturnStmt>(E)) {
        DEBUG_AST(std::cout << " ReturnStmt");
        if (RS->getRetValue()) {
            return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN, {exprToExpr(RS->getRetValue(), Ctx)}};
        }
        return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN};
    }
/*
    if (auto* FL = dyn_cast<FloatingLiteral>(E)) {
        DEBUG_AST(std::cout << " FloatingLiteral");
//        return cpphdl::Expr{std::to_string(FL->getValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
        DEBUG_AST(std::cout << " UnresolvedLookupExpr");
    }
    if (auto* DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST(std::cout << " DesignatedInitExpr");
    }
    if (auto* TOE = dyn_cast<CXXTemporaryObjectExpr>(E)) {
        DEBUG_AST(std::cout << " CXXTemporaryObjectExpr");
    }
    if (auto* BCO = dyn_cast<BinaryConditionalOperator>(E)) {
        DEBUG_AST(std::cout << " BinaryConditionalOperator");
    }
    if (auto* SILE = dyn_cast<CXXStdInitializerListExpr>(E)) {
        DEBUG_AST(std::cout << " CXXStdInitializerListExpr");
    }
    if (auto* DIE = dyn_cast<DesignatedInitExpr>(E)) {
        DEBUG_AST(std::cout << " DesignatedInitExpr");
    }
    if (auto* DSDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST(std::cout << " DependentScopeDeclRefExpr");
    }
    if (auto* DDRE = dyn_cast<DependentScopeDeclRefExpr>(E)) {
        DEBUG_AST(std::cout << " DependentScopeDeclRefExpr");
    }
    if (auto* SOPE = dyn_cast<SizeOfPackExpr>(E)) {
        DEBUG_AST(std::cout << " SizeOfPackExpr");
    }
    if (auto* OOE = dyn_cast<OffsetOfExpr>(E)) {
        DEBUG_AST(std::cout << " OffsetOfExpr");
    }
    if (auto* FE = dyn_cast<FullExpr>(E)) {
        DEBUG_AST(std::cout << " FullExpr");
    }
*/
    else {
        DEBUG_AST(std::cout << " unknown: " << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "(" << E->getStmtClassName() << ")");

        return cpphdl::Expr{std::string(Lexer::getSourceText(Range, SM, LangOpts)) + "(" + E->getStmtClassName() + ")", cpphdl::Expr::EXPR_UNKNOWN};
    }
    ASSERT(0);
    return cpphdl::Expr{"", cpphdl::Expr::EXPR_UNKNOWN};
}

bool specializationToParameters(cpphdl::Module& mod, CXXRecordDecl* RD, std::vector<cpphdl::Field>& params, ASTContext& Ctx)
{
    if (auto* TSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        DEBUG_AST(std::cout << " Specialization parameters: \n");

        const TemplateArgumentList& Args = TSD->getTemplateArgs();
        const TemplateParameterList* Params = TSD->getSpecializedTemplate()->getTemplateParameters();
        for (unsigned i = 0; i < Args.size(); ++i) {
            const TemplateArgument& Arg = Args[i];
            switch (Arg.getKind()) {
                case TemplateArgument::Type:
                {
                    QualType QT = Arg.getAsType();

                    bool pointer = false;
                    QT.getNonReferenceType();
                    if (QT->isPointerType()) {  // while?
                        QT = QT->getPointeeType();
                        pointer = true;
                        DEBUG_AST(std::cout << " *pointer*");
                    }

//                QT = QT.getNonReferenceType();
//                QT = QT.getDesugaredType(Ctx); // remove typedefs, aliases, etc.
//                QT = QT.getCanonicalType();        // ensure you have the actual canonical form
//digQT(mod, QT, Ctx);

                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    QT.print(OS, Ctx.getPrintingPolicy());
                    OS.flush();
                    cpphdl::Expr expr = cpphdl::Expr{str, cpphdl::Expr::EXPR_TYPE};
                    DEBUG_AST(std::cout << " type: " << OS.str() << "\n");
                    params.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), expr});
                    DEBUG_AST(std::cout << "\n");
                    DEBUG_EXPR(std::cout << "        Expr: " << params.back().type.debug() << "\n");
                    if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                        auto st = exportStruct(mod, QT->getAsCXXRecordDecl(), Ctx);
                        auto ret = mod.imports.emplace(st.name);
                        if (ret.second) {
                            currProject->structs.emplace_back(std::move(st));
                        }
                    }
                    break;
                }
                case TemplateArgument::Integral:
                {
                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    Arg.getAsIntegral().print(OS, true);
                    OS.flush();
                    DEBUG_AST(std::cout << " integral: " << str);
                    if (str.length() > 2 && str.find("UL") == str.length()-2) {
                        str = str.replace(str.find("UL"), 2, "");
                    }
                    params.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(),
                        cpphdl::Expr{Arg.getIntegralType()->isBooleanType() ? (Arg.getAsIntegral().isZero() ? "false" : "true") : str, cpphdl::Expr::EXPR_VALUE}});
                    DEBUG_AST(std::cout << "\n");
                    DEBUG_EXPR(std::cout << "        Expr: " << params.back().type.debug() << "\n");
                    break;
                }
                case TemplateArgument::Declaration:
                    DEBUG_AST(std::cout << " declaration: " << Arg.getAsDecl()->getNameAsString() << "\n");
                    break;
                case TemplateArgument::Template:
                {
                    TemplateName TN = Arg.getAsTemplate();
                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    TN.print(OS, Ctx.getPrintingPolicy());
                    OS.flush();
                    DEBUG_AST(std::cout << " template: " << OS.str() << "\n");
                    break;
                }
                case TemplateArgument::Expression:
                {
                    const Expr* E = Arg.getAsExpr();
                    SourceManager &SM = Ctx.getSourceManager();
                    LangOptions LO = Ctx.getLangOpts();
                    SourceRange Range = E->getSourceRange();
                    if (!Range.isInvalid()) {
                        [[maybe_unused]] llvm::StringRef SR = Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, LO);
                        DEBUG_AST(std::cout << " expression: " << SR.str());
                        DEBUG_AST(std::cout << "\n");
                        DEBUG_EXPR(std::cout << "        Expr: " << exprToExpr(E,Ctx).debug() << "\n");
                    }
                    else {
                        DEBUG_AST(std::cout << "\n");
                    }
                    break;
                }
                case TemplateArgument::Pack:
                default:
                    DEBUG_AST(std::cout << " unhandled\n");
                break;
            }
        }
        return true;
    }
    return false;
}

bool templateToExpr(cpphdl::Module& mod, QualType QT, cpphdl::Expr& expr, ASTContext& Ctx)
{
    auto* TSD = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(QT->getAsCXXRecordDecl());
    auto* TST = QT->getAs<TemplateSpecializationType>();
    if (TSD || TST) {

        expr.type = cpphdl::Expr::EXPR_TEMPLATE;
        expr.value = TSD ? TSD->getSpecializedTemplate()->getQualifiedNameAsString()
                         : TST->getTemplateName().getAsTemplateDecl()->getQualifiedNameAsString();

        size_t i = 0;
        for (const auto &Arg : (TSD ? ArrayRef<TemplateArgument>(TSD->getTemplateArgs().asArray()) : TST->template_arguments())) {
            std::string str;
            llvm::raw_string_ostream OS(str);
            Arg.print(Ctx.getPrintingPolicy(), OS, true);
            OS.flush();

            if (Arg.getKind() == TemplateArgument::Expression) {
                DEBUG_AST(std::cout << " (");
                expr.sub.push_back(exprToExpr(Arg.getAsExpr(), Ctx));
                DEBUG_AST(std::cout << " expression),");
            } else
            if (Arg.getKind() == TemplateArgument::Type || Arg.getKind() == TemplateArgument::Template) {
                QualType QT = TSD ? TSD->getTemplateArgs()[i].getAsType().getNonReferenceType() : Arg.getAsType().getNonReferenceType();
                DEBUG_AST(std::cout << "(" << TST << " " << TSD << " ");

//                if (templateToExpr(mod, QT, expr1, Ctx)) {
//                    DEBUG_AST(std::cout << " template " << expr1.value << "),");
//                }
//                else {
//                    QT = QT.getCanonicalType();
//                    QT = QT.getDesugaredType(Ctx);
//                    str = QT.getAsString(Ctx.getPrintingPolicy());
//                    DEBUG_AST(std::cout << " type " << str << "),");
//                    expr1.value = str;
//                    expr1.type = cpphdl::Expr::EXPR_TYPE;
//                }
                cpphdl::Expr expr1 = digQT(mod, QT, Ctx);
                expr.sub.emplace_back(std::move(expr1));

                if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                    auto st = exportStruct(mod, QT->getAsCXXRecordDecl(), Ctx);
                    auto ret = mod.imports.emplace(st.name);
                    if (ret.second) {
                        currProject->structs.emplace_back(std::move(st));
                    }
                }
            } else
            if (Arg.getKind() == TemplateArgument::Integral) {
/*                if (params && params->size() > i) {
                    DEBUG_AST(std::cout << "(" << (*params)[i] << ")");
                    expr1.value = (*params)[i];
                    expr1.sub.push_back(cpphdl::Expr{str, cpphdl::Expr::EXPR_VALUE});
                }
                else {
                }*/
                if (str.length() > 2 && str.find("UL") == str.length()-2) {
                    str = str.replace(str.find("UL"), 2, "");
                }
                cpphdl::Expr expr1 = cpphdl::Expr{str, cpphdl::Expr::EXPR_VALUE};
                expr.sub.emplace_back(std::move(expr1));
                DEBUG_AST(std::cout << "(integral " << str << "),");
            } else
            if (Arg.getKind() == TemplateArgument::Declaration) {
//                expr1.value = str;
                DEBUG_AST(std::cout << "(decl " << str << "),");
            } else {
//                expr1.value = str;
                DEBUG_AST(std::cout << "(unhandled " << str << "),");
            }
        }
        return true;
    }
    return false;
}

cpphdl::Expr digQT(cpphdl::Module& mod, QualType& QT, ASTContext& Ctx)
{
    cpphdl::Expr arrayExpr;
    bool array = false;
    while (const clang::ArrayType* AT = Ctx.getAsArrayType(QT)) {
        if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
            DEBUG_AST(std::cout << " [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
            arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_VALUE});
            arrayExpr.value = "c_array";
        }
        else if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
            DEBUG_AST(std::cout << " [v_array");
            arrayExpr.sub.push_back(exprToExpr(VAT->getSizeExpr(), Ctx));
            DEBUG_AST(std::cout << "] ");
            arrayExpr.value = "v_array";
        }
        else if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
            DEBUG_AST(std::cout << " [d_array");
            arrayExpr.sub.push_back(exprToExpr(DSAT->getSizeExpr(), Ctx));
            DEBUG_AST(std::cout << "] ");
            arrayExpr.value = "d_array";
        }

        arrayExpr.type = cpphdl::Expr::EXPR_ARRAY;
        QT = AT->getElementType();
        array = true;
    }

    cpphdl::Expr expr;
    DEBUG_AST(std::cout << " (");
    if (templateToExpr(mod, QT, expr, Ctx)) {
        DEBUG_AST(std::cout << " template) " << expr.value);
    }
    else {
//        QT = QT.getDesugaredType(Ctx);
//        QT = QT.getCanonicalType();
        std::string str = QT.getAsString(Ctx.getPrintingPolicy());
        DEBUG_AST(std::cout << str << " type) " << str);
        expr.value = str;
        expr.type = cpphdl::Expr::EXPR_TYPE;
    }

    if (array) {
        arrayExpr.sub.push_back(std::move(expr));
        expr = std::move(arrayExpr);
    }
    return expr;
}

void addSpecializationName(std::string& name, std::vector<cpphdl::Field>& params, bool onlyTypes = true)
{
    bool first = true;
    for (auto& param : params) {
        if (!onlyTypes || param.type.type != cpphdl::Expr::EXPR_VALUE) {
            std::string str = param.type.value;
            size_t pos;
            while ((pos = str.find("<")) != (size_t)-1 || (pos = str.find(">")) != (size_t)-1 || (pos = str.find(" ")) != (size_t)-1) {
                str.replace(pos, 1, "");
            }
            while ((pos = str.find(",")) != (size_t)-1) {
                str.replace(pos, 1, "_");
            }
            while ((pos = str.find("-")) != (size_t)-1) {
                str.replace(pos, 1, "m");
            }
            name += (!first?std::string("_"):"") + str;
            first = false;
        }
    }
}

cpphdl::Struct exportStruct(cpphdl::Module& mod, CXXRecordDecl* RD, ASTContext& Ctx)
{
    std::string sname = RD->getQualifiedNameAsString();
    size_t pos;
    while ((pos = sname.find("::")) != (size_t)-1) {
        sname.replace(pos, 2, "__");
    }

    // extracting parameters of the template
    std::vector<cpphdl::Field> params;
    specializationToParameters(mod, RD, params, Ctx);
    addSpecializationName(sname, params, false);

    cpphdl::Struct st{sname, (RD->isUnion() ? cpphdl::Struct::STRUCT_UNION : cpphdl::Struct::STRUCT_STRUCT)};
    st.origName = RD->getQualifiedNameAsString();
    DEBUG_AST(std::cout << "    exportStruct(" << RD->getQualifiedNameAsString() << "):\n");

    for (Decl* D : RD->decls()) {
        if (auto* FD = dyn_cast<FieldDecl>(D)) {
            DEBUG_AST(std::cout << "    SField:");

            bool pointer = false;
            QualType QT = FD->getType().getNonReferenceType();
            if (QT->isPointerType()) {
                QT = QT->getPointeeType();
                pointer = true;
                DEBUG_AST(std::cout << " *pointer*");
            }

            cpphdl::Expr arrayExpr;
            bool array = false;
            while (const clang::ArrayType* AT = Ctx.getAsArrayType(QT)) {
                if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
                    DEBUG_AST(std::cout << " [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
                    arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_VALUE});
                    arrayExpr.value = "c_array";
                }
                else if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
                    DEBUG_AST(std::cout << " [v_array");
                    arrayExpr.sub.push_back(exprToExpr(VAT->getSizeExpr(), Ctx));
                    DEBUG_AST(std::cout << "] ");
                    arrayExpr.value = "v_array";
                }
                else if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
                    DEBUG_AST(std::cout << " [d_array");
                    arrayExpr.sub.push_back(exprToExpr(DSAT->getSizeExpr(), Ctx));
                    DEBUG_AST(std::cout << "] ");
                    arrayExpr.value = "d_array";
                }

                arrayExpr.type = cpphdl::Expr::EXPR_ARRAY;
                QT = AT->getElementType();
                array = true;
            }

            cpphdl::Expr expr;
            DEBUG_AST(std::cout << " (");
//            if (templateToExpr(mod, QT, expr, Ctx)) {
//            std::vector<cpphdl::Field> params;
//            if (specializationToParameters(mod, FD, params, Ctx)) {
//                DEBUG_AST(std::cout << " template) " << expr.value);
//            }
//            else {
                QT = QT.getCanonicalType();
                QT = QT.getDesugaredType(Ctx);
                std::string str = QT.getAsString(Ctx.getPrintingPolicy());
                DEBUG_AST(std::cout << str << " type) " << str);
                expr.value = str;
                expr.type = cpphdl::Expr::EXPR_TYPE;
//            }

            if (array) {
                arrayExpr.sub.push_back(std::move(expr));
                expr = std::move(arrayExpr);
            }

            if (!pointer) {

                QT = QT.getNonReferenceType();
                QT = QT.getDesugaredType(Ctx); // remove typedefs, aliases, etc.
                QT = QT.getCanonicalType();        // ensure you have the actual canonical form

                DEBUG_AST(std::cout << " {var " << FD->getNameAsString() << "} " << (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->isAnonymousStructOrUnion()?"ANON":""));
                st.fields.emplace_back(cpphdl::Field{FD->getNameAsString(), std::move(expr)/*, std::move(params)*/});
                if (FD->isBitField()) {
                    DEBUG_AST(std::cout << " |bitfield ");
                    st.fields.back().bitwidth = exprToExpr(FD->getBitWidth(), Ctx);
                    DEBUG_AST(std::cout << "|");
                }
//                DEBUG_EXPR(std::cout << "    Expr: " << st.fields.back().type.debug());
                if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1 &&
                    QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("std::") == (size_t)-1) {  // we need std containers in structs?
                    auto st1 = exportStruct(mod, QT->getAsCXXRecordDecl(), Ctx);

                    if (QT->getAsCXXRecordDecl()->isAnonymousStructOrUnion() || !QT->getAsCXXRecordDecl()->getIdentifier()) {
                        st1.name = FD->getNameAsString();
                        st.fields.back().definition = std::move(st1);
                    }
                    else {
                        auto ret = mod.imports.emplace(st1.name);
                        if (ret.second) {
                            currProject->structs.emplace_back(std::move(st1));
                        }
                    }
                }
            }
            DEBUG_EXPR(std::cout << "    Expr: " << st.fields.back().type.debug());
            if (FD->isBitField()) {
                DEBUG_EXPR(std::cout << "    Expr(bitfield): " << st.fields.back().bitwidth.debug());
            }
        }
        DEBUG_AST(std::cout << "    ");
    }
    DEBUG_AST(std::cout << "\n");

    return st;
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

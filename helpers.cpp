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
#include "Field.h"

#include <iostream>

unsigned debugIndent = 0;

cpphdl::Struct exportStruct(CXXRecordDecl* RD, Helpers& hlp);
std::string putMethod(const CXXMethodDecl* MD, Helpers& hlp);
CXXRecordDecl* lookupQualifiedRecord(ASTContext* Ctx, llvm::StringRef QualifiedName);
const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext &Ctx);

cpphdl::Expr Helpers::exprToExpr(const Stmt* E)
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
    DEBUG_AST1(" exprToExpr(" << std::string(Lexer::getSourceText(Range, SM, LangOpts)) << "): {");
    on_return ret_debug([](){ DEBUG_AST1("}"); });

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

    if (auto* CS = dyn_cast<CompoundStmt>(E)) {
        DEBUG_AST1(" CompoundStmt");

        cpphdl::Expr expr = cpphdl::Expr{"compound", cpphdl::Expr::EXPR_BODY};
        for (auto* S : CS->body()) {
            expr.sub.push_back(exprToExpr(S));
        }
        return expr;
    }

    if (dyn_cast<NullStmt>(E)) {
        DEBUG_AST1(" NullStmt");

        cpphdl::Expr expr = cpphdl::Expr{"", cpphdl::Expr::EXPR_VALUE};
        return expr;
    }

//    E = E->IgnoreParenImpCasts();  //?

    if (auto* BO = dyn_cast<BinaryOperator>(E)) {
        DEBUG_AST1(" BinaryOperator(" << BO->getOpcodeStr().data() << ")");
        return cpphdl::Expr{BO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(BO->getLHS()),exprToExpr(BO->getRHS())}};
    }
    if (auto* CAO = dyn_cast<CompoundAssignOperator>(E)) {
        DEBUG_AST1(" CompoundAssignOperator(" << CAO->getOpcodeStr().data() << ")");
        return cpphdl::Expr{CAO->getOpcodeStr().data(), cpphdl::Expr::EXPR_BINARY, {exprToExpr(CAO->getLHS()),exprToExpr(CAO->getRHS())}};
    }
    if (auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        DEBUG_AST1(" DeclRefExpr(" << DRE->getNameInfo().getAsString() << ")");

        const auto* Parent = getParentClassOfExpr(DRE, Ctx);

        std::string prefix;
        if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
//std::cout << "\n!!!" << (size_t)Parent << " " << FD->hasInClassInitializer() << "!!!\n";
            if (Parent && VD->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
                std::string sname = Parent->getQualifiedNameAsString();
                size_t pos;
                while ((pos = sname.find("::")) != (size_t)-1) {
                    sname.replace(pos, 2, "__");
                }
                // extracting parameters of the template
                std::vector<cpphdl::Field> params;
                specializationToParameters(Parent, params);
                addSpecializationName(sname, params, false);
                prefix = sname + "_pkg::";
            }
        }

        return cpphdl::Expr{prefix + DRE->getDecl()->getNameAsString(), cpphdl::Expr::EXPR_VAR};
    }
    if (auto* IL = dyn_cast<IntegerLiteral>(E)) {
        DEBUG_AST1(" IntegerLiteral(" << std::to_string(IL->getValue().getSExtValue()) << ")");
        return cpphdl::Expr{std::to_string(IL->getValue().getSExtValue()), cpphdl::Expr::EXPR_VALUE};
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
            llvm::outs() << "Called method: " << MD->getQualifiedNameAsString() << "\n";
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
        const CXXRecordDecl *Parent = dyn_cast<CXXRecordDecl>(ME->getMemberDecl()->getDeclContext());
        if (/*const auto* FD =*/ dyn_cast<FieldDecl>(ME->getMemberDecl())) {
//            Parent = FD->getParent();
            if (Parent && Parent->isAnonymousStructOrUnion()) {
                anon = true;
                DEBUG_AST1(" ANON");
            }
        }

        bool ignoreBase = false;
        std::string prefix;
        if (const auto* VD = dyn_cast<VarDecl>(ME->getMemberDecl())) {
            if (Parent && VD->isConstexpr() && (flags&FLAG_EXTERNAL_THIS)) {  // make name for pkg constexpr parameter access
                std::string sname = Parent->getQualifiedNameAsString();
                size_t pos;
                while ((pos = sname.find("::")) != (size_t)-1) {
                    sname.replace(pos, 2, "__");
                }
                // extracting parameters of the template
                std::vector<cpphdl::Field> params;
                specializationToParameters(Parent, params);
                addSpecializationName(sname, params, false);
                prefix = sname + "_pkg::";
                ignoreBase = true;
            }
        }

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL");
        }

        return cpphdl::Expr{prefix + ME->getMemberDecl()->getNameAsString(), cpphdl::Expr::EXPR_MEMBER, {exprToExpr(ME->getBase())},
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
    if (auto* CE = dyn_cast<CallExpr>(E)) {
        DEBUG_AST1(" CallExpr(" << (CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():"") << ")");

        cpphdl::Expr call = cpphdl::Expr{(CE->getDirectCallee()?CE->getDirectCallee()->getNameAsString():""), cpphdl::Expr::EXPR_CALL};
        for (auto* arg : CE->arguments()) {
            call.sub.push_back(exprToExpr(arg));
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
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST, {exprToExpr(CE->getArg(0))}};
            }
            else {
                return cpphdl::Expr{"constructor", cpphdl::Expr::EXPR_CAST};
            }
        }
    }
    if (auto* CL = dyn_cast<CharacterLiteral>(E)) {
        DEBUG_AST1(" CharacterLiteral");
        return cpphdl::Expr{std::to_string(CL->getValue()), cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* CLE = dyn_cast<CompoundLiteralExpr>(E)) {
        DEBUG_AST1(" CompoundLiteralExpr");
        return cpphdl::Expr{CLE->getType().getAsString(), cpphdl::Expr::EXPR_INIT, {{exprToExpr(CLE->getInitializer())}}};
    }
    if (auto* SL = dyn_cast<StringLiteral>(E)) {
        DEBUG_AST1(" StringLiteral");

        clang::SourceRange Range = SL->getSourceRange();
        clang::LangOptions LO = Ctx.getLangOpts();

        llvm::StringRef RawText = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(Range),
            Ctx.getSourceManager(),
            LO);

        return cpphdl::Expr{RawText.str(), cpphdl::Expr::EXPR_STRING};
    }
    if (auto* BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
        DEBUG_AST1(" CXXBoolLiteralExpr");
        return cpphdl::Expr{BLE->getValue()?"1":"0", cpphdl::Expr::EXPR_VALUE};
    }
    if (/*auto* ME =*/dyn_cast<CXXThisExpr>(E)) {
        DEBUG_AST1(" CXXThisExpr");
        return cpphdl::Expr{"_this", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* UO = dyn_cast<UnaryOperator>(E)) {
        DEBUG_AST1(" UnaryOperator");
        return cpphdl::Expr{UO->getOpcodeStr(UO->getOpcode()).str(), cpphdl::Expr::EXPR_UNARY, {exprToExpr(UO->getSubExpr())}};
    }
    if (auto* CO = dyn_cast<ConditionalOperator>(E)) {
        DEBUG_AST1(" ConditionalOperator");
        return cpphdl::Expr{"", cpphdl::Expr::EXPR_COND, {exprToExpr(CO->getCond()),exprToExpr(CO->getTrueExpr()),exprToExpr(CO->getFalseExpr())}};
    }
    if (auto* ASE = dyn_cast<ArraySubscriptExpr>(E)) {
        DEBUG_AST1(" ArraySubscriptExpr");
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
        return cpphdl::Expr{"MaterializeTemporaryExpr", cpphdl::Expr::EXPR_CAST, {exprToExpr(MTE->getSubExpr())}};
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
        return cpphdl::Expr{"nullptr", cpphdl::Expr::EXPR_VALUE};
    }
    if (/*auto* CE = */dyn_cast<InitListExpr>(E)) {
        DEBUG_AST1(" InitListExpr");
        return cpphdl::Expr{"0", cpphdl::Expr::EXPR_VALUE};
    }
    if (auto* UETTE = dyn_cast<UnaryExprOrTypeTraitExpr>(E)) {
        DEBUG_AST1(" UnaryExprOrTypeTraitExpr");

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
                return cpphdl::Expr{op, cpphdl::Expr::EXPR_TRAIT, {exprToExpr(UETTE->getArgumentExpr())}};
            }
        }
    }
    if (auto* PE = dyn_cast<ParenExpr>(E)) {
        DEBUG_AST1(" ParenExpr");
        return cpphdl::Expr{"paren", cpphdl::Expr::EXPR_PAREN, {exprToExpr(PE->getSubExpr())}};
    }
    if (auto* SNTTPE = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
        DEBUG_AST1(" SubstNonTypeTemplateParmExpr");

        if ((flags&FLAG_EXTERNAL_THIS)) {
            DEBUG_AST1(" EXTERNAL");
        }

        return cpphdl::Expr{SNTTPE->getParameter()->getName().str(), cpphdl::Expr::EXPR_PARAM, {exprToExpr(SNTTPE->getReplacement())},
            ((flags&FLAG_EXTERNAL_THIS)?cpphdl::Expr::FLAG_SPECVAL:0U)};
    }
    if (auto* RS = dyn_cast<ReturnStmt>(E)) {
        DEBUG_AST1(" ReturnStmt");
        if (RS->getRetValue()) {
            return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN, {exprToExpr(RS->getRetValue())}};
        }
        return cpphdl::Expr{"return", cpphdl::Expr::EXPR_RETURN};
    }
/*
    if (auto* FL = dyn_cast<FloatingLiteral>(E)) {
        DEBUG_AST1(" FloatingLiteral");
//        return cpphdl::Expr{std::to_string(FL->getValue()), cpphdl::Expr::EXPR_VALUE};
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

bool Helpers::specializationToParameters(const CXXRecordDecl* RD, std::vector<cpphdl::Field>& params)
{
    if (auto* TSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
        DEBUG_AST(debugIndent++, "Specialization parameters:");
        on_return ret_debug([](){ --debugIndent; });

        const TemplateArgumentList& Args = TSD->getTemplateArgs();
        const TemplateParameterList* Params = TSD->getSpecializedTemplate()->getTemplateParameters();
        for (unsigned i = 0; i < Args.size(); ++i) {
            const TemplateArgument& Arg = Args[i];
            switch (Arg.getKind()) {
                case TemplateArgument::Type:
                {
                    QualType QT = Arg.getAsType();

                    [[maybe_unused]] bool pointer = false;
                    QT.getNonReferenceType();
                    if (QT->isPointerType()) {  // while?
                        QT = QT->getPointeeType();
                        pointer = true;
                        DEBUG_AST1(" *pointer*");
                    }

//                QT = QT.getNonReferenceType();
//                QT = QT.getDesugaredType(Ctx); // remove typedefs, aliases, etc.
//                QT = QT.getCanonicalType();        // ensure you have the actual canonical form
//digQT(mod, QT);

                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    QT.print(OS, Ctx.getPrintingPolicy());
                    OS.flush();
                    cpphdl::Expr expr = cpphdl::Expr{str, cpphdl::Expr::EXPR_TYPE};
                    DEBUG_AST1(" type: " << OS.str());
                    params.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(), expr});
                    DEBUG_EXPR1(" Expr: " << params.back().expr.debug(debugIndent));
                    if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                        auto st = exportStruct(QT->getAsCXXRecordDecl(), *this);
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
                    DEBUG_AST1(" integral: " << str);
                    if (str.length() > 2 && str.find("UL") == str.length()-2) {
                        str = str.replace(str.find("UL"), 2, "");
                    }
                    params.emplace_back(cpphdl::Field{Params->getParam(i)->getNameAsString(),
                        cpphdl::Expr{Arg.getIntegralType()->isBooleanType() ? (Arg.getAsIntegral().isZero() ? "false" : "true") : str, cpphdl::Expr::EXPR_VALUE}});
                    DEBUG_EXPR1(" Expr: " << params.back().expr.debug(debugIndent));
                    break;
                }
                case TemplateArgument::Declaration:
                    DEBUG_AST1(" declaration: " << Arg.getAsDecl()->getNameAsString());
                    break;
                case TemplateArgument::Template:
                {
                    TemplateName TN = Arg.getAsTemplate();
                    std::string str;
                    llvm::raw_string_ostream OS(str);
                    TN.print(OS, Ctx.getPrintingPolicy());
                    OS.flush();
                    DEBUG_AST1(" template: " << OS.str());
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
                        DEBUG_AST1(" expression: " << SR.str());
                        DEBUG_EXPR1(" Expr: " << exprToExpr(E).debug(debugIndent));
                    }
                    break;
                }
                case TemplateArgument::Pack:
                default:
                    DEBUG_AST1(" unhandled\n");
                break;
            }
        }
        return true;
    }
    return false;
}

bool Helpers::templateToExpr(QualType QT, cpphdl::Expr& expr)
{
    DEBUG_AST1(/*debugIndent++, */"templateToExpr:");
//    on_return ret_debug([](){ --debugIndent; });
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
                DEBUG_AST1(" (");
                expr.sub.push_back(exprToExpr(Arg.getAsExpr()));
                DEBUG_AST1(" expression),");
            } else
            if (Arg.getKind() == TemplateArgument::Type || Arg.getKind() == TemplateArgument::Template) {
                QualType QT = TSD ? TSD->getTemplateArgs()[i].getAsType().getNonReferenceType() : Arg.getAsType().getNonReferenceType();
                DEBUG_AST1("(" << TST << ":" << TSD << " ");

//                if (templateToExpr(mod, QT, expr1)) {
//                    DEBUG_AST1(" template " << expr1.value << "),");
//                }
//                else {
//                    QT = QT.getCanonicalType();
//                    QT = QT.getDesugaredType(Ctx);
//                    str = QT.getAsString(Ctx.getPrintingPolicy());
//                    DEBUG_AST1(" type " << str << "),");
//                    expr1.value = str;
//                    expr1.type = cpphdl::Expr::EXPR_TYPE;
//                }
                cpphdl::Expr expr1 = digQT(QT);
                expr.sub.emplace_back(std::move(expr1));

                if (QT->getAsCXXRecordDecl() && QT->getAsCXXRecordDecl()->getQualifiedNameAsString().find("cpphdl::") == (size_t)-1) {
                    auto st = exportStruct(QT->getAsCXXRecordDecl(), *this);
                    auto ret = mod.imports.emplace(st.name);
                    if (ret.second) {
                        currProject->structs.emplace_back(std::move(st));
                    }
                }
                DEBUG_AST1("), ");
            } else
            if (Arg.getKind() == TemplateArgument::Integral) {
/*                if (params && params->size() > i) {
                    DEBUG_AST1("(" << (*params)[i] << ")");
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
                DEBUG_AST1("(integral " << str << "),");
            } else
            if (Arg.getKind() == TemplateArgument::Declaration) {
//                expr1.value = str;
                DEBUG_AST1("(decl " << str << "),");
            } else {
//                expr1.value = str;
                DEBUG_AST1("(unhandled " << str << "),");
            }
        }
        return true;
    }
    return false;
}

cpphdl::Expr Helpers::digQT(QualType& QT)
{
    cpphdl::Expr arrayExpr;
    bool array = false;
    while (const clang::ArrayType* AT = Ctx.getAsArrayType(QT)) {
        if (const auto* CAT = llvm::dyn_cast<clang::ConstantArrayType>(AT)) {
            DEBUG_AST1(" [c_array " << std::to_string(CAT->getSize().getLimitedValue()) << "]");
            arrayExpr.sub.push_back(cpphdl::Expr{std::to_string(CAT->getSize().getLimitedValue()), cpphdl::Expr::EXPR_VALUE});
            arrayExpr.value = "c_array";
        }
        else if (const auto* VAT = llvm::dyn_cast<clang::VariableArrayType>(AT)) {
            DEBUG_AST1(" [v_array");
            arrayExpr.sub.push_back(exprToExpr(VAT->getSizeExpr()));
            DEBUG_AST1("] ");
            arrayExpr.value = "v_array";
        }
        else if (const auto* DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(AT)) {
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
        DEBUG_AST1(" template) " << expr.value);
    }
    else {
        QT = QT.getDesugaredType(Ctx);
        QT = QT.getCanonicalType();
        std::string str = QT.getAsString(Ctx.getPrintingPolicy());
        DEBUG_AST1(str << " type) " << str);
        expr.value = str;
        expr.type = cpphdl::Expr::EXPR_TYPE;
    }

    if (array) {
        arrayExpr.sub.push_back(std::move(expr));
        expr = std::move(arrayExpr);
    }
    return expr;
}

void Helpers::addSpecializationName(std::string& name, std::vector<cpphdl::Field>& params, bool onlyTypes)
{
    bool first = true;
    for (auto& param : params) {
        if (!onlyTypes || param.expr.type != cpphdl::Expr::EXPR_VALUE) {
            std::string str = param.expr.value;
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

const CXXRecordDecl* getParentClassOfExpr(const DeclRefExpr* DRE, ASTContext &Ctx)
{
    DynTypedNode Node = DynTypedNode::create(*DRE);

    while (true) {
        auto Parents = Ctx.getParents(Node);

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

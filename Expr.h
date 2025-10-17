


std::string exprToString(const Expr *E)
{
    E = E->IgnoreParenImpCasts();

    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
        std::string LHS = exprToString(BO->getLHS());
        std::string RHS = exprToString(BO->getRHS());
        return "(" + LHS + " " + BO->getOpcodeStr().str() + " " + RHS + ")";
    } 
    else if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
        return DRE->getNameInfo().getAsString();
    } 
    else if (auto *IL = dyn_cast<IntegerLiteral>(E)) {
        return std::to_string(IL->getValue().getSExtValue());
    } 
    else if (auto *CE = dyn_cast<CallExpr>(E)) {
        std::string result;
        if (const FunctionDecl *FD = CE->getDirectCallee())
            result += FD->getNameAsString();
        result += "(";
        bool first = true;
        for (auto *arg : CE->arguments()) {
            if (!first) result += ", ";
            result += exprToString(arg);
            first = false;
        }
        result += ")";
        return result;
    } 
    else {
        return "<unknown-expr>";
    }
}

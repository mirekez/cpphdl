    std::string exprType(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return exprType(*expr.as<ParenthesizedExpressionSyntax>().expression);
        }
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod && mod->types.count(name)) {
                return mod->types[name];
            }
            return "";
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return fieldTypeFor(exprType(*e.left), tok(e.name));
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            auto type = unwrappedValueType(exprType(*e.left));
            auto args = templateArgsFor(type, "array");
            if (args.size() == 2) {
                return args[0];
            }
            return type;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            std::string type;
            if (mod && mod->types.count(tok(n.identifier))) {
                type = mod->types[tok(n.identifier)];
            }
            for (auto sel : n.selectors) {
                auto args = templateArgsFor(unwrappedValueType(type), "array");
                if (args.size() == 2) {
                    type = args[0];
                }
            }
            return type;
        }
        return "";
    }

    std::string pathType(std::string path)
    {
        path = trim(exprText(std::move(path)));
        if (path.find("::") != std::string::npos) {
            return "";
        }
        std::vector<std::string> names;
        for (size_t i = 0; i < path.size();) {
            if (!(std::isalpha(static_cast<unsigned char>(path[i])) || path[i] == '_')) {
                ++i;
                continue;
            }
            auto start = i++;
            while (i < path.size() && (std::isalnum(static_cast<unsigned char>(path[i])) || path[i] == '_')) {
                ++i;
            }
            names.push_back(path.substr(start, i - start));
            while (i < path.size() && std::isspace(static_cast<unsigned char>(path[i]))) {
                ++i;
            }
            if (i + 1 < path.size() && path[i] == '(' && path[i + 1] == ')') {
                i += 2;
            }
            while (i < path.size() && path[i] == '[') {
                int depth = 1;
                ++i;
                while (i < path.size() && depth != 0) {
                    if (path[i] == '[') {
                        ++depth;
                    }
                    else if (path[i] == ']') {
                        --depth;
                    }
                    ++i;
                }
            }
            while (i < path.size() && std::isspace(static_cast<unsigned char>(path[i]))) {
                ++i;
            }
            if (i >= path.size()) {
                break;
            }
            if (path[i] != '.') {
                return "";
            }
            ++i;
        }
        if (names.empty() || !mod || !mod->types.count(names.front())) {
            return "";
        }
        auto type = mod->types[names.front()];
        for (size_t i = 1; i < names.size(); ++i) {
            type = fieldTypeFor(type, names[i]);
            if (type.empty()) {
                return "";
            }
        }
        return type;
    }

    std::string pathValueExpr(std::string path)
    {
        path = trim(exprText(std::move(path)));
        if (path.find("::") != std::string::npos || path.find('(') != std::string::npos) {
            return "";
        }
        std::vector<std::pair<std::string, std::string>> segments;
        for (size_t i = 0; i < path.size();) {
            if (!(std::isalpha(static_cast<unsigned char>(path[i])) || path[i] == '_')) {
                ++i;
                continue;
            }
            auto start = i++;
            while (i < path.size() && (std::isalnum(static_cast<unsigned char>(path[i])) || path[i] == '_')) {
                ++i;
            }
            auto name = path.substr(start, i - start);
            std::string selects;
            while (i < path.size() && path[i] == '[') {
                auto selStart = i;
                int depth = 1;
                ++i;
                while (i < path.size() && depth != 0) {
                    if (path[i] == '[') {
                        ++depth;
                    }
                    else if (path[i] == ']') {
                        --depth;
                    }
                    ++i;
                }
                selects += path.substr(selStart, i - selStart);
            }
            segments.push_back({name, selects});
            if (i >= path.size()) {
                break;
            }
            if (path[i] != '.') {
                return "";
            }
            ++i;
        }
        if (segments.empty()) {
            return "";
        }
        auto base = segments.front().first;
        std::string out;
        if (mod && mod->outputPortCppNames.count(base)) {
            out = isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : outputStorageName(*mod, base);
        }
        else if (mod && mod->portCppNames.count(base)) {
            out = mod->portCppNames[base] + "()";
        }
        else if (mod && isAssignDrivenVar(*mod, base)) {
            out = base + "()";
        }
        else {
            out = base;
        }
        out += replaceKeywordMemberAccess(segments.front().second);
        for (size_t i = 1; i < segments.size(); ++i) {
            out += "." + cppIdent(segments[i].first);
            out += replaceKeywordMemberAccess(segments[i].second);
        }
        return out;
    }

    std::string exprWidth(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return exprWidth(*expr.as<ParenthesizedExpressionSyntax>().expression);
        }
        auto text = expr.toString();
        auto quote = text.find('\'');
        if (quote != std::string::npos) {
            if (quote > 0) {
                auto width = text.substr(0, quote);
                if (std::all_of(width.begin(), width.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
                    return foldWidth(width);
                }
            }
            else {
                auto literal = trim(exprText(text));
                if (literal.rfind("0x", 0) == 0 || literal.rfind("0X", 0) == 0) {
                    return std::to_string(std::max<unsigned>(32, (literal.size() > 2 ? unsigned(literal.size() - 2) * 4u : 1u)));
                }
                if (literal.rfind("0b", 0) == 0 || literal.rfind("0B", 0) == 0) {
                    return std::to_string(std::max<unsigned>(32, (literal.size() > 2 ? unsigned(literal.size() - 2) : 1u)));
                }
            }
        }
        if (quote == std::string::npos) {
            auto literal = trim(text);
            if (!literal.empty() && (std::isdigit((unsigned char)literal[0]) || literal[0] == '.')) {
                auto clean = literal;
                clean.erase(std::remove(clean.begin(), clean.end(), '_'), clean.end());
                if (clean.rfind("0x", 0) == 0 || clean.rfind("0X", 0) == 0) {
                    return std::to_string(std::max<unsigned>(32, (clean.size() > 2 ? unsigned(clean.size() - 2) * 4u : 1u)));
                }
                if (clean.rfind("0b", 0) == 0 || clean.rfind("0B", 0) == 0) {
                    return std::to_string(std::max<unsigned>(32, (clean.size() > 2 ? unsigned(clean.size() - 2) : 1u)));
                }
                if (std::all_of(clean.begin(), clean.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
                    return "32";
                }
            }
        }
        auto simple = trim(exprText(text));
        for (const auto& [prefix, defaultWidth] : configuredTextMap("HDLCPP_ENUM_WIDTH_PREFIXES")) {
            if (simple.rfind(prefix, 0) == 0) {
                auto name = simple.substr(prefix.size());
                if (name == "C0" || name == "C1" || name == "C2") {
                    return "2";
                }
                if (name.rfind("C0", 0) == 0 || name.rfind("C1", 0) == 0 || name.rfind("C2", 0) == 0) {
                    return "3";
                }
                return defaultWidth;
            }
        }
        if (auto literalWidth = numericLiteralWidth(simple); !literalWidth.empty()) {
            return literalWidth;
        }
        if (simple.find('.') != std::string::npos) {
            auto width = foldWidth(resolvedTypeWidth(pathType(simple)));
            if (!width.empty()) {
                return width;
            }
            auto emitted = pathValueExpr(simple);
            if (emitted.find('.') != std::string::npos) {
                return "cpphdl::type_width<std::remove_cvref_t<decltype(" + emitted + ")>>()";
            }
        }
        if (mod && mod->types.count(simple)) {
            auto w = resolvedTypeWidth(mod->types[simple]);
            if (!w.empty()) {
                return foldWidth(w);
            }
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            if (mod) {
                auto baseName = tok(n.identifier);
                auto it = mod->types.find(baseName);
                if (it != mod->types.end()) {
                    auto selectedType = it->second;
                    bool consumedArraySelect = false;
                    for (auto s : n.selectors) {
                        if (selectedType.rfind("array<", 0) == 0) {
                            auto args = memoryArgs("memory<" + selectedType.substr(6, selectedType.size() - 7) + ">");
                            if (args.size() == 2) {
                                selectedType = args[0];
                                consumedArraySelect = true;
                                continue;
                            }
                        }
                        auto w = foldWidth(selectWidth(*s));
                        if (!w.empty()) {
                            return w;
                        }
                    }
                    if (consumedArraySelect) {
                        auto w = foldWidth(resolvedTypeWidth(selectedType));
                        return w;
                    }
                }
            }
            auto width = foldWidth(selectsWidth(n.selectors));
            if (!width.empty()) {
                return width;
            }
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto width = foldWidth(resolvedTypeWidth(exprType(expr)));
            if (!width.empty()) {
                return width;
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            if (e.select) {
                if (e.left->kind == SyntaxKind::IdentifierName) {
                    auto baseName = tok(e.left->as<IdentifierNameSyntax>().identifier);
                    if (loopVars.count(baseName)) {
                        auto w = foldWidth(selectWidth(*e.select));
                        if (!w.empty()) {
                            return w;
                        }
                    }
                }
                auto rawSelect = e.select->toString();
                if ((loopVars.count("i") && rawSelect.find("i") != std::string::npos) ||
                    (loopVars.count("j") && rawSelect.find("j") != std::string::npos) ||
                    (loopVars.count("k") && rawSelect.find("k") != std::string::npos) ||
                    (loopVars.count("z_gen") && rawSelect.find("z_gen") != std::string::npos) ||
                    (loopVars.count("w_gen") && rawSelect.find("w_gen") != std::string::npos)) {
                    return "64";
                }
                auto rawWidth = selectWidth(*e.select);
                if (rawWidth.find("(i") != std::string::npos ||
                    rawWidth.find("(j") != std::string::npos ||
                    rawWidth.find("(k") != std::string::npos ||
                    rawWidth.find("(uint64_t)(i)") != std::string::npos ||
                    rawWidth.find("(uint64_t)(j)") != std::string::npos ||
                    rawWidth.find("(uint64_t)(k)") != std::string::npos ||
                    rawWidth.find("(uint64_t)((uint64_t)(i))") != std::string::npos ||
                    rawWidth.find("(uint64_t)((uint64_t)(j))") != std::string::npos ||
                    rawWidth.find("(uint64_t)((uint64_t)(k))") != std::string::npos ||
                    rawWidth.find("* i") != std::string::npos ||
                    rawWidth.find("* j") != std::string::npos ||
                    rawWidth.find("* k") != std::string::npos ||
                    rawWidth.find(" i ") != std::string::npos ||
                    rawWidth.find(" j ") != std::string::npos ||
                    rawWidth.find(" k ") != std::string::npos) {
                    return "64";
                }
                auto width = foldWidth(rawWidth);
                if (!width.empty()) {
                    return width;
                }
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            return emitNumericExpr(expr, emitExpr(expr));
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto width = knownFunctionReturnWidth(exprText(i.left->toString()));
            if (!width.empty()) {
                return width;
            }
        }
        auto base = assignedBase(expr);
        if (mod->types.count(base)) {
            auto w = typeWidth(mod->types[base]);
            if (!w.empty()) {
                return foldWidth(w);
            }
        }
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            size_t total = 0;
            bool allConst = true;
            std::vector<std::string> widths;
            for (auto e : expr.as<ConcatenationExpressionSyntax>().expressions) {
                auto w = foldWidth(exprWidth(*e));
                if (!w.empty()) {
                    widths.push_back(w);
                }
                if (w.empty() || !isNumber(w)) {
                    allConst = false;
                    continue;
                }
                total += std::stoul(w);
            }
            if (allConst) {
                return std::to_string(total);
            }
            if (!widths.empty()) {
                std::string out;
                for (auto& w : widths) {
                    if (!out.empty()) {
                        out += "+";
                    }
                    out += "(" + w + ")";
                }
                return out;
            }
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto& m = expr.as<MultipleConcatenationExpressionSyntax>();
            auto count = replaceKeywordMemberAccess(exprText(m.expression->toString()));
            if (count.size() >= 2 && count.front() == '{' && count.back() == '}') {
                count = trim(count.substr(1, count.size() - 2));
            }
            auto innerWidth = foldWidth(exprWidth(*m.concatenation));
            if (!count.empty() && !innerWidth.empty() &&
                isNumber(count) && isNumber(innerWidth)) {
                return std::to_string(std::stoul(count) * std::stoul(innerWidth));
            }
            if (!count.empty() && !innerWidth.empty()) {
                return "(" + count + ")*(" + innerWidth + ")";
            }
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            auto left = foldWidth(exprWidth(*c.left));
            auto right = foldWidth(exprWidth(*c.right));
            if (isNumber(left) && isNumber(right)) {
                return std::to_string(std::max(std::stoul(left), std::stoul(right)));
            }
            if (!left.empty() && left == right) {
                return left;
            }
            if (left.rfind("cpphdl::type_width<", 0) == 0 && right == "1") {
                return left;
            }
            if (right.rfind("cpphdl::type_width<", 0) == 0 && left == "1") {
                return right;
            }
            if (isZeroLiteralExpr(*c.left) && !right.empty()) {
                return right;
            }
            if (isZeroLiteralExpr(*c.right) && !left.empty()) {
                return left;
            }
            if (!left.empty() || !right.empty()) {
                return "64";
            }
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto op = tok(b.operatorToken);
            if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
                op == "&&" || op == "||") {
                return "1";
            }
            auto left = foldWidth(exprWidth(*b.left));
            auto right = foldWidth(exprWidth(*b.right));
            if (op == "<<" || op == ">>" || op == "<<<" || op == ">>>") {
                return left.empty() ? "32" : left;
            }
            if (isNumber(left) && isNumber(right)) {
                return std::to_string(std::max(std::stoul(left), std::stoul(right)));
            }
            if (!left.empty()) {
                return left;
            }
            if (!right.empty()) {
                return right;
            }
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            auto op = tok(u.operatorToken);
            if (op == "!" || op == "&" || op == "|" || op == "^" || op == "~&" || op == "~|" || op == "~^" || op == "^~") {
                return "1";
            }
            auto operand = foldWidth(exprWidth(*u.operand));
            if (!operand.empty()) {
                return operand;
            }
        }
        return "1";
    }

    std::string emittedBitsCallWidth(const std::string& expr)
    {
        auto pos = expr.rfind(".bits(");
        if (pos == std::string::npos) {
            return "";
        }
        auto start = pos + 6;
        int depth = 1;
        size_t comma = std::string::npos;
        size_t end = std::string::npos;
        for (size_t i = start; i < expr.size(); ++i) {
            if (expr[i] == '(') {
                ++depth;
            }
            else if (expr[i] == ')') {
                if (--depth == 0) {
                    end = i;
                    break;
                }
            }
            else if (expr[i] == ',' && depth == 1) {
                comma = i;
            }
        }
        if (comma == std::string::npos || end == std::string::npos || comma <= start || comma >= end) {
            return "";
        }
        auto left = foldWidth(expr.substr(start, comma - start));
        auto right = foldWidth(expr.substr(comma + 1, end - comma - 1));
        if (isNumber(left) && isNumber(right)) {
            auto lv = std::stoul(left);
            auto rv = std::stoul(right);
            return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
        }
        if (right == "0" || right == "0x0") {
            return "(" + left + ")+1";
        }
        return "";
    }

    std::string emittedLogicCastWidth(const std::string& expr)
    {
        auto pos = expr.find("logic<");
        if (pos == std::string::npos) {
            return "";
        }
        auto start = pos + 6;
        int depth = 1;
        for (size_t i = start; i < expr.size(); ++i) {
            if (expr[i] == '<') {
                ++depth;
            }
            else if (expr[i] == '>') {
                if (--depth == 0) {
                    return trim(expr.substr(start, i - start));
                }
            }
        }
        return "";
    }

    std::string emitConcat(const ConcatenationExpressionSyntax& c)
    {
        std::vector<std::pair<std::string, std::string>> parts;
        size_t total = 0;
        bool numeric = true;
        for (auto e : c.expressions) {
            auto width = foldWidth(exprWidth(*e));
            auto emitted = emitNumericExpr(*e);
            auto bitsWidth = emittedBitsCallWidth(emitted);
            if (!bitsWidth.empty()) {
                width = bitsWidth;
            }
            auto castWidth = emittedLogicCastWidth(emitted);
            if (!castWidth.empty() && (width.empty() || width == "1")) {
                width = castWidth;
            }
            if (width.empty()) {
                auto base = assignedBase(*e);
                if (mod && !base.empty() && mod->types.count(base)) {
                    width = foldWidth(resolvedTypeWidth(mod->types[base]));
                }
            }
            if (width.empty()) {
                width = "64";
            }
            parts.push_back({width, emitted});
            if (width.empty() || !isNumber(width)) {
                numeric = false;
            }
            else {
                total += std::stoul(width);
            }
        }
        if (!numeric) {
            std::string args;
            for (auto& p : parts) {
                if (!args.empty()) {
                    args += ", ";
                }
                args += "logic<" + p.first + ">(" + p.second + ")";
            }
            return "cat{" + args + "}";
        }
        std::string out = "logic<" + std::to_string(total) + ">(0)";
        size_t remaining = total;
        for (auto& p : parts) {
            auto width = std::stoul(p.first);
            remaining -= width;
            auto term = "logic<" + std::to_string(total) + ">(" + p.second + ")";
            if (remaining != 0) {
                term = "(" + term + " << " + std::to_string(remaining) + ")";
            }
            out = "(" + out + " | " + term + ")";
        }
        return out;
    }

    std::string emitCaseLabelExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            auto& c = expr.as<ConcatenationExpressionSyntax>();
            uint64_t value = 0;
            size_t total = 0;
            for (auto e : c.expressions) {
                auto width = foldWidth(exprWidth(*e));
                if (!isNumber(width)) {
                    return emitNumericExpr(expr);
                }
                auto w = std::stoul(width);
                if (w == 0 || w >= 64 || total + w >= 64) {
                    return emitNumericExpr(expr);
                }
                uint64_t part = 0;
                if (!parseCppIntegralLiteral(exprText(e->toString()), part)) {
                    return emitNumericExpr(expr);
                }
                value = (value << w) | (part & ((1ull << w) - 1ull));
                total += w;
            }
            return std::to_string(value) + "ull";
        }
        return emitNumericExpr(expr);
    }

    bool statementCallsWork(const StatementSyntax& st)
    {
        if (st.kind == SyntaxKind::TimingControlStatement) {
            return statementCallsWork(*st.as<TimingControlStatementSyntax>().statement);
        }
        if (st.kind == SyntaxKind::SequentialBlockStatement || st.kind == SyntaxKind::ParallelBlockStatement) {
            for (auto item : st.as<BlockStatementSyntax>().items) {
                if (StatementSyntax::isKind(item->kind) && statementCallsWork(item->as<StatementSyntax>())) {
                    return true;
                }
            }
        }
        if (st.kind == SyntaxKind::ExpressionStatement) {
            auto& e = st.as<ExpressionStatementSyntax>();
            return exprText(e.expr->toString()).rfind("_work(", 0) == 0;
        }
        return false;
    }

    void collectLoopVars(const SyntaxNode& node)
    {
        if (node.kind == SyntaxKind::ForLoopStatement) {
            auto& f = node.as<ForLoopStatementSyntax>();
            for (auto init : f.initializers) {
                if (init->kind == SyntaxKind::ForVariableDeclaration) {
                    loopVars.insert(tok(init->as<ForVariableDeclarationSyntax>().declarator->name));
                }
                else if (ExpressionSyntax::isKind(init->kind)) {
                    auto& e = init->as<ExpressionSyntax>();
                    if (BinaryExpressionSyntax::isKind(e.kind)) {
                        loopVars.insert(assignedBase(*e.as<BinaryExpressionSyntax>().left));
                    }
                    else {
                        loopVars.insert(assignedBase(e));
                    }
                }
            }
            for (auto step : f.steps) {
                if (BinaryExpressionSyntax::isKind(step->kind)) {
                    loopVars.insert(assignedBase(*step->as<BinaryExpressionSyntax>().left));
                }
                else if (PostfixUnaryExpressionSyntax::isKind(step->kind)) {
                    loopVars.insert(assignedBase(*step->as<PostfixUnaryExpressionSyntax>().operand));
                }
                else if (PrefixUnaryExpressionSyntax::isKind(step->kind)) {
                    loopVars.insert(assignedBase(*step->as<PrefixUnaryExpressionSyntax>().operand));
                }
                else {
                    loopVars.insert(assignedBase(*step));
                }
            }
            collectLoopVars(*f.statement);
        }
        else if (node.kind == SyntaxKind::TimingControlStatement) {
            collectLoopVars(*node.as<TimingControlStatementSyntax>().statement);
        }
        else if (node.kind == SyntaxKind::ConditionalStatement) {
            auto& c = node.as<ConditionalStatementSyntax>();
            collectLoopVars(*c.statement);
            if (c.elseClause) {
                collectLoopVars(*c.elseClause->clause);
            }
        }
        else if (node.kind == SyntaxKind::SequentialBlockStatement || node.kind == SyntaxKind::ParallelBlockStatement) {
            for (auto item : node.as<BlockStatementSyntax>().items) {
                collectLoopVars(*item);
            }
        }
    }

    void emitNode(const SyntaxNode& node, std::vector<std::string>& out, bool comb, int indent)
    {
        if (StatementSyntax::isKind(node.kind)) {
            emitStatement(node.as<StatementSyntax>(), out, comb, indent);
        }
        else if (node.kind == SyntaxKind::DataDeclaration) {
            emitDataDeclaration(node.as<DataDeclarationSyntax>(), out, indent);
        }
    }

    void emitStatement(const StatementSyntax& st, std::vector<std::string>& out, bool comb, int indent)
    {
        auto pre = std::string(indent * 4, ' ');
        if (st.kind == SyntaxKind::TimingControlStatement) {
            emitStatement(*st.as<TimingControlStatementSyntax>().statement, out, comb, indent);
        }
        else if (st.kind == SyntaxKind::SequentialBlockStatement || st.kind == SyntaxKind::ParallelBlockStatement) {
            auto& block = st.as<BlockStatementSyntax>();
            if (blockNeedsScope(block)) {
                out.push_back(pre + "{");
                for (auto item : block.items) {
                    emitNode(*item, out, comb, indent + 1);
                }
                out.push_back(pre + "}");
            }
            else {
                for (auto item : block.items) {
                    emitNode(*item, out, comb, indent);
                }
            }
        }
        else if (st.kind == SyntaxKind::ConditionalStatement) {
            auto& c = st.as<ConditionalStatementSyntax>();
            out.push_back(pre + "if (" + repairMalformedEquality(emitPredicate(*c.predicate)) + ") {");
            emitStatementBody(*c.statement, out, comb, indent + 1);
            out.push_back(pre + "}");
            if (c.elseClause) {
                out.push_back(pre + "else {");
                if (StatementSyntax::isKind(c.elseClause->clause->kind)) {
                    emitStatementBody(c.elseClause->clause->as<StatementSyntax>(), out, comb, indent + 1);
                }
                out.push_back(pre + "}");
            }
        }
        else if (st.kind == SyntaxKind::ForLoopStatement) {
            auto& f = st.as<ForLoopStatementSyntax>();
            auto savedLoopVars = loopVars;
            for (auto init : f.initializers) {
                if (init->kind == SyntaxKind::ForVariableDeclaration) {
                    loopVars.insert(tok(init->as<ForVariableDeclarationSyntax>().declarator->name));
                }
                else if (ExpressionSyntax::isKind(init->kind)) {
                    auto& e = init->as<ExpressionSyntax>();
                    if (BinaryExpressionSyntax::isKind(e.kind)) {
                        auto name = assignedBase(*e.as<BinaryExpressionSyntax>().left);
                        if (!name.empty()) {
                            loopVars.insert(name);
                        }
                    }
                    else {
                        auto name = assignedBase(e);
                        if (!name.empty()) {
                            loopVars.insert(name);
                        }
                    }
                }
            }
            for (auto step : f.steps) {
                std::string name;
                if (BinaryExpressionSyntax::isKind(step->kind)) {
                    name = assignedBase(*step->as<BinaryExpressionSyntax>().left);
                }
                else if (PostfixUnaryExpressionSyntax::isKind(step->kind)) {
                    name = assignedBase(*step->as<PostfixUnaryExpressionSyntax>().operand);
                }
                else if (PrefixUnaryExpressionSyntax::isKind(step->kind)) {
                    name = assignedBase(*step->as<PrefixUnaryExpressionSyntax>().operand);
                }
                else {
                    name = assignedBase(*step);
                }
                if (!name.empty()) {
                    loopVars.insert(name);
                }
            }
            out.push_back(pre + "for (" + emitForInit(f) + ";" + (f.stopExpr ? emitExpr(*f.stopExpr) : "") + ";" + emitExprList(f.steps) + ") {");
            emitStatementBody(*f.statement, out, comb, indent + 1);
            out.push_back(pre + "}");
            loopVars = savedLoopVars;
        }
        else if (st.kind == SyntaxKind::CaseStatement) {
            auto& c = st.as<CaseStatementSyntax>();
            if (tok(c.matchesOrInside) == "inside") {
                bool emittedAny = false;
                for (auto item : c.items) {
                    if (item->kind == SyntaxKind::StandardCaseItem) {
                        auto& sci = item->as<StandardCaseItemSyntax>();
                        std::string cond;
                        for (auto expr : sci.expressions) {
                            if (!cond.empty()) {
                                cond += " || ";
                            }
                            cond += emitInsideMember(emitNumericExpr(*c.expr), *expr);
                        }
                        out.push_back(pre + std::string(emittedAny ? "else if (" : "if (") + (cond.empty() ? "false" : cond) + ") {");
                        emitCaseClause(*sci.clause, out, comb, indent + 1);
                        out.push_back(pre + "}");
                        emittedAny = true;
                    }
                    else if (item->kind == SyntaxKind::DefaultCaseItem) {
                        auto& dci = item->as<DefaultCaseItemSyntax>();
                        out.push_back(pre + std::string(emittedAny ? "else " : "") + "{");
                        emitCaseClause(*dci.clause, out, comb, indent + 1);
                        out.push_back(pre + "}");
                        emittedAny = true;
                    }
                }
                return;
            }
            auto switchExpr = emitExpr(*c.expr);
            auto switchWidth = foldWidth(exprWidth(*c.expr));
            if (isNumber(switchWidth)) {
                auto width = std::stoul(switchWidth);
                if (width > 0 && width < 64) {
                    switchExpr = "(((uint64_t)(" + switchExpr + ")) & ((1ull << " + switchWidth + ") - 1ull))";
                }
                else {
                    switchExpr = "((uint64_t)(" + switchExpr + "))";
                }
            }
            else {
                switchExpr = "((uint64_t)(" + switchExpr + "))";
            }
            bool emittedCase = false;
            const DefaultCaseItemSyntax* defaultCase = nullptr;
            for (auto item : c.items) {
                if (item->kind == SyntaxKind::StandardCaseItem) {
                    auto& sci = item->as<StandardCaseItemSyntax>();
                    std::string cond;
                    for (auto expr : sci.expressions) {
                        if (!cond.empty()) {
                            cond += " || ";
                        }
                        cond += "(" + switchExpr + ") == (" + emitCaseLabelExpr(*expr) + ")";
                    }
                    out.push_back(pre + std::string(emittedCase ? "else if (" : "if (") + (cond.empty() ? "false" : cond) + ") {");
                    emitCaseClause(*sci.clause, out, comb, indent + 1);
                    out.push_back(pre + "}");
                    emittedCase = true;
                }
                else if (item->kind == SyntaxKind::DefaultCaseItem) {
                    defaultCase = &item->as<DefaultCaseItemSyntax>();
                }
            }
            if (defaultCase) {
                out.push_back(pre + std::string(emittedCase ? "else " : "") + "{");
                emitCaseClause(*defaultCase->clause, out, comb, indent + 1);
                out.push_back(pre + "}");
            }
        }
        else if (st.kind == SyntaxKind::ExpressionStatement) {
            auto& e = st.as<ExpressionStatementSyntax>();
            if (e.expr->kind == SyntaxKind::InvocationExpression &&
                e.expr->as<InvocationExpressionSyntax>().left->kind == SyntaxKind::SystemName) {
                out.push_back(pre + "// " + exprText(e.expr->toString()) + ";");
            }
            else {
                out.push_back(pre + emitStatementExpr(*e.expr, comb) + ";");
            }
        }
        else if (st.kind == SyntaxKind::ReturnStatement) {
            auto& ret = st.as<ReturnStatementSyntax>();
            out.push_back(pre + std::string("return") + (ret.returnValue ? " " + emitExpr(*ret.returnValue) : "") + ";");
        }
    }

    void emitCaseClause(const SyntaxNode& clause, std::vector<std::string>& out, bool comb, int indent)
    {
        if (StatementSyntax::isKind(clause.kind)) {
            emitStatementBody(clause.as<StatementSyntax>(), out, comb, indent);
        }
        else if (clause.kind == SyntaxKind::DataDeclaration) {
            emitDataDeclaration(clause.as<DataDeclarationSyntax>(), out, indent);
        }
    }

    bool blockNeedsScope(const BlockStatementSyntax& block)
    {
        for (auto item : block.items) {
            if (item->kind == SyntaxKind::DataDeclaration) {
                return true;
            }
            if (StatementSyntax::isKind(item->kind)) {
                auto& st = item->as<StatementSyntax>();
                if ((st.kind == SyntaxKind::SequentialBlockStatement || st.kind == SyntaxKind::ParallelBlockStatement) &&
                    blockNeedsScope(st.as<BlockStatementSyntax>())) {
                    return true;
                }
            }
        }
        return false;
    }

    void emitStatementBody(const StatementSyntax& st, std::vector<std::string>& out, bool comb, int indent)
    {
        if (st.kind == SyntaxKind::SequentialBlockStatement || st.kind == SyntaxKind::ParallelBlockStatement) {
            localTypeScopes.push_back({});
            for (auto item : st.as<BlockStatementSyntax>().items) {
                emitNode(*item, out, comb, indent);
            }
            localTypeScopes.pop_back();
        }
        else {
            emitStatement(st, out, comb, indent);
        }
    }

    void emitDataDeclaration(const DataDeclarationSyntax& node, std::vector<std::string>& out, int indent)
    {
        auto pre = std::string(indent * 4, ' ');
        for (auto d : node.declarators) {
            auto name = tok(d->name);
            auto type = loopVars.count(name) ? "unsigned" : typeText(*node.type);
            if (type == "bool") {
                type = "u1";
            }
            if (!localTypeScopes.empty()) {
                localTypeScopes.back()[name] = type;
            }
            auto line = pre + type + " " + name;
            if (d->initializer) {
                auto& init = *d->initializer;
                if (init.expr) {
                    auto rhs = emitExpr(*init.expr);
                    auto ctype = constexprType(type);
                    if (type == "bool") {
                        rhs = "static_cast<bool>(" + rhs + ")";
                    }
                    else if (ctype == "unsigned" || ctype == "uint64_t") {
                        rhs = "static_cast<" + ctype + ">(" + emitNumericExpr(*init.expr, rhs) + ")";
                    }
                    else if (type.find("::") != std::string::npos && !foldWidth(exprWidth(*init.expr)).empty()) {
                        rhs = "static_cast<" + type + ">(" + emitNumericExpr(*init.expr, rhs) + ")";
                    }
                    line += " = " + rhs;
                }
                else {
                    auto s = init.toString();
                    s = trim(s);
                    if (!s.empty() && s.front() == '=') {
                        s.erase(s.begin());
                    }
                    s = trim(s);
                    auto rhs = translateExpr(s);
                    if (type == "bool") {
                        rhs = "static_cast<bool>(" + rhs + ")";
                    }
                    line += " = " + rhs;
                }
            }
            out.push_back(line + ";");
        }
    }

    std::string emitForInit(const ForLoopStatementSyntax& f)
    {
        std::string s;
        for (auto init : f.initializers) {
            if (!s.empty()) {
                s += ",";
            }
            if (init->kind == SyntaxKind::ForVariableDeclaration) {
                auto& d = init->as<ForVariableDeclarationSyntax>();
                auto name = tok(d.declarator->name);
                s += (d.type ? (loopVars.count(name) ? "unsigned" : typeText(*d.type)) : "auto") + " " + name;
                if (d.declarator->initializer) {
                    auto t = d.declarator->initializer->toString();
                    t = trim(t);
                    if (!t.empty() && t.front() == '=') {
                        t.erase(t.begin());
                    }
                    t = trim(t);
                    s += " = " + translateExpr(t);
                }
            }
            else if (ExpressionSyntax::isKind(init->kind)) {
                s += emitExpr(init->as<ExpressionSyntax>());
            }
            else {
                s += exprText(init->toString());
            }
        }
        return s;
    }

    template<typename T>
    std::string emitExprList(const T& list)
    {
        std::string s;
        for (auto e : list) {
            if (!s.empty()) {
                s += ",";
            }
            s += emitExpr(*e);
        }
        return s;
    }

    std::string emitArgument(const ArgumentSyntax& arg, bool numericArg = false)
    {
        if (arg.kind == SyntaxKind::OrderedArgument) {
            auto& ordered = arg.as<OrderedArgumentSyntax>();
            if (ordered.expr && ExpressionSyntax::isKind(ordered.expr->kind)) {
                auto& expr = ordered.expr->as<ExpressionSyntax>();
                auto emitted = emitExpr(expr);
                return numericArg ? emitNumericExpr(expr, emitted) : emitted;
            }
            if (ordered.expr->kind == SyntaxKind::SimplePropertyExpr) {
                auto& prop = ordered.expr->as<SimplePropertyExprSyntax>();
                if (prop.expr->kind == SyntaxKind::SimpleSequenceExpr) {
                    auto& expr = *prop.expr->as<SimpleSequenceExprSyntax>().expr;
                    auto emitted = emitExpr(expr);
                    return numericArg ? emitNumericExpr(expr, emitted) : emitted;
                }
            }
            return cppExprText(ordered.expr->toString());
        }
        return cppExprText(arg.toString());
    }

    std::string emitArgumentList(const ArgumentListSyntax& args, bool numericArgs = false, const std::string& callee = "")
    {
        std::string s = "(";
        size_t index = 0;
        for (auto arg : args.parameters) {
            if (s.size() > 1) {
                s += ", ";
            }
            s += emitArgument(*arg, wantsNumericFunctionArg(callee, index, numericArgs));
            ++index;
        }
        s += ")";
        return s;
    }

    std::string emitPredicate(const ConditionalPredicateSyntax& p)
    {
        std::string s;
        for (auto c : p.conditions) {
            if (!s.empty()) {
                s += " && ";
            }
            s += repairMalformedEquality(emitExpr(*c->expr));
        }
        return s;
    }

    bool isBlockingAssignmentKind(SyntaxKind kind) const
    {
        return kind == SyntaxKind::AssignmentExpression;
    }

    bool isNonblockingAssignmentKind(SyntaxKind kind) const
    {
        return kind == SyntaxKind::NonblockingAssignmentExpression;
    }

    bool isCompoundAssignmentKind(SyntaxKind kind) const
    {
        return kind == SyntaxKind::AddAssignmentExpression ||
               kind == SyntaxKind::SubtractAssignmentExpression ||
               kind == SyntaxKind::AndAssignmentExpression ||
               kind == SyntaxKind::OrAssignmentExpression ||
               kind == SyntaxKind::XorAssignmentExpression ||
               kind == SyntaxKind::LogicalLeftShiftAssignmentExpression ||
               kind == SyntaxKind::LogicalRightShiftAssignmentExpression ||
               kind == SyntaxKind::ArithmeticLeftShiftAssignmentExpression ||
               kind == SyntaxKind::ArithmeticRightShiftAssignmentExpression ||
               kind == SyntaxKind::MultiplyAssignmentExpression ||
               kind == SyntaxKind::DivideAssignmentExpression ||
               kind == SyntaxKind::ModAssignmentExpression;
    }

    bool isAssignmentLikeKind(SyntaxKind kind) const
    {
        return isBlockingAssignmentKind(kind) || isNonblockingAssignmentKind(kind) || isCompoundAssignmentKind(kind);
    }

    std::string compoundOperatorForKind(SyntaxKind kind, const std::string& tokenText) const
    {
        if (!tokenText.empty()) {
            return tokenText;
        }
        switch (kind) {
            case SyntaxKind::AddAssignmentExpression: return "+=";
            case SyntaxKind::SubtractAssignmentExpression: return "-=";
            case SyntaxKind::AndAssignmentExpression: return "&=";
            case SyntaxKind::OrAssignmentExpression: return "|=";
            case SyntaxKind::XorAssignmentExpression: return "^=";
            case SyntaxKind::LogicalLeftShiftAssignmentExpression:
            case SyntaxKind::ArithmeticLeftShiftAssignmentExpression: return "<<=";
            case SyntaxKind::LogicalRightShiftAssignmentExpression:
            case SyntaxKind::ArithmeticRightShiftAssignmentExpression: return ">>=";
            case SyntaxKind::MultiplyAssignmentExpression: return "*=";
            case SyntaxKind::DivideAssignmentExpression: return "/=";
            case SyntaxKind::ModAssignmentExpression: return "%=";
            default: return tokenText;
        }
    }

    std::string emitCompoundAssignment(const BinaryExpressionSyntax& b, const std::string& op)
    {
        auto lhs = emitLValue(*b.left);
        auto rhs = emitExpr(*b.right);
        auto base = assignedBase(*b.left);
        if (!base.empty() && !mod->types.count(base) && (op == "&=" || op == "|=" || op == "^=")) {
            auto boolOp = op == "&=" ? "&&" : (op == "|=" ? "||" : "!=");
            if (op == "^=") {
                return lhs + " = ((bool)(" + lhs + ") != " + truthyExpr(rhs, exprWidth(*b.right)) + ")";
            }
            return lhs + " = ((bool)(" + lhs + ") " + boolOp + " " + truthyExpr(rhs, exprWidth(*b.right)) + ")";
        }
        if ((op == "&=" || op == "|=" || op == "^=") &&
            b.right->kind == SyntaxKind::ConditionalExpression) {
            rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
        }
        if (op == "<<=" || op == ">>=") {
            auto shiftOp = op == "<<=" ? "<<" : ">>";
            auto lhsWidth = foldWidth(exprWidth(*b.left));
            if (isNumber(lhsWidth)) {
                return lhs + " = logic<" + lhsWidth + ">(" + emitNumericExpr(*b.left) + " " + shiftOp + " (unsigned)(" + emitNumericExpr(*b.right, rhs) + "))";
            }
            return lhs + " = " + lhs + " " + shiftOp + " (unsigned)(" + emitNumericExpr(*b.right, rhs) + ")";
        }
        auto lhsWidth = foldWidth(exprWidth(*b.left));
        if (op == "+=" || op == "-=" || op == "|=" || op == "&=" || op == "^=") {
            std::string binop = op.substr(0, 1);
            if (lhsWidth == "1" || lhs.find("logic<1>") != std::string::npos) {
                if (binop == "|") {
                    return lhs + " = logic<1>(" + truthyExpr(lhs, "1") + " || " + truthyExpr(rhs, exprWidth(*b.right)) + ")";
                }
                if (binop == "&") {
                    return lhs + " = logic<1>(" + truthyExpr(lhs, "1") + " && " + truthyExpr(rhs, exprWidth(*b.right)) + ")";
                }
                if (binop == "^") {
                    return lhs + " = logic<1>((uint64_t)(" + lhs + ") ^ (" + emitNumericExpr(*b.right, rhs) + " & 1ull))";
                }
            }
            if (!lhsWidth.empty()) {
                return lhs + " = logic<" + lhsWidth + ">(" + emitNumericExpr(*b.left) + " " + binop + " " + emitNumericExpr(*b.right, rhs) + ")";
            }
        }
        return lhs + " " + op + " " + rhs;
    }

    std::string emitStatementExpr(const ExpressionSyntax& expr, bool comb)
    {
        if (isCompoundAssignmentKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            return emitCompoundAssignment(b, compoundOperatorForKind(expr.kind, tok(b.operatorToken)));
        }
        if (isBlockingAssignmentKind(expr.kind) || isNonblockingAssignmentKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto base = assignedBase(*b.left);
            auto lhs = emitLValue(*b.left);
            auto rhs = emitExpr(*b.right);
            bool rhsConditionalSized = false;
            if (b.right->kind == SyntaxKind::ConditionalExpression &&
                b.left->kind == SyntaxKind::ElementSelectExpression) {
                auto& selExpr = b.left->as<ElementSelectExpressionSyntax>();
                if (selExpr.select && selExpr.select->selector && RangeSelectSyntax::isKind(selExpr.select->selector->kind)) {
                    auto width = foldWidth(selectWidth(*selExpr.select));
                    if (!width.empty()) {
                        rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<" + width + ">");
                        rhsConditionalSized = true;
                    }
                }
                else if (selExpr.select && selExpr.select->selector &&
                    selExpr.select->selector->kind == SyntaxKind::BitSelect) {
                    rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<1>");
                    rhsConditionalSized = true;
                }
            }
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression) {
                auto lhsWidth = foldWidth(exprWidth(*b.left));
                if (lhsWidth == "1") {
                    rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<1>");
                    rhsConditionalSized = true;
                }
            }
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
            }
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression && mod->types.count(base) &&
                mod->types[base] != "bool" && mod->types[base] != "u1" && mod->types[base] != "reg<u1>") {
                auto& c = b.right->as<ConditionalExpressionSyntax>();
                auto targetWidth = foldWidth(typeWidth(mod->types[base]));
                if (isNumber(targetWidth)) {
                    auto prim = primitiveForWidth(targetWidth);
                    rhs = emitPredicate(*c.predicate) + " ? " + primitiveCast(prim, emitNumericExpr(*c.left)) + " : " + primitiveCast(prim, emitNumericExpr(*c.right));
                }
                else {
                    rhs = emitPredicate(*c.predicate) + " ? " + emitExpr(*c.left) + " : " + emitExpr(*c.right);
                }
            }
            if (mod->types.count(base) &&
                (isNonblockingAssignmentKind(expr.kind) ||
                 mod->types[base].rfind("reg<", 0) == 0 || mod->outputPortCppNames.count(base))) {
                if (comb && !isNonblockingAssignmentKind(expr.kind)) {
                    mod->combAssignedVars.insert(base);
                }
                else {
                    mod->seqAssignedVars.insert(base);
                }
            }
            if (comb && mod->outputPortCppNames.count(base) && isWholeObjectSelect(*b.left, base)) {
                lhs = combStorageName(*mod, base);
            }
            if (mod->types.count(base) && (mod->types[base] == "bool" || mod->types[base] == "u1" || mod->types[base] == "reg<u1>")) {
                rhs = truthyExpr(rhs, exprWidth(*b.right));
            }
            auto sequentialStorageType = mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
            if (mod->outputPortCppNames.count(base)) {
                sequentialStorageType = outputStorageType(*mod, base, mod->outputPortCppNames[base]);
            }
            if (isNonblockingAssignmentKind(expr.kind) && mod->types.count(base)) {
                sequentialStorageType = regTypeFor(sequentialStorageType);
            }
            auto targetStorageType = unwrapRegType(sequentialStorageType);
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression && targetStorageType != "bool" &&
                targetStorageType != "u1" && targetStorageType != "reg<u1>") {
                auto& c = b.right->as<ConditionalExpressionSyntax>();
                auto targetWidth = foldWidth(typeWidth(targetStorageType));
                if (!isNumber(targetWidth)) {
                    auto leftWidth = foldWidth(exprWidth(*c.left));
                    auto rightWidth = foldWidth(exprWidth(*c.right));
                    if (isNumber(leftWidth) && isNumber(rightWidth)) {
                        targetWidth = std::to_string(std::max(std::stoul(leftWidth), std::stoul(rightWidth)));
                    }
                }
                if ((targetStorageType.rfind("logic<", 0) == 0 || targetStorageType.rfind("u<", 0) == 0 ||
                    (targetStorageType.find("::") != std::string::npos && targetStorageType.rfind("array<", 0) != 0))) {
                    rhs = emitConditionalAsType(c, targetStorageType);
                }
                else if (isNumber(targetWidth) && targetWidth != "1") {
                    auto prim = primitiveForWidth(targetWidth);
                    rhs = emitPredicate(*c.predicate) + " ? " + primitiveCast(prim, emitNumericExpr(*c.left)) + " : " + primitiveCast(prim, emitNumericExpr(*c.right));
                }
            }
            if (isPrimitiveWrapperType(targetStorageType)) {
                auto targetWidth = foldWidth(typeWidth(targetStorageType));
                if (isNumber(targetWidth)) {
                    rhs = primitiveCast(primitiveForWidth(targetWidth), emitNumericExpr(*b.right, rhs));
                }
            }
            auto trimmedRhs = trim(rhs);
            if (!targetStorageType.empty() && trimmedRhs.rfind("{.", 0) == 0) {
                rhs = targetStorageType + trimmedRhs;
            }
            if ((isNonblockingAssignmentKind(expr.kind) || (!comb && mod->varNames.count(base))) &&
                sequentialStorageType.rfind("reg<", 0) == 0 &&
                !memoryLikeType(sequentialStorageType)) {
                if (b.left->kind == SyntaxKind::ElementSelectExpression || lhs.find('[') != std::string::npos ||
                    lhs.find(".bits(") != std::string::npos) {
                    lhs.replace(0, base.size(), base + "._next");
                }
                else {
                    lhs += "._next";
                }
            }
            return lhs + " = " + rhs;
        }
        return emitExpr(expr);
    }

    std::string constantType(const std::string& name) const
    {
        if (!mod) {
            return "";
        }
        for (auto& c : mod->constants) {
            auto text = trim(c.second);
            auto eq = text.find('=');
            auto cname = trim(eq == std::string::npos ? text : text.substr(0, eq));
            if (cname == name) {
                return c.first;
            }
        }
        return "";
    }

    std::string emitLValue(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod->outputPortCppNames.count(name)) {
                if (isAssignOnlyOutput(*mod, name)) {
                    return mod->outputPortCppNames[name];
                }
                return outputStorageName(*mod, name);
            }
            if (mod->portCppNames.count(name)) {
                return mod->portCppNames[name];
            }
            return name;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto base = tok(n.identifier);
            auto s = mod->outputPortCppNames.count(base) ? outputStorageName(*mod, base) :
                (mod->portCppNames.count(base) ? mod->portCppNames[base] : base);
            auto key = base;
            auto memorySelect = mod->types.count(base) && memoryLikeType(mod->types[base]);
            auto memoryScalar = memorySelect && scalarMemory(mod->types[base]);
            for (auto sel : n.selectors) {
                s = emitSelectOn(s, *sel, true, memorySelect, memoryScalar);
                key = emitSelectOn(key, *sel, true);
                memorySelect = false;
                memoryScalar = false;
            }
            return s;
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            if (e.left->kind == SyntaxKind::IdentifierName) {
                auto baseName = tok(e.left->as<IdentifierNameSyntax>().identifier);
                if (e.select && e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind) &&
                    (loopVars.count(baseName) || (mod && mod->types.count(baseName) &&
                     (mod->types[baseName] == "unsigned" || mod->types[baseName] == "u32" ||
                      mod->types[baseName] == "uint32_t" || mod->types[baseName] == "u64" ||
                      mod->types[baseName] == "uint64_t")))) {
                    auto& r = e.select->selector->as<RangeSelectSyntax>();
                    auto width = foldWidth(selectWidth(*e.select));
                    if (width.empty()) {
                        width = "64";
                    }
                    auto first = emitNumericExpr(*r.right);
                    auto value = "((" + emitNumericExpr(*e.left) + ") >> (unsigned)(" + first + "))";
                    if (isNumber(width) && std::stoul(width) < 64) {
                        value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
                    }
                    return "logic<" + width + ">(" + value + ")";
                }
                auto ctype = constantType(baseName);
                auto width = foldWidth(typeWidth(ctype));
                if (!width.empty() && ctype.rfind("logic<", 0) == 0) {
                    return emitSelectOn("logic<" + width + ">(" + baseName + ")", *e.select, false);
                }
            }
            auto base = assignedBase(*e.left);
            if (mod->types.count(base) && memoryLikeType(mod->types[base]) && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitMemoryRowAccess(base, emitLValue(*e.left), *e.select->selector->as<BitSelectSyntax>().expr);
            }
            return emitSelectOn(emitLValue(*e.left), *e.select, true);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitLValue(*e.left) + "." + cppIdent(tok(e.name));
        }
        return replaceKeywordMemberAccess(replaceRawRangeSelects(exprText(expr.toString())));
    }

    std::string emitUntypedNumericExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return "(" + emitUntypedNumericExpr(*expr.as<ParenthesizedExpressionSyntax>().expression) + ")";
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto op = tok(b.operatorToken);
            if (op == "<<<") {
                op = "<<";
            }
            else if (op == ">>>") {
                op = ">>";
            }
            if (op == "**") {
                auto left = emitUntypedNumericExpr(*b.left);
                auto right = emitUntypedNumericExpr(*b.right);
                auto simpleLeft = stripParens(left);
                auto rawLeft = trim(exprText(b.left->toString()));
                if (simpleLeft == "2" || simpleLeft == "2u" || simpleLeft == "2ull" || rawLeft == "2") {
                    return "(1ull << (unsigned)(" + right + "))";
                }
                return "pow(" + left + ", " + right + ")";
            }
            auto left = emitNumericExpr(*b.left);
            auto rhs = emitNumericExpr(*b.right);
            if (op == "<<" || op == ">>") {
                rhs = "(unsigned)(" + rhs + ")";
            }
            return left + " " + op + " " + rhs;
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            auto op = tok(u.operatorToken);
            if (op == "|" || op == "~|" || op == "&" || op == "~&" || op == "^" || op == "~^" || op == "^~") {
                return emitExpr(expr);
            }
            auto operand = emitNumericExpr(*u.operand);
            if (op == "+" || op == "-") {
                return op + "(" + operand + ")";
            }
            return op + emitUntypedNumericExpr(*u.operand);
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            return emitPredicate(*c.predicate) + " ? " + emitUntypedNumericExpr(*c.left) + " : " + emitUntypedNumericExpr(*c.right);
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto callee = exprText(i.left->toString());
            if (auto qualifiedCalls = configuredTextMap("HDLCPP_QUALIFIED_CALLS"); qualifiedCalls.count(callee)) {
                callee = qualifiedCalls[callee];
            }
            if (callee == "$bits") {
                return emitExpr(expr);
            }
            std::string args = "(";
            if (i.arguments) {
                bool first = true;
                for (auto arg : i.arguments->parameters) {
                    if (!first) {
                        args += ", ";
                    }
                    first = false;
                    if (arg->kind == SyntaxKind::OrderedArgument) {
                        auto exprNode = arg->as<OrderedArgumentSyntax>().expr;
                        if (exprNode && ExpressionSyntax::isKind(exprNode->kind)) {
                            args += emitUntypedNumericExpr(exprNode->as<ExpressionSyntax>());
                        }
                        else {
                            args += exprNode ? exprText(exprNode->toString()) : std::string();
                        }
                    }
                    else {
                        args += exprText(arg->toString());
                    }
                }
            }
            args += ")";
            return callee + args;
        }
        auto emitted = emitExpr(expr);
        auto width = foldWidth(exprWidth(expr));
        if (!width.empty()) {
            return emitNumericExpr(expr, emitted);
        }
        return emitted;
    }

    std::string emitIndexExpr(const ExpressionSyntax& expr)
    {
        return "(uint64_t)(" + emitUntypedNumericExpr(expr) + ")";
    }

    std::string emitMemoryRowAccess(const std::string& memoryName, const std::string& baseExpr, const ExpressionSyntax& indexExpr)
    {
        return baseExpr + "[(unsigned)(" + emitIndexExpr(indexExpr) + ")]";
    }

    bool primitiveCastableExpr(const ExpressionSyntax& expr)
    {
        auto w = foldWidth(exprWidth(expr));
        if (!isNumber(w)) {
            return false;
        }
        auto text = trim(expr.toString());
        if (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '\'')) {
            return true;
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            return primitiveCastableExpr(*c.left) && primitiveCastableExpr(*c.right);
        }
        if (BinaryExpressionSyntax::isKind(expr.kind) ||
            PrefixUnaryExpressionSyntax::isKind(expr.kind) ||
            expr.kind == SyntaxKind::ConcatenationExpression ||
            expr.kind == SyntaxKind::MultipleConcatenationExpression ||
            expr.kind == SyntaxKind::InvocationExpression) {
            return true;
        }
        auto base = assignedBase(expr);
        if (mod && !base.empty() && mod->types.count(base)) {
            auto type = mod->types[base];
            while (type.rfind("array<", 0) == 0) {
                auto args = memoryArgs("memory<" + type.substr(6, type.size() - 7) + ">");
                if (args.size() != 2) {
                    break;
                }
                type = args[0];
            }
            return !typeWidth(type).empty();
        }
        return false;
    }

    bool targetCastableExpr(const ExpressionSyntax& expr)
    {
        auto w = foldWidth(exprWidth(expr));
        if (w.empty()) {
            return false;
        }
        auto text = trim(expr.toString());
        if (!text.empty() && (std::isdigit(static_cast<unsigned char>(text[0])) || text[0] == '\'')) {
            return true;
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            return targetCastableExpr(*c.left) && targetCastableExpr(*c.right);
        }
        if (BinaryExpressionSyntax::isKind(expr.kind) ||
            PrefixUnaryExpressionSyntax::isKind(expr.kind) ||
            expr.kind == SyntaxKind::ConcatenationExpression ||
            expr.kind == SyntaxKind::MultipleConcatenationExpression ||
            expr.kind == SyntaxKind::InvocationExpression) {
            return true;
        }
        auto base = assignedBase(expr);
        if (mod && !base.empty() && mod->types.count(base)) {
            auto type = mod->types[base];
            while (type.rfind("array<", 0) == 0) {
                auto args = memoryArgs("memory<" + type.substr(6, type.size() - 7) + ">");
                if (args.size() != 2) {
                    break;
                }
                type = args[0];
            }
            return !typeWidth(type).empty();
        }
        return false;
    }

    bool isZeroLiteralExpr(const ExpressionSyntax& expr)
    {
        auto text = trim(exprText(expr.toString()));
        return text == "0" || text == "0b0" || text == "0x0";
    }

    std::string emitConditionalForLValue(const ConditionalExpressionSyntax& c, const ExpressionSyntax& lhsExpr, const std::string& lhs)
    {
        if (lhs.find("][") != std::string::npos) {
            return emitConditionalAsType(c, "logic<1>");
        }
        auto base = assignedBase(lhsExpr);
        if (base.empty() && lhs.find('[') != std::string::npos) {
            base = trim(lhs.substr(0, lhs.find('[')));
        }
        if (!base.empty() && mod && mod->types.count(base) && lhs.find('[') != std::string::npos) {
            auto baseType = unwrapRegType(mod->types[base]);
            if (baseType.rfind("logic<", 0) == 0 || baseType.rfind("u<", 0) == 0) {
                return emitConditionalAsType(c, "logic<1>");
            }
        }
        auto width = foldWidth(exprWidth(lhsExpr));
        if (isNumber(width)) {
            return emitConditionalAsType(c, "logic<" + width + ">");
        }
        return emitConditionalAsDecltype(c, lhs);
    }

    std::string emitConditionalAsDecltype(const ConditionalExpressionSyntax& c, const std::string& lhs)
    {
        auto targetType = "std::remove_cvref_t<decltype(" + lhs + ")>";
        auto emitBranch = [&](const ExpressionSyntax& branch) -> std::string {
            auto expr = &branch;
            while (expr->kind == SyntaxKind::ParenthesizedExpression) {
                expr = expr->as<ParenthesizedExpressionSyntax>().expression;
            }
            if (expr->kind == SyntaxKind::ConditionalExpression) {
                return "(" + emitConditionalAsDecltype(expr->as<ConditionalExpressionSyntax>(), lhs) + ")";
            }
            if (isZeroLiteralExpr(*expr)) {
                return targetType + "{}";
            }
            return targetType + "(" + emitExpr(*expr) + ")";
        };
        return emitPredicate(*c.predicate) + " ? " + emitBranch(*c.left) + " : " + emitBranch(*c.right);
    }

    std::string emitConditionalAsType(const ConditionalExpressionSyntax& c, const std::string& targetType)
    {
        auto emitBranch = [&](const ExpressionSyntax& branch) -> std::string {
            auto expr = &branch;
            while (expr->kind == SyntaxKind::ParenthesizedExpression) {
                expr = expr->as<ParenthesizedExpressionSyntax>().expression;
            }
            if (expr->kind == SyntaxKind::ConditionalExpression) {
                return "(" + emitConditionalAsType(expr->as<ConditionalExpressionSyntax>(), targetType) + ")";
            }
            return targetType + "(" + emitNumericExpr(*expr) + ")";
        };
        return emitPredicate(*c.predicate) + " ? " + emitBranch(*c.left) + " : " + emitBranch(*c.right);
    }

    std::string emitNumericIdentifierSelectExpr(const IdentifierSelectNameSyntax& n)
    {
        if (n.selectors.empty()) {
            return "";
        }
        auto last = n.selectors.back();
        if (!last || !last->selector) {
            return "";
        }
        auto base = tok(n.identifier);
        auto s = mod->outputPortCppNames.count(base) ?
            (isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : outputStorageName(*mod, base)) :
            (mod->portCppNames.count(base) ? mod->portCppNames[base] + "()" : (isAssignDrivenVar(*mod, base) ? base + "()" : base));
        auto currentType = mod->types.count(base) ? mod->types[base] : std::string();
        auto memorySelect = !currentType.empty() && memoryLikeType(currentType);
        auto memoryScalar = memorySelect && scalarMemory(currentType);
        for (size_t idx = 0; idx + 1 < n.selectors.size(); ++idx) {
            auto sel = n.selectors[idx];
            s = emitSelectOn(s, *sel, false, memorySelect, memoryScalar);
            if (currentType.rfind("array<", 0) == 0 && sel->selector && sel->selector->kind == SyntaxKind::BitSelect) {
                auto args = memoryArgs("memory<" + currentType.substr(6, currentType.size() - 7) + ">");
                currentType = args.size() == 2 ? args[0] : std::string();
            }
            else {
                currentType.clear();
            }
            memorySelect = !currentType.empty() && memoryLikeType(currentType);
            memoryScalar = memorySelect && scalarMemory(currentType);
        }
        if (last->selector->kind == SyntaxKind::BitSelect) {
            auto index = emitNumericExpr(*last->selector->as<BitSelectSyntax>().expr);
            if (loopVars.count(base) || currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" || currentType == "u64" || currentType == "uint64_t") {
                return "(((uint64_t)(" + s + ") >> (unsigned)(" + index + ")) & 1ull)";
            }
            return "(uint64_t)(logic<1>(" + s + "[" + bitIndexArg(index) + "]))";
        }
        if (RangeSelectSyntax::isKind(last->selector->kind)) {
            auto& r = last->selector->as<RangeSelectSyntax>();
            auto bounds = indexedRangeBounds(r);
            if (loopVars.count(base) || currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" || currentType == "u64" || currentType == "uint64_t") {
                auto value = "(((uint64_t)(" + s + ")) >> (unsigned)(" + bounds.second + "))";
                auto width = foldWidth(selectWidth(*last));
                if (isNumber(width) && std::stoul(width) < 64) {
                    value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
                }
                return value;
            }
            return "(uint64_t)(" + s + ".bits(" + bounds.first + "," + bounds.second + "))";
        }
        return "";
    }

    std::string emitNumericRangeSelectExpr(const ElementSelectExpressionSyntax& e)
    {
        if (!e.select || !e.select->selector || !RangeSelectSyntax::isKind(e.select->selector->kind)) {
            return "";
        }
        auto& r = e.select->selector->as<RangeSelectSyntax>();
        auto base = assignedBase(*e.left);
        bool integralBase = false;
        if (!base.empty()) {
            integralBase = loopVars.count(base) || (mod && mod->types.count(base) &&
                (mod->types[base] == "bool" || mod->types[base] == "unsigned" || mod->types[base] == "u32" ||
                 mod->types[base] == "uint32_t" || mod->types[base] == "u64" ||
                 mod->types[base] == "uint64_t"));
            if (!integralBase && !constantType(base).empty()) {
                integralBase = true;
            }
        }
        auto bounds = indexedRangeBounds(r);
        if (integralBase) {
            auto value = "(((uint64_t)(" + emitNumericExpr(*e.left) + ")) >> (unsigned)(" + bounds.second + "))";
            auto width = foldWidth(selectWidth(*e.select));
            if (isNumber(width) && std::stoul(width) < 64) {
                value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
            }
            return value;
        }
        return "(uint64_t)(" + emitExpr(*e.left) + ".bits(" + bounds.first + "," + bounds.second + "))";
    }

    std::string emitNumericExpr(const ExpressionSyntax& expr, const std::string& emitted = "")
    {
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto numericSelect = emitNumericIdentifierSelectExpr(expr.as<IdentifierSelectNameSyntax>());
            if (!numericSelect.empty()) {
                return numericSelect;
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto numericRange = emitNumericRangeSelectExpr(expr.as<ElementSelectExpressionSyntax>());
            if (!numericRange.empty()) {
                return numericRange;
            }
        }
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            auto& p = expr.as<ParenthesizedExpressionSyntax>();
            if (p.expression) {
                return "(" + emitNumericExpr(*p.expression) + ")";
            }
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            auto width = foldWidth(exprWidth(expr));
            auto prim = primitiveForWidth(width);
            return "(" + emitPredicate(*c.predicate) + " ? " + primitiveCast(prim, emitNumericExpr(*c.left)) +
                   " : " + primitiveCast(prim, emitNumericExpr(*c.right)) + ")";
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto op = tok(b.operatorToken);
            if (op == "<<<") {
                op = "<<";
            }
            else if (op == ">>>") {
                op = ">>";
            }
            if (op == "|" || op == "&" || op == "^") {
                return "(" + emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right) + ")";
            }
            if (op == "<<" || op == ">>") {
                return "(" + emitNumericExpr(*b.left) + " " + op + " (unsigned)(" + emitNumericExpr(*b.right) + "))";
            }
        }
        auto text = emitted.empty() ? emitExpr(expr) : emitted;
        auto width = foldWidth(exprWidth(expr));
        if (width == "1") {
            auto simple = trim(text);
            while (simple.size() > 2 && simple.front() == '(' && simple.back() == ')') {
                simple = trim(simple.substr(1, simple.size() - 2));
            }
            if (mod && mod->types.count(simple)) {
                auto knownWidth = resolvedTypeWidth(mod->types[simple]);
                if (!knownWidth.empty()) {
                    width = foldWidth(knownWidth);
                }
            }
        }
        if (width == "1") {
            auto literalWidth = numericLiteralWidth(text);
            if (!literalWidth.empty()) {
                width = literalWidth;
            }
        }
        if (width == "1") {
            bool knownOneBit = false;
            if (expr.kind == SyntaxKind::InsideExpression) {
                knownOneBit = true;
            }
            if (BinaryExpressionSyntax::isKind(expr.kind)) {
                auto op = tok(expr.as<BinaryExpressionSyntax>().operatorToken);
                knownOneBit = (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
                               op == "&&" || op == "||");
            }
            if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
                auto op = tok(expr.as<PrefixUnaryExpressionSyntax>().operatorToken);
                knownOneBit = (op == "!" || op == "&" || op == "|" || op == "^" || op == "~&" ||
                               op == "~|" || op == "~^" || op == "^~");
            }
            if (expr.kind == SyntaxKind::ElementSelectExpression) {
                auto& e = expr.as<ElementSelectExpressionSyntax>();
                knownOneBit = e.select && e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect;
            }
            auto simple = trim(text);
            while (simple.size() > 2 && simple.front() == '(' && simple.back() == ')') {
                simple = trim(simple.substr(1, simple.size() - 2));
            }
            if (simple.find(".bits(") != std::string::npos) {
                return "(uint64_t)(" + text + ")";
            }
            if (!knownOneBit && simple.rfind("logic<1>", 0) != 0) {
                return "(uint64_t)(" + text + ")";
            }
            bool simpleName = !simple.empty();
            for (char c : simple) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) {
                    simpleName = false;
                    break;
                }
            }
            if (simpleName && numericLiteralWidth(simple).empty()) {
                return "(uint64_t)(" + text + ")";
            }
        }
        if (isNumber(width)) {
            auto w = std::stoul(width);
            if (w > 0 && w < 64) {
                return "((uint64_t)(" + text + ") & ((1ull << " + width + ") - 1ull))";
            }
        }
        return "(uint64_t)(" + text + ")";
    }

    std::string emitInsideMember(const std::string& lhs, const ExpressionSyntax& member)
    {
        auto insideValue = [&](const ExpressionSyntax& expr) {
            return "(uint64_t)(" + emitExpr(expr) + ")";
        };
        if (member.kind == SyntaxKind::ValueRangeExpression) {
            auto& range = member.as<ValueRangeExpressionSyntax>();
            auto lo = insideValue(*range.left);
            auto hi = insideValue(*range.right);
            return "(((" + lo + ") <= (" + hi + ")) ? ((" + lhs + ") >= (" + lo + ") && (" + lhs + ") <= (" + hi + ")) : ((" +
                   lhs + ") >= (" + hi + ") && (" + lhs + ") <= (" + lo + ")))";
        }
        return "((" + lhs + ") == (" + insideValue(member) + "))";
    }

    std::string emitInsideList(const ExpressionSyntax& lhsExpr, const SeparatedSyntaxList<ExpressionSyntax>& members)
    {
        auto lhs = emitNumericExpr(lhsExpr);
        std::string out;
        for (auto member : members) {
            if (!out.empty()) {
                out += " || ";
            }
            out += emitInsideMember(lhs, *member);
        }
        return out.empty() ? "false" : "(" + out + ")";
    }

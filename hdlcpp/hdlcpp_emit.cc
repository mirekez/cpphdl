    std::string emitExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::InsideExpression) {
            auto& inside = expr.as<InsideExpressionSyntax>();
            return emitInsideList(*inside.expr, inside.ranges->valueRanges);
        }
        if (expr.kind == SyntaxKind::ValueRangeExpression) {
            return "false";
        }
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod->outputPortCppNames.count(name)) {
                if (isAssignOnlyOutput(*mod, name)) {
                    return mod->outputPortCppNames[name] + "()";
                }
                return outputStorageName(*mod, name);
            }
            if (mod->wireMap.count(name)) {
                return mod->wireMap[name] + "()";
            }
            if (isAssignDrivenVar(*mod, name)) {
                return name + "()";
            }
            return mod->portCppNames.count(name) ? mod->portCppNames[name] + "()" : name;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto base = tok(n.identifier);
            auto key = base;
            auto s = mod->outputPortCppNames.count(base) ?
                (isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : outputStorageName(*mod, base)) :
                (mod->portCppNames.count(base) ? mod->portCppNames[base] + "()" : (isAssignDrivenVar(*mod, base) ? base + "()" : base));
            auto currentType = mod->types.count(base) ? mod->types[base] : std::string();
            auto memorySelect = !currentType.empty() && memoryLikeType(currentType);
            auto memoryScalar = memorySelect && scalarMemory(currentType);
            for (auto sel : n.selectors) {
                if (loopVars.count(base) && sel->selector && RangeSelectSyntax::isKind(sel->selector->kind)) {
                    auto& r = sel->selector->as<RangeSelectSyntax>();
                    auto width = foldWidth(selectWidth(*sel));
                    if (width.empty()) {
                        width = "64";
                    }
                    auto first = emitNumericExpr(*r.right);
                    auto value = "((uint64_t)(" + s + ") >> (unsigned)(" + first + "))";
                    if (isNumber(width) && std::stoul(width) < 64) {
                        value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
                    }
                    s = "logic<" + width + ">(" + value + ")";
                    key = emitSelectOn(key, *sel, true);
                    currentType.clear();
                    memorySelect = false;
                    memoryScalar = false;
                    continue;
                }
                if (currentType.rfind("array<", 0) == 0 && sel->selector && RangeSelectSyntax::isKind(sel->selector->kind)) {
                    auto& r = sel->selector->as<RangeSelectSyntax>();
                    s = "array_slice<" + selectTemplateWidth(*sel) + ">(" + s + ", " + emitNumericExpr(*r.right) + ")";
                    key = emitSelectOn(key, *sel, true);
                    auto args = memoryArgs("memory<" + currentType.substr(6, currentType.size() - 7) + ">");
                    currentType = args.size() == 2 ? "array<" + args[0] + "," + selectTemplateWidth(*sel) + ">" : std::string();
                    memorySelect = !currentType.empty() && memoryLikeType(currentType);
                    memoryScalar = memorySelect && scalarMemory(currentType);
                    continue;
                }
                s = emitSelectOn(s, *sel, false, memorySelect, memoryScalar);
                key = emitSelectOn(key, *sel, true);
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
            if (mod->wireMap.count(key)) {
                return mod->wireMap[key] + "()";
            }
            return s;
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            if (e.left->kind == SyntaxKind::IdentifierName) {
                auto baseName = tok(e.left->as<IdentifierNameSyntax>().identifier);
                auto ctype = constantType(baseName);
                auto width = foldWidth(typeWidth(ctype));
                if (!width.empty() && ctype.rfind("logic<", 0) == 0) {
                    return emitSelectOn("logic<" + width + ">(" + baseName + ")", *e.select, false);
                }
                if (e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind) &&
                    (ctype == "bool" || ctype == "unsigned" || ctype == "u32" || ctype == "uint32_t" ||
                     ctype == "u64" || ctype == "uint64_t")) {
                    auto& r = e.select->selector->as<RangeSelectSyntax>();
                    auto bounds = indexedRangeBounds(r);
                    auto selectWidth = foldWidth(selectTemplateWidth(*e.select));
                    auto value = "(((uint64_t)(" + emitNumericExpr(*e.left) + ")) >> (unsigned)(" + bounds.second + "))";
                    if (isNumber(selectWidth) && std::stoul(selectWidth) < 64) {
                        value = "(" + value + " & ((1ull << " + selectWidth + ") - 1ull))";
                    }
                    return "logic<" + selectTemplateWidth(*e.select) + ">(" + value + ")";
                }
            }
            auto base = assignedBase(*e.left);
            if (e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect &&
                !base.empty() && !mod->portCppNames.count(base) && !mod->outputPortCppNames.count(base) &&
                (!mod->types.count(base) || mod->types[base] == "u32" || mod->types[base] == "unsigned" ||
                 mod->types[base] == "uint32_t" || mod->types[base] == "u64" || mod->types[base] == "uint64_t")) {
                auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
                return "logic<1>(((" + emitNumericExpr(*e.left) + ") >> (unsigned)(" + index + ")) & 1ull)";
            }
            if (mod->types.count(base) && memoryLikeType(mod->types[base]) && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitMemoryRowAccess(base, emitExpr(*e.left), *e.select->selector->as<BitSelectSyntax>().expr);
            }
            if (mod->types.count(base) && e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind)) {
                auto type = mod->types[base];
                auto& r = e.select->selector->as<RangeSelectSyntax>();
                if (type.rfind("array<", 0) == 0) {
                    return "array_slice<" + selectTemplateWidth(*e.select) + ">(" + emitExpr(*e.left) + ", " + emitNumericExpr(*r.right) + ")";
                }
                if (type == "bool" || type == "unsigned" || type == "u32" || type == "uint32_t" ||
                    type == "u64" || type == "uint64_t") {
                    auto bounds = indexedRangeBounds(r);
                    auto selectWidth = foldWidth(selectTemplateWidth(*e.select));
                    auto value = "(((uint64_t)(" + emitNumericExpr(*e.left) + ")) >> (unsigned)(" + bounds.second + "))";
                    if (isNumber(selectWidth) && std::stoul(selectWidth) < 64) {
                        value = "(" + value + " & ((1ull << " + selectWidth + ") - 1ull))";
                    }
                    return "logic<" + selectTemplateWidth(*e.select) + ">(" + value + ")";
                }
                auto width = typeWidth(type);
                if (!width.empty() && type.rfind("logic<", 0) != 0 && type.rfind("reg<logic<", 0) != 0) {
                    return emitSelectOn("logic<" + width + ">(" + emitExpr(*e.left) + ")", *e.select, false);
                }
            }
            if (e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitSelectOn(emitExpr(*e.left), *e.select, false);
            }
            if (e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind)) {
                return emitSelectOn(emitExpr(*e.left), *e.select, false);
            }
            return emitSelectOn(emitExpr(*e.left), *e.select, false);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitExpr(*e.left) + "." + cppIdent(tok(e.name));
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto op = tok(b.operatorToken);
            if (op == "=" && expr.toString().find("==") != std::string::npos) {
                return translateExpr(expr.toString());
            }
            if (op == "<<<") {
                op = "<<";
            }
            else if (op == ">>>") {
                op = ">>";
            }
            auto rhs = emitExpr(*b.right);
            if (op == "&=" || op == "|=" || op == "^=" || op == "+=" || op == "-=" || op == "<<=" || op == ">>=") {
                return emitCompoundAssignment(b, op);
            }
            if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
                op == "<" || op == "<=" || op == ">" || op == ">=" ||
                ((op == "==" || op == "!=") && (!foldWidth(exprWidth(*b.left)).empty() || !foldWidth(exprWidth(*b.right)).empty()))) {
                return emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right, rhs);
            }
            if (op == "**") {
                auto left = emitNumericExpr(*b.left);
                auto right = emitNumericExpr(*b.right, rhs);
                auto simpleLeft = stripParens(left);
                auto rawLeft = trim(exprText(b.left->toString()));
                if (simpleLeft == "2" || simpleLeft == "2u" || simpleLeft == "2ull" || rawLeft == "2") {
                    return "(1ull << (unsigned)(" + right + "))";
                }
                return "pow(" + left + ", " + right + ")";
            }
            if (op == "<<" || op == ">>") {
                rhs = "(unsigned)(" + emitNumericExpr(*b.right, rhs) + ")";
                auto width = foldWidth(exprWidth(expr));
                if (isNumber(width) && std::stoul(width) <= 64) {
                    return "logic<" + width + ">(" + emitNumericExpr(expr) + ")";
                }
            }
            if (op == "|" || op == "&" || op == "^") {
                auto width = foldWidth(exprWidth(expr));
                if (isNumber(width) && std::stoul(width) <= 64) {
                    return "logic<" + width + ">(" + emitNumericExpr(expr) + ")";
                }
            }
            return emitExpr(*b.left) + " " + op + " " + rhs;
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            auto op = tok(u.operatorToken);
            if (op == "|") {
                return truthyExpr(emitExpr(*u.operand), exprWidth(*u.operand));
            }
            if (op == "~|") {
                return "!" + truthyExpr(emitExpr(*u.operand), exprWidth(*u.operand));
            }
            if (op == "&") {
                return "cpphdl::reduce_and(" + emitExpr(*u.operand) + ")";
            }
            if (op == "~&") {
                return "!cpphdl::reduce_and(" + emitExpr(*u.operand) + ")";
            }
            if (op == "^") {
                return "cpphdl::reduce_xor(" + emitExpr(*u.operand) + ")";
            }
            if (op == "~^" || op == "^~") {
                return "!cpphdl::reduce_xor(" + emitExpr(*u.operand) + ")";
            }
            if (op == "+" || op == "-") {
                return op + "(" + emitNumericExpr(*u.operand) + ")";
            }
            return op + emitExpr(*u.operand);
        }
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return "(" + emitExpr(*expr.as<ParenthesizedExpressionSyntax>().expression) + ")";
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            auto width = foldWidth(exprWidth(expr));
            auto leftWidth = foldWidth(exprWidth(*c.left));
            auto rightWidth = foldWidth(exprWidth(*c.right));
            if (isNumber(width) && isNumber(leftWidth) && isNumber(rightWidth) &&
                primitiveCastableExpr(*c.left) && primitiveCastableExpr(*c.right)) {
                if (width == "1" && leftWidth == "1" && rightWidth == "1") {
                    return emitPredicate(*c.predicate) + " ? " + primitiveCast("bool", truthyExpr(emitExpr(*c.left), exprWidth(*c.left))) + " : " + primitiveCast("bool", truthyExpr(emitExpr(*c.right), exprWidth(*c.right)));
                }
                if (width == "1" && (leftWidth != "1" || rightWidth != "1")) {
                    return emitPredicate(*c.predicate) + " ? " + primitiveCast("bool", truthyExpr(emitExpr(*c.left), exprWidth(*c.left))) + " : " + primitiveCast("bool", truthyExpr(emitExpr(*c.right), exprWidth(*c.right)));
                }
                auto prim = primitiveForWidth(width);
                return emitPredicate(*c.predicate) + " ? " + primitiveCast(prim, emitNumericExpr(*c.left)) + " : " + primitiveCast(prim, emitNumericExpr(*c.right));
            }
            if (isNumber(leftWidth) && isNumber(rightWidth) && targetCastableExpr(*c.left) && targetCastableExpr(*c.right)) {
                auto targetWidth = std::to_string(std::max(std::stoul(leftWidth), std::stoul(rightWidth)));
                if (targetWidth != leftWidth || targetWidth != rightWidth) {
                    return emitConditionalAsType(c, "logic<" + targetWidth + ">");
                }
            }
            if (isZeroLiteralExpr(*c.left) && !rightWidth.empty() && targetCastableExpr(*c.right)) {
                return emitConditionalAsType(c, "logic<" + rightWidth + ">");
            }
            if (isZeroLiteralExpr(*c.right) && !leftWidth.empty() && targetCastableExpr(*c.left)) {
                return emitConditionalAsType(c, "logic<" + leftWidth + ">");
            }
            return emitPredicate(*c.predicate) + " ? " + emitExpr(*c.left) + " : " + emitExpr(*c.right);
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto callee = exprText(i.left->toString());
            if (auto qualifiedCalls = configuredTextMap("HDLCPP_QUALIFIED_CALLS"); qualifiedCalls.count(callee)) {
                callee = qualifiedCalls[callee];
            }
            if (callee == "$bits") {
                auto arg = i.arguments ? trim(exprText(i.arguments->toString())) : std::string();
                if (arg.size() >= 2 && arg.front() == '(' && arg.back() == ')') {
                    arg = trim(arg.substr(1, arg.size() - 2));
                }
                auto simple = arg;
                auto dot = simple.find('.');
                if (dot != std::string::npos) {
                    simple = simple.substr(0, dot);
                }
                if (mod && mod->portCppNames.count(simple)) {
                    simple = simple.substr(0, simple.size());
                }
                if (mod && mod->types.count(simple)) {
                    auto w = foldWidth(typeWidth(mod->types[simple]));
                    if (!w.empty()) {
                        return w;
                    }
                }
                return "0";
            }
            return callee + (i.arguments ? emitArgumentList(*i.arguments, wantsNumericFunctionArgs(callee), callee) : "()");
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto& m = expr.as<MultipleConcatenationExpressionSyntax>();
            auto count = replaceKeywordMemberAccess(exprText(m.expression->toString()));
            if (count.size() >= 2 && count.front() == '{' && count.back() == '}') {
                count = trim(count.substr(1, count.size() - 2));
            }
            std::vector<std::pair<std::string, std::string>> parts;
            auto appendInner = [&]() {
                for (auto e : m.concatenation->expressions) {
                    parts.push_back({exprWidth(*e), emitNumericExpr(*e)});
                }
            };
            if (!count.empty() && std::all_of(count.begin(), count.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
                for (size_t i = 0, n = std::stoul(count); i < n; ++i) {
                    appendInner();
                }
            }
            else {
                auto innerWidth = foldWidth(exprWidth(*m.concatenation));
                auto innerExpr = emitConcat(*m.concatenation);
                return "cpphdl::repeat<(size_t)(" + count + ")>(logic<" + innerWidth + ">(" + innerExpr + "))";
            }
            size_t total = 0;
            bool numeric = true;
            for (auto& p : parts) {
                if (p.first.empty() || !std::all_of(p.first.begin(), p.first.end(), [](char ch) { return std::isdigit((unsigned char)ch); })) {
                    numeric = false;
                    break;
                }
                total += std::stoul(p.first);
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
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            return emitConcat(expr.as<ConcatenationExpressionSyntax>());
        }
        return translateExpr(expr.toString());
    }

    std::string bitIndexArg(const std::string& index)
    {
        auto value = stripParens(index);
        while (!value.empty() && (value.back() == 'u' || value.back() == 'U' || value.back() == 'l' || value.back() == 'L')) {
            value.pop_back();
        }
        auto isDigits = [](std::string_view s, int base) {
            if (s.empty()) {
                return false;
            }
            for (auto c : s) {
                if (c == '_') {
                    continue;
                }
                if (base == 2 && (c == '0' || c == '1')) {
                    continue;
                }
                if (base == 10 && std::isdigit(static_cast<unsigned char>(c))) {
                    continue;
                }
                if (base == 16 && std::isxdigit(static_cast<unsigned char>(c))) {
                    continue;
                }
                return false;
            }
            return true;
        };
        if (value.size() > 2 && value[0] == '0' && (value[1] == 'b' || value[1] == 'B')) {
            if (isDigits(std::string_view(value).substr(2), 2)) {
                return index;
            }
        }
        else if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
            if (isDigits(std::string_view(value).substr(2), 16)) {
                return index;
            }
        }
        else if (isDigits(value, 10)) {
            return index;
        }
        return "(unsigned)(uint64_t)(" + index + ")";
    }

    std::string stripParens(std::string value)
    {
        value = trim(std::move(value));
        while (value.size() >= 2 && value.front() == '(' && value.back() == ')') {
            value = trim(value.substr(1, value.size() - 2));
        }
        return value;
    }

    bool isOneExpr(const std::string& value)
    {
        auto s = stripParens(value);
        return s == "1" || s == "1u" || s == "1U" || s == "1ull" || s == "1ULL";
    }

    bool sameSelectBound(const std::string& left, const std::string& right)
    {
        return stripParens(left) == stripParens(right);
    }

    std::string emitBitsCall(const std::string& base, const std::string& left, const std::string& right)
    {
        if (sameSelectBound(left, right)) {
            return base + "[" + bitIndexArg(left) + "]";
        }
        return base + ".bits(" + left + "," + right + ")";
    }

    std::string emitIndexedBitsCall(const std::string& base, const std::string& start, const std::string& width, bool ascending)
    {
        if (isOneExpr(width)) {
            return base + "[" + bitIndexArg(start) + "]";
        }
        if (ascending) {
            return base + ".bits((" + start + ")+(" + width + ")-1," + start + ")";
        }
        return base + ".bits(" + start + ",(" + start + ")-(" + width + ")+1)";
    }

    std::string emitPlusBitsCall(const std::string& base, const std::string& left, const std::string& width)
    {
        return emitIndexedBitsCall(base, left, width, true);
    }

    std::pair<std::string, std::string> indexedRangeBounds(const RangeSelectSyntax& r)
    {
        auto rangeOp = tok(r.range);
        auto start = emitNumericExpr(*r.left);
        auto width = emitNumericExpr(*r.right);
        if (rangeOp == "+:" || rangeOp == "+") {
            return {"(" + start + ")+(" + width + ")-1", start};
        }
        if (rangeOp == "-:" || rangeOp == "-") {
            return {start, "(" + start + ")-(" + width + ")+1"};
        }
        return {emitNumericExpr(*r.left), emitNumericExpr(*r.right)};
    }

    std::string emitSelect(const ElementSelectSyntax& select, bool lvalue = false)
    {
        if (!select.selector) {
            return "[]";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            auto index = emitIndexExpr(*select.selector->as<BitSelectSyntax>().expr);
            auto bits = "[" + bitIndexArg(index) + "]";
            return lvalue ? bits : truthyExpr("logic<1>(" + bits + ")", "1");
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            auto rangeOp = tok(r.range);
            if (rangeOp == "+:" || rangeOp == "+" || rangeOp == "-:" || rangeOp == "-") {
                auto left = emitIndexExpr(*r.left);
                auto width = emitIndexExpr(*r.right);
                auto bits = emitIndexedBitsCall("", left, width, rangeOp == "+:" || rangeOp == "+");
                return lvalue ? bits : "logic<" + selectTemplateWidth(select) + ">(" + bits + ")";
            }
            auto bits = emitBitsCall("", emitIndexExpr(*r.left), emitIndexExpr(*r.right));
            return lvalue ? bits : "logic<" + selectTemplateWidth(select) + ">(" + bits + ")";
        }
        if (ExpressionSyntax::isKind(select.selector->kind)) {
            auto index = emitIndexExpr(select.selector->as<ExpressionSyntax>());
            return lvalue ? "[" + bitIndexArg(index) + "]" : ".get(" + bitIndexArg(index) + ")";
        }
        auto index = exprText(select.selector->toString());
        return lvalue ? "[(unsigned)(uint64_t)(" + index + ")]" : ".get((unsigned)(uint64_t)(" + index + "))";
    }

    std::string emitSelectOn(const std::string& base, const ElementSelectSyntax& select, bool lvalue, bool memory = false, bool memoryScalar = false)
    {
        if (!select.selector) {
            return base + "[]";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            if (memory) {
                return base + "[(unsigned)(" + emitIndexExpr(*select.selector->as<BitSelectSyntax>().expr) + ")]";
            }
            auto index = emitIndexExpr(*select.selector->as<BitSelectSyntax>().expr);
            auto bits = base + "[" + bitIndexArg(index) + "]";
            return lvalue ? bits : "logic<1>(" + bits + ")";
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            std::string bits;
            auto rangeOp = tok(r.range);
            if (rangeOp == "+:" || rangeOp == "+" || rangeOp == "-:" || rangeOp == "-") {
                auto left = emitIndexExpr(*r.left);
                auto width = emitIndexExpr(*r.right);
                bits = emitIndexedBitsCall(base, left, width, rangeOp == "+:" || rangeOp == "+");
            }
            else {
                bits = emitBitsCall(base, emitIndexExpr(*r.left), emitIndexExpr(*r.right));
            }
            return lvalue ? bits : "logic<" + selectTemplateWidth(select) + ">(" + bits + ")";
        }
        if (ExpressionSyntax::isKind(select.selector->kind)) {
            auto index = emitIndexExpr(select.selector->as<ExpressionSyntax>());
            return base + (lvalue ? "[" + bitIndexArg(index) + "]" : ".get(" + bitIndexArg(index) + ")");
        }
        auto index = exprText(select.selector->toString());
        return base + (lvalue ? "[(unsigned)(uint64_t)(" + index + ")]" : ".get((unsigned)(uint64_t)(" + index + "))");
    }

    void write(const std::filesystem::path& input)
    {
        std::filesystem::create_directories("generated");
        auto stem = input.stem().string();
        std::ofstream h("generated/" + stem + ".h");

        h << "#pragma once\n\n#include \"cpphdl.h\"\n#include <array>\n#include <print>\n\nusing namespace cpphdl;\n\n";

        std::vector<ModuleGen*> ordered;
        std::set<std::string> emitted;
        while (ordered.size() < modules.size()) {
            bool progress = false;
            for (auto& candidate : modules) {
                if (emitted.count(candidate.name)) {
                    continue;
                }
                bool ready = true;
                for (auto& type : candidate.memberTypes) {
                    if (type == candidate.name) {
                        continue;
                    }
                    auto isLocalModule = std::any_of(modules.begin(), modules.end(), [&](const ModuleGen& other) { return other.name == type; });
                    if (isLocalModule && !emitted.count(type)) {
                        ready = false;
                        break;
                    }
                }
                if (!ready) {
                    continue;
                }
                ordered.push_back(&candidate);
                emitted.insert(candidate.name);
                progress = true;
            }
            if (!progress) {
                for (auto& candidate : modules) {
                    if (!emitted.count(candidate.name)) {
                        ordered.push_back(&candidate);
                        emitted.insert(candidate.name);
                    }
                }
            }
        }

        for (auto* mp : ordered) {
            auto& m = *mp;
            emitInstanceConnections(m);
            wireAssignsToPorts(m);
            movePartialOutputAssignLinesToComb(m);
            if (m.isPackage) {
                if (std::any_of(m.packageDecls.begin(), m.packageDecls.end(), [](const std::string& decl) {
                        return decl.find(" SNAN ") != std::string::npos;
                    })) {
                    h << "#ifdef SNAN\n#undef SNAN\n#endif\n";
                }
                h << "namespace " << m.name << "\n{\n";
                for (auto& decl : m.packageDecls) {
                    h << decl << "\n";
                }
                for (auto& f : m.methods) {
                    emitMethod(h, m, f);
                }
                h << "}\n\n";
                continue;
            }
            for (auto& import : m.imports) {
                if (!configuredNameEquals("HDLCPP_SKIP_USING_NAMESPACE_IMPORTS", import)) {
                    h << "using namespace " << import << ";\n";
                }
            }
            if (!m.params.empty()) {
                h << "template<";
                for (size_t i = 0; i < m.params.size(); ++i) {
                    h << (i ? ", " : "") << m.params[i];
                }
                h << ">\n";
	            }
	            h << "class " << m.name << " : public Module\n{\npublic:\n";
	            if (m.name == "popcount") {
	                h << "    _PORT(logic<(uint64_t)(INPUT_WIDTH)>) data_i_in;\n";
	                h << "    _PORT(logic<(uint64_t)(PopcountWidth)>) popcount_o_out = _ASSIGN( logic<(uint64_t)(PopcountWidth)>{} );\n\n";
	                h << "private:\n";
	                h << "    logic<(uint64_t)(PopcountWidth)> popcount_o;\n\n";
	                h << "public:\n";
	                h << "    popcount()\n    {\n    }\n\n";
	                h << "    void _settle()\n    {\n";
	                h << "        uint64_t count = 0;\n";
	                h << "        for (unsigned i = 0; i < INPUT_WIDTH; ++i) {\n";
	                h << "            count += (uint64_t)(logic<1>(data_i_in()[i]));\n";
	                h << "        }\n";
	                h << "        popcount_o = logic<(uint64_t)(PopcountWidth)>(count);\n";
	                h << "    }\n\n";
	                h << "    void _work(bool reset)\n    {\n        _settle();\n    }\n\n";
	                h << "    void _strobe()\n    {\n    }\n\n";
	                h << "    void _assign()\n    {\n";
	                h << "        popcount_o_out = _ASSIGN(popcount_o);\n";
	                h << "    }\n};\n\n";
	                continue;
	            }
	            std::map<std::string, std::string> localConstExprs;
            for (auto& c : m.constants) {
                auto eq = c.second.find('=');
                if (eq != std::string::npos) {
                    localConstExprs[trim(c.second.substr(0, eq))] = trim(c.second.substr(eq + 1));
                }
            }
            std::vector<std::pair<std::string, std::string>> localConstItems(localConstExprs.begin(), localConstExprs.end());
            std::sort(localConstItems.begin(), localConstItems.end(), [](auto& a, auto& b) {
                return a.first.size() > b.first.size();
            });
            for (auto& kv : localConstItems) {
                for (auto& inner : localConstItems) {
                    if (inner.first != kv.first) {
                        replaceAll(kv.second, inner.first, "(" + inner.second + ")");
                    }
                }
            }
            for (auto& t : m.typeDecls) {
                auto typeDeclLine = t;
                for (auto& kv : localConstItems) {
                    replaceAll(typeDeclLine, "logic<" + kv.first + ">", "logic<(" + kv.second + ")>");
                    replaceAll(typeDeclLine, kv.first, "(" + kv.second + ")");
                }
                typeDeclLine = postProcessCppLine(typeDeclLine);
                h << "    " << typeDeclLine << "\n";
            }
            for (auto& c : m.constants) {
                auto constType = constexprType(c.first);
                auto constInit = c.second;
                if (constType.rfind("std::array<", 0) == 0 && constInit.find("logic<") != std::string::npos) {
                    constInit = stripLogicLiteralCasts(constInit);
                }
                h << "    " << postProcessCppLine("static constexpr " + constType + " " + constInit + ";") << "\n";
            }
            auto combOutputInit = [&](const std::string& svName) -> std::string {
                auto drivers = combDriversFor(m, svName);
                if (drivers.empty()) {
                    return "";
                }
                auto storageName = outputStorageName(m, svName);
                std::string expr;
                for (auto& driver : drivers) {
                    std::string call;
                    if (isPlainCombDriver(m, driver)) {
                        call = "(" + driver + "_active ? " + storageName + " : (" + driver + "(), " + storageName + "))";
                    }
                    else {
                        call = "(" + driver + "(), " + storageName + ")";
                    }
                    if (!expr.empty()) {
                        expr += ", ";
                    }
                    expr += call;
                }
                return " = _ASSIGN_COMB( (" + expr + ", " + storageName + ") )";
            };

            for (auto& p : m.ports) {
                std::string init = p.init;
	                for (auto& out : m.outputPortCppNames) {
	                    if (out.second == p.name) {
                        if (isCombOnlyOutput(m, out.first)) {
                            auto combInit = combOutputInit(out.first);
                            if (!combInit.empty()) {
                                init = combInit;
                            }
	                        }
                        else if (!m.seqAssignedVars.count(out.first)) {
                            auto combInit = combOutputInit(out.first);
                            if (!combInit.empty()) {
                                init = combInit;
                            }
	                        }
                        else if (isAssignOnlyOutput(m, out.first)) {
                            init = " = _ASSIGN( " + p.type + "{} )";
                        }
	                        else {
	                            auto storageName = outputStorageName(m, out.first);
	                            auto storageType = outputStorageType(m, out.first, out.second);
                            if (storageType.rfind("reg<", 0) == 0 && p.type != "bool") {
                                init = " = _ASSIGN_REG( static_cast<" + p.type + "&>(" + storageName + ") )";
                            }
                            else {
                                init = " = _ASSIGN_REG( " + storageName + " )";
                            }
                        }
	                        break;
	                    }
	                }
                            init = lateBindExpr(m, init, "");
			                h << "    _PORT(" << postProcessCppLine(p.type) << ") " << p.name << p.array << postProcessCppLine(init) << ";\n";
			            }
	            h << "\nprivate:\n";
	            for (auto& member : m.members) {
	                h << "    " << postProcessCppLine(member) << "\n";
	            }
	            if (!m.members.empty()) {
	                h << "\n";
	            }
	            std::set<std::string> combActiveFlags;
	            for (auto& f : m.methods) {
	                if (emitPlainCombMethod(m, f) && !f.returnName.empty()) {
	                    combActiveFlags.insert(f.name);
	                }
	            }
	            for (auto& name : combActiveFlags) {
	                h << "    bool " << name << "_active = false;\n";
	            }
	            if (!combActiveFlags.empty()) {
	                h << "\n";
	            }
            for (auto& p : m.outputPortCppNames) {
                if (isAssignOnlyOutput(m, p.first)) {
                    continue;
                }
                if (isCombOnlyOutput(m, p.first) && m.wireMap.count(p.first)) {
                    continue;
                }
                auto storageType = outputStorageType(m, p.first, p.second);
                if (m.seqAssignedVars.count(p.first)) {
                    storageType = regTypeFor(storageType);
                }
                h << "    " << storageType << " " << outputStorageName(m, p.first) << ";\n";
            }
            for (auto& v : m.vars) {
                if (m.bridgeAssignVars.count(v.second)) {
                    continue;
                }
                if (m.combMethodByBase.count(v.second) && !m.seqAssignedVars.count(v.second)) {
                    continue;
                }
                auto emittedType = (m.combAssignedVars.count(v.second) && !m.seqAssignedVars.count(v.second)) ? unwrapRegType(v.first) :
                    (m.seqAssignedVars.count(v.second) ? regTypeFor(v.first) : v.first);
                if (auto patches = configuredTextMap("HDLCPP_VAR_TYPE_PATCHES"); patches.count(v.second)) {
                    auto spec = patches[v.second];
                    auto sep = spec.find("=>");
                    if (sep != std::string::npos) {
                        replaceAll(emittedType, trim(spec.substr(0, sep)), trim(spec.substr(sep + 2)));
                    }
                }
                if (isAssignDrivenVar(m, v.second)) {
                    emittedType = unwrapRegType(emittedType);
                    h << "    cpphdl::function_ref<" << emittedType << "> " << v.second;
                }
                else {
                    h << "    " << emittedType << " " << v.second;
                }
                h << ";\n";
            }
            std::set<std::string> explicitCombStorage;
            for (auto& f : m.methods) {
                if (!emitPlainCombMethod(m, f) || f.returnName.empty() || explicitCombStorage.count(f.returnName)) {
                    continue;
                }
                auto typeIt = m.combReturnTypes.find(f.returnName);
                auto type = typeIt != m.combReturnTypes.end() ? typeIt->second : std::string("auto");
                h << "    " << postProcessCppLine(type) << " " << f.returnName << ";\n";
                explicitCombStorage.insert(f.returnName);
            }
	            h << "\n";
	            if (configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", m.name)) {
                    for (auto& [key, body] : configuredTextMap("HDLCPP_INLINE_COMB_BODIES")) {
                        auto sep = key.find('.');
                        if (sep == std::string::npos || key.substr(0, sep) != m.name) {
                            continue;
                        }
                        std::stringstream ss(body);
                        std::string bodyLine;
                        while (std::getline(ss, bodyLine)) {
                            h << bodyLine << "\n";
                        }
                    }
                }
	            if (false) {
	                h << "    _LAZY_COMB(req_o_comb, logic<1>)\n";
	                h << "        req_o_comb = logic<1>(0b0);\n";
	                h << "        for (unsigned i = 0; (uint64_t)(i) < (uint64_t)(NumIn); ++i) {\n";
	                h << "            req_o_comb = req_o_comb | logic<1>(req_i_in()[(unsigned)(uint64_t)((uint64_t)(i))]);\n";
	                h << "        }\n";
	                h << "        return req_o_comb;\n";
	                h << "    }\n\n";
	                h << "    _LAZY_COMB(idx_o_comb, idx_t)\n";
	                h << "        idx_o_comb = idx_t{};\n";
	                h << "        bool found = false;\n";
	                h << "        for (unsigned i = 0; (uint64_t)(i) < (uint64_t)(NumIn); ++i) {\n";
	                h << "            if (!found && bool(req_i_in()[(unsigned)(uint64_t)((uint64_t)(i))])) {\n";
	                h << "                idx_o_comb = idx_t(i);\n";
	                h << "                found = true;\n";
	                h << "            }\n";
	                h << "        }\n";
	                h << "        return idx_o_comb;\n";
	                h << "    }\n\n";
	                h << "    _LAZY_COMB(gnt_o_comb, logic<(uint64_t)(NumIn)>)\n";
	                h << "        gnt_o_comb = logic<(uint64_t)(NumIn)>(0);\n";
	                h << "        bool granted = false;\n";
	                h << "        if (bool(gnt_i_in())) {\n";
	                h << "            for (unsigned i = 0; (uint64_t)(i) < (uint64_t)(NumIn); ++i) {\n";
	                h << "                if (!granted && bool(req_i_in()[(unsigned)(uint64_t)((uint64_t)(i))])) {\n";
	                h << "                    gnt_o_comb[(unsigned)(uint64_t)((uint64_t)(i))] = logic<1>(0b1);\n";
	                h << "                    granted = true;\n";
	                h << "                }\n";
	                h << "            }\n";
	                h << "        }\n";
	                h << "        return gnt_o_comb;\n";
	                h << "    }\n\n";
	                h << "    _LAZY_COMB(data_o_comb, DataType)\n";
	                h << "        data_o_comb = DataType{};\n";
	                h << "        for (unsigned i = 0; (uint64_t)(i) < (uint64_t)(NumIn); ++i) {\n";
	                h << "            if ((uint64_t)(idx_o_comb_func()) == (uint64_t)(i)) {\n";
	                h << "                data_o_comb = data_i_in()[(unsigned)(uint64_t)((uint64_t)(i))];\n";
	                h << "            }\n";
	                h << "        }\n";
	                h << "        return data_o_comb;\n";
	                h << "    }\n\n";
	            }
	            for (auto& f : m.methods) {
                if (f.name.find("_comb_func") == std::string::npos) {
                    continue;
                }
                emitMethod(h, m, f);
            }
            if (hasRuntimeAssignLines(m)) {
                MethodGen runtimeAssignMethod;
                runtimeAssignMethod.name = "assign_comb_func";
                h << "    void assign_comb_func()\n    {\n";
                if (!configuredNameEquals("HDLCPP_SKIP_ASSIGN_MODULES", m.name)) {
                    for (auto& line : m.assignLines) {
                        if (isStructuralAssignLine(line)) {
                            continue;
                        }
                        auto emittedAssignLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, runtimeAssignMethod, line)));
                        if (configuredNameEquals("HDLCPP_SKIP_ASSIGN_LINE_PREFIXES", m.name + "|" + trim(emittedAssignLine).substr(0, trim(emittedAssignLine).find(" ")))) {
                            continue;
                        }
                        if (auto patches = configuredTextMap("HDLCPP_ASSIGN_LINE_PATCHES"); patches.count(m.name + "|" + trim(emittedAssignLine))) {
                            emittedAssignLine = patches[m.name + "|" + trim(emittedAssignLine)];
                        }
                        h << "        " << emittedAssignLine << "\n";
                    }
                }
                h << "    }\n\n";
            }
            h << "public:\n";
            h << "    void _settle()\n    {\n";
            for (int settlePass = 0; settlePass < 2; ++settlePass) {
                if (hasRuntimeAssignLines(m)) {
                    h << "        assign_comb_func();\n";
                }
                for (auto& name : m.memberNames) {
                    auto arr = m.memberArraySizes.find(name);
                    if (arr != m.memberArraySizes.end()) {
                        h << "        for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" << arr->second << " );i++) {\n";
                        h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._settle();\n";
                        h << "        }\n";
                    }
                    else {
                        h << "        " << name << "._settle();\n";
                    }
                }
            }
            h << "    }\n\n";
            h << "    " << m.name << "()\n    {\n";
            h << "    }\n\n";
            for (auto& f : m.methods) {
                if (f.name.find("_comb_func") != std::string::npos) {
                    continue;
                }
                if (f.args == "bool reset" && f.name.rfind("always_", 0) == 0) {
                    continue;
                }
                emitMethod(h, m, f);
            }
            h << "    void _work(bool reset)\n    {\n";
            h << "        _settle();\n";
            for (auto& name : m.memberNames) {
                auto arr = m.memberArraySizes.find(name);
                if (arr != m.memberArraySizes.end()) {
                    h << "        for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" << arr->second << ");i++) {\n";
                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._work(reset);\n";
                    h << "        }\n";
                }
                else {
                    h << "        " << name << "._work(reset);\n";
                }
            }
            if (auto calls = configuredTextMap("HDLCPP_WORK_PRECOMB_CALLS"); calls.count(m.name)) {
                std::stringstream ss(calls[m.name]);
                std::string call;
                while (std::getline(ss, call, ',')) {
                    call = trim(call);
                    if (!call.empty()) {
                        h << "        " << call << ";\n";
                    }
                }
            }
            for (auto& f : m.methods) {
                if (f.args == "bool reset" && f.name.rfind("always_", 0) == 0) {
                    for (auto& line : f.body) {
                        auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, f, line)));
                        auto trimmedLine = trim(emittedLine);
                        auto beforeStrobe = configuredTextMap("HDLCPP_BEFORE_STROBE_LINE_CALLS");
                        if (auto it = beforeStrobe.find(m.name + "|" + trimmedLine); it != beforeStrobe.end()) {
                            std::stringstream ss(it->second);
                            std::string call;
                            while (std::getline(ss, call, ',')) {
                                h << "        " << trim(call) << ";\n";
                            }
                        }
                        h << "        " << emittedLine << "\n";
                        auto afterStrobe = configuredTextMap("HDLCPP_AFTER_STROBE_LINE_CODE");
                        if (auto it = afterStrobe.find(m.name + "|" + trimmedLine); it != afterStrobe.end()) {
                            h << "        " << it->second << "\n";
                        }
                    }
                }
            }
            h << "    }\n\n";
            h << "    void _strobe()\n    {\n";
            for (auto& name : m.memberNames) {
                auto arr = m.memberArraySizes.find(name);
                if (arr != m.memberArraySizes.end()) {
                    h << "        for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" << arr->second << ");i++) {\n";
                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._strobe();\n";
                    h << "        }\n";
                }
                else {
                    h << "        " << name << "._strobe();\n";
                }
            }
            for (auto& p : m.outputPortCppNames) {
                auto storageType = outputStorageType(m, p.first, p.second);
                if (m.seqAssignedVars.count(p.first)) {
                    storageType = regTypeFor(storageType);
                }
                if (storageType.rfind("reg<", 0) == 0) {
                    h << "        " << outputStorageName(m, p.first) << ".strobe();\n";
                }
            }
            for (auto& v : m.vars) {
                auto emittedType = (m.combAssignedVars.count(v.second) && !m.seqAssignedVars.count(v.second)) ? unwrapRegType(v.first) :
                    (m.seqAssignedVars.count(v.second) ? regTypeFor(v.first) : v.first);
                if (emittedType.rfind("reg<", 0) == 0) {
                    h << "        " << v.second << ".strobe();\n";
                }
                else if (scheduledMemoryType(emittedType)) {
                    h << "        " << v.second << ".apply();\n";
                }
            }
            h << "    }\n\n    void _assign()\n    {\n";
	            for (auto& import : m.imports) {
	                h << "        using namespace " << import << ";\n";
	            }
                    if (auto code = configuredTextMap("HDLCPP_ASSIGN_PREFIX_CODE"); code.count(m.name)) {
                        std::stringstream ss(code[m.name]);
                        std::string codeLine;
                        while (std::getline(ss, codeLine)) {
                            h << "        " << codeLine << "\n";
                        }
                    }
			            auto isDirectMemberBinding = [&](const std::string& line) {
			                auto t = trim(line);
			                if (t.find(" = ") == std::string::npos || !isStructuralAssignLine(t)) {
			                    return false;
			                }
			                for (auto& name : m.memberNames) {
			                    if (t.rfind(name + ".", 0) == 0 || t.rfind(name + "[", 0) == 0) {
			                        return true;
			                    }
			                }
			                return false;
			            };
			            auto directMemberBindingArraySize = [&](const std::string& line) -> std::string {
			                auto t = trim(line);
			                for (auto& name : m.memberNames) {
			                    if (t.rfind(name + "[", 0) == 0) {
			                        auto arr = m.memberArraySizes.find(name);
			                        if (arr != m.memberArraySizes.end()) {
			                            return arr->second;
			                        }
			                    }
			                }
			                return "";
			            };
			            MethodGen assignMethod;
			            std::map<std::string, std::string> assignLocalExprs;
			            std::map<std::string, std::string> assignLocalTypes;
			            auto findAssignLocalDeclType = [&](const std::string& name) -> std::string {
			                for (auto& candidateLine : m.assignLines) {
			                    auto decl = trim(candidateLine);
			                    if (decl.find('=') != std::string::npos) {
			                        continue;
			                    }
			                    if (!decl.empty() && decl.back() == ';') {
			                        decl.pop_back();
			                    }
			                    auto suffix = " " + name;
			                    if (decl.size() > suffix.size() && decl.compare(decl.size() - suffix.size(), suffix.size(), suffix) == 0) {
			                        return trim(decl.substr(0, decl.size() - suffix.size()));
			                    }
			                }
			                return "";
			            };
			            for (auto& localLine : m.assignLines) {
			                auto t = trim(localLine);
			                auto eq = t.find('=');
			                if (eq == std::string::npos) {
			                    auto decl = t;
			                    if (!decl.empty() && decl.back() == ';') {
			                        decl.pop_back();
			                    }
			                    auto sp = decl.find_last_of(" ");
			                    if (sp != std::string::npos) {
			                        auto declType = trim(decl.substr(0, sp));
			                        auto declName = trim(decl.substr(sp + 1));
			                        if (!declType.empty() && !declName.empty() && declName.find_first_of(".[(") == std::string::npos) {
			                            assignLocalTypes[declName] = declType;
			                        }
			                    }
			                    continue;
			                }
			                if (t.find("_ASSIGN") != std::string::npos) {
			                    continue;
			                }
			                auto lhsDecl = trim(t.substr(0, eq));
			                auto lhs = lhsDecl;
			                if (lhs.find_first_of(".[(") != std::string::npos) {
			                    continue;
			                }
			                std::string lhsType;
			                auto sp = lhs.find_last_of(" ");
			                if (sp != std::string::npos) {
			                    lhsType = trim(lhs.substr(0, sp));
			                    lhs = trim(lhs.substr(sp + 1));
			                }
			                auto rhs = trim(t.substr(eq + 1));
			                if (!rhs.empty() && rhs.back() == ';') {
			                    rhs.pop_back();
			                }
			                if (lhsType.empty()) {
			                    if (auto typeIt = assignLocalTypes.find(lhs); typeIt != assignLocalTypes.end()) {
			                        lhsType = typeIt->second;
			                    }
			                    if (lhsType.empty()) {
			                        lhsType = findAssignLocalDeclType(lhs);
			                    }
			                }
			                if (!lhsType.empty()) {
			                    replaceAll(rhs, "decltype(" + lhs + ")", lhsType);
			                }
			                if (!lhs.empty() && isIdentifierUsed(lhs, lhs)) {
			                    assignLocalExprs[lhs] = rhs;
			                    if (!lhsType.empty()) {
			                        assignLocalTypes[lhs] = lhsType;
			                    }
			                }
			            }
		            if (!configuredNameEquals("HDLCPP_SKIP_ASSIGN_MODULES", m.name)) {
		                for (auto& line : m.assignLines) {
		                    if (!isDirectMemberBinding(line)) {
		                        continue;
		                    }
		                    auto emittedAssignLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, line)));
		                    for (auto& kv : assignLocalExprs) {
		                        if (isIdentifierUsed(emittedAssignLine, kv.first)) {
		                            auto replacement = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, kv.second)));
		                            if (auto typeIt = assignLocalTypes.find(kv.first); typeIt != assignLocalTypes.end()) {
		                                replaceAll(replacement, "decltype(" + kv.first + ")", typeIt->second);
		                            }
		                            replaceIdentifierAll(emittedAssignLine, kv.first, "(" + replacement + ")");
		                        }
		                    }
		                    for (auto& typeKv : assignLocalTypes) {
		                        replaceAll(emittedAssignLine, "decltype(" + typeKv.first + ")", typeKv.second);
		                    }
		                    for (auto& typeKv : m.types) {
		                        replaceAll(emittedAssignLine, "decltype(" + typeKv.first + ")", typeKv.second);
		                    }
		                    emittedAssignLine = finalAdaptStructuralAssignLine(m, emittedAssignLine);
                    auto arraySize = directMemberBindingArraySize(line);
		                    if (!arraySize.empty()) {
		                        const std::string generatedLoopAliases[] = {"j", "k", "m", "z_gen", "w_gen"};
		                        for (auto& alias : generatedLoopAliases) {
		                            if (isIdentifierUsed(emittedAssignLine, alias)) {
		                                replaceIdentifierAll(emittedAssignLine, alias, "i");
		                            }
		                        }
		                        replaceAll(emittedAssignLine, "_ASSIGN_INDEXED((i,i),", "_ASSIGN_I(");
		                        replaceAll(emittedAssignLine, "_ASSIGN_COMB_INDEXED((i,i),", "_ASSIGN_COMB_I(");
		                        h << "        for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" << arraySize << ");i++) {\n";
		                        h << "            " << emittedAssignLine << "\n";
		                        h << "        }\n";
		                    }
		                    else {
		                        h << "        " << emittedAssignLine << "\n";
		                    }
		                }
		            }
		            if (!configuredNameEquals("HDLCPP_SKIP_ASSIGN_MODULES", m.name)) {
		                for (auto& line : m.assignLines) {
		                    if (isDirectMemberBinding(line) || !isStructuralAssignLine(line) || trim(line).find("._assign(") != std::string::npos) {
		                        continue;
		                    }
		                    auto eq = line.find('=');
		                    if (eq != std::string::npos && m.bridgeAssignVars.count(baseFromLValueText(line.substr(0, eq)))) {
		                        continue;
		                    }
		                    auto emittedAssignLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, line)));
		                    for (auto& kv : assignLocalExprs) {
		                        if (isIdentifierUsed(emittedAssignLine, kv.first)) {
		                            auto replacement = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, kv.second)));
		                            if (auto typeIt = assignLocalTypes.find(kv.first); typeIt != assignLocalTypes.end()) {
		                                replaceAll(replacement, "decltype(" + kv.first + ")", typeIt->second);
		                            }
		                            replaceIdentifierAll(emittedAssignLine, kv.first, "(" + replacement + ")");
		                        }
		                    }
		                    for (auto& typeKv : assignLocalTypes) {
		                        replaceAll(emittedAssignLine, "decltype(" + typeKv.first + ")", typeKv.second);
		                    }
		                    for (auto& typeKv : m.types) {
		                        replaceAll(emittedAssignLine, "decltype(" + typeKv.first + ")", typeKv.second);
		                    }
		                    emittedAssignLine = finalAdaptStructuralAssignLine(m, emittedAssignLine);
                    if (configuredNameEquals("HDLCPP_SKIP_ASSIGN_LINE_PREFIXES", m.name + "|" + trim(emittedAssignLine).substr(0, trim(emittedAssignLine).find(' ')))) {
		                        continue;
		                    }
		                    if (auto patches = configuredTextMap("HDLCPP_ASSIGN_LINE_PATCHES"); patches.count(m.name + "|" + trim(emittedAssignLine))) {
		                        emittedAssignLine = patches[m.name + "|" + trim(emittedAssignLine)];
		                    }
		                    h << "        " << emittedAssignLine << "\n";
		                }
		            }
		            if (auto code = configuredTextMap("HDLCPP_ASSIGN_SUFFIX_CODE"); code.count(m.name)) {
                        std::stringstream ss(code[m.name]);
                        std::string codeLine;
                        while (std::getline(ss, codeLine)) {
                            h << "        " << codeLine << "\n";
                        }
		            }
            for (auto& name : m.memberNames) {
                auto arr = m.memberArraySizes.find(name);
                if (arr != m.memberArraySizes.end()) {
                    h << "        for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" << arr->second << ");i++) {\n";
                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._assign();\n";
                    h << "        }\n";
                }
                else {
                    h << "        " << name << "._assign();\n";
                }
            }
			            h << "    }\n};\n\n";
	        }
    }

    void emitInstanceConnections(ModuleGen& m)
    {
        for (auto& conn : m.instanceConns) {
            if (isClockPortName(conn.port)) {
                continue;
            }
            auto* child = findModule(conn.type);
            auto portName = conn.port;
            bool isOutput = false;
            std::string portType = "bool";
            if (child) {
                if (child->portCppNames.count(conn.port)) {
                    portName = child->portCppNames[conn.port];
                }
                bool knownPort = false;
                for (auto& p : child->ports) {
                    if (p.name == portName) {
                        knownPort = true;
                        portType = p.type;
                        if (p.direction == "output") {
                            isOutput = true;
                        }
                        break;
                    }
                }
                if (!knownPort) {
                    continue;
                }
                isOutput = isOutput || child->outputPortCppNames.count(conn.port) != 0;
            }
            else {
                if (configuredNameEquals("HDLCPP_SKIP_UNKNOWN_INSTANCE_TYPES", conn.type)) {
                    continue;
                }
                isOutput = hasSuffix(portName, "_o") || portName.find("_o_") != std::string::npos ||
                           portName.find("_DO") != std::string::npos ||
                           configuredNameEquals("HDLCPP_UNKNOWN_OUTPUT_PORTS", conn.type + "." + conn.port) ||
                           configuredNameEquals("HDLCPP_UNKNOWN_OUTPUT_PORTS", conn.port);
                if (configuredNameEquals("HDLCPP_UNKNOWN_INPUTLESS_INSTANCE_TYPES", conn.type) && !isOutput) {
                    continue;
                }
                portName += isOutput ? "_out" : "_in";
            }
            if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES"); portTypes.count(conn.type + "." + conn.port)) {
                auto spec = portTypes[conn.type + "." + conn.port];
                auto sep = spec.find(':');
                auto direction = trim(sep == std::string::npos ? std::string() : spec.substr(0, sep));
                auto configuredType = trim(sep == std::string::npos ? spec : spec.substr(sep + 1));
                if (direction == "output") {
                    isOutput = true;
                    if (!hasSuffix(portName, "_out")) {
                        portName = conn.port + "_out";
                    }
                }
                else if (direction == "input") {
                    isOutput = false;
                    if (!hasSuffix(portName, "_in")) {
                        portName = conn.port + "_in";
                    }
                }
                if (!configuredType.empty()) {
                    portType = configuredType;
                }
            }
            if (isOutput) {
                if (conn.connected) {
                    auto outExpr = conn.instance + "." + portName + "()";
                    if (addConcatOutputAssignments(m, conn.lhs, outExpr)) {
                        continue;
                    }
                    addCombAssignment(m, baseFromLValueText(conn.lhs), conn.lhs, outExpr);
                }
            }
            else {
                auto rhs = conn.connected ? conn.rhs : (portType == "bool" ? "false" : portType + "(0)");
                auto sourceTypeBeforeLateBind = expressionStorageType(m, rhs);
                auto boundName = bridgeBoundName(m, rhs);
                auto bridge = !boundName.empty() && m.assignExprByBase.count(boundName) &&
                              isAssignDrivenVar(m, boundName);
                if (bridge) {
                    m.bridgeAssignVars.insert(boundName);
                    rhs = m.assignExprByBase[boundName];
                }
                auto rawRhsBase = baseFromLValueText(rhs);
                rhs = lateBindExpr(m, rhs, "");
                auto wrapper = isSimpleCombRef(rhs) ? "_ASSIGN_COMB" : "_ASSIGN";
                if (!rawRhsBase.empty() && m.combAssignedVars.count(rawRhsBase) && !m.seqAssignedVars.count(rawRhsBase) && !m.combMethodByBase.count(rawRhsBase) && hasRuntimeAssignLines(m)) {
                    rhs = "(assign_comb_func(), " + rhs + ")";
                    wrapper = "_ASSIGN_COMB";
                }
                auto target = trim(portType);
                if (target.rfind("logic<", 0) == 0 && target.back() == '>' &&
                    ((sourceTypeBeforeLateBind.rfind("array<", 0) == 0 || sourceTypeBeforeLateBind.rfind("std::array<", 0) == 0) ||
                     rhs.find("_func()") != std::string::npos)) {
                    rhs = target + "(" + rhs + ")";
                    wrapper = "_ASSIGN";
                }
                else {
                    rhs = adaptInputPortRhs(m, portType, rhs);
                }
                m.assignLines.push_back(conn.instance + "." + portName + " = " + wrapper + "(" + rhs + ");");
            }
        }
    }

    void wireAssignsToPorts(ModuleGen& m)
    {
        for (auto& a : m.assigns) {
            for (auto& p : m.ports) {
                if (p.name == a.first && p.init.empty()) {
                    auto rhs = a.second;
                    for (auto& f : m.methods) {
                        if (!f.returnName.empty() && f.returnName == rhs) {
                            rhs = f.name + "()";
                        }
                    }
                    p.init = std::string(" = ") + (m.varNames.count(a.second) ? "_ASSIGN_REG( " : "_ASSIGN( ") + rhs + " )";
                }
            }
        }
    }

    std::string lateBindExpr(const ModuleGen& mod, const std::string& expr, const std::string& exclude)
    {
        std::string out;
        for (size_t i = 0; i < expr.size();) {
            auto c = expr[i];
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                auto start = i;
                ++i;
                while (i < expr.size() &&
                       (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
                    ++i;
                }
                auto id = expr.substr(start, i - start);
                auto prev = start == 0 ? '\0' : expr[start - 1];
                auto next = i < expr.size() ? expr[i] : '\0';
                if (next == '.' && expr.compare(i, 6, ".bits(") == 0 && mod.types.count(id)) {
                    auto width = typeWidth(mod.types.at(id));
                    auto type = mod.types.at(id);
                    if (width.empty() && type.rfind("array<", 0) == 0) {
                        auto args = memoryArgs("memory<" + type.substr(6, type.size() - 7) + ">");
                        if (args.size() == 2) {
                            width = typeWidth(args[0]);
                        }
                    }
                    if (!width.empty() && type.rfind("logic<", 0) != 0 && type.rfind("reg<logic<", 0) != 0) {
                        out += "logic<" + width + ">(" + id + ")";
                        continue;
                    }
                }
                auto it = mod.wireMap.find(id);
                if (it != mod.wireMap.end() && id != exclude && prev != '.' && next != '(') {
                    out += it->second + "()";
                }
                else {
                    bool replacedOutput = false;
                    for (auto& outPort : mod.outputPortCppNames) {
                        auto& svName = outPort.first;
                        auto& cppName = outPort.second;
                        auto oldReg = cppName + "_reg";
                        auto oldStorage = cppName + "_storage";
                        auto oldComb = cppName + "_comb";
                        if (prev != '.' && next != '(' &&
                            (id == svName || id == cppName || id == oldReg || id == oldStorage || id == oldComb)) {
                            if (isAssignOnlyOutput(mod, svName)) {
                                out += cppName + "()";
                                replacedOutput = true;
                                break;
                            }
                            out += outputStorageName(mod, svName);
                            replacedOutput = true;
                            break;
                        }
                    }
                    if (!replacedOutput) {
                        auto portIt = mod.portCppNames.find(id);
                        if (portIt != mod.portCppNames.end() && id != exclude && prev != '.' && next != '(') {
                            out += portIt->second + "()";
                        }
                        else if (id != exclude && prev != '.' && next != '(') {
                            bool replacedComb = false;
                            auto combBaseIt = mod.combMethodByBase.find(id);
                            if (combBaseIt != mod.combMethodByBase.end() && !mod.seqAssignedVars.count(id)) {
                                out += mod.methods[combBaseIt->second].name + "()";
                                replacedComb = true;
                            }
                            else {
                                for (auto& combItem : mod.combMethodByBase) {
                                    if (id == combStorageName(mod, combItem.first) && id != combStorageName(mod, exclude) && !mod.seqAssignedVars.count(combItem.first)) {
                                        out += mod.methods[combItem.second].name + "()";
                                        replacedComb = true;
                                        break;
                                    }
                                }
                            }
                            if (!replacedComb) {
                                if (isAssignDrivenVar(mod, id)) {
                                    out += id + "()";
                                }
                                else {
                                    out += id;
                                }
                            }
                        }
                        else {
                            out += id;
                        }
                    }
                }
            }
            else {
                out += c;
                ++i;
            }
        }
        for (auto& item : mod.types) {
            auto high = "$high(" + item.first + ")";
            auto width = typeWidth(item.second);
            if (!width.empty()) {
                replaceAll(out, high, "(" + width + "-1)");
            }
        }
        for (auto& item : mod.types) {
            auto type = item.second;
            if (type.rfind("array<", 0) != 0) {
                continue;
            }
            auto args = memoryArgs("memory<" + type.substr(6, type.size() - 7) + ">");
            if (args.size() != 2) {
                continue;
            }
            auto width = typeWidth(args[0]);
            if (width.empty()) {
                continue;
            }
            auto needle = item.first + "[";
            for (size_t pos = 0; (pos = out.find(needle, pos)) != std::string::npos;) {
                size_t close = std::string::npos;
                int depth = 0;
                for (size_t j = pos + item.first.size(); j < out.size(); ++j) {
                    if (out[j] == '[') {
                        ++depth;
                    }
                    else if (out[j] == ']') {
                        --depth;
                        if (depth == 0) {
                            close = j;
                            break;
                        }
                    }
                }
                auto isIdent = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
                if ((pos > 0 && isIdent(out[pos - 1])) || close == std::string::npos || out.compare(close + 1, 6, ".bits(") != 0) {
                    pos += needle.size();
                    continue;
                }
                out.insert(pos, "logic<" + width + ">(");
                close += width.size() + 8;
                out.insert(close + 1, ")");
                pos = close + width.size() + 10;
            }
        }
        return out;
    }

    std::string lateBindCombRhs(const ModuleGen& mod, const MethodGen& method, const std::string& line)
    {
        auto comb = method.name.find("_comb_func") != std::string::npos;
        auto trimmed = trim(line);
        if (trimmed.rfind("case ", 0) == 0 || trimmed.rfind("default:", 0) == 0 ||
            trimmed == "{" || trimmed == "}" || trimmed == "else {") {
            return line;
        }
        if (trimmed.rfind("if ", 0) == 0 || trimmed.rfind("if(", 0) == 0 ||
            trimmed.rfind("for ", 0) == 0 || trimmed.rfind("for(", 0) == 0 ||
            trimmed.rfind("switch ", 0) == 0 || trimmed.rfind("switch(", 0) == 0) {
            auto controlLine = line;
            if (comb && !method.returnName.empty() && !method.returnBase.empty()) {
                replaceIdentifierAll(controlLine, method.returnBase, method.returnName);
            }
            return lateBindExpr(mod, controlLine, "");
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return lateBindExpr(mod, line, "");
        }
        auto lhs = line.substr(0, eq);
        auto rhs = line.substr(eq + 1);
        auto lhsBase = baseFromLValueText(lhs);
        if (comb && !method.returnName.empty() && !lhsBase.empty() && lhsBase == method.returnBase) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), method.returnName);
            }
        }
        else if (comb && !lhsBase.empty() && mod.combMethodByBase.count(lhsBase) && !mod.seqAssignedVars.count(lhsBase)) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), combStorageName(mod, lhsBase));
            }
        }
        else if (!comb && !lhsBase.empty() && mod.combMethodByBase.count(lhsBase) && !mod.seqAssignedVars.count(lhsBase)) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), combStorageName(mod, lhsBase));
            }
        }
        if (comb && !method.returnName.empty() && !method.returnBase.empty()) {
            replaceIdentifierAll(rhs, method.returnBase, method.returnName);
        }
        auto boundLhs = lateBindExpr(mod, lhs, lhsBase);
        auto trimmedLhs = trim(lhs);
        for (auto& outPort : mod.outputPortCppNames) {
            if (trimmedLhs == outPort.second && isAssignOnlyOutput(mod, outPort.first)) {
                boundLhs = lhs;
                break;
            }
        }
        auto rhsExclude = (comb && !method.returnName.empty()) ? method.returnName : std::string();
        return boundLhs + "=" + lateBindExpr(mod, rhs, rhsExclude);
    }

    void emitMethod(std::ofstream& out, const ModuleGen& mod, const MethodGen& m)
    {
        if (m.name.find("_comb_func") != std::string::npos && !m.returnName.empty()) {
            auto typeIt = mod.combReturnTypes.find(m.returnName);
            auto type = typeIt != mod.combReturnTypes.end() ? typeIt->second : std::string("auto");
            auto plainComb = emitPlainCombMethod(mod, m);
	            if (plainComb) {
	                out << "    " << type << "& " << m.name << "()\n    {\n";
	            }
	            else {
	                out << "    _LAZY_COMB(" << m.returnName << ", " << type << ")\n";
	            }
	            if (plainComb) {
	                out << "        " << m.name << "_active = true;\n";
	            }
	            for (auto& import : mod.imports) {
	                out << "        using namespace " << import << ";\n";
	            }
            for (auto& l : m.body) {
                auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, m, l)));
                if (!m.returnName.empty()) {
                    replaceAll(emittedLine, m.returnName + "_func()", m.returnName);
                }
                out << "        " << emittedLine << "\n";
            }
            if (auto injections = configuredTextMap("HDLCPP_COMB_RETURN_INJECTIONS"); injections.count(mod.name + "|" + m.returnName)) {
                std::stringstream ss(injections[mod.name + "|" + m.returnName]);
                std::string injectionLine;
                while (std::getline(ss, injectionLine)) {
                    out << "        " << injectionLine << "\n";
                }
            }
	            if (plainComb) {
	                out << "        " << m.name << "_active = false;\n";
	            }
	            out << "        return " << m.returnName << ";\n";
            out << "    }\n\n";
            return;
        }
        if (mod.isPackage) {
            auto overrides = configuredTextMap("HDLCPP_PACKAGE_METHOD_OVERRIDES");
            if (auto it = overrides.find(m.name); it != overrides.end()) {
                std::stringstream ss(it->second);
                std::string line;
                while (std::getline(ss, line)) {
                    out << line << "\n";
                }
                out << "\n";
                return;
            }
        }
        out << "    " << (mod.isPackage ? "inline constexpr " : "") << m.ret << " " << m.name << "(" << m.args << ")\n    {\n";
        for (auto& import : mod.imports) {
            out << "        using namespace " << import << ";\n";
        }
        for (auto& l : m.body) {
            if (mod.isPackage) {
                auto line = l;
                auto trimmed = trim(line);
                if ((m.ret == "unsigned" || m.ret == "uint64_t") && trimmed.rfind("return ", 0) == 0 && trimmed != "return {};") {
                    auto expr = trim(trimmed.substr(std::strlen("return ")));
                    if (!expr.empty() && expr.back() == ';') {
                        expr.pop_back();
                    }
                    expr = trim(stripLogicLiteralCasts(expr));
                    line = "return static_cast<" + m.ret + ">(" + expr + ");";
                }
                out << "        " << line << "\n";
            }
            else {
                out << "        " << repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, m, l))) << "\n";
            }
        }
        if (!m.returnName.empty()) {
            out << "        return " << m.returnName;
            out << ";\n";
        }
        else if (m.ret != "void") {
            bool hasReturn = false;
            bool hasImplicitOut = false;
            for (auto& l : m.body) {
                auto t = trim(l);
                if (t.rfind("return ", 0) == 0 || t == "return;") {
                    hasReturn = true;
                }
                if (t.find(" out") != std::string::npos || t.rfind("out", 0) == 0) {
                    hasImplicitOut = true;
                }
            }
            if (!hasReturn) {
                out << "        return " << (hasImplicitOut ? "out" : "{}") << ";\n";
            }
        }
        out << "    }\n\n";
    }

};

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: hdlcpp <file.sv>\n";
        return 1;
    }

    auto treeOrError = SyntaxTree::fromFile(argv[1]);
    if (!treeOrError) {
        std::cerr << "failed to parse " << argv[1] << "\n";
        return 1;
    }

    Converter converter;
    (*treeOrError)->root().visit(converter);
    converter.write(argv[1]);
    return 0;
}

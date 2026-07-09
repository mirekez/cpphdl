    std::string emitArraySliceExpr(const std::string& base, const std::string& width, const std::string& first)
    {
        return "([&]() { auto&& __cpphdl_slice_src = (" + base + "); "
            "array<std::remove_cvref_t<decltype(std::as_const(__cpphdl_slice_src)[0])>," + width + "> __cpphdl_slice_out{}; "
            "for (uint64_t __cpphdl_slice_i = 0; __cpphdl_slice_i < (uint64_t)(" + width + "); ++__cpphdl_slice_i) { "
            "__cpphdl_slice_out[__cpphdl_slice_i] = __cpphdl_slice_src[(uint64_t)(" + first + ") + __cpphdl_slice_i]; "
            "} return __cpphdl_slice_out; }())";
    }

    std::string emitSvBitsValue(const std::string& base, const ElementSelectSyntax& select)
    {
        if (!select.selector || !RangeSelectSyntax::isKind(select.selector->kind)) {
            return base;
        }
        auto& r = select.selector->as<RangeSelectSyntax>();
        auto rangeOp = tok(r.range);
        auto width = selectTemplateWidth(select);
        auto dynamicWidth = textMentionsRuntimeIndex(width) || textMentionsRuntimeIndex(select.toString());
        for (auto& var : loopVars) {
            if (isIdentifierUsed(width, var)) {
                dynamicWidth = true;
                break;
            }
        }
        static constexpr const char* runtimeIndexNames[] = {
            "i", "j", "k", "x", "z", "w",
            "i_gen", "j_gen", "k_gen", "x_gen", "z_gen", "w_gen",
            "gen_i", "gen_j", "gen_k", "gen_x", "gen_z", "gen_w"
        };
        for (auto* var : runtimeIndexNames) {
            if (isIdentifierUsed(width, var)) {
                dynamicWidth = true;
                break;
            }
        }
        if (rangeOp == "+:" || rangeOp == "+") {
            auto left = emitIndexExpr(*r.left);
            auto count = emitIndexExpr(*r.right);
            if (dynamicWidth) {
                return "cpphdl::sv_bits_runtime(" + base + ",(" + left + ")+(" + count + ")-1," + left + ")";
            }
            return "cpphdl::sv_bits<" + width + ">(" + base + ",(" + left + ")+(" + count + ")-1," + left + ")";
        }
        if (rangeOp == "-:" || rangeOp == "-") {
            auto left = emitIndexExpr(*r.left);
            auto count = emitIndexExpr(*r.right);
            if (dynamicWidth) {
                return "cpphdl::sv_bits_runtime(" + base + "," + left + ",(" + left + ")-(" + count + ")+1)";
            }
            return "cpphdl::sv_bits<" + width + ">(" + base + "," + left + ",(" + left + ")-(" + count + ")+1)";
        }
        if (dynamicWidth) {
            return "cpphdl::sv_bits_runtime(" + base + "," + emitIndexExpr(*r.left) + "," +
                emitIndexExpr(*r.right) + ")";
        }
        return "cpphdl::sv_bits<" + width + ">(" + base + "," + emitIndexExpr(*r.left) + "," +
            emitIndexExpr(*r.right) + ")";
    }

    std::string emitSideEffectRead(const ModuleGen& modRef, const std::string& driver, const std::string& name)
    {
        if (driver.empty()) {
            return name;
        }
        (void)modRef;
        return "((" + driver + "(), " + name + "))";
    }

    std::string stripDecltypeExpressions(std::string text)
    {
        size_t search = 0;
        while (true) {
            auto pos = text.find("decltype(", search);
            if (pos == std::string::npos) {
                break;
            }
            auto open = pos + std::string("decltype").size();
            auto close = matchingParenClose(text, open);
            if (close == std::string::npos) {
                search = pos + 1;
                continue;
            }
            text.erase(pos, close - pos + 1);
            search = pos;
        }
        return text;
    }

    bool referencesDynamicCpphdlGetter(const std::string& expr)
    {
        auto valueExpr = stripDecltypeExpressions(expr);
        return valueExpr.find("_in()") != std::string::npos ||
               valueExpr.find("_out()") != std::string::npos ||
               valueExpr.find("_comb_func()") != std::string::npos;
    }

    bool isStandaloneCombEvalStatement(std::string line)
    {
        line = trim(std::move(line));
        if (!line.empty() && line.back() == ';') {
            line.pop_back();
            line = trim(std::move(line));
        }
        return isSimpleCombRef(line);
    }

    std::string repairProxyValueInitAssignment(std::string line)
    {
        auto findAssignmentEq = [](const std::string& text) {
            int paren = 0;
            int bracket = 0;
            int brace = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                char c = text[i];
                if (c == '(') {
                    ++paren;
                }
                else if (c == ')' && paren > 0) {
                    --paren;
                }
                else if (c == '[') {
                    ++bracket;
                }
                else if (c == ']' && bracket > 0) {
                    --bracket;
                }
                else if (c == '{') {
                    ++brace;
                }
                else if (c == '}' && brace > 0) {
                    --brace;
                }
                else if (c == '=' && paren == 0 && bracket == 0 && brace == 0 &&
                         (i == 0 || (text[i - 1] != '=' && text[i - 1] != '!' && text[i - 1] != '<' && text[i - 1] != '>' &&
                                     text[i - 1] != '|' && text[i - 1] != '&' && text[i - 1] != '^' && text[i - 1] != '+' &&
                                     text[i - 1] != '-' && text[i - 1] != '*' && text[i - 1] != '/' && text[i - 1] != '%')) &&
                         (i + 1 >= text.size() || text[i + 1] != '=')) {
                    return i;
                }
            }
            return std::string::npos;
        };
        auto eq = findAssignmentEq(line);
        if (eq == std::string::npos) {
            return line;
        }
        auto lhs = trim(line.substr(0, eq));
        auto firstBracket = lhs.find('[');
        if (firstBracket == std::string::npos || lhs.substr(0, firstBracket).find('.') != std::string::npos) {
            return line;
        }
        auto rhs = trim(line.substr(eq + 1));
        if (!rhs.empty() && rhs.back() == ';') {
            rhs = trim(rhs.substr(0, rhs.size() - 1));
        }
        if (rhs == "std::remove_cvref_t<decltype(" + lhs + ")>{}") {
            line.replace(eq + 1, std::string::npos, " 0;");
        }
        return line;
    }

    void emitBodyLine(std::ofstream& out, const std::string& line, bool dropStandaloneCombEval = false)
    {
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part)) {
            part = repairProxyValueInitAssignment(part);
            if (dropStandaloneCombEval && isStandaloneCombEvalStatement(part)) {
                continue;
            }
            out << "        " << part << "\n";
        }
    }

    void emitGuardOpen(std::ofstream& out, const std::vector<std::string>& guards, const std::string& indent)
    {
        for (const auto& guard : guards) {
            out << indent << guard << "\n";
        }
    }

    void emitGuardClose(std::ofstream& out, const std::vector<std::string>& guards, const std::string& indent)
    {
        for (size_t i = 0; i < guards.size(); ++i) {
            out << indent << "}\n";
        }
    }

    std::string resolveSelectType(std::string type)
    {
        type = unwrapRegType(trim(std::move(type)));
        for (size_t guard = 0; mod && guard < 16; ++guard) {
            auto it = mod->types.find(type);
            if (it == mod->types.end() || it->second == type) {
                break;
            }
            type = unwrapRegType(trim(it->second));
        }
        return type;
    }

    std::vector<std::string> selectArrayArgs(const std::string& type)
    {
        auto args = templateArgsFor(type, "array");
        if (args.empty()) {
            args = templateArgsFor(type, "std::array");
        }
        return args;
    }

    bool integralSelectType(const std::string& type)
    {
        return type == "bool" || type == "unsigned" || type == "u32" || type == "uint32_t" ||
            type == "u64" || type == "uint64_t";
    }

    std::string emitTypedSelectChain(std::string s, const std::string& base,
                                     std::string currentType,
                                     const std::vector<const ElementSelectSyntax*>& selectors,
                                     bool lvalue)
    {
        currentType = resolveSelectType(std::move(currentType));
        auto memorySelect = !currentType.empty() && memoryLikeType(currentType);
        auto memoryScalar = memorySelect && scalarMemory(currentType);
        size_t arrayLevel = 0;
        for (size_t idx = 0; idx < selectors.size(); ++idx) {
            auto sel = selectors[idx];
            if (!sel || !sel->selector) {
                continue;
            }
            auto arrayArgs = selectArrayArgs(currentType);
            if (!arrayArgs.empty() && sel->selector->kind == SyntaxKind::BitSelect) {
                auto index = arrayIndexExpr(base, arrayLevel, *sel->selector->as<BitSelectSyntax>().expr);
                s += "[(unsigned)(" + index + ")]";
                currentType = arrayArgs.size() >= 2 ? resolveSelectType(arrayArgs[0]) : std::string();
                ++arrayLevel;
                memorySelect = !currentType.empty() && memoryLikeType(currentType);
                memoryScalar = memorySelect && scalarMemory(currentType);
                continue;
            }
            if (!arrayArgs.empty() && RangeSelectSyntax::isKind(sel->selector->kind)) {
                auto& r = sel->selector->as<RangeSelectSyntax>();
                s = emitArraySliceExpr(s, selectTemplateWidth(*sel),
                    arrayIndexExpr(base, arrayLevel, emitNumericExpr(*r.right)));
                currentType = arrayArgs.size() >= 2 ?
                    "array<" + arrayArgs[0] + "," + selectTemplateWidth(*sel) + ">" : std::string();
                ++arrayLevel;
                memorySelect = !currentType.empty() && memoryLikeType(currentType);
                memoryScalar = memorySelect && scalarMemory(currentType);
                continue;
            }
            if (currentType.empty() && !lvalue && idx + 1 < selectors.size() &&
                sel->selector->kind == SyntaxKind::BitSelect) {
                auto index = emitIndexExpr(*sel->selector->as<BitSelectSyntax>().expr);
                s += "[(unsigned)(" + arrayIndexExpr(base, arrayLevel, index) + ")]";
                ++arrayLevel;
                continue;
            }
            if (!lvalue && integralSelectType(currentType) && RangeSelectSyntax::isKind(sel->selector->kind)) {
                s = emitSvBitsValue(s, *sel);
                currentType.clear();
                memorySelect = false;
                memoryScalar = false;
                continue;
            }
            if (currentType.empty() && !lvalue && RangeSelectSyntax::isKind(sel->selector->kind)) {
                s = emitSvBitsValue(s, *sel);
                memorySelect = false;
                memoryScalar = false;
                continue;
            }
            s = emitSelectOn(s, *sel, lvalue, memorySelect, memoryScalar);
            if (sel->selector->kind == SyntaxKind::BitSelect && !selectArrayArgs(currentType).empty()) {
                auto args = selectArrayArgs(currentType);
                currentType = args.size() >= 2 ? resolveSelectType(args[0]) : std::string();
            }
            else {
                currentType.clear();
            }
            memorySelect = !currentType.empty() && memoryLikeType(currentType);
            memoryScalar = memorySelect && scalarMemory(currentType);
        }
        return s;
    }

    std::string emitMemberBaseExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return "(" + emitMemberBaseExpr(*expr.as<ParenthesizedExpressionSyntax>().expression) + ")";
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto base = tok(n.identifier);
            auto s = mod->outputPortCppNames.count(base) ?
                (isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : emitCombOutputRead(*mod, base)) :
                (mod->portCppNames.count(base) ? mod->portCppNames[base] + "()" : (isAssignDrivenVar(*mod, base) ? base + "()" : base));
            std::vector<const ElementSelectSyntax*> selectors;
            for (auto sel : n.selectors) {
                selectors.push_back(sel);
            }
            auto currentType = mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
            return emitTypedSelectChain(s, base, currentType, selectors, true);
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            return emitSelectOn(emitMemberBaseExpr(*e.left), *e.select, true);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitMemberBaseExpr(*e.left) + "." + cppIdent(tok(e.name));
        }
        return emitExpr(expr);
    }

    std::string emitExpr(const ExpressionSyntax& expr)
    {
        auto isStringLiteral = [](const ExpressionSyntax& e) {
            auto text = trim(exprText(e.toString()));
            return !text.empty() && text.front() == '"';
        };
        if (isStringLiteral(expr)) {
            return trim(exprText(expr.toString()));
        }
        if (expr.kind == SyntaxKind::InsideExpression) {
            auto& inside = expr.as<InsideExpressionSyntax>();
            return emitInsideList(*inside.expr, inside.ranges->valueRanges);
        }
        if (expr.kind == SyntaxKind::ValueRangeExpression) {
            return "false";
        }
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (auto localExpr = lookupGenerateLocalExpr(name)) {
                return *localExpr;
            }
            if (mod->outputPortCppNames.count(name)) {
                if (isAssignOnlyOutput(*mod, name)) {
                    return mod->outputPortCppNames[name] + "()";
                }
                return emitCombOutputRead(*mod, name);
            }
            auto isRegisterObject = mod->types.count(name) && mod->types.at(name).rfind("reg<", 0) == 0;
            if (!isRegisterObject && mod->wireMap.count(name) &&
                moduleMethodExists(*mod, mod->wireMap[name])) {
                return mod->wireMap[name] + "()";
            }
            if (!isRegisterObject && mod->combSideEffectDriver.count(name)) {
                return emitSideEffectRead(*mod, mod->combSideEffectDriver[name], name);
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
                (isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : emitCombOutputRead(*mod, base)) :
                (mod->portCppNames.count(base) ? mod->portCppNames[base] + "()" : (isAssignDrivenVar(*mod, base) ? base + "()" : base));
            auto resolveAliasType = [&](std::string type) {
                type = unwrapRegType(trim(std::move(type)));
                for (size_t guard = 0; mod && guard < 16; ++guard) {
                    auto it = mod->types.find(type);
                    if (it == mod->types.end() || it->second == type) {
                        break;
                    }
                    type = unwrapRegType(trim(it->second));
                }
                return type;
            };
            auto arrayArgsForType = [&](const std::string& type) {
                auto args = templateArgsFor(type, "array");
                if (args.empty()) {
                    args = templateArgsFor(type, "std::array");
                }
                return args;
            };
            auto currentType = resolveAliasType(mod->types.count(base) ? mod->types[base] : lookupLocalType(base));
            if (currentType.empty() && mod->outputPortCppNames.count(base)) {
                currentType = resolveAliasType(outputStorageType(*mod, base, mod->outputPortCppNames[base]));
            }
            if (currentType.empty() && mod) {
                auto cppIt = mod->portCppNames.find(base);
                for (const auto& port : mod->ports) {
                    if (port.name == base || (cppIt != mod->portCppNames.end() && port.name == cppIt->second)) {
                        currentType = resolveAliasType(port.type);
                        break;
                    }
                }
            }
            auto memorySelect = !currentType.empty() && memoryLikeType(currentType);
            auto memoryScalar = memorySelect && scalarMemory(currentType);
            size_t arrayLevel = 0;
            for (auto sel : n.selectors) {
                auto baseType = mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
                auto unknownSingleRangeScalar = currentType.empty() && n.selectors.size() == 1 &&
                    !mod->portCppNames.count(base) && !mod->outputPortCppNames.count(base) &&
                    !isAssignDrivenVar(*mod, base) && sel->selector && RangeSelectSyntax::isKind(sel->selector->kind);
                auto integralBase = loopVars.count(base) || unknownSingleRangeScalar ||
                    baseType == "bool" || baseType == "unsigned" ||
                    baseType == "u32" || baseType == "uint32_t" || baseType == "u64" || baseType == "uint64_t";
                if (integralBase && sel->selector && RangeSelectSyntax::isKind(sel->selector->kind)) {
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
                if (sel->selector && RangeSelectSyntax::isKind(sel->selector->kind) &&
                    currentType.find("decltype(") != std::string::npos) {
                    auto& r = sel->selector->as<RangeSelectSyntax>();
                    auto bounds = indexedRangeBounds(r);
                    auto width = selectTemplateWidth(*sel);
                    s = "logic<" + width + ">(logic<cpphdl::type_width<" + currentType + ">()>(" +
                        s + ").bits(" + bounds.first + "," + bounds.second + "))";
                    key = emitSelectOn(key, *sel, true);
                    currentType = "logic<" + width + ">";
                    memorySelect = false;
                    memoryScalar = false;
                    continue;
                }
                auto arrayArgs = arrayArgsForType(currentType);
                if (!arrayArgs.empty() && sel->selector && RangeSelectSyntax::isKind(sel->selector->kind)) {
                    auto& r = sel->selector->as<RangeSelectSyntax>();
                    s = emitArraySliceExpr(s, selectTemplateWidth(*sel), arrayIndexExpr(base, arrayLevel, emitNumericExpr(*r.right)));
                    key = emitSelectOn(key, *sel, true);
                    currentType = arrayArgs.size() >= 2 ? "array<" + arrayArgs[0] + "," + selectTemplateWidth(*sel) + ">" : std::string();
                    ++arrayLevel;
                    memorySelect = !currentType.empty() && memoryLikeType(currentType);
                    memoryScalar = memorySelect && scalarMemory(currentType);
                    continue;
                }
                arrayArgs = arrayArgsForType(currentType);
                if (!arrayArgs.empty() && sel->selector &&
                    sel->selector->kind == SyntaxKind::BitSelect) {
                    auto index = arrayIndexExpr(base, arrayLevel, *sel->selector->as<BitSelectSyntax>().expr);
                    s += "[(unsigned)(" + index + ")]";
                    key = emitSelectOn(key, *sel, true);
                    currentType = arrayArgs.size() >= 2 ? resolveAliasType(arrayArgs[0]) : std::string();
                    ++arrayLevel;
                    memorySelect = !currentType.empty() && memoryLikeType(currentType);
                    memoryScalar = memorySelect && scalarMemory(currentType);
                    continue;
                }
                if (sel->selector && sel->selector->kind == SyntaxKind::BitSelect) {
                    auto currentTypePrimitive = currentType.empty() || currentType == "bool" ||
                        currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" ||
                        currentType == "u64" || currentType == "uint64_t" ||
                        currentType.rfind("logic<", 0) == 0 || currentType.rfind("u<", 0) == 0;
                    if (!currentTypePrimitive) {
                        auto index = emitNumericExpr(*sel->selector->as<BitSelectSyntax>().expr);
                        s += "[(unsigned)(" + index + ")]";
                        key = emitSelectOn(key, *sel, true);
                        currentType = "std::remove_cvref_t<decltype(" + s + ")>";
                        memorySelect = false;
                        memoryScalar = false;
                        continue;
                    }
                    auto valueType = unwrappedValueType(currentType);
                    auto valueWidth = foldWidth(typeWidth(valueType));
                    if (!valueWidth.empty() && valueType.rfind("array<", 0) != 0 && valueType.rfind("std::array<", 0) != 0 &&
                        valueType != "bool" && valueType != "unsigned" && valueType != "u32" &&
                        valueType != "uint32_t" && valueType != "u64" && valueType != "uint64_t") {
                        auto index = bitIndexArg(emitIndexExpr(*sel->selector->as<BitSelectSyntax>().expr));
                        s = emitLogicBitSelectValue(s, valueWidth, index);
                        key = emitSelectOn(key, *sel, true);
                        currentType = "logic<1>";
                        memorySelect = false;
                        memoryScalar = false;
                        continue;
                    }
                }
                s = emitSelectOn(s, *sel, false, memorySelect, memoryScalar);
                key = emitSelectOn(key, *sel, true);
                arrayArgs = arrayArgsForType(currentType);
                if (!arrayArgs.empty() && sel->selector && sel->selector->kind == SyntaxKind::BitSelect) {
                    currentType = arrayArgs.size() >= 2 ? resolveAliasType(arrayArgs[0]) : std::string();
                }
                else {
                    currentType.clear();
                }
                memorySelect = !currentType.empty() && memoryLikeType(currentType);
                memoryScalar = memorySelect && scalarMemory(currentType);
            }
            auto keyIsRegisterObject = mod->types.count(key) && mod->types.at(key).rfind("reg<", 0) == 0;
            if (!keyIsRegisterObject && mod->wireMap.count(key) &&
                moduleMethodExists(*mod, mod->wireMap[key])) {
                return mod->wireMap[key] + "()";
            }
            return s;
        }
        if (expr.kind == SyntaxKind::ScopedName) {
            auto& scoped = expr.as<ScopedNameSyntax>();
            if (tok(scoped.separator) == ".") {
                std::string member;
                std::vector<const ElementSelectSyntax*> selectors;
                if (scoped.right->kind == SyntaxKind::IdentifierName) {
                    member = cppIdent(tok(scoped.right->as<IdentifierNameSyntax>().identifier));
                }
                else if (scoped.right->kind == SyntaxKind::IdentifierSelectName) {
                    auto& right = scoped.right->as<IdentifierSelectNameSyntax>();
                    member = cppIdent(tok(right.identifier));
                    for (auto sel : right.selectors) {
                        selectors.push_back(sel);
                    }
                }
                if (!member.empty()) {
                    auto s = emitMemberBaseExpr(scoped.left->as<ExpressionSyntax>()) + "." + member;
                    auto currentType = fieldTypeFor(exprType(scoped.left->as<ExpressionSyntax>()), member);
                    return emitTypedSelectChain(s, member, currentType, selectors, false);
                }
            }
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
            auto resolveBaseType = [&](std::string type) {
                type = unwrapRegType(trim(std::move(type)));
                for (size_t guard = 0; mod && guard < 16; ++guard) {
                    auto it = mod->types.find(type);
                    if (it == mod->types.end() || it->second == type) {
                        break;
                    }
                    type = unwrapRegType(trim(it->second));
                }
                return type;
            };
            auto arrayArgsForType = [&](const std::string& type) {
                auto args = templateArgsFor(type, "array");
                if (args.empty()) {
                    args = templateArgsFor(type, "std::array");
                }
                return args;
            };
            auto baseType = resolveBaseType(exprType(*e.left));
            if (baseType.empty()) {
                baseType = resolveBaseType(!base.empty() && mod->types.count(base) ? mod->types[base] : lookupLocalType(base));
            }
            if (baseType.empty() && mod && !base.empty() && mod->outputPortCppNames.count(base)) {
                baseType = resolveBaseType(outputStorageType(*mod, base, mod->outputPortCppNames[base]));
            }
            if (baseType.empty() && mod && !base.empty()) {
                auto cppIt = mod->portCppNames.find(base);
                for (const auto& port : mod->ports) {
                    if (port.name == base || (cppIt != mod->portCppNames.end() && port.name == cppIt->second)) {
                        baseType = resolveBaseType(port.type);
                        break;
                    }
                }
            }
            if (e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect &&
                (baseType == "bool" || baseType == "unsigned" || baseType == "u32" ||
                 baseType == "uint32_t" || baseType == "u64" || baseType == "uint64_t")) {
                auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
                return "logic<1>(((" + emitNumericExpr(*e.left) + ") >> (unsigned)(" + index + ")) & 1ull)";
            }
            auto baseArrayArgs = arrayArgsForType(baseType);
            if (baseArrayArgs.empty() && e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect) {
                auto baseTypePrimitive = baseType.empty() || baseType == "bool" ||
                    baseType == "unsigned" || baseType == "u32" || baseType == "uint32_t" ||
                    baseType == "u64" || baseType == "uint64_t" ||
                    baseType.rfind("logic<", 0) == 0 || baseType.rfind("u<", 0) == 0;
                if (!baseTypePrimitive) {
                    auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
                    return emitExpr(*e.left) + "[(unsigned)(" + index + ")]";
                }
            }
            if (e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect) {
                auto valueType = unwrappedValueType(baseType);
                auto valueWidth = foldWidth(typeWidth(valueType));
                if (!valueWidth.empty() && valueType.rfind("array<", 0) != 0 && valueType.rfind("std::array<", 0) != 0 &&
                    valueType != "bool" && valueType != "unsigned" && valueType != "u32" &&
                    valueType != "uint32_t" && valueType != "u64" && valueType != "uint64_t") {
                    auto index = bitIndexArg(emitIndexExpr(*e.select->selector->as<BitSelectSyntax>().expr));
                    auto selected = emitLogicBitSelectValue(emitExpr(*e.left), valueWidth, index);
                    if (!selected.empty()) {
                        return selected;
                    }
                }
            }
            if (!baseArrayArgs.empty() && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                auto index = arrayIndexExpr(base, 0, *e.select->selector->as<BitSelectSyntax>().expr);
                return emitExpr(*e.left) + "[(unsigned)(" + index + ")]";
            }
            if (!baseArrayArgs.empty() && e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind)) {
                auto& r = e.select->selector->as<RangeSelectSyntax>();
                return emitArraySliceExpr(emitExpr(*e.left), selectTemplateWidth(*e.select), arrayIndexExpr(base, 0, emitNumericExpr(*r.right)));
            }
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
            if (mod->types.count(base) && !selectArrayArgs(unwrapRegType(mod->types[base])).empty() &&
                e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect) {
                auto index = arrayIndexExpr(base, 0, *e.select->selector->as<BitSelectSyntax>().expr);
                return emitExpr(*e.left) + "[(unsigned)(" + index + ")]";
            }
            if (mod->types.count(base) && e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind)) {
                auto type = unwrapRegType(mod->types[base]);
                auto& r = e.select->selector->as<RangeSelectSyntax>();
                if (!selectArrayArgs(type).empty()) {
                    return emitArraySliceExpr(emitExpr(*e.left), selectTemplateWidth(*e.select), arrayIndexExpr(base, 0, emitNumericExpr(*r.right)));
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
            if (e.left->kind == SyntaxKind::IdentifierName) {
                auto base = tok(e.left->as<IdentifierNameSyntax>().identifier);
                if (mod->outputPortCppNames.count(base) && !isAssignOnlyOutput(*mod, base)) {
                    auto field = cppIdent(tok(e.name));
                    return emitCombOutputRead(*mod, base, field) + "." + field;
                }
            }
            return emitMemberBaseExpr(*e.left) + "." + cppIdent(tok(e.name));
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
            if ((op == "==" || op == "!=") && (isStringLiteral(*b.left) || isStringLiteral(*b.right))) {
                return "(" + emitExpr(*b.left) + " " + op + " " + rhs + ")";
            }
            if (op == "&=" || op == "|=" || op == "^=" || op == "+=" || op == "-=" || op == "<<=" || op == ">>=") {
                return emitCompoundAssignment(b, op);
            }
            if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
                return emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right, rhs);
            }
            if (op == "<" || op == "<=" || op == ">" || op == ">=" ||
                ((op == "==" || op == "!=") && (!foldWidth(exprWidth(*b.left)).empty() || !foldWidth(exprWidth(*b.right)).empty()))) {
                return "(" + emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right, rhs) + ")";
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
                auto width = bitwiseExprWidth(expr);
                if (width.empty() || width == "1") {
                    auto leftWidth = usableTemplateLogicWidth(exprWidth(*b.left));
                    auto rightWidth = usableTemplateLogicWidth(exprWidth(*b.right));
                    if (!leftWidth.empty() && leftWidth == rightWidth) {
                        width = leftWidth;
                    }
                    else if (isZeroLiteralExpr(*b.left) && !rightWidth.empty()) {
                        width = rightWidth;
                    }
                    else if (isZeroLiteralExpr(*b.right) && !leftWidth.empty()) {
                        width = leftWidth;
                    }
                    else if (!leftWidth.empty()) {
                        width = leftWidth;
                    }
                    else {
                        width = rightWidth;
                    }
                }
                auto isValueTemplateParamName = [&](const std::string& text) {
                    if (!mod) {
                        return false;
                    }
                    for (const auto& param : mod->params) {
                        if (templateParamName(param) == text && !templateParamValueType(param).empty()) {
                            return true;
                        }
                    }
                    return false;
                };
                auto usesValueTemplateParam = [&](const std::string& text) -> bool {
                    if (!mod) {
                        return false;
                    }
                    for (const auto& param : mod->params) {
                        auto name = templateParamName(param);
                        if (!templateParamValueType(param).empty() && isIdentifierUsed(text, name)) {
                            return true;
                        }
                    }
                    return false;
                };
                auto compileTimeMaskExpr = [&](const ExpressionSyntax& e) -> std::string {
                    const ExpressionSyntax* cur = &e;
                    while (cur && cur->kind == SyntaxKind::ParenthesizedExpression) {
                        cur = cur->as<ParenthesizedExpressionSyntax>().expression;
                    }
                    if (!cur) {
                        return "";
                    }
                    if (isZeroLiteralExpr(*cur)) {
                        return "0";
                    }
                    if (cur->kind == SyntaxKind::IdentifierName) {
                        auto name = tok(cur->as<IdentifierNameSyntax>().identifier);
                        if (isValueTemplateParamName(name)) {
                            return emitNumericExpr(*cur);
                        }
                    }
                    auto text = stripBalancedOuterParens(trim(exprText(cur->toString())));
                    if (isValueTemplateParamName(text)) {
                        return emitNumericExpr(*cur);
                    }
                    auto emittedMask = emitNumericExpr(*cur);
                    if (usesValueTemplateParam(emittedMask)) {
                        return emittedMask;
                    }
                    return "";
                };
                if (op == "&") {
                    auto maskWidth = width.empty() ? std::string("64") : width;
                    if (auto mask = compileTimeMaskExpr(*b.left); !mask.empty()) {
                        if (mask == "0") {
                            return "logic<" + maskWidth + ">(0)";
                        }
                        return "(((uint64_t)(" + mask + ") == 0) ? logic<" + maskWidth + ">(0) : logic<" +
                               maskWidth + ">(" + logicValueExpr(*b.left, maskWidth) + " & " +
                               logicValueExpr(*b.right, maskWidth, rhs) + "))";
                    }
                    if (auto mask = compileTimeMaskExpr(*b.right); !mask.empty()) {
                        if (mask == "0") {
                            return "logic<" + maskWidth + ">(0)";
                        }
                        return "(((uint64_t)(" + mask + ") == 0) ? logic<" + maskWidth + ">(0) : logic<" +
                               maskWidth + ">(" + logicValueExpr(*b.left, maskWidth) + " & " +
                               logicValueExpr(*b.right, maskWidth, rhs) + "))";
                    }
                }
                if (!width.empty() && width != "1") {
                    if (isNumber(width) && std::stoul(width) <= 64) {
                        return "logic<" + width + ">(" + emitNumericExpr(expr) + ")";
                    }
                    return "logic<" + width + ">(" + logicValueExpr(*b.left, width) + " " + op + " " + logicValueExpr(*b.right, width, rhs) + ")";
                }
            }
            if (op == "=") {
                return emitExpr(*b.left) + " " + op + " " + rhs;
            }
            if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
                op == "&&" || op == "||") {
                return "(" + emitExpr(*b.left) + " " + op + " " + rhs + ")";
            }
            return emitExpr(*b.left) + " " + op + " " + rhs;
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            auto op = tok(u.operatorToken);
            auto reductionOperand = [&]() {
                if (auto packed = packedNumericOperandExpr(*u.operand); !packed.empty()) {
                    return packed;
                }
                return emitExpr(*u.operand);
            };
            auto runtimeRangeWidth = [&]() -> std::string {
                const ExpressionSyntax* operand = u.operand;
                while (operand && operand->kind == SyntaxKind::ParenthesizedExpression) {
                    operand = operand->as<ParenthesizedExpressionSyntax>().expression;
                }
                if (!operand || operand->kind != SyntaxKind::ElementSelectExpression) {
                    return "";
                }
                auto& selected = operand->as<ElementSelectExpressionSyntax>();
                if (!selected.select || !selected.select->selector ||
                    !RangeSelectSyntax::isKind(selected.select->selector->kind)) {
                    return "";
                }
                auto& r = selected.select->selector->as<RangeSelectSyntax>();
                auto rangeOp = tok(r.range);
                if (rangeOp == "+:" || rangeOp == "+" || rangeOp == "-:" || rangeOp == "-") {
                    return emitNumericExpr(*r.right);
                }
                return "((" + emitNumericExpr(*r.left) + ")-(" + emitNumericExpr(*r.right) + ")+1)";
            };
            auto reduceOrExpr = [&](const std::string& value) {
                auto width = runtimeRangeWidth();
                if (width.empty()) {
                    width = exprWidth(*u.operand);
                }
                auto foldedWidth = foldWidth(width);
                if (!foldedWidth.empty()) {
                    width = foldedWidth;
                }
                if (width.empty()) {
                    return "logic<1>(((uint64_t)(" + value + ")) != 0)";
                }
                return "logic<1>((((uint64_t)(" + value + ")) & " + widthMaskExpr(width) + ") != 0)";
            };
            auto reduceAndRuntime = [&](const std::string& value) {
                auto width = runtimeRangeWidth();
                if (width.empty()) {
                    width = exprWidth(*u.operand);
                }
                auto mask = widthMaskExpr(width);
                return "logic<1>((((uint64_t)(" + value + ")) & " + mask + ") == " + mask + ")";
            };
            if (op == "|") {
                return reduceOrExpr(reductionOperand());
            }
            if (op == "~|") {
                return "logic<1>(!" + reduceOrExpr(reductionOperand()) + ")";
            }
            if (op == "&") {
                auto operand = reductionOperand();
                if (operand.find("cpphdl::sv_bits_runtime(") != std::string::npos) {
                    return reduceAndRuntime(operand);
                }
                return "cpphdl::reduce_and(" + operand + ")";
            }
            if (op == "~&") {
                auto operand = reductionOperand();
                if (operand.find("cpphdl::sv_bits_runtime(") != std::string::npos) {
                    return "!" + reduceAndRuntime(operand);
                }
                return "!cpphdl::reduce_and(" + operand + ")";
            }
            if (op == "^") {
                return "cpphdl::reduce_xor(" + reductionOperand() + ")";
            }
            if (op == "~^" || op == "^~") {
                return "!cpphdl::reduce_xor(" + reductionOperand() + ")";
            }
            if (op == "~") {
                auto width = bitwiseExprWidth(*u.operand);
                if (width.empty()) {
                    width = foldWidth(exprWidth(*u.operand));
                }
                if (!width.empty() && width != "1") {
                    return "logic<" + width + ">(~(" + logicValueExpr(*u.operand, width) + "))";
                }
                if (width == "1") {
                    return "logic<1>(" + numericBitwiseNotExpr(emitNumericExpr(*u.operand), "1") + ")";
                }
                return "logic<64>(" + numericBitwiseNotExpr(emitNumericExpr(*u.operand), width) + ")";
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
            auto rawCallee = invocationCalleeRaw(i);
            if (rawCallee == "$signed" || rawCallee == "$unsigned") {
                return emitSystemSignednessCast(i, rawCallee);
            }
            auto callee = exprText(i.left->toString());
            if (auto qualifiedCalls = configuredTextMap("HDLCPP_QUALIFIED_CALLS"); qualifiedCalls.count(callee)) {
                callee = qualifiedCalls[callee];
            }
            if (callee == "$bits") {
                return emitSystemBitsValue(i);
            }
            return callee + (i.arguments ? emitArgumentList(*i.arguments, wantsNumericFunctionArgs(callee), callee) : "()");
        }
        if (expr.kind == SyntaxKind::CastExpression) {
            auto& c = expr.as<CastExpressionSyntax>();
            auto rawTarget = trim(exprText(c.left->toString()));
            auto rawTargetValue = stripBalancedOuterParens(rawTarget);
            auto target = trim(cppTypeFromSvText(c.left->toString()));
            const ExpressionSyntax* operand = nullptr;
            if (c.right && c.right->expression) {
                operand = c.right->expression;
            }
            if (target.empty()) {
                target = rawTarget;
            }
            if (!operand) {
                return target + "{}";
            }
            auto isBuiltinValueType = [](const std::string& text) {
                return text == "bool" || text == "int" || text == "unsigned" || text == "uint64_t" ||
                       text == "uint32_t" || text == "uint16_t" || text == "uint8_t" ||
                       text == "longint" || text == "shortint";
            };
            auto isValueTemplateParamName = [&](const std::string& text) {
                if (!mod) {
                    return false;
                }
                for (const auto& param : mod->params) {
                    if (templateParamName(param) == text && !templateParamValueType(param).empty()) {
                        return true;
                    }
                }
                return false;
            };
            auto isDecimal = [](const std::string& text) {
                return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char ch) {
                    return std::isdigit(ch);
                });
            };
            bool widthCast = !isBuiltinValueType(rawTargetValue) &&
                             (isDecimal(rawTargetValue) || rawTargetValue.find('.') != std::string::npos ||
                              isValueTemplateParamName(rawTargetValue) || !constantType(rawTargetValue).empty());
            if (widthCast) {
                auto width = emitUntypedNumericExpr(*c.left);
                return logicValueExpr(*operand, width);
            }
            if (target == "bool") {
                return "static_cast<bool>(" + truthyExpr(emitExpr(*operand), exprWidth(*operand)) + ")";
            }
            if (target == "int" || target == "unsigned" || target == "uint64_t" || target == "uint32_t" ||
                target == "uint16_t" || target == "uint8_t") {
                return "static_cast<" + target + ">(" + emitNumericExpr(*operand) + ")";
            }
            auto resolvedTarget = target;
            if (mod) {
                for (size_t guard = 0; guard < 16; ++guard) {
                    auto it = mod->types.find(resolvedTarget);
                    if (it == mod->types.end() || it->second == resolvedTarget) {
                        break;
                    }
                    resolvedTarget = unwrapRegType(trim(it->second));
                }
            }
            if (resolvedTarget != target && isNumericValueType(resolvedTarget)) {
                auto width = foldWidth(logicWidth(resolvedTarget));
                if (width.empty()) {
                    width = foldWidth(typeWidth(resolvedTarget));
                }
                if (width.empty()) {
                    width = foldWidth(typeWidth(target));
                }
                if (!width.empty()) {
                    return target + "(cpphdl::pack_value<" + width + ">(" + emitExpr(*operand) + "))";
                }
                return target + "(" + emitNumericExpr(*operand) + ")";
            }
            if (target.rfind("logic<", 0) == 0 || target.rfind("u<", 0) == 0 || target == "unsigned" ||
                target == "uint64_t" || target == "uint32_t" || target == "uint16_t" || target == "uint8_t") {
                auto width = foldWidth(logicWidth(target));
                if (!width.empty()) {
                    return target + "(cpphdl::pack_value<" + width + ">(" + emitExpr(*operand) + "))";
                }
                return target + "(" + emitNumericExpr(*operand) + ")";
            }
            auto sourceType = exprType(*operand);
            auto equivalentTypeNames = [](std::string lhs, std::string rhs) {
                lhs = trim(std::move(lhs));
                rhs = trim(std::move(rhs));
                if (lhs.empty() || rhs.empty()) {
                    return false;
                }
                if (lhs == rhs) {
                    return true;
                }
                auto lhsPos = lhs.rfind("::");
                auto rhsPos = rhs.rfind("::");
                auto lhsTail = lhsPos == std::string::npos ? lhs : lhs.substr(lhsPos + 2);
                auto rhsTail = rhsPos == std::string::npos ? rhs : rhs.substr(rhsPos + 2);
                return lhsTail == rhsTail &&
                       ((lhsPos != std::string::npos) || (rhsPos != std::string::npos));
            };
            auto normalizeDecltypeType = [&](std::string type) {
                type = trim(std::move(type));
                const std::string cvrefPrefix = "std::remove_cvref_t<decltype(";
                const std::string refPrefix = "std::remove_reference_t<decltype(";
                std::string inner;
                if (type.rfind(cvrefPrefix, 0) == 0 && type.size() > cvrefPrefix.size() + 2 &&
                    type.substr(type.size() - 2) == ")>") {
                    inner = trim(type.substr(cvrefPrefix.size(), type.size() - cvrefPrefix.size() - 2));
                }
                else if (type.rfind(refPrefix, 0) == 0 && type.size() > refPrefix.size() + 2 &&
                         type.substr(type.size() - 2) == ")>") {
                    inner = trim(type.substr(refPrefix.size(), type.size() - refPrefix.size() - 2));
                }
                if (inner.empty() || !mod) {
                    return type;
                }
                if (auto it = mod->combReturnTypes.find(inner); it != mod->combReturnTypes.end()) {
                    return trim(it->second);
                }
                if (auto it = mod->types.find(inner); it != mod->types.end()) {
                    return unwrapRegType(trim(it->second));
                }
                return type;
            };
            auto resolvedSource = resolveAliasValueType(normalizeDecltypeType(sourceType));
            resolvedTarget = resolveAliasValueType(normalizeDecltypeType(resolvedTarget));
            auto sourceIsAggregate = isAggregateValueType(sourceType) ||
                resolvedSource.rfind("array<", 0) == 0 || resolvedSource.rfind("std::array<", 0) == 0;
            if ((resolvedTarget.rfind("logic<", 0) == 0 || resolvedTarget.rfind("u<", 0) == 0 ||
                 isPrimitiveWrapperType(resolvedTarget)) &&
                !isAggregateValueType(target) && !isAggregateValueType(resolvedTarget)) {
                return target + "(" + emitNumericExpr(*operand) + ")";
            }
            auto scalarAliasName = [](std::string type) {
                type = trim(std::move(type));
                auto scope = type.rfind("::");
                if (scope != std::string::npos) {
                    type = type.substr(scope + 2);
                }
                std::string lowered;
                lowered.reserve(type.size());
                for (char ch : type) {
                    lowered.push_back((char)std::tolower((unsigned char)ch));
                }
                return lowered.find("uint") != std::string::npos || lowered.find("int") != std::string::npos;
            };
            if (!sourceIsAggregate && scalarAliasName(target) && !isAggregateValueType(target)) {
                auto targetWidth = foldWidth(typeWidth(target));
                if (!targetWidth.empty()) {
                    return target + "(" + emitNumericExpr(*operand) + ")";
                }
            }
            auto targetIsAggregate = isAggregateValueType(target) ||
                resolvedTarget.rfind("array<", 0) == 0 || resolvedTarget.rfind("std::array<", 0) == 0 ||
                (mod && mod->typeParamNames.count(target));
            if (targetIsAggregate && sourceIsAggregate) {
                if ((mod && mod->typeParamNames.count(target)) ||
                    (!resolvedSource.empty() && equivalentTypeNames(resolvedSource, resolvedTarget))) {
                    return "cpphdl::sv_cast<" + target + ">(" + emitExpr(*operand) + ")";
                }
                return "cpphdl::unpack_value<" + target + ">(cpphdl::pack_value<cpphdl::type_width<" +
                    target + ">()>(" + emitExpr(*operand) + "))";
            }
            return "cpphdl::sv_cast<" + target + ">(" + emitExpr(*operand) + ")";
        }
        if (expr.kind == SyntaxKind::SignedCastExpression) {
            auto& c = expr.as<SignedCastExpressionSyntax>();
            const ExpressionSyntax* operand = nullptr;
            if (c.inner && c.inner->expression) {
                operand = c.inner->expression;
            }
            if (!operand) {
                return tok(c.signing) == "signed" ? "int64_t(0)" : "uint64_t(0)";
            }
            std::string helper = tok(c.signing) == "signed" ? "cpphdl::sv_signed" : "cpphdl::sv_unsigned";
            auto width = signednessCastWidth(*operand);
            return helper + "<(size_t)(" + width + ")>(" + emitNumericExpr(*operand) + ")";
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto& m = expr.as<MultipleConcatenationExpressionSyntax>();
            auto count = emitUntypedNumericExpr(*m.expression);
            if (count.size() >= 2 && count.front() == '{' && count.back() == '}') {
                count = trim(count.substr(1, count.size() - 2));
            }
            std::vector<std::pair<std::string, std::string>> parts;
            auto appendInner = [&]() {
                for (auto e : m.concatenation->expressions) {
                    auto rawWidth = exprWidth(*e);
                    auto width = foldWidth(rawWidth);
                    if (width.empty() && !rawWidth.empty()) {
                        width = rawWidth;
                    }
                    auto emitted = emitNumericExpr(*e);
                    auto bitsWidth = emittedBitsCallWidth(emitted);
                    if (!bitsWidth.empty()) {
                        width = bitsWidth;
                    }
                    if (emittedOneBitValueExpr(emitted)) {
                        width = "1";
                    }
                    auto castWidth = emittedConcatOperandCastWidth(emitted);
                    if (!castWidth.empty() &&
                        (width.empty() ||
                         (width == "1" && !isBitSelectOperand(*e) && !emittedOneBitValueExpr(emitted)) ||
                         emitted.find("__cpphdl_slice_out") != std::string::npos)) {
                        width = castWidth;
                    }
                    if (width.empty()) {
                        width = "64";
                    }
                    parts.push_back({width, emitted});
                }
            };
            if (!count.empty() && std::all_of(count.begin(), count.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
                for (size_t i = 0, n = std::stoul(count); i < n; ++i) {
                    appendInner();
                }
            }
            else {
                auto rawInnerWidth = exprWidth(*m.concatenation);
                auto innerWidth = foldWidth(rawInnerWidth);
                if (innerWidth.empty() && !rawInnerWidth.empty()) {
                    innerWidth = rawInnerWidth;
                }
                auto innerExpr = emitConcat(*m.concatenation);
                if (innerWidth.empty() || innerWidth == "1") {
                    auto emittedWidth = foldWidth(emittedLogicCastWidth(innerExpr));
                    if (!emittedWidth.empty()) {
                        innerWidth = emittedWidth;
                    }
                }
                if (innerWidth.empty()) {
                    innerWidth = "64";
                }
                auto totalWidth = "((uint64_t)(" + count + ") * (uint64_t)(" + innerWidth + "))";
                return "([&]() { logic<" + totalWidth + "> __cpphdl_rep{}; "
                       "for (size_t __cpphdl_i = 0; __cpphdl_i < (size_t)(" + count + "); ++__cpphdl_i) { "
                       "__cpphdl_rep.bits((__cpphdl_i + 1) * (size_t)(" + innerWidth + ") - 1, "
                       "__cpphdl_i * (size_t)(" + innerWidth + ")) = logic<" + innerWidth + ">(" + innerExpr + "); "
                       "} return __cpphdl_rep; }())";
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
                    if (p.second.find("__cpphdl_rep") != std::string::npos) {
                        args += p.second;
                    }
                    else {
                        args += "logic<" + p.first + ">(" + p.second + ")";
                    }
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

    bool isZeroIndexExpr(const std::string& value)
    {
        auto s = stripParens(value);
        return s == "0" || s == "0u" || s == "0U" || s == "0ull" || s == "0ULL" ||
               s == "0b0" || s == "0B0" || s == "0x0" || s == "0X0";
    }

    std::string subtractIndexLower(std::string index, std::string lower)
    {
        index = stripParens(std::move(index));
        lower = stripParens(std::move(lower));
        if (lower.empty() || isZeroIndexExpr(lower)) {
            return index;
        }
        uint64_t indexValue = 0;
        uint64_t lowerValue = 0;
        if (parseCppIntegralLiteral(index, indexValue) && parseCppIntegralLiteral(lower, lowerValue) &&
            indexValue >= lowerValue) {
            return std::to_string(indexValue - lowerValue);
        }
        return "((uint64_t)(" + index + ") - (uint64_t)((" + lower + ")))";
    }

    std::string arrayIndexExpr(const std::string& base, size_t level, std::string index)
    {
        if (!mod || base.empty()) {
            return index;
        }
        auto it = mod->arrayLowerBounds.find(base);
        if (it == mod->arrayLowerBounds.end() || level >= it->second.size()) {
            return index;
        }
        return subtractIndexLower(std::move(index), it->second[level]);
    }

    std::string arrayIndexExpr(const std::string& base, size_t level, const ExpressionSyntax& index)
    {
        return arrayIndexExpr(base, level, emitIndexExpr(index));
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
            return lvalue ? bits : emitSvBitsValue(base, select);
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

        h << "#pragma once\n\n#include \"cpphdl.h\"\n#include <array>\n#include <tuple>\n#include <print>\n#include <type_traits>\n#include <utility>\n\n"
             "#ifndef HDLCPP_FIXED_STRING_DEFINED\n"
             "#define HDLCPP_FIXED_STRING_DEFINED\n"
             "template <size_t N>\n"
             "struct hdlcpp_fixed_string {\n"
             "    char value[N]{};\n"
             "    constexpr hdlcpp_fixed_string(const char (&str)[N]) { for (size_t i = 0; i < N; ++i) value[i] = str[i]; }\n"
             "};\n"
             "template <size_t N, size_t M>\n"
             "constexpr bool operator==(const hdlcpp_fixed_string<N>& lhs, const char (&rhs)[M]) {\n"
             "    if constexpr (N != M) return false;\n"
             "    else { for (size_t i = 0; i < N; ++i) if (lhs.value[i] != rhs[i]) return false; return true; }\n"
             "}\n"
             "template <size_t N, size_t M>\n"
             "constexpr bool operator!=(const hdlcpp_fixed_string<N>& lhs, const char (&rhs)[M]) { return !(lhs == rhs); }\n"
             "#endif\n\n"
             "using namespace cpphdl;\n\n";

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
            auto tracePhases = std::getenv("HDLCPP_TRACE_PHASES") != nullptr;
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE module begin " << m.name
                          << " methods=" << m.methods.size()
                          << " assigns=" << m.assignLines.size()
                          << " pending=" << m.pendingCombByBase.size() << "\n";
            }
            emitInstanceConnections(m);
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE module after_instance " << m.name << "\n";
            }
            wireAssignsToPorts(m);
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE module after_wire_ports " << m.name << "\n";
            }
            movePartialOutputAssignLinesToComb(m);
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE module after_partial_outputs " << m.name
                          << " methods=" << m.methods.size()
                          << " assigns=" << m.assignLines.size()
                          << " pending=" << m.pendingCombByBase.size() << "\n";
            }
            for (auto it = m.combMethodByBase.begin(); it != m.combMethodByBase.end(); ) {
                auto typeIt = m.types.find(it->first);
                auto mixedCombSeq = m.seqAssignedVars.count(it->first) && m.combAssignedVars.count(it->first);
                if (typeIt != m.types.end() && typeIt->second.rfind("reg<", 0) == 0 && !mixedCombSeq) {
                    m.wireMap.erase(it->first);
                    it = m.combMethodByBase.erase(it);
                }
                else {
                    ++it;
                }
            }
            for (auto it = m.combSideEffectDriver.begin(); it != m.combSideEffectDriver.end(); ) {
                auto typeIt = m.types.find(it->first);
                auto mixedCombSeq = m.seqAssignedVars.count(it->first) && m.combAssignedVars.count(it->first);
                if (typeIt != m.types.end() && typeIt->second.rfind("reg<", 0) == 0 && !mixedCombSeq) {
                    m.combSideEffectChildInputReads.erase(it->first);
                    it = m.combSideEffectDriver.erase(it);
                }
                else {
                    ++it;
                }
            }
            auto isGeneratedCombStorage = [&](const std::string& base) {
                for (const auto& out : m.outputPortCppNames) {
                    if (base == outputStorageName(m, out.first) ||
                        base == combStorageName(m, out.first)) {
                        return true;
                    }
                }
                for (const auto& method : m.methods) {
                    if (!method.returnName.empty() && base == method.returnName) {
                        return true;
                    }
                }
                return false;
            };
            for (const auto& method : m.methods) {
                for (const auto& line : method.body) {
                    auto base = hdlcpp::assignmentBase(line);
                    if (base.empty() || method.localNames.count(base) || isGeneratedCombStorage(base) ||
                        m.types.count(base) || m.outputPortCppNames.count(base)) {
                        continue;
                    }
                    if (std::find(m.memberNames.begin(), m.memberNames.end(), base) != m.memberNames.end() ||
                        m.memberArraySizes.count(base)) {
                        continue;
                    }
                    auto rhs = hdlcpp::assignmentRhs(line);
                    auto width = foldWidth(typeWidth(expressionStorageType(m, rhs)));
                    auto type = (isNumber(width) && width != "0") ? ("logic<" + width + ">") : std::string("logic<1>");
                    m.types[base] = type;
                    if (!m.varNames.count(base)) {
                        m.vars.push_back({type, base});
                        m.varNames.insert(base);
                    }
                }
                if (method.name.find("_comb_func") != std::string::npos) {
                    if (method.localCombBody) {
                        continue;
                    }
                    for (const auto& line : method.body) {
                        auto base = hdlcpp::assignmentBase(line);
                        if (base.empty() || base == method.returnBase || base == method.returnName ||
                            method.localNames.count(base) ||
                            isGeneratedCombStorage(base) ||
                            !m.types.count(base) || (m.types[base].rfind("reg<", 0) == 0)) {
                            continue;
                        }
                        m.combAssignedVars.insert(base);
                        m.combSideEffectDriver.emplace(base, method.name);
                    }
                }
            }
            auto materializePendingComb = [&](const std::string& requestedBase) {
                std::vector<std::string> stack{requestedBase};
                std::set<std::string> visiting;
                while (!stack.empty()) {
                    auto base = stack.back();
                    stack.pop_back();
                    if (base.empty()) {
                        continue;
                    }
                    auto pendingIt = m.pendingCombByBase.find(base);
                    if (pendingIt == m.pendingCombByBase.end()) {
                        continue;
                    }
                    if (!visiting.insert(base).second) {
                        continue;
                    }
                    auto mixedCombSeq = m.seqAssignedVars.count(base) && m.combAssignedVars.count(base);
                    if (m.types.count(base) && m.types[base].rfind("reg<", 0) == 0 && !mixedCombSeq) {
                        continue;
                    }
                    const auto pending = pendingIt->second;
                    auto localCombBody = m.outputPortCppNames.count(base) ||
                        !hdlcpp::canExtractIndependentComb(pending.lines, pending.variables, base);
                    auto relevantAssigned = hdlcpp::targetDependencyVariablesWithPrunedControls(pending.lines, pending.variables, base);
                    auto body = localCombBody ?
                        hdlcpp::extractTargetCombLines(pending.lines, pending.variables, base) :
                        hdlcpp::extractIndependentCombLines(pending.lines, base);
                    auto rewritePendingBodyToReturnStorage = [&](std::vector<std::string>& lines, const std::string& returnName) {
                        if (returnName.empty()) {
                            return;
                        }
                        for (auto& line : lines) {
                            hdlcpp::rewriteLhsBase(line, base, returnName);
                            if (m.outputPortCppNames.count(base)) {
                                hdlcpp::rewriteLhsBase(line, m.outputPortCppNames[base], returnName);
                            }
                        }
                    };
                    auto addLocalDecls = [&](MethodGen& method, std::vector<std::string>& lines) {
                        if (!localCombBody) {
                            return;
                        }
                        std::vector<std::string> localDecls;
                        for (const auto& other : pending.variables) {
                            if (other == base || !relevantAssigned.count(other) ||
                                (m.types.count(other) && m.types[other].rfind("reg<", 0) == 0)) {
                                continue;
                            }
                            std::string localType;
                            if (m.outputPortCppNames.count(other)) {
                                localType = outputStorageType(m, other, m.outputPortCppNames[other]);
                            }
                            else if (m.types.count(other)) {
                                localType = unwrapRegType(m.types[other]);
                            }
                            if (localType.empty()) {
                                continue;
                            }
                            auto localName = hdlcpp::localCombNameFor(other);
                            method.localNames.insert(localName);
                            localDecls.push_back(localType + " " + localName + " = {};");
                        }
                        localDecls.insert(localDecls.end(), lines.begin(), lines.end());
                        lines.swap(localDecls);
                    };
                    auto enqueueReferencedPending = [&]() {
                        for (const auto& item : m.pendingCombByBase) {
                            if (item.first == base || m.combMethodByBase.count(item.first)) {
                                continue;
                            }
                            auto localName = hdlcpp::localCombNameFor(item.first);
                            bool referenced = false;
                            for (const auto& line : pending.lines) {
                                auto assigned = hdlcpp::assignmentBase(line);
                                if (!assigned.empty() && !relevantAssigned.count(assigned)) {
                                    continue;
                                }
                                if (hdlcpp::containsIdentifier(line, item.first) &&
                                    !hdlcpp::containsIdentifier(line, localName)) {
                                    referenced = true;
                                    break;
                                }
                            }
                            if (referenced) {
                                stack.push_back(item.first);
                            }
                        }
                    };
                    if (auto existingIt = m.combMethodByBase.find(base);
                        existingIt != m.combMethodByBase.end() && existingIt->second < m.methods.size()) {
                        auto& method = m.methods[existingIt->second];
                        method.localNames.insert(pending.localNames.begin(), pending.localNames.end());
                        rewritePendingBodyToReturnStorage(body, method.returnName);
                        addLocalDecls(method, body);
                        if (m.outputPortCppNames.count(base)) {
                            auto hasWholeAssignment = false;
                            for (const auto& line : method.body) {
                                auto eq = hdlcpp::topLevelAssignPos(line);
                                if (eq == std::string::npos) {
                                    continue;
                                }
                                auto lhs = trim(line.substr(0, eq));
                                if (lhs == base || lhs == method.returnName ||
                                    lhs == outputStorageName(m, base) ||
                                    lhs == combStorageName(m, base)) {
                                    hasWholeAssignment = true;
                                    break;
                                }
                            }
                            if (!hasWholeAssignment) {
                                method.body.insert(method.body.begin(), method.returnName + " = {};");
                            }
                        }
                        method.body.insert(method.body.end(), body.begin(), body.end());
                        m.combAssignedVars.insert(base);
                        m.combSideEffectDriver[base] = method.name;
                        enqueueReferencedPending();
                        m.pendingCombByBase.erase(base);
                        continue;
                    }
                    std::string retType;
                    std::string returnName;
                    if (m.outputPortCppNames.count(base)) {
                        retType = outputStorageType(m, base, m.outputPortCppNames[base]);
                        returnName = combStorageName(m, base);
                    }
                    else if (m.types.count(base)) {
                        retType = unwrapRegType(m.types[base]);
                        returnName = combStorageName(m, base);
                    }
                    if (retType.empty() || returnName.empty()) {
                        continue;
                    }
                    if (m.outputPortCppNames.count(base)) {
                        auto returnNameForOutput = outputStorageName(m, base);
                        auto normalizedAssignLines = m.assignLines;
                        for (auto& line : normalizedAssignLines) {
                            hdlcpp::rewriteLhsBase(line, returnNameForOutput, base);
                            hdlcpp::rewriteLhsBase(line, combStorageName(m, base), base);
                            hdlcpp::rewriteLhsBase(line, m.outputPortCppNames[base], base);
                        }
                        std::vector<std::string> assignVars{base, returnNameForOutput};
                        auto assignBody = hdlcpp::extractTargetCombLines(normalizedAssignLines, assignVars, base);
                        bool hasWholeAssignment = false;
                        for (const auto& line : assignBody) {
                            auto eq = hdlcpp::topLevelAssignPos(line);
                            if (eq == std::string::npos) {
                                continue;
                            }
                            auto lhs = trim(line.substr(0, eq));
                            if (lhs == base || lhs == returnNameForOutput) {
                                hasWholeAssignment = true;
                                break;
                            }
                        }
                        std::vector<std::string> mergedBody;
                        if (!hasWholeAssignment) {
                            mergedBody.push_back(base + " = {};");
                        }
                        for (const auto& line : assignBody) {
                            if (!isStructuralAssignLine(line)) {
                                mergedBody.push_back(line);
                            }
                        }
                        mergedBody.insert(mergedBody.end(), body.begin(), body.end());
                        body.swap(mergedBody);
                    }

                    MethodGen method;
                    method.name = base + "_comb_func";
                    method.ret = retType + "&";
                    method.returnName = returnName;
                    method.returnBase = base;
                    method.localNames = pending.localNames;

                    rewritePendingBodyToReturnStorage(body, returnName);
                    addLocalDecls(method, body);

                    m.combReturnTypes[returnName] = retType;
                    m.combMethodByBase[base] = m.methods.size();
                    m.wireMap[base] = method.name;
                    m.combAssignedVars.insert(base);
                    m.combSideEffectDriver[base] = method.name;
                    method.body = body;
                    m.methods.push_back(method);

                    enqueueReferencedPending();
                    m.pendingCombByBase.erase(base);
                }
            };
            std::set<std::string> pendingDemand;
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE pending demand begin " << m.name << "\n";
            }
            for (const auto& item : m.pendingCombByBase) {
                if (m.combMethodByBase.count(item.first)) {
                    pendingDemand.insert(item.first);
                }
            }
            for (const auto& out : m.outputPortCppNames) {
                if (m.combAssignedVars.count(out.first) && m.pendingCombByBase.count(out.first)) {
                    pendingDemand.insert(out.first);
                }
            }
            for (const auto& base : m.runtimeAssignDrivenVars) {
                if (m.pendingCombByBase.count(base)) {
                    pendingDemand.insert(base);
                }
            }
            auto scanPendingDemand = [&](const std::string& text) {
                for (const auto& item : m.pendingCombByBase) {
                    if (!m.combMethodByBase.count(item.first) &&
                        hdlcpp::containsIdentifier(text, item.first)) {
                        pendingDemand.insert(item.first);
                    }
                }
            };
            for (const auto& line : m.assignLines) {
                scanPendingDemand(line);
            }
            for (const auto& method : m.methods) {
                for (const auto& line : method.body) {
                    scanPendingDemand(line);
                }
            }
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE pending demand done " << m.name
                          << " demand=" << pendingDemand.size() << "\n";
            }
            for (const auto& base : pendingDemand) {
                if (tracePhases) {
                    std::cerr << "HDLCPP_PHASE pending materialize " << m.name << "." << base << "\n";
                }
                materializePendingComb(base);
            }
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE pending materialize done " << m.name
                          << " methods=" << m.methods.size() << "\n";
            }
            std::vector<std::pair<std::string, std::string>> bridgeDrivers;
            for (const auto& item : m.combSideEffectDriver) {
                bridgeDrivers.push_back(item);
            }
            for (const auto& base : m.runtimeAssignDrivenVars) {
                if (!m.combSideEffectDriver.count(base)) {
                    bridgeDrivers.push_back({base, {}});
                }
            }
            for (const auto& item : bridgeDrivers) {
                const auto& base = item.first;
                const auto& driver = item.second;
                if (base.empty() || driver.empty() || m.combMethodByBase.count(base)) {
                    if (base.empty() || !driver.empty() || m.combMethodByBase.count(base)) {
                        continue;
                    }
                }
                if (m.types.count(base) && m.types[base].rfind("reg<", 0) == 0) {
                    continue;
                }
                std::string retType;
                std::string returnName;
                if (m.outputPortCppNames.count(base)) {
                    retType = outputStorageType(m, base, m.outputPortCppNames[base]);
                    returnName = outputStorageName(m, base);
                }
                else if (m.types.count(base)) {
                    retType = unwrapRegType(m.types[base]);
                    returnName = combStorageName(m, base);
                }
                if (retType.empty() || returnName.empty()) {
                    continue;
                }
                MethodGen bridge;
                bridge.name = base + "_comb_func";
                bridge.ret = retType + "&";
                bridge.returnName = returnName;
                bridge.returnBase = base;
                if (driver.empty()) {
                    auto hasAssignmentTo = [](const std::vector<std::string>& body, const std::string& target) {
                        return std::any_of(body.begin(), body.end(), [&](const std::string& line) {
                            return hdlcpp::assignmentBase(line) == target;
                        });
                    };
                    auto normalizedAssignLines = m.assignLines;
                    if (m.outputPortCppNames.count(base)) {
                        for (auto& line : normalizedAssignLines) {
                            hdlcpp::rewriteLhsBase(line, m.outputPortCppNames[base], base);
                            hdlcpp::rewriteLhsBase(line, outputStorageName(m, base), base);
                            hdlcpp::rewriteLhsBase(line, combStorageName(m, base), base);
                        }
                    }
                    std::vector<std::string> assignVars{base, returnName};
                    std::vector<std::string> body;
                    if (m.outputPortCppNames.count(base) && m.assignExprByBase.count(base)) {
                        auto rhs = m.assignExprByBase.at(base);
                        auto sourceType = expressionStorageType(m, rhs);
                        auto sourceShape = trim(sourceType);
                        auto targetShape = trim(retType);
                        const bool targetIsLogic = targetShape.rfind("logic<", 0) == 0 || targetShape.rfind("u<", 0) == 0;
                        const bool sourceIsArray = sourceShape.rfind("array<", 0) == 0 || sourceShape.rfind("std::array<", 0) == 0;
                        if (targetIsLogic && sourceIsArray) {
                            rhs = "cpphdl::pack_value<cpphdl::type_width<" + retType + ">()>(" + rhs + ")";
                        }
                        body.push_back(base + " = " + rhs + ";");
                    }
                    else {
                        body = hdlcpp::extractTargetCombLines(normalizedAssignLines, assignVars, base);
                        if (!hasAssignmentTo(body, returnName)) {
                            body = hdlcpp::extractTargetCombLines(normalizedAssignLines, assignVars, base);
                        }
                    }
                    bool hasWholeAssignment = false;
                    for (const auto& line : body) {
                        auto eq = hdlcpp::topLevelAssignPos(line);
                        if (eq == std::string::npos) {
                            continue;
                        }
                        auto lhs = trim(line.substr(0, eq));
                        if (lhs == base || lhs == returnName) {
                            hasWholeAssignment = true;
                            break;
                        }
                    }
                    if (!hasWholeAssignment) {
                        bridge.body.push_back(returnName + " = {};");
                    }
                    for (const auto& line : body) {
                        if (!isStructuralAssignLine(line)) {
                            bridge.body.push_back(line);
                        }
                    }
                }
                else {
                    auto drivers = combDriversFor(m, base);
                    if (drivers.empty()) {
                        drivers.push_back(driver);
                    }
                    for (const auto& driverName : drivers) {
                        if (!driverName.empty() && driverName != bridge.name) {
                            bridge.body.push_back(driverName + "();");
                        }
                    }
                }
                m.combReturnTypes[returnName] = retType;
                m.combMethodByBase[base] = m.methods.size();
                m.wireMap[base] = bridge.name;
                m.combAssignedVars.insert(base);
                m.methods.push_back(bridge);
            }
            if (tracePhases) {
                std::cerr << "HDLCPP_PHASE bridge done " << m.name
                          << " bridgeDrivers=" << bridgeDrivers.size()
                          << " methods=" << m.methods.size() << "\n";
            }
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
            for (auto& decl : m.preClassDecls) {
                h << decl << "\n";
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
	                h << "    _PORT(logic<(uint64_t)(PopcountWidth)>) popcount_o_out = _ASSIGN_COMB(popcount_o_comb_func());\n\n";
	                h << "private:\n";
	                h << "    logic<(uint64_t)(PopcountWidth)> popcount_o_comb;\n";
	                h << "    logic<(uint64_t)(PopcountWidth)>& popcount_o_comb_func()\n";
	                h << "    {\n";
	                h << "        uint64_t count = 0;\n";
	                h << "        for (unsigned i = 0; i < INPUT_WIDTH; ++i) {\n";
	                h << "            count += (uint64_t)(logic<1>(data_i_in()[i]));\n";
	                h << "        }\n";
	                h << "        popcount_o_comb = logic<(uint64_t)(PopcountWidth)>(count);\n";
	                h << "        return popcount_o_comb;\n";
	                h << "    }\n\n";
	                h << "public:\n";
	                h << "    popcount()\n    {\n    }\n\n";
	                h << "    void _work(bool reset)\n    {\n    }\n\n";
	                h << "    void _strobe()\n    {\n    }\n\n";
	                h << "    void _assign()\n    {\n";
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
	            auto synthesizeFieldCombMethods = [&]() {
	                auto methodForBase = [&](const std::string& base) -> const MethodGen* {
	                    auto it = m.combMethodByBase.find(base);
	                    if (it == m.combMethodByBase.end() || it->second >= m.methods.size()) {
	                        return nullptr;
	                    }
	                    return &m.methods[it->second];
	                };
	                auto fieldMethodName = [](const std::string& base, const std::string& field) {
	                    return base + "_" + field + "_comb_func";
	                };
		                auto fieldStorageName = [](const std::string& base, const std::string& field) {
		                    return base + "_" + field + "_comb";
		                };
		                auto methodIndexByName = [&](const std::string& name) -> std::optional<size_t> {
		                    for (size_t i = 0; i < m.methods.size(); ++i) {
		                        if (m.methods[i].name == name) {
		                            return i;
		                        }
		                    }
		                    return std::nullopt;
		                };
		                auto isDefaultWholeExpr = [](std::string expr) {
		                    expr = trim(expr);
		                    if (!expr.empty() && expr.back() == ';') {
		                        expr.pop_back();
		                        expr = trim(expr);
		                    }
			                    return expr == "{}" || expr == "0" || expr == "0u" || expr == "0ull" ||
			                           expr == "logic<1>(0)" || expr == "logic<1>(0b0)" ||
			                           expr == "logic<1>(false)" || expr == "false";
			                };
			                std::function<bool(std::string)> isDefaultAggregateExpr = [&](std::string expr) -> bool {
			                    expr = trim(std::move(expr));
			                    if (!expr.empty() && expr.back() == ';') {
			                        expr.pop_back();
			                        expr = trim(expr);
			                    }
			                    while (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
			                        auto close = matchingParenClose(expr, 0);
			                        if (close != expr.size() - 1) {
			                            break;
			                        }
			                        expr = trim(expr.substr(1, expr.size() - 2));
			                    }
			                    if (isDefaultWholeExpr(expr)) {
			                        return true;
			                    }
			                    if (!expr.empty() && expr.front() == '(') {
			                        auto closeType = matchingParenClose(expr, 0);
			                        if (closeType != std::string::npos &&
			                            closeType + 1 < expr.size() &&
			                            expr[closeType + 1] == '(') {
			                            auto closeArg = matchingParenClose(expr, closeType + 1);
			                            if (closeArg == expr.size() - 1 &&
			                                isDefaultAggregateExpr(expr.substr(closeType + 2, closeArg - closeType - 2))) {
			                                return true;
			                            }
			                        }
			                    }
			                    int paren = 0;
			                    int bracket = 0;
			                    int brace = 0;
			                    int angle = 0;
			                    for (size_t pos = 0; pos < expr.size(); ++pos) {
			                        char ch = expr[pos];
			                        if (ch == '(') ++paren;
			                        else if (ch == ')' && paren > 0) --paren;
			                        else if (ch == '[') ++bracket;
			                        else if (ch == ']' && bracket > 0) --bracket;
			                        else if (ch == '{') ++brace;
			                        else if (ch == '}' && brace > 0) --brace;
			                        else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
			                        else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
			                        else if (ch == '&' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
			                            if (isDefaultAggregateExpr(expr.substr(0, pos)) ||
			                                isDefaultAggregateExpr(expr.substr(pos + 1))) {
			                                return true;
			                            }
			                        }
			                    }
			                    auto castArg = [&](const std::string& prefix) -> std::string {
			                        if (expr.rfind(prefix, 0) != 0) {
			                            return {};
			                        }
			                        int angle = 0;
			                        size_t open = std::string::npos;
			                        for (size_t pos = prefix.size(); pos < expr.size(); ++pos) {
			                            if (expr[pos] == '<') {
			                                ++angle;
			                            }
			                            else if (expr[pos] == '>' && angle > 0) {
			                                --angle;
			                            }
			                            else if (expr[pos] == '(' && angle == 0) {
			                                open = pos;
			                                break;
			                            }
			                        }
			                        if (open == std::string::npos) {
			                            return {};
			                        }
			                        auto close = matchingParenClose(expr, open);
			                        if (close != expr.size() - 1) {
			                            return {};
			                        }
			                        return trim(expr.substr(open + 1, close - open - 1));
			                    };
			                    for (const auto* prefix : {"cpphdl::sv_cast<", "logic<", "u<", "static_cast<"}) {
			                        auto arg = castArg(prefix);
			                        if (!arg.empty() && isDefaultAggregateExpr(arg)) {
			                            return true;
			                        }
			                    }
			                    return false;
			                };
		                auto resolveLocalAliasType = [&](std::string type) {
		                    type = unwrapRegType(trim(std::move(type)));
		                    for (size_t guard = 0; guard < 16; ++guard) {
		                        auto it = m.types.find(type);
		                        if (it == m.types.end() || it->second == type) {
		                            break;
		                        }
		                        type = unwrapRegType(trim(it->second));
		                    }
		                    return type;
		                };
		                auto fieldType = [&](const std::string& base, const std::string& field) {
		                    std::string baseType;
		                    if (m.types.count(base)) {
		                        baseType = m.types[base];
		                    }
		                    else if (auto source = methodForBase(base); source && !source->returnName.empty()) {
		                        auto typeIt = m.combReturnTypes.find(source->returnName);
		                        if (typeIt != m.combReturnTypes.end()) {
		                            baseType = typeIt->second;
		                        }
		                    }
		                    if (baseType.empty()) {
		                        return std::string();
		                    }
		                    baseType = unwrapRegType(baseType);
		                    auto resolvedBaseType = resolveLocalAliasType(baseType);
		                    if (resolvedBaseType.empty()) {
		                        resolvedBaseType = baseType;
		                    }
		                    auto type = fieldTypeFor(resolvedBaseType, field);
		                    if (type.empty()) {
		                        for (const auto& param : m.params) {
		                            auto paramName = templateParamName(param);
		                            if (paramName == resolvedBaseType && templateParamValueType(param).empty()) {
		                                auto eq = param.find('=');
		                                if (eq == std::string::npos) {
		                                    return std::string();
		                                }
		                                auto defaultType = unwrapRegType(trim(param.substr(eq + 1)));
		                                auto resolvedDefaultType = resolveLocalAliasType(defaultType);
		                                if (resolvedDefaultType.empty()) {
		                                    resolvedDefaultType = defaultType;
		                                }
		                                auto defaultFieldType = fieldTypeFor(resolvedDefaultType, field);
		                                if (defaultFieldType.empty() &&
		                                    (resolvedDefaultType.find("::") != std::string::npos ||
		                                     resolvedDefaultType.rfind("array<", 0) == 0 ||
		                                     resolvedDefaultType.rfind("std::array<", 0) == 0 ||
		                                     isNumericValueType(resolvedDefaultType) ||
		                                     resolvedDefaultType == "bool" ||
		                                     resolvedDefaultType == "u1" ||
		                                     resolvedDefaultType.rfind("logic<", 0) == 0 ||
		                                     resolvedDefaultType.rfind("u<", 0) == 0)) {
		                                    return std::string();
		                                }
		                                return "std::remove_cvref_t<decltype(std::declval<" + resolvedBaseType + ">()." + field + ")>";
		                            }
		                        }
		                        if (resolvedBaseType.find("::") != std::string::npos) {
		                            return std::string();
		                        }
		                        if (resolvedBaseType.rfind("array<", 0) == 0) {
		                            return std::string();
		                        }
		                        auto knownFields = fieldOrderFor(resolvedBaseType);
		                        if (!knownFields.empty()) {
		                            return std::string();
		                        }
		                        if (isNumericValueType(resolvedBaseType) || resolvedBaseType == "bool" || resolvedBaseType == "u1" ||
		                            resolvedBaseType.rfind("logic<", 0) == 0 || resolvedBaseType.rfind("u<", 0) == 0) {
		                            return std::string();
		                        }
		                        return std::string();
		                    }
		                    return unwrapRegType(type);
		                };
	                auto readBalancedArgument = [](const std::string& text, size_t pos) -> std::pair<std::string, size_t> {
	                    int paren = 0;
	                    int brace = 0;
	                    int bracket = 0;
	                    for (size_t i = pos; i < text.size(); ++i) {
	                        char ch = text[i];
	                        if (ch == '(') ++paren;
	                        else if (ch == ')') {
	                            if (paren == 0 && brace == 0 && bracket == 0) {
	                                return {text.substr(pos, i - pos), i};
	                            }
	                            if (paren > 0) --paren;
	                        }
	                        else if (ch == '{') ++brace;
	                        else if (ch == '}') {
	                            if (brace > 0) --brace;
	                        }
	                        else if (ch == '[') ++bracket;
	                        else if (ch == ']') {
	                            if (bracket > 0) --bracket;
	                        }
	                        else if (ch == ',' && paren == 0 && brace == 0 && bracket == 0) {
	                            return {text.substr(pos, i - pos), i};
	                        }
	                    }
	                    return {"", std::string::npos};
	                };
	                auto aggregateFieldExpr = [&](const MethodGen& source, const std::string& field) -> std::string {
	                    const std::string needle = "sv_assign_field(v." + field + ",";
	                    for (const auto& line : source.body) {
	                        auto pos = line.find(needle);
	                        if (pos == std::string::npos) {
	                            continue;
	                        }
	                        pos += needle.size();
	                        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
	                            ++pos;
	                        }
	                        auto [arg, end] = readBalancedArgument(line, pos);
	                        if (end != std::string::npos) {
	                            return trim(arg);
	                        }
	                    }
	                    return {};
	                };
		                auto wholeAssignExpr = [&](const MethodGen& source) -> std::string {
		                    auto storage = source.returnName;
		                    if (storage.empty()) {
		                        return {};
		                    }
		                    int depth = 0;
		                    for (const auto& line : source.body) {
		                        auto trimmedLine = trim(line);
		                        int beforeDepth = depth;
		                        for (char ch : trimmedLine) {
		                            if (ch == '{') {
		                                ++depth;
		                            }
		                            else if (ch == '}') {
		                                --depth;
		                                if (depth < 0) {
		                                    depth = 0;
		                                }
		                            }
		                        }
		                        if (beforeDepth != 0 || trimmedLine == "{" || trimmedLine == "}") {
		                            continue;
		                        }
		                        auto eq = hdlcpp::topLevelAssignPos(line);
		                        if (eq == std::string::npos) {
		                            continue;
	                        }
	                        auto lhs = trim(line.substr(0, eq));
	                        if (lhs != storage && lhs != source.returnBase) {
	                            continue;
	                        }
	                        auto rhs = trim(line.substr(eq + 1));
	                        if (!rhs.empty() && rhs.back() == ';') {
	                            rhs.pop_back();
	                            rhs = trim(rhs);
	                        }
		                        if (!rhs.empty()) {
		                            return rhs;
		                        }
		                    }
		                    return {};
		                };
		                const bool traceFieldDemands = std::getenv("HDLCPP_TRACE_FIELD_DEMANDS") != nullptr;
		                auto demandFields = [&]() {
		                    std::set<std::pair<std::string, std::string>> demand;
		                    auto scan = [&](const std::string& text, const std::string& inheritedField = "") {
	                        auto demandSelectedAggregateBranch = [&](const std::string& base, const std::string& wholeCall) {
	                            if (inheritedField.empty()) {
	                                return;
	                            }
	                            std::function<bool(std::string)> branchUsesWholeCall = [&](std::string branch) -> bool {
	                                branch = trim(std::move(branch));
	                                while (branch.size() >= 2 && branch.front() == '(' && branch.back() == ')') {
	                                    auto close = matchingParenClose(branch, 0);
	                                    if (close != branch.size() - 1) {
	                                        break;
	                                    }
	                                    branch = trim(branch.substr(1, branch.size() - 2));
	                                }
	                                if (branch == wholeCall) {
	                                    return true;
	                                }
	                                if (branch.find(wholeCall) != std::string::npos) {
	                                    return true;
	                                }
	                                int paren = 0;
	                                int angle = 0;
	                                size_t question = std::string::npos;
	                                size_t colon = std::string::npos;
	                                for (size_t pos = 0; pos < branch.size(); ++pos) {
	                                    if (branch[pos] == '(') ++paren;
	                                    else if (branch[pos] == ')' && paren > 0) --paren;
	                                    else if (branch[pos] == '<' && paren == 0) ++angle;
	                                    else if (branch[pos] == '>' && paren == 0 && angle > 0) --angle;
	                                    else if (branch[pos] == '?' && paren == 0 && angle == 0) question = pos;
	                                    else if (branch[pos] == ':' && paren == 0 && angle == 0 && question != std::string::npos) {
	                                        if ((pos > 0 && branch[pos - 1] == ':') ||
	                                            (pos + 1 < branch.size() && branch[pos + 1] == ':')) {
	                                            continue;
	                                        }
	                                        colon = pos;
	                                        break;
	                                    }
	                                }
	                                if (question == std::string::npos || colon == std::string::npos) {
	                                    return false;
	                                }
	                                return branchUsesWholeCall(branch.substr(question + 1, colon - question - 1)) ||
	                                       branchUsesWholeCall(branch.substr(colon + 1));
	                            };
	                            const auto suffix = ")." + inheritedField;
	                            for (size_t dot = 0; (dot = text.find(suffix, dot)) != std::string::npos;) {
	                                int depth = 0;
	                                size_t open = std::string::npos;
	                                for (size_t pos = dot + 1; pos-- > 0;) {
	                                    if (text[pos] == ')') {
	                                        ++depth;
	                                    }
	                                    else if (text[pos] == '(') {
	                                        --depth;
	                                        if (depth == 0) {
	                                            open = pos;
	                                            break;
	                                        }
	                                    }
	                                    if (pos == 0) {
	                                        break;
	                                    }
	                                }
	                                if (open == std::string::npos) {
	                                    dot += suffix.size();
	                                    continue;
	                                }
	                                auto inner = trim(text.substr(open + 1, dot - open - 1));
	                                while (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') {
	                                    auto close = matchingParenClose(inner, 0);
	                                    if (close != inner.size() - 1) {
	                                        break;
	                                    }
	                                    inner = trim(inner.substr(1, inner.size() - 2));
	                                }
	                                int paren = 0;
	                                int angle = 0;
	                                size_t question = std::string::npos;
	                                size_t colon = std::string::npos;
	                                for (size_t pos = 0; pos < inner.size(); ++pos) {
	                                    if (inner[pos] == '(') ++paren;
	                                    else if (inner[pos] == ')' && paren > 0) --paren;
	                                    else if (inner[pos] == '<' && paren == 0) ++angle;
	                                    else if (inner[pos] == '>' && paren == 0 && angle > 0) --angle;
	                                    else if (inner[pos] == '?' && paren == 0 && angle == 0) question = pos;
	                                    else if (inner[pos] == ':' && paren == 0 && angle == 0 && question != std::string::npos) {
	                                        if ((pos > 0 && inner[pos - 1] == ':') || (pos + 1 < inner.size() && inner[pos + 1] == ':')) {
	                                            continue;
	                                        }
	                                        colon = pos;
	                                        break;
	                                    }
	                                }
	                                if (question != std::string::npos && colon != std::string::npos) {
	                                    auto lhs = trim(inner.substr(question + 1, colon - question - 1));
	                                    auto rhs = trim(inner.substr(colon + 1));
	                                    if (branchUsesWholeCall(lhs) || branchUsesWholeCall(rhs)) {
	                                        demand.insert({base, inheritedField});
	                                        if (traceFieldDemands) {
	                                            std::cerr << "HDLCPP_FIELD_DEMAND module=" << m.name
	                                                      << " base=" << base
	                                                      << " field=" << inheritedField
	                                                      << " via=selected " << wholeCall << "\n";
	                                        }
	                                    }
	                                }
	                                dot += suffix.size();
	                            }
	                        };
		                        std::map<std::string, const MethodGen*> candidateBases;
		                        for (const auto& item : m.combMethodByBase) {
		                            if (auto source = methodForBase(item.first)) {
		                                candidateBases[item.first] = source;
		                            }
		                        }
		                        for (const auto& method : m.methods) {
		                            if (!method.returnBase.empty() && !method.returnName.empty()) {
		                                candidateBases.emplace(method.returnBase, &method);
		                            }
		                        }
		                        for (const auto& item : candidateBases) {
		                            auto source = item.second;
		                            if (!source) {
		                                continue;
		                            }
		                            if (traceFieldDemands) {
		                                std::cerr << "HDLCPP_FIELD_SCAN module=" << m.name
		                                          << " base=" << item.first
		                                          << " method=" << source->name
		                                          << " return=" << source->returnName
		                                          << "\n";
		                            }
		                            const auto call = source->name + "().";
	                            for (size_t pos = 0; (pos = text.find(call, pos)) != std::string::npos;) {
	                                auto start = pos + call.size();
	                                auto end = start;
	                                while (end < text.size() &&
	                                       (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
	                                    ++end;
	                                }
	                                auto field = text.substr(start, end - start);
	                                if (!field.empty() && field != "bits" && field != "get" && field != "_next") {
	                                    demand.insert({item.first, field});
	                                    if (traceFieldDemands) {
	                                        std::cerr << "HDLCPP_FIELD_DEMAND module=" << m.name
	                                                  << " base=" << item.first
	                                                  << " field=" << field
	                                                  << " via=call\n";
	                                    }
	                                }
	                                pos = end;
	                            }
		                            demandSelectedAggregateBranch(item.first, source->name + "()");
		                            const auto direct = item.first + ".";
	                            for (size_t pos = 0; (pos = text.find(direct, pos)) != std::string::npos;) {
	                                auto prev = pos == 0 ? '\0' : text[pos - 1];
	                                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == '.') {
	                                    pos += direct.size();
	                                    continue;
	                                }
	                                auto start = pos + direct.size();
	                                auto end = start;
	                                while (end < text.size() &&
	                                       (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
	                                    ++end;
	                                }
	                                auto field = text.substr(start, end - start);
	                                if (!field.empty() && field != "bits" && field != "get" && field != "_next") {
	                                    demand.insert({item.first, field});
	                                    if (traceFieldDemands) {
	                                        std::cerr << "HDLCPP_FIELD_DEMAND module=" << m.name
	                                                  << " base=" << item.first
	                                                  << " field=" << field
	                                                  << " via=direct\n";
	                                    }
	                                }
	                                pos = end;
	                            }
	                        }
	                    };
	                    for (const auto& line : m.assignLines) {
	                        scan(line);
	                    }
	                    for (const auto& method : m.methods) {
	                        std::string inheritedField;
	                        for (const auto& fieldMethod : m.combMethodByField) {
	                            if (fieldMethod.second < m.methods.size() &&
	                                m.methods[fieldMethod.second].name == method.name) {
	                                inheritedField = fieldMethod.first.second;
	                                break;
	                            }
	                        }
	                        if (inheritedField.empty() && !method.returnBase.empty() && !method.returnName.empty()) {
	                            const auto prefix = method.returnBase + "_";
	                            const auto suffix = std::string("_comb");
	                            if (method.returnName.rfind(prefix, 0) == 0 &&
	                                method.returnName.size() > prefix.size() + suffix.size() &&
	                                method.returnName.compare(method.returnName.size() - suffix.size(), suffix.size(), suffix) == 0) {
	                                inheritedField = method.returnName.substr(prefix.size(),
	                                    method.returnName.size() - prefix.size() - suffix.size());
	                            }
	                        }
		                        for (const auto& line : method.body) {
		                            scan(line, inheritedField);
		                            auto boundLine = lateBindExpr(m, line, method.returnName, method.name);
		                            if (boundLine != line) {
		                                scan(boundLine, inheritedField);
		                            }
		                        }
		                    }
	                    return demand;
	                };
	                bool changed = true;
	                while (changed) {
	                    changed = false;
	                    auto demand = demandFields();
	                    for (const auto& item : demand) {
	                        const auto& base = item.first;
	                        const auto& field = item.second;
	                        size_t mappedMethod = static_cast<size_t>(-1);
	                        if (auto mapped = m.combMethodByField.find(item);
	                            mapped != m.combMethodByField.end() && mapped->second < m.methods.size()) {
	                            mappedMethod = mapped->second;
	                        }
		                        auto source = methodForBase(base);
		                        if (!source) {
		                            if (traceFieldDemands) {
		                                std::cerr << "HDLCPP_FIELD_SKIP module=" << m.name
		                                          << " base=" << base
		                                          << " field=" << field
		                                          << " reason=no_source\n";
		                            }
		                            continue;
		                        }
		                        auto type = fieldType(base, field);
		                        if (type.empty()) {
		                            if (traceFieldDemands) {
		                                std::cerr << "HDLCPP_FIELD_SKIP module=" << m.name
		                                          << " base=" << base
		                                          << " field=" << field
		                                          << " reason=no_type\n";
		                            }
		                            continue;
		                        }
		                        if (traceFieldDemands) {
		                            std::cerr << "HDLCPP_FIELD_BUILD module=" << m.name
		                                      << " base=" << base
		                                      << " field=" << field
		                                      << " type=" << type
		                                      << " source=" << source->name
		                                      << "\n";
		                        }
		                        auto existingMethod = methodIndexByName(fieldMethodName(base, field));
		                        if (existingMethod && mappedMethod == static_cast<size_t>(-1)) {
		                            m.combMethodByField[item] = *existingMethod;
		                            mappedMethod = *existingMethod;
		                        }
			                        auto typeDefaultExpr = [&]() {
			                            return type + "{}";
			                        };
			                        auto branchContainsCall = [](const std::string& text, const std::string& methodName) {
			                            const auto needle = methodName + "()";
			                            auto isIdent = [](char ch) {
			                                return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
			                            };
			                            for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos;) {
			                                auto before = pos == 0 ? '\0' : text[pos - 1];
			                                auto after = pos + needle.size();
			                                if (!isIdent(before) && (after >= text.size() || !isIdent(text[after]))) {
			                                    return true;
			                                }
			                                pos += needle.size();
			                            }
			                            return false;
			                        };
			                        auto stripOuterParens = [&](std::string text) {
			                            text = trim(std::move(text));
			                            while (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
			                                auto close = matchingParenClose(text, 0);
			                                if (close != text.size() - 1) {
			                                    break;
			                                }
			                                text = trim(text.substr(1, text.size() - 2));
			                            }
			                            return text;
			                        };
			                        auto splitTopLevelTernary = [](const std::string& text,
			                                                          std::string& cond,
			                                                          std::string& lhs,
			                                                          std::string& rhsBranch) {
			                            int paren = 0;
			                            int bracket = 0;
			                            int brace = 0;
			                            int angle = 0;
			                            size_t question = std::string::npos;
			                            for (size_t pos = 0; pos < text.size(); ++pos) {
			                                char ch = text[pos];
			                                if (ch == '(') ++paren;
			                                else if (ch == ')' && paren > 0) --paren;
			                                else if (ch == '[') ++bracket;
			                                else if (ch == ']' && bracket > 0) --bracket;
			                                else if (ch == '{') ++brace;
			                                else if (ch == '}' && brace > 0) --brace;
			                                else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
			                                else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
			                                else if (ch == '?' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
			                                    question = pos;
			                                    break;
			                                }
			                            }
			                            if (question == std::string::npos) {
			                                return false;
			                            }
			                            paren = bracket = brace = angle = 0;
			                            for (size_t pos = question + 1; pos < text.size(); ++pos) {
			                                char ch = text[pos];
			                                if (ch == '(') ++paren;
			                                else if (ch == ')' && paren > 0) --paren;
			                                else if (ch == '[') ++bracket;
			                                else if (ch == ']' && bracket > 0) --bracket;
			                                else if (ch == '{') ++brace;
			                                else if (ch == '}' && brace > 0) --brace;
			                                else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
			                                else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
			                                else if (ch == ':' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
			                                    if ((pos > 0 && text[pos - 1] == ':') ||
			                                        (pos + 1 < text.size() && text[pos + 1] == ':')) {
			                                        continue;
			                                    }
			                                    cond = trim(text.substr(0, question));
			                                    lhs = trim(text.substr(question + 1, pos - question - 1));
			                                    rhsBranch = trim(text.substr(pos + 1));
			                                    return !cond.empty() && !lhs.empty() && !rhsBranch.empty();
			                                }
			                            }
			                            return false;
			                        };
		                        std::function<std::string(std::string)> typeDefaultBranches = [&](std::string expr) -> std::string {
		                            expr = trim(std::move(expr));
		                            auto isDefaultBranch = [&](std::string branch) {
		                                branch = trim(std::move(branch));
		                                while (branch.size() >= 2 && branch.front() == '(' && branch.back() == ')') {
		                                    auto close = matchingParenClose(branch, 0);
		                                    if (close != branch.size() - 1) {
		                                        break;
		                                    }
		                                    branch = trim(branch.substr(1, branch.size() - 2));
		                                }
		                                return isDefaultWholeExpr(branch);
		                            };
		                            if (isDefaultBranch(expr)) {
		                                return typeDefaultExpr();
		                            }
		                            std::string stripped = expr;
		                            bool wrapped = false;
		                            while (stripped.size() >= 2 && stripped.front() == '(' && stripped.back() == ')') {
		                                auto close = matchingParenClose(stripped, 0);
		                                if (close != stripped.size() - 1) {
		                                    break;
		                                }
		                                stripped = trim(stripped.substr(1, stripped.size() - 2));
		                                wrapped = true;
		                            }
		                            int paren = 0;
		                            int angle = 0;
		                            size_t question = std::string::npos;
		                            size_t colon = std::string::npos;
		                            for (size_t pos = 0; pos < stripped.size(); ++pos) {
		                                if (stripped[pos] == '(') ++paren;
		                                else if (stripped[pos] == ')' && paren > 0) --paren;
		                                else if (stripped[pos] == '<' && paren == 0) ++angle;
		                                else if (stripped[pos] == '>' && paren == 0 && angle > 0) --angle;
		                                else if (stripped[pos] == '?' && paren == 0 && angle == 0) question = pos;
		                                else if (stripped[pos] == ':' && paren == 0 && angle == 0 && question != std::string::npos) {
		                                    if ((pos > 0 && stripped[pos - 1] == ':') ||
		                                        (pos + 1 < stripped.size() && stripped[pos + 1] == ':')) {
		                                        continue;
		                                    }
		                                    colon = pos;
		                                    break;
		                                }
		                            }
		                            if (question == std::string::npos || colon == std::string::npos) {
		                                return expr;
		                            }
		                            auto cond = trim(stripped.substr(0, question));
		                            auto lhs = typeDefaultBranches(stripped.substr(question + 1, colon - question - 1));
		                            auto rhs = typeDefaultBranches(stripped.substr(colon + 1));
		                            auto out = "(" + cond + " ? " + lhs + " : " + rhs + ")";
		                            return wrapped ? out : out;
		                        };
			                        const auto storageName = fieldStorageName(base, field);
			                        const auto sourceStorageName = source->returnName.empty() ? source->returnBase : source->returnName;
			                        std::function<std::string(std::string)> projectFieldExpr;
			                        projectFieldExpr = [&](std::string expr) -> std::string {
			                            expr = stripOuterParens(std::move(expr));
				                            if (isDefaultAggregateExpr(expr)) {
				                                return typeDefaultExpr();
				                            }
			                            std::string cond;
			                            std::string trueBranch;
			                            std::string falseBranch;
			                            if (splitTopLevelTernary(expr, cond, trueBranch, falseBranch)) {
			                                return "(" + cond + " ? " + projectFieldExpr(trueBranch) +
			                                       " : " + projectFieldExpr(falseBranch) + ")";
			                            }
			                            for (const auto& combItem : m.combMethodByBase) {
			                                if (combItem.second >= m.methods.size()) {
			                                    continue;
			                                }
			                                const auto& methodName = m.methods[combItem.second].name;
			                                const auto call = methodName + "()";
			                                const auto fieldCall = call + "." + field;
			                                if (expr == fieldCall) {
			                                    if (methodName == source->name) {
			                                        if (auto sourceWhole = wholeAssignExpr(*source); !sourceWhole.empty()) {
			                                            return projectFieldExpr(sourceWhole);
			                                        }
			                                    }
			                                    return fieldCall;
			                                }
				                                if (expr == call || branchContainsCall(expr, methodName)) {
				                                    if (expr.find(call + "[") != std::string::npos) {
				                                        return "(" + expr + ")." + field;
				                                    }
				                                    return call + "." + field;
				                                }
			                            }
			                            return "(" + expr + ")." + field;
			                        };
			                        auto sourceBody = source->body;
		                        if (!source->returnBase.empty() && source->returnBase != sourceStorageName) {
		                            for (auto& line : sourceBody) {
		                                hdlcpp::rewriteLhsBase(line, source->returnBase, sourceStorageName);
		                            }
		                        }
		                        auto extractedFieldBody = hdlcpp::extractTargetFieldCombLines(
		                            sourceBody,
		                            sourceStorageName,
		                            field,
		                            storageName,
		                            "");
		                        auto extractedAssignsField = [&]() {
		                            const auto prefix = storageName + " =";
		                            const auto nestedPrefix = storageName + ".";
		                            for (const auto& line : extractedFieldBody) {
		                                auto trimmed = trim(line);
		                                if (trimmed.rfind(prefix, 0) == 0 ||
		                                    trimmed.rfind(nestedPrefix, 0) == 0) {
		                                    return true;
		                                }
		                            }
		                            return false;
		                        };
		                        if (extractedAssignsField()) {
		                            std::vector<std::string> methodBody;
		                            methodBody.push_back(storageName + " = " + typeDefaultExpr() + ";");
		                            for (auto& line : extractedFieldBody) {
		                                replaceAll(line, "std::remove_cvref_t<decltype(" + sourceStorageName + "." + field + ")>", type);
		                                replaceAll(line, "std::remove_cvref_t<decltype(" + source->returnBase + "." + field + ")>", type);
		                                replaceAll(line, "std::remove_cvref_t<decltype(" + storageName + "." + field + ")>", type);
		                                hdlcpp::replaceExactMember(line, sourceStorageName, field, storageName);
		                                hdlcpp::replaceExactMember(line, source->returnBase, field, storageName);
		                                hdlcpp::replaceExactMember(line, storageName, field, storageName);
		                                auto eq = hdlcpp::topLevelAssignPos(line);
		                                if (eq != std::string::npos && trim(line.substr(0, eq)) == storageName) {
		                                    auto rhs = trim(line.substr(eq + 1));
			                                    if (!rhs.empty() && rhs.back() == ';') {
			                                        rhs.pop_back();
			                                    }
			                                    if (rhs.find("()." + field) != std::string::npos) {
			                                        rhs = projectFieldExpr(rhs);
			                                    }
			                                    auto aggregateType = m.types.count(base) ? unwrapRegType(m.types[base]) : std::string();
		                                    if (!aggregateType.empty() && rhs.size() > field.size() + 3 &&
		                                        rhs.front() == '(' &&
		                                        rhs.compare(rhs.size() - field.size() - 2, field.size() + 2, ")." + field) == 0) {
		                                        auto close = matchingParenClose(rhs, 0);
		                                        if (close != std::string::npos &&
		                                            close == rhs.size() - field.size() - 2) {
		                                            auto inner = trim(rhs.substr(1, close - 1));
		                                            if (inner == source->name + "()") {
		                                                if (auto sourceWhole = wholeAssignExpr(*source); !sourceWhole.empty()) {
		                                                    inner = sourceWhole;
		                                                }
		                                            }
		                                            auto splitTernary = [](const std::string& text,
		                                                                   std::string& cond,
		                                                                   std::string& lhs,
		                                                                   std::string& rhsBranch) {
		                                                int paren = 0;
		                                                int bracket = 0;
		                                                int brace = 0;
		                                                int angle = 0;
		                                                size_t question = std::string::npos;
		                                                for (size_t pos = 0; pos < text.size(); ++pos) {
		                                                    char ch = text[pos];
		                                                    if (ch == '(') ++paren;
		                                                    else if (ch == ')' && paren > 0) --paren;
		                                                    else if (ch == '[') ++bracket;
		                                                    else if (ch == ']' && bracket > 0) --bracket;
		                                                    else if (ch == '{') ++brace;
		                                                    else if (ch == '}' && brace > 0) --brace;
		                                                    else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
		                                                    else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
		                                                    else if (ch == '?' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
		                                                        question = pos;
		                                                        break;
		                                                    }
		                                                }
		                                                if (question == std::string::npos) {
		                                                    return false;
		                                                }
		                                                paren = bracket = brace = angle = 0;
		                                                for (size_t pos = question + 1; pos < text.size(); ++pos) {
		                                                    char ch = text[pos];
		                                                    if (ch == '(') ++paren;
		                                                    else if (ch == ')' && paren > 0) --paren;
		                                                    else if (ch == '[') ++bracket;
		                                                    else if (ch == ']' && bracket > 0) --bracket;
		                                                    else if (ch == '{') ++brace;
		                                                    else if (ch == '}' && brace > 0) --brace;
		                                                    else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
		                                                    else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
		                                                    else if (ch == ':' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
		                                                        if ((pos > 0 && text[pos - 1] == ':') ||
		                                                            (pos + 1 < text.size() && text[pos + 1] == ':')) {
		                                                            continue;
		                                                        }
		                                                        cond = trim(text.substr(0, question));
		                                                        lhs = trim(text.substr(question + 1, pos - question - 1));
		                                                        rhsBranch = trim(text.substr(pos + 1));
		                                                        return !cond.empty() && !lhs.empty() && !rhsBranch.empty();
		                                                    }
		                                                }
		                                                return false;
		                                            };
		                                            std::string cond;
		                                            std::string trueBranch;
		                                            std::string falseBranch;
		                                            if (splitTernary(inner, cond, trueBranch, falseBranch)) {
		                                                auto branchFieldExpr = [&](std::string branch) {
		                                                    branch = trim(std::move(branch));
		                                                    auto defaultText = branch;
			                                                    if (isDefaultAggregateExpr(defaultText)) {
			                                                        return typeDefaultExpr();
			                                                    }
		                                                    for (const auto& combItem : m.combMethodByBase) {
		                                                        if (combItem.second >= m.methods.size()) {
		                                                            continue;
		                                                        }
		                                                        const auto& methodName = m.methods[combItem.second].name;
		                                                        if (methodName == fieldMethodName(base, field) ||
		                                                            m.methods[combItem.second].returnName == storageName) {
		                                                            continue;
		                                                        }
			                                                        if (branchContainsCall(branch, methodName)) {
			                                                            if (branch.find(methodName + "()[") != std::string::npos) {
			                                                                return "(" + branch + ")." + field;
			                                                            }
			                                                            return methodName + "()." + field;
			                                                        }
		                                                    }
		                                                    return "(cpphdl::unpack_value<" + aggregateType +
		                                                           ">(cpphdl::pack_value<cpphdl::type_width<" + aggregateType +
		                                                           ">()>(" + branch + ")))." + field;
		                                                };
		                                                rhs = "(" + cond + " ? " + branchFieldExpr(trueBranch) +
		                                                      " : " + branchFieldExpr(falseBranch) + ")";
		                                            }
		                                            else {
		                                                rhs = "(cpphdl::unpack_value<" + aggregateType +
		                                                      ">(cpphdl::pack_value<cpphdl::type_width<" + aggregateType +
		                                                      ">()>(" + inner + ")))." + field;
		                                            }
		                                        }
		                                    }
		                                    line = storageName + " = " + typeDefaultBranches(rhs) + ";";
		                                }
		                            }
		                            methodBody.insert(methodBody.end(), extractedFieldBody.begin(), extractedFieldBody.end());
		                            if (mappedMethod != static_cast<size_t>(-1)) {
		                                auto& method = m.methods[mappedMethod];
		                                if (method.body != methodBody || method.ret != type + "&" ||
		                                    method.returnName != storageName || method.returnBase != base) {
		                                    method.ret = type + "&";
		                                    method.returnName = storageName;
		                                    method.returnBase = base;
		                                    method.body = methodBody;
		                                    changed = true;
		                                }
		                                m.combReturnTypes[method.returnName] = type;
		                                m.combMethodByField[item] = mappedMethod;
		                            }
		                            else {
		                                MethodGen method;
		                                method.name = fieldMethodName(base, field);
		                                method.ret = type + "&";
		                                method.returnName = storageName;
		                                method.returnBase = base;
		                                method.body = methodBody;
		                                m.combReturnTypes[method.returnName] = type;
		                                m.combMethodByField[item] = m.methods.size();
		                                m.methods.push_back(method);
		                                changed = true;
		                            }
		                            continue;
		                        }
			                        auto expr = aggregateFieldExpr(*source, field);
		                        if (!expr.empty()) {
		                            replaceAll(expr, "std::remove_cvref_t<decltype(v." + field + ")>", type);
		                        }
		                        if (expr.empty()) {
		                            expr = wholeAssignExpr(*source);
		                            if (!expr.empty()) {
			                                if (isDefaultWholeExpr(expr)) {
			                                    expr = typeDefaultExpr();
			                                }
			                                else {
			                                    expr = projectFieldExpr(expr);
			                                }
		                            }
		                        }
		                        if (expr.empty()) {
		                            continue;
		                        }
		                        expr = typeDefaultBranches(expr);
		                        std::vector<std::string> methodBody;
		                        methodBody.push_back(fieldStorageName(base, field) + " = " + expr + ";");
		                        if (mappedMethod != static_cast<size_t>(-1)) {
		                            auto& method = m.methods[mappedMethod];
		                            if (method.body != methodBody || method.ret != type + "&" ||
		                                method.returnName != fieldStorageName(base, field) || method.returnBase != base) {
		                                method.ret = type + "&";
		                                method.returnName = fieldStorageName(base, field);
		                                method.returnBase = base;
		                                method.body = methodBody;
		                                changed = true;
		                            }
		                            m.combReturnTypes[method.returnName] = type;
		                            m.combMethodByField[item] = mappedMethod;
		                        }
		                        else {
		                            MethodGen method;
		                            method.name = fieldMethodName(base, field);
		                            method.ret = type + "&";
		                            method.returnName = fieldStorageName(base, field);
		                            method.returnBase = base;
		                            method.body = methodBody;
		                            m.combReturnTypes[method.returnName] = type;
		                            m.combMethodByField[item] = m.methods.size();
		                            m.methods.push_back(method);
		                            changed = true;
		                        }
		                    }
	                }
	            };
	            synthesizeFieldCombMethods();
	            auto isStaticConstHelperCandidate = [](const MethodGen& method) {
                return method.ret != "void" &&
                       method.name.find("_comb_func") == std::string::npos &&
                       method.name != "_work" &&
                       method.name != "_assign" &&
                       method.name != "_strobe" &&
                       method.name.rfind("always_", 0) != 0;
            };
            auto mentionsFunctionCall = [](const std::string& text, const std::string& name) {
                auto isIdent = [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                };
                for (size_t pos = 0; (pos = text.find(name, pos)) != std::string::npos;) {
                    auto end = pos + name.size();
                    bool leftOk = pos == 0 || !isIdent(text[pos - 1]);
                    bool rightOk = end < text.size() && text[end] == '(';
                    if (leftOk && rightOk) {
                        return true;
                    }
                    pos = end;
                }
                return false;
            };
            std::set<std::string> staticConstHelperNames;
            for (const auto& c : m.constants) {
                for (const auto& method : m.methods) {
                    if (isStaticConstHelperCandidate(method) && mentionsFunctionCall(c.second, method.name)) {
                        staticConstHelperNames.insert(method.name);
                    }
                }
            }
            bool addedStaticHelper = true;
            while (addedStaticHelper) {
                addedStaticHelper = false;
                for (const auto& method : m.methods) {
                    if (!staticConstHelperNames.count(method.name)) {
                        continue;
                    }
                    for (const auto& bodyLine : method.body) {
                        for (const auto& candidate : m.methods) {
                            if (staticConstHelperNames.count(candidate.name) ||
                                !isStaticConstHelperCandidate(candidate)) {
                                continue;
                            }
                            if (mentionsFunctionCall(bodyLine, candidate.name)) {
                                staticConstHelperNames.insert(candidate.name);
                                addedStaticHelper = true;
                            }
                        }
                    }
                }
            }
            for (auto& method : m.methods) {
                method.staticConstexpr = staticConstHelperNames.count(method.name);
            }
	            std::vector<std::pair<std::string, std::string>> localConstItems(localConstExprs.begin(), localConstExprs.end());
	            std::sort(localConstItems.begin(), localConstItems.end(), [](auto& a, auto& b) {
	                return a.first.size() > b.first.size();
	            });
            for (auto& kv : localConstItems) {
                for (auto& inner : localConstItems) {
                    if (inner.first != kv.first) {
                        replaceIdentifierAll(kv.second, inner.first, "(" + inner.second + ")");
                    }
                }
            }
            std::set<std::string> constantsBeforeTypes;
            for (const auto& kv : localConstItems) {
                for (const auto& t : m.typeDecls) {
                    if (t.find(kv.first) != std::string::npos) {
                        constantsBeforeTypes.insert(kv.first);
                        break;
                    }
                }
            }
            bool addedConstDependency = true;
            while (addedConstDependency) {
                addedConstDependency = false;
                for (const auto& kv : localConstItems) {
                    if (constantsBeforeTypes.count(kv.first)) {
                        continue;
                    }
                    for (const auto& selected : constantsBeforeTypes) {
                        auto initIt = localConstExprs.find(selected);
                        if (initIt != localConstExprs.end() && initIt->second.find(kv.first) != std::string::npos) {
                            constantsBeforeTypes.insert(kv.first);
                            addedConstDependency = true;
                            break;
                        }
                    }
	                }
	            }
            std::set<std::string> constantsBeforeStaticMethods;
            for (const auto& kv : localConstItems) {
                for (const auto& method : m.methods) {
                    if (!method.staticConstexpr) {
                        continue;
                    }
                    bool found = false;
                    for (const auto& bodyLine : method.body) {
                        if (isIdentifierUsed(bodyLine, kv.first)) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        constantsBeforeStaticMethods.insert(kv.first);
                        break;
                    }
                }
            }
            bool addedStaticConstDependency = true;
            while (addedStaticConstDependency) {
                addedStaticConstDependency = false;
                for (const auto& kv : localConstItems) {
                    if (constantsBeforeStaticMethods.count(kv.first)) {
                        continue;
                    }
                    for (const auto& selected : constantsBeforeStaticMethods) {
                        auto initIt = localConstExprs.find(selected);
                        if (initIt != localConstExprs.end() && isIdentifierUsed(initIt->second, kv.first)) {
                            constantsBeforeStaticMethods.insert(kv.first);
                            addedStaticConstDependency = true;
                            break;
                        }
                    }
                }
            }
	            std::map<std::string, std::string> substitutedConstExprs;
	            for (const auto& kv : localConstItems) {
	                substitutedConstExprs[kv.first] = kv.second;
	            }
            std::set<std::string> emittedConstants;
	            auto emitConstant = [&](const std::pair<std::string, std::string>& c) {
	                auto constType = constexprType(c.first);
	                auto constInit = c.second;
	                auto eq = constInit.find('=');
	                auto name = trim(eq == std::string::npos ? constInit : constInit.substr(0, eq));
                if (emittedConstants.count(name)) {
                    return;
                }
	                auto subIt = substitutedConstExprs.find(name);
	                if (subIt != substitutedConstExprs.end()) {
	                    constInit = name + " = " + subIt->second;
	                }
	                if (constType.rfind("std::array<", 0) == 0 && constInit.find("logic<") != std::string::npos) {
	                    constInit = stripLogicLiteralCasts(constInit);
	                }
                if ((constType == "uint64_t" || constType == "unsigned") && constInit.find("cat{") != std::string::npos) {
                    auto splitComma = [](const std::string& text) {
                        std::vector<std::string> out;
                        size_t begin = 0;
                        int angle = 0;
                        int paren = 0;
                        int brace = 0;
                        int bracket = 0;
                        for (size_t i = 0; i < text.size(); ++i) {
                            char ch = text[i];
                            if (ch == '<') ++angle;
                            else if (ch == '>' && angle > 0) --angle;
                            else if (ch == '(') ++paren;
                            else if (ch == ')' && paren > 0) --paren;
                            else if (ch == '{') ++brace;
                            else if (ch == '}' && brace > 0) --brace;
                            else if (ch == '[') ++bracket;
                            else if (ch == ']' && bracket > 0) --bracket;
                            else if (ch == ',' && angle == 0 && paren == 0 && brace == 0 && bracket == 0) {
                                out.push_back(trim(text.substr(begin, i - begin)));
                                begin = i + 1;
                            }
                        }
                        out.push_back(trim(text.substr(begin)));
                        return out;
                    };
                    auto constexprCat = [&](const std::string& rhs) -> std::string {
                        auto s = trim(rhs);
                        if (s.rfind("cat{", 0) != 0 || s.empty() || s.back() != '}') {
                            return "";
                        }
                        struct Part {
                            uint64_t width;
                            std::string expr;
                        };
                        std::vector<Part> parts;
                        uint64_t total = 0;
                        for (auto item : splitComma(s.substr(4, s.size() - 5))) {
                            item = trim(item);
                            if (item.rfind("logic<", 0) != 0) {
                                return "";
                            }
                            auto lt = item.find('<');
                            int depth = 0;
                            size_t gt = std::string::npos;
                            for (size_t i = lt; i < item.size(); ++i) {
                                if (item[i] == '<') ++depth;
                                else if (item[i] == '>' && --depth == 0) {
                                    gt = i;
                                    break;
                                }
                            }
                            if (gt == std::string::npos) {
                                return "";
                            }
                            auto widthText = trim(item.substr(lt + 1, gt - lt - 1));
                            if (!isNumber(widthText)) {
                                return "";
                            }
                            auto width = std::stoull(widthText);
                            auto rest = trim(item.substr(gt + 1));
                            if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') {
                                return "";
                            }
                            total += width;
                            if (width == 0 || total > 64) {
                                return "";
                            }
                            parts.push_back({width, trim(rest.substr(1, rest.size() - 2))});
                        }
                        std::string out = "0ull";
                        uint64_t shift = 0;
                        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                            auto mask = it->width >= 64 ? "~0ull" : "((1ull << " + std::to_string(it->width) + ") - 1ull)";
                            auto term = "(((uint64_t)(" + it->expr + ")) & " + mask + ")";
                            if (shift != 0) {
                                term = "(" + term + " << " + std::to_string(shift) + ")";
                            }
                            out = "(" + out + " | " + term + ")";
                            shift += it->width;
                        }
                        return out;
                    };
                    auto eqPos = constInit.find('=');
                    auto rhs = trim(eqPos == std::string::npos ? constInit : constInit.substr(eqPos + 1));
                    auto converted = constexprCat(rhs);
                    if (!converted.empty()) {
                        constInit = (eqPos == std::string::npos ? converted : constInit.substr(0, eqPos + 1) + " " + converted);
                    }
                }
                replaceAll(constInit, "([&]()", "([]()");
	                h << "    " << postProcessCppLine("static constexpr " + constType + " " + constInit + ";") << "\n";
                emittedConstants.insert(name);
	            };
	            for (auto& c : m.constants) {
	                auto eq = c.second.find('=');
	                auto name = trim(eq == std::string::npos ? c.second : c.second.substr(0, eq));
	                if (constantsBeforeStaticMethods.count(name)) {
	                    emitConstant(c);
	                }
	            }
            for (auto& f : m.methods) {
                if (f.staticConstexpr) {
                    emitMethod(h, m, f);
                }
            }
	            for (auto& c : m.constants) {
	                auto eq = c.second.find('=');
	                auto name = trim(eq == std::string::npos ? c.second : c.second.substr(0, eq));
	                if (constantsBeforeTypes.count(name)) {
	                    emitConstant(c);
	                }
	            }
            auto typeDeclOverrides = configuredTextMap("HDLCPP_TYPE_DECL_OVERRIDES");
            std::vector<std::pair<std::string, std::string>> injectedTypeDecls;
            auto hasTypeDecl = [&](const std::string& typeName) {
                for (const auto& decl : m.typeDecls) {
                    if (!usingOverrideTarget(typeName, decl).empty()) {
                        return true;
                    }
                    auto text = trim(decl);
                    for (auto keyword : {"struct ", "union ", "class "}) {
                        if (text.rfind(keyword, 0) == 0) {
                            auto rest = trim(text.substr(std::strlen(keyword)));
                            if (rest == typeName || rest.rfind(typeName + " ", 0) == 0 ||
                                rest.rfind(typeName + "{", 0) == 0) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };
            for (const auto& item : typeDeclOverrides) {
                auto dot = item.first.find('.');
                if (dot == std::string::npos || item.first.substr(0, dot) != m.name) {
                    continue;
                }
                auto typeName = item.first.substr(dot + 1);
                auto aliasTarget = usingOverrideTarget(typeName, item.second);
                if (typeName.empty() || aliasTarget.empty() || hasTypeDecl(typeName)) {
                    continue;
                }
                registerUsingOverrideAlias(m, typeName, item.second);
                injectedTypeDecls.push_back({typeName, item.second});
            }
            for (const auto& item : typeDeclOverrides) {
                auto dot = item.first.find('.');
                if (dot == std::string::npos || item.first.substr(0, dot) != m.name) {
                    continue;
                }
                auto typeName = item.first.substr(dot + 1);
                if (typeName.empty() || m.types.count(typeName) || !usingOverrideTarget(typeName, item.second).empty()) {
                    continue;
                }
                auto decl = item.second;
                auto overrideFields = packedFieldsFromOverrideDecl(decl);
                replaceAll(decl, "@PACKED@", overrideFields.empty()
                    ? packedAggregateHelpers(typeName)
                    : packedAggregateHelpers(typeName, joinedPackedWidth(overrideFields), overrideFields, false));
                m.types[typeName] = typeName;
                if (!overrideFields.empty()) {
                    m.typeWidths[typeName] = joinedPackedWidth(overrideFields);
                }
                injectedTypeDecls.push_back({typeName, decl});
            }
            std::stable_sort(injectedTypeDecls.begin(), injectedTypeDecls.end(),
                             [](const auto& lhs, const auto& rhs) {
                                 auto lhsUsing = trim(lhs.second).rfind("using ", 0) == 0;
                                 auto rhsUsing = trim(rhs.second).rfind("using ", 0) == 0;
                                 return lhsUsing != rhsUsing && lhsUsing;
                             });
            for (const auto& item : injectedTypeDecls) {
                h << "    " << postProcessCppLine(item.second) << "\n";
            }
	            for (auto& t : m.typeDecls) {
                auto typeDeclLine = t;
                for (auto& kv : localConstItems) {
                    replaceAll(typeDeclLine, "logic<" + kv.first + ">", "logic<(" + kv.second + ")>");
                    replaceIdentifierAll(typeDeclLine, kv.first, "(" + kv.second + ")");
                }
	                typeDeclLine = postProcessCppLine(typeDeclLine);
	                h << "    " << typeDeclLine << "\n";
	            }
	            for (auto& c : m.constants) {
	                auto eq = c.second.find('=');
	                auto name = trim(eq == std::string::npos ? c.second : c.second.substr(0, eq));
	                if (!emittedConstants.count(name)) {
	                    emitConstant(c);
	                }
	            }
            auto combOutputInit = [&](const std::string& svName) -> std::string {
                if (auto it = m.combMethodByBase.find(svName); it != m.combMethodByBase.end() &&
                    it->second < m.methods.size() && isPurePortBridgeComb(m, m.methods[it->second])) {
	                    return " = _ASSIGN_COMB( " + m.methods[it->second].name + "() )";
                }
                auto drivers = combDriversFor(m, svName);
                if (drivers.empty()) {
                    return "";
                }
                auto storageName = combStorageName(m, svName);
	                return " = _ASSIGN_COMB( " + storageName + "_func() )";
            };

            std::vector<std::string> deferredPortInitLines;
            std::set<std::string> getterOutputPorts;
            auto outputPortWithName = [&](const std::string& cppName) {
                for (auto& out : m.outputPortCppNames) {
                    if (out.second == cppName) {
                        return true;
                    }
                }
                return false;
            };
            auto initReferencesChild = [&](const std::string& init) {
                if (init.find("_ASSIGN") == std::string::npos) {
                    return false;
                }
                for (auto& name : m.memberNames) {
                    if (init.find(name + ".") != std::string::npos ||
                        init.find(name + "[") != std::string::npos) {
                        return true;
                    }
                }
                return false;
            };
            auto normalizeOutputPortInit = [&](std::string init) {
                auto assignPos = init.find("_ASSIGN(");
                if (assignPos == std::string::npos || init.find("_ASSIGN_COMB(") != std::string::npos) {
                    return init;
                }
                auto exprBegin = assignPos + std::string("_ASSIGN(").size();
                auto exprEnd = init.rfind(')');
                if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
                    return init;
                }
                auto expr = trim(init.substr(exprBegin, exprEnd - exprBegin));
                if (isChildOutputPortCall(m, expr) || isSimpleCombRef(expr)) {
                    init.replace(assignPos, std::string("_ASSIGN").size(), "_ASSIGN_COMB");
                }
                return init;
            };
            auto childOutputPortTypeForCall = [&](std::string expr) -> std::string {
                expr = trim(std::move(expr));
                if (!hasSuffix(expr, "()")) {
                    return "";
                }
                expr = trim(expr.substr(0, expr.size() - 2));
                auto dot = expr.find('.');
                if (dot == std::string::npos || expr.find('.', dot + 1) != std::string::npos) {
                    return "";
                }
                auto instance = trim(expr.substr(0, dot));
                auto portName = trim(expr.substr(dot + 1));
                if (instance.empty() || portName.empty() || instance.find('[') != std::string::npos) {
                    return "";
                }
                auto childType = resolveChildTypeForInstance(m, instance);
                auto baseChildType = trim(childType);
                if (auto lt = baseChildType.find('<'); lt != std::string::npos) {
                    baseChildType = trim(baseChildType.substr(0, lt));
                }
                if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES"); !portTypes.empty()) {
                    auto lookupConfigured = [&](const std::string& key) -> std::string {
                        auto it = portTypes.find(key);
                        if (it == portTypes.end()) {
                            return "";
                        }
                        auto spec = it->second;
                        auto sep = spec.find(':');
                        auto direction = trim(sep == std::string::npos ? std::string() : spec.substr(0, sep));
                        auto configuredType = trim(sep == std::string::npos ? spec : spec.substr(sep + 1));
                        return direction == "output" ? configuredType : std::string();
                    };
                    if (auto configured = lookupConfigured(childType + "." + portName); !configured.empty()) {
                        return configured;
                    }
                    if (auto configured = lookupConfigured(baseChildType + "." + portName); !configured.empty()) {
                        return configured;
                    }
                }
                auto* child = baseChildType.empty() ? nullptr : findModule(baseChildType);
                if (!child) {
                    return "";
                }
                for (const auto& out : child->outputPortCppNames) {
                    if (out.first == portName || out.second == portName) {
                        for (const auto& port : child->ports) {
                            if (port.direction == "output" && port.name == out.second) {
                                return port.type;
                            }
                        }
                    }
                }
                for (const auto& port : child->ports) {
                    if (port.direction == "output" && port.name == portName) {
                        return port.type;
                    }
                }
                return "";
            };
            auto childOutputCanForwardDirectly = [&](const std::string& parentType, const std::string& expr) {
                auto childType = childOutputPortTypeForCall(expr);
                return !childType.empty() && trim(postProcessCppLine(childType)) == trim(postProcessCppLine(parentType));
            };
            auto materializeOutputPortInit = [&](const std::string& svName, const std::string& type, std::string init) {
                auto assignPos = init.find("_ASSIGN_COMB(");
                std::string wrapper = "_ASSIGN_COMB(";
                if (assignPos == std::string::npos) {
                    assignPos = init.find("_ASSIGN(");
                    wrapper = "_ASSIGN(";
                }
                if (assignPos == std::string::npos) {
                    return init;
                }
                auto exprBegin = assignPos + wrapper.size();
                auto exprEnd = init.rfind(')');
                if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
                    return init;
                }
                auto expr = trim(init.substr(exprBegin, exprEnd - exprBegin));
                if ((isChildOutputPortCall(m, expr) && childOutputCanForwardDirectly(type, expr)) || isSimpleCombRef(expr)) {
                    return init;
                }
                if (wrapper == "_ASSIGN(" && expr.find("_comb_func()") == std::string::npos &&
                    !isChildOutputPortCall(m, expr)) {
                    return init;
                }

                auto storageName = combStorageName(m, svName);
                auto methodName = storageName + "_func";
                if (auto it = m.combMethodByBase.find(svName); it != m.combMethodByBase.end() &&
                    it->second < m.methods.size()) {
                    methodName = m.methods[it->second].name;
                }
                else {
                    MethodGen method;
                    method.name = methodName;
                    method.ret = type + "&";
                    method.returnName = storageName;
                    method.returnBase = svName;
                    method.body.push_back(storageName + " = " + adaptPackedVectorRhsFromArray(m, type, expr) + ";");
                    m.combReturnTypes[storageName] = type;
                    m.combMethodByBase[svName] = m.methods.size();
                    m.wireMap[svName] = method.name;
                    m.combAssignedVars.insert(svName);
                    m.methods.push_back(method);
                }
                return " = _ASSIGN_COMB( " + methodName + "() )";
            };
            auto childOutputBridgeRhs = [&](const std::string& svName) -> std::string {
                if (!isAssignOnlyOutput(m, svName)) {
                    return "";
                }
                auto it = m.assignExprByBase.find(svName);
                if (it == m.assignExprByBase.end()) {
                    return "";
                }
                auto rhs = postProcessCppLine(lateBindExpr(m, it->second, ""));
                return (isChildOutputPortCall(m, rhs) && childOutputCanForwardDirectly(m.types.count(svName) ? m.types.at(svName) : std::string(), rhs)) ? rhs : std::string();
            };
            auto combOutputDrivers = [&](const std::string& svName) {
                auto drivers = combDriversFor(m, svName);
                return drivers;
            };
            auto inlineCombOutputInit = [&](const std::string& svName) -> std::string {
                if (!configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", m.name)) {
                    return "";
                }
                auto bodies = configuredTextMap("HDLCPP_INLINE_COMB_BODIES");
                for (auto& [key, body] : bodies) {
                    auto sep = key.find('.');
                    if (sep == std::string::npos || key.substr(0, sep) != m.name) {
                        continue;
                    }
                    if (body.find(svName + "_comb") != std::string::npos ||
                        body.find(svName + "_comb_func") != std::string::npos) {
	                        return " = _ASSIGN_COMB( " + svName + "_comb_func() )";
                    }
                }
                return "";
            };
            for (auto& p : m.ports) {
                std::string init = p.init;
                std::string outputSvName;
                bool emittedForwardingPort = false;
		                for (auto& out : m.outputPortCppNames) {
		                    if (out.second == p.name) {
                        outputSvName = out.first;
                        auto inlineCombInit = inlineCombOutputInit(out.first);
                        if (!inlineCombInit.empty() && p.array.empty()) {
                            init = inlineCombInit;
                            break;
                        }
	                        auto bridgeRhs = childOutputBridgeRhs(out.first);
	                        if (!bridgeRhs.empty() && p.array.empty()) {
	                            h << "    " << postProcessCppLine(p.type) << "& " << p.name << "() { return " << bridgeRhs << "; }\n";
	                            getterOutputPorts.insert(p.name);
	                            emittedForwardingPort = true;
	                            break;
	                        }
                        auto combDrivers = combOutputDrivers(out.first);
	                        if (!combDrivers.empty() && isCombOnlyOutput(m, out.first) && p.array.empty() &&
	                            (configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + out.first) ||
	                             configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + p.name))) {
	                            h << "    " << postProcessCppLine(p.type) << "& " << p.name
	                              << "() { __readonly_" << out.first << "_comb_func(); return "
	                              << outputStorageName(m, out.first) << "; }\n";
	                            getterOutputPorts.insert(p.name);
	                            emittedForwardingPort = true;
	                            break;
	                        }
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
                        else if (m.seqAssignedVars.count(out.first) &&
                                 m.combMethodByBase.count(out.first) &&
                                 m.combMethodByBase[out.first] < m.methods.size()) {
	                            init = " = _ASSIGN_COMB( " + m.methods[m.combMethodByBase[out.first]].name + "() )";
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
                            if (emittedForwardingPort) {
                                continue;
                            }
                            init = lateBindExpr(m, init, "");
                            auto processedInit = postProcessCppLine(init);
                            const bool isOutputPort = outputPortWithName(p.name) || p.direction == "output";
                            if (isOutputPort) {
                                if (!outputSvName.empty()) {
                                    processedInit = materializeOutputPortInit(outputSvName, postProcessCppLine(p.type), processedInit);
                                }
                                processedInit = normalizeOutputPortInit(processedInit);
                                auto assignPos = processedInit.find("_ASSIGN(");
                                std::string wrapper = "_ASSIGN(";
                                if (assignPos == std::string::npos) {
                                    assignPos = processedInit.find("_ASSIGN_COMB(");
                                    wrapper = "_ASSIGN_COMB(";
                                }
                                if (assignPos != std::string::npos) {
                                    auto exprBegin = assignPos + wrapper.size();
                                    auto exprEnd = processedInit.rfind(')');
                                    if (exprEnd != std::string::npos && exprEnd > exprBegin) {
                                        auto expr = trim(processedInit.substr(exprBegin, exprEnd - exprBegin));
                                        auto adapted = adaptPackedVectorRhsFromArray(m, postProcessCppLine(p.type), expr);
                                        if (adapted != expr) {
                                            processedInit.replace(exprBegin, exprEnd - exprBegin, adapted);
                                        }
                                    }
                                }
                            }
                            if (isOutputPort && trim(processedInit).empty()) {
                                processedInit = " = _ASSIGN( " + postProcessCppLine(p.type) + "{} )";
                            }
	                            auto assignCombTarget = [](std::string text) {
	                                text = trim(std::move(text));
	                                if (!text.empty() && text.back() == ';') {
	                                    text.pop_back();
	                                    text = trim(std::move(text));
	                                }
	                                if (!text.empty() && text.front() == '=') {
	                                    text = trim(text.substr(1));
	                                }
	                                std::string prefix = "_ASSIGN_COMB(";
	                                if (text.rfind(prefix, 0) != 0) {
	                                    prefix = "_ASSIGN_REG(";
	                                }
	                                if (text.rfind(prefix, 0) != 0 || text.back() != ')') {
	                                    return std::string{};
	                                }
	                                return trim(text.substr(prefix.size(), text.size() - prefix.size() - 1));
	                            };
                            auto canForwardCombGetterTarget = [&](const std::string& target) {
                                return isSimpleCombRef(target) || isChildOutputPortCall(m, target);
                            };
	                            if (isOutputPort && processedInit.find("_ASSIGN_REG(") != std::string::npos) {
	                                auto regTarget = assignCombTarget(processedInit);
	                                if (regTarget.empty() || !canForwardCombGetterTarget(regTarget)) {
	                                    replaceAll(processedInit, "_ASSIGN_REG(", "_ASSIGN_COMB(");
	                                }
	                            }
	                            if (isOutputPort && initReferencesChild(processedInit)) {
                                if (!trim(processedInit).empty()) {
                                    auto eq = processedInit.find('=');
                                    if (eq != std::string::npos) {
                                        deferredPortInitLines.push_back(p.name + " " + processedInit.substr(eq) + ";");
                                    }
                                }
                                processedInit.clear();
                            }
			                h << "    _PORT(" << postProcessCppLine(p.type) << ") " << p.name << p.array << processedInit << ";\n";
			            }
	            h << "\nprivate:\n";
	            for (auto& member : m.members) {
	                h << "    " << postProcessCppLine(member) << "\n";
	            }
	            if (!m.members.empty()) {
	                h << "\n";
	            }
		            MethodGen structuralAssignMethod;
		            for (auto& line : m.assignLines) {
		                auto eq = line.find('=');
		                if (eq != std::string::npos && getterOutputPorts.count(trim(line.substr(0, eq)))) {
		                    continue;
		                }
		                if (!isStructuralAssignLine(line) ||
		                    (line.find("_ASSIGN(") == std::string::npos &&
		                     line.find("_ASSIGN_COMB(") == std::string::npos) ||
	                    line.find("_ASSIGN_I(") != std::string::npos ||
	                    line.find("_ASSIGN_COMB_I(") != std::string::npos) {
	                    continue;
	                }
	                auto boundLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, structuralAssignMethod, line)));
	                repairPatchedConcatOperandWidths(boundLine);
		                if (boundLine.find("_comb_func()") != std::string::npos) {
		                    line = finalAdaptStructuralAssignLine(m, boundLine);
		                }
		            }
		            synthesizeFieldCombMethods();
	            std::set<std::string> declaredPrivateNames;
	            auto emittedLineAssignsGetterOutput = [&](const std::string& emittedLine) {
	                auto eq = emittedLine.find('=');
	                if (eq == std::string::npos) {
	                    return false;
	                }
	                auto lhs = trim(emittedLine.substr(0, eq));
	                if (getterOutputPorts.count(lhs)) {
	                    return true;
	                }
		                for (const auto& out : m.outputPortCppNames) {
		                    if (out.second == lhs) {
		                        return true;
		                    }
		                }
		                for (const auto& port : m.ports) {
		                    if (port.direction == "output" && port.name == lhs) {
		                        return true;
		                    }
		                }
		                return false;
		            };
            auto containsGeneratedToken = [](const std::string& line, const std::string& token) {
                if (token.empty()) {
                    return false;
                }
                auto isIdent = [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                };
                for (size_t pos = 0; (pos = line.find(token, pos)) != std::string::npos;) {
                    auto end = pos + token.size();
                    auto leftOk = pos == 0 || !isIdent(line[pos - 1]);
                    auto rightOk = end >= line.size() || !isIdent(line[end]);
                    if (leftOk && rightOk) {
                        return true;
                    }
                    pos = end;
                }
                return false;
            };
            auto methodMentionsCombMethod = [&](const MethodGen& method, const MethodGen& candidate) {
                if (candidate.name.empty()) {
                    return false;
                }
                for (const auto& line : method.body) {
                    if (containsGeneratedToken(line, candidate.name + "()")) {
                        return true;
                    }
                }
                return false;
            };
            std::map<std::string, bool> combCycleCache;
            auto methodHasCombCycle = [&](const MethodGen& root) {
                if (root.name.empty()) {
                    return false;
                }
                if (auto it = combCycleCache.find(root.name); it != combCycleCache.end()) {
                    return it->second;
                }
                std::set<std::string> visited;
                auto dfs = [&](auto&& self, const MethodGen& current) -> bool {
                    for (const auto& candidate : m.methods) {
                        if (candidate.name.find("_comb_func") == std::string::npos) {
                            continue;
                        }
                        const bool includeReturnNames = candidate.name != current.name;
                        auto mentions = methodMentionsCombMethod(current, candidate);
                        if (!mentions && includeReturnNames) {
                            for (const auto& line : current.body) {
                                if (containsGeneratedToken(line, candidate.returnName) ||
                                    containsGeneratedToken(line, candidate.returnBase)) {
                                    mentions = true;
                                    break;
                                }
                            }
                        }
                        if (!mentions) {
                            continue;
                        }
                        if (candidate.name == root.name) {
                            return true;
                        }
                        if (!visited.insert(candidate.name).second) {
                            continue;
                        }
                        if (self(self, candidate)) {
                            return true;
                        }
                    }
                    return false;
                };
                auto hasCycle = dfs(dfs, root);
                combCycleCache[root.name] = hasCycle;
                return hasCycle;
            };
            auto markNoCacheMethod = [&](const MethodGen& method) {
                if (methodHasCombCycle(method)) {
                    return;
                }
                if (isChildOutputComb(m, method)) {
                    return;
                }
                if (!method.returnBase.empty()) {
                    m.noCacheCombBases.insert(method.returnBase);
                }
                if (!method.returnName.empty()) {
                    m.noCacheCombBases.insert(method.returnName);
                }
                if (!method.name.empty()) {
                    m.noCacheCombBases.insert(method.name);
                }
            };
            auto childModuleHasSequentialState = [](const ModuleGen* child) {
                if (!child) {
                    return false;
                }
                if (!child->seqAssignedVars.empty()) {
                    return true;
                }
                for (const auto& method : child->methods) {
                    if (method.args == "bool reset" && method.name.rfind("always_", 0) == 0) {
                        return true;
                    }
                }
                return false;
            };
            auto childInputPort = [](const ModuleGen* child, const std::string& portName) {
                if (!child) {
                    return false;
                }
                auto matches = [&](const std::string& candidate) {
                    return candidate == portName ||
                           candidate == portName + "_in" ||
                           candidate == portName + "_out";
                };
                auto cppIt = child->portCppNames.find(portName);
                for (const auto& port : child->ports) {
                    if (matches(port.name) ||
                        (cppIt != child->portCppNames.end() && port.name == cppIt->second)) {
                        return port.direction != "output";
                    }
                }
                return false;
            };
            for (const auto& conn : m.instanceConns) {
                if (!conn.connected) {
                    continue;
                }
                auto childType = trim(conn.type);
                if (auto lt = childType.find('<'); lt != std::string::npos) {
                    childType = trim(childType.substr(0, lt));
                }
                auto* child = findModule(conn.type);
                if (!child && !childType.empty()) {
                    child = findModule(childType);
                }
                if (!childModuleHasSequentialState(child) || !childInputPort(child, conn.port)) {
                    continue;
                }
                auto rhsBase = baseFromLValueText(conn.rhs);
                if (rhsBase.empty()) {
                    continue;
                }
                if (auto methodIt = m.combMethodByBase.find(rhsBase);
                    methodIt != m.combMethodByBase.end() && methodIt->second < m.methods.size()) {
                    markNoCacheMethod(m.methods[methodIt->second]);
                }
            }
            auto methodUsesDynamicInputSelect = [&](const MethodGen& method) {
                std::set<std::string> dynamicallyIndexedInputs;
                bool hasSelectorInput = false;
                for (const auto& port : m.ports) {
                    if (port.direction != "input") {
                        continue;
                    }
                    std::vector<std::string> names{port.name};
                    if (!port.name.empty()) {
                        names.push_back(port.name + "_in");
                    }
                    for (const auto& item : m.portCppNames) {
                        if (item.second == port.name) {
                            names.push_back(item.first);
                        }
                    }
                    for (const auto& name : names) {
                        if (name.empty()) {
                            continue;
                        }
                        for (const auto& line : method.body) {
                            const bool mentionsGetter = containsGeneratedToken(line, name + "()");
                            const bool mentionsSource = containsGeneratedToken(line, name);
                            if (!mentionsGetter && !mentionsSource) {
                                continue;
                            }
                            const bool indexesInputArray =
                                (line.find(name + "())") != std::string::npos &&
                                 line.find("[(unsigned") != std::string::npos) ||
                                (line.find(name + "()[") != std::string::npos &&
                                 line.find("[(unsigned") != std::string::npos) ||
                                line.find(name + "[") != std::string::npos;
                            const bool indexesInputBits =
                                line.find("(uint64_t)(" + name + "())") != std::string::npos &&
                                line.find(">> (unsigned") != std::string::npos;
                            if (indexesInputArray || indexesInputBits) {
                                dynamicallyIndexedInputs.insert(port.name);
                            }
                        }
                    }
                }
                if (dynamicallyIndexedInputs.empty()) {
                    return false;
                }
                if (dynamicallyIndexedInputs.size() >= 2) {
                    return true;
                }
                for (const auto& port : m.ports) {
                    if (port.direction != "input" || dynamicallyIndexedInputs.count(port.name)) {
                        continue;
                    }
                    std::vector<std::string> names{port.name};
                    if (!port.name.empty()) {
                        names.push_back(port.name + "_in");
                    }
                    for (const auto& item : m.portCppNames) {
                        if (item.second == port.name) {
                            names.push_back(item.first);
                        }
                    }
                    for (const auto& name : names) {
                        if (name.empty()) {
                            continue;
                        }
                        for (const auto& line : method.body) {
                            if (containsGeneratedToken(line, name + "()") ||
                                containsGeneratedToken(line, name)) {
                                hasSelectorInput = true;
                                break;
                            }
                        }
                        if (hasSelectorInput) {
                            break;
                        }
                    }
                    if (hasSelectorInput) {
                        break;
                    }
                }
                return hasSelectorInput;
            };
            for (const auto& method : m.methods) {
                if (method.name.find("_comb_func") == std::string::npos) {
                    continue;
                }
                auto call = method.name + "()";
                bool usedByWork = false;
                for (const auto& candidate : m.methods) {
                    if (!(candidate.args == "bool reset" && candidate.name.rfind("always_", 0) == 0)) {
                        continue;
                    }
                    for (const auto& line : candidate.body) {
                        if (containsGeneratedToken(line, call) ||
                            (!method.returnBase.empty() &&
                             !m.seqAssignedVars.count(method.returnBase) &&
                             containsGeneratedToken(line, method.returnBase)) ||
                            containsGeneratedToken(line, method.returnName)) {
                            usedByWork = true;
                            break;
                        }
                    }
                    if (usedByWork) {
                        break;
                    }
                }
                if (usedByWork) {
                    markNoCacheMethod(method);
                }
            }
            bool expandedNoCache = true;
            while (expandedNoCache) {
                expandedNoCache = false;
                for (const auto& method : m.methods) {
                    if (method.name.find("_comb_func") == std::string::npos) {
                        continue;
                    }
                    auto methodMarked = (!method.returnBase.empty() && m.noCacheCombBases.count(method.returnBase)) ||
                        (!method.returnName.empty() && m.noCacheCombBases.count(method.returnName)) ||
                        m.noCacheCombBases.count(method.name);
                    if (!methodMarked) {
                        continue;
                    }
                    auto before = m.noCacheCombBases.size();
                    for (const auto& candidate : m.methods) {
                        if (candidate.name.find("_comb_func") == std::string::npos ||
                            candidate.name == method.name) {
                            continue;
                        }
                        if (methodMentionsCombMethod(method, candidate)) {
                            markNoCacheMethod(candidate);
                        }
                    }
                    expandedNoCache = expandedNoCache || m.noCacheCombBases.size() != before;
                }
            }
            std::set<std::string> inlineCombReturnNames;
            if (configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", m.name)) {
                for (auto& [key, body] : configuredTextMap("HDLCPP_INLINE_COMB_BODIES")) {
                    auto sep = key.find('.');
                    if (sep == std::string::npos || key.substr(0, sep) != m.name) {
                        continue;
                    }
                    size_t searchPos = 0;
                    while ((searchPos = body.find("_LAZY_COMB(", searchPos)) != std::string::npos) {
                        auto nameBegin = searchPos + std::string("_LAZY_COMB(").size();
                        auto nameEnd = body.find(',', nameBegin);
                        if (nameEnd != std::string::npos) {
                            inlineCombReturnNames.insert(trim(body.substr(nameBegin, nameEnd - nameBegin)));
                        }
                        searchPos = nameBegin;
                    }
                }
            }
            for (auto& p : m.outputPortCppNames) {
                const bool readonlyCombOutput =
                    configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + p.first) ||
                    configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + p.second);
                if (readonlyCombOutput) {
                    continue;
                }
                if (isAssignOnlyOutput(m, p.first)) {
                    continue;
                }
                auto storageName = outputStorageName(m, p.first);
                if (inlineCombReturnNames.count(storageName)) {
                    continue;
                }
                bool storageDeclaredByLazyComb = false;
                if (auto it = m.combMethodByBase.find(p.first);
                    it != m.combMethodByBase.end() && it->second < m.methods.size()) {
                    const auto& method = m.methods[it->second];
                    if (noCacheCombMethod(m, method) || method.localCombBody) {
                        continue;
                    }
                    storageDeclaredByLazyComb = method.returnName == outputStorageName(m, p.first) &&
                        !noCacheCombMethod(m, method);
                }
                if (!storageDeclaredByLazyComb) {
                    for (const auto& method : m.methods) {
                        if (method.returnName == storageName && !noCacheCombMethod(m, method)) {
                            storageDeclaredByLazyComb = true;
                            break;
                        }
                    }
                }
                if (storageDeclaredByLazyComb) {
                    continue;
                }
                auto storageType = outputStorageType(m, p.first, p.second);
                if (m.seqAssignedVars.count(p.first)) {
                    storageType = regTypeFor(storageType);
                }
                if (!storageName.empty() && !declaredPrivateNames.count(storageName)) {
                    h << "    " << storageType << " " << storageName << ";\n";
                    declaredPrivateNames.insert(storageName);
                }
            }
            for (auto& v : m.vars) {
                if (m.bridgeAssignVars.count(v.second)) {
                    continue;
                }
                bool isOutputStorage = false;
                for (const auto& out : m.outputPortCppNames) {
                    if (v.second == outputStorageName(m, out.first) ||
                        v.second == combStorageName(m, out.first)) {
                        isOutputStorage = true;
                        break;
                    }
                }
                if (isOutputStorage) {
                    continue;
                }
                if (declaredPrivateNames.count(v.second)) {
                    continue;
                }
                if (inlineCombReturnNames.count(v.second)) {
                    continue;
                }
                bool storageOwnedByLazyComb = false;
                for (const auto& method : m.methods) {
                    if (method.returnName == v.second) {
                        storageOwnedByLazyComb = true;
                        break;
                    }
                }
                if (storageOwnedByLazyComb) {
                    continue;
                }
                if (auto it = m.combMethodByBase.find(v.second);
                    it != m.combMethodByBase.end() && it->second < m.methods.size() &&
                    !m.seqAssignedVars.count(v.second) && !noCacheCombMethod(m, m.methods[it->second])) {
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
                declaredPrivateNames.insert(v.second);
            }
            h << "\n";
	            if (configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", m.name)) {
                    for (auto& [key, body] : configuredTextMap("HDLCPP_INLINE_COMB_BODIES")) {
                        auto sep = key.find('.');
                        if (sep == std::string::npos || key.substr(0, sep) != m.name) {
                            continue;
                        }
                        size_t searchPos = 0;
                        while ((searchPos = body.find("_LAZY_COMB(", searchPos)) != std::string::npos) {
                            auto nameBegin = searchPos + std::string("_LAZY_COMB(").size();
                            auto nameEnd = body.find(',', nameBegin);
                            if (nameEnd != std::string::npos) {
                                inlineCombReturnNames.insert(trim(body.substr(nameBegin, nameEnd - nameBegin)));
                            }
                            searchPos = nameBegin;
                        }
                        std::stringstream ss(body);
                        std::string bodyLine;
                        while (std::getline(ss, bodyLine)) {
                            h << bodyLine << "\n";
                        }
                    }
                }
            for (auto& out : m.outputPortCppNames) {
                if (isCombOnlyOutput(m, out.first) &&
                    (configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + out.first) ||
                     configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", m.name + "|" + out.second))) {
                    emitReadonlyCombOutputMethod(h, m, out.first);
                }
            }
	            if (tracePhases) {
	                std::cerr << "HDLCPP_PHASE comb_methods begin " << m.name << "\n";
	            }
                for (const auto& method : m.methods) {
                    if (method.name.find("_comb_func") == std::string::npos) {
                        continue;
                    }
                    if (methodUsesDynamicInputSelect(method)) {
                        markNoCacheMethod(method);
                    }
                }
		            auto combMethodUsedByWork = [&](const MethodGen& method) {
	                if (method.name.empty()) {
	                    return false;
	                }
                auto call = method.name + "()";
                for (const auto& candidate : m.methods) {
                    if (!(candidate.args == "bool reset" && candidate.name.rfind("always_", 0) == 0)) {
                        continue;
                    }
                    for (const auto& line : candidate.body) {
                        if (line.find(call) != std::string::npos ||
                            (!method.returnBase.empty() &&
                             !m.seqAssignedVars.count(method.returnBase) &&
                             line.find(method.returnBase) != std::string::npos) ||
                            (!method.returnName.empty() && line.find(method.returnName) != std::string::npos)) {
                            return true;
                        }
                    }
	                }
	                return false;
	            };
	            auto methodContainsCall = [](const MethodGen& method, const MethodGen& candidate) {
	                if (candidate.name.empty()) {
	                    return false;
	                }
	                auto isIdent = [](char c) {
	                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	                };
	                auto containsToken = [&](const std::string& line, const std::string& token) {
	                    if (token.empty()) {
	                        return false;
	                    }
	                    for (size_t pos = 0; (pos = line.find(token, pos)) != std::string::npos;) {
	                        auto end = pos + token.size();
	                        bool leftOk = pos == 0 || !isIdent(line[pos - 1]);
	                        bool rightOk = end >= line.size() || !isIdent(line[end]);
	                        if (leftOk && rightOk) {
	                            return true;
	                        }
	                        pos = end;
	                    }
	                    return false;
	                };
	                for (const auto& line : method.body) {
	                    if (containsToken(line, candidate.name + "()") ||
	                        containsToken(line, candidate.returnBase) ||
	                        containsToken(line, candidate.returnName)) {
	                        return true;
	                    }
	                }
	                return false;
	            };
	            auto orderedCombMethodIndices = [&]() {
	                std::vector<size_t> order;
	                std::vector<unsigned char> state(m.methods.size(), 0);
	                std::function<void(size_t)> visit = [&](size_t index) {
	                    if (index >= m.methods.size() || state[index] == 2) {
	                        return;
	                    }
	                    if (state[index] == 1) {
	                        return;
	                    }
	                    state[index] = 1;
	                    const auto& method = m.methods[index];
	                    if (method.name.find("_comb_func") != std::string::npos) {
	                        for (size_t candidateIndex = 0; candidateIndex < m.methods.size(); ++candidateIndex) {
	                            if (candidateIndex == index ||
	                                m.methods[candidateIndex].name.find("_comb_func") == std::string::npos) {
	                                continue;
	                            }
	                            if (methodContainsCall(method, m.methods[candidateIndex])) {
	                                visit(candidateIndex);
	                            }
	                        }
	                    }
	                    state[index] = 2;
	                    order.push_back(index);
	                };
	                for (size_t index = 0; index < m.methods.size(); ++index) {
	                    if (m.methods[index].name.find("_comb_func") != std::string::npos) {
	                        visit(index);
	                    }
	                }
	                return order;
	            }();
		            size_t emittedMethodCount = m.methods.size();
		            for (auto methodIndex : orderedCombMethodIndices) {
	                auto& f = m.methods[methodIndex];
	                if (f.name.find("_comb_func") == std::string::npos) {
	                    continue;
	                }
                if (!f.returnName.empty() && inlineCombReturnNames.count(f.returnName)) {
                    continue;
                }
                if (configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", m.name)) {
                    bool usesLoopLocalParam = false;
                    for (const auto& line : f.body) {
                        if (line.find("Idx0") != std::string::npos || line.find("Idx1") != std::string::npos) {
                            usesLoopLocalParam = true;
                            break;
                        }
                    }
                    if (usesLoopLocalParam && !combMethodUsedByWork(f)) {
                        continue;
                    }
                }
                if (tracePhases) {
                    std::cerr << "HDLCPP_PHASE comb_method " << m.name << "." << f.name
                              << " body=" << f.body.size()
                              << " locals=" << f.localNames.size() << "\n";
                }
                emitMethod(h, m, f);
                if (tracePhases) {
                    std::cerr << "HDLCPP_PHASE comb_method_done " << m.name << "." << f.name << "\n";
                }
            }
	            if (tracePhases) {
	                std::cerr << "HDLCPP_PHASE comb_methods done " << m.name << "\n";
	            }
	            h << "public:\n";
	            h << "    " << m.name << "()\n    {\n";
            h << "    }\n\n";
            std::map<std::string, std::vector<std::string>> instanceGuards;
            for (const auto& conn : m.instanceConns) {
                if (!conn.guards.empty() && instanceGuards[conn.instance].empty()) {
                    instanceGuards[conn.instance] = conn.guards;
                }
            }
	            for (auto& f : m.methods) {
                if (f.staticConstexpr) {
                    continue;
                }
	                if (f.name.find("_comb_func") != std::string::npos) {
	                    continue;
	                }
                if (f.args == "bool reset" && f.name.rfind("always_", 0) == 0) {
                    continue;
                }
                emitMethod(h, m, f);
            }
            h << "    void _work(bool reset)\n    {\n";
            for (auto& name : m.memberNames) {
                auto guardIt = instanceGuards.find(name);
                const auto& guards = guardIt == instanceGuards.end() ? std::vector<std::string>{} : guardIt->second;
                emitGuardOpen(h, guards, "        ");
                auto arr = m.memberArraySizes.find(name);
                if (arr != m.memberArraySizes.end()) {
                    h << "        " << postProcessCppLine("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + arr->second + ");i++) {") << "\n";
                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._work(reset);\n";
                    h << "        }\n";
                }
                else {
                    h << "        " << name << "._work(reset);\n";
                }
                emitGuardClose(h, guards, "        ");
            }
            {
                std::set<std::string> seededNext;
                for (auto& p : m.outputPortCppNames) {
                    if (!m.seqAssignedVars.count(p.first)) {
                        continue;
                    }
                    auto storage = outputStorageName(m, p.first);
                    if (!storage.empty() && seededNext.insert(storage).second) {
                        h << "        " << storage << "._next = " << storage << ";\n";
                    }
                }
                for (auto& v : m.vars) {
                    if (!m.seqAssignedVars.count(v.second)) {
                        continue;
                    }
                    auto emittedType = regTypeFor(v.first);
                    if (emittedType.rfind("reg<", 0) == 0 && !scheduledMemoryType(emittedType) &&
                        seededNext.insert(v.second).second) {
                        h << "        " << v.second << "._next = " << v.second << ";\n";
                    }
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
                        repairPatchedConcatOperandWidths(emittedLine);
                        if (auto eq = hdlcpp::topLevelAssignPos(emittedLine); eq != std::string::npos) {
                            auto rhs = emittedLine.substr(eq + 1);
                            for (const auto& sourceMethod : m.methods) {
                                if (sourceMethod.returnName.empty() ||
                                    sourceMethod.name.empty() ||
                                    sourceMethod.name == f.name ||
                                    sourceMethod.name.find("_comb_func") == std::string::npos) {
                                    continue;
                                }
                                replaceIdentifierAll(rhs, sourceMethod.returnName, sourceMethod.name + "()");
                            }
                            emittedLine = emittedLine.substr(0, eq + 1) + rhs;
                        }
                        auto trimmedLine = trim(emittedLine);
                        if (isStandaloneCombEvalStatement(trimmedLine)) {
                            continue;
                        }
                        auto beforeStrobe = configuredTextMap("HDLCPP_BEFORE_STROBE_LINE_CALLS");
                        if (auto it = beforeStrobe.find(m.name + "|" + trimmedLine); it != beforeStrobe.end()) {
                            std::stringstream ss(it->second);
                            std::string call;
                            while (std::getline(ss, call, ',')) {
                                h << "        " << trim(call) << ";\n";
                            }
                        }
                        emitBodyLine(h, emittedLine, true);
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
                auto guardIt = instanceGuards.find(name);
                const auto& guards = guardIt == instanceGuards.end() ? std::vector<std::string>{} : guardIt->second;
                emitGuardOpen(h, guards, "        ");
                auto arr = m.memberArraySizes.find(name);
                if (arr != m.memberArraySizes.end()) {
                    h << "        " << postProcessCppLine("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + arr->second + ");i++) {") << "\n";
                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._strobe();\n";
                    h << "        }\n";
                }
                else {
                    h << "        " << name << "._strobe();\n";
                }
                emitGuardClose(h, guards, "        ");
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
				            auto isCombStorageAssignBinding = [&](const std::string& line) {
				                if (line.find("_ASSIGN") == std::string::npos) {
				                    return false;
				                }
				                auto eq = line.find('=');
				                if (eq == std::string::npos) {
				                    return false;
				                }
				                auto lhsBase = baseFromLValueText(line.substr(0, eq));
				                if (lhsBase.empty()) {
				                    return false;
				                }
				                for (const auto& method : m.methods) {
				                    if (method.name.find("_comb_func") != std::string::npos &&
				                        !method.returnName.empty() && lhsBase == method.returnName) {
				                        return true;
				                    }
				                }
				                return false;
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
			                if (!lhs.empty() && isIdentifierUsed(lhs, lhs) && !m.varNames.count(lhs)) {
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
			                    auto directEq = line.find('=');
			                    if (directEq != std::string::npos && getterOutputPorts.count(trim(line.substr(0, directEq)))) {
			                        continue;
			                    }
			                    auto emittedAssignLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, line)));
			                    repairPatchedConcatOperandWidths(emittedAssignLine);
			                    if (isCombStorageAssignBinding(emittedAssignLine)) {
			                        continue;
			                    }
		                    for (auto& kv : assignLocalExprs) {
		                        if (isIdentifierUsed(emittedAssignLine, kv.first)) {
		                            auto replacement = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, kv.second)));
		                            repairPatchedConcatOperandWidths(replacement);
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
				                    emittedAssignLine = normalizeAssignWrapperForCombCalls(finalAdaptStructuralAssignLine(m, emittedAssignLine));
		                            emittedAssignLine = normalizeChildOutputPortAssignWrapper(m, emittedAssignLine);
		                            emittedAssignLine = normalizeBoolInputPortAssignWrapper(m, emittedAssignLine);
		                            emittedAssignLine = normalizeInputPortCombValueWrapper(emittedAssignLine);
		                            if (emittedLineAssignsGetterOutput(emittedAssignLine)) {
		                                continue;
		                            }
	                    auto arraySize = directMemberBindingArraySize(line);
                        auto assignGuardIt = m.structuralAssignGuards.find(emittedAssignLine);
                        const auto& assignGuards = assignGuardIt == m.structuralAssignGuards.end() ? std::vector<std::string>{} : assignGuardIt->second;
                        emitGuardOpen(h, assignGuards, "        ");
		                    if (!arraySize.empty()) {
		                        const std::string generatedLoopAliases[] = {"j", "k", "m", "z_gen", "w_gen"};
		                        for (auto& alias : generatedLoopAliases) {
		                            if (isIdentifierUsed(emittedAssignLine, alias)) {
		                                replaceIdentifierAll(emittedAssignLine, alias, "i");
		                            }
		                        }
		                        replaceAll(emittedAssignLine, "_ASSIGN_INDEXED((i,i),", "_ASSIGN_I(");
		                        replaceAll(emittedAssignLine, "_ASSIGN_COMB_INDEXED((i,i),", "_ASSIGN_COMB_I(");
		                        emittedAssignLine = normalizeAssignWrapperForCombCalls(std::move(emittedAssignLine));
		                        emittedAssignLine = finalAdaptStructuralAssignLine(m, emittedAssignLine);
		                        emittedAssignLine = downgradeRvalueCombAssignWrappers(std::move(emittedAssignLine));
                                if (auto eq = emittedAssignLine.find('='); eq != std::string::npos) {
                                    auto lhsBase = baseFromLValueText(emittedAssignLine.substr(0, eq));
                                    if (auto renameIt = m.wireMap.find(lhsBase);
                                        renameIt != m.wireMap.end() && !moduleMethodExists(m, renameIt->second)) {
                                        hdlcpp::rewriteLhsBase(emittedAssignLine, renameIt->second, lhsBase);
                                    }
                                }
		                        h << "        " << postProcessCppLine("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + arraySize + ");i++) {") << "\n";
		                        h << "            " << emittedAssignLine << "\n";
		                        h << "        }\n";
		                    }
		                    else {
		                        h << "        " << emittedAssignLine << "\n";
		                    }
                        emitGuardClose(h, assignGuards, "        ");
		                }
		            }
		            if (!configuredNameEquals("HDLCPP_SKIP_ASSIGN_MODULES", m.name)) {
		                for (auto& line : m.assignLines) {
		                    if (isDirectMemberBinding(line) || !isStructuralAssignLine(line) || trim(line).find("._assign(") != std::string::npos) {
		                        continue;
		                    }
			                    auto eq = line.find('=');
			                    if (eq != std::string::npos && getterOutputPorts.count(trim(line.substr(0, eq)))) {
			                        continue;
			                    }
			                    if (eq != std::string::npos && m.bridgeAssignVars.count(baseFromLValueText(line.substr(0, eq)))) {
			                        continue;
			                    }
			                    auto emittedAssignLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, line)));
			                    repairPatchedConcatOperandWidths(emittedAssignLine);
			                    if (isCombStorageAssignBinding(emittedAssignLine)) {
			                        continue;
			                    }
		                    for (auto& kv : assignLocalExprs) {
		                        if (isIdentifierUsed(emittedAssignLine, kv.first)) {
		                            auto replacement = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, assignMethod, kv.second)));
		                            repairPatchedConcatOperandWidths(replacement);
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
				                    emittedAssignLine = normalizeAssignWrapperForCombCalls(finalAdaptStructuralAssignLine(m, emittedAssignLine));
		                            emittedAssignLine = normalizeChildOutputPortAssignWrapper(m, emittedAssignLine);
		                            emittedAssignLine = normalizeBoolInputPortAssignWrapper(m, emittedAssignLine);
		                            emittedAssignLine = normalizeInputPortCombValueWrapper(emittedAssignLine);
                            if (auto eq = emittedAssignLine.find('='); eq != std::string::npos) {
                                auto lhsBase = baseFromLValueText(emittedAssignLine.substr(0, eq));
                                if (auto renameIt = m.wireMap.find(lhsBase);
                                    renameIt != m.wireMap.end() && !moduleMethodExists(m, renameIt->second)) {
                                    hdlcpp::rewriteLhsBase(emittedAssignLine, renameIt->second, lhsBase);
                                }
                            }
		                            if (emittedLineAssignsGetterOutput(emittedAssignLine)) {
		                                continue;
		                            }
	                    if (configuredNameEquals("HDLCPP_SKIP_ASSIGN_LINE_PREFIXES", m.name + "|" + trim(emittedAssignLine).substr(0, trim(emittedAssignLine).find(' ')))) {
		                        continue;
		                    }
		                    if (auto patches = configuredTextMap("HDLCPP_ASSIGN_LINE_PATCHES"); patches.count(m.name + "|" + trim(emittedAssignLine))) {
		                        emittedAssignLine = patches[m.name + "|" + trim(emittedAssignLine)];
		                    }
                        auto assignGuardIt = m.structuralAssignGuards.find(emittedAssignLine);
                        const auto& assignGuards = assignGuardIt == m.structuralAssignGuards.end() ? std::vector<std::string>{} : assignGuardIt->second;
                        emitGuardOpen(h, assignGuards, "        ");
		                    h << "        " << emittedAssignLine << "\n";
                        emitGuardClose(h, assignGuards, "        ");
		                }
		            }
		            if (auto code = configuredTextMap("HDLCPP_ASSIGN_SUFFIX_CODE"); code.count(m.name)) {
                        std::stringstream ss(code[m.name]);
                        std::string codeLine;
                        while (std::getline(ss, codeLine)) {
                            h << "        " << codeLine << "\n";
                        }
		            }
	            for (auto& line : deferredPortInitLines) {
	                auto emittedDeferredLine = normalizeChildOutputPortAssignWrapper(m, line);
	                if (emittedLineAssignsGetterOutput(emittedDeferredLine)) {
	                    continue;
	                }
	                h << "        " << emittedDeferredLine << "\n";
	            }
	            for (auto& name : m.memberNames) {
                    auto guardIt = instanceGuards.find(name);
                    const auto& guards = guardIt == instanceGuards.end() ? std::vector<std::string>{} : guardIt->second;
                    emitGuardOpen(h, guards, "        ");
	                auto arr = m.memberArraySizes.find(name);
	                if (arr != m.memberArraySizes.end()) {
	                    h << "        " << postProcessCppLine("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + arr->second + ");i++) {") << "\n";
	                    h << "            " << name << "[(unsigned)(uint64_t)((uint64_t)(i))]._assign();\n";
	                    h << "        }\n";
                }
                else {
	                    h << "        " << name << "._assign();\n";
	                }
                    emitGuardClose(h, guards, "        ");
	            }
				            h << "    }\n\n";
	            for (size_t methodIndex = emittedMethodCount; methodIndex < m.methods.size(); ++methodIndex) {
	                emitMethod(h, m, m.methods[methodIndex]);
	            }
	            h << "};\n\n";
		        }
    }

    void emitInstanceConnections(ModuleGen& m)
    {
        for (auto& line : m.assignLines) {
            if (isControlOrScopeLine(line)) {
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto base = baseFromLValueText(line.substr(0, eq));
            bool outputAssignLine = false;
            for (auto& out : m.outputPortCppNames) {
                if (base == outputStorageName(m, out.first) || base == combStorageName(m, out.first) ||
                    base == out.second) {
                    outputAssignLine = true;
                    if (!m.seqAssignedVars.count(out.first)) {
                        m.combAssignedVars.insert(out.first);
                        m.runtimeAssignDrivenVars.insert(out.first);
                    }
                    break;
                }
            }
            if (isStructuralAssignLine(line) && !outputAssignLine) {
                continue;
            }
            if (!base.empty() && m.varNames.count(base) && !m.seqAssignedVars.count(base) &&
                !m.outputPortCppNames.count(base) && !m.combMethodByBase.count(base)) {
                m.combAssignedVars.insert(base);
                m.runtimeAssignDrivenVars.insert(base);
            }
        }
        auto substituteConfiguredPortType = [&](const std::string& moduleType, const std::string& params, std::string typeText) {
            if (typeText.empty()) {
                return typeText;
            }
            auto typeAliasOverrides = configuredTextMap("HDLCPP_TYPE_ALIAS_OVERRIDES");
            for (const auto& aliasItem : typeAliasOverrides) {
                auto aliasName = aliasItem.first;
                auto dot = aliasName.rfind('.');
                if (dot != std::string::npos) {
                    if (aliasName.substr(0, dot) != moduleType) {
                        continue;
                    }
                    aliasName = aliasName.substr(dot + 1);
                }
                if (!aliasName.empty() && !aliasItem.second.empty()) {
                    replaceIdentifierAll(typeText, aliasName, aliasItem.second);
                }
            }
            auto* configuredChild = findModule(moduleType);
            if (configuredChild) {
                std::set<std::string> declaredParamNames;
                for (const auto& declared : configuredChild->params) {
                    auto name = templateParamName(declared);
                    if (!name.empty()) {
                        declaredParamNames.insert(name);
                    }
                }
                for (const auto& typeItem : configuredChild->types) {
                    if (declaredParamNames.count(typeItem.first)) {
                        continue;
                    }
                    if (!typeItem.first.empty() && !typeItem.second.empty() && typeItem.first != typeItem.second) {
                        replaceIdentifierAll(typeText, typeItem.first, typeItem.second);
                    }
                }
                auto localAliases = localUsingTypeAliases(*configuredChild);
                for (const auto& aliasItem : localAliases) {
                    if (!aliasItem.first.empty() && !aliasItem.second.empty()) {
                        replaceIdentifierAll(typeText, aliasItem.first, aliasItem.second);
                    }
                }
            }
            if (params.empty()) {
                return typeText;
            }
            if (!configuredChild || configuredChild->params.empty()) {
                return substituteModuleParamValues(moduleType, params, typeText);
            }
            return substituteParamDeclValues(configuredChild->params, splitTopLevelArgs(params), std::move(typeText));
        };
        auto sanitizeGeneratedName = [](std::string text) {
            for (auto& ch : text) {
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                    ch = '_';
                }
            }
            if (text.empty() || std::isdigit(static_cast<unsigned char>(text[0]))) {
                text = "_" + text;
            }
            return text;
        };
        auto needsCombPortBinding = [](const std::string& rhs) {
            return !isSimpleCombRef(rhs) &&
                   rhs.find("_comb_func()") != std::string::npos;
        };
	        auto isParentPortAccessRef = [&](const std::string& rhs) {
	            auto expr = trim(rhs);
	            auto containsGetterCall = [&](const std::string& name) {
	                auto call = name + "()";
	                size_t pos = 0;
	                while ((pos = expr.find(call, pos)) != std::string::npos) {
	                    const bool beforeOk = pos == 0 ||
	                        !(std::isalnum(static_cast<unsigned char>(expr[pos - 1])) || expr[pos - 1] == '_');
	                    const auto afterPos = pos + call.size();
	                    const bool afterOk = afterPos >= expr.size() ||
	                        !(std::isalnum(static_cast<unsigned char>(expr[afterPos])) || expr[afterPos] == '_');
	                    if (beforeOk && afterOk) {
	                        return true;
	                    }
	                    pos += call.size();
	                }
	                return false;
	            };
	            for (const auto& port : m.ports) {
                std::vector<std::string> names{port.name};
                auto mapped = m.portCppNames.find(port.name);
                if (mapped != m.portCppNames.end()) {
                    names.push_back(mapped->second);
                }
                for (const auto& item : m.portCppNames) {
                    if (item.second == port.name) {
                        names.push_back(item.first);
                    }
                }
                for (auto name : names) {
                    name = trim(std::move(name));
                    if (name.empty()) {
                        continue;
                    }
                    if (containsGetterCall(name)) {
                        return true;
                    }
                    auto call = name + "()";
                    auto pos = expr.find(call);
                    if (pos == std::string::npos) {
                        continue;
                    }
                    bool prefixOk = true;
                    for (size_t i = 0; i < pos; ++i) {
                        if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                            prefixOk = false;
                            break;
                        }
                    }
                    if (!prefixOk) {
                        continue;
                    }
                    auto rest = trim(expr.substr(pos + call.size()));
                    while (!rest.empty() && rest.front() == ')') {
                        rest = trim(rest.substr(1));
                    }
                    if (rest.empty() || rest.front() == '[' || rest.front() == '.') {
                        return true;
                    }
                }
	            }
	            return false;
	        };
	        auto isExactParentPortRef = [&](const std::string& rhs) {
	            auto expr = trim(rhs);
	            for (const auto& port : m.ports) {
	                std::vector<std::string> names{port.name};
	                auto mapped = m.portCppNames.find(port.name);
	                if (mapped != m.portCppNames.end()) {
	                    names.push_back(mapped->second);
	                }
	                for (const auto& item : m.portCppNames) {
	                    if (item.second == port.name) {
	                        names.push_back(item.first);
	                    }
	                }
	                for (auto name : names) {
	                    name = trim(std::move(name));
	                    if (!name.empty() && expr == name + "()") {
	                        return true;
	                    }
	                }
	            }
	            return false;
	        };
	        auto isCombAccessRef = [](std::string rhs) {
            auto expr = trim(std::move(rhs));
            auto pos = expr.find("_comb_func()");
            if (pos == std::string::npos) {
                return false;
            }
            auto callEnd = pos + std::string("_comb_func()").size();
            auto start = pos;
            while (start > 0) {
                auto c = expr[start - 1];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                    break;
                }
                --start;
            }
            if (start == pos) {
                return false;
            }
            for (size_t i = 0; i < start; ++i) {
                if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                    return false;
                }
            }
            auto rest = trim(expr.substr(callEnd));
            while (!rest.empty() && rest.front() == ')') {
                rest = trim(rest.substr(1));
            }
            return rest.empty() || rest.front() == '.' || rest.front() == '[';
        };
        auto isAddressableBindingRhs = [&](const std::string& rhs) {
            return isCombAddressableExpr(rhs) || isExactParentPortRef(rhs) ||
                   isCpphdlGetterAddressableExpr(rhs) || isCombAccessRef(rhs);
        };
        auto materializeCombPortBinding = [&](const std::string& instance, const std::string& portName,
                                              const std::string& typeText, const std::string& rhs) {
            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName);
            auto unique = base;
            unsigned suffix = 0;
            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
                unique = base + "_" + std::to_string(++suffix);
            }

            auto retType = trim(typeText);
            if (!instance.empty() && !portName.empty()) {
                retType = "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>";
            }
            if (retType.empty()) {
                retType = "bool";
            }
            m.types[unique] = retType;
            m.varNames.insert(unique);
            m.combAssignedVars.insert(unique);

            MethodGen method;
            method.name = unique + "_comb_func";
            method.ret = retType + "&";
            method.returnName = combStorageName(m, unique);
            method.returnBase = unique;
            m.combReturnTypes[method.returnName] = retType;
            m.combMethodByBase[unique] = m.methods.size();
            m.wireMap[unique] = method.name;
            if (retType == "bool" || retType == "u1") {
                method.body.push_back(unique + " = bool(" + rhs + ");");
            }
            else {
                method.body.push_back(unique + " = " + rhs + ";");
            }
            m.methods.push_back(method);
            return method.name + "()";
        };
        auto materializeArrayPackPortBinding = [&](const std::string& instance, const std::string& portName,
                                                   const std::string& rhs) {
            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName + "_packed_array");
            auto unique = base;
            unsigned suffix = 0;
            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
                unique = base + "_" + std::to_string(++suffix);
            }

            auto retType = "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>";
            auto storage = combStorageName(m, unique);
            m.types[unique] = retType;
            m.varNames.insert(unique);
            m.combAssignedVars.insert(unique);

            MethodGen method;
            method.name = unique + "_comb_func";
            method.ret = retType + "&";
            method.returnName = storage;
            method.returnBase = unique;
            m.combReturnTypes[storage] = retType;
            m.combMethodByBase[unique] = m.methods.size();
            m.wireMap[unique] = method.name;
            method.body.push_back("{");
            method.body.push_back("    using __cpphdl_target_t = " + retType + ";");
            method.body.push_back("    " + storage + " = {};");
            method.body.push_back("    auto __cpphdl_src = " + rhs + ";");
            method.body.push_back("    if constexpr (requires { __cpphdl_target_t::SIZE_BITS; __cpphdl_target_t::ELEMENT_BITS; }) {");
            method.body.push_back("        for (size_t __cpphdl_i = 0; __cpphdl_i < (__cpphdl_target_t::SIZE_BITS / __cpphdl_target_t::ELEMENT_BITS); ++__cpphdl_i) {");
            method.body.push_back("            " + storage + "[__cpphdl_i] = cpphdl::pack_value<__cpphdl_target_t::ELEMENT_BITS>(__cpphdl_src[__cpphdl_i]);");
            method.body.push_back("        }");
            method.body.push_back("    }");
            method.body.push_back("}");
            m.methods.push_back(method);
            return method.name + "()";
        };
        auto materializePackedToArrayPortBinding = [&](const std::string& instance, const std::string& portName,
                                                       const std::string& targetType, const std::string& rhs) {
            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName + "_packed_to_array");
            auto unique = base;
            unsigned suffix = 0;
            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
                unique = base + "_" + std::to_string(++suffix);
            }

            auto retType = "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>";
            auto storage = combStorageName(m, unique);
            m.types[unique] = retType;
            m.varNames.insert(unique);
            m.combAssignedVars.insert(unique);

            MethodGen method;
            method.name = unique + "_comb_func";
            method.ret = retType + "&";
            method.returnName = storage;
            method.returnBase = unique;
            m.combReturnTypes[storage] = retType;
            m.combMethodByBase[unique] = m.methods.size();
            m.wireMap[unique] = method.name;
            method.body.push_back("{");
            method.body.push_back("    using __cpphdl_target_array_t = " + retType + ";");
            method.body.push_back("    using __cpphdl_target_elem_t = std::remove_cvref_t<decltype(std::declval<const __cpphdl_target_array_t&>()[0])>;");
            method.body.push_back("    constexpr size_t __cpphdl_target_count = __cpphdl_target_array_t::SIZE_BITS / __cpphdl_target_array_t::ELEMENT_BITS;");
            method.body.push_back("    constexpr size_t __cpphdl_target_elem_bits = cpphdl::type_width<__cpphdl_target_elem_t>();");
            method.body.push_back("    " + storage + " = {};");
            method.body.push_back("    auto __cpphdl_src = " + rhs + ";");
            method.body.push_back("    using __cpphdl_src_t = std::remove_cvref_t<decltype(__cpphdl_src)>;");
            method.body.push_back("    if constexpr (!cpphdl::is_logic_v<__cpphdl_src_t> && std::is_assignable_v<__cpphdl_target_array_t&, __cpphdl_src_t>) {");
            method.body.push_back("        " + storage + " = __cpphdl_src;");
            method.body.push_back("    }");
            method.body.push_back("    else if constexpr (!cpphdl::is_logic_v<__cpphdl_src_t> && requires { __cpphdl_src[0]; }) {");
            method.body.push_back("        if constexpr (std::is_assignable_v<__cpphdl_target_elem_t&, std::remove_cvref_t<decltype(__cpphdl_src[0])>>) {");
            method.body.push_back("            for (size_t __cpphdl_i = 0; __cpphdl_i < __cpphdl_target_count; ++__cpphdl_i) {");
            method.body.push_back("                " + storage + "[__cpphdl_i] = __cpphdl_src[__cpphdl_i];");
            method.body.push_back("            }");
            method.body.push_back("        }");
            method.body.push_back("        else {");
            method.body.push_back("            auto __cpphdl_packed = cpphdl::pack_value<__cpphdl_target_count * __cpphdl_target_elem_bits>(__cpphdl_src);");
            method.body.push_back("            for (size_t __cpphdl_i = 0; __cpphdl_i < __cpphdl_target_count; ++__cpphdl_i) {");
            method.body.push_back("                " + storage + "[__cpphdl_i] = cpphdl::unpack_value<__cpphdl_target_elem_t>(logic<__cpphdl_target_elem_bits>(__cpphdl_packed.bits((__cpphdl_i + 1) * __cpphdl_target_elem_bits - 1, __cpphdl_i * __cpphdl_target_elem_bits)));");
            method.body.push_back("            }");
            method.body.push_back("        }");
            method.body.push_back("    }");
            method.body.push_back("    else {");
            method.body.push_back("        auto __cpphdl_packed = cpphdl::pack_value<__cpphdl_target_count * __cpphdl_target_elem_bits>(__cpphdl_src);");
            method.body.push_back("        for (size_t __cpphdl_i = 0; __cpphdl_i < __cpphdl_target_count; ++__cpphdl_i) {");
            method.body.push_back("            " + storage + "[__cpphdl_i] = cpphdl::unpack_value<__cpphdl_target_elem_t>(logic<__cpphdl_target_elem_bits>(__cpphdl_packed.bits((__cpphdl_i + 1) * __cpphdl_target_elem_bits - 1, __cpphdl_i * __cpphdl_target_elem_bits)));");
            method.body.push_back("        }");
            method.body.push_back("    }");
            method.body.push_back("}");
            m.methods.push_back(method);
            return method.name + "()";
        };
        auto needsArrayPackPortBinding = [](const std::string& target, const std::string& source) {
            auto targetArgs = templateArgsFor(target, target.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            auto sourceArgs = templateArgsFor(source, source.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            if (targetArgs.size() < 2 || sourceArgs.size() < 2) {
                return false;
            }
            auto targetElem = trim(targetArgs[0]);
            auto sourceElem = trim(sourceArgs[0]);
            if (targetElem == sourceElem) {
                return false;
            }
            if (targetElem.rfind("logic<", 0) != 0 && targetElem.rfind("u<", 0) != 0) {
                return false;
            }
            return sourceElem.rfind("logic<", 0) != 0 && sourceElem.rfind("u<", 0) != 0;
        };
        auto needsArrayUnpackAssignment = [&](const std::string& target, const std::string& source) {
            auto targetArgs = templateArgsFor(target, target.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            auto sourceArgs = templateArgsFor(source, source.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            if (targetArgs.size() < 2 || sourceArgs.size() < 2) {
                return false;
            }
            auto targetElem = trim(targetArgs[0]);
            auto sourceElem = trim(sourceArgs[0]);
            if (targetElem == sourceElem || targetElem.empty()) {
                return false;
            }
            if (targetElem.rfind("logic<", 0) == 0 || targetElem.rfind("u<", 0) == 0 ||
                targetElem == "bool" || targetElem == "u1") {
                return false;
            }
            return true;
        };
        auto lhsStorageType = [&](const std::string& lhs) -> std::string {
            auto type = expressionStorageType(m, lhs);
            if (!type.empty()) {
                return type;
            }
            auto text = trim(lhs);
            auto dot = text.find('.');
            if (dot == std::string::npos) {
                return "";
            }
            auto decltypeFallback = [&]() {
                auto bound = trim(lateBindExpr(m, lhs, ""));
                if (bound.empty()) {
                    return std::string();
                }
                return "std::remove_cvref_t<decltype(" + bound + ")>";
            };
            auto base = trim(text.substr(0, dot));
            auto fields = trim(text.substr(dot + 1));
            std::string baseType;
            auto cppIt = m.outputPortCppNames.find(base);
            for (const auto& port : m.ports) {
                if (port.name == base || port.name == base + "_out" ||
                    (cppIt != m.outputPortCppNames.end() && port.name == cppIt->second)) {
                    baseType = port.type;
                    break;
                }
            }
            if (baseType.empty()) {
                if (auto it = m.types.find(base); it != m.types.end()) {
                    baseType = it->second;
                }
            }
            if (baseType.empty()) {
                return decltypeFallback();
            }
            std::stringstream ss(fields);
            std::string field;
            type = baseType;
            while (std::getline(ss, field, '.')) {
                field = trim(field);
                auto bracket = field.find('[');
                if (bracket != std::string::npos) {
                    field = trim(field.substr(0, bracket));
                }
                if (field.empty()) {
                    return "";
                }
                type = fieldTypeFor(type, field);
                if (type.empty()) {
                    return decltypeFallback();
                }
            }
            return type;
        };
        auto materializeArrayUnpackAssignment = [&](const std::string& instance, const std::string& portName,
                                                    const std::string& targetType, const std::string& rhs,
                                                    bool usePortType = false) {
            auto targetArgs = templateArgsFor(targetType, targetType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            if (targetArgs.empty()) {
                return rhs;
            }
            auto targetElem = trim(targetArgs[0]);
            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName + "_unpacked_array");
            auto unique = base;
            unsigned suffix = 0;
            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
                unique = base + "_" + std::to_string(++suffix);
            }

            auto retType = usePortType ? "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>" : targetType;
            auto storage = combStorageName(m, unique);
            m.types[unique] = retType;
            m.varNames.insert(unique);
            m.combAssignedVars.insert(unique);

            MethodGen method;
            method.name = unique + "_comb_func";
            method.ret = retType + "&";
            method.returnName = storage;
            method.returnBase = unique;
            m.combReturnTypes[storage] = retType;
            m.combMethodByBase[unique] = m.methods.size();
            m.wireMap[unique] = method.name;
            method.body.push_back("{");
            method.body.push_back("    using __cpphdl_target_array_t = " + retType + ";");
            method.body.push_back("    using __cpphdl_target_elem_t = std::remove_cvref_t<decltype(std::declval<const __cpphdl_target_array_t&>()[0])>;");
            method.body.push_back("    " + storage + " = {};");
            method.body.push_back("    auto __cpphdl_src = " + rhs + ";");
            method.body.push_back("    using __cpphdl_src_elem_t = std::remove_cvref_t<decltype(__cpphdl_src[0])>;");
            method.body.push_back("    for (size_t __cpphdl_i = 0; __cpphdl_i < (__cpphdl_target_array_t::SIZE_BITS / __cpphdl_target_array_t::ELEMENT_BITS); ++__cpphdl_i) {");
            method.body.push_back("        if constexpr (std::is_assignable_v<__cpphdl_target_elem_t&, __cpphdl_src_elem_t>) {");
            method.body.push_back("            " + storage + "[__cpphdl_i] = __cpphdl_src[__cpphdl_i];");
            method.body.push_back("        }");
            method.body.push_back("        else {");
            method.body.push_back("            " + storage + "[__cpphdl_i] = cpphdl::unpack_value<__cpphdl_target_elem_t>(cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_elem_t>()>(__cpphdl_src[__cpphdl_i]));");
            method.body.push_back("        }");
            method.body.push_back("    }");
            method.body.push_back("}");
            m.methods.push_back(method);
            return method.name + "()";
        };
        auto packedPortConversionExpr = [](const std::string& targetType, const std::string& rhs) {
            return "cpphdl::unpack_value<" + targetType + ">(cpphdl::pack_value<cpphdl::type_width<" +
                   targetType + ">()>(" + rhs + "))";
        };
        for (auto& conn : m.instanceConns) {
            if (isClockPortName(conn.port)) {
                continue;
            }
            auto resolvedChildType = resolveChildTypeForInstance(m, conn.instance);
            if (resolvedChildType.empty()) {
                resolvedChildType = conn.type;
            }
            auto childElementType = arrayElementTypeText(resolvedChildType);
            std::string resolvedChildParams;
            auto childBaseType = trim(childElementType);
            if (auto lt = childBaseType.find('<'); lt != std::string::npos) {
                int angleDepth = 0;
                size_t gt = std::string::npos;
                for (size_t pos = lt; pos < childElementType.size(); ++pos) {
                    if (childElementType[pos] == '<') {
                        ++angleDepth;
                    }
                    else if (childElementType[pos] == '>' && --angleDepth == 0) {
                        gt = pos;
                        break;
                    }
                }
                if (gt != std::string::npos && gt > lt) {
                    resolvedChildParams = childElementType.substr(lt + 1, gt - lt - 1);
                }
                childBaseType = trim(childBaseType.substr(0, lt));
            }
            auto connParamsForType = !resolvedChildParams.empty() ? resolvedChildParams : conn.params;
            auto* child = findModule(childElementType);
            if (!child && !childBaseType.empty()) {
                child = findModule(childBaseType);
            }
            auto portName = conn.port;
            bool isOutput = false;
            std::string portType = "bool";
            bool portTypeKnown = false;
            bool outputIsPortRef = false;
            if (child) {
                if (child->portCppNames.count(conn.port)) {
                    portName = child->portCppNames[conn.port];
                }
                bool knownPort = false;
                auto portNameMatches = [&](const std::string& candidate) {
                    return candidate == portName ||
                           candidate == conn.port ||
                           candidate == conn.port + "_in" ||
                           candidate == conn.port + "_out";
                };
                for (auto& p : child->ports) {
                    if (portNameMatches(p.name)) {
                        knownPort = true;
                        portName = p.name;
                        portType = p.type;
                        portType = substituteConfiguredPortType(childBaseType.empty() ? conn.type : childBaseType, connParamsForType, portType);
                        portTypeKnown = true;
                        if (p.direction == "output") {
                            isOutput = true;
                            outputIsPortRef = true;
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
            if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES");
                portTypes.count(m.name + "." + conn.instance + "." + conn.port) ||
                portTypes.count(conn.instance + "." + conn.port) ||
                portTypes.count(conn.type + "." + conn.port)) {
                auto specIt = portTypes.find(m.name + "." + conn.instance + "." + conn.port);
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(conn.instance + "." + conn.port);
                }
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(conn.type + "." + conn.port);
                }
                auto spec = specIt->second;
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
                    portType = substituteConfiguredPortType(childBaseType.empty() ? conn.type : childBaseType, connParamsForType, configuredType);
                    portTypeKnown = true;
                }
            }
            if (isOutput) {
                if (conn.connected) {
                    auto outExpr = conn.instance + "." + portName + "()";
                    if (portType.rfind("array<", 0) == 0 || portType.rfind("std::array<", 0) == 0) {
                        auto lhsType = lhsStorageType(conn.lhs);
                        if (lhsType.rfind("array<", 0) != 0 && lhsType.rfind("std::array<", 0) != 0) {
                            auto packedTarget = trim(lhsType);
                            if (!packedTarget.empty() && packedTarget != "bool" && packedTarget != "u1") {
                                outExpr = "cpphdl::unpack_value<" + packedTarget + ">(cpphdl::pack_value<cpphdl::type_width<" +
                                          packedTarget + ">()>(" + outExpr + "))";
                            }
                            else {
                                outExpr += "[0]";
                            }
                        }
                        else if (needsArrayUnpackAssignment(lhsType, portType)) {
                            outExpr = materializeArrayUnpackAssignment(conn.instance, portName, lhsType, outExpr);
                        }
                    }
                    if (addConcatOutputAssignments(m, conn.lhs, outExpr)) {
                        continue;
                    }
                    auto lhsForOutput = conn.lhs;
                    auto outBase = baseFromLValueText(lhsForOutput);
                    if (!outBase.empty() && !m.outputPortCppNames.count(outBase)) {
                        for (const auto& mapped : m.wireMap) {
                            if (trim(lhsForOutput) == mapped.second && m.outputPortCppNames.count(mapped.first)) {
                                outBase = mapped.first;
                                lhsForOutput = mapped.first;
                                break;
                            }
                        }
                    }
                    if (conn.guards.empty() && !outBase.empty() && m.outputPortCppNames.count(outBase) &&
                        (trim(lhsForOutput) == outBase || trim(lhsForOutput) == m.outputPortCppNames[outBase])) {
                        m.assignExprByBase[outBase] = outExpr;
                        for (auto& p : m.ports) {
                            if (p.name == m.outputPortCppNames[outBase]) {
                                p.init = " = " + std::string(outputIsPortRef ? "_ASSIGN_COMB" : "_ASSIGN") + "( " + outExpr + " )";
                                break;
                            }
                        }
                        continue;
                    }
                    addCombAssignment(m, outBase, lhsForOutput, outExpr, conn.guards);
                }
            }
            else {
                auto rhs = conn.connected ? conn.rhs : (portType == "bool" ? "false" : portType + "(0)");
                auto rhsWasZeroLiteral = isZeroLiteralText(rhs);
                auto rawRhsBase = baseFromLValueText(rhs);
                auto sourceTypeBeforeLateBind = expressionStorageType(m, rhs);
                if (sourceTypeBeforeLateBind.empty() && !rawRhsBase.empty()) {
                    std::vector<std::string> sourceCandidates{rawRhsBase};
                    auto rhsTrimmed = trim(rhs);
                    if (hasSuffix(rhsTrimmed, "()")) {
                        sourceCandidates.push_back(rhsTrimmed.substr(0, rhsTrimmed.size() - 2));
                    }
                    for (const auto& item : m.portCppNames) {
                        if (item.second == rawRhsBase) {
                            sourceCandidates.push_back(item.first);
                        }
                    }
                    for (const auto& candidate : sourceCandidates) {
                        auto cppIt = m.portCppNames.find(candidate);
                        for (const auto& sourcePort : m.ports) {
                            if (sourcePort.name == candidate ||
                                sourcePort.name == candidate + "_in" ||
                                sourcePort.name == candidate + "_out" ||
                                (cppIt != m.portCppNames.end() && sourcePort.name == cppIt->second)) {
                                sourceTypeBeforeLateBind = sourcePort.type;
                                break;
                            }
                        }
                        if (!sourceTypeBeforeLateBind.empty()) {
                            break;
                        }
                    }
                }
                auto boundName = bridgeBoundName(m, rhs);
                auto bridge = !boundName.empty() && m.assignExprByBase.count(boundName) &&
                              isAssignDrivenVar(m, boundName);
                if (bridge) {
                    m.bridgeAssignVars.insert(boundName);
                    rhs = m.assignExprByBase[boundName];
                }
                rhs = lateBindExpr(m, rhs, "");
                {
                    auto recoverGetterType = [&](const std::string& getterName) {
                        std::string recoveredType;
                        for (const auto& sourcePort : m.ports) {
                            if (sourcePort.name == getterName) {
                                recoveredType = sourcePort.type;
                                break;
                            }
                        }
                        if (recoveredType.empty()) {
                            for (const auto& method : m.methods) {
                                if (method.name == getterName) {
                                    auto returnName = method.returnName.empty() ? method.returnBase : method.returnName;
                                    if (!returnName.empty()) {
                                        if (auto typeIt = m.combReturnTypes.find(returnName); typeIt != m.combReturnTypes.end()) {
                                            recoveredType = typeIt->second;
                                        }
                                        else if (auto typeIt = m.types.find(returnName); typeIt != m.types.end()) {
                                            recoveredType = typeIt->second;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        if (recoveredType.empty() && hasSuffix(getterName, "_comb_func")) {
                            auto base = getterName.substr(0, getterName.size() - std::string("_comb_func").size());
                            auto storage = combStorageName(m, base);
                            if (auto typeIt = m.combReturnTypes.find(storage); typeIt != m.combReturnTypes.end()) {
                                recoveredType = typeIt->second;
                            }
                            else if (auto typeIt = m.types.find(storage); typeIt != m.types.end()) {
                                recoveredType = typeIt->second;
                            }
                            else if (auto typeIt = m.types.find(base); typeIt != m.types.end()) {
                                recoveredType = typeIt->second;
                            }
                        }
                        return recoveredType;
                    };
                    auto getterName = leadingCpphdlGetterName(rhs);
                    if (!getterName.empty()) {
                        auto recoveredType = recoverGetterType(getterName);
                        if (!recoveredType.empty() &&
                            (sourceTypeBeforeLateBind.empty() ||
                             (cpphdlGetterExprIndexesArrayElement(rhs) && arrayTypeIsPacked(recoveredType)))) {
                            sourceTypeBeforeLateBind = recoveredType;
                        }
                    }
                }
                auto rhsExactLocalStateRef = [&]() {
                    auto s = trim(rhs);
                    auto base = baseFromLValueText(s);
                    return !base.empty() && s == base && m.varNames.count(base);
                }();
                auto rhsIsExactLocalRegisterRef = [&]() {
                    auto s = trim(rhs);
                    auto base = baseFromLValueText(s);
                    if (base.empty() || s != base || !m.varNames.count(base)) {
                        return false;
                    }
                    auto typeIt = m.types.find(base);
                    return typeIt != m.types.end() && trim(typeIt->second).rfind("reg<", 0) == 0;
                };
                auto rhsReferencesRuntimeState = [&]() {
                    auto valueRhs = stripDecltypeExpressions(rhs);
                    auto memberDeclName = [](std::string decl) {
                        decl = trim(std::move(decl));
                        if (!decl.empty() && decl.back() == ';') {
                            decl.pop_back();
                            decl = trim(std::move(decl));
                        }
                        auto bracket = decl.find('[');
                        if (bracket != std::string::npos) {
                            decl = trim(decl.substr(0, bracket));
                        }
                        size_t end = decl.size();
                        while (end > 0 && std::isspace(static_cast<unsigned char>(decl[end - 1]))) {
                            --end;
                        }
                        size_t start = end;
                        while (start > 0) {
                            auto ch = decl[start - 1];
                            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
                                break;
                            }
                            --start;
                        }
                        return start < end ? decl.substr(start, end - start) : std::string();
                    };
                    if (exprReferencesModuleState(m, valueRhs)) {
                        return true;
                    }
                    for (const auto& name : m.seqAssignedVars) {
                        if (isIdentifierUsed(valueRhs, name)) {
                            return true;
                        }
                    }
                    for (const auto& name : m.memberNames) {
                        if (isIdentifierUsed(valueRhs, name)) {
                            return true;
                        }
                    }
                    for (const auto& decl : m.members) {
                        auto name = memberDeclName(decl);
                        if (!name.empty() && isIdentifierUsed(valueRhs, name)) {
                            return true;
                        }
                    }
                    return false;
                }();
                auto rhsReferencesLocalState = rhsReferencesRuntimeState && !rhsExactLocalStateRef;
		                std::string wrapper = rhsIsExactLocalRegisterRef() ? "_ASSIGN_REG" :
                    ((portTypeKnown && isSimpleCombRef(rhs)) ? "_ASSIGN_COMB" : "_ASSIGN");
                bool rhsIsParentPortAccessRef = isParentPortAccessRef(rhs);
                bool rhsIsCombAccessRef = isCombAccessRef(rhs);
                bool rhsReferencesDynamicGetter = referencesDynamicCpphdlGetter(rhs);
                if (portTypeKnown && (rhsIsParentPortAccessRef || rhsIsCombAccessRef || rhsReferencesDynamicGetter)) {
		                    wrapper = "_ASSIGN_COMB";
	                }
                if (portTypeKnown && rhsReferencesLocalState) {
                    wrapper = "_ASSIGN_COMB";
                }
	                if (!rawRhsBase.empty() && m.combAssignedVars.count(rawRhsBase) && !m.seqAssignedVars.count(rawRhsBase) &&
	                    !m.combMethodByBase.count(rawRhsBase) && !m.combSideEffectDriver.count(rawRhsBase) && hasRuntimeAssignLines(m)) {
		                    wrapper = "_ASSIGN_COMB";
	                }
	                else if (!rawRhsBase.empty() && m.combSideEffectDriver.count(rawRhsBase) &&
	                         !m.combMethodByBase.count(rawRhsBase) && !m.seqAssignedVars.count(rawRhsBase)) {
                    m.combSideEffectChildInputReads.insert(rawRhsBase);
                    if (rhs.find(m.combSideEffectDriver[rawRhsBase] + "()") == std::string::npos) {
                        rhs = emitSideEffectRead(m, m.combSideEffectDriver[rawRhsBase], rhs);
                    }
                }
                auto target = trim(portType);
                auto actualPortType = "std::remove_cvref_t<decltype(" + conn.instance + "." + portName + "())>";
                auto resolvePortAliasType = [&](std::string type) {
                    type = unwrapRegType(trim(std::move(type)));
                    for (size_t guard = 0; guard < 32; ++guard) {
                        std::string next;
                        if (auto it = m.types.find(type); it != m.types.end()) {
                            next = it->second;
                        }
                        else if (child) {
                            if (auto it = child->types.find(type); it != child->types.end()) {
                                next = substituteConfiguredPortType(childBaseType.empty() ? conn.type : childBaseType, connParamsForType, it->second);
                            }
                            else {
                                auto localAliases = localUsingTypeAliases(*child);
                                if (auto aliasIt = localAliases.find(type); aliasIt != localAliases.end()) {
                                    next = substituteParamDeclValues(child->params, splitTopLevelArgs(connParamsForType), aliasIt->second);
                                }
                            }
                        }
                        if (next.empty()) {
                            break;
                        }
                        next = unwrapRegType(trim(next));
                        if (next == type) {
                            break;
                        }
                        type = next;
                    }
                    return type;
	                };
	                auto resolvedTarget = resolvePortAliasType(target);
	                if (wrapper == "_ASSIGN_COMB" && isExactParentPortRef(rhs)) {
	                    auto resolvedSourceType = resolvePortAliasType(sourceTypeBeforeLateBind);
	                    auto resolvedTargetIsArray = resolvedTarget.rfind("array<", 0) == 0 ||
	                        resolvedTarget.rfind("std::array<", 0) == 0;
	                    auto resolvedSourceIsArray = resolvedSourceType.rfind("array<", 0) == 0 ||
	                        resolvedSourceType.rfind("std::array<", 0) == 0;
	                    if (!resolvedSourceType.empty() &&
	                        !resolvedTargetIsArray && !resolvedSourceIsArray &&
	                        trim(postProcessCppLine(resolvedSourceType)) != trim(postProcessCppLine(resolvedTarget))) {
	                        rhs = actualPortType + "(" + rhs + ")";
	                    }
	                }
                auto sourceLooksAggregateObject = [&](const std::string& typeText) {
                    auto raw = trim(typeText);
                    if (!raw.empty() &&
                        raw.rfind("logic<", 0) != 0 &&
                        raw.rfind("u<", 0) != 0 &&
                        raw.rfind("array<", 0) != 0 &&
                        raw.rfind("std::array<", 0) != 0 &&
                        raw != "bool" &&
                        raw != "u1" &&
                        raw != "unsigned" &&
                        raw != "uint64_t" &&
                        raw != "uint32_t" &&
                        raw != "uint16_t" &&
                        raw != "uint8_t") {
                        return true;
                    }
                    auto resolved = resolvePortAliasType(raw);
                    return !resolved.empty() &&
                        resolved.rfind("logic<", 0) != 0 &&
                        resolved.rfind("u<", 0) != 0 &&
                        resolved.rfind("array<", 0) != 0 &&
                        resolved.rfind("std::array<", 0) != 0 &&
                        resolved != "bool" &&
                        resolved != "u1" &&
                        resolved != "unsigned" &&
                        resolved != "uint64_t" &&
                        resolved != "uint32_t" &&
                        resolved != "uint16_t" &&
                        resolved != "uint8_t";
                };
	                if (portTypeKnown && target == "bool" && sourceLooksAggregateObject(sourceTypeBeforeLateBind)) {
                    target = actualPortType;
                    resolvedTarget = actualPortType;
                }
                auto resolvedSourceForPort = resolvePortAliasType(sourceTypeBeforeLateBind);
                auto sourceIsArrayForPort = resolvedSourceForPort.rfind("array<", 0) == 0 ||
                    resolvedSourceForPort.rfind("std::array<", 0) == 0;
                auto sourceIsPackedAggregateForPort = !resolvedSourceForPort.empty() &&
                    resolvedSourceForPort.rfind("logic<", 0) != 0 &&
                    resolvedSourceForPort.rfind("u<", 0) != 0 &&
                    resolvedSourceForPort.rfind("array<", 0) != 0 &&
                    resolvedSourceForPort.rfind("std::array<", 0) != 0 &&
                    resolvedSourceForPort != "bool" &&
                    resolvedSourceForPort != "unsigned" &&
                    resolvedSourceForPort != "uint64_t" &&
                    resolvedSourceForPort != "uint32_t" &&
                    resolvedSourceForPort != "uint16_t" &&
                    resolvedSourceForPort != "uint8_t";
                auto sourceIsWholePackedAggregateForPort = sourceIsPackedAggregateForPort &&
                    (isSimpleCombRef(rhs) || isExactParentPortRef(rhs) || rhsExactLocalStateRef);
                auto sourceIsScalarLogicForPort = resolvedSourceForPort.rfind("logic<", 0) == 0 ||
                    resolvedSourceForPort.rfind("u<", 0) == 0 ||
                    resolvedSourceForPort == "bool" ||
                    resolvedSourceForPort == "u1";
                auto targetIsScalarLogicForPort = resolvedTarget.rfind("logic<", 0) == 0 ||
                    resolvedTarget.rfind("u<", 0) == 0 ||
                    resolvedTarget == "bool" ||
                    resolvedTarget == "u1";
                auto scalarLogicWidthsMatch = [&]() {
                    auto sourceWidth = foldWidth(logicWidth(resolvedSourceForPort));
                    if (sourceWidth.empty()) {
                        sourceWidth = foldWidth(typeWidth(resolvedSourceForPort));
                    }
                    auto targetWidth = foldWidth(logicWidth(resolvedTarget));
                    if (targetWidth.empty()) {
                        targetWidth = foldWidth(typeWidth(resolvedTarget));
                    }
                    if (!sourceWidth.empty() && !targetWidth.empty()) {
                        return sourceWidth == targetWidth;
                    }
                    return trim(postProcessCppLine(resolvedSourceForPort)) ==
                        trim(postProcessCppLine(resolvedTarget));
                };
                auto scalarLogicWidthMismatchKnown = [&]() {
                    if (!sourceIsScalarLogicForPort || !targetIsScalarLogicForPort) {
                        return false;
                    }
                    auto sourceWidth = foldWidth(logicWidth(resolvedSourceForPort));
                    if (sourceWidth.empty()) {
                        sourceWidth = foldWidth(typeWidth(resolvedSourceForPort));
                    }
                    auto targetWidth = foldWidth(logicWidth(resolvedTarget));
                    if (targetWidth.empty()) {
                        targetWidth = foldWidth(typeWidth(resolvedTarget));
                    }
                    return isNumber(sourceWidth) && isNumber(targetWidth) && sourceWidth != targetWidth;
                }();
                if (portTypeKnown && target == "bool" && wrapper == "_ASSIGN_COMB") {
                    const bool dynamicBoolSource = rhsIsParentPortAccessRef || rhsIsCombAccessRef ||
                        rhsReferencesDynamicGetter || isSimpleCombRef(rhs) || exprReferencesModuleState(m, rhs);
                    rhs = "bool(" + rhs + ")";
                    if (!dynamicBoolSource && rhs.find("_comb_func()") == std::string::npos) {
                        wrapper = "_ASSIGN";
                    }
                }
                else if (portTypeKnown && rhsWasZeroLiteral && needsTypedZero(target)) {
                    rhs = actualPortType + "{}";
                    wrapper = "_ASSIGN";
                }
                else if (portTypeKnown && isSimpleCombRef(rhs) &&
                    targetIsScalarLogicForPort && sourceIsScalarLogicForPort && scalarLogicWidthsMatch()) {
		                    wrapper = "_ASSIGN_COMB";
                }
                else if (portTypeKnown && resolvedTarget.rfind("logic<", 0) == 0 && resolvedTarget.back() == '>' &&
                    (sourceIsArrayForPort || sourceIsWholePackedAggregateForPort ||
                     scalarLogicWidthMismatchKnown ||
                     (!isSimpleCombRef(rhs) && rhs.find("_func()") != std::string::npos))) {
                    if (sourceIsArrayForPort || sourceIsWholePackedAggregateForPort) {
                        rhs = "cpphdl::pack_value<cpphdl::type_width<" + actualPortType + ">()>(" + rhs + ")";
                    }
                    else {
                        if (rhs.rfind(actualPortType + "(", 0) != 0) {
                            rhs = actualPortType + "(" + rhs + ")";
                        }
                    }
                    if (!isSimpleCombRef(rhs) &&
                        (rhs.find("().") != std::string::npos || rhs.find("_func().") != std::string::npos)) {
                        rhs = materializeCombPortBinding(conn.instance, portName, portType, rhs);
                        wrapper = "_ASSIGN_COMB";
                    }
                }
                else if (portTypeKnown && isSimpleCombRef(rhs) &&
                         resolvedTarget.rfind("logic<", 0) != 0 &&
                         resolvedTarget.rfind("array<", 0) != 0 &&
                         resolvedTarget.rfind("std::array<", 0) != 0) {
                    if (!sourceTypeBeforeLateBind.empty() && trim(sourceTypeBeforeLateBind) != target &&
                        sourceTypeBeforeLateBind.rfind("logic<", 0) != 0 &&
                        sourceTypeBeforeLateBind.rfind("u<", 0) != 0 &&
                        sourceTypeBeforeLateBind.rfind("array<", 0) != 0 &&
                        sourceTypeBeforeLateBind.rfind("std::array<", 0) != 0) {
                        rhs = packedPortConversionExpr(actualPortType, rhs);
                    }
                    else {
                        rhs = actualPortType + "(" + rhs + ")";
                    }
                    wrapper = "_ASSIGN";
                }
                else if (portTypeKnown &&
                         (target.rfind("array<", 0) == 0 || target.rfind("std::array<", 0) == 0) &&
                         !sourceTypeBeforeLateBind.empty() && sourceTypeBeforeLateBind != target) {
                    if (needsArrayPackPortBinding(target, sourceTypeBeforeLateBind)) {
                        rhs = materializeArrayPackPortBinding(conn.instance, portName, rhs);
                        wrapper = "_ASSIGN_COMB";
                    }
                    else if (needsArrayUnpackAssignment(target, sourceTypeBeforeLateBind)) {
                        rhs = materializeArrayUnpackAssignment(conn.instance, portName, target, rhs, true);
                        wrapper = "_ASSIGN_COMB";
                    }
                    else {
                        rhs = materializePackedToArrayPortBinding(conn.instance, portName, target, rhs);
                        wrapper = "_ASSIGN_COMB";
                    }
                }
                else if (!portTypeKnown &&
                         (sourceTypeBeforeLateBind.rfind("array<", 0) == 0 ||
                          sourceTypeBeforeLateBind.rfind("std::array<", 0) == 0)) {
                    rhs = materializeArrayPackPortBinding(conn.instance, portName, rhs);
                    wrapper = "_ASSIGN_COMB";
                }
                else if (portTypeKnown) {
                    rhs = adaptInputPortRhs(m, portType, rhs, sourceTypeBeforeLateBind);
                }
                auto targetAggregateObject = portTypeKnown &&
                    resolvedTarget != "bool" && resolvedTarget != "u1" &&
                    resolvedTarget.rfind("logic<", 0) != 0 &&
                    resolvedTarget.rfind("u<", 0) != 0 &&
                    resolvedTarget.rfind("array<", 0) != 0 &&
                    resolvedTarget.rfind("std::array<", 0) != 0;
	                if (targetAggregateObject && !isSimpleCombRef(rhs) &&
	                    (rhs.find("().") != std::string::npos || rhs.find("_func().") != std::string::npos)) {
	                    if (rhs.rfind("cpphdl::unpack_value<" + actualPortType + ">", 0) != 0 &&
	                        rhs.rfind(actualPortType + "(", 0) != 0) {
	                        rhs = packedPortConversionExpr(actualPortType, rhs);
                    }
	                    rhs = materializeCombPortBinding(conn.instance, portName, portType, rhs);
	                    wrapper = "_ASSIGN_COMB";
	                }
	                if (wrapper == "_ASSIGN_REG" && !rhsIsExactLocalRegisterRef() &&
	                    !isSimpleCombRef(rhs) && !isExactParentPortRef(rhs)) {
	                    wrapper = "_ASSIGN_COMB";
	                }
                if (wrapper == "_ASSIGN_COMB") {
                    auto resolvedSourceType = resolvePortAliasType(sourceTypeBeforeLateBind);
                    if (arrayTypeIsPacked(resolvedSourceType) && cpphdlGetterExprIndexesArrayElement(rhs)) {
                        wrapper = "_ASSIGN";
                    }
                }
                if (wrapper == "_ASSIGN_COMB" && !isSimpleCombRef(rhs)) {
                    rhsIsParentPortAccessRef = isParentPortAccessRef(rhs);
                    rhsIsCombAccessRef = isCombAccessRef(rhs);
                    rhsReferencesDynamicGetter = referencesDynamicCpphdlGetter(rhs);
                    if (needsCombPortBinding(rhs) && !rhsIsCombAccessRef) {
                        rhs = materializeCombPortBinding(conn.instance, portName, portType, rhs);
                        rhsReferencesDynamicGetter = referencesDynamicCpphdlGetter(rhs);
                    }
                    else if (!rhsIsParentPortAccessRef && !rhsIsCombAccessRef && !rhsReferencesDynamicGetter &&
                             !rhsReferencesRuntimeState) {
                        wrapper = "_ASSIGN";
                    }
                }
                if (wrapper == "_ASSIGN_COMB" && !isAddressableBindingRhs(rhs)) {
                    auto dynamicRhs = referencesDynamicCpphdlGetter(rhs) || exprReferencesModuleState(m, rhs) ||
                                      rhsReferencesRuntimeState || rhsReferencesLocalState;
                    if (dynamicRhs) {
                        rhs = materializeCombPortBinding(conn.instance, portName, portType, rhs);
                        wrapper = "_ASSIGN_COMB";
                    }
                    else {
                        wrapper = "_ASSIGN";
                    }
                }
                auto emittedAssignLine = conn.instance + "." + portName + " = " + wrapper + "(" + rhs + ");";
                if (const char* debug = std::getenv("HDLCPP_DEBUG_ASSIGN_ADAPT")) {
                    std::string filter = debug;
                    if (filter.empty() || filter == "1" || emittedAssignLine.find(filter) != std::string::npos ||
                        portName.find(filter) != std::string::npos || conn.instance.find(filter) != std::string::npos) {
                        std::cerr << "instance-bind module=" << m.name
                                  << " instance=" << conn.instance
                                  << " child=" << conn.type
                                  << " params=" << conn.params
                                  << " port=" << portName
                                  << " portType=" << portType
                                  << " portTypeKnown=" << (portTypeKnown ? "1" : "0")
                                  << " sourceType=" << sourceTypeBeforeLateBind
                                  << " wrapper=" << wrapper
                                  << " rhs=" << rhs
                                  << "\n";
                    }
                }
                auto deferUnknownCombAdapt = !portTypeKnown && wrapper == "_ASSIGN" && !isSimpleCombRef(rhs);
                if (!deferUnknownCombAdapt) {
                    emittedAssignLine = finalAdaptStructuralAssignLine(m, emittedAssignLine);
                    emittedAssignLine = normalizeInputPortCombValueWrapper(emittedAssignLine);
                }
                m.assignLines.push_back(emittedAssignLine);
                if (!conn.guards.empty()) {
                    m.structuralAssignGuards[emittedAssignLine] = conn.guards;
                }
            }
        }
    }

    void wireAssignsToPorts(ModuleGen& m)
    {
        auto rhsIsChildOutputPortRef = [&](const std::string& expr) {
            auto s = trim(expr);
            if (!hasSuffix(s, "()")) {
                return false;
            }
            s = trim(s.substr(0, s.size() - 2));
            auto dot = s.find('.');
            if (dot == std::string::npos || s.find('.', dot + 1) != std::string::npos) {
                return false;
            }
            auto instance = trim(s.substr(0, dot));
            auto portName = trim(s.substr(dot + 1));
            if (instance.empty() || portName.empty() || instance.find('[') != std::string::npos) {
                return false;
            }
            std::string childType;
            for (size_t i = 0; i < m.memberNames.size() && i < m.memberTypes.size(); ++i) {
                if (m.memberNames[i] == instance) {
                    childType = m.memberTypes[i];
                    break;
                }
            }
            auto* child = childType.empty() ? nullptr : findModule(childType);
            if (!child) {
                return false;
            }
            for (const auto& out : child->outputPortCppNames) {
                if (out.first == portName || out.second == portName) {
                    return true;
                }
            }
            for (const auto& p : child->ports) {
                if (p.direction == "output" && p.name == portName) {
                    return true;
                }
            }
            return false;
        };
        for (auto& a : m.assigns) {
            for (auto& p : m.ports) {
                if (p.name == a.first && p.init.empty()) {
                    bool runtimeAssignDrivenOutput = false;
                    for (const auto& out : m.outputPortCppNames) {
                        if (out.second == p.name && m.runtimeAssignDrivenVars.count(out.first)) {
                            runtimeAssignDrivenOutput = true;
                            break;
                        }
                    }
                    if (runtimeAssignDrivenOutput) {
                        continue;
                    }
                    auto rhs = a.second;
                    for (auto& f : m.methods) {
                        if (!f.returnName.empty() && f.returnName == rhs) {
                            rhs = f.name + "()";
                        }
                    }
                    auto rhsBase = baseFromLValueText(a.second);
                    auto combDrivenLocal = !rhsBase.empty() && m.varNames.count(rhsBase) &&
                        !m.seqAssignedVars.count(rhsBase) &&
                        (m.combAssignedVars.count(rhsBase) ||
                         m.combMethodByBase.count(rhsBase) ||
                         m.combSideEffectDriver.count(rhsBase));
	                    std::string wrapper = (m.varNames.count(a.second) && !combDrivenLocal) ? "_ASSIGN_REG" :
	                        (combDrivenLocal ? "_ASSIGN_COMB" : "_ASSIGN");
                    auto rhsForWrapper = lateBindExpr(m, rhs, "");
                    if (wrapper == "_ASSIGN" && rhsIsChildOutputPortRef(rhsForWrapper)) {
                        wrapper = "_ASSIGN_COMB";
                    }
                    p.init = std::string(" = ") + wrapper + "( " + rhs + " )";
                }
            }
        }
    }

    std::string lateBindExpr(const ModuleGen& mod, std::string expr, const std::string& exclude, const std::string& excludeDriver = "", bool lhsMode = false)
    {
        std::string out;
        struct LateBindLookupCache {
            const ModuleGen* mod = nullptr;
            size_t outputSize = 0;
            size_t combSize = 0;
            size_t methodSize = 0;
            std::map<std::string, std::string> outputAliasToSv;
            std::set<std::string> outputStorageAliases;
            std::map<std::string, std::pair<std::string, std::string>> combStorageToMethod;
        };
        static LateBindLookupCache cache;
        if (cache.mod != &mod ||
            cache.outputSize != mod.outputPortCppNames.size() ||
            cache.combSize != mod.combMethodByBase.size() ||
            cache.methodSize != mod.methods.size()) {
            cache = {};
            cache.mod = &mod;
            cache.outputSize = mod.outputPortCppNames.size();
            cache.combSize = mod.combMethodByBase.size();
            cache.methodSize = mod.methods.size();
            for (const auto& outPort : mod.outputPortCppNames) {
                const auto& svName = outPort.first;
                const auto& cppName = outPort.second;
                cache.outputAliasToSv[svName] = svName;
                cache.outputAliasToSv[cppName] = svName;
                cache.outputStorageAliases.insert(outputStorageName(mod, svName));
                cache.outputStorageAliases.insert(cppName + "_reg");
                cache.outputStorageAliases.insert(cppName + "_storage");
                cache.outputStorageAliases.insert(cppName + "_comb");
            }
            for (const auto& combItem : mod.combMethodByBase) {
                if (combItem.second >= mod.methods.size()) {
                    continue;
                }
                auto combIsRegisterObject = mod.types.count(combItem.first) &&
                    mod.types.at(combItem.first).rfind("reg<", 0) == 0;
                auto mixedCombSeq = mod.seqAssignedVars.count(combItem.first) &&
                    mod.combAssignedVars.count(combItem.first);
                if ((mod.seqAssignedVars.count(combItem.first) || combIsRegisterObject) && !mixedCombSeq) {
                    continue;
                }
                cache.combStorageToMethod[combStorageName(mod, combItem.first)] =
                    {combItem.first, mod.methods[combItem.second].name};
            }
        }
	        auto memberAfter = [&](size_t pos) -> std::string {
	            while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
	                ++pos;
	            }
            if (pos >= expr.size() || expr[pos] != '.') {
                return "";
            }
            ++pos;
            while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
                ++pos;
            }
            auto start = pos;
            while (pos < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            if (start == pos) {
                return "";
	            }
	            return cppIdent(expr.substr(start, pos - start));
	        };
	        auto memberRangeAfter = [&](size_t pos) -> std::pair<std::string, size_t> {
	            while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
	                ++pos;
	            }
	            if (pos >= expr.size() || expr[pos] != '.') {
	                return {"", pos};
	            }
	            ++pos;
	            while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
	                ++pos;
	            }
	            auto start = pos;
	            while (pos < expr.size() &&
	                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
	                ++pos;
	            }
	            if (start == pos) {
	                return {"", start};
	            }
	            return {cppIdent(expr.substr(start, pos - start)), pos};
	        };
	        bool replacedFieldComb = true;
	        while (replacedFieldComb) {
	            replacedFieldComb = false;
	            for (const auto& item : mod.combMethodByField) {
	                if (item.second >= mod.methods.size()) {
	                    continue;
	                }
	                auto baseIt = mod.combMethodByBase.find(item.first.first);
	                if (baseIt == mod.combMethodByBase.end() || baseIt->second >= mod.methods.size()) {
	                    continue;
	                }
		                const auto& baseMethod = mod.methods[baseIt->second].name;
		                const auto& field = item.first.second;
		                const auto& fieldMethod = mod.methods[item.second].name;
			                if (fieldMethod == excludeDriver ||
			                    mod.methods[item.second].returnName == exclude) {
			                    continue;
			                }
		                auto needle = baseMethod + "()." + field;
	                auto replacement = fieldMethod + "()";
	                for (size_t pos = 0; (pos = expr.find(needle, pos)) != std::string::npos;) {
	                    auto end = pos + needle.size();
	                    auto next = end < expr.size() ? expr[end] : '\0';
	                    if (std::isalnum(static_cast<unsigned char>(next)) || next == '_') {
	                        pos = end;
	                        continue;
	                    }
	                    expr.replace(pos, needle.size(), replacement);
	                    pos += replacement.size();
	                    replacedFieldComb = true;
	                }
	            }
	        }
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
                    if (width.empty() && (type.rfind("array<", 0) == 0 || type.rfind("std::array<", 0) == 0)) {
                        auto args = templateArgsFor(type, type.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                        if (args.size() >= 2) {
                            width = typeWidth(args[0]);
                        }
                    }
                    if (!width.empty() && type.rfind("logic<", 0) != 0 && type.rfind("reg<logic<", 0) != 0) {
                        auto baseValue = id;
                        auto wireIt = mod.wireMap.find(id);
                        if (wireIt != mod.wireMap.end() && id != exclude && prev != '.' && next != '(') {
                            baseValue = moduleMethodExists(mod, wireIt->second) ? wireIt->second + "()" : wireIt->second;
                        }
                        out += "logic<" + width + ">(" + baseValue + ")";
                        continue;
                    }
                }
                auto it = mod.wireMap.find(id);
                if (it != mod.wireMap.end() && id != exclude && prev != '.' && next != '(') {
                    out += moduleMethodExists(mod, it->second) ? it->second + "()" : it->second;
                }
                else {
                    bool replacedOutput = false;
                    if (prev != '.' && next != '(' && cache.outputStorageAliases.count(id)) {
                        out += id;
                        replacedOutput = true;
                    }
                    auto aliasIt = cache.outputAliasToSv.find(id);
                    if (!replacedOutput && aliasIt != cache.outputAliasToSv.end()) {
                        const auto& svName = aliasIt->second;
                        const auto& cppName = mod.outputPortCppNames.at(svName);
                        auto selfPortCall = false;
                        if (id == cppName && next == '(') {
                            auto close = i + 1;
                            while (close < expr.size() && std::isspace(static_cast<unsigned char>(expr[close]))) {
                                ++close;
                            }
                            selfPortCall = close < expr.size() && expr[close] == ')';
                        }
                        if (prev != '.' && next != '(') {
                            if (isAssignOnlyOutput(mod, svName)) {
                                out += cppName + "()";
                                replacedOutput = true;
                            }
                            else {
                                out += lhsMode ? outputStorageName(mod, svName) : emitCombOutputRead(mod, svName, memberAfter(i), excludeDriver);
                                replacedOutput = true;
                            }
                        }
                        else if (prev != '.' && selfPortCall && !isAssignOnlyOutput(mod, svName)) {
                            while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) {
                                ++i;
                            }
                            if (i < expr.size() && expr[i] == '(') {
                                ++i;
                                while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) {
                                    ++i;
                                }
                                if (i < expr.size() && expr[i] == ')') {
                                    ++i;
                                }
                            }
                            out += lhsMode ? outputStorageName(mod, svName) : emitCombOutputRead(mod, svName, memberAfter(i), excludeDriver);
                            replacedOutput = true;
                        }
                    }
                    if (!replacedOutput) {
                        auto portIt = mod.portCppNames.find(id);
                        if (portIt != mod.portCppNames.end() && id != exclude && prev != '.' && next != '(') {
                            out += portIt->second + "()";
                        }
                        else if (id != exclude && prev != '.' && next != '(') {
                            bool replacedComb = false;
                            auto isRegisterObject = mod.types.count(id) && mod.types.at(id).rfind("reg<", 0) == 0;
                            auto combBaseIt = mod.combMethodByBase.find(id);
                            auto mixedCombSeq = mod.seqAssignedVars.count(id) && mod.combAssignedVars.count(id);
	                            if (combBaseIt != mod.combMethodByBase.end() &&
	                                (!mod.seqAssignedVars.count(id) || mixedCombSeq) &&
	                                (!isRegisterObject || mixedCombSeq)) {
	                                const auto& combMethodName = mod.methods[combBaseIt->second].name;
	                                auto sideIt = mod.combSideEffectDriver.find(id);
	                                auto drivenByCurrentMethod = !excludeDriver.empty() &&
	                                    (combMethodName == excludeDriver ||
	                                     (sideIt != mod.combSideEffectDriver.end() && sideIt->second == excludeDriver));
	                                if (!drivenByCurrentMethod) {
		                            auto [member, memberEnd] = memberRangeAfter(i);
		                            auto fieldIt = !member.empty() ? mod.combMethodByField.find({id, member}) : mod.combMethodByField.end();
		                            if (fieldIt != mod.combMethodByField.end() && fieldIt->second < mod.methods.size()) {
		                                const auto& fieldMethod = mod.methods[fieldIt->second];
		                                if (fieldMethod.name != excludeDriver && fieldMethod.returnName != exclude) {
		                                    out += fieldMethod.name + "()";
		                                    i = memberEnd;
		                                }
		                                else {
		                                    out += combMethodName + "()";
		                                }
		                            }
	                                    else {
	                                        out += combMethodName + "()";
	                                    }
	                                    replacedComb = true;
	                                }
	                            }
                            if (!replacedComb) {
                                auto storageIt = cache.combStorageToMethod.find(id);
                                if (storageIt != cache.combStorageToMethod.end() && id != combStorageName(mod, exclude)) {
                                    const auto& combBase = storageIt->second.first;
                                    const auto& combMethodName = storageIt->second.second;
                                    auto sideIt = mod.combSideEffectDriver.find(combBase);
                                    auto drivenByCurrentMethod = !excludeDriver.empty() &&
                                        (combMethodName == excludeDriver ||
                                         (sideIt != mod.combSideEffectDriver.end() && sideIt->second == excludeDriver));
                                    if (!drivenByCurrentMethod) {
                                        out += combMethodName + "()";
                                        replacedComb = true;
                                    }
                                }
                            }
                            if (!replacedComb) {
                                auto sideIt = mod.combSideEffectDriver.find(id);
                                if (sideIt != mod.combSideEffectDriver.end() && !mod.seqAssignedVars.count(id) &&
                                    sideIt->second != exclude && sideIt->second != excludeDriver) {
                                    out += emitSideEffectRead(mod, sideIt->second, id);
                                }
                                else if (isAssignDrivenVar(mod, id)) {
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
        if (out.find("$high(") != std::string::npos) {
            for (auto& item : mod.types) {
                auto high = "$high(" + item.first + ")";
                auto width = typeWidth(item.second);
                if (!width.empty()) {
                    replaceAll(out, high, "(" + width + "-1)");
                }
            }
        }
	        if (out.find(".bits(") != std::string::npos && out.find('[') != std::string::npos) {
	            for (auto& item : mod.types) {
                auto type = item.second;
                if (type.rfind("array<", 0) != 0 && type.rfind("std::array<", 0) != 0) {
                    continue;
                }
                auto args = templateArgsFor(type, type.rfind("std::array<", 0) == 0 ? "std::array" : "array");
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
	        }
	        std::function<std::string(std::string, const std::string&)> projectedFieldCall;
	        projectedFieldCall = [&](std::string branch, const std::string& field) -> std::string {
	            branch = trim(branch);
	            auto sourceAggregateFieldExpr = [&](const MethodGen& source) -> std::string {
	                auto bindExtractedExpr = [&](std::string expr) {
	                    expr = trim(std::move(expr));
	                    auto isIdentText = [](const std::string& text) {
	                        if (text.empty()) {
	                            return false;
	                        }
	                        if (!std::isalpha(static_cast<unsigned char>(text.front())) && text.front() != '_') {
	                            return false;
	                        }
	                        for (char ch : text) {
	                            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
	                                return false;
	                            }
	                        }
	                        return true;
	                    };
	                    if (isIdentText(expr)) {
	                        if (auto it = mod.combMethodByBase.find(expr);
	                            it != mod.combMethodByBase.end() && it->second < mod.methods.size()) {
	                            return mod.methods[it->second].name + "()";
	                        }
	                        if (auto it = mod.wireMap.find(expr); it != mod.wireMap.end()) {
	                            return it->second + "()";
	                        }
	                        if (auto it = mod.portCppNames.find(expr); it != mod.portCppNames.end()) {
	                            return it->second + "()";
	                        }
	                    }
	                    std::vector<std::pair<std::string, std::string>> replacements;
	                    for (const auto& item : mod.combMethodByBase) {
	                        if (item.second < mod.methods.size()) {
	                            replacements.push_back({item.first, mod.methods[item.second].name + "()"});
	                        }
	                    }
	                    std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) {
	                        return a.first.size() > b.first.size();
	                    });
	                    for (const auto& item : replacements) {
	                        replaceIdentifierAll(expr, item.first, item.second);
	                    }
	                    return expr;
	                };
	                const std::string assignNeedle = "__cpphdl_assign_field(v." + field + ", __cpphdl_field_value)";
	                const std::string valueNeedle = "auto __cpphdl_field_value = ";
	                for (const auto& line : source.body) {
	                    auto assignPos = line.find(assignNeedle);
	                    if (assignPos == std::string::npos) {
	                        continue;
	                    }
	                    auto valuePos = line.rfind(valueNeedle, assignPos);
	                    if (valuePos == std::string::npos) {
	                        continue;
	                    }
	                    valuePos += valueNeedle.size();
	                    int paren = 0;
	                    int brace = 0;
	                    int bracket = 0;
	                    for (size_t end = valuePos; end < line.size(); ++end) {
	                        char ch = line[end];
	                        if (ch == '(') {
	                            ++paren;
	                        }
	                        else if (ch == ')' && paren > 0) {
	                            --paren;
	                        }
	                        else if (ch == '{') {
	                            ++brace;
	                        }
	                        else if (ch == '}' && brace > 0) {
	                            --brace;
	                        }
	                        else if (ch == '[') {
	                            ++bracket;
	                        }
	                        else if (ch == ']' && bracket > 0) {
	                            --bracket;
	                        }
	                        else if (ch == ';' && paren == 0 && brace == 0 && bracket == 0) {
	                            return bindExtractedExpr(line.substr(valuePos, end - valuePos));
	                        }
	                    }
	                }
	                return {};
	            };
	            auto unwrapSvCast = [&](std::string text) {
	                text = trim(std::move(text));
	                for (;;) {
		                    const std::string cppCast = "cpphdl::sv_cast<";
		                    const std::string bareCast = "sv_cast<";
		                    const std::string staticCast = "static_cast<";
		                    std::string marker;
		                    if (text.rfind(cppCast, 0) == 0) {
		                        marker = cppCast;
		                    }
		                    else if (text.rfind(bareCast, 0) == 0) {
		                        marker = bareCast;
		                    }
		                    else if (text.rfind(staticCast, 0) == 0) {
		                        marker = staticCast;
		                    }
		                    else {
		                        return text;
		                    }
	                    int angleDepth = 0;
	                    size_t closeAngle = std::string::npos;
	                    for (size_t i = marker.size(); i < text.size(); ++i) {
	                        if (text[i] == '<') {
	                            ++angleDepth;
	                        }
	                        else if (text[i] == '>') {
	                            if (angleDepth == 0) {
	                                closeAngle = i;
	                                break;
	                            }
	                            --angleDepth;
	                        }
	                    }
	                    if (closeAngle == std::string::npos) {
	                        return text;
	                    }
	                    size_t openParen = closeAngle + 1;
	                    while (openParen < text.size() && std::isspace(static_cast<unsigned char>(text[openParen]))) {
	                        ++openParen;
	                    }
	                    if (openParen >= text.size() || text[openParen] != '(') {
	                        return text;
	                    }
	                    auto closeParen = matchingParenClose(text, openParen);
	                    if (closeParen == std::string::npos) {
	                        return text;
	                    }
	                    size_t tail = closeParen + 1;
	                    while (tail < text.size() && std::isspace(static_cast<unsigned char>(text[tail]))) {
	                        ++tail;
	                    }
	                    if (tail != text.size()) {
	                        return text;
	                    }
	                    text = trim(text.substr(openParen + 1, closeParen - openParen - 1));
	                }
	            };
	            auto directBranch = unwrapSvCast(branch);
	            auto containsCall = [](const std::string& text, const std::string& name) {
	                const auto needle = name + "()";
	                auto isIdent = [](char ch) {
	                    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
	                };
	                for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos;) {
	                    auto before = pos == 0 ? '\0' : text[pos - 1];
	                    auto after = pos + needle.size();
	                    if (!isIdent(before) && (after >= text.size() || !isIdent(text[after]))) {
	                        return true;
	                    }
	                    pos += needle.size();
	                }
	                return false;
	            };
	            auto fieldDefault = [&]() -> std::string {
	                for (const auto& item : mod.combMethodByField) {
	                    if (item.second >= mod.methods.size() || item.first.second != field) {
	                        continue;
	                    }
	                    auto type = trim(mod.methods[item.second].ret);
	                    if (!type.empty() && type.back() == '&') {
	                        type.pop_back();
	                        type = trim(type);
	                    }
	                    if (!type.empty()) {
	                        return type + "{}";
	                    }
	                }
	                return "0";
	            };
		            std::function<bool(std::string)> isDefaultFieldBranch;
		            isDefaultFieldBranch = [&](std::string text) {
		                text = unwrapSvCast(trim(std::move(text)));
		                while (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
		                    auto close = matchingParenClose(text, 0);
		                    if (close != text.size() - 1) {
		                        break;
		                    }
		                    text = unwrapSvCast(trim(text.substr(1, text.size() - 2)));
		                }
		                auto stripCStyleCast = [&]() -> bool {
		                    if (text.empty() || text.front() != '(') {
		                        return false;
		                    }
		                    auto close = matchingParenClose(text, 0);
		                    if (close == std::string::npos || close + 1 >= text.size()) {
		                        return false;
		                    }
		                    auto type = trim(text.substr(1, close - 1));
		                    if (type.empty() ||
		                        (!std::isalpha(static_cast<unsigned char>(type.front())) && type.front() != '_') ||
		                        type.find_first_of("+-*/&|?:") != std::string::npos) {
		                        return false;
		                    }
		                    text = unwrapSvCast(trim(text.substr(close + 1)));
		                    return true;
		                };
		                while (stripCStyleCast()) {
		                    while (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
		                        auto close = matchingParenClose(text, 0);
		                        if (close != text.size() - 1) {
		                            break;
		                        }
		                        text = unwrapSvCast(trim(text.substr(1, text.size() - 2)));
		                    }
		                }
		                if (text == "{}" || text == "0" || text == "0u" || text == "0ull" ||
		                    text == "0b0" || text == "false") {
		                    return true;
		                }
		                auto topLevelBinary = [&](char op, std::string& lhs, std::string& rhs) {
		                    int paren = 0;
		                    int bracket = 0;
		                    int brace = 0;
		                    int angle = 0;
		                    for (size_t pos = 0; pos < text.size(); ++pos) {
		                        char ch = text[pos];
		                        if (ch == '(') ++paren;
		                        else if (ch == ')' && paren > 0) --paren;
		                        else if (ch == '[') ++bracket;
		                        else if (ch == ']' && bracket > 0) --bracket;
		                        else if (ch == '{') ++brace;
		                        else if (ch == '}' && brace > 0) --brace;
		                        else if (ch == '<' && paren == 0 && bracket == 0 && brace == 0) ++angle;
		                        else if (ch == '>' && paren == 0 && bracket == 0 && brace == 0 && angle > 0) --angle;
		                        else if (ch == op && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
		                            lhs = trim(text.substr(0, pos));
		                            rhs = trim(text.substr(pos + 1));
		                            return true;
		                        }
		                    }
		                    return false;
		                };
		                std::string lhs;
		                std::string rhs;
		                if (topLevelBinary('&', lhs, rhs)) {
		                    return isDefaultFieldBranch(lhs) || isDefaultFieldBranch(rhs);
		                }
		                auto isZeroCtor = [&](const std::string& prefix) {
		                    if (text.rfind(prefix, 0) != 0 || text.back() != ')') {
		                        return false;
		                    }
		                    auto open = text.find('(');
		                    if (open == std::string::npos) {
		                        return false;
		                    }
		                    auto close = matchingParenClose(text, open);
		                    if (close != text.size() - 1) {
		                        return false;
		                    }
		                    auto arg = trim(text.substr(open + 1, close - open - 1));
		                    return arg == "0" || arg == "0u" || arg == "0ull" || arg == "0b0" || arg == "false";
		                };
		                return isZeroCtor("logic<") || isZeroCtor("u<");
		            };
		            if (isDefaultFieldBranch(branch)) {
		                return fieldDefault();
		            }
	            for (const auto& item : mod.combMethodByField) {
		                if (item.second >= mod.methods.size() || item.first.second != field) {
		                    continue;
		                }
			                if (mod.methods[item.second].name == excludeDriver ||
			                    mod.methods[item.second].returnName == exclude) {
			                    continue;
			                }
		                auto baseIt = mod.combMethodByBase.find(item.first.first);
		                if (baseIt == mod.combMethodByBase.end() || baseIt->second >= mod.methods.size()) {
		                    continue;
	                }
	                const auto& baseMethod = mod.methods[baseIt->second].name;
	                if (directBranch == baseMethod + "()") {
	                    return mod.methods[item.second].name + "()";
	                }
	                if (containsCall(directBranch, baseMethod)) {
	                    return mod.methods[item.second].name + "()";
	                }
	            }
	            for (const auto& item : mod.combMethodByBase) {
	                if (item.second >= mod.methods.size()) {
	                    continue;
	                }
	                const auto& sourceMethod = mod.methods[item.second];
	                if (directBranch != sourceMethod.name + "()") {
	                    continue;
	                }
	                auto expr = sourceAggregateFieldExpr(sourceMethod);
	                if (!expr.empty()) {
	                    return expr;
	                }
	            }
	            {
	                std::string inner = directBranch;
	                while (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') {
	                    auto close = matchingParenClose(inner, 0);
	                    if (close != inner.size() - 1) {
	                        break;
	                    }
	                    inner = trim(inner.substr(1, inner.size() - 2));
	                }
	                int paren = 0;
	                int angle = 0;
	                size_t question = std::string::npos;
	                size_t colon = std::string::npos;
	                for (size_t pos = 0; pos < inner.size(); ++pos) {
	                    if (inner[pos] == '(') ++paren;
	                    else if (inner[pos] == ')' && paren > 0) --paren;
	                    else if (inner[pos] == '<' && paren == 0) ++angle;
	                    else if (inner[pos] == '>' && paren == 0 && angle > 0) --angle;
	                    else if (inner[pos] == '?' && paren == 0 && angle == 0) question = pos;
	                    else if (inner[pos] == ':' && paren == 0 && angle == 0 && question != std::string::npos) {
	                        if ((pos > 0 && inner[pos - 1] == ':') || (pos + 1 < inner.size() && inner[pos + 1] == ':')) {
	                            continue;
	                        }
	                        colon = pos;
	                        break;
	                    }
	                }
	                if (question != std::string::npos && colon != std::string::npos) {
	                    auto cond = trim(inner.substr(0, question));
	                    auto lhs = trim(inner.substr(question + 1, colon - question - 1));
	                    auto rhs = trim(inner.substr(colon + 1));
	                    return "(" + cond + " ? " + projectedFieldCall(lhs, field) +
	                        " : " + projectedFieldCall(rhs, field) + ")";
	                }
	            }
	            for (const auto& item : mod.combMethodByBase) {
	                if (item.second >= mod.methods.size()) {
	                    continue;
	                }
	                const auto& sourceMethod = mod.methods[item.second];
	                if (!containsCall(directBranch, sourceMethod.name)) {
	                    continue;
	                }
	                auto expr = sourceAggregateFieldExpr(sourceMethod);
	                if (!expr.empty()) {
	                    return expr;
	                }
	            }
	            return "(" + branch + ")." + field;
	        };
	        bool conditionalFieldProjection = true;
	        while (conditionalFieldProjection) {
	            conditionalFieldProjection = false;
	            for (size_t dot = 0; (dot = out.find(").", dot)) != std::string::npos;) {
	                auto fieldStart = dot + 2;
	                auto fieldEnd = fieldStart;
	                while (fieldEnd < out.size() &&
	                       (std::isalnum(static_cast<unsigned char>(out[fieldEnd])) || out[fieldEnd] == '_')) {
	                    ++fieldEnd;
	                }
	                auto field = out.substr(fieldStart, fieldEnd - fieldStart);
	                if (field.empty() || field == "bits" || field == "get") {
	                    dot = fieldEnd;
	                    continue;
	                }
	                int depth = 0;
	                size_t open = std::string::npos;
	                for (size_t pos = dot + 1; pos-- > 0;) {
	                    if (out[pos] == ')') {
	                        ++depth;
	                    }
	                    else if (out[pos] == '(') {
	                        --depth;
	                        if (depth == 0) {
	                            open = pos;
	                            break;
	                        }
	                    }
	                    if (pos == 0) {
	                        break;
	                    }
	                }
	                if (open == std::string::npos) {
	                    dot = fieldEnd;
	                    continue;
	                }
	                auto inner = trim(out.substr(open + 1, dot - open - 1));
	                while (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') {
	                    auto close = matchingParenClose(inner, 0);
	                    if (close != inner.size() - 1) {
	                        break;
	                    }
	                    inner = trim(inner.substr(1, inner.size() - 2));
	                }
	                int paren = 0;
	                int angle = 0;
	                size_t question = std::string::npos;
	                size_t colon = std::string::npos;
	                for (size_t pos = 0; pos < inner.size(); ++pos) {
	                    if (inner[pos] == '(') ++paren;
	                    else if (inner[pos] == ')' && paren > 0) --paren;
	                    else if (inner[pos] == '<' && paren == 0) ++angle;
	                    else if (inner[pos] == '>' && paren == 0 && angle > 0) --angle;
	                    else if (inner[pos] == '?' && paren == 0 && angle == 0) question = pos;
	                    else if (inner[pos] == ':' && paren == 0 && angle == 0 && question != std::string::npos) {
	                        if ((pos > 0 && inner[pos - 1] == ':') || (pos + 1 < inner.size() && inner[pos + 1] == ':')) {
	                            continue;
	                        }
	                        colon = pos;
	                        break;
	                    }
	                }
	                if (question == std::string::npos || colon == std::string::npos) {
	                    dot = fieldEnd;
	                    continue;
	                }
	                auto cond = trim(inner.substr(0, question));
	                auto lhs = trim(inner.substr(question + 1, colon - question - 1));
	                auto rhs = trim(inner.substr(colon + 1));
	                auto replacement = "(" + cond + " ? " + projectedFieldCall(lhs, field) +
	                    " : " + projectedFieldCall(rhs, field) + ")";
	                out.replace(open, fieldEnd - open, replacement);
	                dot = open + replacement.size();
	                conditionalFieldProjection = true;
	            }
	        }
	        bool finalFieldProjection = true;
	        while (finalFieldProjection) {
	            finalFieldProjection = false;
	            for (const auto& item : mod.combMethodByField) {
	                if (item.second >= mod.methods.size()) {
	                    continue;
	                }
	                auto baseIt = mod.combMethodByBase.find(item.first.first);
	                if (baseIt == mod.combMethodByBase.end() || baseIt->second >= mod.methods.size()) {
	                    continue;
	                }
		                const auto& baseMethod = mod.methods[baseIt->second].name;
		                const auto& field = item.first.second;
		                const auto& fieldMethod = mod.methods[item.second].name;
		                if (fieldMethod == excludeDriver ||
		                    mod.methods[item.second].returnName == exclude) {
		                    continue;
		                }
		                auto needle = baseMethod + "()." + field;
	                auto replacement = fieldMethod + "()";
	                for (size_t pos = 0; (pos = out.find(needle, pos)) != std::string::npos;) {
	                    auto end = pos + needle.size();
	                    auto next = end < out.size() ? out[end] : '\0';
	                    if (std::isalnum(static_cast<unsigned char>(next)) || next == '_') {
	                        pos = end;
	                        continue;
	                    }
	                    out.replace(pos, needle.size(), replacement);
	                    pos += replacement.size();
	                    finalFieldProjection = true;
	                }
	            }
	        }
	        return out;
	    }

    std::string stripSelfSideEffectWrappers(const ModuleGen& mod, const std::string& line, const std::string& driver)
    {
        if (driver.empty()) {
            return line;
        }
        if (line.find("((") == std::string::npos) {
            return line;
        }
        auto out = line;
        bool changed = true;
        auto stripForName = [&](const std::string& name) {
            if (name.empty()) {
                return false;
            }
            auto before = out;
            replaceAll(out, "((" + driver + "(), " + name + "))", name);
            replaceAll(out, "((" + driver + "(), " + name + "), " + name + ")", name);
            return before != out;
        };
        auto isSimpleName = [](const std::string& name) {
            if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_')) {
                return false;
            }
            for (auto ch : name) {
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                    return false;
                }
            }
            return true;
        };
        auto stripDirectDriverWrappers = [&]() {
            auto beforeAll = out;
            auto prefix = "((" + driver + "(), ";
            for (size_t pos = 0; (pos = out.find(prefix, pos)) != std::string::npos;) {
                auto nameBegin = pos + prefix.size();
                auto nameEnd = nameBegin;
                while (nameEnd < out.size() &&
                       (std::isalnum(static_cast<unsigned char>(out[nameEnd])) || out[nameEnd] == '_')) {
                    ++nameEnd;
                }
                auto name = out.substr(nameBegin, nameEnd - nameBegin);
                if (!isSimpleName(name)) {
                    pos += prefix.size();
                    continue;
                }
                auto replacementEnd = std::string::npos;
                if (out.compare(nameEnd, 2, "))") == 0) {
                    replacementEnd = nameEnd + 2;
                }
                else {
                    auto suffix = "), " + name + ")";
                    if (out.compare(nameEnd, suffix.size(), suffix) == 0) {
                        replacementEnd = nameEnd + suffix.size();
                    }
                }
                if (replacementEnd == std::string::npos) {
                    pos += prefix.size();
                    continue;
                }
                out.replace(pos, replacementEnd - pos, name);
                pos += name.size();
            }
            return out != beforeAll;
        };
        struct SideEffectWrapperCache {
            const ModuleGen* mod = nullptr;
            size_t methodSize = 0;
            std::map<std::string, std::vector<std::pair<std::string, std::string>>> callersByDriver;
        };
        static SideEffectWrapperCache cache;
        if (cache.mod != &mod || cache.methodSize != mod.methods.size()) {
            cache = {};
            cache.mod = &mod;
            cache.methodSize = mod.methods.size();
        }
        auto callersForDriver = [&]() -> const std::vector<std::pair<std::string, std::string>>& {
            auto it = cache.callersByDriver.find(driver);
            if (it != cache.callersByDriver.end()) {
                return it->second;
            }
            std::vector<std::pair<std::string, std::string>> callers;
            for (const auto& method : mod.methods) {
                if (method.name.empty() || method.returnName.empty() || method.name == driver) {
                    continue;
                }
                auto callsDriver = std::any_of(method.body.begin(), method.body.end(), [&](const std::string& bodyLine) {
                    return trim(bodyLine) == driver + "();" || bodyLine.find(driver + "();") != std::string::npos;
                });
                if (callsDriver) {
                    callers.push_back({method.name, method.returnName});
                }
            }
            auto [inserted, _] = cache.callersByDriver.emplace(driver, std::move(callers));
            return inserted->second;
        };
        while (changed) {
            changed = false;
            changed = stripDirectDriverWrappers() || changed;
            for (const auto& method : callersForDriver()) {
                auto before = out;
                replaceAll(out, method.first + "()", method.second);
                changed = changed || before != out;
            }
            if (out.find("((" + driver + "(), ") == std::string::npos) {
                continue;
            }
            for (auto& name : mod.varNames) {
                changed = stripForName(name) || changed;
            }
            for (auto& outPort : mod.outputPortCppNames) {
                changed = stripForName(outPort.first) || changed;
                changed = stripForName(outPort.second) || changed;
                changed = stripForName(outputStorageName(mod, outPort.first)) || changed;
                changed = stripForName(combStorageName(mod, outPort.first)) || changed;
            }
            for (auto& item : mod.combSideEffectDriver) {
                if (item.second != driver) {
                    continue;
                }
                auto before = out;
                auto storageName = combStorageName(mod, item.first);
                if (mod.outputPortCppNames.count(item.first)) {
                    storageName = outputStorageName(mod, item.first);
                }
                if (auto methodIt = mod.combMethodByBase.find(item.first);
                    methodIt != mod.combMethodByBase.end() && methodIt->second < mod.methods.size()) {
                    const auto& bridge = mod.methods[methodIt->second];
                    if (!bridge.name.empty() && !bridge.returnName.empty()) {
                        replaceAll(out, bridge.name + "()", bridge.returnName);
                    }
                }
                replaceAll(out, "((" + driver + "(), " + item.first + "))", item.first);
                replaceAll(out, "((" + driver + "(), " + item.first + "), " + item.first + ")", item.first);
                replaceAll(out, "((" + driver + "(), " + storageName + "))", storageName);
                replaceAll(out, "((" + driver + "(), " + storageName + "), " + storageName + ")", storageName);
                changed = changed || before != out;
            }
        }
        return out;
    }

    bool isPurePortBridgeComb(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.name.find("_comb_func") == std::string::npos || method.returnName.empty() ||
            method.returnBase.empty() || method.body.size() != 1) {
            return false;
        }
        auto line = trim(method.body.front());
        if (!line.empty() && line.back() == ';') {
            line.pop_back();
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return false;
        }
        auto lhs = trim(line.substr(0, eq));
        auto rhs = trim(line.substr(eq + 1));
        auto lhsOk = lhs == method.returnBase || lhs == method.returnName ||
            lhs == combStorageName(mod, method.returnBase);
        if (!lhsOk && mod.outputPortCppNames.count(method.returnBase)) {
            lhsOk = lhs == outputStorageName(mod, method.returnBase);
        }
        if (!lhsOk) {
            auto outIt = mod.outputPortCppNames.find(method.returnBase);
            lhsOk = outIt != mod.outputPortCppNames.end() && lhs == outIt->second;
        }
        if (!lhsOk) {
            return false;
        }
        auto isIdent = [](const std::string& s) {
            if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_')) {
                return false;
            }
            for (auto c : s) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
                    return false;
                }
            }
            return true;
        };
        auto dot = rhs.find('.');
        if (dot == std::string::npos || rhs.find('.', dot + 1) != std::string::npos) {
            return false;
        }
        auto instance = trim(rhs.substr(0, dot));
        auto call = trim(rhs.substr(dot + 1));
        if (call.size() < 7 || call.substr(call.size() - 2) != "()") {
            return false;
        }
        auto port = call.substr(0, call.size() - 2);
        auto isOutputPortName = [](const std::string& name) {
            return hasSuffix(name, "_out") || hasSuffix(name, "_o") ||
                   name.find("_o_") != std::string::npos;
        };
        return isIdent(instance) && isIdent(port) && isOutputPortName(port);
    }

    bool methodReadsChildOutputPort(const MethodGen& method)
    {
        auto isOutputPortName = [](const std::string& name) {
            return hasSuffix(name, "_out") || hasSuffix(name, "_o") ||
                   name.find("_o_") != std::string::npos;
        };
        auto isIdent = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        for (const auto& line : method.body) {
            for (size_t pos = 0; (pos = line.find("_out()", pos)) != std::string::npos; pos += 6) {
                if (pos > 0 && line[pos - 1] == '.') {
                    return true;
                }
            }
            for (size_t dot = 0; (dot = line.find('.', dot)) != std::string::npos; ++dot) {
                auto start = dot + 1;
                if (start >= line.size() || !isIdent(line[start])) {
                    continue;
                }
                auto end = start;
                while (end < line.size() && isIdent(line[end])) {
                    ++end;
                }
                if (end + 1 >= line.size() || line[end] != '(' || line[end + 1] != ')') {
                    continue;
                }
                if (isOutputPortName(line.substr(start, end - start))) {
                    return true;
                }
            }
        }
        return false;
    }

    bool isChildOutputComb(const ModuleGen& mod, const MethodGen& method)
    {
        return method.name.find("_comb_func") != std::string::npos &&
               !method.returnName.empty() &&
               !method.returnBase.empty() &&
               !mod.seqAssignedVars.count(method.returnBase) &&
               methodReadsChildOutputPort(method);
    }

    bool isPureCombOutputBundle(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.name.find("_comb_func") == std::string::npos ||
            method.returnName.empty() || !mod.seqAssignedVars.empty()) {
            return false;
        }
        for (const auto& item : mod.combSideEffectDriver) {
            if (item.second != method.name) {
                continue;
            }
            const auto& base = item.first;
            if (base.empty() || base == method.returnBase || base == method.returnName) {
                continue;
            }
            if (mod.outputPortCppNames.count(base)) {
                return true;
            }
        }
        return false;
    }

    bool baseNoCacheCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        if (configuredNameEquals("HDLCPP_NOCACHE_COMB_METHODS", mod.name + "|" + method.name) ||
            configuredNameEquals("HDLCPP_NOCACHE_COMB_METHODS", mod.name + "|" + method.returnName) ||
            configuredNameEquals("HDLCPP_NOCACHE_COMB_METHODS", method.name) ||
            configuredNameEquals("HDLCPP_NOCACHE_COMB_METHODS", method.returnName)) {
            return true;
        }
        if ((!method.returnBase.empty() && mod.noCacheCombBases.count(method.returnBase)) ||
            (!method.returnName.empty() && mod.noCacheCombBases.count(method.returnName)) ||
            mod.noCacheCombBases.count(method.name)) {
            return true;
        }
        return emitPlainCombMethod(mod, method);
    }

    bool combMethodFeedsSequentialChildInput(const ModuleGen& mod, const MethodGen& method)
    {
        auto childModuleHasSequentialState = [](const ModuleGen* child) {
            if (!child) {
                return false;
            }
            if (!child->seqAssignedVars.empty()) {
                return true;
            }
            for (const auto& childMethod : child->methods) {
                if (childMethod.args == "bool reset" && childMethod.name.rfind("always_", 0) == 0) {
                    return true;
                }
            }
            return false;
        };
        auto childInputPort = [](const ModuleGen* child, const std::string& portName) {
            if (!child) {
                return false;
            }
            auto matches = [&](const std::string& candidate) {
                return candidate == portName ||
                       candidate == portName + "_in" ||
                       candidate == portName + "_out";
            };
            auto cppIt = child->portCppNames.find(portName);
            for (const auto& port : child->ports) {
                if (matches(port.name) ||
                    (cppIt != child->portCppNames.end() && port.name == cppIt->second)) {
                    return port.direction != "output";
                }
            }
            return false;
        };
        for (const auto& conn : mod.instanceConns) {
            if (!conn.connected) {
                continue;
            }
            auto rhsBase = baseFromLValueText(conn.rhs);
            if (rhsBase.empty() ||
                (rhsBase != method.returnBase && rhsBase != method.returnName && rhsBase != method.name)) {
                continue;
            }
            auto childType = trim(conn.type);
            if (auto lt = childType.find('<'); lt != std::string::npos) {
                childType = trim(childType.substr(0, lt));
            }
            auto* child = findModule(conn.type);
            if (!child && !childType.empty()) {
                child = findModule(childType);
            }
            if (childModuleHasSequentialState(child) && childInputPort(child, conn.port)) {
                return true;
            }
        }
        for (const auto& line : mod.assignLines) {
            if (method.name.empty() || line.find(method.name + "()") == std::string::npos) {
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto lhs = trim(line.substr(0, eq));
            auto dot = lhs.find('.');
            if (dot == std::string::npos) {
                continue;
            }
            auto instanceName = trim(lhs.substr(0, dot));
            auto portName = trim(lhs.substr(dot + 1));
            for (const auto& conn : mod.instanceConns) {
                if (conn.instance != instanceName) {
                    continue;
                }
                auto childType = trim(conn.type);
                if (auto lt = childType.find('<'); lt != std::string::npos) {
                    childType = trim(childType.substr(0, lt));
                }
                auto* child = findModule(conn.type);
                if (!child && !childType.empty()) {
                    child = findModule(childType);
                }
                if (!child) {
                    return true;
                }
                if (childModuleHasSequentialState(child) && childInputPort(child, portName)) {
                    return true;
                }
                break;
            }
        }
        return false;
    }

    bool outputPortMatches(const ModuleGen& mod, const std::string& portName, const std::string& candidate)
    {
        if (portName.empty() || candidate.empty()) {
            return false;
        }
        if (candidate == portName || candidate == portName + "_out") {
            return true;
        }
        auto cppIt = mod.outputPortCppNames.find(portName);
        if (cppIt != mod.outputPortCppNames.end() &&
            (candidate == cppIt->second || candidate == outputStorageName(mod, portName) ||
             candidate == combStorageName(mod, portName))) {
            return true;
        }
        return false;
    }

    bool methodImplementsOutputPort(const ModuleGen& mod, const MethodGen& method, const std::string& portName)
    {
        if (method.name.find("_comb_func") == std::string::npos || !mod.outputPortCppNames.count(portName)) {
            return false;
        }
        if (outputPortMatches(mod, portName, method.returnBase) ||
            outputPortMatches(mod, portName, method.returnName)) {
            return true;
        }
        auto cppName = mod.outputPortCppNames.at(portName);
        return method.name == portName + "_comb_func" ||
               method.name == cppName + "_comb_func" ||
               method.name == outputStorageName(mod, portName) + "_func" ||
               method.name == combStorageName(mod, portName) + "_func";
    }

    bool methodBodyMentionsCombMethod(const MethodGen& method, const MethodGen& candidate,
                                      bool includeReturnStorage, bool includeReturnBase)
    {
        auto isIdent = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        auto containsToken = [&](const std::string& line, const std::string& token) {
            if (token.empty()) {
                return false;
            }
            for (size_t pos = 0; (pos = line.find(token, pos)) != std::string::npos;) {
                auto end = pos + token.size();
                auto leftOk = pos == 0 || !isIdent(line[pos - 1]);
                auto rightOk = end >= line.size() || !isIdent(line[end]);
                if (leftOk && rightOk) {
                    return true;
                }
                pos = end;
            }
            return false;
        };
        const std::string call = candidate.name + "()";
        for (const auto& line : method.body) {
            if (containsToken(line, call) ||
                (includeReturnStorage && containsToken(line, candidate.returnName)) ||
                (includeReturnBase && containsToken(line, candidate.returnBase))) {
                return true;
            }
        }
        return false;
    }

    bool methodBodyMentionsIdentifier(const MethodGen& method, const std::string& token)
    {
        if (token.empty()) {
            return false;
        }
        auto isIdent = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        for (const auto& line : method.body) {
            for (size_t pos = 0; (pos = line.find(token, pos)) != std::string::npos;) {
                auto end = pos + token.size();
                auto leftOk = pos == 0 || !isIdent(line[pos - 1]);
                auto rightOk = end >= line.size() || !isIdent(line[end]);
                if (leftOk && rightOk) {
                    return true;
                }
                pos = end;
            }
        }
        return false;
    }

    bool parentCombPathFromBaseFeedsSequentialChildInput(const ModuleGen& parent, const std::string& base)
    {
        if (base.empty()) {
            return false;
        }
        auto childModuleHasSequentialState = [](const ModuleGen* child) {
            if (!child) {
                return false;
            }
            if (!child->seqAssignedVars.empty()) {
                return true;
            }
            for (const auto& childMethod : child->methods) {
                if (childMethod.args == "bool reset" && childMethod.name.rfind("always_", 0) == 0) {
                    return true;
                }
            }
            return false;
        };
        auto childInputPort = [](const ModuleGen* child, const std::string& portName) {
            if (!child) {
                return false;
            }
            auto matches = [&](const std::string& candidate) {
                return candidate == portName ||
                       candidate == portName + "_in" ||
                       candidate == portName + "_out";
            };
            auto cppIt = child->portCppNames.find(portName);
            for (const auto& port : child->ports) {
                if (matches(port.name) ||
                    (cppIt != child->portCppNames.end() && port.name == cppIt->second)) {
                    return port.direction != "output";
                }
            }
            return false;
        };
        for (const auto& conn : parent.instanceConns) {
            if (!conn.connected || baseFromLValueText(conn.rhs) != base) {
                continue;
            }
            auto* child = findModule(conn.type);
            if (childModuleHasSequentialState(child) && childInputPort(child, conn.port)) {
                return true;
            }
        }
        const MethodGen* baseMethod = nullptr;
        if (auto it = parent.combMethodByBase.find(base);
            it != parent.combMethodByBase.end() && it->second < parent.methods.size()) {
            baseMethod = &parent.methods[it->second];
            if (combMethodFeedsSequentialChildInput(parent, *baseMethod)) {
                return true;
            }
        }
        for (const auto& candidate : parent.methods) {
            if (candidate.name.find("_comb_func") == std::string::npos ||
                !combMethodFeedsSequentialChildInput(parent, candidate)) {
                continue;
            }
            if (baseMethod && methodBodyMentionsCombMethod(candidate, *baseMethod, true, true)) {
                return true;
            }
            if (methodBodyMentionsIdentifier(candidate, base)) {
                return true;
            }
        }
        return false;
    }

    bool childOutputFeedsSequentialParentComb(const ModuleGen& childMod, const MethodGen& method)
    {
        std::vector<std::string> outputPorts;
        for (const auto& out : childMod.outputPortCppNames) {
            if (methodImplementsOutputPort(childMod, method, out.first)) {
                outputPorts.push_back(out.first);
            }
        }
        if (outputPorts.empty()) {
            return false;
        }
        for (const auto& parent : modules) {
            for (const auto& conn : parent.instanceConns) {
                if (!conn.connected) {
                    continue;
                }
                auto* resolvedChild = findModule(conn.type);
                if (!resolvedChild || resolvedChild->name != childMod.name) {
                    continue;
                }
                bool portMatches = false;
                for (const auto& outputPort : outputPorts) {
                    if (outputPortMatches(childMod, outputPort, conn.port)) {
                        portMatches = true;
                        break;
                    }
                }
                if (!portMatches) {
                    continue;
                }
                auto rhsBase = baseFromLValueText(conn.rhs);
                auto lhsBase = baseFromLValueText(conn.lhs);
                if (parentCombPathFromBaseFeedsSequentialChildInput(parent, rhsBase) ||
                    (lhsBase != rhsBase && parentCombPathFromBaseFeedsSequentialChildInput(parent, lhsBase))) {
                    return true;
                }
            }
        }
        return false;
    }

    bool sequentialModuleCombOutputMethod(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.name.find("_comb_func") == std::string::npos || mod.seqAssignedVars.empty() ||
            (!method.returnBase.empty() && mod.seqAssignedVars.count(method.returnBase))) {
            return false;
        }
        for (const auto& out : mod.outputPortCppNames) {
            if (methodImplementsOutputPort(mod, method, out.first)) {
                return true;
            }
        }
        return false;
    }

    bool directNoCacheCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        return combMethodFeedsSequentialChildInput(mod, method) ||
               childOutputFeedsSequentialParentComb(mod, method) ||
               sequentialModuleCombOutputMethod(mod, method) ||
               baseNoCacheCombMethod(mod, method);
    }

    bool noCacheCombMethodRec(const ModuleGen& mod, const MethodGen& method, std::set<std::string>& visiting)
    {
        if (directNoCacheCombMethod(mod, method)) {
            return true;
        }
        if (!visiting.insert(method.name).second) {
            return false;
        }
        for (const auto& candidate : mod.methods) {
            if (candidate.name == method.name ||
                candidate.name.find("_comb_func") == std::string::npos) {
                continue;
            }
            auto directBaseNoCache = directNoCacheCombMethod(mod, candidate);
            auto directLocalChildOutput = isChildOutputComb(mod, candidate) &&
                !mod.outputPortCppNames.count(candidate.returnBase);
            if (methodBodyMentionsCombMethod(method, candidate, directBaseNoCache, directLocalChildOutput) &&
                noCacheCombMethodRec(mod, candidate, visiting)) {
                visiting.erase(method.name);
                return true;
            }
        }
        visiting.erase(method.name);
        return false;
    }

    bool combMethodHasCallCycle(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.name.empty() || method.name.find("_comb_func") == std::string::npos) {
            return false;
        }
        std::set<std::string> visited;
        auto dfs = [&](auto&& self, const MethodGen& current) -> bool {
            for (const auto& candidate : mod.methods) {
                if (candidate.name.find("_comb_func") == std::string::npos) {
                    continue;
                }
                const bool includeReturnNames = candidate.name != current.name;
                if (!methodBodyMentionsCombMethod(current, candidate, includeReturnNames, includeReturnNames)) {
                    continue;
                }
                if (candidate.name == method.name) {
                    return true;
                }
                if (!visited.insert(candidate.name).second) {
                    continue;
                }
                if (self(self, candidate)) {
                    return true;
                }
            }
            return false;
        };
        return dfs(dfs, method);
    }

    bool noCacheCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        if (combMethodHasCallCycle(mod, method)) {
            return false;
        }
        if (directNoCacheCombMethod(mod, method)) {
            return true;
        }
        for (const auto& candidate : mod.methods) {
            if (candidate.name == method.name ||
                candidate.name.find("_comb_func") == std::string::npos ||
                !directNoCacheCombMethod(mod, candidate)) {
                continue;
            }
            if (methodBodyMentionsCombMethod(candidate, method, true, true)) {
                return true;
            }
        }
        auto proceduralCombMethod = [](const MethodGen& item) {
            if (item.localCombBody) {
                return true;
            }
            if (item.returnName.empty()) {
                return false;
            }
            const auto initLine = item.returnName + " = {};";
            for (const auto& line : item.body) {
                if (trim(line) == initLine) {
                    return true;
                }
            }
            return false;
        };
        if (proceduralCombMethod(method)) {
            for (const auto& candidate : mod.methods) {
                if (candidate.name == method.name ||
                    candidate.name.find("_comb_func") == std::string::npos) {
                    continue;
                }
                if (methodBodyMentionsCombMethod(method, candidate, true, true)) {
                    return true;
                }
            }
        }
        for (const auto& candidate : mod.methods) {
            if (candidate.name == method.name ||
                candidate.name.find("_comb_func") == std::string::npos ||
                !proceduralCombMethod(candidate)) {
                continue;
            }
            if (methodBodyMentionsCombMethod(candidate, method, true, true)) {
                return true;
            }
        }
        return false;
    }

    std::string lateBindCombRhs(const ModuleGen& mod, const MethodGen& method, const std::string& line)
    {
        auto comb = method.name.find("_comb_func") != std::string::npos;
        auto selfStrippedLine = stripSelfSideEffectWrappers(mod, line, method.name);
        auto trimmed = trim(selfStrippedLine);
        auto bindWithLocals = [&](std::string text, const std::string& exclude, bool lhs = false) {
            std::vector<std::pair<std::string, std::string>> protectedNames;
            std::set<std::string> textIdentifiers;
            for (size_t pos = 0; pos < text.size();) {
                if (!(std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
                    ++pos;
                    continue;
                }
                auto start = pos++;
                while (pos < text.size() &&
                       (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
                    ++pos;
                }
                textIdentifiers.insert(text.substr(start, pos - start));
            }
            size_t idx = 0;
            for (const auto& local : method.localNames) {
                if (local.empty() || local == exclude) {
                    continue;
                }
                if (!textIdentifiers.count(local)) {
                    continue;
                }
                auto placeholder = "__hdlcpp_local_" + std::to_string(idx++) + "__";
                replaceIdentifierAll(text, local, placeholder);
                protectedNames.push_back({placeholder, local});
            }
            auto out = lateBindExpr(mod, text, exclude, method.name, lhs);
            for (const auto& item : protectedNames) {
                replaceIdentifierAll(out, item.first, item.second);
            }
            out = stripSelfSideEffectWrappers(mod, out, method.name);
            return out;
        };
        if (trimmed.rfind("case ", 0) == 0 || trimmed.rfind("default:", 0) == 0 ||
            trimmed == "{" || trimmed == "}" || trimmed == "else {") {
            return selfStrippedLine;
        }
        if (trimmed.rfind("if ", 0) == 0 || trimmed.rfind("if(", 0) == 0 ||
            trimmed.rfind("for ", 0) == 0 || trimmed.rfind("for(", 0) == 0 ||
            trimmed.rfind("switch ", 0) == 0 || trimmed.rfind("switch(", 0) == 0) {
            auto controlLine = selfStrippedLine;
            if (comb && !method.returnName.empty() && !method.returnBase.empty()) {
                replaceIdentifierAll(controlLine, method.returnBase, method.returnName);
            }
            return bindWithLocals(controlLine, "");
        }
        auto eq = selfStrippedLine.find('=');
        if (eq == std::string::npos) {
            if (!hdlcpp::declarationName(selfStrippedLine).empty()) {
                return selfStrippedLine;
            }
            return bindWithLocals(selfStrippedLine, "");
        }
        auto lhs = selfStrippedLine.substr(0, eq);
        auto rhs = selfStrippedLine.substr(eq + 1);
        auto declaredLocal = hdlcpp::declarationName(selfStrippedLine);
        if (!declaredLocal.empty()) {
            auto boundRhs = bindWithLocals(rhs, "");
            return lhs + "=" + boundRhs;
        }
        auto lhsBase = baseFromLValueText(lhs);
        if (comb && !method.returnName.empty() && !lhsBase.empty() && lhsBase == method.returnBase &&
            !method.localNames.count(lhsBase)) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), method.returnName);
            }
        }
        else if (comb && !lhsBase.empty() && !method.localNames.count(lhsBase) &&
                 mod.combMethodByBase.count(lhsBase) && !mod.seqAssignedVars.count(lhsBase)) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), combStorageName(mod, lhsBase));
            }
        }
        else if (!comb && !lhsBase.empty() && !method.localNames.count(lhsBase) &&
                 mod.combMethodByBase.count(lhsBase) && !mod.seqAssignedVars.count(lhsBase)) {
            auto baseEnd = lhs.find(lhsBase);
            if (baseEnd != std::string::npos) {
                lhs.replace(baseEnd, lhsBase.size(), combStorageName(mod, lhsBase));
            }
        }
        if (comb && !method.returnName.empty() && !method.returnBase.empty() &&
            !method.localNames.count(method.returnBase)) {
            replaceIdentifierAll(rhs, method.returnBase, method.returnName);
        }
        auto boundLhs = bindWithLocals(lhs, lhsBase, true);
        if (!lhsBase.empty()) {
            if (auto renameIt = mod.wireMap.find(lhsBase);
                renameIt != mod.wireMap.end() && !moduleMethodExists(mod, renameIt->second)) {
                replaceIdentifierAll(boundLhs, lhsBase, renameIt->second);
            }
        }
        auto trimmedLhs = trim(lhs);
        for (auto& outPort : mod.outputPortCppNames) {
            if (trimmedLhs == outPort.second && isAssignOnlyOutput(mod, outPort.first)) {
                boundLhs = lhs;
                break;
            }
        }
        auto rhsExclude = (comb && !method.returnName.empty()) ? method.returnName : std::string();
        auto boundRhs = bindWithLocals(rhs, rhsExclude);
        return boundLhs + "=" + boundRhs;
    }

    bool isCombAddressableExpr(std::string expr)
    {
        expr = trim(std::move(expr));
        if (isSimpleCombRef(expr)) {
            return true;
        }
        auto pos = expr.find("_comb_func()");
        if (pos == std::string::npos) {
            return false;
        }
        auto callEnd = pos + std::string("_comb_func()").size();
        auto start = pos;
        while (start > 0) {
            auto c = expr[start - 1];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                break;
            }
            --start;
        }
        if (start == pos) {
            return false;
        }
        for (size_t i = 0; i < start; ++i) {
            if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                return false;
            }
        }
        auto rest = trim(expr.substr(callEnd));
        while (!rest.empty() && rest.front() == ')') {
            rest = trim(rest.substr(1));
        }
        return rest.empty() || rest.front() == '.' || rest.front() == '[';
    }

    bool isCpphdlGetterAddressableExpr(std::string expr)
    {
        expr = trim(std::move(expr));
        while (expr.size() > 2 && expr.front() == '(' && expr.back() == ')') {
            auto close = matchingParenClose(expr, 0);
            if (close != expr.size() - 1) {
                break;
            }
            expr = trim(expr.substr(1, expr.size() - 2));
        }

        const char* getters[] = {"_in()", "_out()", "_comb_func()"};
        for (auto* getter : getters) {
            auto pos = expr.find(getter);
            if (pos == std::string::npos) {
                continue;
            }
            auto callEnd = pos + std::string(getter).size();
            auto start = pos;
            while (start > 0) {
                auto c = expr[start - 1];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                    break;
                }
                --start;
            }
            if (start == pos) {
                continue;
            }
            for (size_t i = 0; i < start; ++i) {
                if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                    return false;
                }
            }
            auto rest = trim(expr.substr(callEnd));
            while (!rest.empty() && rest.front() == ')') {
                rest = trim(rest.substr(1));
            }
            return rest.empty() || rest.front() == '.' || rest.front() == '[';
        }
        return false;
    }

    bool arrayTypeIsPacked(std::string type)
    {
        type = unwrapRegType(trim(std::move(type)));
        auto args = templateArgsFor(type, "array");
        if (args.empty()) {
            return false;
        }
        return args.size() >= 3 && trim(args[2]) == "true";
    }

    bool cpphdlGetterExprIndexesArrayElement(std::string expr)
    {
        expr = trim(std::move(expr));
        while (expr.size() > 2 && expr.front() == '(' && expr.back() == ')') {
            auto close = matchingParenClose(expr, 0);
            if (close != expr.size() - 1) {
                break;
            }
            expr = trim(expr.substr(1, expr.size() - 2));
        }

        const char* getters[] = {"_in()", "_out()", "_comb_func()"};
        for (auto* getter : getters) {
            auto pos = expr.find(getter);
            if (pos == std::string::npos) {
                continue;
            }
            auto callEnd = pos + std::string(getter).size();
            auto start = pos;
            while (start > 0) {
                auto c = expr[start - 1];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                    break;
                }
                --start;
            }
            if (start == pos) {
                continue;
            }
            for (size_t i = 0; i < start; ++i) {
                if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                    return false;
                }
            }
            auto rest = trim(expr.substr(callEnd));
            while (!rest.empty() && rest.front() == ')') {
                rest = trim(rest.substr(1));
            }
            return !rest.empty() && rest.front() == '[';
        }
        return false;
    }

    std::string leadingCpphdlGetterName(std::string expr)
    {
        expr = trim(std::move(expr));
        while (expr.size() > 2 && expr.front() == '(' && expr.back() == ')') {
            auto close = matchingParenClose(expr, 0);
            if (close != expr.size() - 1) {
                break;
            }
            expr = trim(expr.substr(1, expr.size() - 2));
        }

        const char* getters[] = {"_in()", "_out()", "_comb_func()"};
        for (auto* getter : getters) {
            auto pos = expr.find(getter);
            if (pos == std::string::npos) {
                continue;
            }
            auto callEnd = pos + std::string(getter).size();
            auto start = pos;
            while (start > 0) {
                auto c = expr[start - 1];
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                    break;
                }
                --start;
            }
            if (start == pos) {
                continue;
            }
            for (size_t i = 0; i < start; ++i) {
                if (!std::isspace(static_cast<unsigned char>(expr[i])) && expr[i] != '(') {
                    return {};
                }
            }
            auto rest = trim(expr.substr(callEnd));
            while (!rest.empty() && rest.front() == ')') {
                rest = trim(rest.substr(1));
            }
            if (!rest.empty() && rest.front() != '.' && rest.front() != '[') {
                return {};
            }
            return expr.substr(start, callEnd - start - 2);
        }
        return {};
    }

    bool isKnownRvalueBindingExpr(std::string expr)
    {
        expr = trim(std::move(expr));
        while (expr.size() > 2 && expr.front() == '(' && expr.back() == ')') {
            auto close = matchingParenClose(expr, 0);
            if (close != expr.size() - 1) {
                break;
            }
            expr = trim(expr.substr(1, expr.size() - 2));
        }
        auto hasLiveValueIdentifier = [&](const std::string& text) {
            static const std::set<std::string> ignored{
                "array", "bool", "cat", "cpphdl", "decltype", "false", "logic",
                "pack_value", "remove_cvref_t", "signed", "static_cast", "std",
                "sv_bits", "sv_bits_runtime", "true", "type_width", "u",
                "uint16_t", "uint32_t", "uint64_t", "uint8_t", "unsigned",
                "unpack_value"
            };
            auto valueText = stripDecltypeExpressions(text);
            for (size_t i = 0; i < valueText.size();) {
                if (!(std::isalpha(static_cast<unsigned char>(valueText[i])) || valueText[i] == '_')) {
                    ++i;
                    continue;
                }
                auto start = i++;
                while (i < valueText.size() &&
                       (std::isalnum(static_cast<unsigned char>(valueText[i])) || valueText[i] == '_')) {
                    ++i;
                }
                auto ident = valueText.substr(start, i - start);
                if (ignored.count(ident)) {
                    continue;
                }
                return true;
            }
            return false;
        };
        const char* prefixes[] = {
            "logic<", "u<", "bool(", "static_cast<",
            "cpphdl::pack_value", "cpphdl::unpack_value",
            "cpphdl::sv_bits",
            "std::remove_cvref_t<", "cat{"
        };
        for (auto* prefix : prefixes) {
            if (expr.rfind(prefix, 0) == 0) {
                if ((std::string(prefix) == "logic<" || std::string(prefix) == "u<" ||
                     std::string(prefix) == "bool(" || std::string(prefix) == "static_cast<") &&
                    hasLiveValueIdentifier(expr)) {
                    return false;
                }
                return true;
            }
        }
        if (expr.rfind("[&]", 0) == 0 || expr.rfind("[]", 0) == 0) {
            return true;
        }
        if (isCpphdlGetterAddressableExpr(expr)) {
            return false;
        }
        auto combGetter = expr.find("_comb_func()");
        if (combGetter != std::string::npos && !isCombAddressableExpr(expr)) {
            return true;
        }
        return false;
    }

    std::string downgradeRvalueCombAssignWrappers(std::string line)
    {
	        const std::tuple<const char*, const char*, bool> wrappers[] = {
	            {"_ASSIGN_COMB_INDEXED(", "_ASSIGN_INDEXED", true},
	            {"_ASSIGN_COMB_IJ(", "_ASSIGN_IJ", true},
	            {"_ASSIGN_COMB_I(", "_ASSIGN_I", true},
	            {"_ASSIGN_COMB_J(", "_ASSIGN_J", true},
	            {"_ASSIGN_COMB(", "_ASSIGN", false},
	        };
        for (const auto& [combWrapper, valueWrapper, indexed] : wrappers) {
            auto pos = line.find(combWrapper);
            if (pos == std::string::npos) {
                continue;
            }
            auto open = pos + std::string(combWrapper).size() - 1;
            auto close = matchingParenClose(line, open);
            if (close == std::string::npos || close <= open + 1) {
                continue;
            }
            auto content = trim(line.substr(open + 1, close - open - 1));
            auto expr = content;
            if (indexed) {
                auto args = splitTopLevelArgs(content);
                if (args.size() < 2) {
                    continue;
                }
                expr = trim(args.back());
            }
            if (!isCombAddressableExpr(expr) && !isCpphdlGetterAddressableExpr(expr) &&
                (isKnownRvalueBindingExpr(expr) || referencesDynamicCpphdlGetter(expr))) {
                line.replace(pos, std::string(combWrapper).size() - 1, valueWrapper);
            }
        }
        return line;
    }

	    std::string normalizeAssignWrapperForCombCalls(std::string line)
	    {
	        if (line.find("_comb_func()") == std::string::npos &&
                line.find("_in()") == std::string::npos &&
                line.find("_out()") == std::string::npos) {
	            return downgradeRvalueCombAssignWrappers(std::move(line));
	        }
	        auto assignPos = line.find("_ASSIGN_REG(");
	        if (assignPos != std::string::npos) {
	            line.replace(assignPos, std::string("_ASSIGN_REG").size(), "_ASSIGN_COMB");
	            return line;
	        }
	        const std::pair<const char*, const char*> regIndexedWrappers[] = {
	            {"_ASSIGN_REG_IJ(", "_ASSIGN_COMB_IJ"},
	            {"_ASSIGN_REG_I(", "_ASSIGN_COMB_I"},
	            {"_ASSIGN_REG_J(", "_ASSIGN_COMB_J"},
	            {"_ASSIGN_REG_INDEXED(", "_ASSIGN_COMB_INDEXED"},
	        };
	        for (const auto& [regWrapper, combWrapper] : regIndexedWrappers) {
	            assignPos = line.find(regWrapper);
	            if (assignPos != std::string::npos) {
	                line.replace(assignPos, std::string(regWrapper).size() - 1, combWrapper);
	                return line;
	            }
	        }
	        {
	            auto downgraded = downgradeRvalueCombAssignWrappers(line);
	            if (downgraded != line) {
	                return downgraded;
	            }
	        }
	        const std::tuple<const char*, const char*, const char*> indexedWrappers[] = {
	            {"_ASSIGN_IJ(", "_ASSIGN_COMB_IJ", "_ASSIGN_REG_IJ"},
	            {"_ASSIGN_I(", "_ASSIGN_COMB_I", "_ASSIGN_REG_I"},
            {"_ASSIGN_J(", "_ASSIGN_COMB_J", "_ASSIGN_REG_J"},
            {"_ASSIGN_INDEXED(", "_ASSIGN_COMB_INDEXED", "_ASSIGN_REG_INDEXED"},
        };
        for (const auto& [valueWrapper, combWrapper, regWrapper] : indexedWrappers) {
            assignPos = line.find(valueWrapper);
            if (assignPos == std::string::npos) {
                continue;
            }
            std::string wrapper = valueWrapper;
            auto exprBegin = assignPos + wrapper.size();
            auto exprEnd = line.rfind(')');
            if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
                return line;
            }
            auto expr = trim(line.substr(exprBegin, exprEnd - exprBegin));
            if (referencesDynamicCpphdlGetter(expr) &&
                (isCombAddressableExpr(expr) || isCpphdlGetterAddressableExpr(expr))) {
                line.replace(assignPos, wrapper.size() - 1, combWrapper);
                return line;
            }
            if (!isCombAddressableExpr(expr) && !isCpphdlGetterAddressableExpr(expr)) {
                return line;
            }
	            line.replace(assignPos, wrapper.size() - 1, combWrapper);
            return line;
        }
        std::string wrapper = "_ASSIGN(";
        assignPos = line.find(wrapper);
        if (assignPos == std::string::npos) {
            wrapper = "_ASSIGN_COMB(";
            assignPos = line.find(wrapper);
        }
        if (assignPos == std::string::npos) {
            return line;
        }
        auto exprBegin = assignPos + wrapper.size();
        auto exprEnd = line.rfind(')');
        if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
            return line;
        }
        auto expr = trim(line.substr(exprBegin, exprEnd - exprBegin));
        if (referencesDynamicCpphdlGetter(expr) &&
            (isCombAddressableExpr(expr) || isCpphdlGetterAddressableExpr(expr))) {
            if (wrapper == "_ASSIGN(") {
                line.replace(assignPos, wrapper.size() - 1, "_ASSIGN_COMB");
            }
            return line;
        }
	        if (isCombAddressableExpr(expr) || isCpphdlGetterAddressableExpr(expr)) {
	            line.replace(assignPos, wrapper.size() - 1, "_ASSIGN_COMB");
        }
        return downgradeRvalueCombAssignWrappers(std::move(line));
    }

    std::string resolveChildTypeForInstance(const ModuleGen& mod, std::string instance)
    {
        instance = trim(instance);
        auto bracket = instance.find('[');
        if (bracket != std::string::npos) {
            instance = trim(instance.substr(0, bracket));
        }
        std::string metadataType;
        for (size_t i = 0; i < mod.memberNames.size() && i < mod.memberTypes.size(); ++i) {
            if (mod.memberNames[i] == instance) {
                metadataType = mod.memberTypes[i];
                break;
            }
        }
        for (auto decl : mod.members) {
            decl = trim(decl);
            if (!decl.empty() && decl.back() == ';') {
                decl.pop_back();
            }
            auto instPos = decl.rfind(instance);
            if (instPos == std::string::npos) {
                continue;
            }
            auto after = trim(decl.substr(instPos + instance.size()));
            if (!after.empty() && after[0] != '[') {
                continue;
            }
            auto before = trim(decl.substr(0, instPos));
            if (!before.empty()) {
                if (metadataType.empty() || before.find('<') != std::string::npos) {
                    return before;
                }
                return metadataType;
            }
        }
        if (!metadataType.empty()) {
            return metadataType;
        }
        return "";
    }

    std::string resolveChildBaseTypeForInstance(const ModuleGen& mod, std::string instance)
    {
        auto childType = arrayElementTypeText(resolveChildTypeForInstance(mod, std::move(instance)));
        if (auto lt = childType.find('<'); lt != std::string::npos) {
            return trim(childType.substr(0, lt));
        }
        return trim(childType);
    }

    bool isChildOutputPortCall(const ModuleGen& mod, std::string expr)
    {
        expr = trim(expr);
        if (!hasSuffix(expr, "()")) {
            return false;
        }
        expr = trim(expr.substr(0, expr.size() - 2));
        auto dot = expr.find('.');
        if (dot == std::string::npos || expr.find('.', dot + 1) != std::string::npos) {
            return false;
        }
        auto instance = trim(expr.substr(0, dot));
        auto portName = trim(expr.substr(dot + 1));
        if (instance.empty() || portName.empty() || instance.find('[') != std::string::npos) {
            return false;
        }
        auto childType = resolveChildTypeForInstance(mod, instance);
        auto baseChildType = trim(childType);
        if (auto lt = baseChildType.find('<'); lt != std::string::npos) {
            baseChildType = trim(baseChildType.substr(0, lt));
        }
        auto configuredOutput = [&](const std::string& name) {
            auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES");
            auto check = [&](const std::string& key) {
                auto it = portTypes.find(key);
                if (it == portTypes.end()) {
                    return false;
                }
                auto spec = it->second;
                auto sep = spec.find(':');
                auto direction = trim(sep == std::string::npos ? spec : spec.substr(0, sep));
                return direction == "output";
            };
            return check(childType + "." + name) || check(baseChildType + "." + name);
        };
        if (configuredOutput(portName)) {
            return true;
        }
        if (hasSuffix(portName, "_out") && configuredOutput(portName.substr(0, portName.size() - 4))) {
            return true;
        }
        auto* child = childType.empty() ? nullptr : findModule(childType);
        if (!child) {
            return false;
        }
        for (const auto& out : child->outputPortCppNames) {
            if (out.first == portName || out.second == portName) {
                return true;
            }
        }
        for (const auto& p : child->ports) {
            if (p.direction == "output" && p.name == portName) {
                return true;
            }
        }
        return false;
    }

    std::string inputPortTypeForBinding(const ModuleGen& mod, std::string line)
    {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return "";
        }
        auto lhs = trim(line.substr(0, eq));
        auto dot = lhs.find('.');
        if (dot == std::string::npos || lhs.find('.', dot + 1) != std::string::npos) {
            return "";
        }
        auto instance = trim(lhs.substr(0, dot));
        auto bracket = instance.find('[');
        if (bracket != std::string::npos) {
            instance = trim(instance.substr(0, bracket));
        }
        auto portName = trim(lhs.substr(dot + 1));
        auto portBracket = portName.find('[');
        if (portBracket != std::string::npos) {
            portName = trim(portName.substr(0, portBracket));
        }
        auto childType = resolveChildTypeForInstance(mod, instance);
        auto svPortName = portName;
        if (hasSuffix(svPortName, "_in")) {
            svPortName.resize(svPortName.size() - 3);
        }
        else if (hasSuffix(svPortName, "_out")) {
            svPortName.resize(svPortName.size() - 4);
        }
        auto baseChildType = trim(childType);
        std::string childParams;
        if (auto lt = baseChildType.find('<'); lt != std::string::npos) {
            size_t depth = 0;
            size_t close = std::string::npos;
            for (size_t i = lt; i < childType.size(); ++i) {
                if (childType[i] == '<') {
                    ++depth;
                }
                else if (childType[i] == '>') {
                    if (depth == 0) {
                        break;
                    }
                    --depth;
                    if (depth == 0) {
                        close = i;
                        break;
                    }
                }
            }
            if (close != std::string::npos && close > lt) {
                childParams = childType.substr(lt + 1, close - lt - 1);
            }
            baseChildType = trim(baseChildType.substr(0, lt));
        }
        auto substituteChildPortType = [&](std::string typeText) {
            typeText = trim(std::move(typeText));
            if (typeText.empty()) {
                return typeText;
            }
            auto typeAliasOverrides = configuredTextMap("HDLCPP_TYPE_ALIAS_OVERRIDES");
            for (const auto& aliasItem : typeAliasOverrides) {
                auto aliasName = aliasItem.first;
                auto dot = aliasName.rfind('.');
                if (dot != std::string::npos) {
                    if (aliasName.substr(0, dot) != baseChildType) {
                        continue;
                    }
                    aliasName = aliasName.substr(dot + 1);
                }
                if (!aliasName.empty() && !aliasItem.second.empty()) {
                    replaceIdentifierAll(typeText, aliasName, aliasItem.second);
                }
            }
            if (childParams.empty()) {
                return typeText;
            }
            auto* childModule = findModule(baseChildType);
            if (childModule && !childModule->params.empty()) {
                std::set<std::string> declaredParamNames;
                for (const auto& declared : childModule->params) {
                    auto name = templateParamName(declared);
                    if (!name.empty()) {
                        declaredParamNames.insert(name);
                    }
                }
                for (const auto& typeItem : childModule->types) {
                    if (declaredParamNames.count(typeItem.first)) {
                        continue;
                    }
                    if (!typeItem.first.empty() && !typeItem.second.empty() && typeItem.first != typeItem.second) {
                        replaceIdentifierAll(typeText, typeItem.first, typeItem.second);
                    }
                }
                auto localAliases = localUsingTypeAliases(*childModule);
                for (const auto& aliasItem : localAliases) {
                    if (!aliasItem.first.empty() && !aliasItem.second.empty()) {
                        replaceIdentifierAll(typeText, aliasItem.first, aliasItem.second);
                    }
                }
                return substituteParamDeclValues(childModule->params, splitTopLevelArgs(childParams), std::move(typeText));
            }
            return substituteModuleParamValues(baseChildType, childParams, typeText);
        };
        if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES"); !portTypes.empty()) {
            const std::string keys[] = {
                childType + "." + svPortName,
                childType + "." + portName,
                baseChildType + "." + svPortName,
                baseChildType + "." + portName,
                mod.name + "." + instance + "." + svPortName,
                mod.name + "." + instance + "." + portName,
            };
            for (const auto& key : keys) {
                if (auto it = portTypes.find(key); it != portTypes.end()) {
                    auto spec = it->second;
                    auto sep = spec.find(':');
                    auto direction = trim(sep == std::string::npos ? std::string{} : spec.substr(0, sep));
                    if (direction.empty() || direction == "input" || direction == "inout") {
                        return substituteChildPortType(sep == std::string::npos ? spec : spec.substr(sep + 1));
                    }
                }
            }
        }
        auto* child = childType.empty() ? nullptr : findModule(childType);
        if (!child) {
            child = baseChildType.empty() ? nullptr : findModule(baseChildType);
        }
        if (!child) {
            return "";
        }
        for (const auto& port : child->ports) {
            if ((port.name == portName || port.name == svPortName ||
                 port.name == svPortName + "_in" || port.name == svPortName + "_out") &&
                port.direction != "output") {
                return substituteChildPortType(port.type);
            }
        }
        return "";
    }

    bool isBoolInputPortBinding(const ModuleGen& mod, const std::string& line)
    {
        return inputPortTypeForBinding(mod, line) == "bool";
    }

    bool isChildInputPortBinding(const std::string& line)
    {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return false;
        }
        auto lhs = trim(line.substr(0, eq));
        auto dot = lhs.find('.');
        return dot != std::string::npos && lhs.find('.', dot + 1) == std::string::npos;
    }

    std::string normalizeChildOutputPortAssignWrapper(const ModuleGen& mod, std::string line)
    {
        auto assignPos = line.find("_ASSIGN(");
        if (assignPos == std::string::npos || line.find("_ASSIGN_COMB(") != std::string::npos) {
            return line;
        }
        auto exprBegin = assignPos + std::string("_ASSIGN(").size();
        auto exprEnd = line.rfind(')');
        if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
            return line;
        }
        auto expr = trim(line.substr(exprBegin, exprEnd - exprBegin));
        if (isChildOutputPortCall(mod, expr) || isSimpleCombRef(expr)) {
            auto inputType = inputPortTypeForBinding(mod, line);
            if (inputType == "bool") {
                if (expr.rfind("bool(", 0) != 0) {
                    expr = "bool(" + expr + ")";
                }
                line.replace(exprBegin, exprEnd - exprBegin, expr);
            }
            else if (isSimpleCombRef(expr) && isChildInputPortBinding(line) && inputType.empty()) {
                return line;
            }
            else {
	                line.replace(assignPos, std::string("_ASSIGN").size(), "_ASSIGN_COMB");
            }
        }
        return line;
    }

	    std::string normalizeBoolInputPortAssignWrapper(const ModuleGen& mod, std::string line)
	    {
        auto assignPos = line.find("_ASSIGN_COMB(");
        if (assignPos == std::string::npos) {
            return line;
        }
        if (!isBoolInputPortBinding(mod, line)) {
            return line;
        }
        auto exprBegin = assignPos + std::string("_ASSIGN_COMB(").size();
        auto exprEnd = line.rfind(')');
        if (exprEnd == std::string::npos || exprEnd <= exprBegin) {
            return line;
        }
        auto expr = trim(line.substr(exprBegin, exprEnd - exprBegin));
        if (referencesDynamicCpphdlGetter(expr) || isCombAddressableExpr(expr) ||
            isCpphdlGetterAddressableExpr(expr) || isSimpleCombRef(expr)) {
            return line;
        }
        if (expr.rfind("bool(", 0) != 0) {
            expr = "bool(" + expr + ")";
        }
	        line.replace(exprBegin, exprEnd - exprBegin, expr);
	        line.replace(assignPos, std::string("_ASSIGN_COMB").size(), "_ASSIGN");
	        if (line.find("_ASSIGN(bool(") != std::string::npos) {
	            int balance = 0;
	            for (size_t i = assignPos; i < line.size(); ++i) {
	                if (line[i] == '(') {
	                    ++balance;
	                }
	                else if (line[i] == ')') {
	                    --balance;
	                }
	            }
	            if (balance > 0 && balance <= 8) {
	                auto semi = line.rfind(';');
	                auto insertAt = semi == std::string::npos ? line.size() : semi;
	                line.insert(insertAt, std::string(static_cast<size_t>(balance), ')'));
	            }
	            auto trimmed = trim(line);
	            if (trimmed.empty() || trimmed.back() != ';') {
	                line += ";";
	            }
	        }
	        return line;
	    }

	    std::string normalizeInputPortCombValueWrapper(std::string line)
	    {
	        const std::string assignPackMarker = "_ASSIGN(cpphdl::pack_value";
	        if (auto packPos = line.find(assignPackMarker); packPos != std::string::npos) {
	            auto funcPos = line.find("__port_bind_", packPos);
	            auto funcEnd = funcPos == std::string::npos ? std::string::npos : line.find("_comb_func()", funcPos);
	            if (funcEnd != std::string::npos) {
	                funcEnd += std::string("_comb_func()").size();
	                auto expr = line.substr(funcPos, funcEnd - funcPos);
	                auto close = line.find(");", funcEnd);
	                if (close != std::string::npos) {
		                    line = line.substr(0, packPos) + "_ASSIGN_REG(" + expr + ")" + line.substr(close + 1);
	                }
	            }
	        }
	        const std::string packMarker = "_ASSIGN_COMB(cpphdl::pack_value";
	        if (auto packPos = line.find(packMarker); packPos != std::string::npos) {
	            line.replace(packPos, std::string("_ASSIGN_COMB").size(), "_ASSIGN");
	        }
	        const std::string marker = "_ASSIGN_COMB(array__port_bind";
	        auto pos = line.find(marker);
        if (pos != std::string::npos) {
            auto exprStart = pos + std::string("_ASSIGN_COMB(").size();
            auto funcEnd = line.find("_comb_func()", exprStart);
            if (funcEnd != std::string::npos) {
                funcEnd += std::string("_comb_func()").size();
                auto expr = line.substr(exprStart, funcEnd - exprStart);
                if (expr.rfind("array__", 0) == 0) {
                    expr.erase(0, std::string("array").size());
                    line = line.substr(0, exprStart) + expr + ");";
                }
            }
        }
        return line;
    }

    bool lineAssignsTarget(const std::string& line, const std::string& target)
    {
        auto t = trim(line);
        if (t.rfind(target, 0) != 0) {
            return false;
        }
        if (t.size() == target.size()) {
            return false;
        }
        auto c = t[target.size()];
        if (c != ' ' && c != '\t' && c != '=' && c != '[' && c != '.') {
            return false;
        }
        return t.find('=') != std::string::npos;
    }

    int braceDelta(const std::string& line)
    {
        int delta = 0;
        bool inString = false;
        bool escape = false;
        for (char c : line) {
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\' && inString) {
                escape = true;
                continue;
            }
            if (c == '"') {
                inString = !inString;
                continue;
            }
            if (inString) {
                continue;
            }
            if (c == '{') {
                ++delta;
            }
            else if (c == '}') {
                --delta;
            }
        }
        return delta;
    }

    void emitReadonlyCombOutputMethod(std::ofstream& out, const ModuleGen& mod, const std::string& svName)
    {
        auto drivers = combDriversFor(mod, svName);
        if (drivers.empty()) {
            return;
        }
        auto driverName = drivers.front();
        const MethodGen* driver = nullptr;
        for (auto& method : mod.methods) {
            if (method.name == driverName) {
                driver = &method;
                break;
            }
        }
        if (!driver) {
            return;
        }
        auto storageName = combStorageName(mod, svName);
        size_t scanLimit = driver->body.size();
        auto stopTokens = configuredTextMap("HDLCPP_READONLY_COMB_STOP_TOKENS");
        std::string stopSpec;
        if (auto it = stopTokens.find(mod.name + "|" + svName); it != stopTokens.end()) {
            stopSpec = it->second;
        }
        else if (auto it = stopTokens.find(mod.name + "|" + mod.outputPortCppNames.at(svName)); it != stopTokens.end()) {
            stopSpec = it->second;
        }
        if (!stopSpec.empty()) {
            std::vector<std::string> tokens;
            std::stringstream ss(stopSpec);
            std::string token;
            while (std::getline(ss, token, ',')) {
                token = trim(token);
                if (!token.empty()) {
                    tokens.push_back(token);
                }
            }
            for (size_t i = 0; i < driver->body.size(); ++i) {
                for (auto& item : tokens) {
                    if (driver->body[i].find(item) != std::string::npos) {
                        scanLimit = i;
                        break;
                    }
                }
                if (scanLimit != driver->body.size()) {
                    break;
                }
            }
        }
        size_t lastAssign = std::string::npos;
        for (size_t i = 0; i < scanLimit && i < driver->body.size(); ++i) {
            if (lineAssignsTarget(driver->body[i], storageName) ||
                lineAssignsTarget(driver->body[i], svName)) {
                lastAssign = i;
            }
        }
        if (lastAssign == std::string::npos) {
            if (driver->returnName == storageName) {
                auto storageType = outputStorageType(mod, svName, mod.outputPortCppNames.at(svName));
                out << "    " << postProcessCppLine(storageType) << " " << storageName << ";\n";
                out << "    " << postProcessCppLine(storageType) << "& __readonly_" << svName << "_comb_func()\n    {\n";
                out << "        " << driver->name << "();\n";
                out << "        return " << storageName << ";\n";
                out << "    }\n\n";
            }
            return;
        }
        int depth = 0;
        size_t emitThrough = lastAssign;
        for (size_t i = 0; i < driver->body.size(); ++i) {
            depth += braceDelta(driver->body[i]);
            if (i >= lastAssign && depth <= 0) {
                emitThrough = i;
                break;
            }
        }
        auto storageType = outputStorageType(mod, svName, mod.outputPortCppNames.at(svName));
        out << "    " << postProcessCppLine(storageType) << " " << storageName << ";\n";
        out << "    " << postProcessCppLine(storageType) << "& __readonly_" << svName << "_comb_func()\n    {\n";
        for (auto& import : mod.imports) {
            out << "        using namespace " << import << ";\n";
        }
        for (size_t i = 0; i <= emitThrough && i < driver->body.size(); ++i) {
            auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, *driver, driver->body[i])));
            repairPatchedConcatOperandWidths(emittedLine);
            emitBodyLine(out, emittedLine);
        }
        out << "        return " << storageName << ";\n";
        out << "    }\n\n";
    }

    void emitMethod(std::ofstream& out, const ModuleGen& mod, const MethodGen& m)
    {
        auto traceMethod = [&]() {
            if (const char* filter = std::getenv("HDLCPP_TRACE_METHOD")) {
                return m.name.find(filter) != std::string::npos;
            }
            return false;
        }();
        auto tracePhases = std::getenv("HDLCPP_TRACE_PHASES") != nullptr;
        auto replaceSelfCombCall = [](std::string& line, const std::string& returnName) {
            if (returnName.empty()) {
                return;
            }
            auto call = returnName + "_func()";
            auto isIdent = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            };
            for (size_t pos = 0; (pos = line.find(call, pos)) != std::string::npos;) {
                auto end = pos + call.size();
                bool leftOk = pos == 0 || !isIdent(line[pos - 1]);
                bool rightOk = end >= line.size() || !isIdent(line[end]);
                if (leftOk && rightOk) {
                    line.replace(pos, call.size(), returnName);
                    pos += returnName.size();
                }
                else {
                    pos = end;
                }
            }
        };
        std::set<std::string> methodAssignedBases;
        for (const auto& bodyLine : m.body) {
            auto base = hdlcpp::assignmentBase(bodyLine);
            if (!base.empty()) {
                methodAssignedBases.insert(base);
                methodAssignedBases.insert(combStorageName(mod, base));
                if (mod.outputPortCppNames.count(base)) {
                    methodAssignedBases.insert(outputStorageName(mod, base));
                }
            }
        }
        for (const auto& localName : m.localNames) {
            methodAssignedBases.insert(localName);
        }
        std::map<std::string, std::string> methodLocalTypes;
        for (const auto& bodyLine : m.body) {
            auto local = hdlcpp::declarationName(bodyLine);
            if (local.empty()) {
                continue;
            }
            auto type = hdlcpp::declarationType(bodyLine);
            if (!type.empty()) {
                methodLocalTypes[local] = type;
            }
        }
        auto rewritePrimitiveLocalBitSelects = [&](std::string& line) {
            auto assignPos = hdlcpp::topLevelAssignPos(line);
            for (const auto& item : methodLocalTypes) {
                const auto& name = item.first;
                auto type = trim(unwrapRegType(item.second));
                if (!(type == "bool" || type == "unsigned" || type == "u32" ||
                      type == "uint32_t" || type == "u64" || type == "uint64_t")) {
                    continue;
                }
                auto pattern = name + "[(unsigned)";
                for (size_t pos = 0; (pos = line.find(pattern, pos)) != std::string::npos;) {
                    if (assignPos != std::string::npos && pos < assignPos) {
                        pos += pattern.size();
                        continue;
                    }
                    auto idxStart = pos + pattern.size();
                    int paren = 0;
                    size_t end = idxStart;
                    for (; end < line.size(); ++end) {
                        auto ch = line[end];
                        if (ch == '(') {
                            ++paren;
                        }
                        else if (ch == ')') {
                            --paren;
                        }
                        else if (ch == ']' && paren == 0) {
                            break;
                        }
                    }
                    if (end >= line.size() || line[end] != ']') {
                        pos += pattern.size();
                        continue;
                    }
                    auto index = trim(line.substr(idxStart, end - idxStart));
                    auto replacement = "logic<1>((((uint64_t)(" + name + ")) >> (unsigned)(" + index + ")) & 1ull)";
                    line.replace(pos, end - pos + 1, replacement);
                    pos += replacement.size();
                }
            }
        };
        auto replaceSameMethodCombReads = [&](std::string& line) {
            if (line.find("_func()") == std::string::npos) {
                return;
            }
            auto isIdent = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            };
            for (const auto& base : methodAssignedBases) {
                auto call = base + "_func()";
                for (size_t pos = 0; (pos = line.find(call, pos)) != std::string::npos;) {
                    auto end = pos + call.size();
                    auto leftOk = pos == 0 || !isIdent(line[pos - 1]);
                    auto rightOk = end >= line.size() || !isIdent(line[end]);
                    if (leftOk && rightOk) {
                        line.replace(pos, call.size(), base);
                        pos += base.size();
                    }
                    else {
                        pos = end;
                    }
                }
            }
        };
        auto forceExternalCombStorageReads = [&](std::string& line, bool rhsOnly) {
            auto rewriteText = [&](std::string text) {
                for (const auto& sourceMethod : mod.methods) {
                    if (sourceMethod.returnName.empty() ||
                        sourceMethod.name.empty() ||
                        sourceMethod.name == m.name ||
                        sourceMethod.name.find("_comb_func") == std::string::npos ||
                        sourceMethod.returnName == m.returnName ||
                        methodAssignedBases.count(sourceMethod.returnName) ||
                        methodAssignedBases.count(sourceMethod.returnBase)) {
                        continue;
                    }
                    replaceIdentifierAll(text, sourceMethod.returnName, sourceMethod.name + "()");
                }
                return text;
            };
            auto eq = hdlcpp::topLevelAssignPos(line);
            if (rhsOnly && eq == std::string::npos) {
                return;
            }
            if (eq == std::string::npos) {
                line = rewriteText(line);
                return;
            }
            line = line.substr(0, eq + 1) + rewriteText(line.substr(eq + 1));
        };
        auto findMatchingBracket = [](const std::string& text, size_t open) -> size_t {
            if (open >= text.size() || text[open] != '[') {
                return std::string::npos;
            }
            int depth = 0;
            for (size_t i = open; i < text.size(); ++i) {
                if (text[i] == '[') {
                    ++depth;
                }
                else if (text[i] == ']') {
                    --depth;
                    if (depth == 0) {
                        return i;
                    }
                }
            }
            return std::string::npos;
        };
        auto fieldReadType = [&](const MethodGen& sourceMethod, const std::string& fieldPath) -> std::string {
            if (sourceMethod.returnName.empty()) {
                return "";
            }
            auto typeIt = mod.combReturnTypes.find(sourceMethod.returnName);
            if (typeIt == mod.combReturnTypes.end()) {
                return "";
            }
            auto parentType = unwrappedValueType(typeIt->second);
            std::string type = parentType;
            std::stringstream fields(fieldPath);
            std::string field;
            while (std::getline(fields, field, '.')) {
                field = trim(field);
                if (field.empty()) {
                    return "";
                }
                type = fieldTypeFor(type, field);
                if (type.empty()) {
                    return "";
                }
            }
            return type.empty() ? std::string() : type;
        };
        auto makeExternalCombFieldRead = [&](const MethodGen& sourceMethod,
                                             const std::string& field,
                                             const std::string& indexExpr) -> std::string {
            auto type = fieldReadType(sourceMethod, field);
            if (sourceMethod.returnBase.empty()) {
                return "";
            }
            if (type.empty()) {
                auto sample = "std::as_const(" + sourceMethod.returnName + ")";
                if (!indexExpr.empty()) {
                    sample += "[0]";
                }
                sample += "." + field;
                type = "std::remove_cvref_t<decltype(" + sample + ")>";
            }
            const auto fieldValue = std::string("__hdlcpp_field_value");
            const auto fieldIndex = std::string("__hdlcpp_field_index");
            auto sourceLines = sourceMethod.body;
            for (auto& sourceLine : sourceLines) {
                hdlcpp::rewriteLhsBase(sourceLine, sourceMethod.returnName, sourceMethod.returnBase);
            }
            auto lines = hdlcpp::extractTargetFieldCombLines(sourceLines, sourceMethod.returnBase,
                                                             field, fieldValue,
                                                             indexExpr.empty() ? std::string() : fieldIndex);
            if (lines.empty()) {
                return "";
            }
            std::string expr = "([&]() { ";
            for (auto& import : mod.imports) {
                expr += "using namespace " + import + "; ";
            }
            if (!indexExpr.empty()) {
                expr += "const uint64_t " + fieldIndex + " = (uint64_t)(" + indexExpr + "); ";
            }
            expr += postProcessCppLine(type) + " " + fieldValue + " = {}; ";
            for (auto line : lines) {
                auto bound = lateBindCombRhs(mod, sourceMethod, line);
                auto emitted = repairMalformedEquality(postProcessCppLine(std::move(bound)));
                repairPatchedConcatOperandWidths(emitted);
                expr += emitted + " ";
            }
            expr += "return " + fieldValue + "; }())";
            return expr;
        };
        auto replaceExternalCombFieldReads = [&](std::string& line) {
            auto isIdent = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            };
            auto rewriteText = [&](std::string text) {
                if (text.find("_comb") == std::string::npos) {
                    return text;
                }
                for (const auto& sourceMethod : mod.methods) {
                    if (sourceMethod.returnName.empty() ||
                        sourceMethod.returnBase.empty() ||
                        sourceMethod.name.empty() ||
                        sourceMethod.name == m.name ||
                        sourceMethod.name.find("_comb_func") == std::string::npos ||
                        sourceMethod.returnName == m.returnName ||
                        methodAssignedBases.count(sourceMethod.returnName) ||
                        methodAssignedBases.count(sourceMethod.returnBase)) {
                        continue;
                    }
                    const auto& token = sourceMethod.returnName;
                    if (text.find(token) == std::string::npos) {
                        continue;
                    }
                    for (size_t pos = 0; (pos = text.find(token, pos)) != std::string::npos;) {
                        auto end = pos + token.size();
                        bool leftOk = pos == 0 || !isIdent(text[pos - 1]);
                        bool rightOk = end >= text.size() || !isIdent(text[end]);
                        if (!leftOk || !rightOk) {
                            pos = end;
                            continue;
                        }
                        size_t after = end;
                        std::string indexExpr;
                        if (after < text.size() && text[after] == '[') {
                            auto close = findMatchingBracket(text, after);
                            if (close == std::string::npos) {
                                pos = end;
                                continue;
                            }
                            indexExpr = trim(text.substr(after + 1, close - after - 1));
                            after = close + 1;
                        }
                        if (after >= text.size() || text[after] != '.') {
                            pos = end;
                            continue;
                        }
                        ++after;
                        auto fieldStart = after;
                        if (after >= text.size() ||
                            (!std::isalpha(static_cast<unsigned char>(text[after])) && text[after] != '_')) {
                            pos = end;
                            continue;
                        }
                        ++after;
                        while (after < text.size() && isIdent(text[after])) {
                            ++after;
                        }
                            std::string field = text.substr(fieldStart, after - fieldStart);
                            while (after < text.size() && text[after] == '.') {
                                auto nextStart = after + 1;
                                if (nextStart >= text.size() ||
                                    (!std::isalpha(static_cast<unsigned char>(text[nextStart])) && text[nextStart] != '_')) {
                                    break;
                                }
                                auto nextEnd = nextStart + 1;
                                while (nextEnd < text.size() && isIdent(text[nextEnd])) {
                                    ++nextEnd;
                                }
                                if (nextEnd < text.size() && text[nextEnd] == '(') {
                                    break;
                                }
                                field += text.substr(after, nextEnd - after);
                                after = nextEnd;
                            }
                            auto replacement = makeExternalCombFieldRead(sourceMethod, field, indexExpr);
                        if (replacement.empty()) {
                            pos = after;
                            continue;
                        }
                        text.replace(pos, after - pos, replacement);
                        pos += replacement.size();
                    }
                }
                return text;
            };
            auto eq = hdlcpp::topLevelAssignPos(line);
            if (eq == std::string::npos) {
                line = rewriteText(line);
                return;
            }
            auto lhs = line.substr(0, eq + 1);
            auto rhs = line.substr(eq + 1);
            line = lhs + rewriteText(rhs);
        };
            if (m.name.find("_comb_func") != std::string::npos && !m.returnName.empty()) {
            auto typeIt = mod.combReturnTypes.find(m.returnName);
            auto type = typeIt != mod.combReturnTypes.end() ? typeIt->second : std::string("auto");
            auto noCacheComb = noCacheCombMethod(mod, m);
		    if (noCacheComb) {
                const bool readonlyCombOutput = !m.returnBase.empty() &&
                    mod.outputPortCppNames.count(m.returnBase) &&
                    (configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", mod.name + "|" + m.returnBase) ||
                     configuredNameEquals("HDLCPP_READONLY_COMB_OUTPUTS", mod.name + "|" + mod.outputPortCppNames.at(m.returnBase)));
                if (!readonlyCombOutput) {
                    out << "    " << postProcessCppLine(type) << " " << m.returnName << ";\n";
                }
		        out << "    " << type << "& " << m.name << "()\n    {\n";
		    }
	            else {
	                out << "    _LAZY_COMB(" << m.returnName << ", " << type << ")\n";
	            }
            for (auto& import : mod.imports) {
                out << "        using namespace " << import << ";\n";
            }
            if (!m.returnBase.empty() &&
                mod.outputPortCppNames.count(m.returnBase) &&
                mod.seqAssignedVars.count(m.returnBase) &&
                m.returnName == combStorageName(mod, m.returnBase)) {
                auto storageName = outputStorageName(mod, m.returnBase);
                if (storageName != m.returnName) {
                    out << "        " << m.returnName << " = " << storageName << ";\n";
                }
            }
            auto methodStart = std::chrono::steady_clock::now();
            long long lateBindNs = 0;
            long long postProcessNs = 0;
            auto normalizeReturnDefaultBranches = [&](std::string& line) {
                if (m.returnName.empty() || type.empty() || type == "auto") {
                    return;
                }
                auto eq = hdlcpp::topLevelAssignPos(line);
                if (eq == std::string::npos || trim(line.substr(0, eq)) != m.returnName) {
                    return;
                }
                auto rhs = trim(line.substr(eq + 1));
                const bool hadSemi = !rhs.empty() && rhs.back() == ';';
                if (hadSemi) {
                    rhs.pop_back();
                    rhs = trim(rhs);
                }
                if (rhs.find("([&]") != std::string::npos ||
                    rhs.find("[&]()") != std::string::npos ||
                    rhs.find("__hdlcpp_field_value") != std::string::npos) {
                    return;
                }
                auto isDefaultBranch = [&](std::string branch) {
                    branch = trim(std::move(branch));
                    while (branch.size() >= 2 && branch.front() == '(' && branch.back() == ')') {
                        auto close = matchingParenClose(branch, 0);
                        if (close != branch.size() - 1) {
                            break;
                        }
                        branch = trim(branch.substr(1, branch.size() - 2));
                    }
                    return branch == "0" || branch == "0u" || branch == "0ull" ||
                           branch == "0b0" || branch == "{}" ||
                           branch == "logic<1>(0)" || branch == "logic<1>(0b0)" ||
                           branch == "logic<1>(false)" || branch == "false";
                };
                std::function<std::string(std::string)> normalizeExpr = [&](std::string expr) -> std::string {
                    expr = trim(std::move(expr));
                    if (isDefaultBranch(expr)) {
                        return type + "{}";
                    }
                    std::string stripped = expr;
                    while (stripped.size() >= 2 && stripped.front() == '(' && stripped.back() == ')') {
                        auto close = matchingParenClose(stripped, 0);
                        if (close != stripped.size() - 1) {
                            break;
                        }
                        stripped = trim(stripped.substr(1, stripped.size() - 2));
                    }
                    int paren = 0;
                    int angle = 0;
                    size_t question = std::string::npos;
                    size_t colon = std::string::npos;
                    for (size_t pos = 0; pos < stripped.size(); ++pos) {
                        if (stripped[pos] == '(') ++paren;
                        else if (stripped[pos] == ')' && paren > 0) --paren;
                        else if (stripped[pos] == '<' && paren == 0) ++angle;
                        else if (stripped[pos] == '>' && paren == 0 && angle > 0) --angle;
                        else if (stripped[pos] == '?' && paren == 0 && angle == 0) {
                            question = pos;
                            break;
                        }
                    }
                    if (question == std::string::npos) {
                        return expr;
                    }
                    paren = 0;
                    angle = 0;
                    for (size_t pos = question + 1; pos < stripped.size(); ++pos) {
                        if (stripped[pos] == '(') ++paren;
                        else if (stripped[pos] == ')' && paren > 0) --paren;
                        else if (stripped[pos] == '<' && paren == 0) ++angle;
                        else if (stripped[pos] == '>' && paren == 0 && angle > 0) --angle;
                        else if (stripped[pos] == ':' && paren == 0 && angle == 0) {
                            if ((pos > 0 && stripped[pos - 1] == ':') ||
                                (pos + 1 < stripped.size() && stripped[pos + 1] == ':')) {
                                continue;
                            }
                            colon = pos;
                            break;
                        }
                    }
                    if (colon == std::string::npos) {
                        return expr;
                    }
                    auto cond = trim(stripped.substr(0, question));
                    auto lhs = normalizeExpr(stripped.substr(question + 1, colon - question - 1));
                    auto rhsBranch = normalizeExpr(stripped.substr(colon + 1));
                    return "(" + cond + " ? " + lhs + " : " + rhsBranch + ")";
                };
                line = m.returnName + " = " + normalizeExpr(rhs) + (hadSemi ? ";" : "");
            };
            for (size_t bodyIndex = 0; bodyIndex < m.body.size(); ++bodyIndex) {
                auto& l = m.body[bodyIndex];
                if (tracePhases && m.body.size() >= 100 && bodyIndex != 0 && bodyIndex % 50 == 0) {
                    std::cerr << "HDLCPP_PHASE comb_method_progress " << mod.name << "."
                              << m.name << " line=" << bodyIndex << "/" << m.body.size() << "\n";
                }
                if (traceMethod) {
                    std::cerr << "HDLCPP_METHOD_LINE begin " << m.name << ": " << l << "\n";
                }
	                auto lateStart = std::chrono::steady_clock::now();
	                auto boundLine = lateBindCombRhs(mod, m, l);
	                replaceExternalCombFieldReads(boundLine);
	                forceExternalCombStorageReads(boundLine, false);
	                auto lateEnd = std::chrono::steady_clock::now();
                auto postStart = lateEnd;
                auto emittedLine = repairMalformedEquality(postProcessCppLine(std::move(boundLine)));
                repairPatchedConcatOperandWidths(emittedLine);
                auto postEnd = std::chrono::steady_clock::now();
                if (tracePhases && m.body.size() >= 100) {
                    lateBindNs += std::chrono::duration_cast<std::chrono::nanoseconds>(lateEnd - lateStart).count();
                    postProcessNs += std::chrono::duration_cast<std::chrono::nanoseconds>(postEnd - postStart).count();
                }
                if (traceMethod) {
                    std::cerr << "HDLCPP_METHOD_LINE after_latebind " << m.name << ": " << emittedLine << "\n";
                }
                if (!m.returnBase.empty()) {
                    auto assigned = hdlcpp::assignmentBase(emittedLine);
                    if (!assigned.empty() && assigned != m.returnBase &&
                        assigned != m.returnName &&
                        !(mod.outputPortCppNames.count(m.returnBase) &&
                          assigned == outputStorageName(mod, m.returnBase))) {
                        bool assignsKnownCombBase = false;
                        for (const auto& item : mod.combAssignedVars) {
                            if (assigned == item || assigned == combStorageName(mod, item) ||
                                (mod.outputPortCppNames.count(item) && assigned == outputStorageName(mod, item))) {
                                assignsKnownCombBase = true;
                                break;
                            }
                        }
                        if (assignsKnownCombBase) {
                            continue;
                        }
                    }
                }
                if (!m.returnName.empty()) {
                    replaceSelfCombCall(emittedLine, m.returnName);
                }
                replaceSameMethodCombReads(emittedLine);
                normalizeReturnDefaultBranches(emittedLine);
                auto normalizeWholeArrayAssignment = [&]() {
                    auto eq = hdlcpp::topLevelAssignPos(emittedLine);
                    if (eq == std::string::npos) {
                        return;
                    }
                    auto lhs = trim(emittedLine.substr(0, eq));
                    if (lhs.find('[') != std::string::npos || lhs.find('.') != std::string::npos ||
                        lhs.find(".bits(") != std::string::npos || lhs.find(".get(") != std::string::npos) {
                        return;
                    }
                    auto assigned = hdlcpp::assignmentBase(emittedLine);
                    std::string targetType;
                    if (!assigned.empty()) {
                        if (assigned == m.returnName || assigned == m.returnBase ||
                            (!m.returnBase.empty() && assigned == combStorageName(mod, m.returnBase)) ||
                            (!m.returnBase.empty() && mod.outputPortCppNames.count(m.returnBase) &&
                             assigned == outputStorageName(mod, m.returnBase))) {
                            if (auto typeIt = mod.combReturnTypes.find(m.returnName);
                                typeIt != mod.combReturnTypes.end()) {
                                targetType = typeIt->second;
                            }
                            else {
                                targetType = trim(m.ret);
                                while (!targetType.empty() && targetType.back() == '&') {
                                    targetType.pop_back();
                                    targetType = trim(targetType);
                                }
                            }
                        }
                        else if (auto typeIt = mod.types.find(assigned); typeIt != mod.types.end()) {
                            targetType = unwrapRegType(typeIt->second);
                        }
                        else if (auto outIt = mod.outputPortCppNames.find(assigned);
                                 outIt != mod.outputPortCppNames.end()) {
                            targetType = outputStorageType(mod, assigned, outIt->second);
                        }
                    }
                    auto shapeType = targetType;
                    auto aliases = localUsingTypeAliases(mod);
                    for (size_t guard = 0; guard < 32; ++guard) {
                        auto it = aliases.find(shapeType);
                        if (it == aliases.end() || it->second == shapeType) {
                            break;
                        }
                        shapeType = trim(it->second);
                    }
                    auto shapeArgs = templateArgsFor(shapeType, shapeType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                    const bool targetIsPackedArray = shapeArgs.size() >= 3 && trim(shapeArgs[2]) == "true";
                    const bool targetIsUnpackedArray = shapeArgs.size() >= 2 && !targetIsPackedArray;
                    auto canonicalArrayBound = [](std::string text) {
                        text = trim(std::move(text));
                        replaceAll(text, " ", "");
                        replaceAll(text, "\t", "");
                        replaceAll(text, "(uint64_t)", "");
                        replaceAll(text, "(unsigned)", "");
                        replaceAll(text, "(size_t)", "");
                        replaceAll(text, "&((1ull<<32)-1ull)", "");
                        replaceAll(text, "&((1ull<<64)-1ull)", "");
                        while (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
                            auto close = matchingParenClose(text, 0);
                            if (close != text.size() - 1) {
                                break;
                            }
                            text = trim(text.substr(1, text.size() - 2));
                            replaceAll(text, " ", "");
                            replaceAll(text, "\t", "");
                        }
                        return text;
                    };
                    auto sameArrayValueShape = [&](std::string target, std::string source) {
                        auto aliases = localUsingTypeAliases(mod);
                        for (auto* type : {&target, &source}) {
                            *type = trim(*type);
                            for (size_t guard = 0; guard < 32; ++guard) {
                                auto it = aliases.find(*type);
                                if (it == aliases.end() || it->second == *type) {
                                    break;
                                }
                                *type = trim(it->second);
                            }
                        }
                        auto targetArgs = templateArgsFor(target, target.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                        auto sourceArgs = templateArgsFor(source, source.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                        if (targetArgs.size() < 2 || sourceArgs.size() < 2) {
                            return false;
                        }
                        auto targetPacked = targetArgs.size() >= 3 ? trim(targetArgs[2]) : "false";
                        auto sourcePacked = sourceArgs.size() >= 3 ? trim(sourceArgs[2]) : "false";
                        return trim(targetArgs[0]) == trim(sourceArgs[0]) &&
                            canonicalArrayBound(targetArgs[1]) == canonicalArrayBound(sourceArgs[1]) &&
                            targetPacked == sourcePacked;
                    };
                    auto primitiveTarget = [&](const std::string& type) {
                        auto t = trim(type);
                        return t.empty() || t == "bool" || t == "u1" || t == "unsigned" ||
                            t == "uint32_t" || t == "uint64_t" || t.rfind("logic<", 0) == 0 ||
                            t.rfind("u<", 0) == 0 || t.rfind("std::remove_cvref_t<decltype(", 0) == 0;
                    };
                    const bool targetNeedsGenericUnpack = !targetIsPackedArray && !targetIsUnpackedArray &&
                        !primitiveTarget(shapeType);
                    if (targetType.empty() &&
                        (!targetIsPackedArray && !targetIsUnpackedArray && !targetNeedsGenericUnpack)) {
                        return;
                    }
                    if (!targetIsPackedArray && !targetIsUnpackedArray && !targetNeedsGenericUnpack) {
                        return;
                    }
                    auto rhs = trim(emittedLine.substr(eq + 1));
                    bool hasSemicolon = false;
                    if (!rhs.empty() && rhs.back() == ';') {
                        hasSemicolon = true;
                        rhs.pop_back();
                        rhs = trim(rhs);
                    }
                    auto simpleGetterTypeFor = [&](std::string value) -> std::string {
                        value = trim(std::move(value));
                        while (!value.empty() && value.front() == '(' && value.back() == ')') {
                            auto close = matchingParenClose(value, 0);
                            if (close != value.size() - 1) {
                                break;
                            }
                            value = trim(value.substr(1, value.size() - 2));
                        }
                        if (value.size() < 3 || value.substr(value.size() - 2) != "()") {
                            return "";
                        }
                        value = value.substr(0, value.size() - 2);
                        if (value.find('[') != std::string::npos) {
                            return "";
                        }
                        if (value.find('.') == std::string::npos) {
                            for (const auto& port : mod.ports) {
                                auto cppIt = mod.portCppNames.find(port.name);
                                if (value == port.name ||
                                    (cppIt != mod.portCppNames.end() && value == cppIt->second)) {
                                    return port.type;
                                }
                            }
                            if (auto it = mod.combReturnTypes.find(value); it != mod.combReturnTypes.end()) {
                                return it->second;
                            }
                            static const std::string suffix = "_func";
                            if (value.size() > suffix.size() && value.substr(value.size() - suffix.size()) == suffix) {
                                auto storage = value.substr(0, value.size() - suffix.size());
                                if (auto it = mod.combReturnTypes.find(storage); it != mod.combReturnTypes.end()) {
                                    return it->second;
                                }
                            }
                            for (const auto& method : mod.methods) {
                                if (method.name == value && !method.returnName.empty()) {
                                    if (auto it = mod.combReturnTypes.find(method.returnName); it != mod.combReturnTypes.end()) {
                                        return it->second;
                                    }
                                }
                            }
                            return "";
                        }
                        auto dot = value.find('.');
                        if (dot == std::string::npos || value.find('.', dot + 1) != std::string::npos) {
                            return "";
                        }
                        auto instance = trim(value.substr(0, dot));
                        auto getter = trim(value.substr(dot + 1));
                        std::string childType;
                        for (size_t i = 0; i < mod.memberNames.size() && i < mod.memberTypes.size(); ++i) {
                            if (mod.memberNames[i] == instance) {
                                childType = mod.memberTypes[i];
                                break;
                            }
                        }
                        if (childType.empty()) {
                            return "";
                        }
                        auto childElementType = arrayElementTypeText(childType);
                        auto childBaseType = trim(childElementType);
                        std::string childParams;
                        if (auto lt = childBaseType.find('<'); lt != std::string::npos) {
                            int angleDepth = 0;
                            size_t gt = std::string::npos;
                            for (size_t pos = lt; pos < childElementType.size(); ++pos) {
                                if (childElementType[pos] == '<') {
                                    ++angleDepth;
                                }
                                else if (childElementType[pos] == '>' && --angleDepth == 0) {
                                    gt = pos;
                                    break;
                                }
                            }
                            if (gt != std::string::npos && gt > lt) {
                                childParams = childElementType.substr(lt + 1, gt - lt - 1);
                            }
                            childBaseType = trim(childBaseType.substr(0, lt));
                        }
                        auto* child = findModule(childElementType);
                        if (!child && !childBaseType.empty()) {
                            child = findModule(childBaseType);
                        }
                        if (!child) {
                            return "";
                        }
                        for (const auto& port : child->ports) {
                            auto cppIt = child->portCppNames.find(port.name);
                            if (getter == port.name ||
                                (cppIt != child->portCppNames.end() && getter == cppIt->second)) {
                                auto resolvedPortType = port.type;
                                if (!childParams.empty()) {
                                    resolvedPortType = substituteParamDeclValues(child->params,
                                                                                 splitTopLevelArgs(childParams),
                                                                                 resolvedPortType);
                                }
                                return resolvedPortType;
                            }
                        }
                        return "";
                    };
                    auto unpackTemplateCall = [](const std::string& text, const std::string& prefix) -> std::pair<std::string, std::string> {
                        if (text.rfind(prefix, 0) != 0) {
                            return {};
                        }
                        size_t pos = prefix.size();
                        int angleDepth = 1;
                        size_t typeEnd = pos;
                        for (; typeEnd < text.size(); ++typeEnd) {
                            if (text[typeEnd] == '<' && typeEnd + 1 < text.size() && text[typeEnd + 1] == '<') {
                                ++typeEnd;
                            }
                            else if (text[typeEnd] == '<') {
                                ++angleDepth;
                            }
                            else if (text[typeEnd] == '>' && --angleDepth == 0) {
                                break;
                            }
                        }
                        if (typeEnd >= text.size() || typeEnd + 1 >= text.size() || text[typeEnd + 1] != '(') {
                            return {};
                        }
                        auto target = trim(text.substr(pos, typeEnd - pos));
                        size_t argStart = typeEnd + 2;
                        int parenDepth = 1;
                        size_t argEnd = argStart;
                        for (; argEnd < text.size(); ++argEnd) {
                            if (text[argEnd] == '(') {
                                ++parenDepth;
                            }
                            else if (text[argEnd] == ')' && --parenDepth == 0) {
                                break;
                            }
                        }
                        if (argEnd >= text.size() || trim(text.substr(argEnd + 1)) != "") {
                            return {};
                        }
                        return {target, trim(text.substr(argStart, argEnd - argStart))};
                    };
                    if (targetIsPackedArray && rhs.rfind("cpphdl::pack_value<", 0) == 0) {
                        auto packed = unpackTemplateCall(rhs, "cpphdl::pack_value<");
                        auto sourceType = simpleGetterTypeFor(packed.second);
                        if (!sourceType.empty() && sameArrayValueShape(targetType, sourceType)) {
                            emittedLine = emittedLine.substr(0, eq + 1) + " " + packed.second +
                                (hasSemicolon ? ";" : "");
                            return;
                        }
                        if (!packed.second.empty()) {
                            if (packed.second == "0" || packed.second == "0b0" || packed.second == "0x0" ||
                                packed.second == "1" || packed.second == "0b1" || packed.second == "0x1") {
                                emittedLine = emittedLine.substr(0, eq + 1) + " cpphdl::pack_value<cpphdl::type_width<" +
                                    targetType + ">()>(" + packed.second + ")" + (hasSemicolon ? ";" : "");
                                return;
                            }
                            emittedLine = emittedLine.substr(0, eq + 1) + " ([&]() -> " + targetType +
                                " { auto&& __cpphdl_src = (" + packed.second +
                                "); using __cpphdl_target_t = " + targetType +
                                "; auto __cpphdl_assign = [&]<typename __cpphdl_src_arg_t>(__cpphdl_src_arg_t&& __cpphdl_src_val) -> __cpphdl_target_t { " +
                                "using __cpphdl_src_t = std::remove_cvref_t<__cpphdl_src_arg_t>; __cpphdl_target_t __cpphdl_out{}; " +
                                "if constexpr (requires { __cpphdl_target_t::PACKED; __cpphdl_src_t::PACKED; " +
                                "__cpphdl_target_t::ELEMENT_BITS; __cpphdl_src_t::ELEMENT_BITS; " +
                                "__cpphdl_target_t::SIZE_BITS; __cpphdl_src_t::SIZE_BITS; }) { " +
                                "if constexpr ((__cpphdl_target_t::PACKED == __cpphdl_src_t::PACKED) && " +
                                "(__cpphdl_target_t::ELEMENT_BITS == __cpphdl_src_t::ELEMENT_BITS) && " +
                                "(__cpphdl_target_t::SIZE_BITS == __cpphdl_src_t::SIZE_BITS)) { " +
                                "__cpphdl_out = __cpphdl_src_val; } else { " +
                                "__cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                                "} else { __cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                                "return __cpphdl_out; }; return __cpphdl_assign(__cpphdl_src); })()" + (hasSemicolon ? ";" : "");
                            return;
                        }
                    }
                    if (rhs.empty() || rhs == "{}" || rhs.rfind("cpphdl::unpack_value<", 0) == 0 ||
                        rhs.rfind("cpphdl::sv_cast<", 0) == 0 ||
                        (targetIsPackedArray && rhs.rfind("cpphdl::pack_value<", 0) == 0)) {
                        return;
                    }
                    if (rhs == "0" || rhs == "0b0" || rhs == "0x0" ||
                        rhs == "1" || rhs == "0b1" || rhs == "0x1" ||
                        rhs.find(';') != std::string::npos || rhs.find('\n') != std::string::npos) {
                        return;
                    }
                    if (!rhs.empty() && rhs.front() == '{') {
                        return;
                    }
                    auto rhsPortGetterType = [&]() -> std::string {
                        auto call = trim(rhs);
                        while (!call.empty() && call.front() == '(' && call.back() == ')') {
                            call = trim(call.substr(1, call.size() - 2));
                        }
                        if (call.size() < 3 || call.substr(call.size() - 2) != "()") {
                            return "";
                        }
                        call = call.substr(0, call.size() - 2);
                        if (call.find('.') != std::string::npos || call.find('[') != std::string::npos) {
                            return "";
                        }
                        for (const auto& port : mod.ports) {
                            auto cppIt = mod.portCppNames.find(port.name);
                            if (call == port.name ||
                                (cppIt != mod.portCppNames.end() && call == cppIt->second)) {
                                return port.type;
                            }
                        }
                        return "";
                    }();
                    if (!rhsPortGetterType.empty() && trim(rhsPortGetterType) == trim(targetType)) {
                        return;
                    }
                    if (!rhsPortGetterType.empty() && sameArrayValueShape(targetType, rhsPortGetterType)) {
                        return;
                    }
                    auto rhsLocalCombGetterType = [&]() -> std::string {
                        auto call = trim(rhs);
                        while (!call.empty() && call.front() == '(' && call.back() == ')') {
                            call = trim(call.substr(1, call.size() - 2));
                        }
                        if (call.size() < 3 || call.substr(call.size() - 2) != "()") {
                            return "";
                        }
                        call = call.substr(0, call.size() - 2);
                        if (call.find('.') != std::string::npos || call.find('[') != std::string::npos) {
                            return "";
                        }
                        if (auto it = mod.combReturnTypes.find(call); it != mod.combReturnTypes.end()) {
                            return it->second;
                        }
                        static const std::string suffix = "_func";
                        if (call.size() > suffix.size() && call.substr(call.size() - suffix.size()) == suffix) {
                            auto storage = call.substr(0, call.size() - suffix.size());
                            if (auto it = mod.combReturnTypes.find(storage); it != mod.combReturnTypes.end()) {
                                return it->second;
                            }
                        }
                        for (const auto& method : mod.methods) {
                            if (method.name == call && !method.returnName.empty()) {
                                if (auto it = mod.combReturnTypes.find(method.returnName); it != mod.combReturnTypes.end()) {
                                    return it->second;
                                }
                            }
                        }
                        return "";
                    }();
                    if (!rhsLocalCombGetterType.empty() && sameArrayValueShape(targetType, rhsLocalCombGetterType)) {
                        return;
                    }
                    auto rhsChildGetterType = [&]() -> std::string {
                        auto call = trim(rhs);
                        while (!call.empty() && call.front() == '(' && call.back() == ')') {
                            call = trim(call.substr(1, call.size() - 2));
                        }
                        if (call.size() < 3 || call.substr(call.size() - 2) != "()") {
                            return "";
                        }
                        call = call.substr(0, call.size() - 2);
                        auto dot = call.find('.');
                        if (dot == std::string::npos || call.find('.', dot + 1) != std::string::npos ||
                            call.find('[') != std::string::npos) {
                            return "";
                        }
                        auto instance = trim(call.substr(0, dot));
                        auto getter = trim(call.substr(dot + 1));
                        std::string childType;
                        for (size_t i = 0; i < mod.memberNames.size() && i < mod.memberTypes.size(); ++i) {
                            if (mod.memberNames[i] == instance) {
                                childType = mod.memberTypes[i];
                                break;
                            }
                        }
                        if (childType.empty()) {
                            return "";
                        }
                        auto childElementType = arrayElementTypeText(childType);
                        auto childBaseType = trim(childElementType);
                        std::string childParams;
                        if (auto lt = childBaseType.find('<'); lt != std::string::npos) {
                            int angleDepth = 0;
                            size_t gt = std::string::npos;
                            for (size_t pos = lt; pos < childElementType.size(); ++pos) {
                                if (childElementType[pos] == '<') {
                                    ++angleDepth;
                                }
                                else if (childElementType[pos] == '>' && --angleDepth == 0) {
                                    gt = pos;
                                    break;
                                }
                            }
                            if (gt != std::string::npos && gt > lt) {
                                childParams = childElementType.substr(lt + 1, gt - lt - 1);
                            }
                            childBaseType = trim(childBaseType.substr(0, lt));
                        }
                        auto* child = findModule(childElementType);
                        if (!child && !childBaseType.empty()) {
                            child = findModule(childBaseType);
                        }
                        if (!child) {
                            return "";
                        }
                        for (const auto& port : child->ports) {
                            auto cppIt = child->portCppNames.find(port.name);
                            if (getter == port.name ||
                                (cppIt != child->portCppNames.end() && getter == cppIt->second)) {
                                auto resolvedPortType = port.type;
                                if (!childParams.empty()) {
                                    resolvedPortType = substituteParamDeclValues(child->params,
                                                                                 splitTopLevelArgs(childParams),
                                                                                 resolvedPortType);
                                }
                                std::set<std::string> declaredParamNames;
                                for (const auto& declared : child->params) {
                                    auto name = templateParamName(declared);
                                    if (!name.empty()) {
                                        declaredParamNames.insert(name);
                                    }
                                }
                                for (const auto& typeItem : child->types) {
                                    if (!declaredParamNames.count(typeItem.first) && !typeItem.first.empty() &&
                                        !typeItem.second.empty() && typeItem.first != typeItem.second) {
                                        replaceIdentifierAll(resolvedPortType, typeItem.first, typeItem.second);
                                    }
                                }
                                auto localAliases = localUsingTypeAliases(*child);
                                for (const auto& aliasItem : localAliases) {
                                    if (!aliasItem.first.empty() && !aliasItem.second.empty()) {
                                        replaceIdentifierAll(resolvedPortType, aliasItem.first, aliasItem.second);
                                    }
                                }
                                auto parentAliases = localUsingTypeAliases(mod);
                                for (const auto& aliasItem : parentAliases) {
                                    if (!aliasItem.first.empty() && !aliasItem.second.empty()) {
                                        replaceIdentifierAll(resolvedPortType, aliasItem.first, aliasItem.second);
                                    }
                                }
                                return resolvedPortType;
                            }
                        }
                        return "";
                    }();
                    if (!rhsChildGetterType.empty()) {
                        auto lhsTypeName = trim(rhsChildGetterType);
                        auto rhsTypeName = trim(targetType);
                        auto lhsPos = lhsTypeName.rfind("::");
                        auto rhsPos = rhsTypeName.rfind("::");
                        auto lhsTail = lhsPos == std::string::npos ? lhsTypeName : lhsTypeName.substr(lhsPos + 2);
                        auto rhsTail = rhsPos == std::string::npos ? rhsTypeName : rhsTypeName.substr(rhsPos + 2);
                        if (lhsTypeName == rhsTypeName ||
                            (lhsTail == rhsTail &&
                             ((lhsPos != std::string::npos) || (rhsPos != std::string::npos)))) {
                            return;
                        }
                        if (sameArrayValueShape(rhsTypeName, lhsTypeName)) {
                            return;
                        }
                    }
                    auto targetCtorPrefix = trim(targetType) + "{";
                    if (!targetCtorPrefix.empty() && rhs.rfind(targetCtorPrefix, 0) == 0) {
                        return;
                    }
                    if (targetIsPackedArray) {
                        emittedLine = emittedLine.substr(0, eq + 1) + " ([&]() -> " + targetType +
                            " { auto&& __cpphdl_src = (" + rhs +
                            "); using __cpphdl_target_t = " + targetType +
                            "; auto __cpphdl_assign = [&]<typename __cpphdl_src_arg_t>(__cpphdl_src_arg_t&& __cpphdl_src_val) -> __cpphdl_target_t { " +
                            "using __cpphdl_src_t = std::remove_cvref_t<__cpphdl_src_arg_t>; __cpphdl_target_t __cpphdl_out{}; " +
                            "if constexpr (requires { __cpphdl_target_t::PACKED; __cpphdl_src_t::PACKED; " +
                            "__cpphdl_target_t::ELEMENT_BITS; __cpphdl_src_t::ELEMENT_BITS; " +
                            "__cpphdl_target_t::SIZE_BITS; __cpphdl_src_t::SIZE_BITS; }) { " +
                            "if constexpr ((__cpphdl_target_t::PACKED == __cpphdl_src_t::PACKED) && " +
                            "(__cpphdl_target_t::ELEMENT_BITS == __cpphdl_src_t::ELEMENT_BITS) && " +
                            "(__cpphdl_target_t::SIZE_BITS == __cpphdl_src_t::SIZE_BITS)) { " +
                            "__cpphdl_out = __cpphdl_src_val; } else { " +
                            "__cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                            "} else { __cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                            "return __cpphdl_out; }; return __cpphdl_assign(__cpphdl_src); })()" + (hasSemicolon ? ";" : "");
                    }
                    else {
                        emittedLine = emittedLine.substr(0, eq + 1) + " ([&]() -> " + targetType +
                            " { if constexpr (std::is_assignable_v<" + targetType +
                            "&, std::remove_cvref_t<decltype((" + rhs + "))>>) { " + targetType +
                            " __cpphdl_direct{}; __cpphdl_direct = " + rhs + "; return __cpphdl_direct; " +
                            "; } else { return cpphdl::unpack_value<" + targetType +
                            ">(cpphdl::pack_value<cpphdl::type_width<" + targetType + ">()>(" + rhs +
                            ")); } })()" + (hasSemicolon ? ";" : "");
                    }
                };
                auto rewritePackedScalarAggregateAssignment = [&]() {
                    auto eq = hdlcpp::topLevelAssignPos(emittedLine);
                    if (eq == std::string::npos || m.returnName.empty()) {
                        return;
                    }
                    auto lhs = trim(emittedLine.substr(0, eq));
                    std::string targetType;
                    if (lhs == m.returnName) {
                        if (auto typeIt = mod.combReturnTypes.find(m.returnName);
                            typeIt != mod.combReturnTypes.end()) {
                            targetType = typeIt->second;
                        }
                        if (targetType.empty()) {
                            targetType = type;
                        }
                    }
                    else if (lhs.rfind(m.returnName + "[", 0) == 0) {
                        std::string arrayType;
                        if (auto typeIt = mod.combReturnTypes.find(m.returnName);
                            typeIt != mod.combReturnTypes.end()) {
                            arrayType = typeIt->second;
                        }
                        if (arrayType.empty()) {
                            arrayType = type;
                        }
                        auto args = templateArgsFor(arrayType,
                            arrayType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                        if (args.empty()) {
                            auto lt = arrayType.find('<');
                            auto gt = arrayType.rfind('>');
                            if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
                                args = splitTopLevelArgs(arrayType.substr(lt + 1, gt - lt - 1));
                            }
                        }
                        if (!args.empty()) {
                            targetType = args[0];
                        }
                        if (targetType.empty()) {
                            targetType = "std::remove_cvref_t<decltype(" + lhs + ")>";
                        }
                    }
                    targetType = unwrapRegType(trim(targetType));
                    const bool targetPacked = targetType.rfind("logic<", 0) == 0 ||
                        targetType.rfind("u<", 0) == 0 || isNumericValueType(targetType) ||
                        isPackedArrayValueType(targetType) ||
                        targetType.rfind("std::remove_cvref_t<decltype(", 0) == 0;
                    if (!targetPacked) {
                        return;
                    }
                    auto rhs = trim(emittedLine.substr(eq + 1));
                    bool hasSemicolon = false;
                    if (!rhs.empty() && rhs.back() == ';') {
                        hasSemicolon = true;
                        rhs.pop_back();
                        rhs = trim(rhs);
                    }
                    auto unpackTemplateCall = [](const std::string& text, const std::string& prefix) -> std::pair<std::string, std::string> {
                        if (text.rfind(prefix, 0) != 0) {
                            return {};
                        }
                        size_t pos = prefix.size();
                        int angleDepth = 1;
                        size_t typeEnd = pos;
                        for (; typeEnd < text.size(); ++typeEnd) {
                            if (text[typeEnd] == '<' && typeEnd + 1 < text.size() && text[typeEnd + 1] == '<') {
                                ++typeEnd;
                            }
                            else if (text[typeEnd] == '<') {
                                ++angleDepth;
                            }
                            else if (text[typeEnd] == '>' && --angleDepth == 0) {
                                break;
                            }
                        }
                        if (typeEnd >= text.size() || typeEnd + 1 >= text.size() || text[typeEnd + 1] != '(') {
                            return {};
                        }
                        auto target = trim(text.substr(pos, typeEnd - pos));
                        size_t argStart = typeEnd + 2;
                        int parenDepth = 1;
                        size_t argEnd = argStart;
                        for (; argEnd < text.size(); ++argEnd) {
                            if (text[argEnd] == '(') {
                                ++parenDepth;
                            }
                            else if (text[argEnd] == ')' && --parenDepth == 0) {
                                break;
                            }
                        }
                        if (argEnd >= text.size() || trim(text.substr(argEnd + 1)) != "") {
                            return {};
                        }
                        return {target, trim(text.substr(argStart, argEnd - argStart))};
                    };
                    auto packedArrayAssignExpr = [&](const std::string& value) {
                        if (value == "0" || value == "0b0" || value == "0x0" ||
                            value == "1" || value == "0b1" || value == "0x1") {
                            return "cpphdl::pack_value<cpphdl::type_width<" + targetType + ">()>(" + value + ")";
                        }
                        return "([&]() -> " + targetType + " { auto&& __cpphdl_src = (" + value +
                            "); using __cpphdl_target_t = " + targetType +
                            "; auto __cpphdl_assign = [&]<typename __cpphdl_src_arg_t>(__cpphdl_src_arg_t&& __cpphdl_src_val) -> __cpphdl_target_t { " +
                            "using __cpphdl_src_t = std::remove_cvref_t<__cpphdl_src_arg_t>; __cpphdl_target_t __cpphdl_out{}; " +
                            "if constexpr (requires { __cpphdl_target_t::PACKED; __cpphdl_src_t::PACKED; " +
                            "__cpphdl_target_t::ELEMENT_BITS; __cpphdl_src_t::ELEMENT_BITS; " +
                            "__cpphdl_target_t::SIZE_BITS; __cpphdl_src_t::SIZE_BITS; }) { " +
                            "if constexpr ((__cpphdl_target_t::PACKED == __cpphdl_src_t::PACKED) && " +
                            "(__cpphdl_target_t::ELEMENT_BITS == __cpphdl_src_t::ELEMENT_BITS) && " +
                            "(__cpphdl_target_t::SIZE_BITS == __cpphdl_src_t::SIZE_BITS)) { " +
                            "__cpphdl_out = __cpphdl_src_val; } else { " +
                            "__cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                            "} else { __cpphdl_out = cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(__cpphdl_src_val); } " +
                            "return __cpphdl_out; }; return __cpphdl_assign(__cpphdl_src); })()";
                    };
                    auto unpacked = unpackTemplateCall(rhs, "cpphdl::unpack_value<");
                    if (unpacked.first.empty()) {
                        unpacked = unpackTemplateCall(rhs, "unpack_value<");
                    }
                    if (unpacked.first.empty()) {
                        auto casted = unpackTemplateCall(rhs, "cpphdl::sv_cast<");
                        if (casted.first.empty()) {
                            casted = unpackTemplateCall(rhs, "sv_cast<");
                        }
                        if (casted.first.empty()) {
                            return;
                        }
                        auto resolvedCastedType = resolveAliasValueType(casted.first);
                        auto primitiveCastTarget = [](std::string t) {
                            t = trim(std::move(t));
                            return t == "bool" || t == "u1" || t == "unsigned" ||
                                t == "uint8_t" || t == "uint16_t" || t == "uint32_t" ||
                                t == "uint64_t" || t == "int" || t == "logic<1>" ||
                                t.rfind("logic<", 0) == 0 || t.rfind("u<", 0) == 0;
                        };
                        const bool castedAggregate = isAggregateValueType(casted.first) ||
                            isAggregateValueType(resolvedCastedType) ||
                            resolvedCastedType.rfind("array<", 0) == 0 ||
                            resolvedCastedType.rfind("std::array<", 0) == 0 ||
                            (!primitiveCastTarget(casted.first) && !primitiveCastTarget(resolvedCastedType));
                        if (!castedAggregate) {
                            return;
                        }
                        emittedLine = emittedLine.substr(0, eq + 1) + " " +
                            (isPackedArrayValueType(targetType)
                                 ? packedArrayAssignExpr(casted.second)
                                 : "cpphdl::pack_value<cpphdl::type_width<" + targetType + ">()>(" +
                                       casted.second + ")") +
                            (hasSemicolon ? ";" : "");
                        return;
                    }
                    auto resolvedUnpackedType = resolveAliasValueType(unpacked.first);
                    const bool unpackedAggregate = isAggregateValueType(unpacked.first) ||
                        isAggregateValueType(resolvedUnpackedType) ||
                        resolvedUnpackedType.rfind("array<", 0) == 0 ||
                        resolvedUnpackedType.rfind("std::array<", 0) == 0;
                    if (!unpackedAggregate) {
                        return;
                    }
                    auto packed = unpackTemplateCall(unpacked.second, "cpphdl::pack_value<");
                    if (packed.first.empty()) {
                        packed = unpackTemplateCall(unpacked.second, "pack_value<");
                    }
                    if (packed.first.empty()) {
                        return;
                    }
                    emittedLine = emittedLine.substr(0, eq + 1) + " " +
                        (isPackedArrayValueType(targetType)
                             ? packedArrayAssignExpr(packed.second)
                             : "cpphdl::pack_value<cpphdl::type_width<" + targetType + ">()>(" +
                                   packed.second + ")") +
                        (hasSemicolon ? ";" : "");
                };
                auto rewriteAggregateSvCastAssignmentFallback = [&]() {
                    auto eq = hdlcpp::topLevelAssignPos(emittedLine);
                    if (eq == std::string::npos) {
                        return;
                    }
                    auto lhs = trim(emittedLine.substr(0, eq));
                    auto rhs = trim(emittedLine.substr(eq + 1));
                    bool hasSemicolon = false;
                    if (!rhs.empty() && rhs.back() == ';') {
                        hasSemicolon = true;
                        rhs.pop_back();
                        rhs = trim(rhs);
                    }
                    auto unpackTemplateCall = [](const std::string& text, const std::string& prefix) -> std::pair<std::string, std::string> {
                        if (text.rfind(prefix, 0) != 0) {
                            return {};
                        }
                        size_t pos = prefix.size();
                        int angleDepth = 1;
                        size_t typeEnd = pos;
                        for (; typeEnd < text.size(); ++typeEnd) {
                            if (text[typeEnd] == '<' && typeEnd + 1 < text.size() && text[typeEnd + 1] == '<') {
                                ++typeEnd;
                            }
                            else if (text[typeEnd] == '<') {
                                ++angleDepth;
                            }
                            else if (text[typeEnd] == '>' && --angleDepth == 0) {
                                break;
                            }
                        }
                        if (typeEnd >= text.size() || typeEnd + 1 >= text.size() || text[typeEnd + 1] != '(') {
                            return {};
                        }
                        auto target = trim(text.substr(pos, typeEnd - pos));
                        size_t argStart = typeEnd + 2;
                        int parenDepth = 1;
                        size_t argEnd = argStart;
                        for (; argEnd < text.size(); ++argEnd) {
                            if (text[argEnd] == '(') {
                                ++parenDepth;
                            }
                            else if (text[argEnd] == ')' && --parenDepth == 0) {
                                break;
                            }
                        }
                        if (argEnd >= text.size() || trim(text.substr(argEnd + 1)) != "") {
                            return {};
                        }
                        return {target, trim(text.substr(argStart, argEnd - argStart))};
                    };
                    auto casted = unpackTemplateCall(rhs, "cpphdl::sv_cast<");
                    if (casted.first.empty()) {
                        casted = unpackTemplateCall(rhs, "sv_cast<");
                    }
                    if (casted.first.empty()) {
                        return;
                    }
                    auto resolvedCastedType = resolveAliasValueType(casted.first);
                    auto primitiveCastTarget = [](std::string t) {
                        t = trim(std::move(t));
                        return t == "bool" || t == "u1" || t == "unsigned" ||
                            t == "uint8_t" || t == "uint16_t" || t == "uint32_t" ||
                            t == "uint64_t" || t == "int" || t == "logic<1>" ||
                            t.rfind("logic<", 0) == 0 || t.rfind("u<", 0) == 0;
                    };
                    const bool castedAggregate = isAggregateValueType(casted.first) ||
                        isAggregateValueType(resolvedCastedType) ||
                        resolvedCastedType.rfind("array<", 0) == 0 ||
                        resolvedCastedType.rfind("std::array<", 0) == 0 ||
                        (!primitiveCastTarget(casted.first) && !primitiveCastTarget(resolvedCastedType));
                    if (!castedAggregate) {
                        return;
                    }
                    auto lvalueValueType = [&](const std::string& lvalue) {
                        if (auto bitsPos = lvalue.rfind(".bits("); bitsPos != std::string::npos) {
                            auto open = bitsPos + std::string(".bits").size();
                            auto close = matchingParenClose(lvalue, open);
                            if (close != std::string::npos && close + 1 == lvalue.size()) {
                                auto args = splitTopLevelArgs(lvalue.substr(open + 1, close - open - 1));
                                if (args.size() == 2) {
                                    return "logic<((uint64_t)(" + args[0] + "))-((uint64_t)(" + args[1] + "))+1>";
                                }
                            }
                        }
                        return "std::remove_cvref_t<decltype(" + lvalue + ")>";
                    };
                    auto targetType = lvalueValueType(lhs);
                    emittedLine = emittedLine.substr(0, eq + 1) +
                        " ([&]() -> " + targetType + " { using __cpphdl_target_t = " + targetType +
                        "; using __cpphdl_cast_t = " + casted.first +
                        "; if constexpr (std::is_assignable_v<__cpphdl_target_t&, __cpphdl_cast_t>) { " +
                        "__cpphdl_target_t __cpphdl_value{}; __cpphdl_value = cpphdl::sv_cast<__cpphdl_cast_t>(" +
                        casted.second + "); return __cpphdl_value; } else { return cpphdl::pack_value<cpphdl::type_width<__cpphdl_target_t>()>(" +
                        casted.second + "); } })()" + (hasSemicolon ? ";" : "");
                };
                normalizeWholeArrayAssignment();
                rewritePackedScalarAggregateAssignment();
                rewriteAggregateSvCastAssignmentFallback();
                emittedLine = repairProxyValueInitAssignment(std::move(emittedLine));
                if (traceMethod) {
                    std::cerr << "HDLCPP_METHOD_LINE after_external " << m.name << ": " << emittedLine << "\n";
                }
                rewritePrimitiveLocalBitSelects(emittedLine);
                if (traceMethod) {
                    std::cerr << "HDLCPP_METHOD_LINE emit " << m.name << ": " << emittedLine << "\n";
                }
                emitBodyLine(out, emittedLine, true);
            }
            if (tracePhases && m.body.size() >= 100) {
                auto methodEnd = std::chrono::steady_clock::now();
                auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(methodEnd - methodStart).count();
                std::cerr << "HDLCPP_PHASE comb_method_timing " << mod.name << "." << m.name
                          << " total_ms=" << totalMs
                          << " late_ms=" << (lateBindNs / 1000000)
                          << " post_ms=" << (postProcessNs / 1000000) << "\n";
            }
            if (auto injections = configuredTextMap("HDLCPP_COMB_RETURN_INJECTIONS"); injections.count(mod.name + "|" + m.returnName)) {
                std::stringstream ss(injections[mod.name + "|" + m.returnName]);
                std::string injectionLine;
                while (std::getline(ss, injectionLine)) {
                    out << "        " << injectionLine << "\n";
                }
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
        if (!mod.isPackage && m.name.find("_comb_func") != std::string::npos && m.returnName.empty()) {
            std::string inferredReturn;
            for (auto it = m.body.rbegin(); it != m.body.rend(); ++it) {
                auto line = trim(*it);
                if (line.rfind("return ", 0) != 0 || line.size() <= 7) {
                    continue;
                }
                inferredReturn = trim(line.substr(7));
                if (!inferredReturn.empty() && inferredReturn.back() == ';') {
                    inferredReturn.pop_back();
                    inferredReturn = trim(inferredReturn);
                }
                break;
            }
            if (!inferredReturn.empty()) {
                auto type = trim(m.ret);
                while (!type.empty() && type.back() == '&') {
                    type.pop_back();
                    type = trim(type);
                }
                if (!type.empty()) {
                    out << "    " << postProcessCppLine(type) << " " << inferredReturn << ";\n";
                }
            }
        }
        out << "    " << (mod.isPackage ? "inline constexpr " : (m.staticConstexpr ? "static constexpr " : ""))
            << m.ret << " " << m.name << "(" << m.args << ")\n    {\n";
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
                    if (expr.find("cat{") == std::string::npos) {
                        expr = trim(stripLogicLiteralCasts(expr));
                    }
                    line = "return static_cast<" + m.ret + ">(" + expr + ");";
                }
                emitBodyLine(out, line);
            }
            else {
                auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, m, l)));
                repairPatchedConcatOperandWidths(emittedLine);
                replaceSameMethodCombReads(emittedLine);
                forceExternalCombStorageReads(emittedLine, true);
                rewritePrimitiveLocalBitSelects(emittedLine);
                if (m.name == "_work" && isStandaloneCombEvalStatement(emittedLine)) {
                        continue;
                }
                emitBodyLine(out, emittedLine, m.name == "_work");
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

static std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << text;
    return true;
}

static bool identChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static size_t findMatchingAngle(const std::string& text, size_t open)
{
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '<' && i + 1 < text.size() && text[i + 1] == '<') {
            ++i;
        }
        else if (text[i] == '<') {
            ++depth;
        }
        else if (text[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

static std::set<std::string> findGeneratedModuleNames(const std::filesystem::path& root)
{
    std::set<std::string> names;
    auto generated = root / "generated";
    std::error_code ec;
    if (!std::filesystem::exists(generated, ec)) {
        return names;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(generated, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file() || it->path().extension() != ".h") {
            continue;
        }
        std::ifstream in(it->path());
        std::string line;
        while (std::getline(in, line)) {
            auto cls = line.find("class ");
            if (cls == std::string::npos || line.find(": public Module", cls) == std::string::npos) {
                continue;
            }
            auto begin = cls + 6;
            while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) {
                ++begin;
            }
            auto end = begin;
            while (end < line.size() && identChar(line[end])) {
                ++end;
            }
            if (end > begin) {
                names.insert(line.substr(begin, end - begin));
            }
        }
    }
    return names;
}

struct GeneratedModuleInfo {
    std::filesystem::path path;
    std::vector<std::pair<std::string, std::string>> params;
    std::set<std::string> localTypes;
    std::set<std::string> localValues;
};

static std::vector<std::string> splitTopLevelCommaList(const std::string& text)
{
    std::vector<std::string> out;
    size_t begin = 0;
    int angle = 0;
    int paren = 0;
    int brace = 0;
    int bracket = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '<') {
            ++angle;
        }
        else if (c == '>' && angle > 0) {
            --angle;
        }
        else if (c == '(') {
            ++paren;
        }
        else if (c == ')' && paren > 0) {
            --paren;
        }
        else if (c == '{') {
            ++brace;
        }
        else if (c == '}' && brace > 0) {
            --brace;
        }
        else if (c == '[') {
            ++bracket;
        }
        else if (c == ']' && bracket > 0) {
            --bracket;
        }
        else if (c == ',' && angle == 0 && paren == 0 && brace == 0 && bracket == 0) {
            out.push_back(trim(text.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    auto tail = trim(text.substr(begin));
    if (!tail.empty()) {
        out.push_back(tail);
    }
    return out;
}

static std::string replaceIdentifiers(const std::string& text, const std::map<std::string, std::string>& replacements)
{
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            auto begin = i;
            while (i < text.size() && identChar(text[i])) {
                ++i;
            }
            auto ident = text.substr(begin, i - begin);
            auto it = replacements.find(ident);
            auto prev = begin;
            while (prev > 0 && std::isspace(static_cast<unsigned char>(text[prev - 1]))) {
                --prev;
            }
            bool memberName = prev > 0 && (text[prev - 1] == '.' || text[prev - 1] == ':');
            out += (it == replacements.end() || memberName) ? ident : it->second;
        }
        else {
            out += text[i++];
        }
    }
    return out;
}

static std::string moduleNameOfConcreteType(const std::string& type)
{
    auto lt = type.find('<');
    auto name = trim(type.substr(0, lt));
    auto scope = name.rfind("::");
    if (scope != std::string::npos) {
        name = name.substr(scope + 2);
    }
    return name;
}

static std::vector<std::string> templateArgsOfConcreteType(const std::string& type)
{
    auto lt = type.find('<');
    if (lt == std::string::npos) {
        return {};
    }
    auto gt = findMatchingAngle(type, lt);
    if (gt == std::string::npos || gt <= lt + 1) {
        return {};
    }
    return splitTopLevelCommaList(type.substr(lt + 1, gt - lt - 1));
}

static std::string normalizeConcreteType(std::string type)
{
    type = trim(std::move(type));
    std::string out;
    bool prevSpace = false;
    for (char c : type) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty()) {
                prevSpace = true;
            }
            continue;
        }
        if (c == ',' || c == '<' || c == '>' || c == '(' || c == ')' || c == '[' || c == ']') {
            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }
            out += c;
            prevSpace = false;
        }
        else {
            if (prevSpace && !out.empty()) {
                out += ' ';
            }
            out += c;
            prevSpace = false;
        }
    }
    return out;
}

static std::vector<std::pair<std::string, std::string>> parseTemplateParamSpecs(const std::string& templateLine)
{
    auto lt = templateLine.find('<');
    auto gt = lt == std::string::npos ? std::string::npos : templateLine.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1) {
        return {};
    }
    std::vector<std::pair<std::string, std::string>> params;
    for (auto spec : splitTopLevelCommaList(templateLine.substr(lt + 1, gt - lt - 1))) {
        auto eq = spec.find('=');
        auto left = trim(eq == std::string::npos ? spec : spec.substr(0, eq));
        auto def = eq == std::string::npos ? std::string{} : trim(spec.substr(eq + 1));
        while (!left.empty() && (left.back() == '&' || left.back() == '*')) {
            left.pop_back();
            left = trim(left);
        }
        auto end = left.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(left[end - 1]))) {
            --end;
        }
        auto begin = end;
        while (begin > 0 && identChar(left[begin - 1])) {
            --begin;
        }
        if (end > begin) {
            params.push_back({left.substr(begin, end - begin), def});
        }
    }
    return params;
}

static std::set<std::string> parseLocalTypeNames(const std::string& text)
{
    std::set<std::string> names;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto t = trim(line);
        if (t.rfind("using ", 0) == 0) {
            auto begin = size_t(6);
            while (begin < t.size() && std::isspace(static_cast<unsigned char>(t[begin]))) {
                ++begin;
            }
            auto end = begin;
            while (end < t.size() && identChar(t[end])) {
                ++end;
            }
            if (end > begin && t.find('=', end) != std::string::npos) {
                names.insert(t.substr(begin, end - begin));
            }
        }
        else if (t.rfind("struct ", 0) == 0) {
            auto begin = size_t(7);
            while (begin < t.size() && std::isspace(static_cast<unsigned char>(t[begin]))) {
                ++begin;
            }
            auto end = begin;
            while (end < t.size() && identChar(t[end])) {
                ++end;
            }
            if (end > begin) {
                names.insert(t.substr(begin, end - begin));
            }
        }
    }
    return names;
}

static std::set<std::string> parseLocalValueNames(const std::string& text)
{
    std::set<std::string> names;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto t = trim(line);
        if (t.rfind("static constexpr ", 0) != 0 && t.rfind("static const ", 0) != 0) {
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto left = trim(t.substr(0, eq));
        while (!left.empty() && (left.back() == '&' || left.back() == '*')) {
            left.pop_back();
            left = trim(left);
        }
        auto end = left.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(left[end - 1]))) {
            --end;
        }
        auto begin = end;
        while (begin > 0 && identChar(left[begin - 1])) {
            --begin;
        }
        if (end > begin) {
            names.insert(left.substr(begin, end - begin));
        }
    }
    return names;
}

static std::map<std::string, GeneratedModuleInfo> findGeneratedModuleInfos(const std::filesystem::path& root)
{
    std::map<std::string, GeneratedModuleInfo> infos;
    auto generated = root / "generated";
    std::error_code ec;
    if (!std::filesystem::exists(generated, ec)) {
        return infos;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(generated, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file() || it->path().extension() != ".h") {
            continue;
        }
        auto text = readTextFile(it->path());
        if (text.empty()) {
            continue;
        }
        std::istringstream in(text);
        std::string prev;
        std::string line;
        while (std::getline(in, line)) {
            auto cls = line.find("class ");
            if (cls == std::string::npos || line.find(": public Module", cls) == std::string::npos) {
                prev = line;
                continue;
            }
            auto begin = cls + 6;
            while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) {
                ++begin;
            }
            auto end = begin;
            while (end < line.size() && identChar(line[end])) {
                ++end;
            }
            if (end > begin) {
                auto name = line.substr(begin, end - begin);
                auto& info = infos[name];
                info.path = it->path();
                info.params = parseTemplateParamSpecs(trim(prev));
                info.localTypes = parseLocalTypeNames(text);
                info.localValues = parseLocalValueNames(text);
            }
            prev = line;
        }
    }
    return infos;
}

static std::vector<std::string> findConcreteModuleTypes(const std::string& mainText, const std::set<std::string>& moduleNames)
{
    std::set<std::string> unique;
    for (const auto& name : moduleNames) {
        std::string needle = name + "<";
        for (size_t pos = 0; (pos = mainText.find(needle, pos)) != std::string::npos;) {
            if (pos != 0 && identChar(mainText[pos - 1])) {
                pos += needle.size();
                continue;
            }
            auto close = findMatchingAngle(mainText, pos + name.size());
            if (close == std::string::npos) {
                pos += needle.size();
                continue;
            }
            auto after = close + 1;
            while (after < mainText.size() && std::isspace(static_cast<unsigned char>(mainText[after]))) {
                ++after;
            }
            if (after >= mainText.size() || (!identChar(mainText[after]) && mainText[after] != '&' && mainText[after] != '*')) {
                pos = close + 1;
                continue;
            }
            unique.insert(mainText.substr(pos, close - pos + 1));
            pos = close + 1;
        }
    }
    return {unique.begin(), unique.end()};
}

static std::string applyInstantiationContext(
    const std::string& type,
    const std::map<std::string, std::string>& paramValues,
    const std::set<std::string>& localTypes,
    const std::set<std::string>& localValues,
    const std::string& parentTypeName)
{
    std::map<std::string, std::string> replacements = paramValues;
    for (const auto& local : localTypes) {
        replacements.emplace(local, "typename " + parentTypeName + "::" + local);
    }
    for (const auto& local : localValues) {
        replacements.emplace(local, parentTypeName + "::" + local);
    }
    return normalizeConcreteType(replaceIdentifiers(type, replacements));
}

static std::map<std::string, std::string> concreteTemplateParamValues(
    const std::string& concreteType,
    const GeneratedModuleInfo& info)
{
    auto args = templateArgsOfConcreteType(concreteType);
    std::map<std::string, std::string> values;
    for (size_t i = 0; i < info.params.size(); ++i) {
        const auto& [name, defaultValue] = info.params[i];
        std::string value;
        if (i < args.size() && !args[i].empty()) {
            value = replaceIdentifiers(args[i], values);
        }
        else if (!defaultValue.empty()) {
            value = replaceIdentifiers(defaultValue, values);
        }
        if (!value.empty()) {
            values[name] = normalizeConcreteType(value);
        }
    }
    return values;
}

static std::vector<std::string> findConcreteModuleTypesRecursive(
    const std::string& mainText,
    const std::map<std::string, GeneratedModuleInfo>& moduleInfos)
{
    std::set<std::string> moduleNames;
    for (const auto& [name, _] : moduleInfos) {
        moduleNames.insert(name);
    }

    std::vector<std::string> ordered;
    std::set<std::string> seen;
    std::deque<size_t> queue;
    for (auto type : findConcreteModuleTypes(mainText, moduleNames)) {
        type = normalizeConcreteType(type);
        if (seen.insert(type).second) {
            ordered.push_back(type);
            queue.push_back(ordered.size() - 1);
        }
    }

    while (!queue.empty()) {
        auto parentIndex = queue.front();
        queue.pop_front();
        auto parentType = ordered[parentIndex];
        auto parentAlias = "cpphdl_opt_t" + std::to_string(parentIndex);
        auto moduleName = moduleNameOfConcreteType(parentType);
        auto infoIt = moduleInfos.find(moduleName);
        if (infoIt == moduleInfos.end()) {
            continue;
        }
        auto header = readTextFile(infoIt->second.path);
        if (header.empty()) {
            continue;
        }
        auto paramValues = concreteTemplateParamValues(parentType, infoIt->second);
        for (auto child : findConcreteModuleTypes(header, moduleNames)) {
            child = applyInstantiationContext(child, paramValues, infoIt->second.localTypes, infoIt->second.localValues, parentAlias);
            auto childName = moduleNameOfConcreteType(child);
            if (moduleInfos.find(childName) == moduleInfos.end()) {
                continue;
            }
            if (seen.insert(child).second) {
                ordered.push_back(child);
                queue.push_back(ordered.size() - 1);
            }
        }
    }
    return ordered;
}

static std::vector<std::string> findGeneratedIncludes(const std::string& text)
{
    std::set<std::string> seen;
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto includePos = line.find("#include");
        auto generated = line.find("\"generated/");
        if (includePos == std::string::npos || generated == std::string::npos) {
            continue;
        }
        auto end = line.find('"', generated + 1);
        if (end == std::string::npos) {
            continue;
        }
        auto include = line.substr(generated + 1, end - generated - 1);
        if (!seen.count(include)) {
            seen.insert(include);
            out.push_back(include);
        }
    }
    return out;
}

static std::string insertOptimizedExternInclude(std::string mainText)
{
    const std::string includeLine = "#include \"cpphdl_optimized_externs.h\"\n";
    if (mainText.find(includeLine) != std::string::npos) {
        return mainText;
    }
    auto marker = std::string("#endif\n\nlong _system_clock");
    auto pos = mainText.find(marker);
    if (pos != std::string::npos) {
        mainText.insert(pos + 7, "\n" + includeLine);
        return mainText;
    }
    auto allGenerated = mainText.find("#include \"all_generated.h\"");
    if (allGenerated != std::string::npos) {
        auto eol = mainText.find('\n', allGenerated);
        mainText.insert(eol == std::string::npos ? mainText.size() : eol + 1, includeLine);
        return mainText;
    }
    auto lastInclude = mainText.rfind("#include ");
    if (lastInclude != std::string::npos) {
        auto eol = mainText.find('\n', lastInclude);
        mainText.insert(eol == std::string::npos ? mainText.size() : eol + 1, includeLine);
        return mainText;
    }
    return includeLine + mainText;
}

static std::string normalizeOptimizedPortBindings(std::string mainText)
{
    std::ostringstream out;
    std::istringstream in(mainText);
    std::string line;
    while (std::getline(in, line)) {
        auto marker = line.find("= [&]() { return &");
        if (marker != std::string::npos) {
            auto varBegin = marker + std::string("= [&]() { return &").size();
            auto varEnd = line.find("; };", varBegin);
            if (varEnd != std::string::npos) {
                auto var = trim(line.substr(varBegin, varEnd - varBegin));
                bool simpleName = !var.empty() && (std::isalpha((unsigned char)var[0]) || var[0] == '_');
                for (char ch : var) {
                    if (!(std::isalnum((unsigned char)ch) || ch == '_')) {
                        simpleName = false;
                        break;
                    }
                }
                if (simpleName) {
                    auto prefix = line.substr(0, marker);
                    auto suffix = line.substr(varEnd + std::string("; };").size());
                    line = prefix + "= _ASSIGN_REG(" + var + ");" + suffix;
                }
            }
        }
        out << line << "\n";
    }
    return out.str();
}

static int optimizeConcreteRun(const std::filesystem::path& mainPath)
{
    auto root = std::filesystem::current_path();
    auto mainText = readTextFile(mainPath);
    if (mainText.empty()) {
        std::cerr << "failed to read " << mainPath << "\n";
        return 1;
    }
    auto moduleInfos = findGeneratedModuleInfos(root);
    auto concreteTypes = findConcreteModuleTypesRecursive(mainText, moduleInfos);
    auto generatedIncludes = findGeneratedIncludes(mainText);
    if (concreteTypes.empty()) {
        std::cerr << "no concrete generated module template instantiations found in " << mainPath << "\n";
        return 1;
    }

    std::ostringstream externs;
    externs << "#pragma once\n\n#include \"all_generated.h\"\n\n";
    for (const auto& include : generatedIncludes) {
        externs << "#include \"" << include << "\"\n";
    }
    if (!generatedIncludes.empty()) {
        externs << "\n";
    }
    for (size_t i = 0; i < concreteTypes.size(); ++i) {
        externs << "using cpphdl_opt_t" << i << " = " << concreteTypes[i] << ";\n";
    }
    if (!concreteTypes.empty()) {
        externs << "\n";
    }
    for (size_t i = 0; i < concreteTypes.size(); ++i) {
        externs << "extern template void cpphdl_opt_t" << i << "::_work(bool);\n";
        externs << "extern template void cpphdl_opt_t" << i << "::_strobe();\n";
        externs << "extern template void cpphdl_opt_t" << i << "::_assign();\n";
    }

    auto instantiationFile = [&](size_t index) {
        std::ostringstream inst;
        inst << "#include \"cpphdl_optimized_externs.h\"\n\n";
        for (const auto& include : generatedIncludes) {
            inst << "#include \"" << include << "\"\n";
        }
        if (!generatedIncludes.empty()) {
            inst << "\n";
        }
        auto type = "cpphdl_opt_t" + std::to_string(index);
        inst << "template void " << type << "::_work(bool);\n";
        inst << "template void " << type << "::_strobe();\n";
        inst << "template void " << type << "::_assign();\n";
        return inst.str();
    };

    std::vector<std::pair<std::filesystem::path, std::string>> instantiationFiles;
    for (size_t i = 0; i < concreteTypes.size(); ++i) {
        const auto index = std::to_string(i);
        instantiationFiles.push_back({"cpphdl_optimized_inst_" + index + ".cpp", instantiationFile(i)});
    }

    std::ostringstream make;
    make << "CXX ?= g++\n";
    make << "CPPHDL_INCLUDE ?= /home/me/cpphdl/include\n";
    make << "CXXFLAGS ?= -std=c++23 -O0 -g0 -w -pipe -fno-asynchronous-unwind-tables -I$(CPPHDL_INCLUDE) -I$(CURDIR)\n";
    make << "LDFLAGS ?=\n\n";
    make << "RUNNER := run_cpphdl_matrix_opt\n";
    make << "OBJS := build/opt/cpphdl_optimized_main.o";
    for (const auto& [path, _] : instantiationFiles) {
        auto obj = path;
        obj.replace_extension(".o");
        make << " \\\n        build/opt/" << obj.string();
    }
    make << "\n\n";
    make << ".PHONY: all clean\n\n";
    make << "all: $(RUNNER)\n\n";
    make << "build/opt/%.o: %.cpp all_generated.h cpphdl_optimized_externs.h\n";
    make << "\t@mkdir -p $(dir $@)\n";
    make << "\t$(CXX) $(CXXFLAGS) -c $< -o $@\n\n";
    make << "$(RUNNER): $(OBJS)\n";
    make << "\t$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)\n\n";
    make << "clean:\n";
    make << "\trm -rf build/opt $(RUNNER)\n";

    auto optimizedMain = normalizeOptimizedPortBindings(insertOptimizedExternInclude(mainText));
    bool writeOk = writeTextFile(root / "cpphdl_optimized_externs.h", externs.str()) &&
        writeTextFile(root / "cpphdl_optimized_main.cpp", optimizedMain) &&
        writeTextFile(root / "Makefile.optimize", make.str());
    for (const auto& [path, text] : instantiationFiles) {
        writeOk = writeOk && writeTextFile(root / path, text);
    }
    if (!writeOk) {
        std::cerr << "failed to write optimized build files in " << root << "\n";
        return 1;
    }

    std::cerr << "optimized instantiations: " << concreteTypes.size() << "\n";
    for (const auto& type : concreteTypes) {
        std::cerr << "  " << type << "\n";
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--optimize") {
        return optimizeConcreteRun(argv[2]);
    }
    if (argc != 2) {
        std::cerr << "usage: hdlcpp <file.sv>\n";
        std::cerr << "       hdlcpp --optimize <main.cpp>\n";
        return 1;
    }

    auto tracePhases = std::getenv("HDLCPP_TRACE_PHASES") != nullptr;
    if (tracePhases) {
        std::cerr << "HDLCPP_PHASE parse begin " << argv[1] << "\n";
    }
    auto treeOrError = SyntaxTree::fromFile(argv[1]);
    if (!treeOrError) {
        std::cerr << "failed to parse " << argv[1] << "\n";
        return 1;
    }
    if (tracePhases) {
        std::cerr << "HDLCPP_PHASE parse done " << argv[1] << "\n";
        std::cerr << "HDLCPP_PHASE visit begin " << argv[1] << "\n";
    }

    Converter converter;
    (*treeOrError)->root().visit(converter);
    if (tracePhases) {
        std::cerr << "HDLCPP_PHASE visit done modules=" << converter.modules.size() << "\n";
    }
    if (const char* portTypesPath = std::getenv("HDLCPP_WRITE_PORT_TYPES")) {
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE port_types begin\n";
        }
        std::ofstream portTypes(portTypesPath, std::ios::app);
        if (!portTypes) {
            std::cerr << "failed to open HDLCPP_WRITE_PORT_TYPES=" << portTypesPath << "\n";
            return 1;
        }
        for (const auto& module : converter.modules) {
            for (const auto& port : module.ports) {
                auto svName = port.name;
                if (port.direction == "input" && hasSuffix(svName, "_in")) {
                    svName.resize(svName.size() - 3);
                }
                else if (port.direction == "output" && hasSuffix(svName, "_out")) {
                    svName.resize(svName.size() - 4);
                }
                portTypes << module.name << "." << svName << "\t"
                          << port.direction << ":" << port.type << "\n";
                if (svName != port.name) {
                    portTypes << module.name << "." << port.name << "\t"
                              << port.direction << ":" << port.type << "\n";
                }
            }
        }
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE port_types done\n";
        }
    }
    if (const char* typeWidthsPath = std::getenv("HDLCPP_WRITE_TYPE_WIDTHS")) {
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE type_widths begin\n";
        }
        std::ofstream typeWidths(typeWidthsPath, std::ios::app);
        if (!typeWidths) {
            std::cerr << "failed to open HDLCPP_WRITE_TYPE_WIDTHS=" << typeWidthsPath << "\n";
            return 1;
        }
        for (const auto& module : converter.modules) {
            for (const auto& [typeName, width] : module.typeWidths) {
                if (!typeName.empty() && !width.empty()) {
                    typeWidths << module.name << "." << typeName << "\t" << width << "\n";
                }
            }
        }
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE type_widths done\n";
        }
    }
    if (const char* moduleParamsPath = std::getenv("HDLCPP_WRITE_MODULE_PARAMS")) {
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE module_params begin\n";
        }
        std::ofstream moduleParams(moduleParamsPath, std::ios::app);
        if (!moduleParams) {
            std::cerr << "failed to open HDLCPP_WRITE_MODULE_PARAMS=" << moduleParamsPath << "\n";
            return 1;
        }
        for (const auto& module : converter.modules) {
            if (module.params.empty()) {
                continue;
            }
            moduleParams << module.name;
            for (const auto& param : module.params) {
                moduleParams << "\t" << param;
            }
            moduleParams << "\n";
        }
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE module_params done\n";
        }
    }
    if (const char* moduleTraitsPath = std::getenv("HDLCPP_WRITE_MODULE_TRAITS")) {
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE module_traits begin\n";
        }
        std::ofstream moduleTraits(moduleTraitsPath, std::ios::app);
        if (!moduleTraits) {
            std::cerr << "failed to open HDLCPP_WRITE_MODULE_TRAITS=" << moduleTraitsPath << "\n";
            return 1;
        }
        auto moduleHasSequentialState = [](const ModuleGen& module) {
            if (!module.seqAssignedVars.empty()) {
                return true;
            }
            for (const auto& var : module.vars) {
                if (trim(var.first).rfind("reg<", 0) == 0) {
                    return true;
                }
            }
            for (const auto& outReg : module.outputRegTypes) {
                if (trim(outReg.second).rfind("reg<", 0) == 0) {
                    return true;
                }
            }
            for (const auto& method : module.methods) {
                if (trim(method.args) == "bool reset") {
                    return true;
                }
            }
            return false;
        };
        for (const auto& module : converter.modules) {
            if (moduleHasSequentialState(module)) {
                moduleTraits << module.name << "\tsequential=1\n";
            }
        }
        if (tracePhases) {
            std::cerr << "HDLCPP_PHASE module_traits done\n";
        }
    }
    if (std::getenv("HDLCPP_METADATA_ONLY")) {
        return 0;
    }
    if (tracePhases) {
        std::cerr << "HDLCPP_PHASE write begin\n";
    }
    converter.write(argv[1]);
    if (tracePhases) {
        std::cerr << "HDLCPP_PHASE write done\n";
    }
    return 0;
}

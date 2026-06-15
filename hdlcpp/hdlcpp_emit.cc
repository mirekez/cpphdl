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
        auto dynamicWidth = false;
        for (auto& var : loopVars) {
            if (isIdentifierUsed(width, var)) {
                dynamicWidth = true;
                break;
            }
        }
        static constexpr const char* runtimeIndexNames[] = {
            "i", "j", "k", "x", "z", "w",
            "i_gen", "j_gen", "k_gen", "x_gen", "z_gen", "w_gen"
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
        if (isPlainCombDriver(modRef, driver)) {
            return "((" + driver + "_active ? " + name + " : (" + driver + "(), " + name + ")))";
        }
        return "((" + driver + "(), " + name + "))";
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
            if (mod->outputPortCppNames.count(name)) {
                if (isAssignOnlyOutput(*mod, name)) {
                    return mod->outputPortCppNames[name] + "()";
                }
                return emitCombOutputRead(*mod, name);
            }
            auto isRegisterObject = mod->types.count(name) && mod->types.at(name).rfind("reg<", 0) == 0;
            if (!isRegisterObject && mod->wireMap.count(name)) {
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
            if (!keyIsRegisterObject && mod->wireMap.count(key)) {
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
            auto baseArrayArgs = arrayArgsForType(baseType);
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
                return emitExpr(*b.left) + " " + op + " " + rhs;
            }
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
            if (op == "~") {
                auto width = foldWidth(exprWidth(*u.operand));
                if (!width.empty()) {
                    return "logic<" + width + ">(" + numericBitwiseNotExpr(emitNumericExpr(*u.operand), width) + ")";
                }
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

        h << "#pragma once\n\n#include \"cpphdl.h\"\n#include <array>\n#include <print>\n\n"
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
            emitInstanceConnections(m);
            wireAssignsToPorts(m);
            movePartialOutputAssignLinesToComb(m);
            for (auto it = m.combMethodByBase.begin(); it != m.combMethodByBase.end(); ) {
                auto typeIt = m.types.find(it->first);
                if (typeIt != m.types.end() && typeIt->second.rfind("reg<", 0) == 0) {
                    m.wireMap.erase(it->first);
                    it = m.combMethodByBase.erase(it);
                }
                else {
                    ++it;
                }
            }
            for (auto it = m.combSideEffectDriver.begin(); it != m.combSideEffectDriver.end(); ) {
                auto typeIt = m.types.find(it->first);
                if (typeIt != m.types.end() && typeIt->second.rfind("reg<", 0) == 0) {
                    m.combSideEffectChildInputReads.erase(it->first);
                    it = m.combSideEffectDriver.erase(it);
                }
                else {
                    ++it;
                }
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
                if (auto it = m.combMethodByBase.find(svName); it != m.combMethodByBase.end() &&
                    it->second < m.methods.size() && isPurePortBridgeComb(m, m.methods[it->second])) {
                    return " = _ASSIGN_COMB( " + m.methods[it->second].name + "() )";
                }
                auto drivers = combDriversFor(m, svName);
                if (!hasRuntimeAssignLines(m)) {
                    drivers.erase(std::remove(drivers.begin(), drivers.end(), std::string("assign_comb_func")), drivers.end());
                }
                if (drivers.empty()) {
                    return "";
                }
                auto storageName = outputStorageName(m, svName);
                std::string expr;
                for (auto& driver : drivers) {
                    std::string call = "(" + driver + "(), " + storageName + ")";
                    if (!expr.empty()) {
                        expr += ", ";
                    }
                    expr += call;
                }
                return " = _ASSIGN_COMB( (" + expr + ", " + storageName + ") )";
            };

            std::vector<std::string> deferredPortInitLines;
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
                            auto processedInit = postProcessCppLine(init);
                            if (outputPortWithName(p.name) && initReferencesChild(processedInit)) {
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
	                if (!isStructuralAssignLine(line) ||
	                    (line.find("_ASSIGN(") == std::string::npos &&
	                     line.find("_ASSIGN_COMB(") == std::string::npos) ||
	                    line.find("_ASSIGN_I(") != std::string::npos ||
	                    line.find("_ASSIGN_COMB_I(") != std::string::npos) {
	                    continue;
	                }
	                auto boundLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(m, structuralAssignMethod, line)));
	                if (boundLine.find("_comb_func()") != std::string::npos ||
	                    boundLine.find("assign_comb_func()") != std::string::npos) {
	                    line = finalAdaptStructuralAssignLine(m, boundLine);
	                }
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
                if (!noCacheCombMethod(m, f) || f.returnName.empty() ||
                    explicitCombStorage.count(f.returnName)) {
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
		                    emittedAssignLine = normalizeAssignWrapperForCombCalls(finalAdaptStructuralAssignLine(m, emittedAssignLine));
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
		                    emittedAssignLine = normalizeAssignWrapperForCombCalls(finalAdaptStructuralAssignLine(m, emittedAssignLine));
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
            for (auto& line : deferredPortInitLines) {
                h << "        " << line << "\n";
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
        auto hasRuntimeAssigns = hasRuntimeAssignLines(m);
        for (auto& line : m.assignLines) {
            if (isStructuralAssignLine(line) || isControlOrScopeLine(line)) {
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto base = baseFromLValueText(line.substr(0, eq));
            if (hasRuntimeAssigns) {
                for (auto& out : m.outputPortCppNames) {
                    if (base == outputStorageName(m, out.first) || base == combStorageName(m, out.first) ||
                        base == out.second) {
                        if (!m.seqAssignedVars.count(out.first)) {
                            m.combAssignedVars.insert(out.first);
                            m.combSideEffectDriver.emplace(out.first, "assign_comb_func");
                        }
                        break;
                    }
                }
            }
            if (!base.empty() && m.varNames.count(base) && !m.seqAssignedVars.count(base) &&
                !m.outputPortCppNames.count(base) && !m.combMethodByBase.count(base)) {
                m.combAssignedVars.insert(base);
                m.combSideEffectDriver.emplace(base, "assign_comb_func");
            }
        }
        auto substituteConfiguredPortType = [&](const std::string& moduleType, const std::string& params, std::string typeText) {
            if (typeText.empty() || params.empty()) {
                return typeText;
            }
            auto* configuredChild = findModule(moduleType);
            auto configuredParams = configuredModuleParams(moduleType);
            auto& declaredParams = (configuredChild && !configuredChild->params.empty()) ? configuredChild->params : configuredParams;
            auto actualParams = splitTopLevelArgs(params);
            auto count = std::min(declaredParams.size(), actualParams.size());
            for (size_t i = 0; i < count; ++i) {
                auto name = templateParamName(declaredParams[i]);
                if (!name.empty() && !actualParams[i].empty()) {
                    replaceIdentifierAll(typeText, name, actualParams[i]);
                }
            }
            return typeText;
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
                   (rhs.find("_comb_func()") != std::string::npos ||
                    rhs.find("assign_comb_func()") != std::string::npos);
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
        for (auto& conn : m.instanceConns) {
            if (isClockPortName(conn.port)) {
                continue;
            }
            auto* child = findModule(conn.type);
            auto portName = conn.port;
            bool isOutput = false;
            std::string portType = "bool";
            bool portTypeKnown = false;
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
                        if (!child->params.empty()) {
                            portType = substituteConfiguredPortType(conn.type, conn.params, portType);
                        }
                        portTypeKnown = true;
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
                    portType = substituteConfiguredPortType(conn.type, conn.params, configuredType);
                    portTypeKnown = true;
                }
            }
            if (isOutput) {
                if (conn.connected) {
                    auto outExpr = conn.instance + "." + portName + "()";
                    if (addConcatOutputAssignments(m, conn.lhs, outExpr)) {
                        continue;
                    }
                    auto outBase = baseFromLValueText(conn.lhs);
                    if (!outBase.empty() && m.outputPortCppNames.count(outBase) &&
                        (trim(conn.lhs) == outBase || trim(conn.lhs) == m.outputPortCppNames[outBase])) {
                        m.assignExprByBase[outBase] = outExpr;
                        for (auto& p : m.ports) {
                            if (p.name == m.outputPortCppNames[outBase]) {
                                p.init = " = _ASSIGN_COMB( " + outExpr + " )";
                                break;
                            }
                        }
                        continue;
                    }
                    addCombAssignment(m, outBase, conn.lhs, outExpr);
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
                std::string wrapper = isSimpleCombRef(rhs) ? "_ASSIGN_COMB" : "_ASSIGN";
                if (!rawRhsBase.empty() && m.combAssignedVars.count(rawRhsBase) && !m.seqAssignedVars.count(rawRhsBase) && !m.combMethodByBase.count(rawRhsBase) && !m.combSideEffectDriver.count(rawRhsBase) && hasRuntimeAssignLines(m)) {
                    rhs = "(assign_comb_func(), " + rhs + ")";
                    wrapper = "_ASSIGN_COMB";
                }
                else if (!rawRhsBase.empty() && m.combSideEffectDriver.count(rawRhsBase) && !m.seqAssignedVars.count(rawRhsBase)) {
                    m.combSideEffectChildInputReads.insert(rawRhsBase);
                    if (rhs.find(m.combSideEffectDriver[rawRhsBase] + "()") == std::string::npos) {
                        rhs = emitSideEffectRead(m, m.combSideEffectDriver[rawRhsBase], rhs);
                    }
                }
                auto target = trim(portType);
                if (portTypeKnown && target == "bool" && wrapper == "_ASSIGN_COMB") {
                    rhs = "bool(" + rhs + ")";
                }
                else if (portTypeKnown && target.rfind("logic<", 0) == 0 && target.back() == '>' &&
                    ((sourceTypeBeforeLateBind.rfind("array<", 0) == 0 || sourceTypeBeforeLateBind.rfind("std::array<", 0) == 0) ||
                     rhs.find("_func()") != std::string::npos)) {
                    rhs = target + "(" + rhs + ")";
                }
                else if (portTypeKnown &&
                         (target.rfind("array<", 0) == 0 || target.rfind("std::array<", 0) == 0) &&
                         !sourceTypeBeforeLateBind.empty() && sourceTypeBeforeLateBind != target) {
                    rhs = target + "(" + rhs + ")";
                }
                else if (portTypeKnown) {
                    rhs = adaptInputPortRhs(m, portType, rhs);
                }
                if (wrapper == "_ASSIGN_COMB" && !isSimpleCombRef(rhs)) {
                    wrapper = "_ASSIGN";
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
                }
                m.assignLines.push_back(emittedAssignLine);
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
                    auto rhsBase = baseFromLValueText(a.second);
                    auto combDrivenLocal = !rhsBase.empty() && m.varNames.count(rhsBase) &&
                        !m.seqAssignedVars.count(rhsBase) &&
                        (m.combAssignedVars.count(rhsBase) ||
                         m.combMethodByBase.count(rhsBase) ||
                         m.combSideEffectDriver.count(rhsBase));
                    auto wrapper = (m.varNames.count(a.second) && !combDrivenLocal) ? "_ASSIGN_REG" :
                        (combDrivenLocal ? "_ASSIGN_COMB" : "_ASSIGN");
                    p.init = std::string(" = ") + wrapper + "( " + rhs + " )";
                }
            }
        }
    }

    std::string lateBindExpr(const ModuleGen& mod, const std::string& expr, const std::string& exclude, const std::string& excludeDriver = "", bool lhsMode = false)
    {
        std::string out;
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
                        auto storageName = outputStorageName(mod, svName);
                        if (prev != '.' && next != '(' &&
                            (id == storageName || id == oldReg || id == oldStorage || id == oldComb)) {
                            out += id;
                            replacedOutput = true;
                            break;
                        }
                        auto selfPortCall = false;
                        if (id == cppName && next == '(') {
                            auto close = i + 1;
                            while (close < expr.size() && std::isspace(static_cast<unsigned char>(expr[close]))) {
                                ++close;
                            }
                            selfPortCall = close < expr.size() && expr[close] == ')';
                        }
                        if (prev != '.' && next != '(' &&
                            (id == svName || id == cppName)) {
                            if (isAssignOnlyOutput(mod, svName)) {
                                out += cppName + "()";
                                replacedOutput = true;
                                break;
                            }
                            out += lhsMode ? outputStorageName(mod, svName) : emitCombOutputRead(mod, svName, memberAfter(i), excludeDriver);
                            replacedOutput = true;
                            break;
                        }
                        if (prev != '.' && selfPortCall && !isAssignOnlyOutput(mod, svName)) {
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
                            auto isRegisterObject = mod.types.count(id) && mod.types.at(id).rfind("reg<", 0) == 0;
                            auto combBaseIt = mod.combMethodByBase.find(id);
                            if (combBaseIt != mod.combMethodByBase.end() && !mod.seqAssignedVars.count(id) && !isRegisterObject) {
                                out += mod.methods[combBaseIt->second].name + "()";
                                replacedComb = true;
                            }
                            else {
                                for (auto& combItem : mod.combMethodByBase) {
                                    auto combIsRegisterObject = mod.types.count(combItem.first) &&
                                        mod.types.at(combItem.first).rfind("reg<", 0) == 0;
                                    if (id == combStorageName(mod, combItem.first) && id != combStorageName(mod, exclude) &&
                                        !mod.seqAssignedVars.count(combItem.first) && !combIsRegisterObject) {
                                        out += mod.methods[combItem.second].name + "()";
                                        replacedComb = true;
                                        break;
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
        for (auto& item : mod.types) {
            auto high = "$high(" + item.first + ")";
            auto width = typeWidth(item.second);
            if (!width.empty()) {
                replaceAll(out, high, "(" + width + "-1)");
            }
        }
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
        return out;
    }

    std::string stripSelfSideEffectWrappers(const ModuleGen& mod, const std::string& line, const std::string& driver)
    {
        if (driver.empty()) {
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
            replaceAll(out, "((" + driver + "_active ? " + name + " : (" + driver + "(), " + name + ")), " + name + ")", name);
            return before != out;
        };
        while (changed) {
            changed = false;
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
                replaceAll(out, "((" + driver + "(), " + item.first + "))", item.first);
                replaceAll(out, "((" + driver + "(), " + item.first + "), " + item.first + ")", item.first);
                replaceAll(out, "((" + driver + "(), " + combStorageName(mod, item.first) + "))", combStorageName(mod, item.first));
                replaceAll(out, "((" + driver + "(), " + combStorageName(mod, item.first) + "), " + combStorageName(mod, item.first) + ")", combStorageName(mod, item.first));
                replaceAll(out, "((" + driver + "_active ? " + item.first + " : (" + driver + "(), " + item.first + ")), " + item.first + ")", item.first);
                replaceAll(out, "((" + driver + "_active ? " + combStorageName(mod, item.first) + " : (" + driver + "(), " + combStorageName(mod, item.first) + ")), " + combStorageName(mod, item.first) + ")", combStorageName(mod, item.first));
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
        return emitPlainCombMethod(mod, method) ||
               isPurePortBridgeComb(mod, method) ||
               isChildOutputComb(mod, method) ||
               isPureCombOutputBundle(mod, method);
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

    bool noCacheCombMethodRec(const ModuleGen& mod, const MethodGen& method, std::set<std::string>& visiting)
    {
        if (baseNoCacheCombMethod(mod, method)) {
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
            auto directBaseNoCache = baseNoCacheCombMethod(mod, candidate);
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

    bool noCacheCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        return baseNoCacheCombMethod(mod, method);
    }

    std::string lateBindCombRhs(const ModuleGen& mod, const MethodGen& method, const std::string& line)
    {
        auto comb = method.name.find("_comb_func") != std::string::npos;
        auto selfStrippedLine = stripSelfSideEffectWrappers(mod, line, method.name);
        auto trimmed = trim(selfStrippedLine);
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
            return lateBindExpr(mod, controlLine, "", method.name);
        }
        auto eq = selfStrippedLine.find('=');
        if (eq == std::string::npos) {
            return lateBindExpr(mod, selfStrippedLine, "", method.name);
        }
        auto lhs = selfStrippedLine.substr(0, eq);
        auto rhs = selfStrippedLine.substr(eq + 1);
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
        auto boundLhs = lateBindExpr(mod, lhs, lhsBase, method.name, true);
        auto trimmedLhs = trim(lhs);
        for (auto& outPort : mod.outputPortCppNames) {
            if (trimmedLhs == outPort.second && isAssignOnlyOutput(mod, outPort.first)) {
                boundLhs = lhs;
                break;
            }
        }
        auto rhsExclude = (comb && !method.returnName.empty()) ? method.returnName : std::string();
        auto boundRhs = lateBindExpr(mod, rhs, rhsExclude, method.name);
        for (auto& combItem : mod.combMethodByBase) {
            if (combItem.second >= mod.methods.size()) {
                continue;
            }
            auto storage = combStorageName(mod, combItem.first);
            if (storage.empty() || storage == method.returnName || storage == rhsExclude) {
                continue;
            }
            if (mod.seqAssignedVars.count(combItem.first) ||
                (mod.types.count(combItem.first) && mod.types.at(combItem.first).rfind("reg<", 0) == 0)) {
                continue;
            }
            replaceIdentifierAll(boundRhs, storage, mod.methods[combItem.second].name + "()");
        }
        for (auto& sourceMethod : mod.methods) {
            auto storage = sourceMethod.returnName;
            if (storage.empty() || storage == method.returnName || storage == rhsExclude ||
                sourceMethod.name == method.name || sourceMethod.name.find("_comb_func") == std::string::npos) {
                continue;
            }
            replaceIdentifierAll(boundRhs, storage, sourceMethod.name + "()");
        }
        return boundLhs + "=" + boundRhs;
    }

    std::string normalizeAssignWrapperForCombCalls(std::string line)
    {
        if (line.find("_ASSIGN_REG(") != std::string::npos &&
            line.find("_comb_func()") != std::string::npos) {
            replaceAll(line, "_ASSIGN_REG(", "_ASSIGN_COMB(");
        }
        return line;
    }

    void emitMethod(std::ofstream& out, const ModuleGen& mod, const MethodGen& m)
    {
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
            if (m.name.find("_comb_func") != std::string::npos && !m.returnName.empty()) {
            auto typeIt = mod.combReturnTypes.find(m.returnName);
            auto type = typeIt != mod.combReturnTypes.end() ? typeIt->second : std::string("auto");
            auto sideEffectPlainComb = emitPlainCombMethod(mod, m);
            auto noCacheComb = noCacheCombMethod(mod, m);
	            if (noCacheComb) {
	                out << "    " << type << "& " << m.name << "()\n    {\n";
	            }
	            else {
	                out << "    _LAZY_COMB(" << m.returnName << ", " << type << ")\n";
	            }
	            if (sideEffectPlainComb) {
	                out << "        " << m.name << "_active = true;\n";
	            }
	            for (auto& import : mod.imports) {
	                out << "        using namespace " << import << ";\n";
	            }
            for (auto& l : m.body) {
                auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, m, l)));
                if (!m.returnName.empty()) {
                    replaceSelfCombCall(emittedLine, m.returnName);
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
	            if (sideEffectPlainComb) {
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
                auto emittedLine = repairMalformedEquality(postProcessCppLine(lateBindCombRhs(mod, m, l)));
                out << "        " << emittedLine << "\n";
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
        if (text[i] == '<') {
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

static int optimizeConcreteRun(const std::filesystem::path& mainPath)
{
    auto root = std::filesystem::current_path();
    auto mainText = readTextFile(mainPath);
    if (mainText.empty()) {
        std::cerr << "failed to read " << mainPath << "\n";
        return 1;
    }
    auto moduleNames = findGeneratedModuleNames(root);
    auto concreteTypes = findConcreteModuleTypes(mainText, moduleNames);
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
    for (const auto& type : concreteTypes) {
        externs << "extern template void " << type << "::_work(bool);\n";
        externs << "extern template void " << type << "::_strobe();\n";
        externs << "extern template void " << type << "::_assign();\n";
    }

    std::ostringstream inst;
    inst << "#include \"all_generated.h\"\n\n";
    for (const auto& include : generatedIncludes) {
        inst << "#include \"" << include << "\"\n";
    }
    if (!generatedIncludes.empty()) {
        inst << "\n";
    }
    for (const auto& type : concreteTypes) {
        inst << "template void " << type << "::_work(bool);\n";
        inst << "template void " << type << "::_strobe();\n";
        inst << "template void " << type << "::_assign();\n";
    }

    std::ostringstream make;
    make << "CXX ?= g++\n";
    make << "CPPHDL_INCLUDE ?= /home/me/cpphdl/include\n";
    make << "CXXFLAGS ?= -std=c++23 -O0 -g0 -I$(CPPHDL_INCLUDE) -I$(CURDIR)\n";
    make << "LDFLAGS ?=\n\n";
    make << "RUNNER := run_cpphdl_matrix_opt\n";
    make << "OBJS := build/opt/cpphdl_optimized_main.o build/opt/cpphdl_optimized_instantiations.o\n\n";
    make << ".PHONY: all clean\n\n";
    make << "all: $(RUNNER)\n\n";
    make << "build/opt/%.o: %.cpp all_generated.h cpphdl_optimized_externs.h\n";
    make << "\t@mkdir -p $(dir $@)\n";
    make << "\t$(CXX) $(CXXFLAGS) -c $< -o $@\n\n";
    make << "$(RUNNER): $(OBJS)\n";
    make << "\t$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)\n\n";
    make << "clean:\n";
    make << "\trm -rf build/opt $(RUNNER)\n";

    if (!writeTextFile(root / "cpphdl_optimized_externs.h", externs.str()) ||
        !writeTextFile(root / "cpphdl_optimized_instantiations.cpp", inst.str()) ||
        !writeTextFile(root / "cpphdl_optimized_main.cpp", insertOptimizedExternInclude(mainText)) ||
        !writeTextFile(root / "Makefile.optimize", make.str())) {
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

    std::string widthMaskExpr(const std::string& width)
    {
        if (width.empty()) {
            return "~0ull";
        }
        if (isNumber(width)) {
            return std::stoul(width) >= 64 ? "~0ull" : "((1ull << " + width + ") - 1ull)";
        }
        return "(((uint64_t)(" + width + ") >= 64) ? ~0ull : ((1ull << (uint64_t)(" + width + ")) - 1ull))";
    }

    std::string numericBitwiseNotExpr(const std::string& value, const std::string& width)
    {
        return "((~(uint64_t)(" + value + ")) & " + widthMaskExpr(width) + ")";
    }

    bool isUnbasedUnsizedLiteralExpr(const ExpressionSyntax& expr, char value)
    {
        const ExpressionSyntax* e = &expr;
        while (e->kind == SyntaxKind::ParenthesizedExpression) {
            e = e->as<ParenthesizedExpressionSyntax>().expression;
        }
        if (e->kind != SyntaxKind::UnbasedUnsizedLiteralExpression) {
            return false;
        }
        auto text = trim(e->toString());
        return text.size() == 2 && text[0] == '\'' && text[1] == value;
    }

    std::string emitUnbasedUnsizedAssignmentValue(const ExpressionSyntax& expr, const std::string& targetType)
    {
        if (!isUnbasedUnsizedLiteralExpr(expr, '0') && !isUnbasedUnsizedLiteralExpr(expr, '1')) {
            return "";
        }
        auto value = isUnbasedUnsizedLiteralExpr(expr, '1') ? '1' : '0';
        auto type = unwrapRegType(trim(targetType));
        if (type.empty() || type == "bool" || type == "u1") {
            return value == '1' ? "true" : "false";
        }
        auto width = foldWidth(typeWidth(type));
        if (width.empty()) {
            width = foldWidth(logicWidth(type));
        }
        if (width.empty() && type.find("decltype(") != std::string::npos) {
            width = "cpphdl::type_width<" + type + ">()";
        }
        if (width.empty()) {
            return value == '1' ? "1" : "0";
        }
        if (value == '0') {
            if (isNumericValueType(type)) {
                return type + "(0)";
            }
            return "cpphdl::sv_cast<" + type + ">(0)";
        }
        if (isNumericValueType(type)) {
            return type + "(" + widthMaskExpr(width) + ")";
        }
        return "cpphdl::sv_cast<" + type + ">(" + widthMaskExpr(width) + ")";
    }

    std::string emitLogicBitSelectValue(const std::string& value, const std::string& width, const std::string& index)
    {
        if (width.empty()) {
            return "";
        }
        return "logic<1>((((uint64_t)(static_cast<logic<" + width + ">>(" + value + "))) >> (unsigned)(" + index + ")) & 1ull)";
    }

    std::string exprType(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return exprType(*expr.as<ParenthesizedExpressionSyntax>().expression);
        }
        if (mod) {
            auto rawPath = expr.toString();
            if (rawPath.find('[') == std::string::npos) {
                auto pathResolved = pathType(rawPath);
                if (!pathResolved.empty()) {
                    return pathResolved;
                }
            }
        }
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            auto localType = lookupLocalType(name);
            if (!localType.empty()) {
                return localType;
            }
            if (mod && mod->types.count(name)) {
                return mod->types[name];
            }
            if (mod && mod->outputPortCppNames.count(name)) {
                auto type = outputStorageType(*mod, name, mod->outputPortCppNames[name]);
                if (!type.empty()) {
                    return type;
                }
            }
            if (mod) {
                auto cppIt = mod->portCppNames.find(name);
                for (const auto& port : mod->ports) {
                    if (port.name == name || (cppIt != mod->portCppNames.end() && port.name == cppIt->second)) {
                        return port.type;
                    }
                }
            }
            return "";
        }
        if (expr.kind == SyntaxKind::ScopedName) {
            return pathType(expr.toString());
        }
        if (expr.kind == SyntaxKind::InvocationExpression && mod) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto callee = trim(exprText(i.left->toString()));
            auto lookup = [&](std::string name) -> std::string {
                auto it = mod->functionReturnTypes.find(name);
                if (it != mod->functionReturnTypes.end()) {
                    return it->second;
                }
                auto pos = name.rfind("::");
                if (pos != std::string::npos) {
                    it = mod->functionReturnTypes.find(name.substr(pos + 2));
                    if (it != mod->functionReturnTypes.end()) {
                        return it->second;
                    }
                }
                return "";
            };
            if (auto type = lookup(callee); !type.empty()) {
                return type;
            }
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return fieldTypeFor(exprType(*e.left), tok(e.name));
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            auto type = unwrapRegType(resolveAliasValueType(exprType(*e.left)));
            auto args = templateArgsFor(type, "array");
            if (args.empty()) {
                args = templateArgsFor(type, "std::array");
            }
            if (args.size() >= 2) {
                return args[0];
            }
            return type;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            std::string type;
            auto baseName = tok(n.identifier);
            type = lookupLocalType(baseName);
            if (type.empty() && mod && mod->types.count(baseName)) {
                type = mod->types[baseName];
            }
            for (size_t idx = 0; idx < n.selectors.size(); ++idx) {
                type = unwrapRegType(resolveAliasValueType(type));
                auto args = templateArgsFor(type, "array");
                if (args.empty()) {
                    args = templateArgsFor(type, "std::array");
                }
                if (args.size() >= 2) {
                    type = args[0];
                }
            }
            return type;
        }
        return "";
    }

    std::string resolveAliasValueType(std::string type)
    {
        type = unwrapRegType(trim(std::move(type)));
        for (size_t guard = 0; mod && guard < 32; ++guard) {
            auto it = mod->types.find(type);
            if (it == mod->types.end() || it->second == type) {
                break;
            }
            type = unwrapRegType(trim(it->second));
        }
        return type;
    }

    bool isAggregateValueType(const std::string& type)
    {
        auto resolvedType = resolveAliasValueType(type);
        if (resolvedType.empty()) {
            return false;
        }
        auto isArrayType = [](const std::string& t) {
            return t.rfind("array<", 0) == 0 || t.rfind("std::array<", 0) == 0;
        };
        if (isArrayType(resolvedType)) {
            return true;
        }
        if (mod) {
            if (mod->typeFields.count(resolvedType) || mod->typeFields.count(type)) {
                return true;
            }
        }
        for (auto& candidate : modules) {
            if (candidate.typeFields.count(resolvedType) || candidate.typeFields.count(type)) {
                return true;
            }
        }
        return false;
    }

    bool isPackedArrayValueType(const std::string& type)
    {
        auto resolvedType = resolveAliasValueType(type);
        auto args = templateArgsFor(resolvedType, "array");
        if (args.empty()) {
            args = templateArgsFor(resolvedType, "std::array");
        }
        return args.size() >= 3 && trim(args[2]) == "true";
    }

    bool isUnpackedArrayValueType(const std::string& type)
    {
        auto resolvedType = resolveAliasValueType(type);
        auto args = templateArgsFor(resolvedType, "array");
        if (args.empty()) {
            args = templateArgsFor(resolvedType, "std::array");
        }
        return args.size() >= 2 && (args.size() < 3 || trim(args[2]) != "true");
    }

    std::string packedValueExpr(const ExpressionSyntax& expr, const std::string& emitted = "")
    {
        auto type = exprType(expr);
        auto base = assignedBase(expr);
        if (type.empty() && mod && !base.empty()) {
            auto it = mod->types.find(base);
            if (it != mod->types.end()) {
                type = it->second;
            }
        }
        if (type.empty() && !base.empty()) {
            type = lookupLocalType(base);
        }
        auto resolvedType = resolveAliasValueType(type);
        if (resolvedType.empty() || isNumericValueType(resolvedType) || !isAggregateValueType(type)) {
            return "";
        }
        auto width = usableTemplateLogicWidth(exprWidth(expr));
        if (width.empty() || width == "1") {
            auto rawWidth = typeWidth(type);
            width = foldWidth(rawWidth);
            if (width.empty()) {
                width = usableTemplateLogicWidth(rawWidth);
            }
        }
        if (width.empty() || width == "1") {
            auto rawWidth = typeWidth(resolvedType);
            width = foldWidth(rawWidth);
            if (width.empty()) {
                width = usableTemplateLogicWidth(rawWidth);
            }
        }
        if (width.empty()) {
            return "";
        }
        auto value = emitted.empty() ? emitExpr(expr) : emitted;
        return "cpphdl::pack_value<" + width + ">(" + value + ")";
    }

    std::string packedNumericOperandExpr(const ExpressionSyntax& expr, const std::string& emitted = "")
    {
        auto packed = packedValueExpr(expr, emitted);
        if (packed.empty()) {
            return "";
        }
        return "(uint64_t)(" + packed + ")";
    }

    std::string logicValueExpr(const ExpressionSyntax& expr, const std::string& width, const std::string& emitted = "")
    {
        if (auto packed = packedValueExpr(expr, emitted); !packed.empty()) {
            return packed;
        }
        auto value = emitted.empty() ? emitExpr(expr) : emitted;
        return "logic<" + width + ">(" + value + ")";
    }

    std::string bitwiseExprWidth(const ExpressionSyntax& expr)
    {
        auto width = usableTemplateLogicWidth(exprWidth(expr));
        if (!width.empty() && width != "1") {
            return width;
        }

        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return bitwiseExprWidth(*expr.as<ParenthesizedExpressionSyntax>().expression);
        }

        auto typeWidthFromExpr = [&]() -> std::string {
            auto type = unwrapRegType(resolveAliasValueType(exprType(expr)));
            auto w = usableTemplateLogicWidth(typeWidth(type));
            return w == "1" ? std::string() : w;
        };
        if (auto w = typeWidthFromExpr(); !w.empty()) {
            return w;
        }

        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto op = tok(b.operatorToken);
            if (op == "|" || op == "&" || op == "^") {
                auto leftWidth = bitwiseExprWidth(*b.left);
                auto rightWidth = bitwiseExprWidth(*b.right);
                if (!leftWidth.empty() && leftWidth == rightWidth) {
                    return leftWidth;
                }
                if (isZeroLiteralExpr(*b.left) && !rightWidth.empty()) {
                    return rightWidth;
                }
                if (isZeroLiteralExpr(*b.right) && !leftWidth.empty()) {
                    return leftWidth;
                }
                if (isNumber(leftWidth) && isNumber(rightWidth)) {
                    return std::to_string(std::max(std::stoul(leftWidth), std::stoul(rightWidth)));
                }
                if (!leftWidth.empty()) {
                    return leftWidth;
                }
                return rightWidth;
            }
        }

        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            if (tok(u.operatorToken) == "~") {
                return bitwiseExprWidth(*u.operand);
            }
        }

        return "";
    }

    std::string usableTemplateLogicWidth(std::string width)
    {
        width = foldWidth(std::move(width));
        if (width.find(".bits(") != std::string::npos ||
            width.find("'(") != std::string::npos ||
            width.find('[') != std::string::npos ||
            width.find(']') != std::string::npos ||
            width.find(" | ") != std::string::npos) {
            return "";
        }
        return width;
    }

    std::string invocationCalleeRaw(const InvocationExpressionSyntax& call)
    {
        return call.left ? trim(call.left->toString()) : std::string();
    }

    const ExpressionSyntax* firstInvocationExpressionArg(const InvocationExpressionSyntax& call)
    {
        if (!call.arguments) {
            return nullptr;
        }
        for (auto arg : call.arguments->parameters) {
            if (arg->kind != SyntaxKind::OrderedArgument) {
                continue;
            }
            auto exprNode = arg->as<OrderedArgumentSyntax>().expr;
            if (exprNode && ExpressionSyntax::isKind(exprNode->kind)) {
                return &exprNode->as<ExpressionSyntax>();
            }
            if (exprNode && exprNode->kind == SyntaxKind::SimplePropertyExpr) {
                auto& prop = exprNode->as<SimplePropertyExprSyntax>();
                if (prop.expr && prop.expr->kind == SyntaxKind::SimpleSequenceExpr) {
                    auto& seq = prop.expr->as<SimpleSequenceExprSyntax>();
                    if (seq.expr && ExpressionSyntax::isKind(seq.expr->kind)) {
                        return &seq.expr->as<ExpressionSyntax>();
                    }
                }
            }
        }
        return nullptr;
    }

    std::string signednessCastWidth(const ExpressionSyntax& operand)
    {
        if (operand.kind == SyntaxKind::ParenthesizedExpression) {
            return signednessCastWidth(*operand.as<ParenthesizedExpressionSyntax>().expression);
        }
        if (operand.kind == SyntaxKind::IdentifierName) {
            auto name = tok(operand.as<IdentifierNameSyntax>().identifier);
            if (!name.empty() && exprType(operand).empty() && (constantType(name).empty() || loopVars.count(name))) {
                return "32";
            }
        }
        auto width = foldWidth(exprWidth(operand));
        if (width.empty()) {
            width = "64";
        }
        return width;
    }

    std::string emitSystemSignednessCast(const InvocationExpressionSyntax& call, const std::string& rawCallee)
    {
        auto arg = firstInvocationExpressionArg(call);
        if (!arg) {
            return rawCallee == "$signed" ? "int64_t(0)" : "uint64_t(0)";
        }
        auto width = signednessCastWidth(*arg);
        std::string helper = rawCallee == "$signed" ? "cpphdl::sv_signed" : "cpphdl::sv_unsigned";
        return helper + "<(size_t)(" + width + ")>(" + emitNumericExpr(*arg) + ")";
    }

    std::string emitSystemBitsValue(const InvocationExpressionSyntax& call)
    {
        auto arg = firstInvocationExpressionArg(call);
        if (!arg) {
            return "0";
        }
        auto widthForType = [&](const std::string& type) -> std::string {
            auto width = foldWidth(resolvedTypeWidth(type));
            if (!width.empty()) {
                return width;
            }
            if (!trim(type).empty()) {
                return "cpphdl::type_width<" + trim(type) + ">()";
            }
            return "";
        };
        if (auto type = exprType(*arg); !type.empty()) {
            if (auto width = widthForType(type); !width.empty()) {
                return width;
            }
        }
        auto base = assignedBase(*arg);
        if (!base.empty()) {
            if (auto localType = lookupLocalType(base); !localType.empty()) {
                if (auto width = widthForType(localType); !width.empty()) {
                    return width;
                }
            }
            if (mod && mod->types.count(base)) {
                if (auto width = widthForType(mod->types[base]); !width.empty()) {
                    return width;
                }
            }
            if (auto knownTypeWidth = typeWidth(base); !knownTypeWidth.empty()) {
                auto width = foldWidth(knownTypeWidth);
                if (width.empty()) {
                    width = knownTypeWidth;
                }
                return width;
            }
        }
        auto width = foldWidth(exprWidth(*arg));
        return width.empty() ? "0" : width;
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
        if (names.empty()) {
            return "";
        }
        auto type = lookupLocalType(names.front());
        if (type.empty() && mod && mod->types.count(names.front())) {
            type = mod->types[names.front()];
        }
        if (type.empty()) {
            return "";
        }
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
            auto field = segments.size() > 1 ? cppIdent(segments[1].first) : std::string();
            out = isAssignOnlyOutput(*mod, base) ? mod->outputPortCppNames[base] + "()" : emitCombOutputRead(*mod, base, field);
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
        auto sizedCastWidthFromText = [&](const std::string& text) -> std::string {
            auto tick = text.find("'(");
            if (tick == std::string::npos || tick == 0) {
                return "";
            }
            auto open = tick + 1;
            auto close = matchingParenClose(text, open);
            if (close == std::string::npos || !trim(text.substr(close + 1)).empty()) {
                return "";
            }
            auto target = stripBalancedOuterParens(trim(text.substr(0, tick)));
            if (target.empty()) {
                return "";
            }
            auto isBuiltinValueType = [](const std::string& value) {
                return value == "bool" || value == "int" || value == "unsigned" || value == "uint64_t" ||
                       value == "uint32_t" || value == "uint16_t" || value == "uint8_t" ||
                       value == "longint" || value == "shortint";
            };
            auto isValueTemplateParamName = [&](const std::string& value) {
                if (!mod) {
                    return false;
                }
                for (const auto& param : mod->params) {
                    if (templateParamName(param) == value && !templateParamValueType(param).empty()) {
                        return true;
                    }
                }
                return false;
            };
            auto isDecimal = [](const std::string& value) {
                return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                    return std::isdigit(ch);
                });
            };
            bool widthCast = !isBuiltinValueType(target) &&
                             (isDecimal(target) || target.find('.') != std::string::npos ||
                              isValueTemplateParamName(target) || !constantType(target).empty());
            if (!widthCast) {
                return "";
            }
            return foldWidth(replaceKeywordMemberAccess(exprText(target)));
        };
        if (auto castWidth = sizedCastWidthFromText(simple); !castWidth.empty()) {
            return castWidth;
        }
        if (expr.kind == SyntaxKind::CastExpression) {
            auto& c = expr.as<CastExpressionSyntax>();
            auto rawTarget = trim(exprText(c.left->toString()));
            auto rawTargetValue = stripBalancedOuterParens(rawTarget);
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
                auto width = foldWidth(emitUntypedNumericExpr(*c.left));
                if (!width.empty()) {
                    return width;
                }
            }
        }
        if (expr.kind == SyntaxKind::ScopedName) {
            auto& scoped = expr.as<ScopedNameSyntax>();
            auto selectorWidth = [&]() -> std::string {
                if (scoped.right->kind == SyntaxKind::IdentifierSelectName) {
                    auto& n = scoped.right->as<IdentifierSelectNameSyntax>();
                    if (!n.selectors.empty()) {
                        auto last = n.selectors.back();
                        if (last && last->selector) {
                            return selectWidth(*last);
                        }
                    }
                }
                return "";
            }();
            if (!selectorWidth.empty()) {
                return foldWidth(selectorWidth);
            }
            auto width = foldWidth(resolvedTypeWidth(pathType(simple)));
            if (!width.empty()) {
                return width;
            }
        }
        auto finalDottedSegmentHasSelect = [](const std::string& path) {
            auto dot = path.rfind('.');
            if (dot == std::string::npos) {
                return false;
            }
            return path.find('[', dot + 1) != std::string::npos;
        };
        if (!BinaryExpressionSyntax::isKind(expr.kind) &&
            simple.find('.') != std::string::npos && !finalDottedSegmentHasSelect(simple)) {
            auto width = foldWidth(resolvedTypeWidth(pathType(simple)));
            if (!width.empty()) {
                return width;
            }
            if (mod) {
                auto dot = simple.find('.');
                auto base = trim(simple.substr(0, dot));
                auto field = trim(simple.substr(dot + 1));
                if (!base.empty() && !field.empty() && field.find('.') == std::string::npos) {
                    auto baseType = lookupLocalType(base);
                    if (baseType.empty()) {
                        auto typeIt = mod->types.find(base);
                        if (typeIt != mod->types.end()) {
                            baseType = typeIt->second;
                        }
                    }
                    baseType = unwrappedValueType(baseType);
                    if (!baseType.empty()) {
                        return "cpphdl::type_width<std::remove_cvref_t<decltype(std::declval<" + baseType + ">()." + field + ")>>()";
                    }
                }
            }
            auto emitted = pathValueExpr(simple);
            if (emitted.find('.') != std::string::npos) {
                return "cpphdl::type_width<std::remove_cvref_t<decltype(" + emitted + ")>>()";
            }
        }
        if (mod && expr.kind == SyntaxKind::IdentifierName && mod->types.count(simple)) {
            auto w = resolvedTypeWidth(mod->types[simple]);
            if (!w.empty()) {
                return foldWidth(w);
            }
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            if (mod) {
                auto baseName = tok(n.identifier);
                auto selectedType = lookupLocalType(baseName);
                if (selectedType.empty()) {
                    auto it = mod->types.find(baseName);
                    if (it != mod->types.end()) {
                        selectedType = it->second;
                    }
                }
                if (selectedType.empty() && mod->outputPortCppNames.count(baseName)) {
                    selectedType = outputStorageType(*mod, baseName, mod->outputPortCppNames[baseName]);
                }
                if (!selectedType.empty()) {
                    selectedType = unwrapRegType(selectedType);
                    bool consumedArraySelect = false;
                    for (auto s : n.selectors) {
                        if (selectedType.rfind("array<", 0) == 0 || selectedType.rfind("std::array<", 0) == 0) {
                            auto args = templateArgsFor(selectedType, "array");
                            if (args.empty()) {
                                args = templateArgsFor(selectedType, "std::array");
                            }
                            if (args.size() >= 2) {
                                auto elemWidth = foldWidth(resolvedTypeWidth(args[0]));
                                if (!elemWidth.empty() && s->selector && RangeSelectSyntax::isKind(s->selector->kind)) {
                                    auto count = foldWidth(selectTemplateWidth(*s));
                                    if (!count.empty()) {
                                        return foldWidth("(" + elemWidth + ") * (" + count + ")");
                                    }
                                }
                                selectedType = args[0];
                                consumedArraySelect = true;
                                continue;
                            }
                        }
                        if (s->selector && RangeSelectSyntax::isKind(s->selector->kind)) {
                            auto raw = selectTemplateWidth(*s);
                            auto folded = foldWidth(raw);
                            if (!folded.empty()) {
                                return folded;
                            }
                            if (!raw.empty()) {
                                return raw;
                            }
                        }
                        else if (s->selector && s->selector->kind == SyntaxKind::BitSelect &&
                                 mod->typeParamNames.count(selectedType)) {
                            auto emitted = emitExpr(expr);
                            if (!emitted.empty()) {
                                return "cpphdl::type_width<std::remove_cvref_t<decltype(" + emitted + ")>>()";
                            }
                        }
                        else {
                            auto w = foldWidth(selectWidth(*s));
                            if (!w.empty()) {
                                return w;
                            }
                        }
                    }
                    if (consumedArraySelect) {
                        selectedType = resolveAliasValueType(selectedType);
                        auto w = foldWidth(resolvedTypeWidth(selectedType));
                        return w;
                    }
                }
            }
            if (n.selectors.size() >= 2) {
                auto last = n.selectors.back();
                if (last && last->selector && last->selector->kind == SyntaxKind::BitSelect) {
                    auto width = foldWidth(selectWidth(*last));
                    if (!width.empty()) {
                        return width;
                    }
                }
            }
            auto width = foldWidth(selectsWidth(n.selectors));
            if (!width.empty()) {
                return width;
            }
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& member = expr.as<MemberAccessExpressionSyntax>();
            auto width = foldWidth(resolvedTypeWidth(exprType(expr)));
            if (!width.empty()) {
                return width;
            }
            auto baseType = unwrappedValueType(exprType(*member.left));
            auto field = tok(member.name);
            if (!baseType.empty() && !field.empty()) {
                return "cpphdl::type_width<std::remove_cvref_t<decltype(std::declval<" + baseType + ">()." + field + ")>>()";
            }
            auto emitted = pathValueExpr(expr.toString());
            if (emitted.find('.') != std::string::npos) {
                return "cpphdl::type_width<std::remove_cvref_t<decltype(" + emitted + ")>>()";
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            if (e.select) {
                if (e.left->kind == SyntaxKind::ElementSelectExpression && e.select->selector &&
                    e.select->selector->kind == SyntaxKind::BitSelect) {
                    auto width = foldWidth(selectWidth(*e.select));
                    if (!width.empty()) {
                        return width;
                    }
                }
                auto leftType = unwrapRegType(resolveAliasValueType(exprType(*e.left)));
                auto arrayArgs = templateArgsFor(leftType, "array");
                if (arrayArgs.empty()) {
                    arrayArgs = templateArgsFor(leftType, "std::array");
                }
                if (arrayArgs.size() >= 2 && e.select->selector) {
                    auto elemWidth = foldWidth(resolvedTypeWidth(arrayArgs[0]));
                    if (elemWidth.empty()) {
                        elemWidth = foldWidth(resolvedTypeWidth(resolveAliasValueType(arrayArgs[0])));
                    }
                    if (elemWidth.empty() && !trim(arrayArgs[0]).empty()) {
                        elemWidth = "cpphdl::type_width<" + trim(arrayArgs[0]) + ">()";
                    }
                    if (!elemWidth.empty()) {
                        if (e.select->selector->kind == SyntaxKind::BitSelect) {
                            return elemWidth;
                        }
                        if (RangeSelectSyntax::isKind(e.select->selector->kind)) {
                            auto count = foldWidth(selectTemplateWidth(*e.select));
                            if (!count.empty()) {
                                return foldWidth("(" + elemWidth + ") * (" + count + ")");
                            }
                        }
                    }
                }
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
                if (!rawWidth.empty()) {
                    return rawWidth;
                }
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            if (e.select) {
                auto width = foldWidth(selectWidth(*e.select));
                if (!width.empty()) {
                    return width;
                }
            }
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto rawCallee = invocationCalleeRaw(i);
            if (rawCallee == "$signed" || rawCallee == "$unsigned") {
                if (auto arg = firstInvocationExpressionArg(i)) {
                    return exprWidth(*arg);
                }
                return "64";
            }
            if (rawCallee == "$bits") {
                return "32";
            }
            auto width = knownFunctionReturnWidth(exprText(i.left->toString()));
            if (!width.empty()) {
                return width;
            }
            if (mod) {
                auto callee = trim(exprText(i.left->toString()));
                auto lookup = [&](std::string name) -> std::string {
                    auto it = mod->functionReturnTypes.find(name);
                    if (it != mod->functionReturnTypes.end()) {
                        return it->second;
                    }
                    auto pos = name.rfind("::");
                    if (pos != std::string::npos) {
                        it = mod->functionReturnTypes.find(name.substr(pos + 2));
                        if (it != mod->functionReturnTypes.end()) {
                            return it->second;
                        }
                    }
                    return "";
                };
                auto retType = lookup(callee);
                auto retWidth = foldWidth(typeWidth(retType));
                if (retWidth.empty()) {
                    retWidth = foldWidth(logicWidth(retType));
                }
                if (!retWidth.empty()) {
                    return retWidth;
                }
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
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            size_t total = 0;
            bool allConst = true;
            std::vector<std::string> widths;
            for (auto e : expr.as<ConcatenationExpressionSyntax>().expressions) {
                auto rawWidth = exprWidth(*e);
                auto w = foldWidth(rawWidth);
                if (w.empty() && !rawWidth.empty()) {
                    w = rawWidth;
                }
                if (w.empty() || w == "1") {
                    auto valueType = resolveAliasValueType(exprType(*e));
                    auto primitive = valueType.empty() || valueType == "bool" || valueType == "unsigned" ||
                        valueType == "u8" || valueType == "u16" || valueType == "u32" || valueType == "u64" ||
                        valueType == "uint8_t" || valueType == "uint16_t" || valueType == "uint32_t" ||
                        valueType == "uint64_t" || valueType.rfind("logic<", 0) == 0 || valueType.rfind("u<", 0) == 0;
                    if (!primitive) {
                        auto typeW = foldWidth(resolvedTypeWidth(valueType));
                        if (!typeW.empty()) {
                            w = typeW;
                        }
                    }
                }
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
            auto rawInnerWidth = exprWidth(*m.concatenation);
            auto innerWidth = foldWidth(rawInnerWidth);
            if (innerWidth.empty() && !rawInnerWidth.empty()) {
                innerWidth = rawInnerWidth;
            }
            if (!count.empty() && !innerWidth.empty() &&
                isNumber(count) && isNumber(innerWidth)) {
                return std::to_string(std::stoul(count) * std::stoul(innerWidth));
            }
            if (!count.empty() && !innerWidth.empty()) {
                return "(" + count + ")*(" + innerWidth + ")";
            }
        }
        auto selectedArrayWidth = [&]() -> std::string {
            if (!mod) {
                return "";
            }
            std::string base;
            const ElementSelectSyntax* select = nullptr;
            if (expr.kind == SyntaxKind::IdentifierSelectName) {
                auto& n = expr.as<IdentifierSelectNameSyntax>();
                if (n.selectors.empty()) {
                    return "";
                }
                base = tok(n.identifier);
                select = n.selectors.back();
            }
            else if (expr.kind == SyntaxKind::ElementSelectExpression) {
                auto& e = expr.as<ElementSelectExpressionSyntax>();
                base = assignedBase(*e.left);
                select = e.select;
            }
            if (base.empty() || !select || !select->selector) {
                return "";
            }
            auto storageType = mod->types.count(base) ? mod->types[base] : std::string();
            if (storageType.empty() && mod->outputPortCppNames.count(base)) {
                storageType = outputStorageType(*mod, base, mod->outputPortCppNames[base]);
            }
            if (storageType.empty()) {
                return "";
            }
            auto type = resolveAliasValueType(storageType);
            auto args = templateArgsFor(type, "array");
            if (args.empty()) {
                args = templateArgsFor(type, "std::array");
            }
            if (args.size() < 2) {
                return "";
            }
            auto elemWidth = foldWidth(resolvedTypeWidth(args[0]));
            if (elemWidth.empty()) {
                elemWidth = foldWidth(resolvedTypeWidth(resolveAliasValueType(args[0])));
            }
            if (elemWidth.empty() && !trim(args[0]).empty()) {
                elemWidth = "cpphdl::type_width<" + trim(args[0]) + ">()";
            }
            if (elemWidth.empty()) {
                return "";
            }
            if (select->selector->kind == SyntaxKind::BitSelect) {
                return elemWidth;
            }
            if (RangeSelectSyntax::isKind(select->selector->kind)) {
                auto count = foldWidth(selectTemplateWidth(*select));
                if (!count.empty()) {
                    return foldWidth("(" + elemWidth + ") * (" + count + ")");
                }
            }
            return "";
        }();
        if (!selectedArrayWidth.empty()) {
            return selectedArrayWidth;
        }
        if (mod) {
            auto directType = exprType(expr);
            auto directWidth = foldWidth(typeWidth(directType));
            if (!directWidth.empty()) {
                return directWidth;
            }
        }
        auto base = assignedBase(expr);
        if (mod->types.count(base)) {
            auto w = typeWidth(mod->types[base]);
            if (!w.empty()) {
                return foldWidth(w);
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
            auto usableWidth = [](std::string width) {
                width = foldWidth(std::move(width));
                if (width.find(".bits(") != std::string::npos ||
                    width.find("'(") != std::string::npos ||
                    width.find('[') != std::string::npos ||
                    width.find(']') != std::string::npos) {
                    return std::string();
                }
                return width;
            };
            auto left = usableWidth(exprWidth(*b.left));
            auto right = usableWidth(exprWidth(*b.right));
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
        auto zeroBound = [](std::string value) {
            value = trim(value);
            if (isZeroLiteralText(value)) {
                return true;
            }
            auto compact = value;
            compact.erase(std::remove_if(compact.begin(), compact.end(), [](unsigned char c) {
                return std::isspace(c);
            }), compact.end());
            return compact.find("(0)&") != std::string::npos ||
                   compact.find("(0))&") != std::string::npos ||
                   compact.find("(uint64_t)(0)") != std::string::npos;
        };
        if (isNumber(left) && isNumber(right)) {
            auto lv = std::stoul(left);
            auto rv = std::stoul(right);
            return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
        }
        if (zeroBound(right)) {
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
            if (i + 1 < expr.size() && expr[i] == '<' && expr[i + 1] == '<') {
                ++i;
                continue;
            }
            if (i + 1 < expr.size() && expr[i] == '>' && expr[i + 1] == '>') {
                ++i;
                continue;
            }
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

    std::string emittedConcatOperandCastWidth(const std::string& expr)
    {
        std::string first;
        size_t pos = 0;
        while ((pos = expr.find("logic<", pos)) != std::string::npos) {
            auto start = pos + 6;
            int depth = 1;
            std::string width;
            for (size_t i = start; i < expr.size(); ++i) {
                if (i + 1 < expr.size() && expr[i] == '<' && expr[i + 1] == '<') {
                    ++i;
                    continue;
                }
                if (i + 1 < expr.size() && expr[i] == '>' && expr[i + 1] == '>') {
                    ++i;
                    continue;
                }
                if (expr[i] == '<') {
                    ++depth;
                }
                else if (expr[i] == '>') {
                    if (--depth == 0) {
                        width = trim(expr.substr(start, i - start));
                        pos = i + 1;
                        break;
                    }
                }
            }
            if (width.empty()) {
                break;
            }
            if (first.empty()) {
                first = width;
            }
            auto folded = foldWidth(width);
            if (folded != "64") {
                return width;
            }
        }
        return first;
    }

    bool emittedOneBitValueExpr(const std::string& expr)
    {
        auto s = stripBalancedOuterParens(trim(expr));
        return s.rfind("logic<1>(", 0) == 0 ||
               s.rfind("(uint64_t)(logic<1>(", 0) == 0 ||
               s.rfind("uint64_t(logic<1>(", 0) == 0;
    }

    bool isBitSelectOperand(const ExpressionSyntax& expr)
    {
        const ExpressionSyntax* e = &expr;
        while (e->kind == SyntaxKind::ParenthesizedExpression) {
            e = e->as<ParenthesizedExpressionSyntax>().expression;
        }
        if (e->kind == SyntaxKind::ElementSelectExpression) {
            auto& sel = e->as<ElementSelectExpressionSyntax>();
            return sel.select && sel.select->selector &&
                   sel.select->selector->kind == SyntaxKind::BitSelect;
        }
        if (e->kind == SyntaxKind::IdentifierSelectName) {
            auto& n = e->as<IdentifierSelectNameSyntax>();
            if (n.selectors.empty()) {
                return false;
            }
            auto last = n.selectors.back();
            return last && last->selector &&
                   last->selector->kind == SyntaxKind::BitSelect;
        }
        if (e->kind == SyntaxKind::ScopedName) {
            auto& scoped = e->as<ScopedNameSyntax>();
            if (scoped.right && scoped.right->kind == SyntaxKind::IdentifierSelectName) {
                return isBitSelectOperand(*scoped.right);
            }
        }
        return false;
    }

    std::string rangeSelectOperandWidth(const ExpressionSyntax& expr)
    {
        const ExpressionSyntax* e = &expr;
        while (e->kind == SyntaxKind::ParenthesizedExpression) {
            e = e->as<ParenthesizedExpressionSyntax>().expression;
        }
        auto widthForSelect = [&](const ElementSelectSyntax& select) -> std::string {
            if (!select.selector || !RangeSelectSyntax::isKind(select.selector->kind)) {
                return "";
            }
            auto width = foldWidth(selectTemplateWidth(select));
            if (width.empty()) {
                width = selectTemplateWidth(select);
            }
            return width;
        };
        if (e->kind == SyntaxKind::ElementSelectExpression) {
            auto& sel = e->as<ElementSelectExpressionSyntax>();
            auto leftType = unwrapRegType(resolveAliasValueType(exprType(*sel.left)));
            auto arrayArgs = templateArgsFor(leftType, "array");
            if (arrayArgs.empty()) {
                arrayArgs = templateArgsFor(leftType, "std::array");
            }
            if (!arrayArgs.empty()) {
                return "";
            }
            return sel.select ? widthForSelect(*sel.select) : std::string();
        }
        if (e->kind == SyntaxKind::IdentifierSelectName) {
            auto& n = e->as<IdentifierSelectNameSyntax>();
            if (n.selectors.empty()) {
                return "";
            }
            auto last = n.selectors.back();
            return last ? widthForSelect(*last) : std::string();
        }
        if (e->kind == SyntaxKind::ScopedName) {
            auto& scoped = e->as<ScopedNameSyntax>();
            if (scoped.right && scoped.right->kind == SyntaxKind::IdentifierSelectName) {
                return rangeSelectOperandWidth(*scoped.right);
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
            auto rawWidth = exprWidth(*e);
            auto width = foldWidth(rawWidth);
            if (width.empty() && !rawWidth.empty()) {
                width = rawWidth;
            }
            auto rangeSelectWidth = rangeSelectOperandWidth(*e);
            if (!rangeSelectWidth.empty()) {
                width = rangeSelectWidth;
            }
            auto valueType = unwrappedValueType(exprType(*e));
            auto primitive = valueType.empty() || valueType == "bool" || valueType == "unsigned" ||
                valueType == "u8" || valueType == "u16" || valueType == "u32" || valueType == "u64" ||
                valueType == "uint8_t" || valueType == "uint16_t" || valueType == "uint32_t" || valueType == "uint64_t" ||
                valueType.rfind("logic<", 0) == 0 || valueType.rfind("u<", 0) == 0;
            if ((width.empty() || width == "1") && !primitive) {
                auto resolvedValueWidth = foldWidth(resolvedTypeWidth(resolveAliasValueType(exprType(*e))));
                if (!resolvedValueWidth.empty()) {
                    width = resolvedValueWidth;
                }
            }
            auto emitted = (!primitive && !width.empty())
                ? "cpphdl::pack_value<" + width + ">(" + emitExpr(*e) + ")"
                : emitNumericExpr(*e);
            auto bitsWidth = emittedBitsCallWidth(emitted);
            if (!bitsWidth.empty()) {
                width = bitsWidth;
            }
            if (emittedOneBitValueExpr(emitted)) {
                width = "1";
            }
            auto castWidth = emittedConcatOperandCastWidth(emitted);
            if (!castWidth.empty() &&
                rangeSelectWidth.empty() &&
                (width.empty() ||
                 (width == "1" && !isBitSelectOperand(*e) && !emittedOneBitValueExpr(emitted)) ||
                 emitted.find("__cpphdl_slice_out") != std::string::npos)) {
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

    std::string emitNumericReplication(const MultipleConcatenationExpressionSyntax& m)
    {
        auto count = emitUntypedNumericExpr(*m.expression);
        const ExpressionSyntax* item = m.concatenation.get();
        if (m.concatenation->kind == SyntaxKind::ConcatenationExpression) {
            auto& c = m.concatenation->as<ConcatenationExpressionSyntax>();
            if (c.expressions.size() == 1) {
                item = c.expressions[0];
            }
        }

        auto width = foldWidth(exprWidth(*item));
        if (width.empty()) {
            width = exprWidth(*item);
        }
        uint64_t value = 0;
        auto parseLiteral = [&](std::string text) -> bool {
            text = trim(std::move(text));
            text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
            auto quote = text.find('\'');
            if (quote != std::string::npos) {
                if (quote > 0 && isNumber(text.substr(0, quote))) {
                    width = text.substr(0, quote);
                }
                auto digits = text.substr(quote + 1);
                if (!digits.empty() && (digits[0] == 's' || digits[0] == 'S')) {
                    digits.erase(digits.begin());
                }
                int base = 10;
                if (!digits.empty()) {
                    char b = digits[0];
                    if (b == 'b' || b == 'B') {
                        base = 2;
                        digits.erase(digits.begin());
                    }
                    else if (b == 'h' || b == 'H') {
                        base = 16;
                        digits.erase(digits.begin());
                    }
                    else if (b == 'o' || b == 'O') {
                        base = 8;
                        digits.erase(digits.begin());
                    }
                    else if (b == 'd' || b == 'D') {
                        base = 10;
                        digits.erase(digits.begin());
                    }
                }
                if (digits.empty()) {
                    return false;
                }
                uint64_t parsed = 0;
                for (char ch : digits) {
                    unsigned digit = 0;
                    if (ch >= '0' && ch <= '9') digit = unsigned(ch - '0');
                    else if (ch >= 'a' && ch <= 'f') digit = unsigned(ch - 'a' + 10);
                    else if (ch >= 'A' && ch <= 'F') digit = unsigned(ch - 'A' + 10);
                    else return false;
                    if (digit >= unsigned(base)) {
                        return false;
                    }
                    parsed = parsed * unsigned(base) + digit;
                }
                value = parsed;
                return true;
            }
            return parseCppIntegralLiteral(text, value);
        };

        if (!parseLiteral(item->toString()) || width.empty()) {
            return "";
        }
        auto maskForWidth = [](const std::string& w) {
            return "(((uint64_t)(" + w + ") >= 64) ? ~0ull : ((1ull << (uint64_t)(" + w + ")) - 1ull))";
        };
        if (value == 0) {
            return "0ull";
        }
        auto valueText = std::to_string(value) + "ull";
        if (width == "1" && (value & 1ull) == 1ull) {
            return maskForWidth(count);
        }
        return "([]() { uint64_t __cpphdl_rep = 0; for (size_t __cpphdl_i = 0; __cpphdl_i < (size_t)(" +
               count + "); ++__cpphdl_i) { __cpphdl_rep = ((__cpphdl_rep << (unsigned)(" + width +
               ")) | (" + valueText + " & " + maskForWidth(width) + ")); } return __cpphdl_rep; }())";
    }

    std::string emitNumericConcat(const ConcatenationExpressionSyntax& c)
    {
        std::vector<std::pair<std::string, std::string>> parts;
        bool numericWidths = true;
        size_t total = 0;
        for (auto e : c.expressions) {
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
            if (!isNumber(width)) {
                numericWidths = false;
            }
            else {
                total += std::stoul(width);
            }
        }
        if (!numericWidths || total >= 64) {
            std::string args;
            for (auto& p : parts) {
                if (!args.empty()) {
                    args += ", ";
                }
                args += "logic<" + p.first + ">(" + p.second + ")";
            }
            return "cat{" + args + "}";
        }
        auto maskForWidth = [](const std::string& w) {
            return "(((uint64_t)(" + w + ") >= 64) ? ~0ull : ((1ull << (uint64_t)(" + w + ")) - 1ull))";
        };
        std::string out = "0ull";
        std::string shift;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            auto term = "((uint64_t)(" + it->second + ") & " + maskForWidth(it->first) + ")";
            if (!shift.empty()) {
                term = "(" + term + " << (unsigned)(" + shift + "))";
            }
            out = "(" + out + " | " + term + ")";
            shift = shift.empty() ? it->first : "(" + shift + ")+(" + it->first + ")";
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
        else if (node.kind == SyntaxKind::ParameterDeclarationStatement) {
            emitParameterDeclarationStatement(node.as<ParameterDeclarationStatementSyntax>(), out, indent);
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
            auto initText = emitForInit(f);
            auto stopText = f.stopExpr ? emitExpr(*f.stopExpr) : std::string();
            auto stepText = emitForStepList(f.steps);
            std::string decrementLoopVar;
            if (f.steps.size() == 1) {
                auto* step = f.steps[0];
                if (PostfixUnaryExpressionSyntax::isKind(step->kind) &&
                    tok(step->as<PostfixUnaryExpressionSyntax>().operatorToken) == "--") {
                    decrementLoopVar = emitLValue(*step->as<PostfixUnaryExpressionSyntax>().operand);
                }
                else if (PrefixUnaryExpressionSyntax::isKind(step->kind) &&
                         tok(step->as<PrefixUnaryExpressionSyntax>().operatorToken) == "--") {
                    decrementLoopVar = emitLValue(*step->as<PrefixUnaryExpressionSyntax>().operand);
                }
            }
            if (!decrementLoopVar.empty() && stopText.find(">=") != std::string::npos) {
                out.push_back(pre + "for (" + initText + ";;) {");
                out.push_back(pre + "    if (!(" + stopText + ")) break;");
                emitStatementBody(*f.statement, out, comb, indent + 1);
                out.push_back(pre + "    if ((uint64_t)(" + decrementLoopVar + ") == 0) break;");
                out.push_back(pre + "    " + stepText + ";");
                out.push_back(pre + "}");
            }
            else {
                out.push_back(pre + "for (" + initText + ";" + stopText + ";" + stepText + ") {");
                emitStatementBody(*f.statement, out, comb, indent + 1);
                out.push_back(pre + "}");
            }
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

    void emitParameterDeclarationStatement(const ParameterDeclarationStatementSyntax& node, std::vector<std::string>& out, int indent)
    {
        if (node.parameter->kind != SyntaxKind::ParameterDeclaration) {
            return;
        }
        auto pre = std::string(indent * 4, ' ');
        auto& pd = node.parameter->as<ParameterDeclarationSyntax>();
        for (auto d : pd.declarators) {
            auto name = tok(d->name);
            auto type = constexprType(varType(*pd.type, *d));
            if (type == "bool") {
                type = "u1";
            }
            if (!localTypeScopes.empty()) {
                localTypeScopes.back()[name] = type;
            }
            auto line = pre + "static constexpr " + type + " " + name;
            if (d->initializer && d->initializer->expr) {
                auto init = emitUntypedNumericExpr(*d->initializer->expr);
                if (type == "bool") {
                    init = "static_cast<bool>(" + init + ")";
                }
                else if (type == "u1") {
                    init = "logic<1>(" + init + ")";
                }
                line += " = " + init;
            }
            else {
                line += " = " + type + "{}";
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

    std::string emitForStep(const ExpressionSyntax& expr)
    {
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto base = assignedBase(*b.left);
            if (!base.empty() && loopVars.count(base) &&
                (isBlockingAssignmentKind(expr.kind) || isCompoundAssignmentKind(expr.kind))) {
                auto lhs = emitLValue(*b.left);
                auto op = isCompoundAssignmentKind(expr.kind) ? compoundOperatorForKind(expr.kind, tok(b.operatorToken)) : "=";
                if (op == "<<=" || op == ">>=") {
                    auto shiftOp = op == "<<=" ? "<<" : ">>";
                    return lhs + " = " + emitUntypedNumericExpr(*b.left) + " " + shiftOp + " (unsigned)(" + emitUntypedNumericExpr(*b.right) + ")";
                }
                if (op == "=") {
                    return lhs + " = " + emitUntypedNumericExpr(*b.right);
                }
                return lhs + " " + op + " " + emitUntypedNumericExpr(*b.right);
            }
        }
        if (PostfixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PostfixUnaryExpressionSyntax>();
            auto base = assignedBase(*u.operand);
            if (!base.empty() && loopVars.count(base)) {
                return emitLValue(*u.operand) + tok(u.operatorToken);
            }
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            auto base = assignedBase(*u.operand);
            if (!base.empty() && loopVars.count(base)) {
                return tok(u.operatorToken) + emitLValue(*u.operand);
            }
        }
        return emitExpr(expr);
    }

    template<typename T>
    std::string emitForStepList(const T& list)
    {
        std::string s;
        for (auto e : list) {
            if (!s.empty()) {
                s += ",";
            }
            s += emitForStep(*e);
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
        auto lhsWidth = usableTemplateLogicWidth(exprWidth(*b.left));
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
                if (binop == "|" || binop == "&" || binop == "^") {
                    if (isNumber(lhsWidth) && std::stoul(lhsWidth) <= 64) {
                        return lhs + " = logic<" + lhsWidth + ">(" + emitNumericExpr(*b.left) + " " + binop + " " + emitNumericExpr(*b.right, rhs) + ")";
                    }
                    return lhs + " = logic<" + lhsWidth + ">(" + logicValueExpr(*b.left, lhsWidth, lhs) + " " + binop + " " + logicValueExpr(*b.right, lhsWidth, rhs) + ")";
                }
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
            if ((!mod || !mod->types.count(base)) && !lhs.empty()) {
                auto textBase = baseFromLValueText(lhs);
                if (mod && mod->types.count(textBase)) {
                    base = textBase;
                }
            }
            auto rhs = emitExpr(*b.right);
            auto designatedAggregateRhs = [](const std::string& text) {
                auto s = trim(text);
                return s.rfind("{.", 0) == 0 || s.rfind("{ .", 0) == 0;
            };
            auto wrapDesignatedAggregate = [&](std::string value) {
                if (!designatedAggregateRhs(value)) {
                    return value;
                }
                std::string targetType;
                if (mod && !lhs.empty()) {
                    targetType = unwrapRegType(expressionStorageType(*mod, lhs));
                }
                if (targetType.empty() && mod && mod->outputPortCppNames.count(base)) {
                    targetType = unwrapRegType(outputStorageType(*mod, base, mod->outputPortCppNames[base]));
                }
                if (targetType.empty() && mod && mod->types.count(base)) {
                    targetType = unwrapRegType(mod->types[base]);
                }
                if (targetType.empty()) {
                    targetType = lookupLocalType(base);
                }
                if (targetType.empty()) {
                    return value;
                }
                return namedAggregateToTypedExpression(targetType, value);
            };
            rhs = wrapDesignatedAggregate(std::move(rhs));
            auto lhsPartSelectWidth = [&]() -> std::string {
                const ElementSelectSyntax* select = nullptr;
                if (b.left->kind == SyntaxKind::ElementSelectExpression) {
                    select = b.left->as<ElementSelectExpressionSyntax>().select;
                }
                else if (b.left->kind == SyntaxKind::IdentifierSelectName) {
                    auto& n = b.left->as<IdentifierSelectNameSyntax>();
                    if (!n.selectors.empty()) {
                        select = n.selectors.back();
                    }
                }
                if (!select || !select->selector || !RangeSelectSyntax::isKind(select->selector->kind)) {
                    return std::string();
                }
                auto width = foldWidth(selectTemplateWidth(*select));
                if (!width.empty()) {
                    return width;
                }
                return selectTemplateWidth(*select);
            }();
            bool rhsConditionalSized = false;
            if (b.right->kind == SyntaxKind::ConditionalExpression && !lhsPartSelectWidth.empty()) {
                rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<" + lhsPartSelectWidth + ">");
                rhsConditionalSized = true;
            }
            else if (b.right->kind == SyntaxKind::ConditionalExpression &&
                b.left->kind == SyntaxKind::ElementSelectExpression) {
                auto& selExpr = b.left->as<ElementSelectExpressionSyntax>();
                auto selectBase = assignedBase(*selExpr.left);
                auto selectBaseType = (mod && !selectBase.empty() && mod->types.count(selectBase)) ?
                    unwrapRegType(mod->types[selectBase]) : std::string();
                auto selectIsMemoryElement = !selectBaseType.empty() &&
                    (memoryLikeType(selectBaseType) || selectBaseType.rfind("array<", 0) == 0 ||
                     selectBaseType.rfind("std::array<", 0) == 0);
                if (selExpr.select && selExpr.select->selector && RangeSelectSyntax::isKind(selExpr.select->selector->kind)) {
                    auto width = foldWidth(selectWidth(*selExpr.select));
                    if (!width.empty()) {
                        rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<" + width + ">");
                        rhsConditionalSized = true;
                    }
                }
                else if (selExpr.select && selExpr.select->selector &&
                    selExpr.select->selector->kind == SyntaxKind::BitSelect && !selectIsMemoryElement) {
                    rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<1>");
                    rhsConditionalSized = true;
                }
            }
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression) {
                auto lhsWidth = foldWidth(exprWidth(*b.left));
                auto lhsType = mod && mod->types.count(base) ? unwrapRegType(mod->types[base]) : lookupLocalType(base);
                if (mod && mod->outputPortCppNames.count(base)) {
                    lhsType = unwrapRegType(outputStorageType(*mod, base, mod->outputPortCppNames[base]));
                }
                auto lhsTypeWidth = foldWidth(typeWidth(lhsType));
                auto lhsIsOneBitScalar = lhsType == "bool" || lhsType == "u1" || lhsType == "reg<u1>" ||
                    ((lhsType.rfind("logic<", 0) == 0 || lhsType.rfind("u<", 0) == 0) && lhsTypeWidth == "1");
                if (lhsWidth == "1" && lhsIsOneBitScalar) {
                    rhs = emitConditionalAsType(b.right->as<ConditionalExpressionSyntax>(), "logic<1>");
                    rhsConditionalSized = true;
                }
            }
            if (!rhsConditionalSized && b.right->kind == SyntaxKind::ConditionalExpression &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
                rhsConditionalSized = true;
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
            auto lvalueStorageType = mod ? unwrapRegType(expressionStorageType(*mod, lhs)) : std::string();
            auto unbasedTargetType = !lvalueStorageType.empty() ? lvalueStorageType : targetStorageType;
            if (lvalueStorageType.empty() && (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos) &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                unbasedTargetType = "std::remove_cvref_t<decltype(" + lhs + ")>";
            }
            if (auto unbasedValue = emitUnbasedUnsizedAssignmentValue(*b.right, unbasedTargetType);
                !unbasedValue.empty()) {
                rhs = unbasedValue;
            }
            if (!lhsPartSelectWidth.empty() && !rhsConditionalSized && !isUnbasedUnsizedLiteralExpr(*b.right, '0') &&
                !isUnbasedUnsizedLiteralExpr(*b.right, '1')) {
                rhs = "logic<" + lhsPartSelectWidth + ">(" + emitNumericExpr(*b.right, rhs) + ")";
            }
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
            if (isZeroLiteralExpr(*b.right) &&
                (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos) &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                if (auto zeroRhs = zeroAssignmentRhsForLValue(lhs); !zeroRhs.empty()) {
                    rhs = zeroRhs;
                }
            }
            auto trimmedRhs = trim(rhs);
            if (!targetStorageType.empty() && trimmedRhs.rfind("{.", 0) == 0) {
                rhs = targetStorageType + trimmedRhs;
            }
            auto unpackSvCast = [](const std::string& text) -> std::pair<std::string, std::string> {
                std::string prefix = "cpphdl::sv_cast<";
                size_t pos = text.rfind(prefix, 0) == 0 ? prefix.size() : std::string::npos;
                if (pos == std::string::npos) {
                    prefix = "sv_cast<";
                    pos = text.rfind(prefix, 0) == 0 ? prefix.size() : std::string::npos;
                }
                if (pos == std::string::npos) {
                    return {};
                }
                int angleDepth = 1;
                size_t typeEnd = pos;
                for (; typeEnd < text.size(); ++typeEnd) {
                    if (text[typeEnd] == '<') {
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
            auto unpackTemplateCall = [](const std::string& text, const std::string& prefix) -> std::pair<std::string, std::string> {
                if (text.rfind(prefix, 0) != 0) {
                    return {};
                }
                size_t pos = prefix.size();
                int angleDepth = 1;
                size_t typeEnd = pos;
                for (; typeEnd < text.size(); ++typeEnd) {
                    if (text[typeEnd] == '<') {
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
            auto packedSourceFromUnpack = [&](const std::string& text) -> std::pair<std::string, std::string> {
                auto outer = unpackTemplateCall(text, "cpphdl::unpack_value<");
                if (outer.first.empty()) {
                    outer = unpackTemplateCall(text, "unpack_value<");
                }
                if (outer.first.empty()) {
                    return {};
                }
                auto inner = unpackTemplateCall(outer.second, "cpphdl::pack_value<");
                if (inner.first.empty()) {
                    inner = unpackTemplateCall(outer.second, "pack_value<");
                }
                if (inner.first.empty()) {
                    return {};
                }
                return {outer.first, inner.second};
            };
            auto assignmentTargetType = [&]() {
                return !lvalueStorageType.empty() ? lvalueStorageType : targetStorageType;
            };
            auto isAggregateTarget = [&](const std::string& type) {
                auto resolved = resolveAliasValueType(type);
                return isAggregateValueType(type) || isAggregateValueType(resolved) ||
                    resolved.rfind("array<", 0) == 0 || resolved.rfind("std::array<", 0) == 0;
            };
            auto repackAggregateForPackedTarget = [&](const std::string& aggregateType, const std::string& value) {
                auto assignTargetType = assignmentTargetType();
                const bool assignTargetPacked = isNumericValueType(assignTargetType) ||
                    isPackedArrayValueType(assignTargetType) ||
                    assignTargetType.rfind("logic<", 0) == 0 ||
                    assignTargetType.rfind("u<", 0) == 0;
                if (!assignTargetType.empty() && assignTargetPacked && isAggregateTarget(aggregateType)) {
                    rhs = "cpphdl::pack_value<cpphdl::type_width<" + assignTargetType + ">()>(" + value + ")";
                    trimmedRhs = trim(rhs);
                    return true;
                }
                return false;
            };
            if (auto cast = unpackSvCast(trimmedRhs); !cast.first.empty()) {
                repackAggregateForPackedTarget(cast.first, cast.second);
            }
            if (auto packed = packedSourceFromUnpack(trimmedRhs); !packed.first.empty()) {
                repackAggregateForPackedTarget(packed.first, packed.second);
            }
            if (!targetStorageType.empty() && isPackedArrayValueType(targetStorageType) &&
                lhs.find('[') == std::string::npos && lhs.find('.') == std::string::npos &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos &&
                !isUnbasedUnsizedLiteralExpr(*b.right, '0') && !isUnbasedUnsizedLiteralExpr(*b.right, '1') &&
                trimmedRhs.rfind("cpphdl::pack_value<", 0) != 0 &&
                trimmedRhs.rfind("cpphdl::unpack_value<", 0) != 0 &&
                trimmedRhs.rfind("cpphdl::sv_cast<", 0) != 0) {
                rhs = "cpphdl::pack_value<cpphdl::type_width<" + targetStorageType + ">()>(" + rhs + ")";
            }
            else if (!targetStorageType.empty() && isUnpackedArrayValueType(targetStorageType) &&
                lhs.find('[') == std::string::npos && lhs.find('.') == std::string::npos &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos &&
                !isUnbasedUnsizedLiteralExpr(*b.right, '0') && !isUnbasedUnsizedLiteralExpr(*b.right, '1') &&
                trimmedRhs.rfind("cpphdl::unpack_value<", 0) != 0 &&
                trimmedRhs.rfind("cpphdl::sv_cast<", 0) != 0) {
                rhs = "cpphdl::unpack_value<" + targetStorageType + ">(cpphdl::pack_value<cpphdl::type_width<" +
                    targetStorageType + ">()>(" + rhs + "))";
            }
            if (((isNonblockingAssignmentKind(expr.kind) && mod->types.count(base)) ||
                 ((!comb && mod->varNames.count(base)) &&
                  sequentialStorageType.rfind("reg<", 0) == 0 &&
                  !memoryLikeType(sequentialStorageType)))) {
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
                return name;
            }
            if (mod->portCppNames.count(name)) {
                return mod->portCppNames[name];
            }
            return name;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto base = tok(n.identifier);
            auto s = mod->outputPortCppNames.count(base) ? base :
                (mod->portCppNames.count(base) ? mod->portCppNames[base] : base);
            auto key = base;
            auto memorySelect = mod->types.count(base) && memoryLikeType(mod->types[base]);
            auto memoryScalar = memorySelect && scalarMemory(mod->types[base]);
            auto currentType = mod->types.count(base) ? unwrapRegType(mod->types[base]) : std::string();
            auto arrayArgsForType = [&](const std::string& type) {
                auto args = templateArgsFor(type, "array");
                if (args.empty()) {
                    args = templateArgsFor(type, "std::array");
                }
                return args;
            };
            size_t arrayLevel = 0;
            for (auto sel : n.selectors) {
                auto baseType = mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
                auto integralBase = loopVars.count(base) || baseType == "bool" ||
                    baseType == "unsigned" || baseType == "u32" || baseType == "uint32_t" ||
                    baseType == "u64" || baseType == "uint64_t";
                if (integralBase && sel->selector && sel->selector->kind == SyntaxKind::BitSelect) {
                    auto index = emitNumericExpr(*sel->selector->as<BitSelectSyntax>().expr);
                    s = "logic<1>((((uint64_t)(" + s + ")) >> (unsigned)(" + index + ")) & 1ull)";
                    key = emitSelectOn(key, *sel, true);
                    currentType.clear();
                    memorySelect = false;
                    memoryScalar = false;
                    continue;
                }
                auto currentArrayArgs = arrayArgsForType(currentType);
                if (!currentArrayArgs.empty() && sel->selector &&
                    sel->selector->kind == SyntaxKind::BitSelect) {
                    auto index = arrayIndexExpr(base, arrayLevel, *sel->selector->as<BitSelectSyntax>().expr);
                    s += "[(unsigned)(" + index + ")]";
                    key = emitSelectOn(key, *sel, true);
                    currentType = currentArrayArgs.size() >= 2 ? currentArrayArgs[0] : std::string();
                    ++arrayLevel;
                    memorySelect = !currentType.empty() && memoryLikeType(currentType);
                    memoryScalar = memorySelect && scalarMemory(currentType);
                    continue;
                }
                currentArrayArgs = arrayArgsForType(currentType);
                if (!currentArrayArgs.empty() && sel->selector &&
                    RangeSelectSyntax::isKind(sel->selector->kind)) {
                    auto& r = sel->selector->as<RangeSelectSyntax>();
                    auto left = arrayIndexExpr(base, arrayLevel, emitIndexExpr(*r.left));
                    auto right = arrayIndexExpr(base, arrayLevel, emitIndexExpr(*r.right));
                    s = emitBitsCall(s, left, right);
                    key = emitSelectOn(key, *sel, true);
                    currentType = currentArrayArgs.size() >= 2 ? currentArrayArgs[0] : std::string();
                    ++arrayLevel;
                    memorySelect = !currentType.empty() && memoryLikeType(currentType);
                    memoryScalar = memorySelect && scalarMemory(currentType);
                    continue;
                }
                s = emitSelectOn(s, *sel, true, memorySelect, memoryScalar);
                key = emitSelectOn(key, *sel, true);
                currentArrayArgs = arrayArgsForType(currentType);
                if (!currentArrayArgs.empty() && sel->selector &&
                    sel->selector->kind == SyntaxKind::BitSelect) {
                    currentType = currentArrayArgs.size() >= 2 ? currentArrayArgs[0] : std::string();
                    ++arrayLevel;
                }
                else {
                    currentType.clear();
                }
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
                auto localType = lookupLocalType(baseName);
                if (e.select && e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect &&
                    (localType == "bool" || localType == "unsigned" || localType == "u32" ||
                     localType == "uint32_t" || localType == "u64" || localType == "uint64_t")) {
                    auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
                    return "logic<1>(((" + emitNumericExpr(*e.left) + ") >> (unsigned)(" + index + ")) & 1ull)";
                }
            }
            auto base = assignedBase(*e.left);
            if (mod->types.count(base) && memoryLikeType(mod->types[base]) && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitMemoryRowAccess(base, emitLValue(*e.left), *e.select->selector->as<BitSelectSyntax>().expr);
            }
            auto baseArrayArgsForType = [&](const std::string& type) {
                auto args = templateArgsFor(type, "array");
                if (args.empty()) {
                    args = templateArgsFor(type, "std::array");
                }
                return args;
            };
            if (mod->types.count(base) && !baseArrayArgsForType(unwrapRegType(mod->types[base])).empty() &&
                e.select && e.select->selector && e.select->selector->kind == SyntaxKind::BitSelect) {
                auto index = arrayIndexExpr(base, 0, *e.select->selector->as<BitSelectSyntax>().expr);
                return emitLValue(*e.left) + "[(unsigned)(" + index + ")]";
            }
            if (mod->types.count(base) && !baseArrayArgsForType(unwrapRegType(mod->types[base])).empty() &&
                e.select && e.select->selector && RangeSelectSyntax::isKind(e.select->selector->kind)) {
                auto& r = e.select->selector->as<RangeSelectSyntax>();
                auto left = arrayIndexExpr(base, 0, emitIndexExpr(*r.left));
                auto right = arrayIndexExpr(base, 0, emitIndexExpr(*r.right));
                return emitBitsCall(emitLValue(*e.left), left, right);
            }
            return emitSelectOn(emitLValue(*e.left), *e.select, false);
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
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            auto& c = expr.as<ConcatenationExpressionSyntax>();
            if (c.expressions.size() == 1) {
                return emitUntypedNumericExpr(*c.expressions[0]);
            }
            return emitNumericExpr(expr);
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto repeated = emitNumericReplication(expr.as<MultipleConcatenationExpressionSyntax>());
            if (!repeated.empty()) {
                return repeated;
            }
            return emitNumericExpr(expr);
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
            if (op == "~") {
                auto width = bitwiseExprWidth(*u.operand);
                if (width.empty()) {
                    width = foldWidth(exprWidth(*u.operand));
                }
                if (!width.empty() && width != "1") {
                    return "(uint64_t)(logic<" + width + ">(~(" + logicValueExpr(*u.operand, width) + ")))";
                }
                return numericBitwiseNotExpr(operand, width);
            }
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
            return callee + (i.arguments ? emitArgumentList(*i.arguments, false, callee) : "()");
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
            while (type.rfind("array<", 0) == 0 || type.rfind("std::array<", 0) == 0) {
                auto args = templateArgsFor(type, type.rfind("std::array<", 0) == 0 ? "std::array" : "array");
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
            while (type.rfind("array<", 0) == 0 || type.rfind("std::array<", 0) == 0) {
                auto args = templateArgsFor(type, type.rfind("std::array<", 0) == 0 ? "std::array" : "array");
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
        if ((base.empty() || (mod && !mod->types.count(base))) && lhs.find('[') != std::string::npos) {
            auto textBase = baseFromLValueText(lhs);
            if (!textBase.empty()) {
                base = textBase;
            }
        }
        auto lvalueBaseType = std::string();
        if (!base.empty() && lhs.find('[') != std::string::npos) {
            auto baseType = mod && mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
            baseType = unwrapRegType(baseType);
            lvalueBaseType = baseType;
            if ((baseType.rfind("logic<", 0) == 0 || baseType.rfind("u<", 0) == 0) &&
                !memoryLikeType(baseType) && baseType.rfind("array<", 0) != 0 &&
                baseType.rfind("std::array<", 0) != 0) {
                return emitConditionalAsType(c, "logic<1>");
            }
            auto lhsWidth = foldWidth(exprWidth(lhsExpr));
            if (lhsWidth == "1" && lhs.find('.') == std::string::npos) {
                return emitConditionalAsType(c, "logic<1>");
            }
        }
        if (lvalueBaseType.empty() && !base.empty()) {
            lvalueBaseType = mod && mod->types.count(base) ? mod->types[base] : lookupLocalType(base);
            lvalueBaseType = unwrapRegType(lvalueBaseType);
        }
        auto lhsType = std::string();
        if (mod && (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos)) {
            lhsType = expressionStorageType(*mod, lhs);
        }
        if (lhsType.empty()) {
            lhsType = exprType(lhsExpr);
        }
        if (lhsType.empty() && mod) {
            lhsType = expressionStorageType(*mod, lhs);
        }
        if (lhsType.empty() && mod && lhs.find('.') == std::string::npos) {
            lhsType = lvalueBaseType;
        }
        if (!lhsType.empty() && (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos)) {
            return emitConditionalAsType(c, lhsType);
        }
        if (lhsType.empty() && lhs.find('.') != std::string::npos) {
            return emitConditionalAsDecltype(c, lhs);
        }
        auto width = foldWidth(exprWidth(lhsExpr));
        auto baseTypeWidth = foldWidth(typeWidth(lvalueBaseType));
        auto widthIsKnownTarget = lvalueBaseType.empty() || baseTypeWidth == width ||
            lvalueBaseType.rfind("logic<", 0) == 0 || lvalueBaseType.rfind("u<", 0) == 0 ||
            lvalueBaseType == "bool" || lvalueBaseType == "u1" || lvalueBaseType == "reg<u1>";
        if (isNumber(width) && widthIsKnownTarget) {
            return emitConditionalAsType(c, "logic<" + width + ">");
        }
        if (!lhsType.empty()) {
            auto remaining = lhs;
            while ((lhsType.rfind("array<", 0) == 0 || lhsType.rfind("std::array<", 0) == 0) &&
                   remaining.find('[') != std::string::npos) {
                auto args = templateArgsFor(lhsType, lhsType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
                if (args.size() < 2) {
                    break;
                }
                lhsType = args[0];
                remaining = remaining.substr(remaining.find('[') + 1);
            }
        }
        if (!lhsType.empty()) {
            return emitConditionalAsType(c, lhsType);
        }
        return emitConditionalAsDecltype(c, lhs);
    }

    std::string emitConditionalAsDecltype(const ConditionalExpressionSyntax& c, const std::string& lhs)
    {
        auto targetType = "std::remove_cvref_t<decltype(" + lhs + ")>";
        auto leadingTemplateCallType = [](const std::string& value) -> std::string {
            auto v = trim(value);
            auto findTypeStart = [&](const std::string& prefix) -> size_t {
                for (size_t pos = 0; (pos = v.find(prefix, pos)) != std::string::npos; ++pos) {
                    auto topLevelCtor = true;
                    for (size_t i = 0; i < pos; ++i) {
                        if (!std::isspace(static_cast<unsigned char>(v[i])) && v[i] != '(') {
                            topLevelCtor = false;
                            break;
                        }
                    }
                    if (topLevelCtor && (pos == 0 || (!std::isalnum(static_cast<unsigned char>(v[pos - 1])) && v[pos - 1] != '_' && v[pos - 1] != ':'))) {
                        return pos;
                    }
                }
                return std::string::npos;
            };
            auto start = std::min({findTypeStart("logic<"), findTypeStart("u<"), findTypeStart("array<")});
            if (start == std::string::npos) {
                return {};
            }
            v = v.substr(start);
            auto open = v.find('<');
            int depth = 0;
            for (size_t i = open; i < v.size(); ++i) {
                if (v[i] == '<' && i + 1 < v.size() && v[i + 1] == '<') {
                    ++i;
                }
                else if (v[i] == '>' && i + 1 < v.size() && v[i + 1] == '>') {
                    ++i;
                }
                else if (v[i] == '<') {
                    ++depth;
                }
                else if (v[i] == '>') {
                    --depth;
                    if (depth == 0) {
                        auto type = v.substr(0, i + 1);
                        auto rest = trim(v.substr(i + 1));
                        return !rest.empty() && rest.front() == '(' ? type : std::string();
                    }
                }
            }
            return {};
        };
        std::function<std::string(const ExpressionSyntax&)> explicitBranchType;
        explicitBranchType = [&](const ExpressionSyntax& branch) -> std::string {
            auto expr = &branch;
            while (expr->kind == SyntaxKind::ParenthesizedExpression) {
                expr = expr->as<ParenthesizedExpressionSyntax>().expression;
            }
            if (expr->kind == SyntaxKind::ConditionalExpression) {
                return explicitBranchType(*expr->as<ConditionalExpressionSyntax>().left);
            }
            return leadingTemplateCallType(emitExpr(*expr));
        };
        if (auto explicitType = explicitBranchType(*c.left); !explicitType.empty()) {
            targetType = explicitType;
        }
        else if (auto explicitType = explicitBranchType(*c.right); !explicitType.empty()) {
            targetType = explicitType;
        }
        auto emitBranch = [&](const ExpressionSyntax& branch) -> std::string {
            auto expr = &branch;
            while (expr->kind == SyntaxKind::ParenthesizedExpression) {
                expr = expr->as<ParenthesizedExpressionSyntax>().expression;
            }
            if (expr->kind == SyntaxKind::ConditionalExpression) {
                return "(" + emitConditionalAsDecltype(expr->as<ConditionalExpressionSyntax>(), lhs) + ")";
            }
            auto emitted = emitExpr(*expr);
            if (!leadingTemplateCallType(emitted).empty()) {
                return emitted;
            }
            if (isZeroLiteralExpr(*expr)) {
                return targetType + "{}";
            }
            return "cpphdl::sv_cast<" + targetType + ">(" + emitted + ")";
        };
        return emitPredicate(*c.predicate) + " ? " + emitBranch(*c.left) + " : " + emitBranch(*c.right);
    }

    std::string emitConditionalAsType(const ConditionalExpressionSyntax& c, const std::string& targetType)
    {
        auto numericTarget = isNumericValueType(targetType);
        auto emitBranch = [&](const ExpressionSyntax& branch) -> std::string {
            auto expr = &branch;
            while (expr->kind == SyntaxKind::ParenthesizedExpression) {
                expr = expr->as<ParenthesizedExpressionSyntax>().expression;
            }
            if (expr->kind == SyntaxKind::ConditionalExpression) {
                return "(" + emitConditionalAsType(expr->as<ConditionalExpressionSyntax>(), targetType) + ")";
            }
            if (auto unbasedValue = emitUnbasedUnsizedAssignmentValue(*expr, targetType); !unbasedValue.empty()) {
                return unbasedValue;
            }
            if (numericTarget) {
                if (targetType.rfind("logic<", 0) == 0 || targetType.rfind("u<", 0) == 0) {
                    return targetType + "(" + emitExpr(*expr) + ")";
                }
                return targetType + "(" + emitNumericExpr(*expr) + ")";
            }
            if (isZeroLiteralExpr(*expr)) {
                return targetType + "{}";
            }
            return "cpphdl::sv_cast<" + targetType + ">(" + emitExpr(*expr) + ")";
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
        if (currentType.empty() && mod && mod->outputPortCppNames.count(base)) {
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
        for (size_t idx = 0; idx + 1 < n.selectors.size(); ++idx) {
            auto sel = n.selectors[idx];
            auto arrayArgs = arrayArgsForType(currentType);
            if (!arrayArgs.empty() && sel->selector && sel->selector->kind == SyntaxKind::BitSelect) {
                auto index = arrayIndexExpr(base, arrayLevel, *sel->selector->as<BitSelectSyntax>().expr);
                s += "[(unsigned)(" + index + ")]";
                currentType = arrayArgs.size() >= 2 ? resolveAliasType(arrayArgs[0]) : std::string();
                ++arrayLevel;
            }
            else {
                if (sel->selector && sel->selector->kind == SyntaxKind::BitSelect &&
                    mod && mod->typeParamNames.count(currentType)) {
                    auto index = emitNumericExpr(*sel->selector->as<BitSelectSyntax>().expr);
                    s += "[(unsigned)(" + index + ")]";
                    currentType = "std::remove_cvref_t<decltype(" + s + ")>";
                }
                else {
                    s = emitSelectOn(s, *sel, false, memorySelect, memoryScalar);
                    currentType.clear();
                }
            }
            memorySelect = !currentType.empty() && memoryLikeType(currentType);
            memoryScalar = memorySelect && scalarMemory(currentType);
        }
        if (last->selector->kind == SyntaxKind::BitSelect) {
            auto arrayArgs = arrayArgsForType(currentType);
            if (!arrayArgs.empty()) {
                auto index = arrayIndexExpr(base, arrayLevel, emitNumericExpr(*last->selector->as<BitSelectSyntax>().expr));
                return "(uint64_t)(" + s + "[(unsigned)(" + index + ")])";
            }
            auto currentTypePrimitive = currentType.empty() || currentType == "bool" ||
                currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" ||
                currentType == "u64" || currentType == "uint64_t" ||
                currentType.rfind("logic<", 0) == 0 || currentType.rfind("u<", 0) == 0;
            if (!currentTypePrimitive) {
                auto index = emitNumericExpr(*last->selector->as<BitSelectSyntax>().expr);
                return "(uint64_t)(" + s + "[(unsigned)(" + index + ")])";
            }
            auto index = emitNumericExpr(*last->selector->as<BitSelectSyntax>().expr);
            auto localBaseType = lookupLocalType(base);
            bool unknownSingleSelectScalar = currentType.empty() && n.selectors.size() == 1 &&
                (!mod || (!mod->portCppNames.count(base) && !mod->outputPortCppNames.count(base) && !isAssignDrivenVar(*mod, base)));
            if (loopVars.count(base) || unknownSingleSelectScalar || currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" || currentType == "u64" || currentType == "uint64_t" ||
                localBaseType == "unsigned" || localBaseType == "u32" || localBaseType == "uint32_t" || localBaseType == "u64" || localBaseType == "uint64_t") {
                return "(((uint64_t)(" + s + ") >> (unsigned)(" + index + ")) & 1ull)";
            }
            return "(uint64_t)(logic<1>(" + s + "[" + bitIndexArg(index) + "]))";
        }
        if (RangeSelectSyntax::isKind(last->selector->kind)) {
            auto& r = last->selector->as<RangeSelectSyntax>();
            auto bounds = indexedRangeBounds(r);
            auto arrayArgs = arrayArgsForType(currentType);
            if (!arrayArgs.empty()) {
                if (arrayArgs.size() >= 2) {
                    auto elemWidth = foldWidth(resolvedTypeWidth(arrayArgs[0]));
                    auto count = foldWidth(selectTemplateWidth(*last));
                    if (!elemWidth.empty() && !count.empty()) {
                        auto width = foldWidth("(" + elemWidth + ") * (" + count + ")");
                        auto first = arrayIndexExpr(base, arrayLevel, emitNumericExpr(*r.right));
                        auto slice = emitArraySliceExpr(s, count, first);
                        return "(uint64_t)(logic<" + width + ">(" + slice + "))";
                    }
                }
            }
            auto localBaseType = lookupLocalType(base);
            bool unknownSingleRangeScalar = currentType.empty() && n.selectors.size() == 1 &&
                (!mod || (!mod->portCppNames.count(base) && !mod->outputPortCppNames.count(base) && !isAssignDrivenVar(*mod, base)));
            if (loopVars.count(base) || unknownSingleRangeScalar || currentType == "unsigned" || currentType == "u32" || currentType == "uint32_t" || currentType == "u64" || currentType == "uint64_t" ||
                localBaseType == "unsigned" || localBaseType == "u32" || localBaseType == "uint32_t" || localBaseType == "u64" || localBaseType == "uint64_t") {
                auto value = "(((uint64_t)(" + s + ")) >> (unsigned)(" + bounds.second + "))";
                auto width = foldWidth(selectWidth(*last));
                if (isNumber(width) && std::stoul(width) < 64) {
                    value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
                }
                return value;
            }
            auto width = selectTemplateWidth(*last);
            if (currentType.find("decltype(") != std::string::npos) {
                return "(uint64_t)(logic<" + width + ">(logic<cpphdl::type_width<" + currentType + ">()>(" +
                       s + ").bits(" + bounds.first + "," + bounds.second + ")))";
            }
            return "(uint64_t)(logic<" + width + ">(" + s + ".bits(" + bounds.first + "," + bounds.second + ")))";
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
        auto leftType = unwrapRegType(exprType(*e.left));
        if (!base.empty()) {
            integralBase = loopVars.count(base) || (mod && mod->types.count(base) &&
                (mod->types[base] == "bool" || mod->types[base] == "unsigned" || mod->types[base] == "u32" ||
                 mod->types[base] == "uint32_t" || mod->types[base] == "u64" ||
                 mod->types[base] == "uint64_t"));
            if (!integralBase && !constantType(base).empty()) {
                integralBase = true;
            }
        }
        if (!integralBase &&
            (leftType == "bool" || leftType == "unsigned" || leftType == "u32" || leftType == "uint32_t" ||
             leftType == "u64" || leftType == "uint64_t")) {
            integralBase = true;
        }
        auto bounds = indexedRangeBounds(r);
        if (!base.empty() && mod && mod->types.count(base)) {
            auto type = unwrapRegType(mod->types[base]);
            auto args = templateArgsFor(type, "array");
            if (args.empty()) {
                args = templateArgsFor(type, "std::array");
            }
            if (args.size() >= 2) {
                auto elemWidth = foldWidth(resolvedTypeWidth(args[0]));
                auto count = foldWidth(selectTemplateWidth(*e.select));
                if (!elemWidth.empty() && !count.empty()) {
                    auto width = foldWidth("(" + elemWidth + ") * (" + count + ")");
                    auto first = arrayIndexExpr(base, 0, emitNumericExpr(*r.right));
                    auto slice = emitArraySliceExpr(emitExpr(*e.left), count, first);
                    return "(uint64_t)(logic<" + width + ">(" + slice + "))";
                }
            }
        }
        if (!base.empty() && mod && mod->outputPortCppNames.count(base)) {
            auto type = unwrapRegType(outputStorageType(*mod, base, mod->outputPortCppNames[base]));
            auto args = templateArgsFor(resolveAliasValueType(type), "array");
            if (args.empty()) {
                args = templateArgsFor(resolveAliasValueType(type), "std::array");
            }
            if (args.size() >= 2) {
                auto elemWidth = foldWidth(resolvedTypeWidth(args[0]));
                auto count = foldWidth(selectTemplateWidth(*e.select));
                if (!elemWidth.empty() && !count.empty()) {
                    auto width = foldWidth("(" + elemWidth + ") * (" + count + ")");
                    auto first = arrayIndexExpr(base, 0, emitNumericExpr(*r.right));
                    auto slice = emitArraySliceExpr(emitExpr(*e.left), count, first);
                    return "(uint64_t)(logic<" + width + ">(" + slice + "))";
                }
            }
        }
        if (integralBase) {
            auto value = "(((uint64_t)(" + emitNumericExpr(*e.left) + ")) >> (unsigned)(" + bounds.second + "))";
            auto width = foldWidth(selectWidth(*e.select));
            if (isNumber(width) && std::stoul(width) < 64) {
                value = "(" + value + " & ((1ull << " + width + ") - 1ull))";
            }
            return value;
        }
        auto width = selectTemplateWidth(*e.select);
        return "(uint64_t)(logic<" + width + ">(" + emitExpr(*e.left) + ".bits(" + bounds.first + "," + bounds.second + ")))";
    }

    std::string emitNumericBitSelectExpr(const ElementSelectExpressionSyntax& e)
    {
        if (!e.select || !e.select->selector || e.select->selector->kind != SyntaxKind::BitSelect) {
            return "";
        }
        auto base = assignedBase(*e.left);
        if (base.empty()) {
            return "";
        }
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
        auto baseType = resolveAliasType(exprType(*e.left));
        if (baseType.empty()) {
            baseType = resolveAliasType(mod && mod->types.count(base) ? mod->types[base] : lookupLocalType(base));
        }
        if (baseType == "bool" || baseType == "unsigned" || baseType == "u32" ||
            baseType == "uint32_t" || baseType == "u64" || baseType == "uint64_t") {
            auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
            return "(((uint64_t)(" + emitNumericExpr(*e.left) + ") >> (unsigned)(" + index + ")) & 1ull)";
        }
        if (arrayArgsForType(baseType).empty()) {
            auto baseIsPrimitive = baseType.empty() || baseType == "bool" || baseType == "unsigned" ||
                baseType == "u32" || baseType == "uint32_t" || baseType == "u64" ||
                baseType == "uint64_t" || baseType.rfind("logic<", 0) == 0 || baseType.rfind("u<", 0) == 0;
            if (!baseIsPrimitive) {
                auto index = emitNumericExpr(*e.select->selector->as<BitSelectSyntax>().expr);
                return "(uint64_t)(" + emitExpr(*e.left) + "[(unsigned)(" + index + ")])";
            }
            return "";
        }
        auto index = arrayIndexExpr(base, 0, *e.select->selector->as<BitSelectSyntax>().expr);
        return "(uint64_t)(" + emitExpr(*e.left) + "[(unsigned)(" + index + ")])";
    }

    std::string emitNumericExpr(const ExpressionSyntax& expr, const std::string& emitted = "")
    {
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            auto rawCallee = invocationCalleeRaw(i);
            if (rawCallee == "$signed" || rawCallee == "$unsigned") {
                return emitSystemSignednessCast(i, rawCallee);
            }
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto numericSelect = emitNumericIdentifierSelectExpr(expr.as<IdentifierSelectNameSyntax>());
            if (!numericSelect.empty()) {
                return numericSelect;
            }
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto numericBit = emitNumericBitSelectExpr(expr.as<ElementSelectExpressionSyntax>());
            if (!numericBit.empty()) {
                return numericBit;
            }
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
        if (expr.kind == SyntaxKind::ConcatenationExpression) {
            return emitNumericConcat(expr.as<ConcatenationExpressionSyntax>());
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto repeated = emitNumericReplication(expr.as<MultipleConcatenationExpressionSyntax>());
            if (!repeated.empty()) {
                return repeated;
            }
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
                auto width = bitwiseExprWidth(expr);
                if (!width.empty() && width != "1") {
                    return "(uint64_t)(logic<" + width + ">(" + logicValueExpr(*b.left, width) +
                           " " + op + " " + logicValueExpr(*b.right, width) + "))";
                }
                return "(" + emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right) + ")";
            }
            if (op == "<<" || op == ">>") {
                return "(" + emitNumericExpr(*b.left) + " " + op + " (unsigned)(" + emitNumericExpr(*b.right) + "))";
            }
        }
        auto text = dropExtraTrailingClosingParens(emitted.empty() ? emitExpr(expr) : emitted);
        auto width = foldWidth(exprWidth(expr));
        if (auto packed = packedNumericOperandExpr(expr, text); !packed.empty()) {
            return packed;
        }
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

    Project project;
    std::vector<ModuleGen> modules;
    ModuleGen* mod = nullptr;
    std::set<std::string> loopVars;
    std::vector<std::map<std::string, std::string>> localTypeScopes;
    std::vector<std::set<std::string>> generateLocalNameScopes;
    std::vector<std::map<std::string, std::string>> generateLocalExprScopes;

    std::string lookupLocalType(const std::string& name) const
    {
        for (auto it = localTypeScopes.rbegin(); it != localTypeScopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return {};
    }

    void pushGenerateLocalScope()
    {
        generateLocalNameScopes.emplace_back();
        generateLocalExprScopes.emplace_back();
    }

    void popGenerateLocalScope()
    {
        if (!generateLocalNameScopes.empty()) {
            generateLocalNameScopes.pop_back();
        }
        if (!generateLocalExprScopes.empty()) {
            generateLocalExprScopes.pop_back();
        }
    }

    void markGenerateLocalName(const std::string& name)
    {
        if (!name.empty() && !generateLocalNameScopes.empty()) {
            generateLocalNameScopes.back().insert(name);
        }
    }

    bool isGenerateLocalName(const std::string& name) const
    {
        for (auto it = generateLocalNameScopes.rbegin(); it != generateLocalNameScopes.rend(); ++it) {
            if (it->count(name)) {
                return true;
            }
        }
        return false;
    }

    std::string generateLocalDefaultExpr(const std::string& name) const
    {
        if (mod) {
            auto found = mod->types.find(name);
            if (found != mod->types.end()) {
                return unwrapRegType(found->second) + "{}";
            }
        }
        return "logic<1>{}";
    }

    std::string generateLocalGuardExpr(const std::vector<std::string>& headers) const
    {
        std::vector<std::string> guards;
        for (auto header : headers) {
            header = trim(std::move(header));
            if (header.rfind("if constexpr", 0) != 0) {
                continue;
            }
            auto open = header.find('(');
            auto close = header.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open) {
                continue;
            }
            guards.push_back(trim(header.substr(open + 1, close - open - 1)));
        }
        if (guards.empty()) {
            return {};
        }
        std::string out;
        for (size_t n = 0; n < guards.size(); ++n) {
            if (n) {
                out += " && ";
            }
            out += "(" + guards[n] + ")";
        }
        return out;
    }

    void storeGenerateLocalExpr(const std::string& name, const std::string& expr, const std::string& guard = "")
    {
        if (name.empty() || generateLocalExprScopes.empty()) {
            return;
        }
        auto guardedExpr = [&](const std::string& previous) {
            if (guard.empty()) {
                return expr;
            }
            return "([&]() { if constexpr (" + guard + ") { return " + expr +
                   "; } else { return " + previous + "; } }())";
        };
        for (size_t n = generateLocalNameScopes.size(); n > 0; --n) {
            if (generateLocalNameScopes[n - 1].count(name)) {
                auto found = generateLocalExprScopes[n - 1].find(name);
                auto previous = found == generateLocalExprScopes[n - 1].end() ?
                    generateLocalDefaultExpr(name) : found->second;
                generateLocalExprScopes[n - 1][name] = guardedExpr(previous);
                return;
            }
        }
        auto found = generateLocalExprScopes.back().find(name);
        auto previous = found == generateLocalExprScopes.back().end() ?
            generateLocalDefaultExpr(name) : found->second;
        generateLocalExprScopes.back()[name] = guardedExpr(previous);
    }

    std::optional<std::string> lookupGenerateLocalExpr(const std::string& name) const
    {
        for (auto it = generateLocalExprScopes.rbegin(); it != generateLocalExprScopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    ModuleGen* findModule(const std::string& name)
    {
        auto normalizedName = trim(name);
        if (normalizedName.rfind("::", 0) == 0) {
            normalizedName = normalizedName.substr(2);
        }
        for (auto& candidate : modules) {
            if (candidate.name == normalizedName) {
                return &candidate;
            }
        }
        auto base = normalizedName;
        auto lt = base.find('<');
        if (lt != std::string::npos) {
            base = trim(base.substr(0, lt));
            if (base.rfind("::", 0) == 0) {
                base = base.substr(2);
            }
            for (auto& candidate : modules) {
                if (candidate.name == base) {
                    return &candidate;
                }
            }
        }
        return nullptr;
    }

    bool moduleTypeHasTemplateParams(const std::string& type)
    {
        auto* child = findModule(type);
        auto configured = configuredModuleParams(type);
        return (child && !child->params.empty()) || !configured.empty();
    }

    std::string moduleMemberType(const std::string& type, const std::string& params)
    {
        auto cppType = type.find("::") == std::string::npos ? "::" + type : type;
        if (!params.empty()) {
            return cppType + "<" + params + ">";
        }
        return moduleTypeHasTemplateParams(type) ? cppType + "<>" : cppType;
    }

    std::string templateBaseName(std::string type) const
    {
        type = trim(std::move(type));
        if (type.rfind("::", 0) == 0) {
            type = type.substr(2);
        }
        auto lt = type.find('<');
        if (lt != std::string::npos) {
            type = trim(type.substr(0, lt));
        }
        auto scope = type.rfind("::");
        if (scope != std::string::npos) {
            type = type.substr(scope + 2);
        }
        return type;
    }

    std::vector<std::string> templateArgsForType(const std::string& type) const
    {
        auto base = templateBaseName(type);
        if (base.empty()) {
            return {};
        }
        auto args = templateArgsFor(type, base);
        if (args.empty()) {
            args = templateArgsFor(type, "::" + base);
        }
        return args;
    }

    std::string memberStorageType(const std::string& name) const
    {
        if (!mod) {
            return {};
        }
        auto typed = mod->types.find(name);
        if (typed != mod->types.end()) {
            return typed->second;
        }
        for (size_t i = 0; i < mod->memberNames.size() && i < mod->memberTypes.size(); ++i) {
            if (mod->memberNames[i] == name) {
                return mod->memberTypes[i];
            }
        }
        return {};
    }

    void rememberMemberType(const std::string& name, const std::string& type)
    {
        if (!mod || name.empty() || type.empty()) {
            return;
        }
        mod->types[name] = type;
    }

    static std::string normalizedParamKey(std::string name)
    {
        std::string out;
        for (char c : name) {
            if (c == '_') {
                continue;
            }
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    }

    std::string matchingModuleParamName(const ModuleGen& target, const std::string& name) const
    {
        auto key = normalizedParamKey(name);
        for (const auto& existing : target.params) {
            auto existingName = templateParamName(existing);
            if (existingName == name) {
                return existingName;
            }
        }
        for (const auto& existing : target.params) {
            auto existingName = templateParamName(existing);
            if (!existingName.empty() && normalizedParamKey(existingName) == key) {
                return existingName;
            }
        }
        return {};
    }

    void ensureModuleParamsFromInterfacePort(ModuleGen& target, const std::vector<std::string>& declaredParams)
    {
        for (const auto& declared : declaredParams) {
            auto name = templateParamName(declared);
            if (name.empty()) {
                continue;
            }
            if (matchingModuleParamName(target, name).empty()) {
                target.params.push_back(declared);
            }
        }
    }

    std::string inferInstanceParamsFromInterfaceConnections(const std::string& type,
                                                            const HierarchicalInstanceSyntax& instance)
    {
        auto* child = findModule(type);
        auto configuredParams = configuredModuleParams(type);
        const auto& declParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
        if (declParams.empty()) {
            return {};
        }
        std::map<std::string, std::string> inferred;
        for (auto conn : instance.connections) {
            if (conn->kind != SyntaxKind::NamedPortConnection) {
                continue;
            }
            auto& named = conn->as<NamedPortConnectionSyntax>();
            if (!named.expr) {
                continue;
            }
            auto portName = tok(named.name);
            const PortGen* childPort = nullptr;
            std::string childPortType;
            bool childPortIsInterface = false;
            if (child) {
                for (const auto& port : child->ports) {
                    if (port.name == portName || child->portCppNames.count(portName) && child->portCppNames.at(portName) == port.name) {
                        childPort = &port;
                        childPortType = port.type;
                        childPortIsInterface = port.isInterface;
                        break;
                    }
                }
            }
            if (!childPort) {
                auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES");
                auto specIt = portTypes.find(type + "." + portName);
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(type + "." + portName + "_in");
                }
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(type + "." + portName + "_out");
                }
                if (specIt != portTypes.end()) {
                    auto spec = specIt->second;
                    auto sep = spec.find(':');
                    auto direction = trim(sep == std::string::npos ? std::string() : spec.substr(0, sep));
                    childPortType = trim(sep == std::string::npos ? spec : spec.substr(sep + 1));
                    auto base = templateBaseName(childPortType);
                    childPortIsInterface = direction.empty() && !base.empty() && !configuredModuleParams(base).empty();
                }
            }
            if (childPortType.empty() || !childPortIsInterface) {
                continue;
            }
            auto expr = propertyExpr(*named.expr);
            if (!expr) {
                continue;
            }
            auto connectedBase = assignedBase(*expr);
            auto connectedType = !connectedBase.empty() ? memberStorageType(connectedBase) : std::string();
            if (connectedType.empty()) {
                connectedType = exprType(*expr);
            }
            if (connectedType.empty()) {
                continue;
            }
            auto formalBase = templateBaseName(childPortType);
            auto actualBase = templateBaseName(connectedType);
            if (formalBase.empty() || formalBase != actualBase) {
                continue;
            }
            auto formalArgs = templateArgsForType(childPortType);
            auto actualArgs = templateArgsForType(connectedType);
            if (formalArgs.empty() || actualArgs.empty() || formalArgs.size() != actualArgs.size()) {
                continue;
            }
            for (size_t i = 0; i < formalArgs.size(); ++i) {
                auto formal = trim(formalArgs[i]);
                if (formal.empty()) {
                    continue;
                }
                for (const auto& declared : declParams) {
                    if (templateParamName(declared) == formal) {
                        inferred[formal] = trim(actualArgs[i]);
                        break;
                    }
                }
            }
        }
        if (inferred.empty()) {
            return {};
        }
        std::string params;
        int lastNeeded = -1;
        std::vector<std::string> names;
        names.reserve(declParams.size());
        for (int i = 0; i < static_cast<int>(declParams.size()); ++i) {
            auto name = templateParamName(declParams[i]);
            names.push_back(name);
            if (inferred.count(name)) {
                lastNeeded = i;
            }
        }
        for (int i = 0; i <= lastNeeded; ++i) {
            auto value = inferred.count(names[i]) ? inferred[names[i]] : templateParamDefault(declParams[i]);
            if (value.empty()) {
                return {};
            }
            if (!params.empty()) {
                params += ",";
            }
            params += castTemplateParamValue(declParams[i], value);
        }
        return params;
    }

    std::vector<PackedFieldInfo> packedHelperFields(const StructUnionTypeSyntax& st,
                                                    const std::vector<PackedFieldInfo>& fields) const
    {
        auto ordered = fields;
        if (st.toString().find("packed") != std::string::npos && tok(st.keyword) != "union") {
            std::reverse(ordered.begin(), ordered.end());
        }
        return ordered;
    }

    bool isPackedAggregate(const StructUnionTypeSyntax& st) const
    {
        return st.toString().find("packed") != std::string::npos;
    }

    bool isPackedUnion(const StructUnionTypeSyntax& st) const
    {
        return isPackedAggregate(st) && tok(st.keyword) == "union";
    }

    std::string cppAggregateKeyword(const StructUnionTypeSyntax& st) const
    {
        return isPackedUnion(st) ? "struct" : (tok(st.keyword) == "union" ? "union" : "struct");
    }

    std::string usingOverrideTarget(const std::string& typeName, const std::string& decl) const
    {
        auto text = trim(decl);
        if (text.rfind("using ", 0) != 0) {
            return {};
        }
        text = trim(text.substr(std::strlen("using ")));
        auto eq = text.find('=');
        if (eq == std::string::npos) {
            return {};
        }
        auto lhs = trim(text.substr(0, eq));
        auto rhs = trim(text.substr(eq + 1));
        if (!rhs.empty() && rhs.back() == ';') {
            rhs.pop_back();
            rhs = trim(rhs);
        }
        if (!typeName.empty() && lhs != typeName) {
            return {};
        }
        return rhs;
    }

    void registerUsingOverrideAlias(ModuleGen& targetMod, const std::string& typeName,
                                    const std::string& decl)
    {
        auto aliasTarget = usingOverrideTarget(typeName, decl);
        if (aliasTarget.empty()) {
            return;
        }
        targetMod.types[typeName] = aliasTarget;
        auto savedMod = mod;
        mod = &targetMod;
        auto width = typeWidth(aliasTarget);
        mod = savedMod;
        if (!width.empty()) {
            targetMod.typeWidths[typeName] = width;
        }
    }

    void registerUsingOverrideAliases(ModuleGen& targetMod,
                                      const std::map<std::string, std::string>& overrides)
    {
        auto prefix = targetMod.name + ".";
        for (const auto& item : overrides) {
            if (item.first.rfind(prefix, 0) != 0) {
                continue;
            }
            auto typeName = item.first.substr(prefix.size());
            registerUsingOverrideAlias(targetMod, typeName, item.second);
        }
    }

    std::vector<PackedFieldInfo> packedFieldsFromOverrideDecl(const std::string& decl)
    {
        std::vector<PackedFieldInfo> fields;
        std::string body = decl;
        auto open = body.find('{');
        auto close = body.rfind('}');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            body = body.substr(open + 1, close - open - 1);
        }
        std::stringstream ss(body);
        std::string line;
        while (std::getline(ss, line)) {
            line = trim(line);
            if (line.empty() || line == "@PACKED@" || line.rfind("//", 0) == 0 ||
                line.rfind("struct ", 0) == 0 || line.rfind("union ", 0) == 0 ||
                line.find(';') == std::string::npos) {
                continue;
            }
            line = trim(line.substr(0, line.find(';')));
            auto space = line.find_last_of(" \t");
            if (space == std::string::npos) {
                continue;
            }
            auto fieldType = trim(line.substr(0, space));
            auto fieldName = cppIdent(trim(line.substr(space + 1)));
            if (fieldName.empty()) {
                continue;
            }
            auto bracket = fieldName.find('[');
            if (bracket != std::string::npos) {
                fieldName = fieldName.substr(0, bracket);
            }
            auto directFieldWidth = typeWidth(fieldType);
            auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
            if (!fieldWidth.empty()) {
                fields.push_back({fieldName, fieldWidth, packedFieldUsesPack(fieldType)});
            }
        }
        std::reverse(fields.begin(), fields.end());
        return fields;
    }

    std::string packedOverrideHelpers(const std::string& typeName, const std::string& decl)
    {
        auto fields = packedFieldsFromOverrideDecl(decl);
        if (fields.empty()) {
            return packedAggregateHelpers(typeName);
        }
        return packedAggregateHelpers(typeName, joinedPackedWidth(fields), fields, false);
    }

    bool generateBranchCanBeMethodGuarded(const SyntaxNode* node) const
    {
        if (!node) {
            return true;
        }
        switch (node->kind) {
        case SyntaxKind::GenerateBlock:
            for (auto member : node->as<GenerateBlockSyntax>().members) {
                if (!generateBranchCanBeMethodGuarded(member)) {
                    return false;
                }
            }
            return true;
        case SyntaxKind::LoopGenerate:
            return generateBranchCanBeMethodGuarded(node->as<LoopGenerateSyntax>().block.get());
        case SyntaxKind::IfGenerate: {
            auto& ifGen = node->as<IfGenerateSyntax>();
            if (!generateBranchCanBeMethodGuarded(ifGen.block.get())) {
                return false;
            }
            return !ifGen.elseClause || !ifGen.elseClause->clause ||
                   generateBranchCanBeMethodGuarded(ifGen.elseClause->clause.get());
        }
        case SyntaxKind::ContinuousAssign:
        case SyntaxKind::TypedefDeclaration:
        case SyntaxKind::ParameterDeclarationStatement:
        case SyntaxKind::AlwaysBlock:
        case SyntaxKind::AlwaysCombBlock:
        case SyntaxKind::AlwaysFFBlock:
        case SyntaxKind::AlwaysLatchBlock:
            return true;
        default:
            return false;
        }
    }

    bool generateConditionNeedsRuntimeGuard(const std::string& cond) const
    {
        static constexpr const char* vars[] = {"i", "j", "k", "m", "z_gen", "w_gen"};
        for (auto* var : vars) {
            if (isIdentifierUsed(cond, var)) {
                return true;
            }
        }
        if (cond.find("logic<") != std::string::npos ||
            cond.find("_func()") != std::string::npos ||
            cond.find(".bits(") != std::string::npos) {
            return true;
        }
        return false;
    }

    void collectSequentialAssignedBases(const SyntaxNode& node, std::set<std::string>& found)
    {
        if (node.kind == SyntaxKind::AlwaysBlock ||
            node.kind == SyntaxKind::AlwaysCombBlock ||
            node.kind == SyntaxKind::AlwaysFFBlock ||
            node.kind == SyntaxKind::AlwaysLatchBlock) {
            auto& proc = node.as<ProceduralBlockSyntax>();
            bool comb = tok(proc.keyword).find("comb") != std::string::npos;
            if (proc.statement->kind == SyntaxKind::TimingControlStatement) {
                auto& t = proc.statement->as<TimingControlStatementSyntax>();
                comb = comb || t.timingControl->kind == SyntaxKind::ImplicitEventControl;
            }
            if (!comb) {
                collectAssignedBases(*proc.statement, found);
            }
            return;
        }
        if (node.kind == SyntaxKind::GenerateRegion) {
            for (auto member : node.as<GenerateRegionSyntax>().members) {
                collectSequentialAssignedBases(*member, found);
            }
            return;
        }
        if (node.kind == SyntaxKind::GenerateBlock) {
            for (auto member : node.as<GenerateBlockSyntax>().members) {
                collectSequentialAssignedBases(*member, found);
            }
            return;
        }
        if (node.kind == SyntaxKind::LoopGenerate) {
            collectSequentialAssignedBases(*node.as<LoopGenerateSyntax>().block, found);
            return;
        }
        if (node.kind == SyntaxKind::IfGenerate) {
            auto& ifGen = node.as<IfGenerateSyntax>();
            if (mod) {
                if (auto decision = evalConfiguredGenerateCondition(*mod, emitExpr(*ifGen.condition))) {
                    if (*decision) {
                        collectSequentialAssignedBases(*ifGen.block, found);
                    }
                    else if (ifGen.elseClause && ifGen.elseClause->clause) {
                        collectSequentialAssignedBases(*ifGen.elseClause->clause, found);
                    }
                    return;
                }
            }
            collectSequentialAssignedBases(*ifGen.block, found);
            if (ifGen.elseClause && ifGen.elseClause->clause) {
                collectSequentialAssignedBases(*ifGen.elseClause->clause, found);
            }
        }
    }

    std::string generateBranchGuard(const std::string& cond) const
    {
        return std::string(generateConditionNeedsRuntimeGuard(cond) ? "if (" : "if constexpr (") + cond + ") {";
    }

    std::string generateInverseBranchGuard(const std::string& cond) const
    {
        return std::string(generateConditionNeedsRuntimeGuard(cond) ? "if (!(" : "if constexpr (!(") + cond + ")) {";
    }

    void registerTypeField(const std::string& typeName, const std::string& fieldName, const std::string& fieldType)
    {
        if (!mod || typeName.empty() || fieldName.empty() || fieldType.empty()) {
            return;
        }
        mod->typeFields[typeName][fieldName] = fieldType;
        auto& order = mod->typeFieldOrder[typeName];
        if (std::find(order.begin(), order.end(), fieldName) == order.end()) {
            order.push_back(fieldName);
        }
        if (mod->isPackage) {
            mod->typeFields[mod->name + "::" + typeName][fieldName] = fieldType;
            auto& qualifiedOrder = mod->typeFieldOrder[mod->name + "::" + typeName];
            if (std::find(qualifiedOrder.begin(), qualifiedOrder.end(), fieldName) == qualifiedOrder.end()) {
                qualifiedOrder.push_back(fieldName);
            }
        }
    }

    template<typename T>
    void recordArrayLowerBounds(const std::string& name, const T& dimensions)
    {
        if (!mod || name.empty()) {
            return;
        }
        auto bounds = dimensionLowerBounds(dimensions);
        if (!bounds.empty()) {
            mod->arrayLowerBounds[name] = bounds;
        }
    }

    std::string unwrappedValueType(std::string type)
    {
        type = trim(std::move(type));
        bool changed = true;
        while (changed) {
            changed = false;
            if (type.rfind("reg<", 0) == 0 && type.back() == '>') {
                type = trim(type.substr(4, type.size() - 5));
                changed = true;
                continue;
            }
            auto arrayArgs = templateArgsFor(type, "array");
            if (arrayArgs.empty()) {
                arrayArgs = templateArgsFor(type, "std::array");
            }
            if (arrayArgs.size() >= 2) {
                type = arrayArgs[0];
                changed = true;
                continue;
            }
        }
        return type;
    }

    std::string fieldTypeFor(std::string parentType, const std::string& field)
    {
        parentType = unwrappedValueType(std::move(parentType));
        if (parentType.empty()) {
            return "";
        }
        auto findInModule = [&](ModuleGen& m, const std::string& type) -> std::string {
            auto it = m.typeFields.find(type);
            if (it != m.typeFields.end()) {
                auto fit = it->second.find(field);
                if (fit != it->second.end()) {
                    return fit->second;
                }
            }
            auto alias = m.types.find(type);
            if (alias != m.types.end() && alias->second != type) {
                auto ait = m.typeFields.find(alias->second);
                if (ait != m.typeFields.end()) {
                    auto fit = ait->second.find(field);
                    if (fit != ait->second.end()) {
                        return fit->second;
                    }
                }
            }
            return "";
        };
        auto sep = parentType.rfind("::");
        if (sep != std::string::npos) {
            auto pkg = parentType.substr(0, sep);
            if (auto* m = findModule(pkg)) {
                auto found = findInModule(*m, parentType);
                if (!found.empty()) {
                    return found;
                }
                return findInModule(*m, parentType.substr(sep + 2));
            }
        }
        if (mod) {
            auto found = findInModule(*mod, parentType);
            if (!found.empty()) {
                return found;
            }
            for (auto& import : mod->imports) {
                if (auto* m = findModule(import)) {
                    found = findInModule(*m, parentType);
                    if (!found.empty()) {
                        return found;
                    }
                    found = findInModule(*m, import + "::" + parentType);
                    if (!found.empty()) {
                        return found;
                    }
                }
            }
        }
        for (auto& candidate : modules) {
            auto found = findInModule(candidate, parentType);
            if (!found.empty()) {
                return found;
            }
        }
        return "";
    }

    bool typeHasRegisteredFields(const std::string& type)
    {
        return !fieldOrderFor(type).empty();
    }

    std::string configTypeName(std::string type)
    {
        type = trim(std::move(type));
        const std::string typenamePrefix = "typename ";
        if (type.rfind(typenamePrefix, 0) == 0) {
            type = trim(type.substr(typenamePrefix.size()));
        }
        auto args = templateArgsFor(type, "array");
        if (args.empty()) {
            args = templateArgsFor(type, "std::array");
        }
        if (!args.empty()) {
            type = trim(args[0]);
        }
        return type;
    }

    bool configuredAddressablePackedArrayElement(std::string type)
    {
        type = configTypeName(std::move(type));
        if (type.empty()) {
            return false;
        }
        if (configuredNameEquals("HDLCPP_ADDRESSABLE_PACKED_ARRAY_TYPES", type)) {
            return true;
        }
        auto sep = type.rfind("::");
        if (sep != std::string::npos &&
            configuredNameEquals("HDLCPP_ADDRESSABLE_PACKED_ARRAY_TYPES", type.substr(sep + 2))) {
            return true;
        }
        return false;
    }

    std::string arrayTypeForDimension(const std::string& elemType, const std::string& width, bool packed)
    {
        auto baseType = configTypeName(elemType);
        const bool typeParamElement = mod && mod->typeParamNames.count(baseType);
        if (packed && !typeHasRegisteredFields(elemType) &&
            !typeParamElement &&
            !configuredAddressablePackedArrayElement(elemType)) {
            return "array<" + elemType + "," + width + ",true>";
        }
        return "array<" + elemType + "," + width + ">";
    }

    std::vector<std::string> fieldOrderFor(std::string parentType)
    {
        parentType = unwrappedValueType(std::move(parentType));
        if (parentType.empty()) {
            return {};
        }
        auto findInModule = [&](ModuleGen& m, const std::string& type) -> std::vector<std::string> {
            auto it = m.typeFieldOrder.find(type);
            if (it != m.typeFieldOrder.end()) {
                return it->second;
            }
            auto alias = m.types.find(type);
            if (alias != m.types.end() && alias->second != type) {
                auto ait = m.typeFieldOrder.find(alias->second);
                if (ait != m.typeFieldOrder.end()) {
                    return ait->second;
                }
            }
            return {};
        };
        auto sep = parentType.rfind("::");
        if (sep != std::string::npos) {
            auto pkg = parentType.substr(0, sep);
            if (auto* m = findModule(pkg)) {
                auto found = findInModule(*m, parentType);
                if (!found.empty()) {
                    return found;
                }
                return findInModule(*m, parentType.substr(sep + 2));
            }
        }
        if (mod) {
            auto found = findInModule(*mod, parentType);
            if (!found.empty()) {
                return found;
            }
            for (auto& import : mod->imports) {
                if (auto* m = findModule(import)) {
                    found = findInModule(*m, parentType);
                    if (!found.empty()) {
                        return found;
                    }
                    found = findInModule(*m, import + "::" + parentType);
                    if (!found.empty()) {
                        return found;
                    }
                }
            }
        }
        for (auto& candidate : modules) {
            auto found = findInModule(candidate, parentType);
            if (!found.empty()) {
                return found;
            }
        }
        return {};
    }

    std::string namedAggregateToPositional(const std::string& type, const std::string& init)
    {
        std::vector<std::pair<std::string, std::string>> entries;
        if (!parseNamedAggregateEntries(init, entries)) {
            return init;
        }
        auto order = fieldOrderFor(type);
        if (order.empty()) {
            return namedAggregateToConstexprLambda(type, init, true);
        }
        std::map<std::string, std::string> byName;
        for (auto& entry : entries) {
            byName[entry.first] = entry.second;
        }
        std::string out = "{ ";
        bool emitted = false;
        for (const auto& field : order) {
            auto it = byName.find(field);
            if (it == byName.end()) {
                out += emitted ? ", {}" : "{}";
            }
            else {
                if (emitted) {
                    out += ", ";
                }
                out += it->second.empty() ? "{}" : it->second;
            }
            emitted = true;
        }
        out += " }";
        return out;
    }

    std::string namedAggregateToTypedExpression(const std::string& type, const std::string& init)
    {
        auto converted = namedAggregateToPositional(type, init);
        auto trimmed = trim(converted);
        if (trimmed.rfind("[]", 0) == 0 || trimmed.rfind("[&]", 0) == 0) {
            return converted;
        }
        return type + converted;
    }

    std::string constexprAggregateInit(std::string type, const std::string& init)
    {
        std::vector<std::pair<std::string, std::string>> entries;
        if (!parseNamedAggregateEntries(init, entries)) {
            return init;
        }
        bool changed = false;
        std::string out = "{ ";
        for (size_t i = 0; i < entries.size(); ++i) {
            auto field = entries[i].first;
            auto value = entries[i].second;
            auto fieldType = fieldTypeFor(type, field);
            auto fieldWidth = typeWidth(fieldType);
            auto ctype = constexprType(fieldType);
            if ((fieldType.rfind("logic<", 0) == 0 || ctype == "unsigned" || ctype == "uint64_t" || !fieldWidth.empty()) &&
                value.find("logic<") != std::string::npos) {
                value = wrapLogicCastsForConstexprNumeric(std::move(value));
                changed = true;
            }
            if (i != 0) {
                out += ", ";
            }
            out += "." + field + " = " + value;
        }
        out += " }";
        return changed ? out : init;
    }

    const ExpressionSyntax* propertyExpr(const PropertyExprSyntax& prop)
    {
        if (prop.kind == SyntaxKind::ParenthesizedPropertyExpr) {
            auto& paren = prop.as<ParenthesizedPropertyExprSyntax>();
            return propertyExpr(*paren.expr);
        }
        if (prop.kind == SyntaxKind::SimplePropertyExpr) {
            auto& simpleProp = prop.as<SimplePropertyExprSyntax>();
            const SequenceExprSyntax* seq = simpleProp.expr;
            while (seq && seq->kind == SyntaxKind::ParenthesizedSequenceExpr) {
                seq = seq->as<ParenthesizedSequenceExprSyntax>().expr;
            }
            if (seq && seq->kind == SyntaxKind::SimpleSequenceExpr) {
                return seq->as<SimpleSequenceExprSyntax>().expr;
            }
        }
        return nullptr;
    }

    void handle(const ModuleDeclarationSyntax& node)
    {
        modules.push_back({});
        mod = &modules.back();
        mod->name = tok(node.header->name);
        mod->isPackage = node.kind == SyntaxKind::PackageDeclaration;
        mod->isInterface = node.kind == SyntaxKind::InterfaceDeclaration;
        for (auto import : node.header->imports) {
            for (auto item : import->items) {
                auto pkg = tok(item->package);
                if (!pkg.empty()) {
                    mod->imports.push_back(pkg);
                }
            }
        }

        project.modules.push_back({});
        project.modules.back().name = mod->name;

        if (node.header->parameters) {
            for (auto p : node.header->parameters->declarations) {
                if (p->kind == SyntaxKind::ParameterDeclaration) {
                    auto& pd = p->as<ParameterDeclarationSyntax>();
                    for (auto d : pd.declarators) {
                        auto name = tok(d->name);
                        auto init = d->initializer ? cppExprText(d->initializer->toString()).substr(1) : "";
                        auto rawParamType = trim(exprText(pd.type->toString()));
                        auto type = constexprType(varType(*pd.type, *d));
                        if (rawParamType.find("unsigned") == std::string::npos) {
                            if (rawParamType == "int" || rawParamType == "integer" ||
                                rawParamType == "signed int" || rawParamType == "signed integer") {
                                type = "int";
                            }
                            else if (rawParamType == "longint" || rawParamType == "signed longint") {
                                type = "int64_t";
                            }
                        }
                        auto rawInit = d->initializer ? trim(d->initializer->toString()) : "";
                        if ((!rawInit.empty() && rawInit.front() == '"') ||
                            (!init.empty() && init.find("hdlcpp_fixed_string") != std::string::npos)) {
                            type = "hdlcpp_fixed_string";
                        }
                        if (type == "unsigned" || type == "uint64_t") {
                            init = stripLogicLiteralCasts(std::move(init));
                        }
                        if (name == "INTERRUPTS") {
                            init = type + "{}";
                        }
                        if (configuredNameEquals("HDLCPP_SKIP_PARAMS", mod->name + "." + name)) {
                            continue;
                        }
                        auto trackedType = type;
                        if (trackedType.rfind("std::array<", 0) == 0 && trackedType.back() == '>') {
                            trackedType = "array<" + trackedType.substr(std::strlen("std::array<"));
                        }
                        mod->types[name] = trackedType;
                        if (!init.empty()) {
                            init = trim(init);
                            init = replaceSystemBitsOfKnownTypes(std::move(init), "cpphdl");
                            if (init.find('?') != std::string::npos && init.front() != '(') {
                                init = "(" + init + ")";
                            }
                        }
	                        mod->params.push_back(type + " " + name + (init.empty() ? "" : " = " + init));
	                    }
	                }
                else if (p->kind == SyntaxKind::TypeParameterDeclaration) {
                    auto& td = p->as<TypeParameterDeclarationSyntax>();
                    auto localTypeModules = configuredNameSet("HDLCPP_LOCAL_TYPE_MODULES");
                    auto localTypeNames = configuredNameSet("HDLCPP_LOCAL_TYPE_NAMES");
                    auto typeDeclOverrides = configuredTextMap("HDLCPP_TYPE_DECL_OVERRIDES");
                    registerUsingOverrideAliases(*mod, typeDeclOverrides);
                    auto typeParamDefaults = configuredTextMap("HDLCPP_TYPE_PARAM_DEFAULTS");
                    for (auto d : td.declarators) {
                        auto name = tok(d->name);
                        auto overrideIt = typeDeclOverrides.find(mod->name + "." + name);
                        if (overrideIt != typeDeclOverrides.end() && d->assignment) {
                            auto decl = overrideIt->second;
                            auto aliasTarget = usingOverrideTarget(name, decl);
                            if (!aliasTarget.empty()) {
                                registerUsingOverrideAlias(*mod, name, decl);
                            }
                            else {
                                auto overrideFields = packedFieldsFromOverrideDecl(decl);
                                replaceAll(decl, "@PACKED@", overrideFields.empty()
                                    ? packedAggregateHelpers(name)
                                    : packedAggregateHelpers(name, joinedPackedWidth(overrideFields), overrideFields, false));
                                mod->types[name] = name;
                                if (!overrideFields.empty()) {
                                    mod->typeWidths[name] = joinedPackedWidth(overrideFields);
                                }
                            }
                            mod->typeDecls.push_back(decl);
                            continue;
                        }
                        bool localparamType = tok(td.keyword) == "localparam";
                        bool makeLocalType = (localparamType ||
                                              (localTypeModules.count(mod->name) && localTypeNames.count(name))) &&
                                             d->assignment;
                        if (makeLocalType) {
                            mod->types[name] = name;
	                            if (d->assignment->type->kind == SyntaxKind::StructType ||
	                                d->assignment->type->kind == SyntaxKind::UnionType) {
	                                auto& st = d->assignment->type->as<StructUnionTypeSyntax>();
	                                std::string line = cppAggregateKeyword(st) + " " + name + " {\n";
	                                std::vector<PackedFieldInfo> fieldWidths;
	                                std::vector<std::string> fieldLines;
	                                for (auto member : st.members) {
	                                    for (auto md : member->declarators) {
	                                        if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
	                                auto& nested = member->type->as<StructUnionTypeSyntax>();
		                                line += std::string("    ") + cppAggregateKeyword(nested) + " { ";
                                for (auto nestedMember : nested.members) {
                                    for (auto nd : nestedMember->declarators) {
                                        line += varType(*nestedMember->type, *nd) + " " + cppIdent(tok(nd->name)) + "; ";
                                    }
                                }
                                line += "} " + cppIdent(tok(md->name)) + ";\n";
	                            }
	                            else {
	                                auto fieldType = varType(*member->type, *md);
	                                registerTypeField(name, tok(md->name), fieldType);
	                                auto directFieldWidth = typeWidth(fieldType);
	                                auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
	                                if (!fieldWidth.empty()) {
	                                    fieldWidths.push_back({cppIdent(tok(md->name)), fieldWidth, packedFieldUsesPack(fieldType)});
	                                }
	                                fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(md->name)) + ";\n");
	                }
	            }
		        }
		                                auto packedWidth = isPackedUnion(st) ? packedUnionWidth(fieldWidths) : joinedPackedWidth(fieldWidths);
	                                for (auto& fieldLine : fieldLines) {
	                                    line += fieldLine;
	                                }
	                                auto helperFields = packedHelperFields(st, fieldWidths);
	                                line += packedAggregateHelpers(name, packedWidth, helperFields, isPackedUnion(st));
	                                line += "};";
	                                if (!packedWidth.empty()) {
	                                    mod->typeWidths[name] = packedWidth;
	                                }
	                                mod->typeDecls.push_back(line);
                            }
                            else {
                                auto initType = d->assignment && d->assignment->type ? typeText(*d->assignment->type) :
                                    cppTypeFromSvText(d->assignment->toString());
                                if (initType == "logic") {
                                    initType = "bool";
                                }
                                mod->typeDecls.push_back("using " + name + " = " + initType + ";");
                                mod->types[name] = initType;
                                if (configuredNameEquals("HDLCPP_FALSE_CONSTANT_TYPES", mod->name + "." + name)) {
                                    mod->constants.push_back({"acc_cfg_t", "AccCfg = false"});
                                }
                                auto initWidth = typeWidth(initType);
                                if (!initWidth.empty()) {
                                    mod->typeWidths[name] = initWidth;
                                }
                            }
                            continue;
                        }
                        auto init = d->assignment ? trim(cppTypeFromSvText(d->assignment->toString())) : "bool";
                        auto assignmentText = d->assignment ? d->assignment->toString() : std::string();
                        auto defaultIt = typeParamDefaults.find(name);
                        if (d->assignment && d->assignment->type &&
                            (d->assignment->type->kind == SyntaxKind::StructType ||
                             d->assignment->type->kind == SyntaxKind::UnionType)) {
                            auto& st = d->assignment->type->as<StructUnionTypeSyntax>();
                            auto defaultName = mod->name + "_" + name + "_default_t";
                            std::string line;
                            if (!mod->params.empty()) {
                                line += "template<";
                                for (size_t i = 0; i < mod->params.size(); ++i) {
                                    line += (i ? ", " : "") + mod->params[i];
                                }
                                line += ">\n";
                            }
                            line += cppAggregateKeyword(st) + " " + defaultName + " {\n";
                            std::vector<PackedFieldInfo> fieldWidths;
                            std::vector<std::string> fieldLines;
                            for (auto member : st.members) {
                                for (auto md : member->declarators) {
                                    if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
                                        auto& nested = member->type->as<StructUnionTypeSyntax>();
                                        std::string fieldLine = std::string("    ") + cppAggregateKeyword(nested) + " { ";
                                        for (auto nestedMember : nested.members) {
                                            for (auto nd : nestedMember->declarators) {
                                                fieldLine += varType(*nestedMember->type, *nd) + " " + cppIdent(tok(nd->name)) + "; ";
                                            }
                                        }
                                        fieldLine += "} " + cppIdent(tok(md->name)) + ";\n";
                                        fieldLines.push_back(fieldLine);
                                    }
                                    else {
                                        auto fieldType = varType(*member->type, *md);
                                        registerTypeField(defaultName, tok(md->name), fieldType);
                                        auto directFieldWidth = typeWidth(fieldType);
                                        auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
                                        if (!fieldWidth.empty()) {
                                            fieldWidths.push_back({cppIdent(tok(md->name)), fieldWidth, packedFieldUsesPack(fieldType)});
                                        }
                                        fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(md->name)) + ";\n");
                                    }
                                }
                            }
                            auto packedWidth = isPackedUnion(st) ? packedUnionWidth(fieldWidths) : joinedPackedWidth(fieldWidths);
                            for (auto& fieldLine : fieldLines) {
                                line += fieldLine;
                            }
                            auto helperFields = packedHelperFields(st, fieldWidths);
                            line += packedAggregateHelpers(defaultName, packedWidth, helperFields, isPackedUnion(st));
                            line += "};\n";
                            mod->preClassDecls.push_back(line);
                            if (!packedWidth.empty()) {
                                mod->typeWidths[defaultName] = packedWidth;
                            }
                            std::string args;
                            for (auto& declared : mod->params) {
                                if (!args.empty()) {
                                    args += ",";
                                }
                                args += templateParamName(declared);
                            }
                            init = defaultName + (args.empty() ? "" : "<" + args + ">");
                        }
                        else if (defaultIt != typeParamDefaults.end() &&
                            (assignmentText.empty() || assignmentText.find("struct") != std::string::npos || init == "bool")) {
                            init = defaultIt->second;
                        }
                        else if (init == "logic") {
                            init = "bool";
                        }
                        mod->params.push_back("typename " + name + " = " + init);
                        mod->typeParamNames.insert(name);
                        mod->types[name] = name;
                    }
                }
	            }
	        }
	        auto configuredParams = configuredModuleParams(mod->name);
	        if (!configuredParams.empty()) {
	            mod->params = configuredParams;
	        }

	        if (node.header->ports && node.header->ports->kind == SyntaxKind::AnsiPortList) {
            auto& ports = node.header->ports->as<AnsiPortListSyntax>();
            for (auto p : ports.ports) {
                p->visit(*this);
            }
        }

        for (auto member : node.members) {
            collectSequentialAssignedBases(*member, mod->seqAssignedVars);
        }

        for (auto member : node.members) {
            member->visit(*this);
        }

        mod = nullptr;
    }

    void handle(const PackageImportDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto item : node.items) {
            auto pkg = tok(item->package);
            if (!pkg.empty() &&
                std::find(mod->imports.begin(), mod->imports.end(), pkg) == mod->imports.end()) {
                mod->imports.push_back(pkg);
            }
        }
    }

    void handle(const ImplicitAnsiPortSyntax& node)
    {
        if (!mod) {
            return;
        }
        PortGen port;
        port.name = tok(node.declarator->name);
        port.array = listText(node.declarator->dimensions);
        if (node.header->kind == SyntaxKind::NetPortHeader) {
            auto& h = node.header->as<NetPortHeaderSyntax>();
            port.direction = tok(h.direction);
            port.type = port.array.empty() ? typeText(*h.dataType) : varType(*h.dataType, *node.declarator);
        }
        else if (node.header->kind == SyntaxKind::VariablePortHeader) {
            auto& h = node.header->as<VariablePortHeaderSyntax>();
            port.direction = tok(h.direction);
            port.type = port.array.empty() ? typeText(*h.dataType) : varType(*h.dataType, *node.declarator);
        }
        auto interfacePortType = [&]() -> std::string {
            auto raw = trim(node.toString());
            {
                std::stringstream in(raw);
                std::string cleaned;
                std::string rawLine;
                while (std::getline(in, rawLine)) {
                    auto comment = rawLine.find("//");
                    if (comment != std::string::npos) {
                        rawLine = rawLine.substr(0, comment);
                    }
                    if (!cleaned.empty()) {
                        cleaned += "\n";
                    }
                    cleaned += rawLine;
                }
                raw = trim(cleaned);
            }
            if (!raw.empty() && raw.back() == ',') {
                raw.pop_back();
                raw = trim(raw);
            }
            const auto name = tok(node.declarator->name);
            auto namePos = raw.rfind(name);
            if (name.empty() || namePos == std::string::npos) {
                return {};
            }
            auto head = trim(raw.substr(0, namePos));
            if (head.empty()) {
                return {};
            }
            std::string typeToken;
            for (char c : head) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    typeToken.push_back(c);
                }
            }
            if (std::getenv("HDLCPP_DEBUG_INTERFACE_PORTS")) {
                std::cerr << "interface-port-candidate module=" << mod->name
                          << " raw=[" << raw << "]"
                          << " name=[" << name << "]"
                          << " head=[" << head << "]"
                          << " typeToken=[" << typeToken << "]\n";
            }
            auto dot = typeToken.rfind('.');
            if (dot == std::string::npos) {
                return {};
            }
            auto ifaceBegin = dot;
            while (ifaceBegin > 0 &&
                   (std::isalnum(static_cast<unsigned char>(typeToken[ifaceBegin - 1])) ||
                    typeToken[ifaceBegin - 1] == '_')) {
                --ifaceBegin;
            }
            auto ifaceName = trim(typeToken.substr(ifaceBegin, dot - ifaceBegin));
            if (ifaceName.empty()) {
                return {};
            }
            auto* iface = findModule(ifaceName);
            auto configuredParams = configuredModuleParams(ifaceName);
            if (std::getenv("HDLCPP_DEBUG_INTERFACE_PORTS")) {
                std::cerr << "interface-port-resolve module=" << mod->name
                          << " port=[" << name << "]"
                          << " iface=[" << ifaceName << "]"
                          << " local=" << (iface ? "1" : "0")
                          << " configured=" << configuredParams.size() << "\n";
            }
            if ((!iface || !iface->isInterface) && configuredParams.empty()) {
                return {};
            }
            std::vector<std::string> args;
            const auto& declaredParams = iface ? iface->params : configuredParams;
            ensureModuleParamsFromInterfacePort(*mod, declaredParams);
            for (const auto& declared : declaredParams) {
                auto paramName = templateParamName(declared);
                if (paramName.empty()) {
                    continue;
                }
                auto matchedName = matchingModuleParamName(*mod, paramName);
                args.push_back(matchedName.empty() ? paramName : matchedName);
            }
            auto type = moduleMemberType(ifaceName, [&]() {
                std::string joined;
                for (const auto& arg : args) {
                    if (!joined.empty()) {
                        joined += ",";
                    }
                    joined += arg;
                }
                return joined;
            }());
            return type;
        }();
        if (!interfacePortType.empty()) {
            port.type = interfacePortType;
            port.isInterface = true;
            auto dims = dimensionWidths(node.declarator->dimensions);
            for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
                port.type = "array<" + port.type + "," + *it + ">";
            }
            port.array.clear();
        }
        if (!port.array.empty()) {
            port.array.clear();
        }
        auto svName = port.name;
        if (isClockPortName(svName) || svName == "reset") {
            return;
        }
        auto cppName = svName;
        if (port.direction == "output") {
            if (!hasSuffix(cppName, "_out")) {
                cppName += "_out";
            }
        }
        else if (port.direction == "input") {
            if (!hasSuffix(cppName, "_in")) {
                cppName += "_in";
            }
        }
        port.name = cppName;
        if (port.direction == "output") {
            auto backingType = port.type;
            if (port.type.rfind("reg<", 0) == 0 && port.type.back() == '>') {
                port.type = port.type.substr(4, port.type.size() - 5);
                if (port.type == "u1") {
                    port.type = "bool";
                }
            }
            mod->outputRegTypes[cppName] = backingType;
            mod->outputPortCppNames[svName] = cppName;
        }
        mod->portNames.insert(svName);
        mod->portCppNames[svName] = cppName;
        mod->types[svName] = port.type;
        mod->types[cppName] = port.type;
        recordArrayLowerBounds(svName, node.declarator->dimensions);
        if (cppName != svName && mod->arrayLowerBounds.count(svName)) {
            mod->arrayLowerBounds[cppName] = mod->arrayLowerBounds[svName];
        }
        mod->ports.push_back(port);
    }

    void handle(const DataDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        auto declarationText = node.toString();
        if (declarationText.find("automatic ") != std::string::npos ||
            declarationText.rfind("automatic", 0) == 0) {
            return;
        }
        if (node.type && node.type->kind == SyntaxKind::EnumType) {
            unsigned value = 0;
            for (auto member : node.type->as<EnumTypeSyntax>().members) {
                auto name = tok(member->name);
                auto init = std::to_string(value);
                if (member->initializer) {
                    init = cppExprText(member->initializer->toString());
                    if (!init.empty() && init.front() == '=') {
                        init.erase(init.begin());
                    }
                    init = trim(stripLogicLiteralCasts(init));
                }
                if (!mod->types.count(name)) {
                    mod->constants.push_back({"unsigned", name + " = " + init});
                    mod->types[name] = "unsigned";
                }
                ++value;
            }
        }
        std::string anonymousStructType;
        if ((node.type->kind == SyntaxKind::StructType || node.type->kind == SyntaxKind::UnionType) && !node.declarators.empty()) {
            auto& st = node.type->as<StructUnionTypeSyntax>();
            auto firstName = tok(node.declarators[0]->name);
            anonymousStructType = firstName + "_t";
	            std::string line = cppAggregateKeyword(st) + " " + anonymousStructType + " {\n";
	            std::vector<PackedFieldInfo> fieldWidths;
	            std::vector<std::string> fieldLines;
	            for (auto member : st.members) {
                for (auto d : member->declarators) {
                    if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
                        auto& nested = member->type->as<StructUnionTypeSyntax>();
	                        line += std::string("    ") + cppAggregateKeyword(nested) + " { ";
                        for (auto nestedMember : nested.members) {
                            for (auto nd : nestedMember->declarators) {
                                line += varType(*nestedMember->type, *nd) + " " + cppIdent(tok(nd->name)) + "; ";
                            }
                        }
                        line += "} " + cppIdent(tok(d->name)) + ";\n";
                    }
	                    else {
	                        auto fieldType = varType(*member->type, *d);
	                        registerTypeField(anonymousStructType, tok(d->name), fieldType);
	                        auto directFieldWidth = typeWidth(fieldType);
	                        auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
	                        if (!fieldWidth.empty()) {
	                            fieldWidths.push_back({cppIdent(tok(d->name)), fieldWidth, packedFieldUsesPack(fieldType)});
	                        }
		                        fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(d->name)) + ";\n");
		                    }
		                }
		            }
		            auto packedWidth = isPackedUnion(st) ? packedUnionWidth(fieldWidths) : joinedPackedWidth(fieldWidths);
	            for (auto& fieldLine : fieldLines) {
	                line += fieldLine;
	            }
	            auto helperFields = packedHelperFields(st, fieldWidths);
		            line += packedAggregateHelpers(anonymousStructType, packedWidth, helperFields, isPackedUnion(st));
            line += "};";
            mod->typeDecls.push_back(line);
            mod->types[anonymousStructType] = anonymousStructType;
        }
        for (auto d : node.declarators) {
            auto svName = tok(d->name);
            auto name = cppIdent(svName);
            auto type = anonymousStructType.empty() ? varType(*node.type, *d) : anonymousStructType;
            if (!anonymousStructType.empty()) {
                auto& st = node.type->as<StructUnionTypeSyntax>();
                auto structDims = dimensionWidths(st.dimensions);
                for (auto it = structDims.rbegin(); it != structDims.rend(); ++it) {
                    type = "array<" + type + "," + *it + ">";
                }
                auto declDims = dimensionWidths(d->dimensions);
                for (auto it = declDims.rbegin(); it != declDims.rend(); ++it) {
                    type = "array<" + type + "," + *it + ">";
                }
            }
            if (mod->seqAssignedVars.count(svName)) {
                type = regTypeFor(type);
            }
            mod->vars.push_back({type, name});
            mod->varNames.insert(name);
            mod->types[name] = type;
            if (name != svName) {
                mod->wireMap[svName] = name;
                mod->types[svName] = type;
            }
            recordArrayLowerBounds(name, d->dimensions);
        }
    }

    void handle(const NetDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto d : node.declarators) {
            auto svName = tok(d->name);
            auto name = cppIdent(svName);
            auto type = varType(*node.type, *d);
            if (node.type->kind == SyntaxKind::ImplicitType) {
                auto& implicit = node.type->as<ImplicitTypeSyntax>();
                if (dimensionWidths(implicit.dimensions).empty()) {
                    type = "unsigned";
                }
            }
            if (mod->seqAssignedVars.count(svName)) {
                type = regTypeFor(type);
            }
            mod->vars.push_back({type, name});
            mod->varNames.insert(name);
            mod->types[name] = type;
            if (name != svName) {
                mod->wireMap[svName] = name;
                mod->types[svName] = type;
            }
            recordArrayLowerBounds(name, d->dimensions);
        }
    }

    void handle(const ParameterDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto d : node.declarators) {
            auto name = tok(d->name);
            auto type = varType(*node.type, *d);
            auto ctype = constexprType(type);
            auto init = d->initializer ? cppExprText(d->initializer->toString()).substr(1) : "{}";
            if (d->initializer && d->initializer->expr &&
                (ctype == "unsigned" || ctype == "uint64_t" || type.rfind("logic<", 0) == 0)) {
                init = emitUntypedNumericExpr(*d->initializer->expr);
            }
            if (!init.empty() && trim(init).front() == '"') {
                init = "0";
            }
            init = replaceSystemBitsOfKnownTypes(std::move(init), "sizeof");
            auto initCount = braceElementCount(trim(init));
            if (initCount > 1 && constexprType(type) == "uint64_t" && type.rfind("logic<", 0) != 0) {
                type = "array<uint64_t," + std::to_string(initCount) + ">";
            }
            auto constInit = constexprAggregateInit(type, trim(init));
            mod->constants.push_back({type, name + " = " + constInit});
            if (mod->isPackage) {
                auto ctype = constexprType(type);
                auto cinit = constInit;
                if (mod->isPackage && cinit.find("'(") != std::string::npos) {
                    cinit = "{}";
                }
                cinit = replaceLogicBraceCasts(std::move(cinit));
                if (ctype == "unsigned" || ctype == "uint64_t") {
                    cinit = trim(stripLogicLiteralCasts(cinit));
                }
                if (mod->isPackage && cinit.find("{64{") != std::string::npos) {
                    cinit = "{}";
                }
                if (cinit == "static_cast<" + ctype + ">(0)" || cinit == "static_cast<" + type + ">(0)") {
                    cinit = "{}";
                }
                if ((ctype == "unsigned" || ctype == "uint64_t") && !cinit.empty() && cinit.front() == '{') {
                    cinit = "0";
                }
                if ((ctype == "unsigned" || ctype == "uint64_t") && cinit.find("logic<") != std::string::npos &&
                    cinit.find('{') != std::string::npos) {
                    cinit = "0";
                }
                if (ctype.rfind("std::array<", 0) == 0 && cinit.find("logic<") != std::string::npos) {
                    cinit = trim(stripLogicLiteralCasts(cinit));
                }
                if (ctype.find("::") == std::string::npos && ctype.rfind("std::array<", 0) != 0 &&
                    cinit.find("logic<") != std::string::npos && cinit.find('{') == std::string::npos &&
                    cinit.find("cat{") == std::string::npos) {
                    cinit = trim(stripLogicLiteralCasts(cinit));
                }
                if (ctype.rfind("std::array<", 0) == 0 && !cinit.empty() && cinit.front() == '{') {
                    cinit = "{" + cinit + "}";
                }
                if (cinit.find(".{") == std::string::npos && cinit.find(".") != std::string::npos && cinit.find(" = ") != std::string::npos &&
                    cinit.find("{") != std::string::npos && cinit.find("}") != std::string::npos &&
                    ctype != "unsigned" && ctype != "uint64_t" && ctype.rfind("std::array<", 0) != 0) {
                    cinit = namedAggregateToConstexprLambda(ctype, cinit);
                }
                mod->packageDecls.push_back("inline constexpr " + ctype + " " + name + " = " + cinit + ";");
                mod->packageValueNames.insert(name);
            }
            mod->types[name] = type;
            mod->valueNames.insert(name);
        }
    }

    void handle(const TypedefDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        auto name = tok(node.name);
        mod->types[name] = name;
        if (node.type->kind == SyntaxKind::EnumType) {
            auto& e = node.type->as<EnumTypeSyntax>();
            std::string width = "32";
            if (e.baseType) {
                auto baseWidth = foldWidth(typeWidth(typeText(*e.baseType)));
                if (!baseWidth.empty()) {
                    width = baseWidth;
                }
            }
            auto dimWidth = foldWidth(dimensionsWidth(e.dimensions));
            if (!dimWidth.empty()) {
                width = dimWidth;
            }
            auto alias = "using " + name + " = logic<" + width + ">;";
            mod->types[name] = "logic<" + width + ">";
            mod->typeWidths[name] = width;
            mod->typeDecls.push_back(alias);
            if (mod->isPackage) {
                mod->packageDecls.push_back(alias);
            }
            uint64_t nextEnumValue = 0;
            for (auto m : e.members) {
                auto memberName = tok(m->name);
                std::string value = std::to_string(nextEnumValue);
                if (m->initializer) {
                    auto init = cppExprText(m->initializer->toString());
                    if (!init.empty() && init.front() == '=') {
                        init.erase(init.begin());
                    }
                    value = trim(init);
                }
                auto valueTrim = trim(value);
                bool parsedEnumValue = false;
                try {
                    size_t parsedLen = 0;
                    auto parsed = std::stoull(valueTrim, &parsedLen, 0);
                    if (parsedLen == valueTrim.size()) {
                        nextEnumValue = parsed + 1;
                        parsedEnumValue = true;
                    }
                }
                catch (...) {
                }
                if (!parsedEnumValue) {
                    ++nextEnumValue;
                }
                auto enumValue = trim(stripLogicLiteralCasts(value));
                auto line = std::string(mod->isPackage ? "inline constexpr unsigned " : "static constexpr unsigned ") +
                            memberName + " = " + enumValue + ";";
                mod->typeDecls.push_back(line);
                mod->valueNames.insert(memberName);
                if (mod->isPackage) {
                    mod->packageDecls.push_back(line);
                    mod->packageValueNames.insert(memberName);
                }
            }
            return;
        }
        if (node.type->kind == SyntaxKind::StructType || node.type->kind == SyntaxKind::UnionType) {
            auto& st = node.type->as<StructUnionTypeSyntax>();
            std::string line = cppAggregateKeyword(st) + " " + name + " {\n";
	            std::vector<PackedFieldInfo> fieldWidths;
	            std::vector<std::string> fieldLines;
            for (auto member : st.members) {
	                for (auto d : member->declarators) {
	                    auto fieldType = varType(*member->type, *d);
	                    registerTypeField(name, tok(d->name), fieldType);
	                    auto directFieldWidth = typeWidth(fieldType);
	                    auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
	                    if (!fieldWidth.empty()) {
	                        fieldWidths.push_back({cppIdent(tok(d->name)), fieldWidth, packedFieldUsesPack(fieldType)});
                    }
                    if (mod->isPackage) {
                        fieldType = constexprStructFieldType(fieldType);
                    }
	                    fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(d->name)) + ";\n");
	                }
	            }
		            auto packedWidth = isPackedUnion(st) ? packedUnionWidth(fieldWidths) : joinedPackedWidth(fieldWidths);
	            for (auto& fieldLine : fieldLines) {
	                line += fieldLine;
	            }
	            auto helperFields = packedHelperFields(st, fieldWidths);
	            line += packedAggregateHelpers(name, packedWidth, helperFields, isPackedUnion(st));
            line += "};";
            mod->typeDecls.push_back(line);
            if (!packedWidth.empty()) {
                mod->typeWidths[name] = packedWidth;
            }
            if (mod->isPackage) {
                mod->packageDecls.push_back(line);
            }
            return;
        }
        auto type = typeText(*node.type);
        auto dims = dimensionWidths(node.dimensions);
        for (auto it = dims.rbegin(); it != dims.rend(); ++it) {
            type = arrayTypeForDimension(type, *it, false);
        }
        auto aliasType = mod->isPackage && type.rfind("logic<", 0) != 0 && type.rfind("u<", 0) != 0
            ? constexprType(type)
            : type;
        auto line = "using " + name + " = " + aliasType + ";";
        mod->typeDecls.push_back(line);
        mod->types[name] = aliasType;
        auto aliasWidth = typeWidth(type);
        if (!aliasWidth.empty()) {
            mod->typeWidths[name] = aliasWidth;
        }
        if (mod->isPackage) {
            mod->packageDecls.push_back(line);
        }
    }

    void handle(const ParameterDeclarationStatementSyntax& node)
    {
        if (!mod) {
            return;
        }
        if (node.parameter->kind == SyntaxKind::ParameterDeclaration) {
            handle(node.parameter->as<ParameterDeclarationSyntax>());
            return;
        }
        if (node.parameter->kind == SyntaxKind::TypeParameterDeclaration) {
            auto& td = node.parameter->as<TypeParameterDeclarationSyntax>();
            for (auto d : td.declarators) {
                auto name = tok(d->name);
                mod->types[name] = name;
                if (d->assignment && (d->assignment->type->kind == SyntaxKind::StructType ||
                                      d->assignment->type->kind == SyntaxKind::UnionType)) {
	                    auto& st = d->assignment->type->as<StructUnionTypeSyntax>();
		                    std::string line = cppAggregateKeyword(st) + " " + name + " {\n";
		                    std::vector<PackedFieldInfo> fieldWidths;
		                    std::vector<std::string> fieldLines;
	                    for (auto member : st.members) {
	                        for (auto md : member->declarators) {
	                            if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
                                auto& nested = member->type->as<StructUnionTypeSyntax>();
	                                line += std::string("    ") + cppAggregateKeyword(nested) + " { ";
                                for (auto nestedMember : nested.members) {
                                    for (auto nd : nestedMember->declarators) {
                                        line += varType(*nestedMember->type, *nd) + " " + cppIdent(tok(nd->name)) + "; ";
                                    }
                                }
	                                line += "} " + cppIdent(tok(md->name)) + ";\n";
	                            }
	                            else {
	                                auto fieldType = varType(*member->type, *md);
	                                registerTypeField(name, tok(md->name), fieldType);
	                                auto directFieldWidth = typeWidth(fieldType);
	                                auto fieldWidth = directFieldWidth.empty() ? packedFieldWidth(fieldType) : directFieldWidth;
	                                if (!fieldWidth.empty()) {
	                                    fieldWidths.push_back({cppIdent(tok(md->name)), fieldWidth, packedFieldUsesPack(fieldType)});
	                                }
		                                fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(md->name)) + ";\n");
		                            }
		                        }
		                    }
			                    auto packedWidth = isPackedUnion(st) ? packedUnionWidth(fieldWidths) : joinedPackedWidth(fieldWidths);
		                    for (auto& fieldLine : fieldLines) {
		                        line += fieldLine;
		                    }
		                    auto helperFields = packedHelperFields(st, fieldWidths);
			                    line += packedAggregateHelpers(name, packedWidth, helperFields, isPackedUnion(st));
	                    line += "};";
	                    if (!packedWidth.empty()) {
	                        mod->typeWidths[name] = packedWidth;
	                    }
	                    mod->typeDecls.push_back(line);
	                }
                else if (d->assignment) {
                    auto aliasOverrides = configuredTextMap("HDLCPP_TYPE_ALIAS_OVERRIDES");
                    auto aliasIt = aliasOverrides.find(name);
                    std::string aliasType;
                    if (aliasIt != aliasOverrides.end()) {
                        aliasType = aliasIt->second;
                    }
                    else {
                        aliasType = typeText(*d->assignment->type);
                    }
                    mod->typeDecls.push_back("using " + name + " = " + aliasType + ";");
                    mod->types[name] = aliasType;
                }
            }
        }
    }

    void handle(const ContinuousAssignSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto a : node.assignments) {
            if (a->kind == SyntaxKind::AssignmentExpression) {
                auto& b = a->as<BinaryExpressionSyntax>();
                auto base = assignedBase(*b.left);
                auto wholeNetAssign = b.left->kind == SyntaxKind::IdentifierName || isWholeObjectSelect(*b.left, base);
                auto lhs = (mod->outputPortCppNames.count(base) && wholeNetAssign) ? mod->outputPortCppNames[base] :
                    (wholeNetAssign && !base.empty() ? base : emitLValue(*b.left));
                if (base.empty()) {
                    base = baseFromLValueText(lhs);
                    wholeNetAssign = false;
                }
                auto internalWholeNet = !base.empty() && mod->varNames.count(base) && wholeNetAssign &&
                                        !mod->outputPortCppNames.count(base);
                if (!base.empty() && mod->varNames.count(base) && !wholeNetAssign) {
                    mod->partialAssignDrivenVars.insert(base);
                }
                auto rhs = emitExpr(*b.right);
                if (b.right->kind == SyntaxKind::ConditionalExpression && !base.empty() && mod->types.count(base)) {
                    auto targetType = unwrapRegType(mod->types[base]);
                    auto& c = b.right->as<ConditionalExpressionSyntax>();
                    if ((targetType.rfind("logic<", 0) == 0 || targetType.rfind("u<", 0) == 0 ||
                        (targetType.find("::") != std::string::npos && targetType.rfind("array<", 0) != 0))) {
                        rhs = emitConditionalAsType(c, targetType);
                    }
                }
                if (b.right->kind == SyntaxKind::ConditionalExpression && (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos) &&
                    lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                    rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
                }
                if (isZeroLiteralExpr(*b.right) &&
                    (lhs.find('.') != std::string::npos || lhs.find('[') != std::string::npos) &&
                    lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                    if (auto zeroRhs = zeroAssignmentRhsForLValue(lhs); !zeroRhs.empty()) {
                        rhs = zeroRhs;
                    }
                }
                for (auto& p : mod->ports) {
                    if (p.name == lhs && needsTypedZero(p.type) && isZeroLiteralText(rhs)) {
                        rhs = p.type + "{}";
                        break;
                    }
                }
                if (addConcatOutputAssignments(*mod, lhs, rhs)) {
                    continue;
                }
                if (!base.empty() && wholeNetAssign) {
                    mod->assignExprByBase[base] = rhs;
                }
                mod->assigns.push_back({lhs, rhs});
                if (!base.empty() && mod->outputPortCppNames.count(base) &&
                    b.left->kind != SyntaxKind::IdentifierName &&
                    !configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", mod->name)) {
                    addCombAssignment(*mod, base, emitLValue(*b.left), rhs);
                    continue;
                }
                if (internalWholeNet || (!base.empty() && mod->varNames.count(base) && !wholeNetAssign &&
                                         !mod->outputPortCppNames.count(base))) {
                    addCombAssignment(*mod, base, lhs, rhs);
                    continue;
                }
                mod->assignLines.push_back(lhs + " = " + ((lhs.find('.') != std::string::npos && lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) ? rhs : assignWrapper(rhs, "")) + ";");
            }
        }
    }

    bool isWholeObjectSelect(const ExpressionSyntax& expr, const std::string& base)
    {
        if (base.empty() || !mod || !mod->types.count(base)) {
            return false;
        }
        auto baseType = mod->types[base];
        if (baseType.rfind("array<", 0) == 0 || baseType.rfind("std::array<", 0) == 0 ||
            memoryLikeType(baseType)) {
            return false;
        }
        if (expr.kind != SyntaxKind::ElementSelectExpression &&
            expr.kind != SyntaxKind::IdentifierSelectName) {
            return false;
        }
        auto lhsWidth = foldWidth(exprWidth(expr));
        auto baseWidth = foldWidth(typeWidth(baseType));
        return !lhsWidth.empty() && lhsWidth == baseWidth;
    }

    void handle(const LoopGenerateSyntax& node)
    {
        if (!mod) {
            return;
        }
        auto id = tok(node.identifier);
        auto stop = emitExpr(*node.stopExpr);
        auto iter = emitExpr(*node.iterationExpr);
        replaceIdentifierAll(stop, id, "i");
        replaceIdentifierAll(iter, id, "i");
        auto limit = stop;
        auto lt = limit.find('<');
        if (lt != std::string::npos) {
            limit = trim(limit.substr(lt + 1));
        }
        auto init = emitExpr(*node.initialExpr);
        replaceIdentifierAll(init, id, "i");
        std::vector<std::string> methodLoopHeaders = {
            "for (unsigned i = " + init + ";" + stop + ";" + iter + ") {"
        };
        mod->assignLines.push_back(methodLoopHeaders.back());
        emitGenerateMember(*node.block, id, limit, {{id, "i"}}, methodLoopHeaders);
        mod->assignLines.push_back("}");
    }

    void handle(const GenerateRegionSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto member : node.members) {
            member->visit(*this);
        }
    }

    void handle(const IfGenerateSyntax& node)
    {
        if (!mod) {
            return;
        }
        if (auto decision = evalConfiguredGenerateCondition(*mod, emitExpr(*node.condition))) {
            if (*decision) {
                emitGenerateMember(*node.block, "");
            }
            else if (node.elseClause && node.elseClause->clause) {
                auto* clause = node.elseClause->clause.get();
                if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                    clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::TypedefDeclaration ||
                    clause->kind == SyntaxKind::ParameterDeclarationStatement ||
                    clause->kind == SyntaxKind::ContinuousAssign ||
                    clause->kind == SyntaxKind::HierarchyInstantiation) {
                    emitGenerateMember(clause->as<MemberSyntax>(), "");
                }
            }
            return;
        }
        if (generateBranchCanBeMethodGuarded(node.block.get()) &&
            (!node.elseClause || !node.elseClause->clause ||
             generateBranchCanBeMethodGuarded(node.elseClause->clause.get()))) {
            auto cond = emitExpr(*node.condition);
            std::vector<std::string> trueGuard = {generateBranchGuard(cond)};
            mod->assignLines.push_back(trueGuard.back());
            emitGenerateMember(*node.block, "", "", {}, trueGuard);
            mod->assignLines.push_back("}");
            if (node.elseClause) {
                if (node.elseClause->clause) {
                    auto* clause = node.elseClause->clause.get();
                    if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                        clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::TypedefDeclaration ||
                        clause->kind == SyntaxKind::ParameterDeclarationStatement ||
                        clause->kind == SyntaxKind::ContinuousAssign) {
                        std::vector<std::string> falseGuard = {generateInverseBranchGuard(cond)};
                        mod->assignLines.push_back(falseGuard.back());
                        emitGenerateMember(clause->as<MemberSyntax>(), "", "", {}, falseGuard);
                        mod->assignLines.push_back("}");
                    }
                }
            }
        }
        else {
            auto cond = emitExpr(*node.condition);
            std::vector<std::string> trueGuard = {generateBranchGuard(cond)};
            mod->assignLines.push_back(trueGuard.back());
            emitGenerateMember(*node.block, "", "", {}, trueGuard);
            mod->assignLines.push_back("}");
            if (node.elseClause && node.elseClause->clause) {
                auto* clause = node.elseClause->clause.get();
                std::vector<std::string> falseGuard = {generateInverseBranchGuard(cond)};
                mod->assignLines.push_back(falseGuard.back());
                emitGenerateMember(clause->as<MemberSyntax>(), "", "", {}, falseGuard);
                mod->assignLines.push_back("}");
            }
        }
    }

    void handle(const HierarchyInstantiationSyntax& node)
    {
        if (!mod) {
            return;
        }
        std::string params;
        if (node.parameters) {
            std::vector<std::string> orderedParams;
            std::map<std::string, std::string> namedParams;
            std::map<std::string, std::string> namedParamRaw;
            std::vector<std::pair<std::string, std::string>> namedParamOrder;
            for (auto p : node.parameters->parameters) {
                if (p->kind == SyntaxKind::OrderedParamAssignment) {
                    orderedParams.push_back(stripLogicLiteralCasts(emitExpr(*p->as<OrderedParamAssignmentSyntax>().expr)));
                }
                else if (p->kind == SyntaxKind::NamedParamAssignment && p->as<NamedParamAssignmentSyntax>().expr) {
                    auto& np = p->as<NamedParamAssignmentSyntax>();
                    auto name = tok(np.name);
                    auto value = DataTypeSyntax::isKind(np.expr->kind) ? typeText(np.expr->as<DataTypeSyntax>()) : stripLogicLiteralCasts(emitExpr(*np.expr));
                    namedParams[name] = value;
                    namedParamRaw[name] = exprText(np.expr->toString());
                    namedParamOrder.push_back({name, value});
                }
            }
            if (!orderedParams.empty() || !namedParams.empty()) {
                auto appendParam = [&](const std::string& value) {
                    if (!params.empty()) {
                        params += ",";
                    }
                    params += value;
                };
                auto type = tok(node.type);
                auto* child = findModule(type);
	                auto configuredParams = configuredModuleParams(type);
	                auto& declParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
                if (!declParams.empty()) {
                    std::vector<std::string> paramNames;
                    for (auto& declared : declParams) {
                        paramNames.push_back(templateParamName(declared));
                    }
                    int lastNeeded = static_cast<int>(orderedParams.size()) - 1;
                    for (int i = 0; i < static_cast<int>(paramNames.size()); ++i) {
                        if (namedParams.count(paramNames[i])) {
                            lastNeeded = std::max(lastNeeded, i);
                        }
                    }
                    lastNeeded = std::min(lastNeeded, static_cast<int>(declParams.size()) - 1);
                    std::map<std::string, std::string> emittedParams;
                    for (int i = 0; i <= lastNeeded; ++i) {
                        auto& declared = declParams[i];
                        auto& pname = paramNames[i];
                        std::string value;
                        bool hasValue = false;
                        auto namedIt = namedParams.find(pname);
                        if (namedIt != namedParams.end()) {
                            value = namedIt->second;
                            hasValue = true;
                            if (declared.rfind("typename ", 0) == 0) {
                                auto raw = namedParamRaw.find(pname);
                                if (raw != namedParamRaw.end()) {
                                    auto rawType = raw->second;
                                    for (auto& item : mod->types) {
                                        auto w = typeWidth(item.second);
                                        replaceAll(rawType, "$bits(" + item.first + ")",
                                                   !w.empty() ? w : "(sizeof(" + item.second + ")*8)");
                                    }
                                    auto typeText = cppTypeFromSvText(rawType);
                                    if (!typeText.empty() && typeText != "logic") {
                                        value = typeText;
                                    }
                                }
                            }
                        }
                        else if (i < static_cast<int>(orderedParams.size())) {
                            value = orderedParams[i];
                            hasValue = true;
                        }
                        if (hasValue) {
                            value = castTemplateParamValue(declared, value);
                            appendParam(value);
                            emittedParams[pname] = value;
                        }
                        else {
                            auto def = templateParamDefault(declared);
                            if (!def.empty()) {
                                for (auto& prior : emittedParams) {
                                    replaceIdentifierAll(def, prior.first, prior.second);
                                }
                                appendParam(def);
                                emittedParams[pname] = def;
                            }
                        }
                    }
                }
                else {
                    for (auto& item : orderedParams) {
                        appendParam(item);
                    }
                    for (auto& item : namedParamOrder) {
                        appendParam(item.second);
                    }
                }
            }
        }
        for (auto inst : node.instances) {
            if (!inst->decl) {
                continue;
            }
            auto name = tok(inst->decl->name);
            auto type = tok(node.type);
            if (params.empty()) {
                params = inferInstanceParamsFromInterfaceConnections(type, *inst);
            }
            auto memberType = moduleMemberType(type, params);
            auto instanceDims = dimensionWidths(inst->decl->dimensions);
            if (!instanceDims.empty()) {
                auto arrayType = memberType;
                for (auto it = instanceDims.rbegin(); it != instanceDims.rend(); ++it) {
                    arrayType = "array<" + arrayType + "," + *it + ">";
                }
                mod->members.push_back(arrayType + " " + name + ";");
                mod->memberArraySizes[name] = instanceDims.front();
                rememberMemberType(name, arrayType);
            }
            else {
                mod->members.push_back(memberType + " " + name + ";");
                rememberMemberType(name, memberType);
            }
            mod->memberTypes.push_back(memberType);
            mod->memberNames.push_back(name);
            for (auto conn : inst->connections) {
                if (conn->kind == SyntaxKind::NamedPortConnection) {
                    auto& c = conn->as<NamedPortConnectionSyntax>();
                    auto port = tok(c.name);
                    if (isClockPortName(port) || port == "reset") {
                        continue;
                    }
                    auto expr = c.expr ? propertyExpr(*c.expr) : nullptr;
                    if (expr) {
                        auto rhs = emitExpr(*expr);
                        auto lhs = emitLValue(*expr);
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, rhs, lhs, true, params});
                    }
                    else {
                        auto connText = c.toString();
                        connText.erase(std::remove_if(connText.begin(), connText.end(),
                            [](unsigned char ch) { return std::isspace(ch); }), connText.end());
                        if (connText.find("." + port + "()") != std::string::npos) {
                            continue;
                        }
                        auto mapped = mod->wireMap.count(port) ? mod->wireMap[port] : port;
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, mapped, mapped, true, params});
                    }
                }
            }
        }
    }

    void handle(const ProceduralBlockSyntax& node)
    {
        if (!mod) {
            return;
        }
        if (node.kind == SyntaxKind::InitialBlock || node.kind == SyntaxKind::FinalBlock) {
            return;
        }
        auto comb = tok(node.keyword).find("comb") != std::string::npos;
        if (node.statement->kind == SyntaxKind::TimingControlStatement) {
            auto& t = node.statement->as<TimingControlStatementSyntax>();
            comb = comb || t.timingControl->kind == SyntaxKind::ImplicitEventControl;
        }
        if (!comb && statementCallsWork(*node.statement)) {
            return;
        }
        MethodGen m;
        auto assigned = firstAssigned(*node.statement);
        if (comb) {
            auto assignedIsReg = !assigned.empty() && mod->types.count(assigned) &&
                mod->types[assigned].rfind("reg<", 0) == 0;
            m.name = (assigned.empty() || assignedIsReg) ? "always_" + std::to_string(mod->alwaysNo) + "_comb_func" : assigned + "_comb_func";
            if (!assigned.empty() && mod->types.count(assigned) && !assignedIsReg) {
                auto retType = unwrapRegType(mod->types[assigned]);
                m.returnName = mod->outputPortCppNames.count(assigned) ?
                    outputStorageName(*mod, assigned) : combStorageName(*mod, assigned);
                m.returnBase = assigned;
                if (mod->outputPortCppNames.count(assigned)) {
                    auto cppName = mod->outputPortCppNames[assigned];
                    auto storageType = mod->types.count(assigned) ? mod->types[assigned] :
                        outputStorageType(*mod, assigned, cppName);
                    if (!storageType.empty()) {
                        retType = storageType;
                    }
                }
                m.ret = retType + "&";
                mod->combReturnTypes[m.returnName] = retType;
            }
            else {
                m.ret = "void";
            }
        }
        else {
            m.name = "always_" + std::to_string(mod->alwaysNo);
            m.args = "bool reset";
        }
        mod->alwaysNo++;
        loopVars.clear();
        collectLoopVars(*node.statement);
        localTypeScopes.push_back({});
        emitStatement(*node.statement, m.body, comb, 0);
        localTypeScopes.pop_back();
        if (comb) {
            for (auto& line : m.body) {
                for (const auto& out : mod->outputPortCppNames) {
                    hdlcpp::rewriteLhsBase(line, combStorageName(*mod, out.first), out.first);
                    hdlcpp::rewriteLhsBase(line, outputStorageName(*mod, out.first), out.first);
                    hdlcpp::rewriteLhsBase(line, out.second, out.first);
                }
            }
            std::set<std::string> localNames;
            for (const auto& line : m.body) {
                auto local = hdlcpp::declarationName(line);
                if (!local.empty()) {
                    localNames.insert(local);
                }
            }
            std::set<std::string> assignedBases;
            collectAssignedBases(*node.statement, assignedBases);
            for (const auto& line : m.body) {
                auto base = hdlcpp::assignmentBase(line);
                if (!base.empty() && !localNames.count(base)) {
                    assignedBases.insert(base);
                }
            }
            for (const auto& local : localNames) {
                assignedBases.erase(local);
            }
            for (const auto& line : m.body) {
                auto base = hdlcpp::assignmentBase(line);
                if (base.empty() || localNames.count(base) || mod->types.count(base) || mod->outputPortCppNames.count(base)) {
                    continue;
                }
                auto rhs = hdlcpp::assignmentRhs(line);
                auto width = foldWidth(typeWidth(expressionStorageType(*mod, rhs)));
                std::string type;
                if (isNumber(width) && width != "0") {
                    type = "logic<" + width + ">";
                }
                else {
                    type = "logic<1>";
                }
                mod->types[base] = type;
                if (!mod->varNames.count(base)) {
                    mod->vars.push_back({type, base});
                    mod->varNames.insert(base);
                }
            }
            std::vector<std::string> assignedList(assignedBases.begin(), assignedBases.end());
            auto plan = hdlcpp::planCombExtraction(m.body, assignedList);
            if (const char* debugModules = std::getenv("HDLCPP_DEBUG_COMB_MODULES");
                debugModules && std::string(debugModules).find(mod->name) != std::string::npos) {
                std::cerr << "HDLCPP_COMB_DEBUG_OLD module=" << mod->name << "\n";
                std::cerr << "assigned:";
                for (const auto& base : assignedList) {
                    std::cerr << " " << base;
                }
                std::cerr << "\nbody:\n";
                for (const auto& line : m.body) {
                    std::cerr << line << "\n";
                }
                std::cerr << "HDLCPP_COMB_DEBUG_OLD_END\n";
            }

            auto isCombAssignableBase = [&](const std::string& base) {
                if (base.empty()) {
                    return false;
                }
                if (mod->outputPortCppNames.count(base)) {
                    return true;
                }
                return mod->types.count(base) && mod->types[base].rfind("reg<", 0) != 0;
            };

            for (const auto& base : plan.independent) {
                if (isCombAssignableBase(base)) {
                    mod->combAssignedVars.insert(base);
                    PendingCombGen pending;
                    pending.lines = m.body;
                    pending.variables = assignedList;
                    pending.localNames = localNames;
                    mergePendingComb(*mod, base, pending);
                }
            }
            return;
        }
        mod->methods.push_back(m);
    }

    void handle(const FunctionDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        MethodGen m;
        auto kw = tok(node.prototype->keyword);
        m.name = trim(node.prototype->name->toString());
        if (m.name == "_work") {
            mod->hasWorkTask = true;
        }
        m.ret = kw == "task" ? "void" : typeText(*node.prototype->returnType);
        mod->functionReturnTypes[m.name] = m.ret;
        if (mod->isPackage) {
            m.ret = constexprType(m.ret);
            if (m.ret == "string") {
                m.ret = "std::string";
            }
            mod->functionReturnTypes[m.name] = m.ret;
        }
        std::map<std::string, std::string> functionLocalTypes;
        if (node.prototype->portList) {
            bool first = true;
            std::string lastDirection = "input";
            for (auto p : node.prototype->portList->ports) {
                if (p->kind == SyntaxKind::FunctionPort) {
                    auto& fp = p->as<FunctionPortSyntax>();
                    if (!first) {
                        m.args += ", ";
                    }
                    first = false;
                    auto type = fp.dataType ? typeText(*fp.dataType) : "bool";
                    if (mod->isPackage) {
                        if (type.rfind("logic<", 0) != 0 && type.rfind("u<", 0) != 0) {
                            type = constexprType(type);
                        }
                    }
                    auto name = tok(fp.declarator->name);
                    std::string direction = lastDirection;
                    if (fp.direction) {
                        direction = tok(fp.direction);
                        lastDirection = direction;
                    }
                    functionLocalTypes[name] = type;
                    const bool byReference =
                        direction == "output" || direction == "inout" || direction == "ref";
                    m.args += type + (byReference ? "& " : " ") + name;
                }
            }
        }
        auto splitTopLevelArgs = [](const std::string& text) {
            std::vector<std::string> args;
            std::string cur;
            int angle = 0;
            int paren = 0;
            for (char c : text) {
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
                if (c == ',' && angle == 0 && paren == 0) {
                    args.push_back(trim(cur));
                    cur.clear();
                }
                else {
                    cur.push_back(c);
                }
            }
            if (!trim(cur).empty()) {
                args.push_back(trim(cur));
            }
            return args;
        };
        for (const auto& arg : splitTopLevelArgs(m.args)) {
            auto name = templateParamName(arg);
            if (name.empty() || functionLocalTypes.count(name)) {
                continue;
            }
            auto pos = arg.rfind(name);
            if (pos != std::string::npos) {
                auto type = trim(arg.substr(0, pos));
                while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
                    type.pop_back();
                    type = trim(type);
                }
                if (!type.empty()) {
                    functionLocalTypes[name] = type;
                }
            }
        }
        std::map<std::string, std::string> savedFunctionParamTypes;
        std::set<std::string> newFunctionParamTypes;
        auto installFunctionParamTypes = [&]() {
            savedFunctionParamTypes.clear();
            newFunctionParamTypes.clear();
            for (const auto& [name, type] : functionLocalTypes) {
                if (auto it = mod->types.find(name); it != mod->types.end()) {
                    savedFunctionParamTypes[name] = it->second;
                }
                else {
                    newFunctionParamTypes.insert(name);
                }
                mod->types[name] = type;
            }
        };
        auto restoreFunctionParamTypes = [&]() {
            for (const auto& name : newFunctionParamTypes) {
                mod->types.erase(name);
            }
            for (const auto& [name, type] : savedFunctionParamTypes) {
                mod->types[name] = type;
            }
        };
        if (mod->isPackage) {
            if (auto body = configuredTextMap("HDLCPP_METHOD_BODY_OVERRIDES"); body.count(m.name + "|" + m.ret)) {
                std::stringstream ss(body[m.name + "|" + m.ret]);
                std::string bodyLine;
                while (std::getline(ss, bodyLine)) {
                    if (!bodyLine.empty()) {
                        m.body.push_back(bodyLine);
                    }
                }
            }
            else if (m.name == "minimum" && m.args.find(',') != std::string::npos) {
                m.body.push_back("return a < b ? a : b;");
            }
            else if (m.name == "maximum" && m.args.find(',') != std::string::npos) {
                m.body.push_back("return a > b ? a : b;");
            }
            else if (m.ret == "std::string") {
                m.body.push_back("return {};");
            }
            else {
                bool hasExplicitReturn = false;
                for (auto item : node.items) {
                    if (item->toString().find("return") != std::string::npos) {
                        hasExplicitReturn = true;
                        break;
                    }
                }
                if (hasExplicitReturn) {
                    auto savedLoopVars = loopVars;
                    loopVars.clear();
                    for (auto item : node.items) {
                        collectLoopVars(*item);
                    }
                    localTypeScopes.push_back(functionLocalTypes);
                    installFunctionParamTypes();
                    for (auto item : node.items) {
                        emitNode(*item, m.body, false, 0);
                    }
                    restoreFunctionParamTypes();
                    localTypeScopes.pop_back();
                    loopVars = savedLoopVars;
                }
                else if (m.ret != "void") {
                    m.body.push_back("return {};");
                }
            }
            mod->methods.push_back(m);
            return;
        }
        loopVars.clear();
        for (auto item : node.items) {
            collectLoopVars(*item);
        }
        localTypeScopes.push_back(functionLocalTypes);
        installFunctionParamTypes();
        for (auto item : node.items) {
            emitNode(*item, m.body, false, 0);
        }
        restoreFunctionParamTypes();
        localTypeScopes.pop_back();
        mod->methods.push_back(m);
    }

    void handle(const StructUnionTypeSyntax& node)
    {
        Struct st;
        st.type = tok(node.keyword) == "union" ? Struct::STRUCT_UNION : Struct::STRUCT_STRUCT;
        project.structs.push_back(st);
        visitDefault(node);
    }

    void emitGenerateMember(const MemberSyntax& node, const std::string& index, const std::string& indexLimit = "",
                            const std::vector<std::pair<std::string, std::string>>& replacements = {},
                            const std::vector<std::string>& methodLoopHeaders = {})
    {
        auto applyGenerateReplacements = [&](std::string& text) {
            if (!replacements.empty()) {
                for (auto& repl : replacements) {
                    replaceIdentifierAll(text, repl.first, repl.second);
                }
            }
            else if (!index.empty()) {
                replaceIdentifierAll(text, index, "i");
            }
        };
        if (node.kind == SyntaxKind::GenerateBlock) {
            pushGenerateLocalScope();
            for (auto member : node.as<GenerateBlockSyntax>().members) {
                emitGenerateMember(*member, index, indexLimit, replacements, methodLoopHeaders);
            }
            popGenerateLocalScope();
        }
        else if (node.kind == SyntaxKind::LoopGenerate) {
            auto& loop = node.as<LoopGenerateSyntax>();
            auto loopId = tok(loop.identifier);
            std::string loopVar = replacements.empty() ? "i" : (replacements.size() == 1 ? "k" : "m");
            auto stop = emitExpr(*loop.stopExpr);
            auto iter = emitExpr(*loop.iterationExpr);
            applyGenerateReplacements(stop);
            applyGenerateReplacements(iter);
            replaceIdentifierAll(stop, loopId, loopVar);
            replaceIdentifierAll(iter, loopId, loopVar);
            auto limit = stop;
            auto lt = limit.find('<');
            if (lt != std::string::npos) {
                limit = trim(limit.substr(lt + 1));
            }
            auto init = emitExpr(*loop.initialExpr);
            applyGenerateReplacements(init);
            replaceIdentifierAll(init, loopId, loopVar);
            auto loopHeader = "for (unsigned " + loopVar + " = " + init + ";" + stop + ";" + iter + ") {";
            mod->assignLines.push_back(loopHeader);
            auto nestedReplacements = replacements;
            if (nestedReplacements.empty() && !index.empty()) {
                nestedReplacements.push_back({index, "i"});
            }
            nestedReplacements.push_back({loopId, loopVar});
            auto nestedMethodLoopHeaders = methodLoopHeaders;
            nestedMethodLoopHeaders.push_back(loopHeader);
            emitGenerateMember(*loop.block, loopId, limit, nestedReplacements, nestedMethodLoopHeaders);
            mod->assignLines.push_back("}");
        }
        else if (node.kind == SyntaxKind::IfGenerate) {
            auto& ifGen = node.as<IfGenerateSyntax>();
            auto condForEval = emitExpr(*ifGen.condition);
            applyGenerateReplacements(condForEval);
            if (auto decision = evalConfiguredGenerateCondition(*mod, condForEval)) {
                if (*decision) {
                    emitGenerateMember(*ifGen.block, index, indexLimit, replacements, methodLoopHeaders);
                }
                else if (ifGen.elseClause && ifGen.elseClause->clause) {
                    auto* clause = ifGen.elseClause->clause.get();
                    if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                        clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::TypedefDeclaration ||
                        clause->kind == SyntaxKind::ParameterDeclarationStatement ||
                        clause->kind == SyntaxKind::ContinuousAssign ||
                        clause->kind == SyntaxKind::HierarchyInstantiation) {
                        emitGenerateMember(clause->as<MemberSyntax>(), index, indexLimit, replacements, methodLoopHeaders);
                    }
                }
                return;
            }
            if (generateBranchCanBeMethodGuarded(ifGen.block.get()) &&
                (!ifGen.elseClause || !ifGen.elseClause->clause ||
                 generateBranchCanBeMethodGuarded(ifGen.elseClause->clause.get()))) {
                auto cond = emitExpr(*ifGen.condition);
                applyGenerateReplacements(cond);
                auto trueGuard = methodLoopHeaders;
                trueGuard.push_back(generateBranchGuard(cond));
                mod->assignLines.push_back(trueGuard.back());
                emitGenerateMember(*ifGen.block, index, indexLimit, replacements, trueGuard);
                mod->assignLines.push_back("}");
                if (ifGen.elseClause) {
                    if (ifGen.elseClause->clause) {
                        auto* clause = ifGen.elseClause->clause.get();
                        if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                            clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::TypedefDeclaration ||
                            clause->kind == SyntaxKind::ParameterDeclarationStatement ||
                            clause->kind == SyntaxKind::ContinuousAssign ||
                            clause->kind == SyntaxKind::HierarchyInstantiation) {
                            auto falseGuard = methodLoopHeaders;
                            falseGuard.push_back(generateInverseBranchGuard(cond));
                            mod->assignLines.push_back(falseGuard.back());
                            emitGenerateMember(clause->as<MemberSyntax>(), index, indexLimit, replacements, falseGuard);
                            mod->assignLines.push_back("}");
                        }
                    }
                }
            }
            else {
                auto cond = emitExpr(*ifGen.condition);
                applyGenerateReplacements(cond);
                auto trueGuard = methodLoopHeaders;
                trueGuard.push_back(generateBranchGuard(cond));
                mod->assignLines.push_back(trueGuard.back());
                emitGenerateMember(*ifGen.block, index, indexLimit, replacements, trueGuard);
                mod->assignLines.push_back("}");
                if (ifGen.elseClause && ifGen.elseClause->clause) {
                    auto falseGuard = methodLoopHeaders;
                    falseGuard.push_back(generateInverseBranchGuard(cond));
                    mod->assignLines.push_back(falseGuard.back());
                    emitGenerateMember(ifGen.elseClause->clause->as<MemberSyntax>(), index, indexLimit, replacements, falseGuard);
                    mod->assignLines.push_back("}");
                }
            }
        }
        else if (node.kind == SyntaxKind::HierarchyInstantiation && !index.empty() && !indexLimit.empty()) {
            auto& instNode = node.as<HierarchyInstantiationSyntax>();
            auto type = tok(instNode.type);
            std::string params;
            if (instNode.parameters) {
                std::vector<std::string> orderedParams;
                std::map<std::string, std::string> namedParams;
                std::map<std::string, std::string> namedParamRaw;
                std::vector<std::pair<std::string, std::string>> namedParamOrder;
                for (auto p : instNode.parameters->parameters) {
                    if (p->kind == SyntaxKind::OrderedParamAssignment) {
                        auto value = stripLogicLiteralCasts(emitExpr(*p->as<OrderedParamAssignmentSyntax>().expr));
                        replaceIdentifierAll(value, index, "i");
                        orderedParams.push_back(value);
                    }
                    else if (p->kind == SyntaxKind::NamedParamAssignment && p->as<NamedParamAssignmentSyntax>().expr) {
                        auto& np = p->as<NamedParamAssignmentSyntax>();
                        auto value = DataTypeSyntax::isKind(np.expr->kind) ? typeText(np.expr->as<DataTypeSyntax>()) : stripLogicLiteralCasts(emitExpr(*np.expr));
                        replaceIdentifierAll(value, index, "i");
                        auto rawValue = exprText(np.expr->toString());
                        replaceIdentifierAll(rawValue, index, "i");
                        namedParams[tok(np.name)] = value;
                        namedParamRaw[tok(np.name)] = rawValue;
                        namedParamOrder.push_back({tok(np.name), value});
                    }
                }
                auto appendParam = [&](const std::string& value) {
                    if (!params.empty()) {
                        params += ",";
                    }
                    params += value;
                };
                auto* child = findModule(type);
	                auto configuredParams = configuredModuleParams(type);
	                auto& declParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
                if (!declParams.empty()) {
                    std::vector<std::string> paramNames;
                    for (auto& declared : declParams) {
                        paramNames.push_back(templateParamName(declared));
                    }
                    int lastNeeded = static_cast<int>(orderedParams.size()) - 1;
                    for (int i = 0; i < static_cast<int>(paramNames.size()); ++i) {
                        if (namedParams.count(paramNames[i])) {
                            lastNeeded = std::max(lastNeeded, i);
                        }
                    }
                    lastNeeded = std::min(lastNeeded, static_cast<int>(declParams.size()) - 1);
                    std::map<std::string, std::string> emittedParams;
                    for (int i = 0; i <= lastNeeded; ++i) {
                        auto& declared = declParams[i];
                        auto& pname = paramNames[i];
                        std::string value;
                        bool hasValue = false;
                        auto namedIt = namedParams.find(pname);
                        if (namedIt != namedParams.end()) {
                            value = namedIt->second;
                            hasValue = true;
                            if (declared.rfind("typename ", 0) == 0) {
                                auto raw = namedParamRaw.find(pname);
                                if (raw != namedParamRaw.end()) {
                                    auto rawType = raw->second;
                                    for (auto& item : mod->types) {
                                        auto w = typeWidth(item.second);
                                        replaceAll(rawType, "$bits(" + item.first + ")",
                                                   !w.empty() ? w : "(sizeof(" + item.second + ")*8)");
                                    }
                                    auto recoveredType = cppTypeFromSvText(rawType);
                                    if (!recoveredType.empty() && recoveredType != "logic") {
                                        value = recoveredType;
                                    }
                                }
                            }
                        }
                        else if (i < static_cast<int>(orderedParams.size())) {
                            value = orderedParams[i];
                            hasValue = true;
                        }
                        if (hasValue) {
                            value = castTemplateParamValue(declared, value);
                            appendParam(value);
                            emittedParams[pname] = value;
                        }
                        else {
                            auto def = templateParamDefault(declared);
                            if (!def.empty()) {
                                for (auto& prior : emittedParams) {
                                    replaceIdentifierAll(def, prior.first, prior.second);
                                }
                                appendParam(def);
                                emittedParams[pname] = def;
                            }
                        }
                    }
                }
                else {
                    for (auto& item : orderedParams) {
                        appendParam(item);
                    }
                    for (auto& item : namedParamOrder) {
                        appendParam(item.second);
                    }
                }
            }
            for (auto inst : instNode.instances) {
                if (!inst->decl) {
                    continue;
                }
                auto name = tok(inst->decl->name);
                if (params.empty()) {
                    params = inferInstanceParamsFromInterfaceConnections(type, *inst);
                }
                auto memberType = moduleMemberType(type, params);
                if (!mod->memberArraySizes.count(name)) {
                    auto arrayType = "array<" + memberType + "," + indexLimit + ">";
                    mod->members.push_back(arrayType + " " + name + ";");
                    mod->memberTypes.push_back(arrayType);
                    mod->memberNames.push_back(name);
                    mod->memberArraySizes[name] = indexLimit;
                    rememberMemberType(name, arrayType);
                }
                auto elem = name + "[(unsigned)(uint64_t)((uint64_t)(i))]";
                for (auto conn : inst->connections) {
                    if (conn->kind != SyntaxKind::NamedPortConnection) {
                        continue;
                    }
                    auto& c = conn->as<NamedPortConnectionSyntax>();
                    auto port = tok(c.name);
                    if (isClockPortName(port) || port == "reset") {
                        continue;
                    }
                    auto* child = findModule(type);
                    auto portName = port;
                    bool isOutput = false;
                    std::string portType = "bool";
                    bool portTypeKnown = false;
                    bool portTypeUsesChildTypeParam = false;
                    bool outputIsPortRef = false;
                    bool isInterfacePort = false;
                    if (child) {
                        if (child->portCppNames.count(port)) {
                            portName = child->portCppNames[port];
                        }
                        bool knownPort = false;
                        for (auto& p : child->ports) {
                            if (p.name == portName) {
                                knownPort = true;
                                portType = p.type;
                                for (const auto& typeParam : child->typeParamNames) {
                                    if (isIdentifierUsed(portType, typeParam)) {
                                        portTypeUsesChildTypeParam = true;
                                        break;
                                    }
                                }
                                portType = substituteModuleParamValues(type, params, portType);
                                isInterfacePort = p.isInterface;
                                isOutput = p.direction == "output";
                                outputIsPortRef = isOutput;
                                portTypeKnown = true;
                                break;
                            }
                        }
                        if (!knownPort) {
                            continue;
                        }
                        isOutput = isOutput || child->outputPortCppNames.count(port) != 0;
                    }
	                    else {
	                        isOutput = hasSuffix(portName, "_o") || portName.find("_o_") != std::string::npos;
	                        portName += isOutput ? "_out" : "_in";
	                    }
	                    if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES"); portTypes.count(type + "." + port)) {
	                        auto spec = portTypes[type + "." + port];
	                        auto sep = spec.find(':');
	                        auto direction = trim(sep == std::string::npos ? std::string() : spec.substr(0, sep));
	                        auto configuredType = trim(sep == std::string::npos ? spec : spec.substr(sep + 1));
	                        if (direction == "output") {
	                            isOutput = true;
	                            if (!hasSuffix(portName, "_out")) {
	                                portName = port + "_out";
	                            }
	                        }
	                        else if (direction == "input") {
	                            isOutput = false;
	                            if (!hasSuffix(portName, "_in")) {
	                                portName = port + "_in";
	                            }
	                        }
	                        if (!configuredType.empty()) {
	                            portType = substituteModuleParamValues(type, params, configuredType);
                                portTypeKnown = true;
                                auto base = templateBaseName(portType);
                                if (direction.empty() && !base.empty() && !configuredModuleParams(base).empty()) {
                                    isInterfacePort = true;
                                    isOutput = false;
                                    portName = port;
                                }
	                        }
	                    }
                    if (isClockPortName(port)) {
                        continue;
                    }
                    std::string rhs;
                    std::string lhs;
                    if (c.expr) {
                        auto expr = propertyExpr(*c.expr);
                        if (!expr) {
                            continue;
                        }
                        rhs = emitExpr(*expr);
                        lhs = emitLValue(*expr);
                    }
                    else {
                        auto mapped = mod->wireMap.count(port) ? mod->wireMap[port] : port;
                        rhs = mapped;
                        lhs = mapped;
                    }
                    applyGenerateReplacements(rhs);
                    applyGenerateReplacements(lhs);
                    if (isOutput) {
                        auto outExpr = elem + "." + portName + "()";
                        if (portType.rfind("array<", 0) == 0 || portType.rfind("std::array<", 0) == 0) {
                            auto lhsType = expressionStorageType(*mod, lhs);
                            if (lhsType.rfind("array<", 0) != 0 && lhsType.rfind("std::array<", 0) != 0) {
                                outExpr += "[0]";
                            }
                        }
                        if (addConcatOutputAssignments(*mod, lhs, outExpr, methodLoopHeaders)) {
                            continue;
                        }
                        auto outBase = baseFromLValueText(lhs);
                        if (methodLoopHeaders.empty() && !outBase.empty() && mod->outputPortCppNames.count(outBase) &&
                            (trim(lhs) == outBase || trim(lhs) == mod->outputPortCppNames[outBase])) {
                            mod->assignExprByBase[outBase] = outExpr;
                            for (auto& p : mod->ports) {
                                if (p.name == mod->outputPortCppNames[outBase]) {
                                    p.init = " = " + std::string(outputIsPortRef ? "_ASSIGN_COMB" : "_ASSIGN") + "( " + outExpr + " )";
                                    break;
                                }
                            }
                            continue;
                        }
                        addCombAssignment(*mod, outBase, lhs, outExpr, methodLoopHeaders);
                    }
                    else {
                        if (isInterfacePort) {
                            mod->assignLines.push_back("    " + elem + "." + portName + " = " + assignRefWrapper(rhs, "i") + ";");
                            continue;
                        }
                        auto rhsWasZeroLiteral = isZeroLiteralText(rhs);
                        auto rawRhsBase = baseFromLValueText(rhs);
                        auto actualPortType = "std::remove_cvref_t<decltype(" + elem + "." + portName + "())>";
                        if (portTypeKnown) {
                            auto sourceTypeBeforeLateBind = expressionStorageType(*mod, rhs);
                            auto target = trim(portType);
                            if (portTypeUsesChildTypeParam) {
                                target = actualPortType;
                                portType = actualPortType;
                            }
                            auto sourceTypeForBinding = sourceTypeBeforeLateBind;
                            if ((sourceTypeForBinding.rfind("array<", 0) == 0 ||
                                 sourceTypeForBinding.rfind("std::array<", 0) == 0) &&
                                rhs.find('[') != std::string::npos) {
                                sourceTypeForBinding = arrayElementTypeText(sourceTypeForBinding);
                            }
                            if (rhsWasZeroLiteral) {
                                rhs = actualPortType + "{}";
                            }
                            else if (!sourceTypeForBinding.empty() &&
                                     trim(postProcessCppLine(sourceTypeForBinding)) == trim(postProcessCppLine(target))) {
                                rhs = rhs;
                            }
                            else if (sourceTypeForBinding.rfind("array<", 0) == 0 || sourceTypeForBinding.rfind("std::array<", 0) == 0) {
                                if (target.rfind("logic<", 0) == 0 && target.back() == '>') {
                                    rhs = target + "(" + rhs + ")";
                                }
                                else {
                                    rhs = adaptInputPortRhs(*mod, portType, rhs);
                                }
                            }
                            else if ((target.rfind("array<", 0) == 0 || target.rfind("std::array<", 0) == 0) &&
                                     sourceTypeForBinding.rfind("array<", 0) != 0 &&
                                     sourceTypeForBinding.rfind("std::array<", 0) != 0) {
                                rhs = actualPortType + "(" + rhs + ")";
                            }
                            else if (isSimpleCombRef(rhs) &&
                                     target.rfind("logic<", 0) != 0 &&
                                     target.rfind("array<", 0) != 0 &&
                                     target.rfind("std::array<", 0) != 0) {
                                rhs = actualPortType + "(" + rhs + ")";
                            }
                            else if (target.rfind("logic<", 0) != 0 &&
                                     target.rfind("u<", 0) != 0 &&
                                     target.rfind("array<", 0) != 0 &&
                                     target.rfind("std::array<", 0) != 0 &&
                                     target != "bool" &&
                                     target != "u1" &&
                                     rhs.find('[') != std::string::npos) {
                                auto scalarWrapped = trim(rhs);
                                auto unwrapScalarCast = [&]() -> std::string {
                                    auto strip = [&](const char* prefix, size_t templateOpen) -> std::string {
                                        if (scalarWrapped.rfind(prefix, 0) != 0) {
                                            return {};
                                        }
                                        auto closeTemplate = matchingTemplateClose(scalarWrapped, templateOpen);
                                        if (closeTemplate == std::string::npos) {
                                            return {};
                                        }
                                        auto open = closeTemplate + 1;
                                        while (open < scalarWrapped.size() && std::isspace(static_cast<unsigned char>(scalarWrapped[open]))) {
                                            ++open;
                                        }
                                        if (open >= scalarWrapped.size() || scalarWrapped[open] != '(') {
                                            return {};
                                        }
                                        auto close = matchingParenClose(scalarWrapped, open);
                                        if (close != scalarWrapped.size() - 1) {
                                            return {};
                                        }
                                        return trim(scalarWrapped.substr(open + 1, close - open - 1));
                                    };
                                    if (auto inner = strip("logic<", 5); !inner.empty()) {
                                        return inner;
                                    }
                                    if (auto inner = strip("u<", 1); !inner.empty()) {
                                        return inner;
                                    }
                                    return scalarWrapped;
                                };
                                rhs = "cpphdl::sv_cast<" + actualPortType + ">(" + unwrapScalarCast() + ")";
                            }
                            else {
                                rhs = adaptInputPortRhs(*mod, portType, rhs);
                            }
                        }
                        else if (rhsWasZeroLiteral) {
                            rhs = actualPortType + "{}";
                        }
                        auto sideIt = mod->combSideEffectDriver.find(rawRhsBase);
                        if (sideIt != mod->combSideEffectDriver.end() && !mod->seqAssignedVars.count(rawRhsBase)) {
                            mod->combSideEffectChildInputReads.insert(rawRhsBase);
                            rhs = emitSideEffectRead(*mod, sideIt->second, rhs);
                        }
                        mod->assignLines.push_back("    " + elem + "." + portName + " = " + assignWrapper(rhs, "i") + ";");
                    }
                }
            }
        }
        else if (node.kind == SyntaxKind::HierarchyInstantiation) {
            auto& instNode = node.as<HierarchyInstantiationSyntax>();
            std::string params;
            if (instNode.parameters) {
                std::vector<std::string> orderedParams;
                std::map<std::string, std::string> namedParams;
                std::map<std::string, std::string> namedParamRaw;
                std::vector<std::pair<std::string, std::string>> namedParamOrder;
                for (auto p : instNode.parameters->parameters) {
                    if (p->kind == SyntaxKind::OrderedParamAssignment) {
                        auto value = stripLogicLiteralCasts(emitExpr(*p->as<OrderedParamAssignmentSyntax>().expr));
                        applyGenerateReplacements(value);
                        orderedParams.push_back(value);
                    }
                    else if (p->kind == SyntaxKind::NamedParamAssignment && p->as<NamedParamAssignmentSyntax>().expr) {
                        auto& np = p->as<NamedParamAssignmentSyntax>();
                        auto name = tok(np.name);
                        auto value = DataTypeSyntax::isKind(np.expr->kind) ? typeText(np.expr->as<DataTypeSyntax>()) : stripLogicLiteralCasts(emitExpr(*np.expr));
                        applyGenerateReplacements(value);
                        auto rawValue = exprText(np.expr->toString());
                        applyGenerateReplacements(rawValue);
                        namedParams[name] = value;
                        namedParamRaw[name] = rawValue;
                        namedParamOrder.push_back({name, value});
                    }
                }
                if (!orderedParams.empty() || !namedParams.empty()) {
                    auto appendParam = [&](const std::string& value) {
                        if (!params.empty()) {
                            params += ",";
                        }
                        params += value;
                    };
                    auto type = tok(instNode.type);
                    auto* child = findModule(type);
                    auto configuredParams = configuredModuleParams(type);
                    auto& declParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
                    if (!declParams.empty()) {
                        std::vector<std::string> paramNames;
                        for (auto& declared : declParams) {
                            paramNames.push_back(templateParamName(declared));
                        }
                        int lastNeeded = static_cast<int>(orderedParams.size()) - 1;
                        for (int i = 0; i < static_cast<int>(paramNames.size()); ++i) {
                            if (namedParams.count(paramNames[i])) {
                                lastNeeded = std::max(lastNeeded, i);
                            }
                        }
                        lastNeeded = std::min(lastNeeded, static_cast<int>(declParams.size()) - 1);
                        std::map<std::string, std::string> emittedParams;
                        for (int i = 0; i <= lastNeeded; ++i) {
                            auto& declared = declParams[i];
                            auto& pname = paramNames[i];
                            std::string value;
                            bool hasValue = false;
                            auto namedIt = namedParams.find(pname);
                            if (namedIt != namedParams.end()) {
                                value = namedIt->second;
                                hasValue = true;
                                if (declared.rfind("typename ", 0) == 0) {
                                    auto raw = namedParamRaw.find(pname);
                                    if (raw != namedParamRaw.end()) {
                                        auto rawType = raw->second;
                                        for (auto& item : mod->types) {
                                            auto w = typeWidth(item.second);
                                            replaceAll(rawType, "$bits(" + item.first + ")",
                                                       !w.empty() ? w : "(sizeof(" + item.second + ")*8)");
                                        }
                                        auto typeText = cppTypeFromSvText(rawType);
                                        if (!typeText.empty() && typeText != "logic") {
                                            value = typeText;
                                        }
                                    }
                                }
                            }
                            else if (i < static_cast<int>(orderedParams.size())) {
                                value = orderedParams[i];
                                hasValue = true;
                            }
                            if (hasValue) {
                                value = castTemplateParamValue(declared, value);
                                appendParam(value);
                                emittedParams[pname] = value;
                            }
                            else {
                                auto def = templateParamDefault(declared);
                                if (!def.empty()) {
                                    for (auto& prior : emittedParams) {
                                        replaceIdentifierAll(def, prior.first, prior.second);
                                    }
                                    appendParam(def);
                                    emittedParams[pname] = def;
                                }
                            }
                        }
                    }
                    else {
                        for (auto& item : orderedParams) {
                            appendParam(item);
                        }
                        for (auto& item : namedParamOrder) {
                            appendParam(item.second);
                        }
                    }
                }
            }
            for (auto inst : instNode.instances) {
                if (!inst->decl) {
                    continue;
                }
                auto name = tok(inst->decl->name);
                auto type = tok(instNode.type);
                mod->members.push_back(moduleMemberType(type, params) + " " + name + ";");
                mod->memberTypes.push_back(type);
                mod->memberNames.push_back(name);
                for (auto conn : inst->connections) {
                    if (conn->kind != SyntaxKind::NamedPortConnection) {
                        continue;
                    }
                    auto& c = conn->as<NamedPortConnectionSyntax>();
                    auto port = tok(c.name);
                    if (isClockPortName(port) || port == "reset") {
                        continue;
                    }
                    auto expr = c.expr ? propertyExpr(*c.expr) : nullptr;
                    if (expr) {
                        auto rhs = emitExpr(*expr);
                        auto lhs = emitLValue(*expr);
                        applyGenerateReplacements(rhs);
                        applyGenerateReplacements(lhs);
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, rhs, lhs, true, params, methodLoopHeaders});
                    }
                    else {
                        auto connText = c.toString();
                        connText.erase(std::remove_if(connText.begin(), connText.end(),
                            [](unsigned char ch) { return std::isspace(ch); }), connText.end());
                        if (connText.find("." + port + "()") != std::string::npos) {
                            continue;
                        }
                        auto mapped = mod->wireMap.count(port) ? mod->wireMap[port] : port;
                        applyGenerateReplacements(mapped);
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, mapped, mapped, true, params, methodLoopHeaders});
                    }
                }
            }
        }
        else if (node.kind == SyntaxKind::AlwaysBlock ||
                 node.kind == SyntaxKind::AlwaysCombBlock ||
                 node.kind == SyntaxKind::AlwaysFFBlock ||
                 node.kind == SyntaxKind::AlwaysLatchBlock ||
                 node.kind == SyntaxKind::InitialBlock ||
                 node.kind == SyntaxKind::FinalBlock) {
            auto& proc = node.as<ProceduralBlockSyntax>();
            bool comb = tok(proc.keyword).find("comb") != std::string::npos;
            if (proc.statement->kind == SyntaxKind::TimingControlStatement) {
                auto& t = proc.statement->as<TimingControlStatementSyntax>();
                comb = comb || t.timingControl->kind == SyntaxKind::ImplicitEventControl;
            }
            if (comb) {
                auto savedLoopVars = loopVars;
                loopVars.clear();
                if (!index.empty()) {
                    loopVars.insert(index);
                }
                collectLoopVars(*proc.statement);
                std::vector<std::string> lines;
                localTypeScopes.push_back({});
                emitStatement(*proc.statement, lines, true, 0);
                localTypeScopes.pop_back();
                for (auto& line : lines) {
                    applyGenerateReplacements(line);
                    for (const auto& out : mod->outputPortCppNames) {
                        hdlcpp::rewriteLhsBase(line, combStorageName(*mod, out.first), out.first);
                        hdlcpp::rewriteLhsBase(line, outputStorageName(*mod, out.first), out.first);
                        hdlcpp::rewriteLhsBase(line, out.second, out.first);
                    }
                }
                auto scopedLines = lines;
                std::set<std::string> localNames;
                for (const auto& line : scopedLines) {
                    auto local = hdlcpp::declarationName(line);
                    if (!local.empty()) {
                        localNames.insert(local);
                    }
                }
                if (!methodLoopHeaders.empty() || (!index.empty() && !indexLimit.empty())) {
                    std::vector<std::string> wrapped;
                    if (!methodLoopHeaders.empty()) {
                        for (auto& header : methodLoopHeaders) {
                            wrapped.push_back(header);
                        }
                    }
                    else {
                        wrapped.push_back("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + indexLimit + ");i++) {");
                    }
                    for (auto& line : lines) {
                        wrapped.push_back("    " + line);
                    }
                    auto closeCount = !methodLoopHeaders.empty() ? methodLoopHeaders.size() : 1;
                    for (size_t n = 0; n < closeCount; ++n) {
                        wrapped.push_back("}");
                    }
                    scopedLines.swap(wrapped);
                }
                std::set<std::string> assignedBases;
                collectAssignedBases(*proc.statement, assignedBases);
                for (const auto& line : scopedLines) {
                    auto base = hdlcpp::assignmentBase(line);
                    if (!base.empty() && !localNames.count(base)) {
                        assignedBases.insert(base);
                    }
                }
                for (const auto& local : localNames) {
                    assignedBases.erase(local);
                }
                for (const auto& line : scopedLines) {
                    auto base = hdlcpp::assignmentBase(line);
                    if (base.empty() || localNames.count(base) || mod->types.count(base) || mod->outputPortCppNames.count(base)) {
                        continue;
                    }
                    auto rhs = hdlcpp::assignmentRhs(line);
                    auto width = foldWidth(typeWidth(expressionStorageType(*mod, rhs)));
                    std::string type;
                    if (isNumber(width) && width != "0") {
                        type = "logic<" + width + ">";
                    }
                    else {
                        type = "logic<1>";
                    }
                    mod->types[base] = type;
                    if (!mod->varNames.count(base)) {
                        mod->vars.push_back({type, base});
                        mod->varNames.insert(base);
                    }
                }
                std::vector<std::string> assignedList(assignedBases.begin(), assignedBases.end());
                bool onlyGenerateLocalAssignments = !assignedList.empty();
                for (const auto& base : assignedList) {
                    if (!isGenerateLocalName(base)) {
                        onlyGenerateLocalAssignments = false;
                        break;
                    }
                }
                if (onlyGenerateLocalAssignments) {
                    auto guard = generateLocalGuardExpr(methodLoopHeaders);
                    for (const auto& base : assignedList) {
                        auto typeIt = mod->types.find(base);
                        auto type = typeIt == mod->types.end() ? std::string("logic<1>") : unwrapRegType(typeIt->second);
                        std::ostringstream expr;
                        expr << "([&]() { " << type << " " << base << " = {}; ";
                        for (const auto& other : assignedList) {
                            if (other != base) {
                                auto otherTypeIt = mod->types.find(other);
                                auto otherType = otherTypeIt == mod->types.end() ? std::string("logic<1>") : unwrapRegType(otherTypeIt->second);
                                expr << otherType << " " << other << " = {}; ";
                            }
                        }
                        for (const auto& line : lines) {
                            auto text = trim(line);
                            if (!text.empty()) {
                                expr << text << " ";
                            }
                        }
                        expr << "return " << base << "; }())";
                        storeGenerateLocalExpr(base, expr.str(), guard);
                    }
                    loopVars = savedLoopVars;
                    return;
                }
                auto plan = hdlcpp::planCombExtraction(scopedLines, assignedList);
                if (const char* debugModules = std::getenv("HDLCPP_DEBUG_COMB_MODULES");
                    debugModules && std::string(debugModules).find(mod->name) != std::string::npos) {
                    std::cerr << "HDLCPP_COMB_DEBUG module=" << mod->name << "\n";
                    std::cerr << "assigned:";
                    for (const auto& base : assignedList) {
                        std::cerr << " " << base;
                    }
                    std::cerr << "\nindependent:";
                    for (const auto& base : plan.independent) {
                        std::cerr << " " << base;
                    }
                    std::cerr << "\ncombined:";
                    for (const auto& base : plan.combined) {
                        std::cerr << " " << base;
                    }
                    std::cerr << "\nlines:\n";
                    for (const auto& line : scopedLines) {
                        std::cerr << line << "\n";
                    }
                    std::cerr << "HDLCPP_COMB_DEBUG_END\n";
                }

                auto isCombAssignableBase = [&](const std::string& base) {
                    if (base.empty()) {
                        return false;
                    }
                    if (mod->outputPortCppNames.count(base)) {
                        return true;
                    }
                    return mod->types.count(base) && mod->types[base].rfind("reg<", 0) != 0;
                };

                for (const auto& base : plan.independent) {
                    if (isCombAssignableBase(base)) {
                        mod->combAssignedVars.insert(base);
                        PendingCombGen pending;
                        pending.lines = scopedLines;
                        pending.variables = assignedList;
                        pending.localNames = localNames;
                        mergePendingComb(*mod, base, pending);
                    }
                }
                loopVars = savedLoopVars;
            }
            else if (node.kind != SyntaxKind::InitialBlock && node.kind != SyntaxKind::FinalBlock) {
                auto savedLoopVars = loopVars;
                loopVars.clear();
                if (!index.empty()) {
                    loopVars.insert(index);
                }
                collectLoopVars(*proc.statement);
                MethodGen m;
                m.name = "always_" + std::to_string(mod->alwaysNo++);
                m.args = "bool reset";
                for (auto& header : methodLoopHeaders) {
                    m.body.push_back(header);
                }
                if (methodLoopHeaders.empty() && !index.empty() && !indexLimit.empty()) {
                    m.body.push_back("for (unsigned i = 0;(uint64_t)(i) < (uint64_t)(" + indexLimit + ");i++) {");
                }
                std::vector<std::string> lines;
                localTypeScopes.push_back({});
                emitStatement(*proc.statement, lines, false, 0);
                localTypeScopes.pop_back();
                for (auto line : lines) {
                    applyGenerateReplacements(line);
                    m.body.push_back(((!methodLoopHeaders.empty() || (!index.empty() && !indexLimit.empty())) ? "    " : "") + line);
                }
                if (methodLoopHeaders.empty() && !index.empty() && !indexLimit.empty()) {
                    m.body.push_back("}");
                }
                else {
                    for (size_t n = 0; n < methodLoopHeaders.size(); ++n) {
                        m.body.push_back("}");
                    }
                }
                mod->methods.push_back(m);
                loopVars = savedLoopVars;
            }
        }
        else if (node.kind == SyntaxKind::DataDeclaration) {
            auto& data = node.as<DataDeclarationSyntax>();
            bool hasGenerateLoopHeader = false;
            for (const auto& header : methodLoopHeaders) {
                auto trimmedHeader = trim(header);
                if (trimmedHeader.rfind("for ", 0) == 0 || trimmedHeader.rfind("for(", 0) == 0) {
                    hasGenerateLoopHeader = true;
                    break;
                }
            }
            for (auto d : data.declarators) {
                auto name = tok(d->name);
                auto type = typeText(*data.type);
                auto unpacked = dimensionWidths(d->dimensions);
                for (auto it = unpacked.rbegin(); it != unpacked.rend(); ++it) {
                    type = "array<" + type + "," + *it + ">";
                }
                if (mod->seqAssignedVars.count(name)) {
                    type = regTypeFor(type);
                }
                mod->types[name] = type;
                recordArrayLowerBounds(name, d->dimensions);
                if (!hasGenerateLoopHeader) {
                    if (!mod->varNames.count(name)) {
                        mod->vars.push_back({type, name});
                        mod->varNames.insert(name);
                    }
                }
                else {
                    markGenerateLocalName(name);
                    mod->assignLines.push_back("    " + type + " " + name + ";");
                }
            }
        }
        else if (node.kind == SyntaxKind::TypedefDeclaration) {
            handle(node.as<TypedefDeclarationSyntax>());
        }
        else if (node.kind == SyntaxKind::ParameterDeclarationStatement) {
            auto& stmt = node.as<ParameterDeclarationStatementSyntax>();
            bool hasGenerateLoopHeader = false;
            for (const auto& header : methodLoopHeaders) {
                auto trimmedHeader = trim(header);
                if (trimmedHeader.rfind("for ", 0) == 0 || trimmedHeader.rfind("for(", 0) == 0) {
                    hasGenerateLoopHeader = true;
                    break;
                }
            }
            if (stmt.parameter->kind == SyntaxKind::ParameterDeclaration) {
                auto& pd = stmt.parameter->as<ParameterDeclarationSyntax>();
                for (auto d : pd.declarators) {
                    auto name = tok(d->name);
                    auto type = varType(*pd.type, *d);
                    applyGenerateReplacements(type);
                    auto ctype = constexprType(type);
                    auto init = d->initializer ? cppExprText(d->initializer->toString()).substr(1) : "{}";
                    if (d->initializer && d->initializer->expr &&
                        (ctype == "unsigned" || ctype == "uint64_t" || type.rfind("logic<", 0) == 0)) {
                        init = emitUntypedNumericExpr(*d->initializer->expr);
                    }
                    if (!init.empty() && trim(init).front() == '"') {
                        init = "0";
                    }
                    applyGenerateReplacements(init);
                    auto constInit = constexprAggregateInit(type, trim(init));
                    if (hasGenerateLoopHeader) {
                        mod->assignLines.push_back("    const " + type + " " + name + " = " + constInit + ";");
                    }
                    else {
                        mod->constants.push_back({type, name + " = " + constInit});
                    }
                    mod->types[name] = type;
                }
            }
            else if (stmt.parameter->kind == SyntaxKind::TypeParameterDeclaration) {
                auto& td = stmt.parameter->as<TypeParameterDeclarationSyntax>();
                for (auto d : td.declarators) {
                    auto name = tok(d->name);
                    mod->types[name] = name;
                }
            }
        }
        else if (node.kind == SyntaxKind::ContinuousAssign) {
            for (auto a : node.as<ContinuousAssignSyntax>().assignments) {
                if (a->kind == SyntaxKind::AssignmentExpression) {
                    auto& b = a->as<BinaryExpressionSyntax>();
                    auto base = assignedBase(*b.left);
                    auto wholeNetAssign = b.left->kind == SyntaxKind::IdentifierName || isWholeObjectSelect(*b.left, base);
                    auto lhs = (mod->outputPortCppNames.count(base) && wholeNetAssign) ? mod->outputPortCppNames[base] :
                        (wholeNetAssign && !base.empty() ? base : emitLValue(*b.left));
                    if (base.empty()) {
                        base = baseFromLValueText(lhs);
                        wholeNetAssign = false;
                    }
                    auto internalWholeNet = !base.empty() && mod->varNames.count(base) && wholeNetAssign &&
                                            !mod->outputPortCppNames.count(base);
                    auto rhs = emitExpr(*b.right);
                    if (b.right->kind == SyntaxKind::ConditionalExpression &&
                        lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                        rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
                    }
                    applyGenerateReplacements(lhs);
                    applyGenerateReplacements(rhs);
                    if (!base.empty() && wholeNetAssign && isGenerateLocalName(base)) {
                        storeGenerateLocalExpr(base, "(" + rhs + ")", generateLocalGuardExpr(methodLoopHeaders));
                        continue;
                    }
                    if (addConcatOutputAssignments(*mod, lhs, rhs, methodLoopHeaders)) {
                        continue;
                    }
                    if (const char* traceAssign = std::getenv("HDLCPP_TRACE_CONT_ASSIGN");
                        traceAssign && std::string(traceAssign).find(mod->name) != std::string::npos) {
                        std::cerr << "HDLCPP_CONT_ASSIGN module=" << mod->name
                                  << " base=" << base
                                  << " lhs=" << lhs
                                  << " rhs=" << rhs
                                  << " loops=" << methodLoopHeaders.size()
                                  << " output=" << (mod->outputPortCppNames.count(base) ? 1 : 0)
                                  << " var=" << (mod->varNames.count(base) ? 1 : 0)
                                  << "\n";
                    }
                    if (!base.empty() && mod->outputPortCppNames.count(base) &&
                        !configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", mod->name)) {
                        addCombAssignment(*mod, base, lhs, rhs, methodLoopHeaders);
                        continue;
                    }
                    if (internalWholeNet || (!base.empty() && mod->varNames.count(base) &&
                        (lhs.find('[') != std::string::npos || lhs.find('.') != std::string::npos))) {
                        addCombAssignment(*mod, base, lhs, rhs, methodLoopHeaders);
                        continue;
                    }
                    // Dotted lhs here is a struct member/value assignment, not a port binding.
                    mod->assignLines.push_back("    " + lhs + " = " + rhs + ";");
                }
            }
        }
    }

    std::string assignWrapper(const std::string& rhs, const std::string& index)
    {
        std::vector<std::string> captures;
        auto addCapture = [&](const std::string& name) {
            if (!name.empty() && std::find(captures.begin(), captures.end(), name) == captures.end()) {
                captures.push_back(name);
            }
        };
        addCapture(index);
        const std::string names[] = {"i", "j", "k", "m", "z_gen", "w_gen"};
        for (auto& name : names) {
            if (rhs.find(name) != std::string::npos && isIdentifierUsed(rhs, name)) {
                addCapture(name);
            }
        }
        auto isCombAccessRef = [](std::string text) {
            auto expr = trim(std::move(text));
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
            return rest.empty() || rest.front() == '[' || rest.front() == '.';
        };
        auto isParentPortAccessRef = [&](std::string text) {
            if (!mod) {
                return false;
            }
            auto expr = trim(std::move(text));
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
            for (const auto& port : mod->ports) {
                std::vector<std::string> names{port.name};
                auto mapped = mod->portCppNames.find(port.name);
                if (mapped != mod->portCppNames.end()) {
                    names.push_back(mapped->second);
                }
                for (const auto& item : mod->portCppNames) {
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
        auto comb = isSimpleCombRef(rhs) || isCombAccessRef(rhs) ||
                    rhs.find("_comb_func()") != std::string::npos ||
                    isParentPortAccessRef(rhs);
        auto stateDependentExpr = mod && exprReferencesModuleState(*mod, rhs);
        auto combDrivenLocal = false;
        auto regBindable = [&]() {
            auto s = trim(rhs);
            if (!mod || s.empty() || comb) {
                return false;
            }
            auto base = baseFromLValueText(s);
            if (!base.empty() && mod->varNames.count(base)) {
                combDrivenLocal = !mod->seqAssignedVars.count(base) &&
                (mod->combAssignedVars.count(base) ||
                 mod->combMethodByBase.count(base) ||
                 mod->combSideEffectDriver.count(base));
            }
            if (base.empty() || !mod->varNames.count(base) || combDrivenLocal ||
                s.find('(') != std::string::npos ||
                s.find('?') != std::string::npos || s.find(':') != std::string::npos ||
                s.find('{') != std::string::npos || s.find('}') != std::string::npos) {
                return false;
            }
            for (char ch : s) {
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
                    ch == '[' || ch == ']' || ch == '.' || std::isspace(static_cast<unsigned char>(ch))) {
                    continue;
                }
                return false;
            }
            return true;
        }();
        auto needsCombWrapper = comb || combDrivenLocal || (stateDependentExpr && !regBindable);
        auto assignPrefix = [&](const std::string& valuePrefix, const std::string& combPrefix) {
            return regBindable ? std::string("_ASSIGN_REG") : (needsCombWrapper ? combPrefix : valuePrefix);
        };
        if (captures.empty()) {
            return assignPrefix("_ASSIGN", "_ASSIGN_COMB") + "( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "i") {
            return assignPrefix("_ASSIGN_I", "_ASSIGN_COMB_I") + "( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "j") {
            return assignPrefix("_ASSIGN_J", "_ASSIGN_COMB_J") + "( " + rhs + " )";
        }
        if (captures.size() == 2 && captures[0] == "i" && captures[1] == "j") {
            return assignPrefix("_ASSIGN_IJ", "_ASSIGN_COMB_IJ") + "( " + rhs + " )";
        }
        std::string capList;
        for (size_t n = 0; n < captures.size(); ++n) {
            if (n) {
                capList += ",";
            }
            capList += captures[n];
        }
        return (regBindable ? std::string("_ASSIGN_REG_INDEXED") :
            std::string(needsCombWrapper ? "_ASSIGN_COMB_INDEXED" : "_ASSIGN_INDEXED")) + "((" + capList + "), " + rhs + " )";
    }

    std::string assignRefWrapper(const std::string& rhs, const std::string& index)
    {
        std::vector<std::string> captures;
        auto addCapture = [&](const std::string& name) {
            if (!name.empty() && std::find(captures.begin(), captures.end(), name) == captures.end()) {
                captures.push_back(name);
            }
        };
        addCapture(index);
        const std::string names[] = {"i", "j", "k", "m", "z_gen", "w_gen"};
        for (auto& name : names) {
            if (rhs.find(name) != std::string::npos && isIdentifierUsed(rhs, name)) {
                addCapture(name);
            }
        }
        if (captures.empty()) {
            return "_ASSIGN_REG( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "i") {
            return "_ASSIGN_REG_I( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "j") {
            return "_ASSIGN_REG_J( " + rhs + " )";
        }
        if (captures.size() == 2 && captures[0] == "i" && captures[1] == "j") {
            return "_ASSIGN_REG_IJ( " + rhs + " )";
        }
        std::string capList;
        for (size_t n = 0; n < captures.size(); ++n) {
            if (n) {
                capList += ",";
            }
            capList += captures[n];
        }
        return "_ASSIGN_REG_INDEXED((" + capList + "), " + rhs + " )";
    }

    std::string baseFromLValueText(const std::string& lhs)
    {
        auto s = trim(lhs);
        auto end = s.find_first_of("[.");
        if (end != std::string::npos) {
            s = s.substr(0, end);
        }
        return trim(s);
    }

    std::vector<std::string> splitTopLevelCommaList(const std::string& text)
    {
        std::vector<std::string> out;
        std::string cur;
        int paren = 0;
        int bracket = 0;
        int brace = 0;
        int angle = 0;
        for (char c : text) {
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
            else if (c == '<') {
                ++angle;
            }
            else if (c == '>' && angle > 0) {
                --angle;
            }

            if (c == ',' && paren == 0 && bracket == 0 && brace == 0 && angle == 0) {
                auto item = trim(cur);
                if (!item.empty()) {
                    out.push_back(item);
                }
                cur.clear();
            }
            else {
                cur.push_back(c);
            }
        }
        auto item = trim(cur);
        if (!item.empty()) {
            out.push_back(item);
        }
        return out;
    }

    std::vector<std::string> splitConcatLValueArgs(const std::string& lhs)
    {
        auto s = trim(lhs);
        if (s.rfind("cat{", 0) == 0 && s.size() >= 5 && s.back() == '}') {
            return splitTopLevelCommaList(s.substr(4, s.size() - 5));
        }
        if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
            return splitTopLevelCommaList(s.substr(1, s.size() - 2));
        }
        else {
            return {};
        }
    }

    bool addConcatOutputAssignments(ModuleGen& target, const std::string& lhs, const std::string& rhs,
                                    const std::vector<std::string>& loopHeaders = {})
    {
        auto args = splitConcatLValueArgs(lhs);
        if (args.empty()) {
            return false;
        }

        std::string offset = "0";
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            auto elem = trim(*it);
            auto base = baseFromLValueText(elem);
            if (base.empty() || !target.types.count(base)) {
                return false;
            }
            auto elemType = expressionStorageType(target, elem);
            if (elemType.empty()) {
                elemType = target.types[base];
            }
            auto width = typeWidth(elemType);
            if (width.empty()) {
                width = "sizeof(" + elemType + ")*8";
            }
            width = foldWidth(width);
            auto hi = offset == "0" ? "((" + width + ") - 1)" : "((" + offset + ") + (" + width + ") - 1)";
            auto slice = "logic<" + width + ">(" + rhs + ".bits((uint64_t)(" + hi + "),(uint64_t)(" + offset + ")))";
            addCombAssignment(target, base, elem, slice, loopHeaders);
            offset = "((" + offset + ") + (" + width + "))";
        }
        return true;
    }

    bool methodHasCombSideEffects(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.localCombBody) {
            return false;
        }
        if (method.name.find("_comb_func") == std::string::npos || method.returnName.empty()) {
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
            auto internalDriverWithOutputSideEffect =
                mod.outputPortCppNames.count(base) && !mod.outputPortCppNames.count(method.returnBase);
            if (!mod.combSideEffectChildInputReads.count(base) && !internalDriverWithOutputSideEffect) {
                continue;
            }
            return true;
        }
        return false;
    }

    bool emitPlainCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        return methodHasCombSideEffects(mod, method);
    }

    bool isPlainCombDriver(const ModuleGen& mod, const std::string& driver)
    {
        for (auto& method : mod.methods) {
            if (method.name == driver) {
                return emitPlainCombMethod(mod, method);
            }
        }
        return false;
    }

    std::string emitCombOutputRead(const ModuleGen& mod, const std::string& svName,
                                   const std::string& field = "",
                                   const std::string& excludeDriver = "")
    {
        auto storageName = outputStorageName(mod, svName);
        if (mod.seqAssignedVars.count(svName)) {
            return storageName;
        }
        struct CombOutputReadCache {
            const ModuleGen* mod = nullptr;
            size_t methodSize = 0;
            size_t wireSize = 0;
            size_t sideEffectSize = 0;
            std::map<std::pair<std::string, std::string>, std::vector<std::string>> driversByOutput;
        };
        static CombOutputReadCache cache;
        if (cache.mod != &mod ||
            cache.methodSize != mod.methods.size() ||
            cache.wireSize != mod.wireMap.size() ||
            cache.sideEffectSize != mod.combSideEffectDriver.size()) {
            cache = {};
            cache.mod = &mod;
            cache.methodSize = mod.methods.size();
            cache.wireSize = mod.wireMap.size();
            cache.sideEffectSize = mod.combSideEffectDriver.size();
        }
        auto cachedDriversFor = [&](const std::string& name, const std::string& member) -> std::vector<std::string> {
            auto key = std::make_pair(name, member);
            auto it = cache.driversByOutput.find(key);
            if (it != cache.driversByOutput.end()) {
                return it->second;
            }
            auto drivers = combDriversFor(mod, name, member);
            auto [inserted, _] = cache.driversByOutput.emplace(std::move(key), drivers);
            return inserted->second;
        };
        auto drivers = cachedDriversFor(svName, field);
        if (drivers.empty() && !field.empty()) {
            drivers = cachedDriversFor(svName, "");
        }
        if (drivers.empty()) {
            return storageName;
        }
        bool hasExternalDriver = false;
        for (auto& driver : drivers) {
            if (!excludeDriver.empty() && driver == excludeDriver) {
                continue;
            }
            hasExternalDriver = true;
            break;
        }
        if (!hasExternalDriver) {
            return storageName;
        }
        return storageName + "_func()";
    }

    std::vector<std::string> sideEffectCombCalls(const ModuleGen& mod)
    {
        std::vector<std::string> calls;
        for (auto& method : mod.methods) {
            if (methodHasCombSideEffects(mod, method)) {
                calls.push_back(method.name + "();");
            }
        }
        return calls;
    }

    bool isControlOrScopeLine(const std::string& line)
    {
        auto t = trim(line);
        return t.empty() || t == "{" || t == "}" || t == "};" || t == "else {" ||
               t.rfind("for ", 0) == 0 || t.rfind("for(", 0) == 0 ||
               t.rfind("if ", 0) == 0 || t.rfind("if(", 0) == 0 ||
               t.rfind("if constexpr", 0) == 0 || t.rfind("else", 0) == 0 ||
               t.rfind("switch ", 0) == 0 || t.rfind("switch(", 0) == 0 ||
               t.rfind("case ", 0) == 0 || t.rfind("default:", 0) == 0 ||
               t == "break;" || t == "continue;";
    }

    bool isStructuralAssignLine(const std::string& line)
    {
        auto t = trim(line);
        if (t.find("._assign(") != std::string::npos) {
            return true;
        }
        auto eq = t.find("=");
        if (eq == std::string::npos) {
            return false;
        }
        auto rhs = trim(t.substr(eq + 1));
        return rhs.rfind("_ASSIGN", 0) == 0;
    }

    bool isRuntimeAssignLine(const std::string& line)
    {
        return !isControlOrScopeLine(line) && !isStructuralAssignLine(line);
    }

    bool hasRuntimeAssignLines(const ModuleGen& mod)
    {
        for (auto& line : mod.assignLines) {
            if (isRuntimeAssignLine(line)) {
                return true;
            }
        }
        return false;
    }

    void movePartialOutputAssignLinesToComb(ModuleGen& target)
    {
        std::vector<std::string> kept;
        for (auto& line : target.assignLines) {
            auto t = trim(line);
            auto eq = t.find('=');
            if (eq == std::string::npos) {
                kept.push_back(line);
                continue;
            }
            auto lhs = trim(t.substr(0, eq));
            auto rhs = trim(t.substr(eq + 1));
            if (!rhs.empty() && rhs.back() == ';') {
                rhs.pop_back();
                rhs = trim(rhs);
            }
            auto base = baseFromLValueText(lhs);
            auto containsIdentifier = [](const std::string& text, const std::string& id) {
                for (size_t pos = 0; (pos = text.find(id, pos)) != std::string::npos; ++pos) {
                    auto before = pos == 0 ? '\0' : text[pos - 1];
                    auto after = pos + id.size() >= text.size() ? '\0' : text[pos + id.size()];
                    if (!std::isalnum(static_cast<unsigned char>(before)) && before != '_' &&
                        !std::isalnum(static_cast<unsigned char>(after)) && after != '_') {
                        return true;
                    }
                }
                return false;
            };
            auto loopScoped = containsIdentifier(lhs, "i") || containsIdentifier(rhs, "i") ||
                              containsIdentifier(lhs, "j") || containsIdentifier(rhs, "j") ||
                              containsIdentifier(lhs, "k") || containsIdentifier(rhs, "k") ||
                              containsIdentifier(lhs, "m") || containsIdentifier(rhs, "m");
            if (!base.empty() && target.outputPortCppNames.count(base) && lhs != target.outputPortCppNames[base] &&
                !loopScoped && !configuredNameEquals("HDLCPP_INLINE_COMB_MODULES", target.name)) {
                addCombAssignment(target, base, lhs, rhs);
                continue;
            }
            kept.push_back(line);
        }
        target.assignLines.swap(kept);
    }

    void addCombAssignment(ModuleGen& target, const std::string& svBase, const std::string& lhs, const std::string& rhs,
                           const std::vector<std::string>& loopHeaders = {})
    {
        auto base = !svBase.empty() ? svBase : baseFromLValueText(lhs);
        if (base.empty() || (!target.types.count(base) && !target.outputPortCppNames.count(base))) {
            return;
        }

        auto retType = target.types.count(base) ? unwrapRegType(target.types[base]) : std::string();
        auto returnName = combStorageName(target, base);
        if (target.outputPortCppNames.count(base)) {
            auto cppName = target.outputPortCppNames[base];
            auto storageType = outputStorageType(target, base, cppName);
            if (!storageType.empty()) {
                retType = storageType;
            }
        }

        auto methodName = base + "_comb_func";
        MethodGen* method = nullptr;
        auto it = target.combMethodByBase.find(base);
        if (it != target.combMethodByBase.end()) {
            method = &target.methods[it->second];
            target.combReturnTypes[method->returnName] = retType;
        }
        else {
            MethodGen m;
            m.name = methodName;
            m.ret = retType + "&";
            m.returnName = returnName;
            m.returnBase = base;
            target.combReturnTypes[returnName] = retType;
            target.combMethodByBase[base] = target.methods.size();
            target.wireMap[base] = methodName;
            target.combAssignedVars.insert(base);
            if (target.seqAssignedVars.count(base)) {
                m.body.push_back(returnName + " = (*this)." + base + ";");
            }
            target.methods.push_back(m);
            method = &target.methods.back();
        }
        target.combAssignedVars.insert(base);

        auto finalRhs = rhs;
        if (retType == "bool" || retType == "u1") {
            finalRhs = truthyExpr(finalRhs, target.types.count(base) ? typeWidth(target.types[base]) : "1");
        }

        auto bodyLhs = lhs;
        if (target.outputPortCppNames.count(base) && trim(bodyLhs) == target.outputPortCppNames[base]) {
            bodyLhs = base;
        }
        if (target.seqAssignedVars.count(base) && trim(bodyLhs) == base) {
            bodyLhs = returnName;
        }
        auto designatedAggregateRhs = [](const std::string& text) {
            auto s = trim(text);
            return s.rfind("{.", 0) == 0 || s.rfind("{ .", 0) == 0;
        };
        if (designatedAggregateRhs(finalRhs)) {
            auto targetType = unwrapRegType(expressionStorageType(target, bodyLhs));
            if (targetType.empty()) {
                targetType = unwrapRegType(retType);
            }
            if (!targetType.empty()) {
                finalRhs = namedAggregateToTypedExpression(targetType, finalRhs);
            }
        }

        auto wholeArrayAssignmentNeedsElementUnpack = [&]() {
            auto lhsText = trim(bodyLhs);
            if (lhsText.empty() || lhsText.find('[') != std::string::npos ||
                lhsText.find('.') != std::string::npos) {
                return false;
            }
            auto targetType = unwrapRegType(expressionStorageType(target, bodyLhs));
            if (targetType.empty()) {
                targetType = unwrapRegType(retType);
            }
            auto sourceType = unwrapRegType(expressionStorageType(target, finalRhs));
            auto targetArgs = templateArgsFor(targetType, targetType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            auto sourceArgs = templateArgsFor(sourceType, sourceType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
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

        // Procedural assignments to fields are value updates. Port/function_ref binding is
        // handled when emitting port initializers and instance connections.
        if (!loopHeaders.empty()) {
            for (auto& header : loopHeaders) {
                method->body.push_back(header);
            }
            for (const auto& line : target.assignLines) {
                auto trimmed = trim(line);
                if (trimmed.rfind("static constexpr ", 0) == 0) {
                    method->body.push_back(line);
                }
            }
            method->body.push_back("    " + bodyLhs + " = " + finalRhs + ";");
            for (size_t n = 0; n < loopHeaders.size(); ++n) {
                method->body.push_back("}");
            }
        }
        else if (wholeArrayAssignmentNeedsElementUnpack()) {
            auto targetType = unwrapRegType(expressionStorageType(target, bodyLhs));
            if (targetType.empty()) {
                targetType = unwrapRegType(retType);
            }
            auto targetArgs = templateArgsFor(targetType, targetType.rfind("std::array<", 0) == 0 ? "std::array" : "array");
            auto targetElem = trim(targetArgs[0]);
            method->body.push_back("{");
            method->body.push_back("    using __cpphdl_target_array_t = " + targetType + ";");
            method->body.push_back("    " + bodyLhs + " = {};");
            method->body.push_back("    auto __cpphdl_src = " + finalRhs + ";");
            method->body.push_back("    for (std::size_t __cpphdl_i = 0; __cpphdl_i < (__cpphdl_target_array_t::SIZE_BITS / __cpphdl_target_array_t::ELEMENT_BITS); ++__cpphdl_i) {");
            method->body.push_back("        " + bodyLhs + "[__cpphdl_i] = cpphdl::unpack_value<" + targetElem + ">(cpphdl::pack_value<cpphdl::type_width<" + targetElem + ">()>(__cpphdl_src[__cpphdl_i]));");
            method->body.push_back("    }");
            method->body.push_back("}");
        }
        else {
            method->body.push_back(bodyLhs + " = " + finalRhs + ";");
        }
    }

    std::string qualifyImportedValueName(const std::string& name)
    {
        if (!mod || name.empty() || name.find("::") != std::string::npos) {
            return name;
        }
        if (loopVars.count(name) || mod->valueNames.count(name) ||
            mod->types.count(name) || mod->typeParamNames.count(name) ||
            mod->varNames.count(name) || mod->portNames.count(name) || mod->portCppNames.count(name) ||
            mod->outputPortCppNames.count(name) || mod->wireMap.count(name) ||
            mod->functionReturnTypes.count(name) || mod->combReturnTypes.count(name)) {
            return name;
        }
        for (const auto& constant : mod->constants) {
            auto text = trim(constant.second);
            auto eq = text.find('=');
            auto constantName = trim(eq == std::string::npos ? text : text.substr(0, eq));
            if (constantName == name) {
                return name;
            }
        }

        std::string qualified;
        bool ambiguous = false;
        for (const auto& import : mod->imports) {
            auto* package = findModule(import);
            if (!package || !package->isPackage || !package->packageValueNames.count(name)) {
                continue;
            }
            auto candidate = import + "::" + name;
            if (qualified.empty()) {
                qualified = candidate;
            }
            else if (qualified != candidate) {
                ambiguous = true;
                break;
            }
        }
        if (!ambiguous && !qualified.empty()) {
            return qualified;
        }
        auto isConstantLike = [](const std::string& text) {
            bool sawUpper = false;
            for (auto ch : text) {
                if (std::isupper(static_cast<unsigned char>(ch))) {
                    sawUpper = true;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '_') {
                    continue;
                }
                return false;
            }
            return sawUpper;
        };
        if (mod->imports.size() == 1 && isConstantLike(name)) {
            return mod->imports.front() + "::" + name;
        }
        return name;
    }

    std::string qualifyImportedValueIdentifiers(std::string s)
    {
        if (!mod || mod->imports.empty()) {
            return s;
        }
        for (size_t pos = 0; pos < s.size();) {
            if (!(std::isalpha(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
                ++pos;
                continue;
            }
            auto start = pos++;
            while (pos < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
                ++pos;
            }
            auto before = start == 0 ? '\0' : s[start - 1];
            auto after = pos >= s.size() ? '\0' : s[pos];
            if (before == ':' || before == '.' || after == ':' ||
                std::isalnum(static_cast<unsigned char>(before)) || before == '_') {
                continue;
            }
            auto name = s.substr(start, pos - start);
            auto qualified = qualifyImportedValueName(name);
            if (qualified != name) {
                s.replace(start, name.size(), qualified);
                pos = start + qualified.size();
            }
        }
        return s;
    }

    std::string translateExpr(std::string s)
    {
        s = cppExprText(s);
        if (!mod) {
            return s;
        }
        s = qualifyImportedValueIdentifiers(std::move(s));
        s = replaceSystemBitsOfKnownTypes(std::move(s), "cpphdl");
        for (auto& p : mod->portNames) {
            for (size_t pos = 0; (pos = s.find(p, pos)) != std::string::npos; ) {
                auto before = pos == 0 ? '\0' : s[pos - 1];
                auto after = pos + p.size() >= s.size() ? '\0' : s[pos + p.size()];
                auto next = pos + p.size();
                while (next < s.size() && std::isspace((unsigned char)s[next])) {
                    ++next;
                }
                bool leftOk = !std::isalnum((unsigned char)before) && before != '_';
                bool rightOk = !std::isalnum((unsigned char)after) && after != '_';
                if (leftOk && rightOk && (next >= s.size() || s[next] != '(')) {
                    auto cppName = mod->portCppNames.count(p) ? mod->portCppNames[p] : p;
                    auto replacement = cppName + "()";
                    s.replace(pos, p.size(), replacement);
                    pos += replacement.size();
                }
                else {
                    pos += p.size();
                }
            }
        }
        return s;
    }

    std::string typeText(const DataTypeSyntax& type)
    {
        auto qualifyImportedType = [&](const std::string& name) -> std::string {
            if (!mod || name.find("::") != std::string::npos || name.find('<') != std::string::npos ||
                name == "bool" || name == "unsigned" || name == "u1" || name == "u8" ||
                name == "u16" || name == "u32" || name == "u64" || name == "uint64_t" ||
                name == "std::string") {
                return name;
            }
            if (mod->types.count(name) || mod->typeParamNames.count(name)) {
                return name;
            }
            for (auto& import : mod->imports) {
                auto* package = findModule(import);
                if (package && package->isPackage && package->types.count(name)) {
                    return import + "::" + name;
                }
                if (configuredNameEquals("HDLCPP_QUALIFY_IMPORTED_TYPE_PACKAGES", import) && name.size() >= 2 &&
                    name.substr(name.size() - 2) == "_t") {
                    return import + "::" + name;
                }
            }
            return name;
        };

        auto rawTypeText = trim(exprText(type.toString()));
        if (rawTypeText == "string") {
            return "std::string";
        }
        if (IntegerTypeSyntax::isKind(type.kind)) {
            auto& t = type.as<IntegerTypeSyntax>();
            auto keyword = tok(t.keyword);
            auto packedWidths = dimensionWidths(t.dimensions);
            if (packedWidths.size() > 1 && (keyword == "logic" || keyword == "wire" || keyword == "bit")) {
                auto elemType = "logic<" + packedWidths.back() + ">";
                for (auto it = packedWidths.rbegin() + 1; it != packedWidths.rend(); ++it) {
                    elemType = arrayTypeForDimension(elemType, *it, true);
                }
                return elemType;
            }
            auto width = dimensionsWidth(t.dimensions);
            if (width.empty()) {
                if (keyword == "reg") {
                    return "reg<logic<1>>";
                }
                if (keyword == "logic" || keyword == "wire" || keyword == "bit") {
                    return "logic<1>";
                }
                if (keyword == "byte") {
                    return "u8";
                }
                if (keyword == "int" || keyword == "integer") {
                    return "u32";
                }
                if (keyword == "shortint") {
                    return "int16_t";
                }
                if (keyword == "longint" || keyword == "time") {
                    return "u64";
                }
                if (keyword == "string") {
                    return "std::string";
                }
                return keyword;
            }
            return width.rfind("clog2(", 0) == 0 ? "u<" + width + ">" : "logic<" + width + ">";
        }
        if (type.kind == SyntaxKind::ImplicitType) {
            auto& t = type.as<ImplicitTypeSyntax>();
            auto packedWidths = dimensionWidths(t.dimensions);
            if (packedWidths.size() > 1) {
                auto elemType = "logic<" + packedWidths.back() + ">";
                for (auto it = packedWidths.rbegin() + 1; it != packedWidths.rend(); ++it) {
                    elemType = arrayTypeForDimension(elemType, *it, true);
                }
                return elemType;
            }
            auto width = dimensionsWidth(t.dimensions);
            return width.empty() ? "logic<1>" : "logic<" + width + ">";
        }
        if (type.kind == SyntaxKind::EnumType) {
            return "logic<32>";
        }
        if (type.kind == SyntaxKind::NamedType) {
            auto raw = exprText(type.toString());
            if (raw == "string") {
                return "std::string";
            }
            auto bracket = raw.find('[');
            if (bracket != std::string::npos) {
                auto base = trim(raw.substr(0, bracket));
                base = qualifyImportedType(base);
                if (base == "logic" || base == "bit" || base == "wire") {
                    std::vector<std::string> widths;
                    while (bracket != std::string::npos) {
                        auto end = raw.find(']', bracket);
                        if (end == std::string::npos) {
                            break;
                        }
                        widths.push_back(textRangeWidth(raw.substr(bracket, end - bracket + 1)));
                        bracket = raw.find('[', end + 1);
                    }
                    if (widths.empty()) {
                        return "logic<1>";
                    }
                    auto out = "logic<" + widths.back() + ">";
                    for (auto it = widths.rbegin() + 1; it != widths.rend(); ++it) {
                        out = arrayTypeForDimension(out, *it, true);
                    }
                    return out;
                }
                std::vector<std::string> widths;
                while (bracket != std::string::npos) {
                    auto end = raw.find(']', bracket);
                    if (end == std::string::npos) {
                        break;
                    }
                    widths.push_back(textRangeWidth(raw.substr(bracket, end - bracket + 1)));
                    bracket = raw.find('[', end + 1);
                }
                for (auto it = widths.rbegin(); it != widths.rend(); ++it) {
                    base = arrayTypeForDimension(base, *it, true);
                }
                return base;
            }
            auto& name = *type.as<NamedTypeSyntax>().name;
            if (name.kind == SyntaxKind::IdentifierSelectName) {
                auto& n = name.as<IdentifierSelectNameSyntax>();
                auto width = selectsWidth(n.selectors);
                if (!width.empty()) {
                    return arrayTypeForDimension(qualifyImportedType(tok(n.identifier)), width, true);
                }
            }
            return qualifyImportedType(exprText(type.as<NamedTypeSyntax>().name->toString()));
        }
        return exprText(type.toString());
    }

    std::string varType(const DataTypeSyntax& type, const DeclaratorSyntax& decl)
    {
        if (IntegerTypeSyntax::isKind(type.kind)) {
            auto& t = type.as<IntegerTypeSyntax>();
            auto packed = dimensionWidths(t.dimensions);
            auto unpacked = dimensionWidths(decl.dimensions);
            auto keyword = tok(t.keyword);
            if (packed.empty() && unpacked.empty() && (keyword == "logic" || keyword == "wire" || keyword == "bit")) {
                return "logic<1>";
            }
            if (packed.size() > 1 && unpacked.empty() && (keyword == "logic" || keyword == "wire" || keyword == "bit")) {
                auto elemType = "logic<" + packed.back() + ">";
                for (auto it = packed.rbegin() + 1; it != packed.rend(); ++it) {
                    elemType = arrayTypeForDimension(elemType, *it, true);
                }
                return elemType;
            }
            if (packed.size() == 2 && unpacked.size() == 1 && packed[1] == "8") {
                return "memory<u8," + packed[0] + "," + unpacked[0] + ">";
            }
            if (unpacked.size() == 1) {
                auto elemType = (packed.empty() && (keyword == "logic" || keyword == "wire" || keyword == "bit")) ? "logic<1>" : typeText(type);
                return "array<" + elemType + "," + unpacked[0] + ">";
            }
        }
        auto text = typeText(type);
        auto unpacked = dimensionWidths(decl.dimensions);
        for (auto it = unpacked.rbegin(); it != unpacked.rend(); ++it) {
            text = "array<" + text + "," + *it + ">";
        }
        if (type.kind == SyntaxKind::RegType && text.rfind("reg<", 0) != 0) {
            return (text == "bool" || text == "u1" || text == "logic<1>") ? "reg<logic<1>>" : "reg<" + text + ">";
        }
        return text;
    }

    template<typename T>
    std::vector<std::string> dimensionWidths(const T& dimensions)
    {
        std::vector<std::string> widths;
        for (auto d : dimensions) {
            auto w = dimensionWidth(*d);
            if (!w.empty()) {
                widths.push_back(w);
            }
        }
        return widths;
    }

    template<typename T>
    std::string dimensionsWidth(const T& dimensions)
    {
        auto widths = dimensionWidths(dimensions);
        if (widths.empty()) {
            return "";
        }
        std::string ret;
        for (auto& w : widths) {
            if (!ret.empty()) {
                ret += "*";
            }
            ret += w;
        }
        return ret;
    }

    std::string dimensionWidth(const VariableDimensionSyntax& dim)
    {
        if (!dim.specifier || dim.specifier->kind != SyntaxKind::RangeDimensionSpecifier) {
            return "";
        }
        auto& range = dim.specifier->as<RangeDimensionSpecifierSyntax>();
        if (range.selector->kind == SyntaxKind::BitSelect) {
            return emitExpr(*range.selector->as<BitSelectSyntax>().expr);
        }
        if (RangeSelectSyntax::isKind(range.selector->kind)) {
            auto& r = range.selector->as<RangeSelectSyntax>();
            auto& left = *r.left;
            uint64_t rawLeft = 0;
            uint64_t rawRight = 0;
            if (parseCppIntegralLiteral(exprText(r.left->toString()), rawLeft) &&
                parseCppIntegralLiteral(exprText(r.right->toString()), rawRight)) {
                return std::to_string((rawLeft > rawRight ? rawLeft - rawRight : rawRight - rawLeft) + 1);
            }
            auto rawLeftText = trim(exprText(r.left->toString()));
            auto rawRightText = trim(exprText(r.right->toString()));
            auto isConstValue = [](const std::string& text, uint64_t expected) {
                if (auto value = parseConfiguredUint(text)) {
                    return *value == expected;
                }
                return false;
            };
            if (isConstValue(rawLeftText, 0)) {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto width = minusOneRangeBaseWidth(*r.right);
                    if (!width.empty()) {
                        return width;
                    }
                }
            }
            if (isConstValue(rawRightText, 0)) {
                if (BinaryExpressionSyntax::isKind(r.left->kind)) {
                    auto width = minusOneRangeBaseWidth(*r.left);
                    if (!width.empty()) {
                        return width;
                    }
                }
                return rangeUpperPlusOneWidth(*r.left);
            }
            auto right = emitIndexExpr(*r.right);
            if (isConstValue(right, 0)) {
                if (BinaryExpressionSyntax::isKind(left.kind)) {
                    auto width = minusOneRangeBaseWidth(left);
                    if (!width.empty()) {
                        return width;
                    }
                }
                return rangeUpperPlusOneWidth(left);
            }
            auto leftExpr = emitIndexExpr(left);
            if (isConstValue(leftExpr, 0)) {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto& b = r.right->as<BinaryExpressionSyntax>();
                    auto bright = trim(emitUntypedNumericExpr(*b.right));
                    if (tok(b.operatorToken) == "-" && isConstValue(bright, 1)) {
                        return emitIndexExpr(*b.left);
                    }
                }
                auto e = foldWidth(right);
                if (isNumber(e)) {
                    return std::to_string(std::stoul(e) + 1);
                }
                return "(" + e + ")+1";
            }
            auto l = foldWidth(emitIndexExpr(left));
            right = foldWidth(right);
            if (isNumber(l) && isNumber(right)) {
                auto lv = std::stoul(l);
                auto rv = std::stoul(right);
                return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
            }
            return "(((uint64_t)(" + l + ") >= (uint64_t)(" + right + ") ? ((uint64_t)(" + l + ") - (uint64_t)(" + right + ")) : ((uint64_t)(" + right + ") - (uint64_t)(" + l + "))) + 1)";
        }
        return textRangeWidth(range.selector->toString());
    }

    template<typename T>
    std::vector<std::string> dimensionLowerBounds(const T& dimensions)
    {
        std::vector<std::string> bounds;
        for (auto d : dimensions) {
            auto b = dimensionLowerBound(*d);
            if (!b.empty()) {
                bounds.push_back(b);
            }
        }
        return bounds;
    }

    std::string dimensionLowerBound(const VariableDimensionSyntax& dim)
    {
        if (!dim.specifier || dim.specifier->kind != SyntaxKind::RangeDimensionSpecifier) {
            return "";
        }
        auto& range = dim.specifier->as<RangeDimensionSpecifierSyntax>();
        if (range.selector->kind == SyntaxKind::BitSelect) {
            return "0";
        }
        if (!RangeSelectSyntax::isKind(range.selector->kind)) {
            return "";
        }
        auto& r = range.selector->as<RangeSelectSyntax>();
        auto rawLeft = trim(exprText(r.left->toString()));
        auto rawRight = trim(exprText(r.right->toString()));
        auto rawLeftValue = parseConfiguredUint(rawLeft);
        auto rawRightValue = parseConfiguredUint(rawRight);
        if (rawLeftValue && rawRightValue) {
            return std::to_string(std::min(*rawLeftValue, *rawRightValue));
        }
        if ((rawLeftValue && *rawLeftValue == 0) || (rawRightValue && *rawRightValue == 0)) {
            return "0";
        }
        if (rawLeftValue && !rawRightValue) {
            return std::to_string(*rawLeftValue);
        }
        if (rawRightValue && !rawLeftValue) {
            return std::to_string(*rawRightValue);
        }
        auto left = emitIndexExpr(*r.left);
        auto right = emitIndexExpr(*r.right);
        uint64_t leftValue = 0;
        uint64_t rightValue = 0;
        if (parseCppIntegralLiteral(stripParens(left), leftValue) &&
            parseCppIntegralLiteral(stripParens(right), rightValue)) {
            return std::to_string(std::min(leftValue, rightValue));
        }
        if (isZeroIndexExpr(left) || isZeroIndexExpr(right)) {
            return "0";
        }
        if (sameSelectBound(left, right)) {
            return left;
        }
        return "(((uint64_t)(" + left + ") < (uint64_t)(" + right + ")) ? (" + left + ") : (" + right + "))";
    }

    bool exprConstValue(const ExpressionSyntax& expr, uint64_t expected)
    {
        std::vector<std::string> forms;
        forms.push_back(trim(exprText(expr.toString())));
        forms.push_back(trim(emitUntypedNumericExpr(expr)));
        forms.push_back(trim(emitIndexExpr(expr)));
        for (auto text : forms) {
            if (auto value = parseConfiguredUint(text)) {
                if (*value == expected) {
                    return true;
                }
            }
            uint64_t literal = 0;
            if (parseCppIntegralLiteral(stripParens(text), literal) && literal == expected) {
                return true;
            }
        }
        return false;
    }

    std::string minusOneRangeBaseWidth(const ExpressionSyntax& expr)
    {
        if (!BinaryExpressionSyntax::isKind(expr.kind)) {
            return "";
        }
        auto& b = expr.as<BinaryExpressionSyntax>();
        if (tok(b.operatorToken) != "-" || !exprConstValue(*b.right, 1)) {
            return "";
        }
        return emitIndexExpr(*b.left);
    }

    std::string rangeUpperPlusOneWidth(const ExpressionSyntax& expr)
    {
        uint64_t literal = 0;
        if (parseCppIntegralLiteral(exprText(expr.toString()), literal)) {
            return std::to_string(literal + 1);
        }
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return rangeUpperPlusOneWidth(*expr.as<ParenthesizedExpressionSyntax>().expression);
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto width = minusOneRangeBaseWidth(expr);
            if (!width.empty()) {
                return width;
            }
        }
        if (ConditionalExpressionSyntax::isKind(expr.kind)) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            return "(" + emitPredicate(*c.predicate) + " ? " + rangeUpperPlusOneWidth(*c.left) + " : " +
                   rangeUpperPlusOneWidth(*c.right) + ")";
        }
        auto emitted = foldWidth(emitIndexExpr(expr));
        if (isNumber(emitted)) {
            return std::to_string(std::stoul(emitted) + 1);
        }
        return "(" + emitted + ")+1";
    }

    template<typename T>
    std::string selectsWidth(const T& selectors)
    {
        std::string ret;
        for (auto s : selectors) {
            auto w = selectWidth(*s);
            if (!w.empty()) {
                if (!ret.empty()) {
                    ret += "*";
                }
                ret += w;
            }
        }
        return ret;
    }

    bool textMentionsIdentifier(const std::string& text, const std::string& ident) const
    {
        if (ident.empty()) {
            return false;
        }
        for (size_t pos = text.find(ident); pos != std::string::npos; pos = text.find(ident, pos + ident.size())) {
            bool leftOk = pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
            auto end = pos + ident.size();
            bool rightOk = end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
            if (leftOk && rightOk) {
                return true;
            }
        }
        return false;
    }

    bool textMentionsRuntimeIndex(const std::string& text) const
    {
        for (auto& name : loopVars) {
            if (textMentionsIdentifier(text, name)) {
                return true;
            }
        }
        static const char* commonLoopNames[] = {
            "i", "j", "k", "m", "n",
            "x_gen", "y_gen", "z_gen", "w_gen",
            "gen_i", "gen_j", "gen_k", "gen_m", "gen_n"
        };
        for (auto name : commonLoopNames) {
            if (textMentionsIdentifier(text, name)) {
                return true;
            }
        }
        return false;
    }

    std::string selectWidth(const ElementSelectSyntax& select)
    {
        auto hasLoopVar = [&](const std::string& w) {
            return textMentionsRuntimeIndex(w);
        };
        if (!select.selector) {
            return "";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            return "1";
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            auto rangeOp = tok(r.range);
            if (rangeOp == "+:" || rangeOp == "+" || rangeOp == "-:" || rangeOp == "-") {
                return foldWidth(emitNumericExpr(*r.right));
            }
            auto parseBound = [](std::string s, uint64_t& value) {
                s = trim(exprText(std::move(s)));
                s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
                int base = 10;
                size_t start = 0;
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    base = 16;
                    start = 2;
                }
                else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
                    base = 2;
                    start = 2;
                }
                if (start >= s.size()) {
                    return false;
                }
                value = 0;
                for (size_t i = start; i < s.size(); ++i) {
                    unsigned digit = 0;
                    if (s[i] >= '0' && s[i] <= '9') {
                        digit = unsigned(s[i] - '0');
                    }
                    else if (s[i] >= 'a' && s[i] <= 'f') {
                        digit = unsigned(s[i] - 'a' + 10);
                    }
                    else if (s[i] >= 'A' && s[i] <= 'F') {
                        digit = unsigned(s[i] - 'A' + 10);
                    }
                    else {
                        return false;
                    }
                    if (digit >= unsigned(base)) {
                        return false;
                    }
                    value = value * unsigned(base) + digit;
                }
                return true;
            };
            uint64_t rawLeft = 0;
            uint64_t rawRight = 0;
            if (parseBound(r.left->toString(), rawLeft) && parseBound(r.right->toString(), rawRight)) {
                return std::to_string((rawLeft > rawRight ? rawLeft - rawRight : rawRight - rawLeft) + 1);
            }
            if (exprConstValue(*r.right, 0)) {
                auto width = minusOneRangeBaseWidth(*r.left);
                if (!width.empty()) {
                    return width;
                }
                return rangeUpperPlusOneWidth(*r.left);
            }
            if (exprConstValue(*r.left, 0)) {
                auto width = minusOneRangeBaseWidth(*r.right);
                if (!width.empty()) {
                    return width;
                }
                return rangeUpperPlusOneWidth(*r.right);
            }
            auto right = emitIndexExpr(*r.right);
            auto isConstValue = [](const std::string& text, uint64_t expected) {
                if (auto value = parseConfiguredUint(text)) {
                    return *value == expected;
                }
                return false;
            };
            if (isConstValue(right, 0)) {
                if (BinaryExpressionSyntax::isKind(r.left->kind)) {
                    auto width = minusOneRangeBaseWidth(*r.left);
                    if (!width.empty()) {
                        return width;
                    }
                }
                return rangeUpperPlusOneWidth(*r.left);
            }
            auto leftExpr = emitIndexExpr(*r.left);
            if (isConstValue(leftExpr, 0)) {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto width = minusOneRangeBaseWidth(*r.right);
                    if (!width.empty()) {
                        return width;
                    }
                }
                auto folded = foldWidth(right);
                if (isNumber(folded)) {
                    return std::to_string(std::stoul(folded) + 1);
                }
                return "(" + folded + ")+1";
            }
            auto l = foldWidth(emitIndexExpr(*r.left));
            right = foldWidth(right);
            if (isNumber(l) && isNumber(right)) {
                auto lv = std::stoul(l);
                auto rv = std::stoul(right);
                return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
            }
            auto width = "(" + l + ")-(" + right + ")+1";
            return hasLoopVar(width) ? "64" : width;
        }
        return "";
    }

    std::string selectTemplateWidth(const ElementSelectSyntax& select)
    {
        if (select.selector && RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            auto rangeOp = tok(r.range);
            if (rangeOp == "+:" || rangeOp == "+" || rangeOp == "-:" || rangeOp == "-") {
                auto w = selectWidth(select);
                if (!textMentionsRuntimeIndex(w)) {
                    return w;
                }
            }
        }
        auto raw = select.toString();
        if (textMentionsRuntimeIndex(raw)) {
            return "64";
        }
        auto w = selectWidth(select);
        if (textMentionsRuntimeIndex(w)) {
            return "64";
        }
        return w;
    }

    std::string firstAssigned(const SyntaxNode& node)
    {
        std::string found;
        findAssigned(node, found);
        return found;
    }

    void collectAssignedBases(const SyntaxNode& node, std::set<std::string>& found)
    {
        if (node.kind == SyntaxKind::ExpressionStatement) {
            auto& st = node.as<ExpressionStatementSyntax>();
            if (st.expr->kind == SyntaxKind::AssignmentExpression || st.expr->kind == SyntaxKind::NonblockingAssignmentExpression) {
                auto& b = st.expr->as<BinaryExpressionSyntax>();
                auto name = assignedBase(*b.left);
                if (!name.empty()) {
                    found.insert(name);
                }
            }
        }
        else if (node.kind == SyntaxKind::TimingControlStatement) {
            collectAssignedBases(*node.as<TimingControlStatementSyntax>().statement, found);
        }
        else if (node.kind == SyntaxKind::ConditionalStatement) {
            auto& st = node.as<ConditionalStatementSyntax>();
            collectAssignedBases(*st.statement, found);
            if (st.elseClause) {
                collectAssignedBases(*st.elseClause->clause, found);
            }
        }
        else if (node.kind == SyntaxKind::ForLoopStatement) {
            collectAssignedBases(*node.as<ForLoopStatementSyntax>().statement, found);
        }
        else if (node.kind == SyntaxKind::CaseStatement) {
            for (auto item : node.as<CaseStatementSyntax>().items) {
                if (item->kind == SyntaxKind::StandardCaseItem) {
                    collectAssignedBases(*item->as<StandardCaseItemSyntax>().clause, found);
                }
                else if (item->kind == SyntaxKind::DefaultCaseItem) {
                    collectAssignedBases(*item->as<DefaultCaseItemSyntax>().clause, found);
                }
            }
        }
        else if (node.kind == SyntaxKind::SequentialBlockStatement || node.kind == SyntaxKind::ParallelBlockStatement) {
            for (auto item : node.as<BlockStatementSyntax>().items) {
                collectAssignedBases(*item, found);
            }
        }
    }

    void findAssigned(const SyntaxNode& node, std::string& found)
    {
        if (node.kind == SyntaxKind::ExpressionStatement) {
            auto& st = node.as<ExpressionStatementSyntax>();
            if (st.expr->kind == SyntaxKind::AssignmentExpression || st.expr->kind == SyntaxKind::NonblockingAssignmentExpression) {
                auto& b = st.expr->as<BinaryExpressionSyntax>();
                auto name = assignedBase(*b.left);
                if (mod->types.count(name)) {
                    found = name;
                }
            }
        }
        else if (node.kind == SyntaxKind::TimingControlStatement) {
            findAssigned(*node.as<TimingControlStatementSyntax>().statement, found);
        }
        else if (node.kind == SyntaxKind::ConditionalStatement) {
            auto& st = node.as<ConditionalStatementSyntax>();
            findAssigned(*st.statement, found);
            if (st.elseClause) {
                findAssigned(*st.elseClause->clause, found);
            }
        }
        else if (node.kind == SyntaxKind::ForLoopStatement) {
            findAssigned(*node.as<ForLoopStatementSyntax>().statement, found);
        }
        else if (node.kind == SyntaxKind::CaseStatement) {
            for (auto item : node.as<CaseStatementSyntax>().items) {
                if (item->kind == SyntaxKind::StandardCaseItem) {
                    findAssigned(*item->as<StandardCaseItemSyntax>().clause, found);
                }
                else if (item->kind == SyntaxKind::DefaultCaseItem) {
                    findAssigned(*item->as<DefaultCaseItemSyntax>().clause, found);
                }
            }
        }
        else if (node.kind == SyntaxKind::SequentialBlockStatement || node.kind == SyntaxKind::ParallelBlockStatement) {
            for (auto item : node.as<BlockStatementSyntax>().items) {
                findAssigned(*item, found);
            }
        }
    }

    std::string assignedBase(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            return tok(expr.as<IdentifierNameSyntax>().identifier);
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            return tok(expr.as<IdentifierSelectNameSyntax>().identifier);
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            return assignedBase(*expr.as<ElementSelectExpressionSyntax>().left);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            return assignedBase(*expr.as<MemberAccessExpressionSyntax>().left);
        }
        return "";
    }

    std::string arrayElementTypeText(const std::string& type)
    {
        auto text = trim(type);
        auto args = templateArgsFor(text, text.rfind("std::array<", 0) == 0 ? "std::array" : "array");
        if (!args.empty() && (text.rfind("array<", 0) == 0 || text.rfind("std::array<", 0) == 0)) {
            return trim(args[0]);
        }
        return text;
    }

	    std::string typeWidth(const std::string& type)
	    {
	        auto between = [&](const std::string& prefix) -> std::string {
	            if (type.rfind(prefix, 0) != 0 || type.back() != '>') {
	                return "";
            }
            return type.substr(prefix.size(), type.size() - prefix.size() - 1);
        };
        auto w = between("logic<");
        if (!w.empty()) {
            return foldWidth(w);
        }
        w = between("u<");
        if (!w.empty()) {
            return foldWidth(w);
        }
        if (type == "bool" || type == "u1" || type == "reg<u1>") {
            return "1";
        }
        if (type == "u8") {
            return "8";
        }
        if (type == "u16") {
            return "16";
        }
        if (type == "u32") {
            return "32";
        }
        if (type == "unsigned" || type == "uint32_t") {
            return "32";
        }
        if (type == "u64") {
            return "64";
        }
        if (type == "uint64_t") {
            return "64";
        }
	        if (type.rfind("reg<", 0) == 0 && type.back() == '>') {
	            return typeWidth(type.substr(4, type.size() - 5));
	        }
        if ((type.rfind("array<", 0) == 0 || type.rfind("std::array<", 0) == 0) && type.back() == '>') {
            return "cpphdl::type_width<" + type + ">()";
        }
        if (mod && mod->typeParamNames.count(type)) {
            return "type_width<" + type + ">()";
        }
        if (mod) {
            auto aliasIt = mod->types.find(type);
            if (aliasIt != mod->types.end() && aliasIt->second != type) {
                auto aliasWidth = typeWidth(aliasIt->second);
                if (!aliasWidth.empty()) {
                    return aliasWidth;
                }
            }
        }
        if (mod) {
            auto it = mod->typeWidths.find(type);
            if (it != mod->typeWidths.end()) {
                return it->second;
            }
        }
        auto configuredWidths = configuredTextMap("HDLCPP_TYPE_WIDTHS");
        auto configuredWidthFor = [&](const std::string& name) -> std::string {
            if (name.empty()) {
                return "";
            }
            auto directIt = configuredWidths.find(name);
            if (directIt != configuredWidths.end()) {
                return directIt->second;
            }
            std::string found;
            bool ambiguous = false;
            const std::string dotSuffix = "." + name;
            const std::string scopeSuffix = "::" + name;
            for (const auto& item : configuredWidths) {
                if (hasSuffix(item.first, dotSuffix) || hasSuffix(item.first, scopeSuffix)) {
                    if (!found.empty() && found != item.second) {
                        ambiguous = true;
                        break;
                    }
                    found = item.second;
                }
            }
            return ambiguous ? std::string() : found;
        };
        if (!configuredWidths.empty()) {
            if (mod) {
                for (const auto& import : mod->imports) {
                    auto qualifiedIt = configuredWidths.find(import + "." + type);
                    if (qualifiedIt != configuredWidths.end()) {
                        return qualifiedIt->second;
                    }
                }
                auto moduleIt = configuredWidths.find(mod->name + "." + type);
                if (moduleIt != configuredWidths.end()) {
                    return moduleIt->second;
                }
            }
            auto directIt = configuredWidths.find(type);
            if (directIt != configuredWidths.end()) {
                return directIt->second;
            }
            if (auto unique = configuredWidthFor(type); !unique.empty()) {
                return unique;
            }
        }
        auto scopePos = type.rfind("::");
        if (scopePos != std::string::npos) {
            auto scope = type.substr(0, scopePos);
            auto localName = type.substr(scopePos + 2);
            auto qualifiedIt = configuredWidths.find(scope + "." + localName);
            if (qualifiedIt != configuredWidths.end()) {
                return qualifiedIt->second;
            }
            if (auto* package = findModule(scope)) {
                auto it = package->typeWidths.find(localName);
                if (it != package->typeWidths.end()) {
                    return it->second;
                }
            }
        }
        else {
            for (auto& candidate : modules) {
                auto it = candidate.typeWidths.find(type);
                if (it != candidate.typeWidths.end()) {
                    return it->second;
                }
            }
        }
	        return "";
	    }

    std::string replaceSystemBitsOfKnownTypes(std::string s, const std::string& fallback)
    {
        if (mod) {
            for (auto& item : mod->types) {
                auto w = typeWidth(item.second);
                std::string replacement;
                if (!w.empty()) {
                    replacement = w;
                }
                else if (fallback == "sizeof") {
                    replacement = "(sizeof(" + item.second + ")*8)";
                }
                else {
                    replacement = "cpphdl::type_width<" + item.second + ">()";
                }
                replaceAll(s, "$bits(" + item.first + ")", replacement);
            }
        }
        for (size_t pos = 0; (pos = s.find("$bits(", pos)) != std::string::npos;) {
            auto open = pos + 5;
            auto close = matchingParenClose(s, open);
            if (close == std::string::npos) {
                break;
            }
            auto name = trim(s.substr(open + 1, close - open - 1));
            bool simpleTypeName = !name.empty();
            for (char c : name) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '.')) {
                    simpleTypeName = false;
                    break;
                }
            }
            if (!simpleTypeName) {
                pos = close + 1;
                continue;
            }
            auto w = typeWidth(name);
            if (w.empty()) {
                pos = close + 1;
                continue;
            }
            s.replace(pos, close - pos + 1, w);
            pos += w.size();
        }
        return s;
    }

    std::string packedFieldWidth(const std::string& type)
    {
        auto w = typeWidth(type);
        if (!w.empty()) {
            return w;
        }
        auto trimmed = trim(type);
        if (trimmed.empty() || trimmed == "void") {
            return "";
        }
        return "cpphdl::type_width<" + trimmed + ">()";
    }

    bool packedFieldUsesPack(const std::string& type)
    {
        (void)type;
        return false;
    }

    std::string resolvedTypeWidth(const std::string& type)
    {
        auto w = typeWidth(type);
        if (!w.empty()) {
            return w;
        }
        if (type.rfind("reg<", 0) == 0 && type.back() == '>') {
            return resolvedTypeWidth(type.substr(4, type.size() - 5));
        }
        if (mod) {
            auto it = mod->types.find(type);
            if (it != mod->types.end() && it->second != type) {
                return resolvedTypeWidth(it->second);
            }
        }
        return "";
    }


    std::string expressionStorageType(const ModuleGen& m, std::string expr)
    {
        expr = trim(expr);
        if (expr.empty()) {
            return "";
        }
        auto wrapsWholeExpr = [](const std::string& text) {
            if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
                return false;
            }
            int depth = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '(') {
                    ++depth;
                }
                else if (text[i] == ')') {
                    --depth;
                    if (depth == 0 && i + 1 != text.size()) {
                        return false;
                    }
                }
            }
            return depth == 0;
        };
        while (wrapsWholeExpr(expr)) {
            expr = trim(expr.substr(1, expr.size() - 2));
        }
        auto templateCallType = [&](const std::string& prefix, const std::string& resultPrefix) -> std::string {
            if (expr.rfind(prefix, 0) != 0) {
                return {};
            }
            auto open = prefix.size() - 1;
            auto close = matchingTemplateClose(expr, open);
            if (close == std::string::npos) {
                return {};
            }
            auto next = close + 1;
            while (next < expr.size() && std::isspace(static_cast<unsigned char>(expr[next]))) {
                ++next;
            }
            if (next >= expr.size() || expr[next] != '(') {
                return {};
            }
            auto argClose = matchingParenClose(expr, next);
            if (argClose == std::string::npos) {
                return {};
            }
            auto tail = trim(expr.substr(argClose + 1));
            if (!tail.empty()) {
                return {};
            }
            return resultPrefix + expr.substr(prefix.size(), close - prefix.size()) + ">";
        };
        if (auto type = templateCallType("logic<", "logic<"); !type.empty()) {
            return type;
        }
        if (auto type = templateCallType("u<", "u<"); !type.empty()) {
            return type;
        }
        if (auto type = templateCallType("cpphdl::sv_bits<", "logic<"); !type.empty()) {
            return type;
        }
        if (auto type = templateCallType("sv_bits<", "logic<"); !type.empty()) {
            return type;
        }
        if (auto type = templateCallType("cpphdl::pack_value<", "logic<"); !type.empty()) {
            return type;
        }
        if (auto type = templateCallType("pack_value<", "logic<"); !type.empty()) {
            return type;
        }
        auto lastTopLevelComma = [](const std::string& text) {
            int paren = 0;
            int angle = 0;
            int brace = 0;
            int bracket = 0;
            size_t last = std::string::npos;
            for (size_t i = 0; i < text.size(); ++i) {
                char c = text[i];
                if (c == '(') {
                    ++paren;
                }
                else if (c == ')' && paren > 0) {
                    --paren;
                }
                else if (c == '<' && i + 1 < text.size() && text[i + 1] == '<') {
                    ++i;
                }
                else if (c == '<') {
                    ++angle;
                }
                else if (c == '>' && angle > 0) {
                    --angle;
                    if (i + 1 < text.size() && text[i + 1] == '>' && angle > 0) {
                        ++i;
                        --angle;
                    }
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
                else if (c == ',' && paren == 0 && angle == 0 && brace == 0 && bracket == 0) {
                    last = i;
                }
            }
            return last;
        };
        if (auto comma = lastTopLevelComma(expr); comma != std::string::npos) {
            return expressionStorageType(m, expr.substr(comma + 1));
        }
        auto topLevelConditional = [](const std::string& text) -> std::pair<size_t, size_t> {
            int paren = 0;
            int angle = 0;
            int brace = 0;
            int bracket = 0;
            int nestedConditional = 0;
            size_t question = std::string::npos;
            for (size_t i = 0; i < text.size(); ++i) {
                char c = text[i];
                if (c == '(') {
                    ++paren;
                }
                else if (c == ')' && paren > 0) {
                    --paren;
                }
                else if (c == '<' && i + 1 < text.size() && text[i + 1] == '<') {
                    ++i;
                }
                else if (c == '<') {
                    ++angle;
                }
                else if (c == '>' && angle > 0) {
                    --angle;
                    if (i + 1 < text.size() && text[i + 1] == '>' && angle > 0) {
                        ++i;
                        --angle;
                    }
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
                if (paren != 0 || angle != 0 || brace != 0 || bracket != 0) {
                    continue;
                }
                if (c == '?') {
                    if (question == std::string::npos) {
                        question = i;
                    }
                    else {
                        ++nestedConditional;
                    }
                }
                else if (c == ':' && question != std::string::npos) {
                    if (nestedConditional == 0) {
                        return {question, i};
                    }
                    --nestedConditional;
                }
            }
            return {std::string::npos, std::string::npos};
        };
        auto conditionalType = [&](const std::string& leftType, const std::string& rightType) {
            if (leftType == rightType) {
                return leftType;
            }
            if (leftType.empty()) {
                return rightType;
            }
            if (rightType.empty()) {
                return leftType;
            }
            auto leftLogic = leftType.rfind("logic<", 0) == 0;
            auto rightLogic = rightType.rfind("logic<", 0) == 0;
            if (leftLogic && (rightType == "bool" || rightType == "u1")) {
                return leftType;
            }
            if (rightLogic && (leftType == "bool" || leftType == "u1")) {
                return rightType;
            }
            if (leftLogic && rightLogic) {
                auto leftWidth = logicWidth(leftType);
                auto rightWidth = logicWidth(rightType);
                if (isNumber(leftWidth) && isNumber(rightWidth)) {
                    return std::string("logic<") +
                           std::to_string(std::max(std::stoull(leftWidth), std::stoull(rightWidth))) + ">";
                }
                return leftType;
            }
            return std::string();
        };
        if (auto cond = topLevelConditional(expr); cond.first != std::string::npos) {
            auto leftType = expressionStorageType(m, expr.substr(cond.first + 1, cond.second - cond.first - 1));
            auto rightType = expressionStorageType(m, expr.substr(cond.second + 1));
            auto type = conditionalType(leftType, rightType);
            if (!type.empty()) {
                return type;
            }
        }
        auto hasTopLevelToken = [](const std::string& text, const std::string& token) {
            int paren = 0;
            int angle = 0;
            int brace = 0;
            int bracket = 0;
            for (size_t i = 0; i < text.size(); ++i) {
                char c = text[i];
                if (c == '(') {
                    ++paren;
                }
                else if (c == ')' && paren > 0) {
                    --paren;
                }
                else if (c == '<' && i + 1 < text.size() && text[i + 1] == '<') {
                    ++i;
                }
                else if (c == '<') {
                    ++angle;
                }
                else if (c == '>' && angle > 0) {
                    --angle;
                    if (i + 1 < text.size() && text[i + 1] == '>' && angle > 0) {
                        ++i;
                        --angle;
                    }
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
                if (paren == 0 && angle == 0 && brace == 0 && bracket == 0 &&
                    text.compare(i, token.size(), token) == 0) {
                    return true;
                }
            }
            return false;
        };
        if (hasTopLevelToken(expr, "||") || hasTopLevelToken(expr, "&&")) {
            return "logic<1>";
        }
        auto resolveAliasType = [&](std::string type) {
            type = trim(std::move(type));
            while (type.rfind("reg<", 0) == 0 && type.back() == '>') {
                type = trim(type.substr(4, type.size() - 5));
            }
            for (size_t guard = 0; guard < 32; ++guard) {
                auto it = m.types.find(type);
                if (it == m.types.end() || it->second == type) {
                    break;
                }
                type = trim(unwrapRegType(it->second));
            }
            return type;
        };
        auto indexedElementType = [&](std::string type) {
            type = resolveAliasType(std::move(type));
            auto args = templateArgsFor(type, "array");
            if (args.size() >= 2) {
                return resolveAliasType(args[0]);
            }
            args = templateArgsFor(type, "std::array");
            if (args.size() >= 2) {
                return resolveAliasType(args[0]);
            }
            return std::string();
        };
        auto resolveSelectors = [&](std::string type, size_t pos) {
            bool hadSelector = false;
            bool resolvedSelectors = true;
            type = resolveAliasType(std::move(type));
            while (!type.empty() && pos < expr.size()) {
                while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
                    ++pos;
                }
                if (pos >= expr.size()) {
                    break;
                }
                if (expr[pos] == '[') {
                    hadSelector = true;
                    int depth = 1;
                    ++pos;
                    while (pos < expr.size() && depth != 0) {
                        if (expr[pos] == '[') {
                            ++depth;
                        }
                        else if (expr[pos] == ']') {
                            --depth;
                        }
                        ++pos;
                    }
                    auto elemType = indexedElementType(type);
                    if (!elemType.empty()) {
                        type = elemType;
                    }
                    else {
                        resolvedSelectors = false;
                        break;
                    }
                    continue;
                }
                if (expr[pos] != '.') {
                    resolvedSelectors = false;
                    break;
                }
                hadSelector = true;
                ++pos;
                while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
                    ++pos;
                }
                auto start = pos;
                if (pos < expr.size() && (std::isalpha(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                    ++pos;
                    while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                        ++pos;
                    }
                }
                if (start == pos) {
                    resolvedSelectors = false;
                    break;
                }
                auto field = expr.substr(start, pos - start);
                auto nextType = fieldTypeFor(resolveAliasType(type), field);
                if (nextType.empty()) {
                    resolvedSelectors = false;
                    break;
                }
                type = resolveAliasType(nextType);
            }
            if (hadSelector && !resolvedSelectors) {
                return std::string();
            }
            return type;
        };
        if (!expr.empty() && expr.front() == '(') {
            int depth = 0;
            size_t close = std::string::npos;
            for (size_t i = 0; i < expr.size(); ++i) {
                if (expr[i] == '(') {
                    ++depth;
                }
                else if (expr[i] == ')' && depth > 0) {
                    --depth;
                    if (depth == 0) {
                        close = i;
                        break;
                    }
                }
            }
            if (close != std::string::npos && close + 1 < expr.size() &&
                (expr[close + 1] == '.' || expr[close + 1] == '[')) {
                auto parentType = expressionStorageType(m, expr.substr(1, close - 1));
                auto selectedType = resolveSelectors(parentType, close + 1);
                if (!selectedType.empty()) {
                    return selectedType;
                }
            }
        }
        if (!expr.empty() && (std::isalpha(static_cast<unsigned char>(expr.front())) || expr.front() == '_')) {
            size_t pos = 1;
            while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            auto base = expr.substr(0, pos);
            auto typeIt = m.combReturnTypes.find(base);
            if (typeIt != m.combReturnTypes.end()) {
                auto type = resolveSelectors(typeIt->second, pos);
                if (!type.empty()) {
                    return type;
                }
            }
        }
        if (auto funcPos = expr.find("_func()["); funcPos != std::string::npos) {
            auto callEnd = funcPos + std::string("_func()").size();
            if (callEnd < expr.size() && expr[callEnd] == '[') {
                auto returnName = expr.substr(0, funcPos);
                auto typeIt = m.combReturnTypes.find(returnName);
                if (typeIt != m.combReturnTypes.end()) {
                    auto type = resolveSelectors(typeIt->second, callEnd);
                    if (!type.empty()) {
                        return type;
                    }
                }
            }
        }
        if (hasSuffix(expr, "_func()")) {
            auto returnName = expr.substr(0, expr.size() - 7);
            auto typeIt = m.combReturnTypes.find(returnName);
            if (typeIt != m.combReturnTypes.end()) {
                return typeIt->second;
            }
        }
        if (auto funcPos = expr.find("_func()."); funcPos != std::string::npos) {
            auto returnName = expr.substr(0, funcPos);
            auto typeIt = m.combReturnTypes.find(returnName);
            if (typeIt != m.combReturnTypes.end()) {
                auto type = typeIt->second;
                auto pos = funcPos + std::string("_func().").size();
                while (!type.empty() && pos < expr.size()) {
                    auto start = pos;
                    if (pos < expr.size() && (std::isalpha(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                        ++pos;
                        while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                            ++pos;
                        }
                    }
                    if (start == pos) {
                        return "";
                    }
                    auto field = expr.substr(start, pos - start);
                    auto nextType = fieldTypeFor(type, field);
                    if (nextType.empty()) {
                        return "";
                    }
                    type = nextType;
                    if (pos == expr.size()) {
                        break;
                    }
                    if (expr[pos] != '.') {
                        return "";
                    }
                    ++pos;
                }
                return type;
            }
        }
        auto resolveBaseType = [&](const std::string& base) -> std::string {
            if (auto typeIt = m.types.find(base); typeIt != m.types.end()) {
                return typeIt->second;
            }
            if (auto typeIt = m.combReturnTypes.find(base); typeIt != m.combReturnTypes.end()) {
                return typeIt->second;
            }
            if (hasSuffix(base, "_comb")) {
                auto svBase = base.substr(0, base.size() - 5);
                if (auto typeIt = m.types.find(svBase); typeIt != m.types.end()) {
                    return typeIt->second;
                }
                if (auto typeIt = m.combReturnTypes.find(svBase); typeIt != m.combReturnTypes.end()) {
                    return typeIt->second;
                }
                if (auto combIt = m.combMethodByBase.find(svBase); combIt != m.combMethodByBase.end() && combIt->second < m.methods.size()) {
                    auto returnName = m.methods[combIt->second].returnName;
                    auto typeIt = m.combReturnTypes.find(returnName);
                    if (typeIt != m.combReturnTypes.end()) {
                        return typeIt->second;
                    }
                }
            }
            if (auto combIt = m.combMethodByBase.find(base); combIt != m.combMethodByBase.end() && combIt->second < m.methods.size()) {
                auto returnName = m.methods[combIt->second].returnName;
                auto typeIt = m.combReturnTypes.find(returnName);
                if (typeIt != m.combReturnTypes.end()) {
                    return typeIt->second;
                }
            }
            return "";
        };
        auto base = baseFromLValueText(expr);
        if (!base.empty()) {
            auto type = resolveBaseType(base);
            auto baseType = type;
            auto selectedType = resolveSelectors(type, base.size());
            if (selectedType.empty() && base.size() != expr.size()) {
                return "";
            }
            return selectedType.empty() ? baseType : selectedType;
        }
        return "";
    }

    std::string adaptInputPortRhs(const ModuleGen& m, const std::string& portType, const std::string& rhs,
                                  const std::string& sourceTypeHint = "")
    {
        auto target = trim(portType);
        if (target.empty()) {
            return rhs;
        }
        auto resolveAliasType = [&](std::string type) {
            type = unwrapRegType(trim(std::move(type)));
            for (size_t guard = 0; guard < 32; ++guard) {
                auto it = m.types.find(type);
                if (it == m.types.end() || it->second == type) {
                    break;
                }
                type = unwrapRegType(trim(it->second));
            }
            return type;
        };
        auto resolvedTarget = resolveAliasType(target);
        if ((resolvedTarget.rfind("logic<", 0) != 0 && resolvedTarget.rfind("u<", 0) != 0) ||
            resolvedTarget.back() != '>') {
            return rhs;
        }
        auto sourceType = !trim(sourceTypeHint).empty() ? trim(sourceTypeHint) : expressionStorageType(m, rhs);
        auto resolvedSource = resolveAliasType(sourceType);
        auto sourceIsArray = resolvedSource.rfind("array<", 0) == 0 || resolvedSource.rfind("std::array<", 0) == 0;
        auto sourceIsPackedAggregate = !resolvedSource.empty() &&
            resolvedSource.rfind("logic<", 0) != 0 &&
            resolvedSource.rfind("u<", 0) != 0 &&
            resolvedSource != "bool" &&
            resolvedSource != "unsigned" &&
            resolvedSource != "uint64_t" &&
            resolvedSource != "uint32_t" &&
            resolvedSource != "uint16_t" &&
            resolvedSource != "uint8_t" &&
            !sourceIsArray;
        if (sourceType.rfind("array<", 0) == 0 || sourceType.rfind("std::array<", 0) == 0) {
            return target + "(" + rhs + ")";
        }
        if (sourceIsArray) {
            return target + "(" + rhs + ")";
        }
        if (sourceIsPackedAggregate) {
            return target + "(" + rhs + ")";
        }
        return rhs;
    }

    std::string adaptPackedVectorRhsFromArray(const ModuleGen& m, const std::string& targetType, const std::string& rhs)
    {
        auto resolveAliasType = [&](std::string type) {
            type = unwrapRegType(trim(std::move(type)));
            for (size_t guard = 0; guard < 32; ++guard) {
                auto it = m.types.find(type);
                if (it == m.types.end() || it->second == type) {
                    break;
                }
                type = unwrapRegType(trim(it->second));
            }
            return type;
        };
        auto isArrayLike = [&](const std::string& type) {
            auto resolved = resolveAliasType(type);
            return resolved.rfind("array<", 0) == 0 || resolved.rfind("std::array<", 0) == 0;
        };
        auto resolvedTarget = resolveAliasType(targetType);
        if (resolvedTarget.empty() || isArrayLike(resolvedTarget) ||
            (resolvedTarget.rfind("logic<", 0) != 0 && resolvedTarget.rfind("u<", 0) != 0)) {
            return rhs;
        }
        auto sourceType = expressionStorageType(m, rhs);
        if (!isArrayLike(sourceType)) {
            return rhs;
        }
        if (rhs.find("pack_value<") != std::string::npos || rhs.find("cpphdl::pack_value<") != std::string::npos) {
            return rhs;
        }
        return "cpphdl::pack_value<cpphdl::type_width<" + trim(targetType) + ">()>(" + rhs + ")";
    }


    std::string finalAdaptStructuralAssignLine(ModuleGen& m, std::string line)
    {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return line;
        }
        auto lhs = trim(line.substr(0, eq));
        auto dot = lhs.rfind('.');
        if (dot == std::string::npos) {
            return line;
        }
        auto instance = trim(lhs.substr(0, dot));
        auto portName = trim(lhs.substr(dot + 1));
        auto bracket = instance.find('[');
        if (bracket != std::string::npos) {
            instance = trim(instance.substr(0, bracket));
        }
        auto paren = portName.find('(');
        if (paren != std::string::npos) {
            portName = trim(portName.substr(0, paren));
        }
        std::string childType;
        std::string memberDecl;
        for (size_t i = 0; i < m.memberNames.size() && i < m.memberTypes.size(); ++i) {
            if (m.memberNames[i] == instance) {
                childType = m.memberTypes[i];
                break;
            }
        }
        for (auto& member : m.members) {
            auto t = trim(member);
            if (hasSuffix(t, " " + instance + ";")) {
                memberDecl = t;
                break;
            }
        }
        if (!memberDecl.empty()) {
            auto decl = memberDecl;
            if (!decl.empty() && decl.back() == ';') {
                decl.pop_back();
            }
            auto instPos = decl.rfind(instance);
            if (instPos != std::string::npos) {
                auto before = trim(decl.substr(0, instPos));
                if (!before.empty() && (childType.empty() || before.find('<') != std::string::npos)) {
                    childType = before;
                }
            }
        }
        auto childElementType = arrayElementTypeText(childType);
        auto baseChildType = trim(childElementType);
        std::string childParams;
        if (auto lt = baseChildType.find('<'); lt != std::string::npos) {
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
            baseChildType = trim(baseChildType.substr(0, lt));
        }
        auto memberTemplateArgs = [&]() -> std::vector<std::string> {
            if (!childParams.empty()) {
                return splitTopLevelArgs(childParams);
            }
            auto lt = memberDecl.find('<');
            if (lt == std::string::npos) {
                return {};
            }
            int angleDepth = 0;
            size_t gt = std::string::npos;
            for (size_t pos = lt; pos < memberDecl.size(); ++pos) {
                if (memberDecl[pos] == '<') {
                    ++angleDepth;
                }
                else if (memberDecl[pos] == '>' && --angleDepth == 0) {
                    gt = pos;
                    break;
                }
            }
            if (gt == std::string::npos) {
                return {};
            }
            return splitTopLevelArgs(memberDecl.substr(lt + 1, gt - lt - 1));
        };
        auto substitutePortParams = [&](std::string typeText) {
            if (typeText.empty() || childType.empty()) {
                return typeText;
            }
            auto* child = findModule(baseChildType);
            auto configuredParams = configuredModuleParams(baseChildType);
            auto& declaredParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
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
            if (child) {
                std::set<std::string> declaredParamNames;
                for (const auto& declared : declaredParams) {
                    auto name = templateParamName(declared);
                    if (!name.empty()) {
                        declaredParamNames.insert(name);
                    }
                }
                for (const auto& typeItem : child->types) {
                    if (declaredParamNames.count(typeItem.first)) {
                        continue;
                    }
                    if (!typeItem.first.empty() && !typeItem.second.empty() && typeItem.first != typeItem.second) {
                        replaceIdentifierAll(typeText, typeItem.first, typeItem.second);
                    }
                }
                auto localAliases = localUsingTypeAliases(*child);
                for (const auto& aliasItem : localAliases) {
                    if (!aliasItem.first.empty() && !aliasItem.second.empty()) {
                        replaceIdentifierAll(typeText, aliasItem.first, aliasItem.second);
                    }
                }
            }
            return substituteParamDeclValues(declaredParams, memberTemplateArgs(), std::move(typeText));
        };
        auto svPortName = portName;
        if (hasSuffix(svPortName, "_in")) {
            svPortName.resize(svPortName.size() - 3);
        }
        else if (hasSuffix(svPortName, "_out")) {
            svPortName.resize(svPortName.size() - 4);
        }
        std::string portType;
        bool inputPort = false;
        if (!childType.empty()) {
            auto* child = findModule(baseChildType);
            if (child) {
                auto portNameMatches = [&](const std::string& candidate) {
                    return candidate == portName ||
                           candidate == svPortName ||
                           candidate == svPortName + "_in" ||
                           candidate == svPortName + "_out";
                };
                for (auto& p : child->ports) {
                    if (portNameMatches(p.name)) {
                        portType = p.type;
                        inputPort = p.direction != "output";
                        break;
                    }
                }
                if (inputPort && !portType.empty()) {
                    portType = substitutePortParams(portType);
                }
            }
        }
        if (auto portTypes = configuredTextMap("HDLCPP_PORT_TYPES");
            portTypes.count(m.name + "." + instance + "." + svPortName) ||
            portTypes.count(m.name + "." + instance + "." + portName) ||
            portTypes.count(instance + "." + svPortName) ||
            portTypes.count(instance + "." + portName) ||
            (!childType.empty() && (portTypes.count(childType + "." + svPortName) || portTypes.count(childType + "." + portName) ||
                                    portTypes.count(baseChildType + "." + svPortName) || portTypes.count(baseChildType + "." + portName)))) {
            auto specIt = portTypes.find(m.name + "." + instance + "." + svPortName);
            if (specIt == portTypes.end()) {
                specIt = portTypes.find(m.name + "." + instance + "." + portName);
            }
            if (specIt == portTypes.end()) {
                specIt = portTypes.find(instance + "." + svPortName);
            }
            if (specIt == portTypes.end()) {
                specIt = portTypes.find(instance + "." + portName);
            }
            if (!childType.empty()) {
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(childType + "." + svPortName);
                }
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(childType + "." + portName);
                }
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(baseChildType + "." + svPortName);
                }
                if (specIt == portTypes.end()) {
                    specIt = portTypes.find(baseChildType + "." + portName);
                }
            }
            auto spec = specIt->second;
            auto sep = spec.find(':');
            auto direction = trim(sep == std::string::npos ? std::string() : spec.substr(0, sep));
            auto configuredType = trim(sep == std::string::npos ? spec : spec.substr(sep + 1));
            if (direction == "input") {
                inputPort = true;
            }
            else if (direction == "output") {
                inputPort = false;
            }
            if (!configuredType.empty()) {
                portType = substitutePortParams(configuredType);
            }
        }
        const std::pair<const char*, const char*> wrapperPairs[] = {
            {"_ASSIGN_COMB_IJ(", "_ASSIGN_IJ("},
            {"_ASSIGN_COMB_I(", "_ASSIGN_I("},
            {"_ASSIGN_COMB_J(", "_ASSIGN_J("},
            {"_ASSIGN_COMB(", "_ASSIGN("},
        };
        std::string combWrapper;
        std::string valueWrapper;
        auto wrap = std::string::npos;
        for (const auto& [comb, value] : wrapperPairs) {
            wrap = line.find(comb, eq);
            if (wrap != std::string::npos) {
                combWrapper = comb;
                valueWrapper = value;
                break;
            }
        }
        size_t argStart = std::string::npos;
        if (wrap != std::string::npos) {
            argStart = wrap + combWrapper.size();
        }
        else {
            for (const auto& [comb, value] : wrapperPairs) {
                wrap = line.find(value, eq);
                if (wrap != std::string::npos) {
                    combWrapper = comb;
                    valueWrapper = value;
                    break;
                }
            }
            if (wrap == std::string::npos) {
                return line;
            }
            argStart = wrap + valueWrapper.size();
        }
        int depth = 1;
        size_t argEnd = std::string::npos;
        for (size_t i = argStart; i < line.size(); ++i) {
            if (line[i] == '(') {
                ++depth;
            }
            else if (line[i] == ')') {
                if (--depth == 0) {
                    argEnd = i;
                    break;
                }
            }
        }
        if (argEnd == std::string::npos) {
            return line;
        }
        auto arg = trim(line.substr(argStart, argEnd - argStart));
        if (line.find(combWrapper, eq) != std::string::npos &&
            arg.rfind("__port_bind_", 0) == 0 &&
            hasSuffix(arg, "_comb_func()")) {
            return line;
        }
        auto sourceType = expressionStorageType(m, arg);
        if (sourceType.empty()) {
            auto sourceBase = baseFromLValueText(arg);
            if (!sourceBase.empty()) {
                std::vector<std::string> sourceCandidates{sourceBase};
                auto sourceTrimmed = trim(arg);
                if (hasSuffix(sourceTrimmed, "()")) {
                    sourceCandidates.push_back(sourceTrimmed.substr(0, sourceTrimmed.size() - 2));
                }
                for (const auto& item : m.portCppNames) {
                    if (item.second == sourceBase) {
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
                            sourceType = sourcePort.type;
                            break;
                        }
                    }
                    if (!sourceType.empty()) {
                        break;
                    }
                }
            }
        }
        auto resolveLocalAliasType = [&](std::string type) {
            type = unwrapRegType(trim(std::move(type)));
            for (size_t guard = 0; guard < 32; ++guard) {
                auto it = m.types.find(type);
                if (it == m.types.end() || it->second == type) {
                    break;
                }
                type = unwrapRegType(trim(it->second));
            }
            return type;
        };
        auto arrayTypeIsPacked = [&](std::string type) {
            type = resolveLocalAliasType(std::move(type));
            auto args = templateArgsFor(type, "array");
            return args.size() >= 3 && trim(args[2]) == "true";
        };
        auto leadingGetterName = [](std::string expr) {
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
                        return std::string{};
                    }
                }
                auto rest = trim(expr.substr(callEnd));
                while (!rest.empty() && rest.front() == ')') {
                    rest = trim(rest.substr(1));
                }
                if (rest.empty() || (rest.front() != '[' && rest.front() != '.')) {
                    return std::string{};
                }
                return expr.substr(start, callEnd - start - 2);
            }
            return std::string{};
        };
        auto getterIndexesArrayElement = [](std::string expr) {
            expr = trim(std::move(expr));
            const char* getters[] = {"_in()", "_out()", "_comb_func()"};
            for (auto* getter : getters) {
                auto pos = expr.find(getter);
                if (pos == std::string::npos) {
                    continue;
                }
                auto rest = trim(expr.substr(pos + std::string(getter).size()));
                while (!rest.empty() && rest.front() == ')') {
                    rest = trim(rest.substr(1));
                }
                return !rest.empty() && rest.front() == '[';
            }
            return false;
        };
        auto getterType = [&](const std::string& getterName) {
            for (const auto& sourcePort : m.ports) {
                if (sourcePort.name == getterName) {
                    return sourcePort.type;
                }
            }
            for (const auto& method : m.methods) {
                if (method.name == getterName) {
                    auto returnName = method.returnName.empty() ? method.returnBase : method.returnName;
                    if (auto typeIt = m.combReturnTypes.find(returnName); typeIt != m.combReturnTypes.end()) {
                        return typeIt->second;
                    }
                    if (auto typeIt = m.types.find(returnName); typeIt != m.types.end()) {
                        return typeIt->second;
                    }
                    auto ret = trim(method.ret);
                    while (!ret.empty() && (ret.back() == '&' || ret.back() == '*')) {
                        ret.pop_back();
                        ret = trim(std::move(ret));
                    }
                    if (!ret.empty() && ret != "void") {
                        return ret;
                    }
                }
            }
            if (hasSuffix(getterName, "_comb_func")) {
                auto base = getterName.substr(0, getterName.size() - std::string("_comb_func").size());
                auto storage = combStorageName(m, base);
                if (auto typeIt = m.combReturnTypes.find(storage); typeIt != m.combReturnTypes.end()) {
                    return typeIt->second;
                }
                if (auto typeIt = m.types.find(storage); typeIt != m.types.end()) {
                    return typeIt->second;
                }
                if (auto typeIt = m.types.find(base); typeIt != m.types.end()) {
                    return typeIt->second;
                }
            }
            return std::string{};
        };
        auto getterName = leadingGetterName(arg);
        if (!getterName.empty() && getterIndexesArrayElement(arg)) {
            auto recovered = getterType(getterName);
            auto recoveredTrim = resolveLocalAliasType(recovered);
            auto recoveredIsPackedElementSource =
                arrayTypeIsPacked(recoveredTrim) ||
                recoveredTrim.rfind("logic<", 0) == 0 ||
                recoveredTrim.rfind("u<", 0) == 0;
            if (!recovered.empty() && recoveredIsPackedElementSource) {
                if (recoveredTrim.rfind("array<", 0) == 0 || recoveredTrim.rfind("std::array<", 0) == 0) {
                    sourceType = arrayElementTypeText(recoveredTrim);
                }
                else {
                    sourceType = recovered;
                }
            }
        }
        auto outerLogicCastType = [](const std::string& expr) {
            auto s = trim(expr);
            while (s.rfind("bool(", 0) == 0) {
                auto close = matchingParenClose(s, 4);
                if (close == std::string::npos || close + 1 != s.size()) {
                    break;
                }
                s = trim(s.substr(5, s.size() - 6));
            }
            if (s.rfind("logic<", 0) != 0) {
                return std::string();
            }
            auto closeTemplate = matchingTemplateClose(s, 5);
            if (closeTemplate == std::string::npos) {
                return std::string();
            }
            auto next = closeTemplate + 1;
            while (next < s.size() && std::isspace(static_cast<unsigned char>(s[next]))) {
                ++next;
            }
            if (next < s.size() && s[next] == '(') {
                return s.substr(0, closeTemplate + 1);
            }
            return std::string();
        };
        auto boolCastInnerType = [&]() -> std::string {
            auto s = trim(arg);
            bool unwrapped = false;
            while (s.rfind("bool(", 0) == 0 && s.back() == ')') {
                int depth = 0;
                bool wrapsWholeExpr = true;
                for (size_t pos = 4; pos < s.size(); ++pos) {
                    if (s[pos] == '(') {
                        ++depth;
                    }
                    else if (s[pos] == ')') {
                        --depth;
                        if (depth == 0 && pos + 1 != s.size()) {
                            wrapsWholeExpr = false;
                            break;
                        }
                    }
                }
                if (!wrapsWholeExpr) {
                    break;
                }
                s = trim(s.substr(5, s.size() - 6));
                unwrapped = true;
            }
            return unwrapped ? expressionStorageType(m, s) : std::string();
        }();
        auto outerAggregateCastType = [](const std::string& expr) {
            auto s = trim(expr);
            size_t open = std::string::npos;
            if (s.rfind("array<", 0) == 0) {
                open = 5;
            }
            else if (s.rfind("std::array<", 0) == 0) {
                open = 10;
            }
            else {
                return std::string();
            }
            auto closeTemplate = matchingTemplateClose(s, open);
            if (closeTemplate == std::string::npos) {
                return std::string();
            }
            auto next = closeTemplate + 1;
            while (next < s.size() && std::isspace(static_cast<unsigned char>(s[next]))) {
                ++next;
            }
            if (next < s.size() && s[next] == '(') {
                return s.substr(0, closeTemplate + 1);
            }
            return std::string();
        };
        auto outerAggregateCastInner = [](const std::string& expr) {
            auto s = trim(expr);
            size_t open = std::string::npos;
            if (s.rfind("array<", 0) == 0) {
                open = 5;
            }
            else if (s.rfind("std::array<", 0) == 0) {
                open = 10;
            }
            else {
                return std::string();
            }
            auto closeTemplate = matchingTemplateClose(s, open);
            if (closeTemplate == std::string::npos) {
                return std::string();
            }
            auto next = closeTemplate + 1;
            while (next < s.size() && std::isspace(static_cast<unsigned char>(s[next]))) {
                ++next;
            }
            if (next >= s.size() || s[next] != '(') {
                return std::string();
            }
            auto closeParen = matchingParenClose(s, next);
            if (closeParen == std::string::npos || closeParen + 1 != s.size()) {
                return std::string();
            }
            return trim(s.substr(next + 1, closeParen - next - 1));
        };
        if (!inputPort) {
            if (line.find("_ASSIGN(", eq) == std::string::npos) {
                return line;
            }
            if (isSimpleCombRef(arg) ||
                arg.find("_comb_func()") == std::string::npos) {
                return line;
            }
            inputPort = true;
            auto castType = outerLogicCastType(arg);
            if (!castType.empty()) {
                portType = castType;
            }
            else if (!boolCastInnerType.empty() && boolCastInnerType != "bool") {
                portType = boolCastInnerType;
            }
            else {
                portType = sourceType.empty() ? std::string("bool") : sourceType;
            }
        }
        if (const char* debug = std::getenv("HDLCPP_DEBUG_ASSIGN_ADAPT")) {
            std::string filter = debug;
            if (filter.empty() || filter == "1" || line.find(filter) != std::string::npos ||
                portName.find(filter) != std::string::npos || instance.find(filter) != std::string::npos) {
                std::cerr << "assign-adapt module=" << m.name
                          << " instance=" << instance
                          << " child=" << childType
                          << " port=" << portName
                          << " portType=" << portType
                          << " sourceType=" << sourceType
                          << " arg=" << arg
                          << "\n";
            }
        }
        auto setAssignComb = [&]() {
            auto assignWrap = line.find(valueWrapper, eq);
            if (assignWrap != std::string::npos && assignWrap < argStart) {
                auto oldSize = valueWrapper.size() - 1;
                auto newSize = combWrapper.size() - 1;
                line.replace(assignWrap, valueWrapper.size() - 1, combWrapper.substr(0, combWrapper.size() - 1));
                auto delta = static_cast<long long>(newSize) - static_cast<long long>(oldSize);
                argStart = static_cast<size_t>(static_cast<long long>(argStart) + delta);
                argEnd = static_cast<size_t>(static_cast<long long>(argEnd) + delta);
            }
        };
        auto setAssignValue = [&]() {
            auto combWrap = line.find(combWrapper, eq);
            if (combWrap != std::string::npos && combWrap < argStart) {
                auto oldSize = combWrapper.size() - 1;
                auto newSize = valueWrapper.size() - 1;
                line.replace(combWrap, combWrapper.size() - 1, valueWrapper.substr(0, valueWrapper.size() - 1));
                auto delta = static_cast<long long>(newSize) - static_cast<long long>(oldSize);
                argStart = static_cast<size_t>(static_cast<long long>(argStart) + delta);
                argEnd = static_cast<size_t>(static_cast<long long>(argEnd) + delta);
            }
        };
	        auto replaceArg = [&](const std::string& replacement) {
	            line.replace(argStart, argEnd - argStart, replacement);
	            argEnd = argStart + replacement.size();
	            arg = replacement;
	        };
	        auto getterAddressableArg = [&](std::string expr) {
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
	                if (!rest.empty() && rest.front() == '[') {
	                    auto getterName = expr.substr(start, callEnd - start - 2);
	                    auto source = resolveLocalAliasType(getterType(getterName));
	                    if (!(source.rfind("array<", 0) == 0 || source.rfind("std::array<", 0) == 0) ||
	                        arrayTypeIsPacked(source)) {
	                        return false;
	                    }
	                }
	                return rest.empty() || rest.front() == '.' || rest.front() == '[';
	            }
	            return false;
	        };
	        auto childArraySize = [&]() {
	            if (auto it = m.memberArraySizes.find(instance); it != m.memberArraySizes.end()) {
	                return it->second;
	            }
	            auto text = trim(childType);
	            auto args = templateArgsFor(text, text.rfind("std::array<", 0) == 0 ? "std::array" : "array");
	            if (args.size() >= 2 && (text.rfind("array<", 0) == 0 || text.rfind("std::array<", 0) == 0)) {
	                return trim(args[1]);
	            }
	            return std::string{};
	        };
	        auto materializeIndexedCombPortBinding = [&]() {
	            if (!referencesDynamicCpphdlGetter(arg) || isSimpleCombRef(arg) || getterAddressableArg(arg)) {
	                return false;
	            }
	            std::string indexName;
	            if (line.find("_ASSIGN_COMB_I(", eq) != std::string::npos) {
	                indexName = "i";
	            }
	            else if (line.find("_ASSIGN_COMB_J(", eq) != std::string::npos) {
	                indexName = "j";
	            }
	            else {
	                return false;
	            }
	            auto size = childArraySize();
	            if (size.empty()) {
	                return false;
	            }
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
	            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName + "_indexed");
	            auto unique = base;
	            unsigned suffix = 0;
	            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
	                unique = base + "_" + std::to_string(++suffix);
	            }
	            auto retType = trim(portType);
	            if (!instance.empty() && !portName.empty()) {
	                retType = "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>";
	            }
	            if (retType.empty()) {
	                retType = "bool";
	            }
	            m.types[unique] = "array<" + retType + "," + size + ">";
	            m.varNames.insert(unique);
	            m.combAssignedVars.insert(unique);

	            MethodGen method;
	            method.name = unique + "_comb_func";
	            method.ret = retType + "&";
	            method.args = "unsigned " + indexName;
	            method.returnBase = unique;
	            m.combReturnTypes[unique] = "array<" + retType + "," + size + ">";
	            auto indexedStorage = unique + "[(unsigned)(uint64_t)(" + indexName + ")]";
	            if (retType == "bool" || retType == "u1") {
	                method.body.push_back(indexedStorage + " = bool(" + arg + ");");
	            }
	            else {
	                method.body.push_back(indexedStorage + " = " + arg + ";");
	            }
	            method.body.push_back("return " + indexedStorage + ";");
	            m.methods.push_back(method);

	            replaceArg(method.name + "(" + indexName + ")");
	            setAssignComb();
	            return true;
	        };
	        (void)materializeIndexedCombPortBinding;
        auto downgradeIndexedCombRvalue = [&]() {
            const bool indexedComb =
                line.find("_ASSIGN_COMB_I(", eq) != std::string::npos ||
                line.find("_ASSIGN_COMB_J(", eq) != std::string::npos ||
                line.find("_ASSIGN_COMB_IJ(", eq) != std::string::npos;
            if (indexedComb && arg.find('[') != std::string::npos) {
                setAssignValue();
            }
        };
	        auto indexedGetterReturnsPackedElement = [&]() {
	            if (!getterIndexesArrayElement(arg)) {
	                return false;
	            }
            auto source = resolveLocalAliasType(sourceType);
            return arrayTypeIsPacked(source) ||
                source.rfind("logic<", 0) == 0 ||
                source.rfind("u<", 0) == 0;
        };
        auto containsUnresolvedChildParam = [&](const std::string& typeText) {
            if (typeText.empty() || baseChildType.empty()) {
                return false;
            }
            auto* child = findModule(baseChildType);
            auto configuredParams = configuredModuleParams(baseChildType);
            const auto& declaredParams = !configuredParams.empty() ? configuredParams : (child ? child->params : configuredParams);
            for (const auto& declared : declaredParams) {
                auto name = templateParamName(declared);
                if (!name.empty() && isIdentifierUsed(typeText, name)) {
                    return true;
                }
            }
            return false;
        };
        auto materializeCombPortBinding = [&]() {
            auto assignWrap = line.find("_ASSIGN(", eq);
            if (assignWrap == std::string::npos || assignWrap >= argStart || isSimpleCombRef(arg) ||
                arg.find("_comb_func()") == std::string::npos) {
                return false;
            }

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

            auto base = sanitizeGeneratedName("__port_bind_" + instance + "_" + portName);
            auto unique = base;
            unsigned suffix = 0;
            while (m.types.count(unique) || m.combMethodByBase.count(unique) || m.varNames.count(unique)) {
                unique = base + "_" + std::to_string(++suffix);
            }

            auto retType = trim(portType);
            if (!instance.empty() && !portName.empty()) {
                retType = "std::remove_cvref_t<decltype(" + instance + "." + portName + "())>";
            }
            auto castType = outerLogicCastType(arg);
            if ((retType.empty() || retType == "bool") && !castType.empty()) {
                retType = castType;
            }
            else if (!castType.empty() && containsUnresolvedChildParam(retType)) {
                retType = castType;
            }
            if (retType.empty()) {
                retType = "bool";
            }
            auto methodArg = arg;
            if (retType != "bool" && retType != "u1") {
                auto trimmedMethodArg = trim(methodArg);
                const std::string boolPrefix = "bool(";
                if (trimmedMethodArg.rfind(boolPrefix, 0) == 0) {
                    auto close = matchingParenClose(trimmedMethodArg, boolPrefix.size() - 1);
                    if (close == trimmedMethodArg.size() - 1) {
                        methodArg = trim(trimmedMethodArg.substr(boolPrefix.size(), close - boolPrefix.size()));
                    }
                }
            }
            if ((retType.rfind("array<", 0) == 0 || retType.rfind("std::array<", 0) == 0)) {
                auto aggregateCast = outerAggregateCastType(methodArg);
                if (!aggregateCast.empty() && aggregateCast != retType) {
                    methodArg.replace(0, aggregateCast.size(), retType);
                }
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
                method.body.push_back(unique + " = bool(" + methodArg + ");");
            }
            else {
                method.body.push_back(unique + " = " + methodArg + ";");
            }
            m.methods.push_back(method);

            replaceArg(method.name + "()");
            setAssignComb();
            return true;
        };
	        if (portType == "bool") {
	            if (isSimpleCombRef(arg) && sourceType == "bool") {
	                setAssignComb();
	                return line;
	            }
	            if (arg.rfind("bool(", 0) != 0) {
	                replaceArg("bool(" + arg + ")");
	            }
	            if (getterAddressableArg(arg)) {
	                setAssignComb();
	            }
	            else {
	                setAssignValue();
	            }
	            materializeCombPortBinding();
	            downgradeIndexedCombRvalue();
	            return line;
	        }
        bool targetAggregate = portType.rfind("array<", 0) == 0 || portType.rfind("std::array<", 0) == 0;
        if (targetAggregate) {
            auto aggregateInner = outerAggregateCastInner(arg);
            if (!aggregateInner.empty() && isSimpleCombRef(aggregateInner)) {
                replaceArg(aggregateInner);
                setAssignComb();
                return line;
            }
            auto resolvedSourceType = resolveLocalAliasType(sourceType);
            if (sourceType.empty() || sourceType == portType ||
                (!resolvedSourceType.empty() &&
                 trim(postProcessCppLine(resolvedSourceType)) == trim(postProcessCppLine(portType)))) {
                if (isSimpleCombRef(arg)) {
                    setAssignComb();
                }
	                else {
	                    materializeCombPortBinding();
	                }
	                downgradeIndexedCombRvalue();
	                return line;
	            }
	            if (arg.rfind(portType + "(", 0) != 0) {
	                replaceArg(portType + "(" + arg + ")");
	            }
	            if (getterAddressableArg(arg)) {
	                setAssignComb();
	            }
	            else {
	                setAssignValue();
	            }
	            materializeCombPortBinding();
	            downgradeIndexedCombRvalue();
	            return line;
	        }
        if (portType.rfind("logic<", 0) != 0 || portType.back() != '>') {
            if (isSimpleCombRef(arg) && (sourceType.empty() || sourceType == portType)) {
                setAssignComb();
            }
            else {
                auto resolvedSourceType = resolveLocalAliasType(sourceType);
                auto resolvedPortType = resolveLocalAliasType(portType);
                if (arg.find('[') != std::string::npos &&
                    arg.rfind("cpphdl::sv_cast<", 0) != 0 &&
                    arg.rfind(portType + "(", 0) != 0 &&
                    (sourceType.empty() || resolvedSourceType != resolvedPortType)) {
                    replaceArg("cpphdl::sv_cast<" + portType + ">(" + arg + ")");
                }
                materializeCombPortBinding();
            }
	            downgradeIndexedCombRvalue();
	            return line;
        }
        bool aggregateSource = sourceType.rfind("array<", 0) == 0 || sourceType.rfind("std::array<", 0) == 0;
        if (getterIndexesArrayElement(arg) && !aggregateSource &&
            (sourceType.empty() || sourceType == portType)) {
            setAssignComb();
            return line;
        }
	        if (indexedGetterReturnsPackedElement() ||
	            (portType.rfind("logic<", 0) == 0 && getterIndexesArrayElement(arg))) {
	            if (getterAddressableArg(arg)) {
	                setAssignComb();
	            }
	            else {
	                setAssignValue();
	            }
	        }
        if (isSimpleCombRef(arg) && !aggregateSource && (sourceType.empty() || sourceType == portType)) {
            setAssignComb();
            return line;
        }
        auto packToPortLogic = [&]() {
            auto width = logicWidth(portType);
            if (width.empty()) {
                return false;
            }
            auto s = trim(arg);
            if (s.rfind("cpphdl::pack_value<", 0) == 0 ||
                s.rfind("pack_value<", 0) == 0 ||
                s.rfind(portType + "(", 0) == 0) {
                return false;
            }
            replaceArg("cpphdl::pack_value<" + width + ">(" + arg + ")");
            return true;
        };
        if (!sourceType.empty() && sourceType != portType && sourceType.rfind("logic<", 0) != 0 &&
            sourceType.rfind("u<", 0) != 0 && sourceType != "bool") {
            packToPortLogic();
        }
	        if (!aggregateSource) {
	            materializeCombPortBinding();
	            downgradeIndexedCombRvalue();
	            return line;
	        }
	        if (arg.rfind(portType + "(", 0) == 0) {
	            materializeCombPortBinding();
	            downgradeIndexedCombRvalue();
	            return line;
	        }
	        if (!packToPortLogic()) {
	            replaceArg(portType + "(" + arg + ")");
	        }
	        if (getterAddressableArg(arg)) {
	            setAssignComb();
	        }
	        else {
	            setAssignValue();
	        }
	        materializeCombPortBinding();
	        downgradeIndexedCombRvalue();
	        return line;
    }

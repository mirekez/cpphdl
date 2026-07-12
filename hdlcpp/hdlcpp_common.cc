#include "hdlcpp.h"

struct PortGen {
    std::string name;
    std::string type;
    std::string direction;
    std::string array;
    std::string init;
    bool isInterface = false;
};

struct MethodGen {
    std::string name;
    std::string ret = "void";
    std::string args;
    std::vector<std::string> body;
    std::set<std::string> localNames;
    std::string returnName;
    std::string returnBase;
    bool localCombBody = false;
    bool staticConstexpr = false;
};

struct InstanceConnGen {
    std::string instance;
    std::string type;
    std::string port;
    std::string rhs;
    std::string lhs;
    bool connected = true;
    std::string params;
    std::vector<std::string> guards;
};

struct PendingCombGen {
    std::vector<std::string> lines;
    std::vector<std::string> variables;
    std::set<std::string> localNames;
};

struct ModuleGen {
    std::string name;
    bool isPackage = false;
    bool isInterface = false;
    std::vector<std::string> params;
    std::vector<PortGen> ports;
    std::vector<std::string> typeDecls;
    std::vector<std::string> preClassDecls;
    std::vector<std::string> packageDecls;
    std::set<std::string> valueNames;
    std::set<std::string> packageValueNames;
    std::vector<std::string> imports;
    std::vector<std::pair<std::string, std::string>> vars;
    std::vector<std::pair<std::string, std::string>> constants;
    std::vector<std::string> members;
    std::vector<std::string> memberTypes;
    std::vector<std::string> memberNames;
    std::map<std::string, std::string> memberArraySizes;
    std::vector<InstanceConnGen> instanceConns;
    std::vector<MethodGen> methods;
    std::vector<std::pair<std::string, std::string>> assigns;
    std::vector<std::string> assignLines;
    std::set<std::string> portNames;
    std::set<std::string> varNames;
    std::set<std::string> combAssignedVars;
    std::set<std::string> seqAssignedVars;
    std::set<std::string> assignDrivenVars;
    std::set<std::string> partialAssignDrivenVars;
    std::set<std::string> bridgeAssignVars;
    std::set<std::string> typeParamNames;
    std::map<std::string, std::string> assignExprByBase;
    std::map<std::string, std::string> portCppNames;
    std::map<std::string, std::string> outputPortCppNames;
    std::map<std::string, std::string> wireMap;
    std::map<std::string, std::string> combSideEffectDriver;
    std::set<std::string> combSideEffectChildInputReads;
    std::set<std::string> runtimeAssignDrivenVars;
    std::map<std::string, std::string> preferredCombDriver;
    std::map<std::string, size_t> combMethodByBase;
    std::map<std::pair<std::string, std::string>, size_t> combMethodByField;
    std::set<std::string> noCacheCombBases;
    std::map<std::string, PendingCombGen> pendingCombByBase;
    std::map<std::string, std::vector<std::string>> structuralAssignGuards;
    std::map<std::string, std::string> combReturnTypes;
    std::map<std::string, std::string> functionReturnTypes;
    std::map<std::string, std::string> outputRegTypes;
    std::map<std::string, std::string> types;
    std::map<std::string, std::string> typeWidths;
    std::map<std::string, std::map<std::string, std::string>> typeFields;
    std::map<std::string, std::vector<std::string>> typeFieldOrder;
    std::map<std::string, std::vector<std::string>> arrayLowerBounds;
    int alwaysNo = 0;
    bool hasWorkTask = false;
};

static void mergePendingComb(ModuleGen& mod, const std::string& base, const PendingCombGen& pending)
{
    auto it = mod.pendingCombByBase.find(base);
    if (it == mod.pendingCombByBase.end()) {
        mod.pendingCombByBase[base] = pending;
        return;
    }
    auto& current = it->second;
    current.lines.insert(current.lines.end(), pending.lines.begin(), pending.lines.end());
    for (const auto& variable : pending.variables) {
        if (std::find(current.variables.begin(), current.variables.end(), variable) == current.variables.end()) {
            current.variables.push_back(variable);
        }
    }
    current.localNames.insert(pending.localNames.begin(), pending.localNames.end());
}

static std::string trim(std::string s);

static bool isCombOnlyOutput(const ModuleGen& m, const std::string& svName)
{
    return m.outputPortCppNames.count(svName) &&
           m.combAssignedVars.count(svName) &&
           !m.seqAssignedVars.count(svName);
}

static bool isAssignOnlyOutput(const ModuleGen& m, const std::string& svName)
{
    return m.outputPortCppNames.count(svName) &&
           m.assignExprByBase.count(svName) &&
           !m.combAssignedVars.count(svName) &&
           !m.seqAssignedVars.count(svName);
}

static bool isDirectCombOutputMethodType(const ModuleGen& m, std::string type)
{
    type = trim(type);
    const std::string constPrefix = "const ";
    if (type.rfind(constPrefix, 0) == 0) {
        type = trim(type.substr(constPrefix.size()));
    }
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        type.pop_back();
        type = trim(type);
    }
    if (type.rfind("array<", 0) == 0 || type.rfind("std::array<", 0) == 0) {
        return true;
    }
    return m.typeParamNames.count(type) != 0;
}

static bool memoryLikeType(const std::string& type);

static std::string regTypeFor(std::string type)
{
    if (type.rfind("reg<", 0) == 0 || type.rfind("memory<", 0) == 0) {
        return type;
    }
    if (type == "bool" || type == "u1" || type == "logic<1>") {
        return "reg<logic<1>>";
    }
    return "reg<" + type + ">";
}

static bool isAssignDrivenVar(const ModuleGen& m, const std::string& name)
{
    return m.assignDrivenVars.count(name) &&
           !m.combAssignedVars.count(name) &&
           !m.seqAssignedVars.count(name) &&
           !m.partialAssignDrivenVars.count(name) &&
           !m.bridgeAssignVars.count(name) &&
           m.varNames.count(name) &&
           m.types.count(name) &&
           !memoryLikeType(m.types.at(name));
}

static std::string outputStorageType(const ModuleGen& m, const std::string& svName, const std::string& cppName)
{
    if (isAssignOnlyOutput(m, svName)) {
        return "";
    }
    if (isCombOnlyOutput(m, svName)) {
        auto it = m.types.find(svName);
        if (it != m.types.end()) {
            return it->second;
        }
    }
    auto it = m.outputRegTypes.find(cppName);
    if (it != m.outputRegTypes.end()) {
        return it->second;
    }
    return "";
}

static std::string outputStorageName(const ModuleGen& m, const std::string& svName)
{
    auto cppName = m.outputPortCppNames.at(svName);
    if (isAssignOnlyOutput(m, svName)) {
        return cppName;
    }
    if (isCombOnlyOutput(m, svName)) {
        return svName + "_comb";
    }
    if (cppName != svName) {
        return svName;
    }
    return svName + "_state";
}

static std::string combStorageName(const ModuleGen& m, const std::string& base)
{
    return base + "_comb";
}

static bool hasSuffix(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool isSimpleCombRef(std::string rhs)
{
    auto begin = rhs.find_first_not_of(" \t\r\n");
    auto end = rhs.find_last_not_of(" \t\r\n");
    rhs = begin == std::string::npos ? std::string() : rhs.substr(begin, end - begin + 1);
    if (rhs.size() < 7 || rhs.substr(rhs.size() - 2) != "()") {
        return false;
    }
    auto name = rhs.substr(0, rhs.size() - 2);
    if (name.size() < 10 || name.substr(name.size() - 10) != "_comb_func") {
        return false;
    }
    for (auto c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

static bool isSideEffectReadExpr(std::string rhs)
{
    auto begin = rhs.find_first_not_of(" \t\r\n");
    auto end = rhs.find_last_not_of(" \t\r\n");
    rhs = begin == std::string::npos ? std::string() : rhs.substr(begin, end - begin + 1);
    return rhs.size() >= 6 && rhs.rfind("((", 0) == 0 &&
           rhs.substr(rhs.size() - 2) == "))" &&
           rhs.find("(), ") != std::string::npos;
}

template<typename T>
static std::string tok(const T& token)
{
    auto raw = std::string(token.rawText());
    if (raw == "==" || raw == "!=" || raw == "===" || raw == "!==" ||
        raw == "&&" || raw == "||" || raw == "<<" || raw == ">>" ||
        raw == "<<<" || raw == ">>>" || raw == "+:" || raw == "-:" ||
        raw == "<=" || raw == ">=" || raw == "**") {
        return raw;
    }
    auto text = std::string(token.valueText());
    if (text.empty()) {
        text = raw;
    }
    return text;
}

static std::string trim(std::string s)
{
    while (!s.empty() && std::isspace((unsigned char)s.front())) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace((unsigned char)s.back())) {
        s.pop_back();
    }
    return s;
}


static bool isIdentifierChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

static bool isClockPortName(const std::string& name)
{
    auto n = name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return n == "clk" || n == "clk_i" || n == "clock" || n == "clock_i" ||
           n == "hclk" || n == "aclk" || n == "clk_in" || n == "clock_in" ||
           n.rfind("clk_", 0) == 0 || n.rfind("clock_", 0) == 0 ||
           (n.size() > 4 && n.substr(n.size() - 4) == "_clk") ||
           (n.size() > 6 && n.substr(n.size() - 6) == "_clk_i") ||
           (n.size() > 5 && n.substr(n.size() - 5) == "_hclk") ||
           (n.size() > 5 && n.substr(n.size() - 5) == "_aclk") ||
           (n.size() > 6 && n.substr(n.size() - 6) == "_clock");
}

static bool isIdentifierUsed(const std::string& text, const std::string& name)
{
    if (name.empty()) {
        return false;
    }
    size_t pos = 0;
    while ((pos = text.find(name, pos)) != std::string::npos) {
        bool leftOk = pos == 0 || !isIdentifierChar(text[pos - 1]);
        auto end = pos + name.size();
        bool rightOk = end >= text.size() || !isIdentifierChar(text[end]);
        if (leftOk && rightOk) {
            return true;
        }
        pos = end;
    }
    return false;
}

static bool exprReferencesModuleState(const ModuleGen& m, const std::string& text)
{
    for (const auto& name : m.varNames) {
        if (isIdentifierUsed(text, name)) {
            return true;
        }
    }
    for (const auto& item : m.outputRegTypes) {
        if (isIdentifierUsed(text, item.first)) {
            return true;
        }
    }
    return false;
}

static std::string zeroAssignmentRhsForLValue(const std::string& lhs)
{
    if (lhs.find(".bits(") != std::string::npos || lhs.find(".get(") != std::string::npos) {
        return "";
    }
    if (lhs.find('.') != std::string::npos) {
        return "std::remove_cvref_t<decltype(" + lhs + ")>{}";
    }
    if (lhs.find('[') != std::string::npos) {
        return "0";
    }
    return "";
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to);
static void replaceIdentifierAll(std::string& s, const std::string& from, const std::string& to);
static bool isNumericValueType(const std::string& type);

static bool isZeroLiteralText(const std::string& expr)
{
    auto s = trim(expr);
    if (s == "0" || s == "0b0" || s == "1'b0" || s == "logic<1>{}" || s == "false") {
        return true;
    }
    auto typedZero = [&](const std::string& prefix) {
        if (s.rfind(prefix, 0) != 0 || s.empty() || s.back() != ')') {
            return false;
        }
        auto gt = s.find('>');
        if (gt == std::string::npos || gt + 1 >= s.size() || s[gt + 1] != '(') {
            return false;
        }
        auto inner = trim(s.substr(gt + 2, s.size() - gt - 3));
        return inner == "0" || inner == "0b0" || inner == "1'b0" || inner == "false";
    };
    return typedZero("logic<") || typedZero("u<");
}

static bool needsTypedZero(const std::string& type)
{
    auto t = trim(type);
    return !t.empty() && t != "bool" && t.rfind("logic<", 0) != 0 && t.rfind("u<", 0) != 0;
}

static std::string templateParamName(const std::string& param)
{
    auto s = trim(param);
    auto eq = s.find('=');
    if (eq != std::string::npos) {
        s = trim(s.substr(0, eq));
    }
    auto pos = s.find_last_of(" 	*&");
    if (pos != std::string::npos) {
        s = trim(s.substr(pos + 1));
    }
    while (!s.empty() && (s.back() == '&' || s.back() == '*')) {
        s.pop_back();
        s = trim(s);
    }
    return s;
}

static std::string templateParamDefault(const std::string& param)
{
    auto eq = param.find('=');
    if (eq == std::string::npos) {
        return "";
    }
    return trim(param.substr(eq + 1));
}

static std::string cppTemplateBaseName(std::string type)
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
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        type.pop_back();
        type = trim(type);
    }
    const std::string constPrefix = "const ";
    if (type.rfind(constPrefix, 0) == 0) {
        type = trim(type.substr(constPrefix.size()));
    }
    return type;
}

static std::vector<std::string> splitTopLevelArgs(const std::string& text)
{
    std::vector<std::string> args;
    std::string current;
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
            current += c;
            current += text[++i];
            continue;
        }
        else if (c == '<') {
            ++angle;
        }
        else if (c == '>' && angle > 0) {
            --angle;
            if (i + 1 < text.size() && text[i + 1] == '>' && angle > 0) {
                current += c;
                c = text[++i];
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
        if (c == ',' && paren == 0 && angle == 0 && brace == 0 && bracket == 0) {
            args.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    if (!current.empty() || !text.empty()) {
        args.push_back(trim(current));
    }
    return args;
}

static std::map<std::string, std::string> localUsingTypeAliases(const ModuleGen& mod)
{
    std::map<std::string, std::string> aliases;
    for (auto decl : mod.typeDecls) {
        decl = trim(std::move(decl));
        const std::string prefix = "using ";
        if (decl.rfind(prefix, 0) != 0) {
            continue;
        }
        auto eq = decl.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto name = trim(decl.substr(prefix.size(), eq - prefix.size()));
        auto type = trim(decl.substr(eq + 1));
        if (!type.empty() && type.back() == ';') {
            type.pop_back();
            type = trim(type);
        }
        if (!name.empty() && !type.empty()) {
            aliases[name] = type;
        }
    }
    return aliases;
}

static std::string templateParamValueType(const std::string& param)
{
    auto text = trim(param);
    if (text.rfind("typename ", 0) == 0 || text.rfind("class ", 0) == 0) {
        return "";
    }
    auto eq = text.find('=');
    if (eq != std::string::npos) {
        text = trim(text.substr(0, eq));
    }
    auto pos = text.rfind(' ');
    if (pos == std::string::npos) {
        return "";
    }
    auto type = trim(text.substr(0, pos));
    if (type == "unsigned" || type == "uint64_t" || type == "uint32_t" || type == "uint16_t" ||
        type == "uint8_t" || type == "int" || type == "bool") {
        return type;
    }
    return "";
}

static std::string normalizeTemplateParamDecl(std::string param)
{
    auto eq = param.find('=');
    if (eq == std::string::npos) {
        return param;
    }
    auto lhs = trim(param.substr(0, eq));
    auto rhs = trim(param.substr(eq + 1));
    if (!rhs.empty() && rhs.front() == '"' &&
        (lhs.rfind("unsigned ", 0) == 0 || lhs.rfind("uint64_t ", 0) == 0 ||
         lhs.rfind("uint32_t ", 0) == 0 || lhs.rfind("int ", 0) == 0)) {
        auto name = templateParamName(lhs);
        if (!name.empty()) {
            return "hdlcpp_fixed_string " + name + " = " + rhs;
        }
    }
    if (lhs.rfind("unsigned ", 0) == 0 && !rhs.empty() && rhs.front() == '-' &&
        rhs.rfind("static_cast<unsigned>", 0) != 0) {
        param = param.substr(0, eq + 1) + " static_cast<unsigned>(" + rhs + ")";
    }
    return param;
}

static std::string castTemplateParamValue(const std::string& declared, const std::string& value)
{
    auto type = templateParamValueType(declared);
    if (type.empty()) {
        return value;
    }
    auto v = trim(value);
    if (v.rfind("static_cast<", 0) == 0 || v.rfind("(uint64_t)(", 0) == 0 || v.rfind("((uint64_t)(", 0) == 0) {
        return value;
    }
    bool expressionLike = v.find('.') != std::string::npos || v.find("logic<") != std::string::npos ||
                          v.find('(') != std::string::npos || v.find('?') != std::string::npos ||
                          v.find('&') != std::string::npos || v.find('|') != std::string::npos ||
                          v.find('+') != std::string::npos || v.find('-') != std::string::npos ||
                          v.find('*') != std::string::npos || v.find('/') != std::string::npos;
    if (!expressionLike) {
        return value;
    }
    return "static_cast<" + type + ">(" + value + ")";
}

static std::vector<std::string> configuredModuleParams(const std::string& type)
{
    static bool loaded = false;
    static std::map<std::string, std::vector<std::string>> params;
    if (!loaded) {
        loaded = true;
        if (auto* path = std::getenv("HDLCPP_MODULE_PARAMS")) {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                auto sep = line.find('\t');
                if (sep == std::string::npos) {
                    continue;
                }
                auto module = trim(line.substr(0, sep));
                auto rest = line.substr(sep + 1);
                std::vector<std::string> decls;
                for (size_t start = 0; start <= rest.size();) {
                    auto end = rest.find('\t', start);
                    auto item = trim(rest.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (!item.empty()) {
                        decls.push_back(item);
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
                if (!module.empty() && !decls.empty()) {
                    params[module] = decls;
                }
            }
        }
    }
    auto it = params.find(type);
    return it == params.end() ? std::vector<std::string>{} : it->second;
}

static bool configuredModuleTrait(const std::string& type, const std::string& trait)
{
    static bool loaded = false;
    static std::map<std::string, std::set<std::string>> traits;
    if (!loaded) {
        loaded = true;
        if (auto* path = std::getenv("HDLCPP_MODULE_TRAITS")) {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                auto sep = line.find('\t');
                if (sep == std::string::npos) {
                    continue;
                }
                auto module = trim(line.substr(0, sep));
                auto rest = line.substr(sep + 1);
                for (size_t start = 0; start <= rest.size();) {
                    auto end = rest.find('\t', start);
                    auto item = trim(rest.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    if (!module.empty() && !item.empty()) {
                        traits[module].insert(item);
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
            }
        }
    }
    auto it = traits.find(type);
    if (it == traits.end()) {
        return false;
    }
    return it->second.count(trait) != 0 || it->second.count(trait + "=1") != 0 ||
           it->second.count(trait + "=true") != 0;
}

static std::string templateParamDefaultValue(const std::string& decl)
{
    int angleDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    for (size_t i = 0; i < decl.size(); ++i) {
        char ch = decl[i];
        if (ch == '<') {
            ++angleDepth;
        }
        else if (ch == '>' && angleDepth > 0) {
            --angleDepth;
        }
        else if (ch == '(') {
            ++parenDepth;
        }
        else if (ch == ')' && parenDepth > 0) {
            --parenDepth;
        }
        else if (ch == '[') {
            ++bracketDepth;
        }
        else if (ch == ']' && bracketDepth > 0) {
            --bracketDepth;
        }
        else if (ch == '=' && angleDepth == 0 && parenDepth == 0 && bracketDepth == 0) {
            return trim(decl.substr(i + 1));
        }
    }
    return {};
}

static size_t matchingTemplateCloseLocal(const std::string& s, size_t open)
{
    if (open >= s.size() || s[open] != '<') {
        return std::string::npos;
    }
    int depth = 1;
    for (size_t i = open + 1; i < s.size(); ++i) {
        if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '<') {
            ++i;
            continue;
        }
        if (s[i] == '>' && i + 1 < s.size() && s[i + 1] == '>' &&
            (i == 0 || std::isspace(static_cast<unsigned char>(s[i - 1])))) {
            ++i;
            continue;
        }
        if (s[i] == '<') {
            ++depth;
        }
        else if (s[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

static bool findTopLevelTernaryExpr(const std::string& text, size_t& qpos, size_t& cpos)
{
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    qpos = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '(') ++paren;
        else if (c == ')' && paren > 0) --paren;
        else if (c == '[') ++bracket;
        else if (c == ']' && bracket > 0) --bracket;
        else if (c == '{') ++brace;
        else if (c == '}' && brace > 0) --brace;
        else if (c == '?' && paren == 0 && bracket == 0 && brace == 0) {
            qpos = i;
            break;
        }
    }
    if (qpos == std::string::npos) {
        return false;
    }
    paren = bracket = brace = 0;
    for (size_t i = qpos + 1; i < text.size(); ++i) {
        char c = text[i];
        if (c == '(') ++paren;
        else if (c == ')' && paren > 0) --paren;
        else if (c == '[') ++bracket;
        else if (c == ']' && bracket > 0) --bracket;
        else if (c == '{') ++brace;
        else if (c == '}' && brace > 0) --brace;
        else if (c == ':' && paren == 0 && bracket == 0 && brace == 0 &&
                 (i == 0 || text[i - 1] != ':') &&
                 (i + 1 >= text.size() || text[i + 1] != ':')) {
            cpos = i;
            return true;
        }
    }
    return false;
}

static std::string stripBalancedOuterParens(std::string text)
{
    text = trim(std::move(text));
    bool changed = true;
    while (changed && text.size() >= 2 && text.front() == '(' && text.back() == ')') {
        changed = false;
        int depth = 0;
        bool wraps = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            }
            else if (text[i] == ')') {
                --depth;
                if (depth == 0 && i + 1 < text.size()) {
                    wraps = false;
                    break;
                }
            }
        }
        if (wraps && depth == 0) {
            text = trim(text.substr(1, text.size() - 2));
            changed = true;
        }
    }
    return text;
}

static std::string dropExtraTrailingClosingParens(std::string text)
{
    for (;;) {
        int balance = 0;
        for (char ch : text) {
            if (ch == '(') {
                ++balance;
            }
            else if (ch == ')') {
                --balance;
            }
        }
        if (balance >= 0) {
            return text;
        }
        auto pos = text.find_last_not_of(" \t\r\n");
        if (pos == std::string::npos || text[pos] != ')') {
            return text;
        }
        text.erase(pos, 1);
    }
}

static std::string coerceNumericWidthTernary(std::string expr)
{
    auto stripped = stripBalancedOuterParens(expr);
    size_t qpos = std::string::npos;
    size_t cpos = std::string::npos;
    if (!findTopLevelTernaryExpr(stripped, qpos, cpos)) {
        return expr;
    }
    auto pred = trim(stripped.substr(0, qpos));
    auto left = trim(stripped.substr(qpos + 1, cpos - qpos - 1));
    auto right = trim(stripped.substr(cpos + 1));
    return "((uint64_t)(" + pred + ") ? (uint64_t)(" + left + ") : (uint64_t)(" + right + "))";
}

static std::string coerceLogicWidthTernaries(std::string text)
{
    for (size_t pos = 0; (pos = text.find("logic<", pos)) != std::string::npos;) {
        auto open = pos + std::string("logic").size();
        auto close = matchingTemplateCloseLocal(text, open);
        if (close == std::string::npos) {
            break;
        }
        auto inner = text.substr(open + 1, close - open - 1);
        auto coerced = coerceNumericWidthTernary(inner);
        if (coerced != inner) {
            text.replace(open + 1, close - open - 1, coerced);
            pos = open + 1 + coerced.size();
        }
        else {
            pos = close + 1;
        }
    }
    return text;
}

static std::string substituteParamDeclValues(const std::vector<std::string>& declaredParams,
                                             const std::vector<std::string>& actualParams,
                                             std::string text)
{
    if (text.empty() || declaredParams.empty()) {
        return text;
    }
    std::vector<std::pair<std::string, std::string>> values;
    for (size_t i = 0; i < declaredParams.size(); ++i) {
        auto name = templateParamName(declaredParams[i]);
        if (name.empty()) {
            continue;
        }
        std::string value;
        if (i < actualParams.size() && !actualParams[i].empty()) {
            value = actualParams[i];
        }
        else {
            value = templateParamDefaultValue(declaredParams[i]);
        }
        if (!value.empty()) {
            values.push_back({name, value});
        }
    }
    for (size_t pass = 0; pass < values.size() + 2; ++pass) {
        auto before = text;
        for (const auto& item : values) {
            replaceIdentifierAll(text, item.first, item.second);
        }
        if (text == before) {
            break;
        }
    }
    return coerceLogicWidthTernaries(text);
}

static std::string substituteModuleParamValues(const std::string& moduleType, const std::string& params, std::string text)
{
    if (text.empty() || params.empty()) {
        return text;
    }
    auto declaredParams = configuredModuleParams(moduleType);
    auto actualParams = splitTopLevelArgs(params);
    return substituteParamDeclValues(declaredParams, actualParams, std::move(text));
}

struct ConfiguredLinePatch {
    std::string mode;
    std::string needle;
    std::string replacement;
};

static std::string decodePatchText(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            if (c == 'n') {
                out += '\n';
            }
            else if (c == 't') {
                out += '\t';
            }
            else if (c == '\\') {
                out += '\\';
            }
            else {
                out += c;
            }
        }
        else {
            out += s[i];
        }
    }
    return out;
}

static std::string decodeConfiguredText(std::string s)
{
    s = trim(s);
    if (s.size() > 1 && s[0] == '@') {
        std::ifstream in(s.substr(1));
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    return decodePatchText(s);
}

static const std::vector<ConfiguredLinePatch>& configuredLinePatches()
{
    static bool loaded = false;
    static std::vector<ConfiguredLinePatch> patches;
    if (!loaded) {
        loaded = true;
        if (auto* path = std::getenv("HDLCPP_LINE_PATCHES")) {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                auto first = line.find('\t');
                auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
                if (first == std::string::npos || second == std::string::npos) {
                    continue;
                }
                ConfiguredLinePatch patch;
                patch.mode = trim(line.substr(0, first));
                patch.needle = decodePatchText(line.substr(first + 1, second - first - 1));
                patch.replacement = decodePatchText(line.substr(second + 1));
                if (!patch.mode.empty() && !patch.needle.empty()) {
                    patches.push_back(std::move(patch));
                }
            }
        }
    }
    return patches;
}

static void applyConfiguredLinePatches(std::string& line)
{
    for (const auto& patch : configuredLinePatches()) {
        if (patch.mode == "replace") {
            replaceAll(line, patch.needle, patch.replacement);
        }
        else if (patch.mode == "whole-contains" && line.find(patch.needle) != std::string::npos) {
            line = patch.replacement;
        }
        else if (patch.mode == "prefix" && trim(line).rfind(patch.needle, 0) == 0) {
            auto indent = line.substr(0, line.find_first_not_of(" \t"));
            line = indent + patch.replacement;
        }
    }
}

static void repairPatchedConcatOperandWidths(std::string& line)
{
    if (line.find("cat{") == std::string::npos) {
        return;
    }
    const std::string prefix = "logic<64>((uint64_t)(logic<";
    for (size_t pos = 0; (pos = line.find(prefix, pos)) != std::string::npos;) {
        auto widthStart = pos + prefix.size();
        int depth = 1;
        size_t widthEnd = std::string::npos;
        for (size_t i = widthStart; i < line.size(); ++i) {
            if (i + 1 < line.size() && line[i] == '<' && line[i + 1] == '<') {
                ++i;
                continue;
            }
            if (i + 1 < line.size() && line[i] == '>' && line[i + 1] == '>') {
                ++i;
                continue;
            }
            if (line[i] == '<') {
                ++depth;
            }
            else if (line[i] == '>') {
                if (--depth == 0) {
                    widthEnd = i;
                    break;
                }
            }
        }
        if (widthEnd == std::string::npos) {
            break;
        }
        auto width = trim(line.substr(widthStart, widthEnd - widthStart));
        if (width.empty() || width == "64" || width == "(64)" || width == "(uint64_t)(64)") {
            pos = widthEnd + 1;
            continue;
        }
        auto replacement = "logic<" + width + ">((uint64_t)(logic<";
        line.replace(pos, prefix.size(), replacement);
        pos += replacement.size() + width.size();
    }
}

static std::set<std::string> configuredNameSet(const char* envName)
{
    std::set<std::string> out;
    if (auto* raw = std::getenv(envName)) {
        std::stringstream ss(raw);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (!item.empty()) {
                out.insert(item);
            }
        }
    }
    return out;
}

static bool configuredNameEquals(const char* envName, const std::string& value)
{
    auto names = configuredNameSet(envName);
    return names.count(value) != 0;
}

static std::map<std::string, std::string> configuredTextMap(const char* envName)
{
    static std::map<std::string, std::map<std::string, std::string>> cache;
    auto pathValue = std::getenv(envName) ? std::string(std::getenv(envName)) : std::string();
    auto key = std::string(envName) + "=" + pathValue;
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }
    std::map<std::string, std::string> out;
    if (!pathValue.empty()) {
        std::ifstream in(pathValue);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            auto sep = line.find('\t');
            if (sep == std::string::npos) {
                continue;
            }
            out[trim(line.substr(0, sep))] = decodeConfiguredText(line.substr(sep + 1));
        }
    }
    cache[key] = out;
    return out;
}

static std::optional<uint64_t> parseConfiguredUint(std::string value)
{
    value = trim(std::move(value));
    auto stripOuterParens = [](std::string text) {
        text = trim(std::move(text));
        bool changed = true;
        while (changed && text.size() >= 2 && text.front() == '(' && text.back() == ')') {
            changed = false;
            int depth = 0;
            bool wraps = true;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '(') {
                    ++depth;
                }
                else if (text[i] == ')') {
                    --depth;
                    if (depth == 0 && i + 1 != text.size()) {
                        wraps = false;
                        break;
                    }
                    if (depth < 0) {
                        wraps = false;
                        break;
                    }
                }
            }
            if (wraps && depth == 0) {
                text = trim(text.substr(1, text.size() - 2));
                changed = true;
            }
        }
        return text;
    };
    value = stripOuterParens(value);
    bool unwrappedCast = true;
    while (unwrappedCast) {
        unwrappedCast = false;
        value = stripOuterParens(value);
        if (value.rfind("(uint64_t)", 0) == 0) {
            value = stripOuterParens(value.substr(std::strlen("(uint64_t)")));
            unwrappedCast = true;
            continue;
        }
        if (value.rfind("static_cast<", 0) == 0) {
            auto close = value.find(">(");
            if (close != std::string::npos && value.back() == ')') {
                value = stripOuterParens(value.substr(close + 1));
                unwrappedCast = true;
                continue;
            }
        }
        auto svCast = value.find("'(");
        if (svCast != std::string::npos && value.back() == ')') {
            auto type = trim(value.substr(0, svCast));
            bool typeOk = !type.empty() &&
                std::all_of(type.begin(), type.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '_' || c == ':' || c == '<' ||
                           c == '>' || c == ',' || c == ' ' || c == '\t';
                });
            if (typeOk) {
                value = stripOuterParens(value.substr(svCast + 1));
                unwrappedCast = true;
            }
        }
    }
    auto findTopLevelOp = [](const std::string& text, const std::string& op) -> size_t {
        int depth = 0;
        for (size_t i = 0; i + op.size() <= text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
                continue;
            }
            if (text[i] == ')') {
                --depth;
                continue;
            }
            if (depth == 0 && text.compare(i, op.size(), op) == 0) {
                return i;
            }
        }
        return std::string::npos;
    };
    for (const auto& op : {std::string("&"), std::string("|"), std::string("+"), std::string("-")}) {
        auto pos = findTopLevelOp(value, op);
        if (pos != std::string::npos) {
            auto lhs = parseConfiguredUint(value.substr(0, pos));
            auto rhs = parseConfiguredUint(value.substr(pos + op.size()));
            if (lhs && rhs) {
                if (op == "&") return *lhs & *rhs;
                if (op == "|") return *lhs | *rhs;
                if (op == "+") return *lhs + *rhs;
                if (op == "-") return *lhs - *rhs;
            }
        }
    }
    {
        auto pos = findTopLevelOp(value, "<<");
        if (pos != std::string::npos) {
            auto lhs = parseConfiguredUint(value.substr(0, pos));
            auto rhs = parseConfiguredUint(value.substr(pos + 2));
            if (lhs && rhs && *rhs < 64) {
                return *lhs << *rhs;
            }
        }
    }
    if (value.size() > 3 && value.substr(value.size() - 3) == "ull") {
        value.resize(value.size() - 3);
    }
    else if (value.size() > 2 && value.substr(value.size() - 2) == "ul") {
        value.resize(value.size() - 2);
    }
    else if (value.size() > 1 && (value.back() == 'u' || value.back() == 'U' || value.back() == 'l' || value.back() == 'L')) {
        value.pop_back();
    }
    if (value.empty()) {
        return std::nullopt;
    }
    if (value == "false") {
        return 0;
    }
    if (value == "true") {
        return 1;
    }
    if (value.rfind("0b", 0) == 0 || value.rfind("0B", 0) == 0) {
        uint64_t parsed = 0;
        for (size_t i = 2; i < value.size(); ++i) {
            if (value[i] != '0' && value[i] != '1') {
                return std::nullopt;
            }
            parsed = (parsed << 1) | static_cast<uint64_t>(value[i] == '1');
        }
        return parsed;
    }
    try {
        size_t pos = 0;
        auto parsed = std::stoull(value, &pos, 0);
        if (pos == value.size()) {
            return parsed;
        }
    }
    catch (...) {
    }
    return std::nullopt;
}

static std::string stripConfiguredCastsAndParens(std::string value)
{
    auto stripOuterParens = [](std::string text) {
        text = trim(std::move(text));
        bool changed = true;
        while (changed && text.size() >= 2 && text.front() == '(' && text.back() == ')') {
            changed = false;
            int depth = 0;
            bool wraps = true;
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '(') {
                    ++depth;
                }
                else if (text[i] == ')') {
                    --depth;
                    if (depth == 0 && i + 1 != text.size()) {
                        wraps = false;
                        break;
                    }
                    if (depth < 0) {
                        wraps = false;
                        break;
                    }
                }
            }
            if (wraps && depth == 0) {
                text = trim(text.substr(1, text.size() - 2));
                changed = true;
            }
        }
        return text;
    };

    value = stripOuterParens(std::move(value));
    bool changed = true;
    while (changed) {
        changed = false;
        value = stripOuterParens(value);
        if (value.rfind("(uint64_t)", 0) == 0) {
            value = stripOuterParens(value.substr(std::strlen("(uint64_t)")));
            changed = true;
            continue;
        }
        if (value.rfind("static_cast<", 0) == 0) {
            auto close = value.find(">(");
            if (close != std::string::npos && value.back() == ')') {
                value = stripOuterParens(value.substr(close + 1));
                changed = true;
                continue;
            }
        }
        auto svCast = value.find("'(");
        if (svCast != std::string::npos && value.back() == ')') {
            auto type = trim(value.substr(0, svCast));
            bool typeOk = !type.empty() &&
                std::all_of(type.begin(), type.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '_' || c == ':' || c == '<' ||
                           c == '>' || c == ',' || c == ' ' || c == '\t';
                });
            if (typeOk) {
                value = stripOuterParens(value.substr(svCast + 1));
                changed = true;
            }
        }
    }
    return trim(std::move(value));
}

static std::optional<std::string> parseConfiguredString(std::string value)
{
    value = stripConfiguredCastsAndParens(std::move(value));
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return std::nullopt;
    }
    std::string out;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) {
            ++i;
        }
        out += value[i];
    }
    return out;
}

static size_t findConfiguredTopLevelOp(const std::string& text, const std::string& op)
{
    int parenDepth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = 0; i + op.size() <= text.size(); ++i) {
        char ch = text[i];
        if (inString) {
            if (escape) {
                escape = false;
            }
            else if (ch == '\\') {
                escape = true;
            }
            else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '(') {
            ++parenDepth;
            continue;
        }
        if (ch == ')') {
            --parenDepth;
            continue;
        }
        if (parenDepth == 0 && text.compare(i, op.size(), op) == 0) {
            return i;
        }
    }
    return std::string::npos;
}

static bool configuredNameChar(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':' || ch == '.';
}

static void replaceConfiguredName(std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        bool leftOk = pos == 0 || !configuredNameChar(text[pos - 1]);
        auto end = pos + from.size();
        bool rightOk = end >= text.size() || !configuredNameChar(text[end]);
        if (leftOk && rightOk) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
        else {
            pos = end;
        }
    }
}

static std::optional<bool> evalConfiguredGenerateCondition(const ModuleGen& m, std::string cond)
{
    auto originalCond = cond;
    auto traceDecision = [&](const std::string& substituted, std::optional<bool> result) {
        if (std::getenv("HDLCPP_TRACE_GENERATE_CONDITIONS")) {
            std::cerr << "HDLCPP_GENERATE_CONDITION " << m.name
                      << " result=" << (result ? (*result ? "true" : "false") : "null")
                      << " original=" << originalCond
                      << " substituted=" << substituted << "\n";
        }
        return result;
    };
    auto values = configuredTextMap("HDLCPP_GENERATE_PARAM_VALUES");
    if (values.empty()) {
        return traceDecision(cond, std::nullopt);
    }

    std::vector<std::pair<std::string, std::string>> orderedValues(values.begin(), values.end());
    std::sort(orderedValues.begin(), orderedValues.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first.size() > rhs.first.size();
    });

    for (auto& item : orderedValues) {
        auto key = item.first;
        replaceAll(cond, "(uint64_t)(" + key + ")", item.second);
        replaceConfiguredName(cond, key, item.second);
        auto dot = key.find('.');
        if (dot != std::string::npos) {
            if (key.substr(0, dot) != m.name) {
                continue;
            }
            key = key.substr(dot + 1);
        }
        replaceAll(cond, "(uint64_t)(" + key + ")", item.second);
        replaceConfiguredName(cond, key, item.second);
    }

    cond = stripConfiguredCastsAndParens(cond);
    if (cond.empty()) {
        return std::nullopt;
    }
    for (auto* op : {"||", "&&"}) {
        auto pos = findConfiguredTopLevelOp(cond, op);
        if (pos == std::string::npos) {
            continue;
        }
        auto lhs = evalConfiguredGenerateCondition(m, cond.substr(0, pos));
        auto rhs = evalConfiguredGenerateCondition(m, cond.substr(pos + std::strlen(op)));
        if (!lhs || !rhs) {
            return traceDecision(cond, std::nullopt);
        }
        return traceDecision(cond, std::string(op) == "||" ? (*lhs || *rhs) : (*lhs && *rhs));
    }
    if (cond.front() == '!') {
        auto inner = evalConfiguredGenerateCondition(m, cond.substr(1));
        if (inner) {
            return traceDecision(cond, !*inner);
        }
        return traceDecision(cond, std::nullopt);
    }

    const char* ops[] = {"==", "!=", ">=", "<=", ">", "<"};
    for (auto* op : ops) {
        auto pos = findConfiguredTopLevelOp(cond, op);
        if (pos == std::string::npos) {
            continue;
        }
        std::string opText(op);
        if (opText == "==" || opText == "!=") {
            auto lhsString = parseConfiguredString(cond.substr(0, pos));
            auto rhsString = parseConfiguredString(cond.substr(pos + std::strlen(op)));
            if (lhsString && rhsString) {
                return traceDecision(cond, opText == "==" ? *lhsString == *rhsString : *lhsString != *rhsString);
            }
        }
        auto lhs = parseConfiguredUint(cond.substr(0, pos));
        auto rhs = parseConfiguredUint(cond.substr(pos + std::strlen(op)));
        if (!lhs || !rhs) {
            return traceDecision(cond, std::nullopt);
        }
        if (opText == "==") return traceDecision(cond, *lhs == *rhs);
        if (opText == "!=") return traceDecision(cond, *lhs != *rhs);
        if (opText == ">=") return traceDecision(cond, *lhs >= *rhs);
        if (opText == "<=") return traceDecision(cond, *lhs <= *rhs);
        if (opText == ">") return traceDecision(cond, *lhs > *rhs);
        if (opText == "<") return traceDecision(cond, *lhs < *rhs);
    }

    auto value = parseConfiguredUint(cond);
    if (value) {
        return traceDecision(cond, *value != 0);
    }
    if (std::getenv("HDLCPP_TRACE_PHASES")) {
        std::cerr << "HDLCPP_PHASE unresolved_generate_condition " << m.name
                  << " original=" << originalCond << " substituted=" << cond << "\n";
    }
    return traceDecision(cond, std::nullopt);
}

static bool lhsAssignsField(const std::string& lhs, const std::string& base, const std::string& field)
{
    if (field.empty()) {
        return true;
    }
    if (lhs == base) {
        return true;
    }
    if (lhs.rfind(base + ".", 0) != 0) {
        return false;
    }
    auto pos = base.size() + 1;
    if (lhs.compare(pos, field.size(), field) != 0) {
        return false;
    }
    auto next = pos + field.size();
    return next >= lhs.size() || lhs[next] == '.' || lhs[next] == '[';
}

static bool moduleMethodExists(const ModuleGen& m, const std::string& name)
{
    return std::any_of(m.methods.begin(), m.methods.end(), [&](const MethodGen& method) {
        return method.name == name;
    });
}

static std::vector<std::string> combDriversFor(const ModuleGen& m, const std::string& base,
                                               const std::string& field = "")
{
    std::vector<std::string> drivers;
    auto addDriver = [&](const std::string& name) {
        if (!name.empty() && std::find(drivers.begin(), drivers.end(), name) == drivers.end()) {
            drivers.push_back(name);
        }
    };

    if (field.empty()) {
        auto preferred = m.preferredCombDriver.find(base);
        if (preferred != m.preferredCombDriver.end()) {
            addDriver(preferred->second);
        }
    }

    if (field.empty() && m.combSideEffectDriver.count(base)) {
        addDriver(m.combSideEffectDriver.at(base));
    }

    if (field.empty()) {
        auto direct = m.wireMap.find(base);
        if (direct != m.wireMap.end()) {
            if (direct->second.find("_comb_func") == std::string::npos || moduleMethodExists(m, direct->second)) {
                addDriver(direct->second);
            }
        }
    }

    auto storage = combStorageName(m, base);
    auto outputStorage = m.outputPortCppNames.count(base) ? outputStorageName(m, base) : std::string();
    for (auto& method : m.methods) {
        if (method.name.find("_comb_func") == std::string::npos) {
            continue;
        }
        if (method.localCombBody) {
            continue;
        }
        for (auto& line : method.body) {
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto lhs = trim(line.substr(0, eq));
            auto lhsBaseEnd = lhs.find_first_of("[.");
            auto lhsBase = trim(lhsBaseEnd == std::string::npos ? lhs : lhs.substr(0, lhsBaseEnd));
            if ((lhs == storage || lhsBase == storage || lhs == base || lhsBase == base ||
                 (!outputStorage.empty() && (lhs == outputStorage || lhsBase == outputStorage))) &&
                (lhsAssignsField(lhs, storage, field) ||
                 lhsAssignsField(lhs, outputStorage, field) ||
                 lhsAssignsField(lhs, base, field))) {
                addDriver(method.name);
                break;
            }
        }
    }
    return drivers;
}

static std::string combDriverFor(const ModuleGen& m, const std::string& base)
{
    auto drivers = combDriversFor(m, base);
    return drivers.empty() ? std::string() : drivers.front();
}

static std::string simpleBoundName(std::string s)
{
    s = trim(std::move(s));
    if (s.size() > 2 && s.substr(s.size() - 2) == "()") {
        s.resize(s.size() - 2);
    }
    if (s.empty()) {
        return "";
    }
    for (auto c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return "";
        }
    }
    return s;
}

static std::string bridgeBoundName(const ModuleGen& m, const std::string& expr)
{
    auto simple = simpleBoundName(expr);
    if (!simple.empty()) {
        return simple;
    }
    auto s = trim(expr);
    for (auto& item : m.assignExprByBase) {
        auto& name = item.first;
        if (s == name) {
            return name;
        }
        auto directBits = name + ".bits(";
        auto callBits = name + "().bits(";
        if (s.rfind("logic<", 0) == 0 &&
            (s.find(directBits) != std::string::npos || s.find(callBits) != std::string::npos)) {
            return name;
        }
    }
    return "";
}

static std::string repairDottedLogicWidthCasts(std::string s);
static std::string repairNumericCastSplitMemberAccess(std::string s)
{
    auto isFieldChar = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };
    auto matchingOpenParen = [](const std::string& text, size_t close) -> size_t {
        int depth = 0;
        for (size_t i = close + 1; i > 0; --i) {
            auto pos = i - 1;
            if (text[pos] == ')') {
                ++depth;
            }
            else if (text[pos] == '(') {
                --depth;
                if (depth == 0) {
                    return pos;
                }
            }
        }
        return std::string::npos;
    };
    auto matchingCloseParen = [](const std::string& text, size_t open) -> size_t {
        int depth = 0;
        for (size_t i = open; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            }
            else if (text[i] == ')') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return std::string::npos;
    };
    auto appendMemberToCastedContent = [&](const std::string& content, const std::string& field) {
        auto inner = trim(content);
        if (!inner.empty() && inner.back() == ')') {
            auto open = matchingOpenParen(inner, inner.size() - 1);
            if (open != std::string::npos) {
                auto callee = trim(inner.substr(0, open));
                bool simpleCallee = !callee.empty();
                for (char ch : callee) {
                    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != ':' && ch != '.') {
                        simpleCallee = false;
                        break;
                    }
                }
                auto args = splitTopLevelArgs(inner.substr(open + 1, inner.size() - open - 2));
                if (simpleCallee && args.size() == 1 && !args[0].empty()) {
                    return callee + "(" + args[0] + "." + field + ")";
                }
            }
        }
        return inner + "." + field;
    };
    const std::string castPrefix = "(uint64_t)(";
    for (size_t pos = 0; (pos = s.find(castPrefix, pos)) != std::string::npos;) {
        auto open = pos + std::string("(uint64_t)").size();
        auto close = matchingCloseParen(s, open);
        if (close == std::string::npos) {
            pos += castPrefix.size();
            continue;
        }
        auto dot = close + 1;
        size_t extraCloseBegin = dot;
        while (dot < s.size() && std::isspace(static_cast<unsigned char>(s[dot]))) {
            ++dot;
        }
        while (dot < s.size() && s[dot] == ')') {
            ++dot;
            while (dot < s.size() && std::isspace(static_cast<unsigned char>(s[dot]))) {
                ++dot;
            }
        }
        if (dot >= s.size() || s[dot] != '.' || dot + 1 >= s.size() ||
            !std::isalpha(static_cast<unsigned char>(s[dot + 1])) && s[dot + 1] != '_') {
            pos = close + 1;
            continue;
        }
        auto fieldStart = dot + 1;
        auto fieldEnd = fieldStart;
        while (fieldEnd < s.size() && isFieldChar(s[fieldEnd])) {
            ++fieldEnd;
        }
        auto content = s.substr(open + 1, close - open - 1);
        auto extraCloses = s.substr(extraCloseBegin, dot - extraCloseBegin);
        auto replacement = "(uint64_t)(" + appendMemberToCastedContent(content, s.substr(fieldStart, fieldEnd - fieldStart)) + ")" + extraCloses;
        s.replace(pos, fieldEnd - pos, replacement);
        pos += replacement.size();
    }
    static const std::regex tripleCastSplit(
        R"(\(uint64_t\)\(\(uint64_t\)\(\(uint64_t\)\(([A-Za-z_][A-Za-z0-9_:]*)\)\)\)\.([A-Za-z_][A-Za-z0-9_]*)\))");
    static const std::regex doubleCastSplit(
        R"(\(uint64_t\)\(\(uint64_t\)\(([A-Za-z_][A-Za-z0-9_:]*)\)\)\)\.([A-Za-z_][A-Za-z0-9_]*)\)\))");
    static const std::regex doubleCastSplitOneClose(
        R"(\(uint64_t\)\(\(uint64_t\)\(([A-Za-z_][A-Za-z0-9_:]*)\)\)\)\.([A-Za-z_][A-Za-z0-9_]*)\))");
    static const std::regex singleCastSplit(
        R"(\(uint64_t\)\(([A-Za-z_][A-Za-z0-9_:]*)\)\.([A-Za-z_][A-Za-z0-9_]*)\))");
    s = std::regex_replace(s, tripleCastSplit, "(uint64_t)((uint64_t)((uint64_t)($1.$2)))");
    s = std::regex_replace(s, doubleCastSplit, "(uint64_t)((uint64_t)($1.$2))");
    s = std::regex_replace(s, doubleCastSplitOneClose, "(uint64_t)((uint64_t)($1.$2))");
    s = std::regex_replace(s, singleCastSplit, "(uint64_t)($1.$2)");
    return s;
}

static std::string repairSplitSvBitsCall(std::string s)
{
    auto matchingCloseParen = [](const std::string& text, size_t open) -> size_t {
        int depth = 0;
        for (size_t i = open; i < text.size(); ++i) {
            if (text[i] == '(') {
                ++depth;
            }
            else if (text[i] == ')') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return std::string::npos;
    };
    auto repairOneName = [&](const std::string& name) {
        for (size_t pos = 0; (pos = s.find(name, pos)) != std::string::npos;) {
            auto open = s.find('(', pos + name.size());
            if (open == std::string::npos) {
                break;
            }
            auto close = matchingCloseParen(s, open);
            if (close == std::string::npos) {
                pos = open + 1;
                continue;
            }
            auto comma = close + 1;
            while (comma < s.size() && std::isspace(static_cast<unsigned char>(s[comma]))) {
                ++comma;
            }
            if (comma >= s.size() || s[comma] != ',') {
                pos = close + 1;
                continue;
            }
            auto argStart = comma + 1;
            while (argStart < s.size() && std::isspace(static_cast<unsigned char>(s[argStart]))) {
                ++argStart;
            }
            if (argStart >= s.size() || s[argStart] != '(') {
                pos = close + 1;
                continue;
            }
            auto argEnd = matchingCloseParen(s, argStart);
            if (argEnd == std::string::npos) {
                pos = close + 1;
                continue;
            }
            auto third = s.substr(argStart, argEnd - argStart + 1);
            s.replace(close, argEnd - close + 1, "," + third + ")");
            pos = close + third.size() + 2;
        }
    };
    repairOneName("cpphdl::sv_bits_runtime");
    for (size_t pos = 0; (pos = s.find("cpphdl::sv_bits<", pos)) != std::string::npos;) {
        auto templ = matchingTemplateCloseLocal(s, pos + std::string("cpphdl::sv_bits").size());
        if (templ == std::string::npos) {
            break;
        }
        auto open = templ + 1;
        if (open >= s.size() || s[open] != '(') {
            pos = templ + 1;
            continue;
        }
        auto close = matchingCloseParen(s, open);
        if (close == std::string::npos) {
            pos = open + 1;
            continue;
        }
        auto comma = close + 1;
        while (comma < s.size() && std::isspace(static_cast<unsigned char>(s[comma]))) {
            ++comma;
        }
        if (comma >= s.size() || s[comma] != ',') {
            pos = close + 1;
            continue;
        }
        auto argStart = comma + 1;
        while (argStart < s.size() && std::isspace(static_cast<unsigned char>(s[argStart]))) {
            ++argStart;
        }
        if (argStart >= s.size() || s[argStart] != '(') {
            pos = close + 1;
            continue;
        }
        auto argEnd = matchingCloseParen(s, argStart);
        if (argEnd == std::string::npos) {
            pos = close + 1;
            continue;
        }
        auto third = s.substr(argStart, argEnd - argStart + 1);
        s.replace(close, argEnd - close + 1, "," + third + ")");
        pos = close + third.size() + 2;
    }
    return s;
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
        s.replace(pos, from.size(), to);
    }
}

static void replaceIdentifierAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty()) {
        return;
    }
    auto isIdent = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
        bool leftOk = pos == 0 || (!isIdent(s[pos - 1]) && s[pos - 1] != '.');
        auto end = pos + from.size();
        bool rightOk = end >= s.size() || !isIdent(s[end]);
        if (leftOk && rightOk) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        else {
            pos = end;
        }
    }
}

static bool isNumber(const std::string& s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c); });
}

static bool isCppKeyword(const std::string& s)
{
    static const std::set<std::string> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
        "case", "catch", "char", "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
        "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires",
        "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor",
        "xor_eq"
    };
    return keywords.count(s) != 0;
}

static std::string knownFunctionReturnWidth(const std::string& callee)
{
    auto name = callee;
    auto widths = configuredTextMap("HDLCPP_FUNCTION_WIDTHS");
    if (auto it = widths.find(name); it != widths.end()) {
        return it->second;
    }
    auto pos = name.rfind("::");
    if (pos != std::string::npos) {
        name = name.substr(pos + 2);
    }
    if (auto it = widths.find(name); it != widths.end()) {
        return it->second;
    }
    return "";
}

static bool wantsNumericFunctionArgs(const std::string& callee)
{
    (void)callee;
    return false;
}

static bool wantsNumericFunctionArg(const std::string& callee, size_t index, bool numericArgs)
{
    auto indices = configuredTextMap("HDLCPP_NUMERIC_ARG_INDICES");
    auto wantsConfiguredIndex = [&](const std::string& name) {
        auto it = indices.find(name);
        if (it == indices.end()) {
            return false;
        }
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (!item.empty() && std::stoull(item) == index) {
                return true;
            }
        }
        return false;
    };
    if (wantsConfiguredIndex(callee)) {
        return true;
    }
    auto pos = callee.rfind("::");
    if (pos != std::string::npos && wantsConfiguredIndex(callee.substr(pos + 2))) {
        return true;
    }
    return numericArgs;
}

static std::string repairMalformedEquality(std::string s)
{
    auto repairPattern = [&](const std::string& pat, const std::string& op) {
    for (size_t pos = 0; (pos = s.find(pat, pos)) != std::string::npos;) {
        auto argStart = pos + pat.size();
        int depth = 1;
        size_t close = argStart;
        for (; close < s.size(); ++close) {
            if (s[close] == '(') {
                ++depth;
            }
            else if (s[close] == ')' && --depth == 0) {
                break;
            }
        }
        if (close >= s.size()) {
            s.replace(pos, pat.size(), " " + op + " ");
            pos += op.size() + 2;
            continue;
        }
        auto rhs = s.substr(argStart, close - argStart);
        s.replace(pos, close - pos + 1, " " + op + " " + rhs + ")");
        pos += op.size() + 3 + rhs.size();
    }
    };
    repairPattern(" = _ASSIGN(= ", "==");
    repairPattern(" != _ASSIGN(", "!=");
    return s;
}

static std::string cppIdent(const std::string& s)
{
    return isCppKeyword(s) ? s + "_" : s;
}

static std::string numericLiteralWidth(const std::string& text)
{
    auto clean = trim(text);
    while (!clean.empty() && clean.front() == '(' && clean.back() == ')') {
        clean = trim(clean.substr(1, clean.size() - 2));
    }
    if (clean.rfind("logic<", 0) == 0) {
        auto end = clean.find('>');
        if (end != std::string::npos) {
            auto width = clean.substr(6, end - 6);
            if (isNumber(width)) {
                return width;
            }
        }
    }
    clean.erase(std::remove(clean.begin(), clean.end(), '_'), clean.end());
    if (clean.rfind("0x", 0) == 0 || clean.rfind("0X", 0) == 0) {
        return std::to_string(std::max<unsigned>(32, (clean.size() > 2 ? unsigned(clean.size() - 2) * 4u : 1u)));
    }
    if (clean.rfind("0b", 0) == 0 || clean.rfind("0B", 0) == 0) {
        return std::to_string(std::max<unsigned>(32, (clean.size() > 2 ? unsigned(clean.size() - 2) : 1u)));
    }
    if (!clean.empty() && std::all_of(clean.begin(), clean.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        return "32";
    }
    return "";
}

static std::string foldWidth(std::string s)
{
    s = trim(s);
    if (isNumber(s)) {
        return s;
    }
    if (s.size() > 4 && s.front() == '(') {
        auto close = s.find(')');
        if (close != std::string::npos && close + 2 < s.size() && s[close + 1] == '+') {
            auto left = s.substr(1, close - 1);
            auto right = s.substr(close + 2);
            if (isNumber(left) && isNumber(right)) {
                return std::to_string(std::stoul(left) + std::stoul(right));
            }
        }
    }
    return s;
}

static std::string primitiveForWidth(const std::string& width)
{
    if (!isNumber(width)) {
        return "uint64_t";
    }
    auto w = std::stoul(width);
    if (w <= 1) {
        return "bool";
    }
    if (w <= 8) {
        return "uint8_t";
    }
    if (w <= 16) {
        return "uint16_t";
    }
    if (w <= 32) {
        return "uint32_t";
    }
    return "uint64_t";
}

static std::string primitiveCast(const std::string& type, const std::string& expr)
{
    return "static_cast<" + type + ">(" + expr + ")";
}

template<typename T>
static std::string listText(const T& list)
{
    std::string s;
    for (auto item : list) {
        s += item->toString();
    }
    return s;
}

static std::string normalizeSvLiterals(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (std::isdigit((unsigned char)s[i])) {
            size_t start = i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) {
                ++i;
            }
            size_t widthEnd = i;
            if (i < s.size() && s[i] == '\'') {
                ++i;
                if (i < s.size() && (s[i] == 's' || s[i] == 'S')) {
                    ++i;
                }
                if (i < s.size()) {
                    char base = std::tolower((unsigned char)s[i++]);
                    std::string digits;
                    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '?')) {
                        char c = s[i++];
                        if (c != '_') {
                            digits += (c == 'x' || c == 'X' || c == 'z' || c == 'Z' || c == '?') ? '0' : c;
                        }
                    }
                    auto width = s.substr(start, widthEnd - start);
                    if (base == 'b') {
                        out += "logic<" + width + ">(0b" + digits + ")";
                        continue;
                    }
                    if (base == 'h') {
                        out += "logic<" + width + ">(0x" + digits + ")";
                        continue;
                    }
                    if (base == 'd') {
                        while (digits.size() > 1 && digits.front() == '0') {
                            digits.erase(digits.begin());
                        }
                        out += "logic<" + width + ">(" + (digits.empty() ? "0" : digits) + ")";
                        continue;
                    }
                }
            }
            out.append(s, start, i - start);
        }
        else {
            out += s[i++];
        }
    }
    return out;
}

static bool isExprBoundary(char c);
static size_t matchingOpenBefore(const std::string& s, size_t close, char openCh, char closeCh);
static size_t selectedExprStartBefore(const std::string& s, size_t endExclusive);
static std::string numericizeSelectedArithmetic(std::string s);

static size_t matchingTemplateClose(const std::string& s, size_t open)
{
    if (open >= s.size() || s[open] != '<') {
        return std::string::npos;
    }
    int depth = 1;
    for (size_t i = open + 1; i < s.size(); ++i) {
        if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '<') {
            ++i;
            continue;
        }
        if (s[i] == '>' && i + 1 < s.size() && s[i + 1] == '>' &&
            (i == 0 || std::isspace(static_cast<unsigned char>(s[i - 1])))) {
            ++i;
            continue;
        }
        if (s[i] == '<') {
            ++depth;
        }
        else if (s[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

static size_t matchingParenClose(const std::string& s, size_t open)
{
    if (open >= s.size() || s[open] != '(') {
        return std::string::npos;
    }
    int depth = 1;
    for (size_t i = open + 1; i < s.size(); ++i) {
        if (s[i] == '(' || s[i] == '[' || s[i] == '{') {
            ++depth;
        }
        else if (s[i] == ')' || s[i] == ']' || s[i] == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

static std::string valueAssignCombFunctionPorts(std::string line)
{
    return line;
}

static bool replicationNeedsCaptureText(const std::string& text);

static std::string postProcessCppLine(std::string line)
{
    line = valueAssignCombFunctionPorts(std::move(line));
    for (size_t pos = 0; (pos = line.find(">(0b) ", pos)) != std::string::npos;) {
        auto digitPos = pos + std::string(">(0b) ").size();
        if (digitPos < line.size() && (line[digitPos] == '0' || line[digitPos] == '1')) {
            line.replace(pos, digitPos - pos + 1, ">(0b" + std::string(1, line[digitPos]) + ")");
            pos += 5;
        }
        else {
            pos = digitPos;
        }
    }
    for (size_t open = 0; (open = line.find("{logic<", open)) != std::string::npos;) {
        if (open >= 3 && line.compare(open - 3, 3, "cat") == 0) {
            open += 1;
            continue;
        }
        int depth = 0;
        bool hasTopComma = false;
        size_t close = std::string::npos;
        for (size_t i = open; i < line.size(); ++i) {
            char c = line[i];
            if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ')' || c == ']') {
                --depth;
                if (depth == 0 && c == '}') {
                    close = i;
                    break;
                }
            }
            else if (c == ',' && depth == 1) {
                hasTopComma = true;
            }
        }
        if (close == std::string::npos) {
            break;
        }
        if (hasTopComma) {
            line.replace(open, 1, "cat{");
            open = close + 3;
        }
        else {
            open = close + 1;
        }
    }
    for (size_t pos = 0; pos < line.size();) {
        if (!std::isdigit(static_cast<unsigned char>(line[pos]))) {
            ++pos;
            continue;
        }
        auto start = pos++;
        while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        if (pos + 1 < line.size() && line[pos] == '\'' && line[pos + 1] == '(') {
            auto width = line.substr(start, pos - start);
            auto repl = "logic<" + width + ">(";
            line.replace(start, pos + 2 - start, repl);
            pos = start + repl.size();
        }
    }
	    replaceAll(line, "<<<", "<<");
	    replaceAll(line, ">>>", ">>");
	    if (line.find(".data.bits(") == std::string::npos) {
	        line = repairDottedLogicWidthCasts(std::move(line));
	    }
	    line = repairNumericCastSplitMemberAccess(std::move(line));
	    line = repairSplitSvBitsCall(std::move(line));
	    applyConfiguredLinePatches(line);
	    repairPatchedConcatOperandWidths(line);
	    repairPatchedConcatOperandWidths(line);
    auto repairOneBitCastsBeforeFields = [&]() {
        const std::string prefix = "logic<1>(";
        for (size_t pos = 0; (pos = line.find(prefix, pos)) != std::string::npos;) {
            auto close = matchingParenClose(line, pos + prefix.size() - 1);
            if (close == std::string::npos || close + 1 >= line.size() || line[close + 1] != '.') {
                pos += prefix.size();
                continue;
            }
            auto fieldStart = close + 2;
            auto fieldEnd = fieldStart;
            while (fieldEnd < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[fieldEnd])) || line[fieldEnd] == '_')) {
                ++fieldEnd;
            }
            auto field = line.substr(fieldStart, fieldEnd - fieldStart);
            if (field.empty() || field == "bits" || field == "get") {
                pos = fieldEnd;
                continue;
            }
            auto inner = line.substr(pos + prefix.size(), close - (pos + prefix.size()));
            auto repl = "(" + inner + ")";
            line.replace(pos, close + 1 - pos, repl);
            pos += repl.size();
        }
    };
    repairOneBitCastsBeforeFields();
    line = repairNumericCastSplitMemberAccess(std::move(line));
    line = repairSplitSvBitsCall(std::move(line));
    auto repairRuntimeLogicWidths = [&]() {
        for (size_t pos = 0; (pos = line.find("logic<", pos)) != std::string::npos;) {
            auto start = pos + 6;
            auto end = matchingTemplateClose(line, pos + 5);
            if (end == std::string::npos) {
                break;
            }
            auto width = line.substr(start, end - start);
            bool runtimeWidth =
                width.find("(i") != std::string::npos || width.find("(j") != std::string::npos ||
                width.find("(k") != std::string::npos || width.find("* i") != std::string::npos ||
                width.find("* j") != std::string::npos || width.find("* k") != std::string::npos ||
                width.find(" i ") != std::string::npos || width.find(" j ") != std::string::npos ||
                width.find(" k ") != std::string::npos;
            if (runtimeWidth) {
                line.replace(start, end - start, "64");
                pos = start + 2;
            }
            else {
                pos = end + 1;
            }
        }
    };
    repairRuntimeLogicWidths();
    line = repairNumericCastSplitMemberAccess(std::move(line));
    line = repairSplitSvBitsCall(std::move(line));
    auto repairForHeaderExtraClosingParens = [&]() {
        for (size_t pos = 0; (pos = line.find("for (", pos)) != std::string::npos;) {
            auto firstSemi = line.find(';', pos);
            if (firstSemi == std::string::npos) {
                break;
            }
            auto semi = line.find(';', firstSemi + 1);
            if (semi == std::string::npos) {
                break;
            }
            int balance = 0;
            for (size_t i = pos; i < semi; ++i) {
                if (line[i] == '(') {
                    ++balance;
                }
                else if (line[i] == ')') {
                    --balance;
                }
            }
            while (balance < 1 && semi > pos) {
                auto rm = semi;
                while (rm > pos && std::isspace(static_cast<unsigned char>(line[rm - 1]))) {
                    --rm;
                }
                if (rm == pos || line[rm - 1] != ')') {
                    break;
                }
                line.erase(rm - 1, 1);
                --semi;
                ++balance;
            }
            pos = semi + 1;
        }
    };
    repairForHeaderExtraClosingParens();
    auto dropExtraClosingParensBeforeDelimiters = [&]() {
        auto parenBalance = [&]() {
            int balance = 0;
            for (char c : line) {
                if (c == '(') {
                    ++balance;
                }
                else if (c == ')') {
                    --balance;
                }
            }
            return balance;
        };
        for (int balance = parenBalance(); balance < 0; balance = parenBalance()) {
            bool removed = false;
            for (size_t pos = line.size(); pos > 0; --pos) {
                size_t idx = pos - 1;
                if (line[idx] != ')') {
                    continue;
                }
                size_t next = idx + 1;
                while (next < line.size() && std::isspace(static_cast<unsigned char>(line[next]))) {
                    ++next;
                }
                if (next < line.size() && (line[next] == ')' || line[next] == '>' || line[next] == ';' || line[next] == ',' || line[next] == ']')) {
                    line.erase(idx, 1);
                    removed = true;
                    break;
                }
            }
            if (!removed) {
                break;
            }
        }
    };
    dropExtraClosingParensBeforeDelimiters();
    auto balanceCompletedLogicStatement = [&]() {
        if (line.find("logic<") == std::string::npos) {
            return;
        }
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.back() != ';') {
            return;
        }
        int balance = 0;
        for (char c : line) {
            if (c == '(') {
                ++balance;
            }
            else if (c == ')') {
                --balance;
            }
        }
        if (balance <= 0 || balance > 8) {
            return;
        }
        std::string closes(static_cast<size_t>(balance), ')');
        auto semi = line.rfind(';');
        if (semi == std::string::npos) {
            return;
        }
        auto insertAt = semi;
        if (line.find("cat{") != std::string::npos) {
            auto catPos = line.find("cat{");
            int braceDepth = 0;
            int parenDepth = 0;
            for (size_t i = 0; i < semi; ++i) {
                char c = line[i];
                if (c == '{') {
                    ++braceDepth;
                }
                else if (c == '}') {
                    --braceDepth;
                }
                else if (c == '(') {
                    ++parenDepth;
                }
                else if (c == ')') {
                    --parenDepth;
                }
                else if (i > catPos && c == ',' && braceDepth > 0 && parenDepth > 0) {
                    auto next = i + 1;
                    while (next < semi && std::isspace(static_cast<unsigned char>(line[next]))) {
                        ++next;
                    }
                    bool catSeparator = line.compare(next, 6, "logic<") == 0 ||
                        line.compare(next, 2, "u<") == 0 ||
                        line.compare(next, 4, "cat{") == 0;
                    if (!catSeparator) {
                        continue;
                    }
                    line.insert(i, std::string(static_cast<size_t>(parenDepth), ')'));
                    return;
                }
            }
            if (semi > 0 && line[semi - 1] == '}') {
                insertAt = semi - 1;
            }
        }
        line.insert(insertAt, closes);
    };
    balanceCompletedLogicStatement();
    line = repairNumericCastSplitMemberAccess(std::move(line));
    line = repairSplitSvBitsCall(std::move(line));
    repairForHeaderExtraClosingParens();
    auto unwrapCatReplicationLambdaCasts = [&]() {
        for (size_t catPos = 0; (catPos = line.find("cat{", catPos)) != std::string::npos;) {
            auto pos = line.find("logic<", catPos + 4);
            if (pos == std::string::npos) {
                break;
            }
            auto widthEnd = matchingTemplateClose(line, pos + 5);
            if (widthEnd == std::string::npos || widthEnd + 1 >= line.size() || line[widthEnd + 1] != '(') {
                catPos += 4;
                continue;
            }
            auto outerClose = matchingParenClose(line, widthEnd + 1);
            if (outerClose == std::string::npos) {
                catPos += 4;
                continue;
            }
            auto inner = trim(line.substr(widthEnd + 2, outerClose - (widthEnd + 2)));
            const std::string castPrefix = "(uint64_t)(";
            if (inner.rfind(castPrefix, 0) == 0 && inner.back() == ')') {
                inner = trim(inner.substr(castPrefix.size(), inner.size() - castPrefix.size() - 1));
            }
            if (inner.find("__cpphdl_rep") == std::string::npos ||
                (inner.rfind("([&]()", 0) != 0 && inner.rfind("([]()", 0) != 0)) {
                catPos = outerClose + 1;
                continue;
            }
            line.replace(pos, outerClose + 1 - pos, inner);
            catPos = pos + inner.size();
        }
    };
    unwrapCatReplicationLambdaCasts();
    auto promoteDynamicReplicationLambdaCaptures = [&]() {
        const std::string needle = "([]() { logic<";
        for (size_t pos = 0; (pos = line.find(needle, pos)) != std::string::npos;) {
            auto end = line.find("}())", pos + needle.size());
            if (end == std::string::npos) {
                pos += needle.size();
                continue;
            }
            if (replicationNeedsCaptureText(line.substr(pos, end - pos))) {
                line.replace(pos + 1, 2, "[&]");
                pos = end + 2;
            }
            else {
                pos = end + 4;
            }
        }
    };
    promoteDynamicReplicationLambdaCaptures();
    auto repairReplicationResultWidths = [&]() {
        auto castArg = [&](size_t searchFrom, const std::string& marker, std::string& arg, size_t& close) {
            auto markerPos = line.find(marker, searchFrom);
            if (markerPos == std::string::npos) {
                return false;
            }
            static const std::array<std::string, 2> casts = {"(std::size_t)(", "(size_t)("};
            size_t castPos = std::string::npos;
            std::string cast;
            for (const auto& candidate : casts) {
                auto p = line.find(candidate, markerPos + marker.size());
                if (p != std::string::npos && (castPos == std::string::npos || p < castPos)) {
                    castPos = p;
                    cast = candidate;
                }
            }
            if (castPos == std::string::npos) {
                return false;
            }
            auto open = castPos + cast.size() - 1;
            close = matchingParenClose(line, open);
            if (close == std::string::npos) {
                return false;
            }
            arg = trim(line.substr(open + 1, close - open - 1));
            return true;
        };
        const std::string oldLogic = "logic<64>";
        const std::string repDecl = oldLogic + " __cpphdl_rep{}; for (";
        for (size_t logicPos = 0; (logicPos = line.find(repDecl, logicPos)) != std::string::npos;) {
            std::string count;
            size_t countClose = std::string::npos;
            if (!castArg(logicPos, "__cpphdl_i <", count, countClose)) {
                logicPos += repDecl.size();
                continue;
            }
            std::string width;
            size_t widthClose = std::string::npos;
            if (!castArg(countClose, "__cpphdl_rep.bits((__cpphdl_i + 1) *", width, widthClose)) {
                logicPos = countClose + 1;
                continue;
            }
            if (count.empty() || width.empty() || width == "64") {
                logicPos = widthClose + 1;
                continue;
            }
            if (line.compare(logicPos, oldLogic.size(), oldLogic) != 0) {
                logicPos = widthClose + 1;
                continue;
            }
            auto replacement = "logic<((uint64_t)(" + count + ") * (uint64_t)(" + width + "))>";
            line.replace(logicPos, oldLogic.size(), replacement);
            logicPos += replacement.size();
        }
    };
    repairReplicationResultWidths();
    auto balanceReplicationAssignments = [&]() {
        for (size_t pos = 0; (pos = line.find("__cpphdl_rep.bits", pos)) != std::string::npos;) {
            auto eq = line.find(" = ", pos);
            if (eq == std::string::npos) {
                pos += std::strlen("__cpphdl_rep.bits");
                continue;
            }
            auto semi = line.find("; } return __cpphdl_rep", eq);
            if (semi == std::string::npos) {
                pos = eq + 3;
                continue;
            }
            int balance = 0;
            for (size_t i = eq + 3; i < semi; ++i) {
                if (line[i] == '(') {
                    ++balance;
                }
                else if (line[i] == ')') {
                    --balance;
                }
            }
            if (balance > 0 && balance <= 8) {
                line.insert(semi, std::string(static_cast<size_t>(balance), ')'));
                semi += static_cast<size_t>(balance);
            }
            pos = semi + 1;
        }
    };
    balanceReplicationAssignments();
    replaceAll(line, "; }()))," , "; }()),");
    replaceAll(line, "; }()))}", "; }())}");
    auto removeSimpleOneBitMasks = [&]() {
        const std::string castPrefix = "((uint64_t)(";
        const std::string maskSuffix = ") & ((1ull << 1) - 1ull))";
        for (size_t pos = 0; (pos = line.find(maskSuffix, pos)) != std::string::npos;) {
            auto exprStart = line.rfind(castPrefix, pos);
            if (exprStart == std::string::npos) {
                pos += maskSuffix.size();
                continue;
            }
            auto exprBegin = exprStart + castPrefix.size();
            auto expr = line.substr(exprBegin, pos - exprBegin);
            bool simple = !expr.empty();
            for (char c : expr) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) {
                    simple = false;
                    break;
                }
            }
            if (!simple) {
                pos += maskSuffix.size();
                continue;
            }
            auto repl = "((uint64_t)(" + expr + "))";
            line.replace(exprStart, pos + maskSuffix.size() - exprStart, repl);
            pos = exprStart + repl.size();
        }
    };
    removeSimpleOneBitMasks();
    auto removeTargetedMasks = [&](const std::string& token) {
        if (line.find(token) == std::string::npos || line.find(" & ((1ull << ") == std::string::npos) {
            return;
        }
        for (size_t amp = 0; (amp = line.find(" & ((1ull << ", amp)) != std::string::npos;) {
            if (amp == 0 || line[amp - 1] != ')') {
                amp += 12;
                continue;
            }
            size_t lhsEnd = amp;
            int depth = 0;
            size_t lhsStart = std::string::npos;
            for (size_t i = amp; i > 0; --i) {
                char c = line[i - 1];
                if (c == ')') {
                    ++depth;
                }
                else if (c == '(') {
                    --depth;
                    if (depth == 0) {
                        lhsStart = i - 1;
                        break;
                    }
                }
            }
            if (lhsStart == std::string::npos) {
                amp += 12;
                continue;
            }
            while (lhsStart > 0 && line[lhsStart - 1] == '(') {
                --lhsStart;
            }
            auto close = line.find(" - 1ull))", amp);
            if (close == std::string::npos) {
                amp += 12;
                continue;
            }
            auto maskEnd = close + std::string(" - 1ull))").size();
            auto lhs = line.substr(lhsStart, lhsEnd - lhsStart);
            if (lhs.find(token) == std::string::npos) {
                amp += 12;
                continue;
            }
            if (lhs.rfind("((((uint64_t)(", 0) == 0) {
                lhs.erase(lhs.begin());
            }
            line.replace(lhsStart, maskEnd - lhsStart, lhs);
            amp = lhsStart + lhs.size();
        }
    };
    if (auto tokens = std::getenv("HDLCPP_REMOVE_TARGETED_MASK_TOKENS")) {
        std::stringstream ss(tokens);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = trim(token);
            if (!token.empty()) {
                removeTargetedMasks(token);
            }
        }
    }
    if (line.find(" & ((1ull << ") != std::string::npos) {
        removeTargetedMasks("issue_instr_i_in()");
        removeTargetedMasks("lsu_ctrl_comb_func().fu");
    }
    applyConfiguredLinePatches(line);
    if (line.find("issue_instr_i_in()") != std::string::npos) {
        replaceAll(line, "(((((uint64_t)((issue_instr_i_in()", "((((uint64_t)((issue_instr_i_in()");
    }
    if (line.find("lsu_ctrl_comb_func().fu") != std::string::npos) {
        replaceAll(line, "(((((uint64_t)(lsu_ctrl_comb_func().fu", "((((uint64_t)(lsu_ctrl_comb_func().fu");
    }
    auto removeOneBitMasks = [&](const std::string& token) {
        if (line.find(token) == std::string::npos || line.find(" & ((1ull << 1) - 1ull))") == std::string::npos) {
            return;
        }
        for (size_t amp = 0; (amp = line.find(" & ((1ull << 1) - 1ull))", amp)) != std::string::npos;) {
            if (amp == 0 || line[amp - 1] != ')') {
                amp += 12;
                continue;
            }
            int depth = 0;
            size_t lhsStart = std::string::npos;
            for (size_t i = amp; i > 0; --i) {
                char c = line[i - 1];
                if (c == ')') {
                    ++depth;
                }
                else if (c == '(') {
                    --depth;
                    if (depth == 0) {
                        lhsStart = i - 1;
                        break;
                    }
                }
            }
            if (lhsStart == std::string::npos) {
                amp += 12;
                continue;
            }
            while (lhsStart > 0 && line[lhsStart - 1] == '(') {
                --lhsStart;
            }
            auto close = line.find(" - 1ull))", amp);
            if (close == std::string::npos) {
                amp += 12;
                continue;
            }
            auto maskEnd = close + std::string(" - 1ull))").size();
            auto lhs = line.substr(lhsStart, amp - lhsStart);
            if (lhs.find(token) == std::string::npos) {
                amp += 12;
                continue;
            }
            if (lhs.rfind("((((uint64_t)(", 0) != 0 && lhs.rfind("(((((uint64_t)(", 0) != 0) {
                amp += 12;
                continue;
            }
            if (lhs.rfind("(((((uint64_t)(", 0) == 0) {
                lhs.erase(lhs.begin());
            }
            line.replace(lhsStart, maskEnd - lhsStart, lhs);
            amp = lhsStart + lhs.size();
        }
    };
    if (auto tokens = std::getenv("HDLCPP_REMOVE_ONE_BIT_MASK_TOKENS")) {
        std::stringstream ss(tokens);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = trim(token);
            if (!token.empty()) {
                removeOneBitMasks(token);
            }
        }
    }
    for (size_t pos = 0; (pos = line.find("_in()[", pos)) != std::string::npos;) {
        auto nameStart = pos;
        while (nameStart > 0 && (std::isalnum(static_cast<unsigned char>(line[nameStart - 1])) || line[nameStart - 1] == '_')) {
            --nameStart;
        }
        line.insert(nameStart, "(");
        line.insert(pos + 5, ")");
        pos += 7;
    }
    auto replacePackedInputElementBits = [&](const std::string& port, const std::string& elemWidth) {
        std::string needle = "logic<1>((" + port + "_in())[";
        for (size_t pos = 0; (pos = line.find(needle, pos)) != std::string::npos;) {
            auto idxStart = pos + needle.size();
            int depth = 0;
            size_t idxEnd = std::string::npos;
            for (size_t i = idxStart; i < line.size(); ++i) {
                char c = line[i];
                if (c == '(' || c == '[' || c == '{') {
                    ++depth;
                }
                else if (c == ')' || c == ']' || c == '}') {
                    if (depth == 0) {
                        idxEnd = i;
                        break;
                    }
                    --depth;
                }
            }
            if (idxEnd == std::string::npos || line.compare(idxEnd, 8, "]).bits(") != 0) {
                pos = idxStart;
                continue;
            }
            auto argStart = idxEnd + 8;
            depth = 0;
            size_t argEnd = std::string::npos;
            for (size_t i = argStart; i < line.size(); ++i) {
                char c = line[i];
                if (c == '(' || c == '[' || c == '{') {
                    ++depth;
                }
                else if (c == ')' || c == ']' || c == '}') {
                    if (depth == 0) {
                        argEnd = i;
                        break;
                    }
                    --depth;
                }
            }
            if (argEnd == std::string::npos) {
                pos = idxStart;
                continue;
            }
            auto idx = line.substr(idxStart, idxEnd - idxStart);
            auto args = line.substr(argStart, argEnd - argStart);
            auto elem = port + "_in().bits((((uint64_t)(" + idx + ") + 1)*" + elemWidth + ") - 1, ((uint64_t)(" + idx + "))*" + elemWidth + ").bits(" + args + ")";
            line.replace(pos, argEnd + 1 - pos, elem);
            pos += elem.size();
        }
    };
    if (auto width = std::getenv("HDLCPP_PACKED_INPUT_ELEMENT_WIDTH")) {
        std::stringstream ss(width);
        std::string item;
        while (std::getline(ss, item, ';')) {
            auto eq = item.find('=');
            if (eq != std::string::npos) {
                replacePackedInputElementBits(trim(item.substr(0, eq)), trim(item.substr(eq + 1)));
            }
        }
    }
    if (auto pos = line.find(".saturation_counter = {"); pos != std::string::npos) {
        auto brace = line.find('{', pos);
        int depth = 0;
        for (size_t i = brace; i < line.size(); ++i) {
            if (line[i] == '{') {
                ++depth;
            }
            else if (line[i] == '}' && --depth == 0) {
                line.replace(brace, i - brace + 1, "0");
                break;
            }
        }
    }
    for (size_t open = 0; (open = line.find('[', open)) != std::string::npos;) {
        int depth = 1;
        size_t colon = std::string::npos;
        size_t close = open + 1;
        for (; close < line.size(); ++close) {
            if (line[close] == '[' || line[close] == '(' || line[close] == '{') {
                ++depth;
            }
            else if (line[close] == ']' || line[close] == ')' || line[close] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            else if (line[close] == ':' && depth == 1 &&
                     (close == 0 || line[close - 1] != ':') &&
                     (close + 1 >= line.size() || line[close + 1] != ':')) {
                colon = close;
            }
        }
        if (close >= line.size() || colon == std::string::npos) {
            open = open + 1;
            continue;
        }
        auto baseStart = open;
        while (baseStart > 0) {
            auto c = line[baseStart - 1];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == ',' || c == '?' ||
                c == ':' || c == '<' || c == '>' || c == '&' || c == '|' || c == '^') {
                break;
            }
            --baseStart;
        }
        auto base = trim(line.substr(baseStart, open - baseStart));
        if (base.empty()) {
            open = close + 1;
            continue;
        }
        auto left = trim(line.substr(open + 1, colon - open - 1));
        auto right = trim(line.substr(colon + 1, close - colon - 1));
        auto repl = base + ".bits(" + left + "," + right + ")";
        line.replace(baseStart, close - baseStart + 1, repl);
        open = baseStart + repl.size();
    }
    for (size_t pos = 0; (pos = line.find(".bits(", pos)) != std::string::npos;) {
        auto argsStart = pos + 6;
        int depth = 1;
        size_t comma = std::string::npos;
        size_t close = argsStart;
        for (; close < line.size(); ++close) {
            auto c = line[close];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            else if (c == ',' && depth == 1) {
                comma = close;
            }
        }
        if (close >= line.size() || comma == std::string::npos) {
            break;
        }
        auto left = trim(line.substr(argsStart, comma - argsStart));
        auto width = trim(line.substr(comma + 1, close - comma - 1));
        if (!left.empty() && left.back() == '+') {
            left.pop_back();
            left = trim(left);
            auto repl = ".bits((" + left + ")+(" + width + ")-1," + left + ")";
            line.replace(pos, close - pos + 1, repl);
            pos += repl.size();
        }
        else {
            pos = close + 1;
        }
    }
    line = numericizeSelectedArithmetic(line);
    auto trimmedForResult = trim(line);
    if (trimmedForResult.rfind("case ((uint64_t)(", 0) == 0) {
        auto mask = line.find(") & ((1ull << ");
        if (mask != std::string::npos) {
            auto colon = line.rfind(':');
            if (colon != std::string::npos && colon > mask) {
                line.replace(mask, colon - mask, "))");
            }
        }
    }
    replaceAll(line, "case ((uint64_t)(\"zeros\")):", "case 0:");
    replaceAll(line, "case ((uint64_t)(\"ones\")):", "case 1:");
    replaceAll(line, "case ((uint64_t)(\"random\")):", "case 2:");
    replaceAll(line, "$urandom()", "random()");
    auto aggregateConstexpr = trim(line);
    if (aggregateConstexpr.rfind("static constexpr", 0) == 0 && aggregateConstexpr.find(" = { .") != std::string::npos) {
        replaceAll(line, " } };", " };");
    }
    if (trimmedForResult.rfind("result = ", 0) == 0 && trimmedForResult.find("logic<") != std::string::npos &&
        trimmedForResult.back() == ';') {
        auto indent = line.substr(0, line.find_first_not_of(" \t"));
        auto rhs = trim(trimmedForResult.substr(9, trimmedForResult.size() - 10));
        line = indent + "result = (uint64_t)(" + rhs + ");";
    }
    auto control = trim(line);
    if ((control.rfind("if (", 0) == 0 || control.rfind("else if (", 0) == 0 ||
         control.rfind("while (", 0) == 0 || control.rfind("for (", 0) == 0 ||
         control.rfind("switch (", 0) == 0) && !control.empty() && control.back() == '{') {
        auto brace = line.rfind('{');
        int balance = 0;
        for (size_t i = 0; i < brace; ++i) {
            if (line[i] == '(') {
                ++balance;
            }
            else if (line[i] == ')') {
                --balance;
            }
        }
        if (balance > 0) {
            line.insert(brace, std::string(static_cast<size_t>(balance), ')'));
        }
    }
    control = trim(line);
    if (control.rfind("if (", 0) == 0 || control.rfind("else if (", 0) == 0 ||
        control.rfind("while (", 0) == 0 || control.rfind("for (", 0) == 0 ||
        control.rfind("switch (", 0) == 0 || control.rfind("case ", 0) == 0) {
        return line;
    }

    auto findAssignmentEq = [](const std::string& text) {
        int depth = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}') {
                if (depth > 0) {
                    --depth;
                }
            }
            else if (c == '=' && depth == 0 &&
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
	    auto lhs = line.substr(0, eq + 1);
	    auto rhs = line.substr(eq + 1);
	    auto lhsTrim = trim(lhs.substr(0, lhs.size() - 1));
	    const bool packedStorageBitsAssign = lhsTrim.find(".data.bits(") != std::string::npos;

	    auto unwrapTypedTargetConstructors = [&](std::string text) {
        auto target = "std::remove_cvref_t<decltype(" + lhsTrim + ")>";
        bool changed = false;
        for (size_t pos = 0; (pos = text.find(target + "(", pos)) != std::string::npos; ) {
            auto innerStart = pos + target.size() + 1;
            auto rest = trim(text.substr(innerStart));
            if (rest.rfind("logic<", 0) != 0 && rest.rfind("u<", 0) != 0 && rest.rfind("array<", 0) != 0) {
                pos = innerStart;
                continue;
            }
            int depth = 1;
            size_t close = std::string::npos;
            for (size_t i = innerStart; i < text.size(); ++i) {
                char c = text[i];
                if (c == '(' || c == '[' || c == '{') {
                    ++depth;
                }
                else if (c == ')' || c == ']' || c == '}') {
                    --depth;
                    if (depth == 0) {
                        close = i;
                        break;
                    }
                }
            }
            if (close == std::string::npos) {
                break;
            }
            auto inner = text.substr(innerStart, close - innerStart);
            text.replace(pos, close - pos + 1, inner);
            pos += inner.size();
            changed = true;
        }
        return std::pair<std::string, bool>{text, changed};
    };
    if (auto cleaned = unwrapTypedTargetConstructors(rhs); cleaned.second) {
        rhs = cleaned.first;
        line = lhs + rhs;
    }
    auto valueInitTarget = "std::remove_cvref_t<decltype(" + lhsTrim + ")>{}";
    auto rhsNoSemi = trim(rhs);
    if (!rhsNoSemi.empty() && rhsNoSemi.back() == ';') {
        rhsNoSemi = trim(rhsNoSemi.substr(0, rhsNoSemi.size() - 1));
    }
    auto firstBracket = lhsTrim.find('[');
    if (firstBracket != std::string::npos && lhsTrim.substr(0, firstBracket).find('.') == std::string::npos &&
        rhsNoSemi == valueInitTarget) {
        rhs = " 0;";
        line = lhs + rhs;
    }

    auto findTopLevelTernary = [&](const std::string& text, size_t& qpos, size_t& cpos) {
        int depth = 0;
        qpos = std::string::npos;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}') {
                if (depth > 0) {
                    --depth;
                }
            }
            else if (c == '?' && depth == 0) {
                qpos = i;
                break;
            }
        }
        if (qpos == std::string::npos) {
            return false;
        }
        depth = 0;
        for (size_t i = qpos + 1; i < text.size(); ++i) {
            char c = text[i];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}') {
                if (depth > 0) {
                    --depth;
                }
            }
            else if (c == ':' && depth == 0 &&
                     (i == 0 || text[i - 1] != ':') &&
                     (i + 1 >= text.size() || text[i + 1] != ':')) {
                cpos = i;
                return true;
            }
        }
        return false;
    };

    auto rhsTrim = trim(rhs);
	    if (!packedStorageBitsAssign &&
	        rhsTrim.find("std::remove_cvref_t<decltype") == std::string::npos &&
	        rhsTrim.find("cpphdl::value_type_for_ref_t<decltype") == std::string::npos &&
	        !lhsTrim.empty() && lhsTrim.rfind("static constexpr", 0) != 0 &&
	        !rhsTrim.empty() && rhsTrim.back() == ';') {
        size_t qpos = std::string::npos;
        size_t cpos = std::string::npos;
        auto expr = trim(rhsTrim.substr(0, rhsTrim.size() - 1));
        if (findTopLevelTernary(expr, qpos, cpos)) {
            auto pred = trim(expr.substr(0, qpos));
            auto left = trim(expr.substr(qpos + 1, cpos - qpos - 1));
            auto right = trim(expr.substr(cpos + 1));
            auto target = "cpphdl::value_type_for_ref_t<decltype(" + lhsTrim + ")>";
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
            if (auto explicitType = leadingTemplateCallType(left); !explicitType.empty()) {
                target = explicitType;
            }
            auto bitsPos = lhsTrim.rfind(".bits(");
            if (bitsPos != std::string::npos) {
                auto argsStart = bitsPos + 6;
                int depth = 1;
                size_t comma = std::string::npos;
                size_t close = std::string::npos;
                for (size_t i = argsStart; i < lhsTrim.size(); ++i) {
                    char c = lhsTrim[i];
                    if (c == '(' || c == '[' || c == '{') {
                        ++depth;
                    }
                    else if (c == ')' || c == ']' || c == '}') {
                        --depth;
                        if (depth == 0) {
                            close = i;
                            break;
                        }
                    }
                    else if (c == ',' && depth == 1) {
                        comma = i;
                    }
                }
                if (comma != std::string::npos && close != std::string::npos) {
                    auto last = trim(lhsTrim.substr(argsStart, comma - argsStart));
                    auto first = trim(lhsTrim.substr(comma + 1, close - comma - 1));
                    auto topLevelPlusRangeWidth = [](const std::string& value) -> std::string {
                        auto v = trim(value);
                        size_t plus = std::string::npos;
                        int depth = 0;
                        for (size_t i = 0; i < v.size(); ++i) {
                            char c = v[i];
                            if (c == '(' || c == '[' || c == '{') {
                                ++depth;
                            }
                            else if (c == ')' || c == ']' || c == '}') {
                                if (depth > 0) {
                                    --depth;
                                }
                            }
                            else if (c == '+' && depth == 0) {
                                plus = i;
                            }
                        }
                        if (plus == std::string::npos) {
                            return {};
                        }
                        auto rhs = trim(v.substr(plus + 1));
                        depth = 0;
                        for (size_t i = rhs.size(); i-- > 0;) {
                            char c = rhs[i];
                            if (c == ')' || c == ']' || c == '}') {
                                ++depth;
                            }
                            else if (c == '(' || c == '[' || c == '{') {
                                if (depth > 0) {
                                    --depth;
                                }
                            }
                            else if (c == '-' && depth == 0 && trim(rhs.substr(i + 1)) == "1") {
                                return trim(rhs.substr(0, i));
                            }
                            if (i == 0) {
                                break;
                            }
                        }
                        return {};
                    };
                    if (trim(last) == trim(first)) {
                        target = "logic<1>";
                    }
                    else if (auto rangeWidth = topLevelPlusRangeWidth(last); !rangeWidth.empty()) {
                        target = "logic<" + rangeWidth + ">";
                    }
                    else {
                        target = "logic<((uint64_t)(" + last + "))-((uint64_t)(" + first + "))+1>";
                    }
                }
            }
            else if (lhsTrim.find('[') != std::string::npos &&
                (lhsTrim.find("][") != std::string::npos ||
                 (left.find("logic<1>") != std::string::npos && right.find("logic<1>") != std::string::npos))) {
                target = "logic<1>";
            }
            auto wrapBranch = [&](const std::string& branch) {
                if (isNumericValueType(target)) {
                    return target + "(" + branch + ")";
                }
                return "cpphdl::sv_cast<" + target + ">(" + branch + ")";
            };
	            line = lhs + " " + pred + " ? " + wrapBranch(left) + " : " + wrapBranch(right) + ";";
	            replaceAll(line, " )", ")");
	            balanceCompletedLogicStatement();
	            balanceReplicationAssignments();
	            replaceAll(line, "; }()))," , "; }()),");
	            replaceAll(line, "; }()))}", "; }())}");
	            return line;
	        }
	    }

    // Struct-field assignments are plain C++ value updates. Wrapping every dotted LHS
    // in _ASSIGN turns these into lambda assignments and breaks packed/local structs.
	line = lhs + rhs;
	balanceCompletedLogicStatement();
	balanceReplicationAssignments();
	replaceAll(line, "; }()))," , "; }()),");
	replaceAll(line, "; }()))}", "; }())}");
	return line;
}

static std::string exprText(const std::string& in)
{
    std::string s;
    bool comment = false;
    for (size_t i = 0; i < in.size(); ++i) {
        if (!comment && i + 1 < in.size() && in[i] == '/' && in[i + 1] == '/') {
            comment = true;
            ++i;
            continue;
        }
        if (comment && in[i] == '\n') {
            comment = false;
        }
        if (!comment) {
            s += in[i];
        }
    }
    auto rewritePowerOfTwo = [](std::string text) {
        auto isIdent = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '$' || c == '.';
        };
        for (size_t pos = 0; pos < text.size();) {
            auto two = text.find('2', pos);
            if (two == std::string::npos) {
                break;
            }
            if ((two > 0 && (std::isalnum(static_cast<unsigned char>(text[two - 1])) || text[two - 1] == '_')) ||
                (two + 1 < text.size() && (std::isalnum(static_cast<unsigned char>(text[two + 1])) || text[two + 1] == '_'))) {
                pos = two + 1;
                continue;
            }
            auto stars = two + 1;
            while (stars < text.size() && std::isspace(static_cast<unsigned char>(text[stars]))) {
                ++stars;
            }
            if (stars + 1 >= text.size() || text[stars] != '*' || text[stars + 1] != '*') {
                pos = two + 1;
                continue;
            }
            auto expBegin = stars + 2;
            while (expBegin < text.size() && std::isspace(static_cast<unsigned char>(text[expBegin]))) {
                ++expBegin;
            }
            if (expBegin >= text.size()) {
                break;
            }
            size_t expEnd = expBegin;
            if (text[expBegin] == '(') {
                int depth = 0;
                for (; expEnd < text.size(); ++expEnd) {
                    if (text[expEnd] == '(') {
                        ++depth;
                    }
                    else if (text[expEnd] == ')') {
                        --depth;
                        if (depth == 0) {
                            ++expEnd;
                            break;
                        }
                    }
                }
            }
            else {
                while (expEnd < text.size() && isIdent(text[expEnd])) {
                    ++expEnd;
                }
                if (expEnd < text.size() && text[expEnd] == '(') {
                    int depth = 0;
                    for (; expEnd < text.size(); ++expEnd) {
                        if (text[expEnd] == '(') {
                            ++depth;
                        }
                        else if (text[expEnd] == ')') {
                            --depth;
                            if (depth == 0) {
                                ++expEnd;
                                break;
                            }
                        }
                    }
                }
            }
            if (expEnd <= expBegin) {
                pos = stars + 2;
                continue;
            }
            auto exponent = trim(text.substr(expBegin, expEnd - expBegin));
            text.replace(two, expEnd - two, "(1ull << (unsigned)(" + exponent + "))");
            pos = two + std::string("(1ull << (unsigned)(").size() + exponent.size() + 2;
        }
        return text;
    };
    replaceAll(s, "\n", " ");
    replaceAll(s, "\t", " ");
    s = normalizeSvLiterals(s);
    replaceAll(s, "'h", "0x");
    replaceAll(s, "'d", "");
    replaceAll(s, "'b", "0b");
    replaceAll(s, "'0", "0");
    replaceAll(s, "'1", "1");
    replaceAll(s, "$clog2", "clog2");
    replaceAll(s, "$random", "random");
    replaceAll(s, "$isunknown", "cpphdl::sv_isunknown");
    replaceAll(s, "$signed", "");
    replaceAll(s, "$unsigned", "");
    s = rewritePowerOfTwo(std::move(s));
    std::string compact;
    bool space = false;
    for (auto c : s) {
        if (std::isspace((unsigned char)c)) {
            space = true;
        }
        else {
            if (space && !compact.empty()) {
                compact += ' ';
            }
            compact += c;
            space = false;
        }
    }
    return trim(compact);
}

static bool parseCppIntegralLiteral(std::string s, uint64_t& out)
{
    s = trim(std::move(s));
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = trim(s.substr(1, s.size() - 2));
    }
    while (!s.empty() && (s.back() == 'u' || s.back() == 'U' || s.back() == 'l' || s.back() == 'L')) {
        s.pop_back();
    }
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    if (s.empty()) {
        return false;
    }

    int base = 10;
    size_t start = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        base = 2;
        start = 2;
    }
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        start = 2;
    }

    uint64_t value = 0;
    for (size_t i = start; i < s.size(); ++i) {
        auto c = s[i];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = unsigned(c - '0');
        }
        else if (c >= 'a' && c <= 'f') {
            digit = unsigned(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F') {
            digit = unsigned(c - 'A' + 10);
        }
        else {
            return false;
        }
        if (digit >= unsigned(base)) {
            return false;
        }
        value = value * unsigned(base) + digit;
    }
    out = value;
    return true;
}

static std::string replaceAssignmentPatternFields(std::string s)
{
    for (size_t pos = 0; pos < s.size();) {
        if (!(std::isalpha(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
            ++pos;
            continue;
        }
        auto start = pos++;
        while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
            ++pos;
        }
        auto end = pos;
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
        if (pos < s.size() && s[pos] == ':' && (pos + 1 >= s.size() || s[pos + 1] != ':')) {
            auto prev = start;
            while (prev > 0 && std::isspace(static_cast<unsigned char>(s[prev - 1]))) {
                --prev;
            }
            char beforeField = prev == 0 ? '\0' : s[prev - 1];
            if (beforeField == '/' && prev >= 2 && s[prev - 2] == '*') {
                auto commentStart = s.rfind("/*", prev - 2);
                if (commentStart != std::string::npos) {
                    prev = commentStart;
                    while (prev > 0 && std::isspace(static_cast<unsigned char>(s[prev - 1]))) {
                        --prev;
                    }
                    beforeField = prev == 0 ? '\0' : s[prev - 1];
                }
            }
            if (beforeField == '{' || beforeField == ',') {
                auto replacementName = cppIdent(s.substr(start, end - start));
                s.insert(start, ".");
                ++pos;
                if (replacementName.size() != end - start) {
                    s.replace(start + 1, end - start, replacementName);
                    auto delta = replacementName.size() - (end - start);
                    pos += delta;
                }
                s.replace(pos, 1, " =");
                pos += 2;
            }
            else {
                ++pos;
            }
        }
        else {
            pos = end;
        }
    }
    return s;
}

static std::string replaceAssignmentPatternFieldConcats(std::string s)
{
    for (size_t pos = 0; (pos = s.find("= {", pos)) != std::string::npos;) {
        auto fieldPos = pos;
        while (fieldPos > 0 && std::isspace(static_cast<unsigned char>(s[fieldPos - 1]))) {
            --fieldPos;
        }
        auto nameStart = fieldPos;
        while (nameStart > 0 &&
               (std::isalnum(static_cast<unsigned char>(s[nameStart - 1])) || s[nameStart - 1] == '_')) {
            --nameStart;
        }
        if (nameStart == 0 || s[nameStart - 1] != '.') {
            pos += 3;
            continue;
        }
        auto open = s.find('{', pos);
        if (open == std::string::npos) {
            break;
        }
        int depth = 0;
        size_t close = std::string::npos;
        bool hasTopComma = false;
        bool hasDesignatedEntry = false;
        for (size_t i = open; i < s.size(); ++i) {
            char c = s[i];
            if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ')' || c == ']') {
                --depth;
                if (depth == 0 && c == '}') {
                    close = i;
                    break;
                }
            }
            else if (depth == 1 && c == ',') {
                hasTopComma = true;
            }
            else if (depth == 1 && c == '.') {
                hasDesignatedEntry = true;
            }
        }
        if (close == std::string::npos) {
            break;
        }
        auto inner = s.substr(open + 1, close - open - 1);
        auto enumLikeList = [&]() {
            auto items = splitTopLevelArgs(inner);
            if (items.empty()) {
                return false;
            }
            for (auto item : items) {
                item = trim(item);
                if (item.empty()) {
                    return false;
                }
                for (char c : item) {
                    if (!(std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) ||
                          c == '_' || c == ':')) {
                        return false;
                    }
                }
            }
            return true;
        };
        if (!hasTopComma || hasDesignatedEntry || inner.find("default =") != std::string::npos || enumLikeList()) {
            pos = close + 1;
            continue;
        }
        s.replace(open, 1, "cat{");
        pos = close + 3;
    }
    return s;
}

static bool replicationNeedsCaptureText(const std::string& text)
{
    static const std::set<std::string> safe = {
        "alignas", "alignof", "auto", "bool", "cat", "const", "constexpr", "cpphdl",
        "__cpphdl_i", "__cpphdl_rep", "b0", "b1", "decltype", "false", "logic", "long",
        "remove_cvref_t", "signed", "size_t", "static_cast", "std", "true",
        "type_width", "uint64_t", "ull", "unsigned"
    };
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
        auto ident = text.substr(start, pos - start);
        if (safe.count(ident) != 0) {
            continue;
        }
        if (ident.size() > 1 && ident.front() == 'b' &&
            std::all_of(ident.begin() + 1, ident.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            continue;
        }
        bool allUpper = true;
        bool hasAlpha = false;
        for (char c : ident) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                hasAlpha = true;
                if (!std::isupper(static_cast<unsigned char>(c))) {
                    allUpper = false;
                    break;
                }
            }
        }
        if (hasAlpha && allUpper) {
            continue;
        }
        auto next = pos;
        while (next < text.size() && std::isspace(static_cast<unsigned char>(text[next]))) {
            ++next;
        }
        if (next + 1 < text.size() && text[next] == ':' && text[next + 1] == ':') {
            continue;
        }
        if (start >= 2 && text[start - 1] == ':' && text[start - 2] == ':') {
            continue;
        }
        if (std::isupper(static_cast<unsigned char>(ident.front()))) {
            continue;
        }
        return true;
    }
    return false;
}

static std::string replaceTextReplications(std::string s)
{
    auto templateArgAfter = [](const std::string& text, size_t namePos, const std::string& name) -> std::string {
        auto open = namePos + name.size();
        auto close = matchingTemplateCloseLocal(text, open);
        if (close == std::string::npos) {
            return "";
        }
        return trim(text.substr(open + 1, close - open - 1));
    };
    auto firstTemplateArg = [&](const std::string& text, const std::string& name) -> std::string {
        for (size_t namePos = 0; (namePos = text.find(name, namePos)) != std::string::npos; ++namePos) {
            if (namePos > 0) {
                auto prev = text[namePos - 1];
                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
                    continue;
                }
            }
            auto arg = templateArgAfter(text, namePos, name);
            if (!arg.empty()) {
                return arg;
            }
        }
        return "";
    };
    auto replicatedWidth = [&](const std::string& expr) {
        std::string width = "64";
        auto exprTrim = trim(expr);
        if (exprTrim.rfind("logic<", 0) == 0) {
            auto close = matchingTemplateCloseLocal(exprTrim, std::string("logic").size());
            if (close != std::string::npos) {
                width = exprTrim.substr(std::string("logic<").size(), close - std::string("logic<").size());
            }
        }
        else if (auto packedWidth = firstTemplateArg(exprTrim, "cpphdl::pack_value"); !packedWidth.empty()) {
            width = packedWidth;
        }
        else if (auto packedWidth = firstTemplateArg(exprTrim, "pack_value"); !packedWidth.empty()) {
            width = packedWidth;
        }
        else if (exprTrim.rfind("cat{", 0) == 0) {
            if (auto logicWidth = firstTemplateArg(exprTrim, "logic"); !logicWidth.empty()) {
                width = logicWidth;
            }
            if (auto packedWidth = firstTemplateArg(exprTrim, "cpphdl::pack_value"); !packedWidth.empty()) {
                width = packedWidth;
            }
            else if (auto packedWidth = firstTemplateArg(exprTrim, "pack_value"); !packedWidth.empty()) {
                width = packedWidth;
            }
        }
        return width;
    };
    auto replicationReplacement = [&](const std::string& count, const std::string& expr) {
        auto width = replicatedWidth(expr);
        bool needsCapture = replicationNeedsCaptureText(count) || replicationNeedsCaptureText(expr);
        if (!needsCapture) {
            return "cpphdl::repeat<(std::size_t)(" + count + "), (std::size_t)(" + width + ")>(logic<" + width + ">(" + expr + "))";
        }
        std::string capture = "[&]";
        return std::string("(") + capture + "() { logic<((uint64_t)(" + count + ") * (uint64_t)(" + width + "))> __cpphdl_rep{}; "
            "for (std::size_t __cpphdl_i = 0; __cpphdl_i < (std::size_t)(" + count + "); ++__cpphdl_i) { "
            "__cpphdl_rep.bits((__cpphdl_i + 1) * (std::size_t)(" + width + ") - 1, __cpphdl_i * (std::size_t)(" + width + ")) = logic<" + width + ">(" + expr + "); "
            "} return __cpphdl_rep; }())";
    };
    auto topLevelBalancedClose = [](const std::string& text, size_t open) -> size_t {
        if (open >= text.size() || text[open] != '{') {
            return std::string::npos;
        }
        int depth = 1;
        int paren = 0;
        int bracket = 0;
        int angle = 0;
        for (size_t i = open + 1; i < text.size(); ++i) {
            char c = text[i];
            if (c == '(') ++paren;
            else if (c == ')' && paren > 0) --paren;
            else if (c == '[') ++bracket;
            else if (c == ']' && bracket > 0) --bracket;
            else if (c == '<' && i + 1 < text.size() && text[i + 1] == '<') ++i;
            else if (c == '<') ++angle;
            else if (c == '>' && angle > 0) --angle;
            else if (paren == 0 && bracket == 0 && angle == 0) {
                if (c == '{') ++depth;
                else if (c == '}') {
                    --depth;
                    if (depth == 0) {
                        return i;
                    }
                }
            }
        }
        return std::string::npos;
    };
    for (size_t pos = 0; (pos = s.find('{', pos)) != std::string::npos;) {
        auto countClose = topLevelBalancedClose(s, pos);
        if (countClose == std::string::npos || countClose + 1 >= s.size() || s[countClose + 1] != '{') {
            ++pos;
            continue;
        }
        auto count = trim(s.substr(pos + 1, countClose - pos - 1));
        if (count.empty() || count.find(',') != std::string::npos || count.find(':') != std::string::npos) {
            ++pos;
            continue;
        }
        auto exprOpen = countClose + 1;
        auto exprClose = topLevelBalancedClose(s, exprOpen);
        if (exprClose == std::string::npos) {
            ++pos;
            continue;
        }
        auto expr = trim(s.substr(exprOpen + 1, exprClose - exprOpen - 1));
        if (expr.empty()) {
            ++pos;
            continue;
        }
        auto replacement = replicationReplacement(count, expr);
        s.replace(pos, exprClose - pos + 1, replacement);
        pos += replacement.size();
    }
    for (size_t pos = 0; (pos = s.find('{', pos)) != std::string::npos;) {
        int paren = 0;
        int bracket = 0;
        int angle = 0;
        size_t innerOpen = std::string::npos;
        bool invalidCount = false;
        for (size_t i = pos + 1; i < s.size(); ++i) {
            char c = s[i];
            if (c == '(') ++paren;
            else if (c == ')' && paren > 0) --paren;
            else if (c == '[') ++bracket;
            else if (c == ']' && bracket > 0) --bracket;
            else if (c == '<' && i + 1 < s.size() && s[i + 1] == '<') ++i;
            else if (c == '<') ++angle;
            else if (c == '>' && angle > 0) --angle;
            else if ((c == ',' || c == '=' || c == ':') && paren == 0 && bracket == 0 && angle == 0) {
                invalidCount = true;
                break;
            }
            else if (c == '{' && paren == 0 && bracket == 0 && angle == 0) {
                innerOpen = i;
                break;
            }
            else if (c == '}' && paren == 0 && bracket == 0 && angle == 0) {
                invalidCount = true;
                break;
            }
        }
        if (invalidCount || innerOpen == std::string::npos) {
            ++pos;
            continue;
        }
        auto count = trim(s.substr(pos + 1, innerOpen - pos - 1));
        if (count.empty()) {
            ++pos;
            continue;
        }
        int depth = 1;
        size_t innerClose = std::string::npos;
        for (size_t i = innerOpen + 1; i < s.size(); ++i) {
            if (s[i] == '{') ++depth;
            else if (s[i] == '}') {
                --depth;
                if (depth == 0) {
                    innerClose = i;
                    break;
                }
            }
        }
        if (innerClose == std::string::npos || innerClose + 1 >= s.size() || s[innerClose + 1] != '}') {
            ++pos;
            continue;
        }
        auto expr = trim(s.substr(innerOpen + 1, innerClose - innerOpen - 1));
        auto replacement = replicationReplacement(count, expr);
        s.replace(pos, innerClose + 2 - pos, replacement);
        pos += replacement.size();
    }
    return s;
}

static std::string removeAssignmentPatternDefault(std::string s)
{
    for (size_t pos = 0; (pos = s.find(".default_", pos)) != std::string::npos;) {
        auto entryStart = pos;
        while (entryStart > 0 && std::isspace(static_cast<unsigned char>(s[entryStart - 1]))) {
            --entryStart;
        }
        if (entryStart == 0 || (s[entryStart - 1] != '{' && s[entryStart - 1] != ',')) {
            pos += 9;
            continue;
        }

        auto eq = s.find('=', pos + 9);
        if (eq == std::string::npos) {
            break;
        }

        int depth = 0;
        size_t end = eq + 1;
        for (; end < s.size(); ++end) {
            auto c = s[end];
            if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ')' || c == ']') {
                if (depth == 0) {
                    break;
                }
                --depth;
            }
            else if (c == ',' && depth == 0) {
                break;
            }
        }

        size_t eraseStart = entryStart;
        size_t eraseEnd = end;
        if (entryStart > 0 && s[entryStart - 1] == ',') {
            eraseStart = entryStart - 1;
        }
        else if (eraseEnd < s.size() && s[eraseEnd] == ',') {
            ++eraseEnd;
            while (eraseEnd < s.size() && std::isspace(static_cast<unsigned char>(s[eraseEnd]))) {
                ++eraseEnd;
            }
        }
        s.erase(eraseStart, eraseEnd - eraseStart);
        pos = eraseStart;
    }
    return s;
}

static std::string replaceDefaultOnlyAssignmentPattern(std::string s)
{
    for (size_t pos = 0; (pos = s.find("{default", pos)) != std::string::npos;) {
        auto keyEnd = pos + 8;
        while (keyEnd < s.size() && std::isspace(static_cast<unsigned char>(s[keyEnd]))) {
            ++keyEnd;
        }
        if (keyEnd >= s.size() || s[keyEnd] != ':') {
            pos += 8;
            continue;
        }
        auto valueStart = keyEnd + 1;
        int depth = 0;
        size_t valueEnd = valueStart;
        for (; valueEnd < s.size(); ++valueEnd) {
            auto c = s[valueEnd];
            if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ')' || c == ']') {
                if (depth == 0) {
                    break;
                }
                --depth;
            }
        }
        if (valueEnd >= s.size() || s[valueEnd] != '}') {
            pos += 8;
            continue;
        }
        auto value = trim(s.substr(valueStart, valueEnd - valueStart));
        if (value.empty()) {
            pos += 8;
            continue;
        }
        s.replace(pos, valueEnd - pos + 1, value);
        pos += value.size();
    }
    return s;
}

static std::string replaceStreamingConcats(std::string s)
{
    for (size_t pos = 0; (pos = s.find("{<<8{", pos)) != std::string::npos;) {
        auto innerStart = pos + 5;
        int depth = 1;
        size_t innerEnd = innerStart;
        for (; innerEnd < s.size(); ++innerEnd) {
            auto c = s[innerEnd];
            if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ')' || c == ']') {
                --depth;
                if (depth == 0 && c == '}') {
                    break;
                }
            }
        }
        if (innerEnd >= s.size() || innerEnd + 1 >= s.size() || s[innerEnd + 1] != '}') {
            pos += 5;
            continue;
        }
        auto inner = trim(s.substr(innerStart, innerEnd - innerStart));
        auto repl = "cpphdl::byteswap(" + inner + ")";
        s.replace(pos, innerEnd + 2 - pos, repl);
        pos += repl.size();
    }
    return s;
}

static std::string replaceGenericSvCasts(std::string s)
{
    auto castNameLooksLikeType = [](const std::string& name) {
        if (name.find("::") != std::string::npos || (name.size() >= 2 && name.substr(name.size() - 2) == "_t")) {
            return true;
        }
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        auto lastSegment = lower;
        auto sep = lastSegment.rfind('_');
        if (sep != std::string::npos) {
            lastSegment = lastSegment.substr(sep + 1);
        }
        for (auto prefix : {std::string("uint"), std::string("int")}) {
            if (lastSegment.rfind(prefix, 0) != 0 || lastSegment.size() == prefix.size()) {
                continue;
            }
            if (std::all_of(lastSegment.begin() + static_cast<std::ptrdiff_t>(prefix.size()), lastSegment.end(), [](unsigned char ch) {
                    return std::isdigit(ch);
                })) {
                return true;
            }
        }
        return false;
    };
    for (size_t pos = 0; pos < s.size();) {
        if (std::isdigit(static_cast<unsigned char>(s[pos]))) {
            auto start = pos++;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                ++pos;
            }
            if (pos + 1 < s.size() && s[pos] == '\'' && s[pos + 1] == '(') {
                auto width = s.substr(start, pos - start);
                s.replace(start, pos + 2 - start, "logic<" + width + ">(");
                pos = start + 7 + width.size();
            }
            continue;
        }
        if (s[pos] == '(') {
            int depth = 0;
            size_t close = std::string::npos;
            for (size_t i = pos; i < s.size(); ++i) {
                if (s[i] == '(') {
                    ++depth;
                }
                else if (s[i] == ')' && --depth == 0) {
                    close = i;
                    break;
                }
            }
            if (close != std::string::npos && close + 2 < s.size() &&
                s[close + 1] == '\'' && s[close + 2] == '(') {
                auto name = trim(s.substr(pos + 1, close - pos - 1));
                if (!name.empty()) {
                    auto repl = castNameLooksLikeType(name) ? ("sv_cast<" + name + ">(") : ("logic<" + name + ">(");
                    s.replace(pos, close + 3 - pos, repl);
                    pos += repl.size();
                    continue;
                }
            }
        }
        if (!(std::isalpha(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
            ++pos;
            continue;
        }
        auto start = pos++;
        while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
            ++pos;
        }
        while (pos + 1 < s.size() && s[pos] == ':' && s[pos + 1] == ':' &&
               pos + 2 < s.size() &&
               (std::isalpha(static_cast<unsigned char>(s[pos + 2])) || s[pos + 2] == '_')) {
            pos += 2;
            while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
                ++pos;
            }
        }
        auto name = s.substr(start, pos - start);
        if (pos + 1 < s.size() && s[pos] == '\'' && s[pos + 1] == '(') {
            auto repl = castNameLooksLikeType(name) ? ("sv_cast<" + name + ">(") : ("logic<" + name + ">(");
            s.replace(start, pos + 2 - start, repl);
            pos = start + repl.size();
        }
    }
    return s;
}

static std::string replacePowerOps(std::string s)
{
    for (size_t pos = 0; (pos = s.find("**", pos)) != std::string::npos;) {
        auto leftEnd = pos;
        auto leftStart = leftEnd;
        while (leftStart > 0 && std::isspace(static_cast<unsigned char>(s[leftStart - 1]))) {
            --leftStart;
        }
        auto tokenEnd = leftStart;
        while (leftStart > 0 && (std::isalnum(static_cast<unsigned char>(s[leftStart - 1])) || s[leftStart - 1] == '_')) {
            --leftStart;
        }
        auto left = trim(s.substr(leftStart, tokenEnd - leftStart));
        auto rightStart = pos + 2;
        while (rightStart < s.size() && std::isspace(static_cast<unsigned char>(s[rightStart]))) {
            ++rightStart;
        }
        auto rightEnd = rightStart;
        while (rightEnd < s.size() && (std::isalnum(static_cast<unsigned char>(s[rightEnd])) || s[rightEnd] == '_')) {
            ++rightEnd;
        }
        auto right = trim(s.substr(rightStart, rightEnd - rightStart));
        if (left == "2" && !right.empty()) {
            auto repl = "(1ull << " + right + ")";
            s.replace(leftStart, rightEnd - leftStart, repl);
            pos = leftStart + repl.size();
        }
        else {
            pos += 2;
        }
    }
    return s;
}

static bool isExprBoundary(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == '?' || c == ':' ||
           c == '{' || c == '}' || c == ';' || c == '=' || c == '&' || c == '|' || c == '^' ||
           c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
}

static size_t matchingOpenBefore(const std::string& s, size_t close, char openCh, char closeCh)
{
    int depth = 0;
    for (size_t i = close + 1; i-- > 0;) {
        if (s[i] == closeCh) {
            ++depth;
        }
        else if (s[i] == openCh) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
        if (i == 0) {
            break;
        }
    }
    return std::string::npos;
}

static size_t selectedExprStartBefore(const std::string& s, size_t endExclusive)
{
    if (endExclusive == 0) {
        return std::string::npos;
    }
    size_t end = endExclusive - 1;
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end]))) {
        --end;
    }
    size_t selectOpen = std::string::npos;
    if (s[end] == ']') {
        selectOpen = matchingOpenBefore(s, end, '[', ']');
    }
    else if (s[end] == ')') {
        auto open = matchingOpenBefore(s, end, '(', ')');
        if (open != std::string::npos && open >= 5 && s.compare(open - 5, 5, ".bits") == 0) {
            selectOpen = open - 5;
        }
    }
    if (selectOpen == std::string::npos) {
        return std::string::npos;
    }
    size_t start = selectOpen;
    while (start > 0 && !isExprBoundary(s[start - 1]) && s[start - 1] != '(') {
        --start;
    }
    if (start >= 11 && s.compare(start - 11, 11, "(uint64_t)(") == 0) {
        return std::string::npos;
    }
    return start;
}

static std::string numericizeSelectedArithmetic(std::string s)
{
    for (size_t op = 0; op < s.size(); ++op) {
        if (s[op] != '*' && s[op] != '/' && s[op] != '%' && s[op] != '+' && s[op] != '-') {
            continue;
        }
        if ((s[op] == '+' || s[op] == '-') && op + 1 < s.size() && s[op + 1] == s[op]) {
            continue;
        }
        auto start = selectedExprStartBefore(s, op);
        if (start == std::string::npos) {
            continue;
        }
        auto operand = trim(s.substr(start, op - start));
        if (operand.empty()) {
            continue;
        }
        auto repl = "(uint64_t)(" + operand + ")";
        s.replace(start, op - start, repl);
        op = start + repl.size();
    }
    return s;
}

static std::string replaceRawRangeSelects(std::string s)
{
    for (size_t pos = 0; (pos = s.find('[', pos)) != std::string::npos;) {
        int depth = 0;
        size_t close = std::string::npos;
        size_t sep = std::string::npos;
        for (size_t i = pos + 1; i < s.size(); ++i) {
            auto c = s[i];
            if (c == '(' || c == '[' || c == '{') {
                ++depth;
            }
            else if (c == ')' || c == ']' || c == '}') {
                if (depth == 0) {
                    close = i;
                    break;
                }
                --depth;
            }
            else if (depth == 0 && c == ',') {
                sep = i;
            }
            else if (depth == 0 && c == ':' &&
                     (i == 0 || s[i - 1] != ':') &&
                     (i + 1 >= s.size() || s[i + 1] != ':')) {
                sep = i;
            }
        }
        if (close == std::string::npos || sep == std::string::npos || sep > close) {
            ++pos;
            continue;
        }
        auto left = trim(s.substr(pos + 1, sep - pos - 1));
        auto right = trim(s.substr(sep + 1, close - sep - 1));
        if (left.empty() || right.empty()) {
            ++pos;
            continue;
        }
        s.replace(pos, close - pos + 1, ".bits(" + left + "," + right + ")");
        pos += left.size() + right.size() + 8;
    }
    return numericizeSelectedArithmetic(s);
}

static std::string repairDottedLogicWidthCasts(std::string s)
{
    for (size_t pos = 0; (pos = s.find(".logic<", pos)) != std::string::npos;) {
        size_t objEnd = pos;
        size_t objStart = objEnd;
        while (objStart > 0 && (std::isalnum(static_cast<unsigned char>(s[objStart - 1])) || s[objStart - 1] == '_' || s[objStart - 1] == ':')) {
            --objStart;
        }
        if (objStart == objEnd) {
            pos += 7;
            continue;
        }
        size_t widthStart = pos + 7;
        size_t widthEnd = s.find('>', widthStart);
        if (widthEnd == std::string::npos || widthEnd + 1 >= s.size() || s[widthEnd + 1] != '(') {
            pos += 7;
            continue;
        }
        auto object = s.substr(objStart, objEnd - objStart);
        auto width = s.substr(widthStart, widthEnd - widthStart);
        bool simpleWidth = !width.empty();
        for (char c : width) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':')) {
                simpleWidth = false;
                break;
            }
        }
        if (!simpleWidth) {
            pos = widthEnd + 1;
            continue;
        }
        auto repl = "logic<" + object + "." + width + ">";
        s.replace(objStart, widthEnd + 1 - objStart, repl);
        pos = objStart + repl.size();
    }
    return s;
}

static std::string replaceSvCastBraced(std::string s)
{
    for (size_t pos = 0; (pos = s.find("sv_cast<", pos)) != std::string::npos;) {
        auto typeStart = pos + 8;
        int angleDepth = 1;
        size_t typeEnd = typeStart;
        for (; typeEnd < s.size(); ++typeEnd) {
            if (s[typeEnd] == '<') {
                ++angleDepth;
            }
            else if (s[typeEnd] == '>' && --angleDepth == 0) {
                break;
            }
        }
        if (typeEnd >= s.size() || typeEnd + 2 >= s.size() || s[typeEnd + 1] != '(' || s[typeEnd + 2] != '{') {
            pos += 8;
            continue;
        }
        auto type = s.substr(typeStart, typeEnd - typeStart);
        s.replace(pos, typeEnd + 1 - pos, type);
        pos += type.size();
    }
    return s;
}

static std::string replaceKeywordMemberAccess(std::string s)
{
    for (size_t pos = 0; (pos = s.find('.', pos)) != std::string::npos;) {
        auto start = pos + 1;
        if (start >= s.size() || !(std::isalpha(static_cast<unsigned char>(s[start])) || s[start] == '_')) {
            ++pos;
            continue;
        }
        auto end = start + 1;
        while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_')) {
            ++end;
        }
        auto field = s.substr(start, end - start);
        auto replacement = cppIdent(field);
        if (replacement != field) {
            s.replace(start, field.size(), replacement);
            pos = start + replacement.size();
        }
        else {
            pos = end;
        }
    }
    return s;
}

static std::string replaceMultipleConcatPattern(std::string s)
{
    for (size_t pos = 0; (pos = s.find("{", pos)) != std::string::npos;) {
        auto innerOpen = s.find('{', pos + 1);
        auto innerClose = innerOpen == std::string::npos ? std::string::npos : s.find('}', innerOpen + 1);
        auto outerClose = innerClose == std::string::npos ? std::string::npos : s.find('}', innerClose + 1);
        if (innerOpen == std::string::npos || innerClose == std::string::npos || outerClose == std::string::npos) {
            ++pos;
            continue;
        }
        auto count = trim(s.substr(pos + 1, innerOpen - pos - 1));
        auto value = trim(s.substr(innerOpen + 1, innerClose - innerOpen - 1));
        auto simpleToken = [](const std::string& text) {
            return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            });
        };
        if (simpleToken(count) && !value.empty() &&
            value.find(',') == std::string::npos && value.find(':') == std::string::npos &&
            value.find('.') == std::string::npos) {
            s.replace(pos, outerClose - pos + 1, value);
            pos += value.size();
        }
        else {
            ++pos;
        }
    }
    return s;
}

static std::string replaceLogicBraceCasts(std::string s)
{
    for (size_t pos = 0; (pos = s.find("logic<", pos)) != std::string::npos;) {
        auto gt = s.find('>', pos + 6);
        if (gt == std::string::npos || gt + 2 >= s.size() || s[gt + 1] != '(' || s[gt + 2] != '{') {
            pos += 6;
            continue;
        }
        int depth = 0;
        size_t end = std::string::npos;
        for (size_t i = gt + 1; i < s.size(); ++i) {
            if (s[i] == '(' || s[i] == '{') {
                ++depth;
            }
            else if (s[i] == ')' || s[i] == '}') {
                --depth;
                if (depth == 0 && s[i] == ')') {
                    end = i;
                    break;
                }
            }
        }
        if (end == std::string::npos) {
            pos += 6;
            continue;
        }
        s.replace(pos, end - pos + 1, "0");
        ++pos;
    }
    return s;
}

static std::string replaceInsideOps(std::string s)
{
    auto pos = s.find(" inside ");
    if (pos == std::string::npos) {
        return s;
    }
    auto lhs = trim(s.substr(0, pos));
    auto open = s.find('{', pos);
    auto close = open == std::string::npos ? std::string::npos : s.find('}', open + 1);
    if (lhs.empty() || open == std::string::npos || close == std::string::npos) {
        return s;
    }
    auto list = s.substr(open + 1, close - open - 1);
    std::string out;
    std::string item;
    std::stringstream ss(list);
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " || ";
        }
        if (item.size() >= 2 && item.front() == '[' && item.back() == ']') {
            auto range = item.substr(1, item.size() - 2);
            auto colon = range.find(':');
            if (colon != std::string::npos) {
                auto lo = trim(range.substr(0, colon));
                auto hi = trim(range.substr(colon + 1));
                out += "((" + lhs + " >= " + lo + ") && (" + lhs + " <= " + hi + "))";
            }
            else {
                out += "(" + lhs + " == " + trim(range) + ")";
            }
        }
        else {
            out += "(" + lhs + " == " + item + ")";
        }
    }
    return out.empty() ? s : "(" + out + ")";
}

static size_t braceElementCount(std::string s)
{
    s = trim(std::move(s));
    if (s.size() < 2 || s.front() != '{' || s.back() != '}') {
        return 0;
    }
    int depth = 0;
    size_t count = 1;
    bool hasContent = false;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        auto c = s[i];
        if (!std::isspace(static_cast<unsigned char>(c))) {
            hasContent = true;
        }
        if (c == '{' || c == '(' || c == '[') {
            ++depth;
        }
        else if (c == '}' || c == ')' || c == ']') {
            --depth;
        }
        else if (c == ',' && depth == 0) {
            ++count;
        }
    }
    return hasContent ? count : 0;
}


static bool parseNamedAggregateEntries(const std::string& s, std::vector<std::pair<std::string, std::string>>& entries)
{
    auto text = trim(s);
    if (text.size() < 2 || text.front() != '{' || text.back() != '}') {
        return false;
    }
    size_t pos = 1;
    while (pos + 1 < text.size()) {
        while (pos + 1 < text.size() && (std::isspace(static_cast<unsigned char>(text[pos])) || text[pos] == ',')) {
            ++pos;
        }
        if (pos + 1 >= text.size()) {
            break;
        }
        if (text[pos] != '.') {
            return false;
        }
        ++pos;
        auto nameStart = pos;
        while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
            ++pos;
        }
        if (pos == nameStart) {
            return false;
        }
        auto name = text.substr(nameStart, pos - nameStart);
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != '=') {
            return false;
        }
        ++pos;
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        auto valueStart = pos;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (; pos < text.size(); ++pos) {
            auto c = text[pos];
            if (inString) {
                if (escaped) {
                    escaped = false;
                }
                else if (c == '\\') {
                    escaped = true;
                }
                else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
                escaped = false;
            }
            else if (c == '{' || c == '(' || c == '[') {
                ++depth;
            }
            else if (c == '<') {
                char prev = pos == 0 ? '\0' : text[pos - 1];
                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == '>') {
                    ++depth;
                }
            }
            else if (c == '}' || c == ')' || c == ']' || c == '>') {
                if (depth == 0) {
                    break;
                }
                --depth;
            }
            else if (c == ',' && depth == 0) {
                break;
            }
        }
        entries.push_back({name, trim(text.substr(valueStart, pos - valueStart))});
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return !entries.empty();
}

static std::string wrapLogicCastsForConstexprNumeric(std::string s)
{
    for (size_t pos = 0; (pos = s.find("logic<", pos)) != std::string::npos;) {
        if (pos >= 10 && s.compare(pos - 10, 10, "(uint64_t)") == 0) {
            pos += 6;
            continue;
        }
        auto widthStart = pos + 6;
        auto widthEnd = s.find('>', widthStart);
        if (widthEnd == std::string::npos || widthEnd + 1 >= s.size() || s[widthEnd + 1] != '(') {
            pos += 6;
            continue;
        }
        auto valueStart = widthEnd + 2;
        int depth = 1;
        size_t valueEnd = valueStart;
        for (; valueEnd < s.size(); ++valueEnd) {
            if (s[valueEnd] == '(') {
                ++depth;
            }
            else if (s[valueEnd] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (valueEnd >= s.size()) {
            break;
        }
        auto original = s.substr(pos, valueEnd - pos + 1);
        auto replacement = "(uint64_t)(" + original + ")";
        s.replace(pos, original.size(), replacement);
        pos += replacement.size();
    }
    return s;
}

static std::string namedAggregateToConstexprLambda(const std::string& type, const std::string& init, bool capture = false)
{
    std::vector<std::pair<std::string, std::string>> entries;
    if (!parseNamedAggregateEntries(init, entries)) {
        return init;
    }
    std::string out = std::string(capture ? "[&]" : "[]") + " { " + type + " v{};";
    for (auto& [name, value] : entries) {
        auto aggregateDefaults = configuredTextMap("HDLCPP_AGGREGATE_DEFAULTS");
        auto defaultIt = aggregateDefaults.find(type + "." + name);
        if (defaultIt != aggregateDefaults.end() && (value == "0" || value == "{}")) {
            value = defaultIt->second;
        }
        if (value.empty()) {
            value = "{}";
        }
        if (trim(value).rfind("{.", 0) == 0 || trim(value).rfind("{ .", 0) == 0) {
            value = namedAggregateToConstexprLambda("std::remove_cvref_t<decltype(v." + name + ")>", value, capture);
        }
        auto isZeroExpression = [](std::string text) {
            text = stripBalancedOuterParens(trim(std::move(text)));
            bool changed = true;
            while (changed) {
                changed = false;
                text = stripBalancedOuterParens(trim(std::move(text)));
                if (text.rfind("logic<", 0) == 0) {
                    auto closeType = matchingTemplateCloseLocal(text, std::string("logic").size());
                    if (closeType != std::string::npos && closeType + 1 < text.size() && text[closeType + 1] == '(') {
                        int depth = 1;
                        size_t closeValue = std::string::npos;
                        for (size_t i = closeType + 2; i < text.size(); ++i) {
                            if (text[i] == '(') ++depth;
                            else if (text[i] == ')') {
                                --depth;
                                if (depth == 0) {
                                    closeValue = i;
                                    break;
                                }
                            }
                        }
                        if (closeValue != std::string::npos && trim(text.substr(closeValue + 1)).empty()) {
                            text = text.substr(closeType + 2, closeValue - closeType - 2);
                            changed = true;
                        }
                    }
                }
            }
            uint64_t literal = 0;
            return parseCppIntegralLiteral(text, literal) && literal == 0;
        };
        auto isZeroReplicationLambda = [&](const std::string& text) {
            if (text.find("__cpphdl_rep") == std::string::npos) {
                return false;
            }
            bool sawAssign = false;
            for (size_t pos = 0; (pos = text.find("__cpphdl_rep.bits", pos)) != std::string::npos;) {
                auto eq = text.find('=', pos);
                auto semi = eq == std::string::npos ? std::string::npos : text.find(';', eq + 1);
                if (eq == std::string::npos || semi == std::string::npos) {
                    return false;
                }
                sawAssign = true;
                if (!isZeroExpression(text.substr(eq + 1, semi - eq - 1))) {
                    return false;
                }
                pos = semi + 1;
            }
            return sawAssign;
        };
        const auto fieldTypeDefault = "std::remove_cvref_t<decltype(v." + name + ")>{}";
        if (isZeroReplicationLambda(value)) {
            value = fieldTypeDefault;
        }
        if (value == fieldTypeDefault) {
            out += " v." + name + " = " + value + ";";
        }
        else if (trim(value).rfind("{", 0) != 0) {
            out += " [&] { auto __cpphdl_field_value = " + value + ";";
            out += " auto __cpphdl_assign_field = [](auto& __cpphdl_dst, const auto& __cpphdl_value) {";
            out += " if constexpr (requires { __cpphdl_dst = __cpphdl_value; }) {";
            out += " __cpphdl_dst = __cpphdl_value;";
            out += " } else { sv_assign_field(__cpphdl_dst, __cpphdl_value); } };";
            out += " __cpphdl_assign_field(v." + name + ", __cpphdl_field_value); }();";
        }
        else {
            out += " sv_assign_field(v." + name + ", " + value + ");";
        }
    }
    out += " return v; }()";
    return out;
}

static std::string cppExprText(std::string s)
{
    s = exprText(s);
    s = replaceStreamingConcats(std::move(s));
    s = replacePowerOps(std::move(s));
    bool hasAssignmentPattern = s.find("'{") != std::string::npos;
    replaceAll(s, "'{", "{");
    replaceAll(s, "0b?", "0b0");
    if (hasAssignmentPattern) {
        s = replaceDefaultOnlyAssignmentPattern(std::move(s));
        s = replaceAssignmentPatternFields(s);
        s = replaceAssignmentPatternFieldConcats(std::move(s));
        s = removeAssignmentPatternDefault(std::move(s));
    }
    s = replaceTextReplications(std::move(s));
    applyConfiguredLinePatches(s);
    repairPatchedConcatOperandWidths(s);
    replaceAll(s, "unsigned'(", "(");
    replaceAll(s, "signed'(", "(");
    replaceAll(s, "int'(", "(");
    replaceAll(s, "longint'(", "(");
    replaceAll(s, "bit'(", "(");
    replaceAll(s, "logic'(", "(");
    replaceAll(s, "fp_format_e'(", "static_cast<fp_format_e>(");
    replaceAll(s, "int_format_e'(", "static_cast<int_format_e>(");
    replaceAll(s, "opgroup_e'(", "static_cast<opgroup_e>(");
    replaceAll(s, "operation_e'(", "static_cast<operation_e>(");
    replaceAll(s, "roundmode_e'(", "static_cast<roundmode_e>(");
    s = replaceGenericSvCasts(std::move(s));
    s = repairDottedLogicWidthCasts(std::move(s));
    s = replaceSvCastBraced(std::move(s));
    s = replaceRawRangeSelects(std::move(s));
    s = replaceMultipleConcatPattern(std::move(s));
    s = replaceInsideOps(std::move(s));
    auto qualifiedCalls = configuredTextMap("HDLCPP_QUALIFIED_CALLS");
    for (const auto& [from, to] : qualifiedCalls) {
        replaceAll(s, from + "(", to + "(");
    }
    replaceAll(s, "idx_width(", "cf_math_pkg::idx_width(");
    replaceAll(s, "cf_math_pkg::cf_math_pkg::idx_width(", "cf_math_pkg::idx_width(");
    replaceAll(s, ".UnitTypes = {{}, {}, {}, {}}", ".UnitTypes = {}");
    s = replaceKeywordMemberAccess(std::move(s));
    return s;
}

static std::string stripLogicLiteralCasts(std::string s)
{
    for (size_t pos = 0; (pos = s.find("logic<", pos)) != std::string::npos;) {
        auto widthStart = pos + 6;
        auto widthEnd = s.find('>', widthStart);
        if (widthEnd == std::string::npos || widthEnd + 1 >= s.size() || s[widthEnd + 1] != '(') {
            pos += 6;
            continue;
        }
        auto width = s.substr(widthStart, widthEnd - widthStart);
        if (!isNumber(width)) {
            pos = widthEnd + 1;
            continue;
        }
        auto valueStart = widthEnd + 2;
        int depth = 1;
        size_t valueEnd = valueStart;
        for (; valueEnd < s.size(); ++valueEnd) {
            if (s[valueEnd] == '(') {
                ++depth;
            }
            else if (s[valueEnd] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (valueEnd >= s.size()) {
            break;
        }
        auto value = s.substr(valueStart, valueEnd - valueStart);
        s.replace(pos, valueEnd - pos + 1, "(" + value + ")");
        pos += value.size() + 2;
    }
    return s;
}

static std::vector<std::string> memoryArgs(const std::string& type);

static bool isPrimitiveWrapperType(const std::string& type)
{
    auto t = trim(type);
    if (t.rfind("reg<", 0) == 0 && t.back() == '>') {
        t = t.substr(4, t.size() - 5);
    }
    return t == "u8" || t == "u16" || t == "u32" || t == "u64" ||
           t == "unsigned" || t == "uint8_t" || t == "uint16_t" ||
           t == "uint32_t" || t == "uint64_t";
}

static bool isNumericValueType(const std::string& type)
{
    auto t = trim(type);
    if (t.rfind("reg<", 0) == 0 && t.back() == '>') {
        t = t.substr(4, t.size() - 5);
    }
    return t == "bool" || t.rfind("logic<", 0) == 0 || t.rfind("u<", 0) == 0 ||
           isPrimitiveWrapperType(t);
}

static std::string constexprType(std::string type)
{
    if (type == "u32" || type == "u16" || type == "u8" || type == "bool") {
        return "unsigned";
    }
    if (type == "u64") {
        return "uint64_t";
    }
    if (type == "string") {
        return "std::string";
    }
    if (type.rfind("logic<", 0) == 0 || type.rfind("u<", 0) == 0) {
        return "uint64_t";
    }
    if (type.rfind("array<", 0) == 0) {
        auto args = memoryArgs("memory<" + type.substr(6, type.size() - 7) + ">");
        if (args.size() >= 2) {
            return "std::array<" + constexprType(args[0]) + "," + args[1] + ">";
        }
        type.replace(0, 5, "std::array");
        return type;
    }
    return type;
}

static size_t findRangeColon(const std::string& range)
{
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    int ternary = 0;
    for (size_t i = 0; i < range.size(); ++i) {
        char c = range[i];
        if (c == '(') ++paren;
        else if (c == ')' && paren > 0) --paren;
        else if (c == '[') ++bracket;
        else if (c == ']' && bracket > 0) --bracket;
        else if (c == '{') ++brace;
        else if (c == '}' && brace > 0) --brace;
        else if (paren == 0 && bracket == 0 && brace == 0) {
            if (c == '?') {
                ++ternary;
            }
            else if (c == ':') {
                if ((i + 1 < range.size() && range[i + 1] == ':') ||
                    (i > 0 && range[i - 1] == ':')) {
                    continue;
                }
                if (ternary > 0) --ternary;
                else return i;
            }
        }
    }
    return std::string::npos;
}

static std::string textRangeWidth(std::string range)
{
    range = trim(range);
    if (range.size() >= 2 && range.front() == '[' && range.back() == ']') {
        range = range.substr(1, range.size() - 2);
    }
    auto colon = findRangeColon(range);
    if (colon == std::string::npos) {
        return cppExprText(range);
    }
    auto left = trim(range.substr(0, colon));
    auto right = trim(range.substr(colon + 1));
    auto compactLeft = left;
    auto compactRight = right;
    compactLeft.erase(std::remove_if(compactLeft.begin(), compactLeft.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), compactLeft.end());
    compactRight.erase(std::remove_if(compactRight.begin(), compactRight.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), compactRight.end());
    if ((compactRight == "0" || compactRight == "0x0") && compactLeft.size() > 2 && compactLeft.substr(compactLeft.size() - 2) == "-1") {
        return trim(left.substr(0, left.rfind('-')));
    }
    if ((compactLeft == "0" || compactLeft == "0x0") && compactRight.size() > 2 && compactRight.substr(compactRight.size() - 2) == "-1") {
        return trim(right.substr(0, right.rfind('-')));
    }
    auto l = cppExprText(left);
    auto r = cppExprText(right);
    return "(((uint64_t)(" + l + ") >= (uint64_t)(" + r + ") ? ((uint64_t)(" + l + ") - (uint64_t)(" + r + ")) : ((uint64_t)(" + r + ") - (uint64_t)(" + l + "))) + 1)";
}

static std::vector<std::string> bracketWidths(std::string raw)
{
    std::vector<std::string> widths;
    for (auto bracket = raw.find('['); bracket != std::string::npos; bracket = raw.find('[', bracket + 1)) {
        auto end = raw.find(']', bracket);
        if (end == std::string::npos) {
            break;
        }
        widths.push_back(textRangeWidth(raw.substr(bracket, end - bracket + 1)));
        bracket = end;
    }
    return widths;
}

static std::string cppTypeFromSvText(std::string raw)
{
    raw = trim(exprText(raw));
    if (!raw.empty() && raw.front() == '=') {
        raw = trim(raw.substr(1));
    }
    if (raw.empty()) {
        return "logic<1>";
    }
    if (raw.find("struct packed") != std::string::npos ||
        raw.find("union packed") != std::string::npos) {
        return "bool";
    }
    if (raw == "logic" || raw == "bit") {
        return "logic<1>";
    }
    if (raw.rfind("logic ", 0) != 0 && raw.rfind("logic[", 0) != 0 &&
        raw.rfind("bit ", 0) != 0 && raw.rfind("bit[", 0) != 0) {
        return raw;
    }

    auto widths = bracketWidths(raw);
    if (widths.empty()) {
        return "logic<1>";
    }
    auto type = "logic<" + widths.back() + ">";
    for (auto i = widths.size() - 1; i-- > 0;) {
        type = "array<" + type + "," + widths[i] + ">";
    }
    return type;
}

static std::string constexprStructFieldType(std::string type)
{
    if ((type.rfind("logic<", 0) == 0 || type.rfind("u<", 0) == 0) && type.back() == '>') {
        return type;
    }
    return constexprType(std::move(type));
}

struct PackedFieldInfo {
    std::string name;
    std::string width;
    bool usePack = false;
};

static std::string joinedPackedWidth(const std::vector<PackedFieldInfo>& fields)
{
    if (fields.empty()) {
        return "";
    }
    auto width = fields.front().width;
    for (size_t i = 1; i < fields.size(); ++i) {
        width += " + " + fields[i].width;
    }
    return foldWidth(width);
}

static std::string packedUnionWidth(const std::vector<PackedFieldInfo>& fields)
{
    if (fields.empty()) {
        return "";
    }
    return fields.front().width;
}

static std::string addWidthExpr(const std::string& offset, const std::string& width)
{
    if (offset == "0") {
        return width;
    }
    return offset + " + " + width;
}

static std::string packedAggregateHelpers(const std::string& name, std::string width = "", const std::vector<PackedFieldInfo>& fields = {}, bool isUnion = false)
{
    if (width.empty() || fields.empty()) {
        width = "sizeof(" + name + ")*8";
    }
    std::string line;
    if (!fields.empty()) {
        line += "    static constexpr std::size_t _size_bits() { return " + width + "; }\n";
        line += "    template<std::size_t W> " + name + "& operator=(const logic<W>& v) { auto packed = logic<" + width + ">(v);\n";
        std::string offset = "0";
        for (auto& field : fields) {
            auto next = isUnion ? field.width : addWidthExpr(offset, field.width);
            line += "        if constexpr ((uint64_t)(" + field.width + ") != 0) {\n";
            line += "            this->" + field.name + " = cpphdl::unpack_value<std::remove_reference_t<decltype(this->" + field.name + ")>, " + field.width + ">(logic<" + field.width + ">(packed.bits((uint64_t)(" + next + " - 1),(uint64_t)(" + offset + "))));\n";
            line += "        }\n";
            if (!isUnion) {
                offset = next;
            }
        }
        line += "        return *this; }\n";
        line += "    template<typename T, typename std::enable_if_t<!std::is_integral_v<std::remove_cvref_t<T>> && !std::is_enum_v<std::remove_cvref_t<T>> && cpphdl::detail::has_pack_method<std::remove_cvref_t<T>>::value, int> = 0> " + name + "& operator=(const T& v) { return this->template operator=<" + width + ">(logic<" + width + ">(v.pack())); }\n";
        line += "    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0> " + name + "& operator=(T v) { return this->template operator=<" + width + ">(logic<" + width + ">(v)); }\n";
        line += "    logic<" + width + "> pack() const { logic<" + width + "> packed = 0;\n";
        offset = "0";
        for (size_t i = 0; i < fields.size(); ++i) {
            auto& field = fields[i];
            auto next = isUnion ? field.width : addWidthExpr(offset, field.width);
            line += "        if constexpr ((uint64_t)(" + field.width + ") != 0) {\n";
            if (isUnion) {
                line += "            if constexpr ((uint64_t)(" + field.width + ") <= (uint64_t)(" + width + ")) {\n";
                line += "                packed.bits((uint64_t)(" + next + " - 1),(uint64_t)(0)) = cpphdl::pack_value<" + field.width + ">(this->" + field.name + ");\n";
                line += "            }\n";
                line += "            return packed;\n";
            }
            else {
                line += "            packed.bits((uint64_t)(" + next + " - 1),(uint64_t)(" + offset + ")) = cpphdl::pack_value<" + field.width + ">(this->" + field.name + ");\n";
            }
            line += "        }\n";
            if (!isUnion) {
                offset = next;
            }
        }
        line += "        return packed; }\n";
        line += "    struct _bits_ref { " + name + "& owner; std::size_t last; std::size_t first;\n";
        line += "        template<typename V> _bits_ref& operator=(const V& v) { auto packed = owner.pack(); packed.bits(last, first) = v; owner.template operator=<" + width + ">(packed); return *this; }\n";
        line += "        template<std::size_t W> operator logic<W>() const { return logic<W>(owner.pack().bits(last, first)); }\n";
        line += "        explicit operator uint64_t() const { return (uint64_t)(owner.pack().bits(last, first)); }\n";
        line += "    };\n";
        line += "    struct _bit_ref { " + name + "& owner; std::size_t index;\n";
        line += "        template<typename V> _bit_ref& operator=(const V& v) { auto packed = owner.pack(); packed.bits(index, index) = logic<1>(v); owner.template operator=<" + width + ">(packed); return *this; }\n";
        line += "        operator logic<1>() const { return logic<1>((((uint64_t)(owner.pack())) >> (unsigned)(index)) & 1ull); }\n";
        line += "        explicit operator bool() const { return (bool)(logic<1>(*this)); }\n";
        line += "    };\n";
        line += "    _bits_ref bits(std::size_t last, std::size_t first) { return _bits_ref{*this, last, first}; }\n";
        line += "    auto bits(std::size_t last, std::size_t first) const { return pack().bits(last, first); }\n";
        line += "    _bit_ref operator[](std::size_t index) { return _bit_ref{*this, index}; }\n";
        line += "    auto operator[](std::size_t index) const { return logic<1>((((uint64_t)(pack())) >> (unsigned)(index)) & 1ull); }\n";
    }
    else {
        line += "    template<std::size_t W> " + name + "& operator=(const logic<W>& v) { (*(logic<" + width + ">*)this) = v; return *this; }\n";
        line += "    template<typename T, typename std::enable_if_t<!std::is_integral_v<std::remove_cvref_t<T>> && !std::is_enum_v<std::remove_cvref_t<T>> && cpphdl::detail::has_pack_method<std::remove_cvref_t<T>>::value, int> = 0> " + name + "& operator=(const T& v) { (*(logic<" + width + ">*)this) = logic<" + width + ">(v.pack()); return *this; }\n";
        line += "    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0> " + name + "& operator=(T v) { (*(logic<" + width + ">*)this) = logic<" + width + ">(v); return *this; }\n";
        line += "    auto bits(std::size_t last, std::size_t first) { return (*(logic<" + width + ">*)this).bits(last, first); }\n";
        line += "    auto bits(std::size_t last, std::size_t first) const { return const_cast<" + name + "*>(this)->bits(last, first); }\n";
    }
    if (!fields.empty()) {
        line += "    template<std::size_t W> auto operator|(const logic<W>& rhs) const { return pack() | rhs; }\n";
        line += "    template<std::size_t W> auto operator&(const logic<W>& rhs) const { return pack() & rhs; }\n";
        line += "    explicit operator uint64_t() const { return pack().to_ullong(); }\n";
    }
    else {
        line += "    template<std::size_t W> auto operator|(const logic<W>& rhs) const { return (*(const logic<" + width + ">*)this) | rhs; }\n";
        line += "    template<std::size_t W> auto operator&(const logic<W>& rhs) const { return (*(const logic<" + width + ">*)this) & rhs; }\n";
        line += "    explicit operator uint64_t() const { return ((const logic<" + width + ">&)*this).to_ullong(); }\n";
    }
    return line;
}

static std::string memoryDepth(const std::string& type)
{
    auto isMemory = type.rfind("memory<", 0) == 0;
    auto isArray = type.rfind("array<", 0) == 0;
    if ((!isMemory && !isArray) || type.empty() || type.back() != '>') {
        return "";
    }
    int nested = 0;
    std::string current;
    std::vector<std::string> args;
    auto start = isMemory ? 7u : 6u;
    for (size_t i = start; i + 1 < type.size(); ++i) {
        char c = type[i];
        if (c == '<') {
            ++nested;
        }
        else if (c == '>') {
            --nested;
        }
        if (c == ',' && nested == 0) {
            args.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    args.push_back(trim(current));
    if (isMemory) {
        return args.size() == 3 ? args[2] : "";
    }
    return args.size() >= 2 ? args[1] : "";
}

static std::vector<std::string> memoryArgs(const std::string& type)
{
    std::vector<std::string> args;
    if (type.rfind("memory<", 0) != 0 || type.empty() || type.back() != '>') {
        return args;
    }
    int nested = 0;
    std::string current;
    for (size_t i = 7; i + 1 < type.size(); ++i) {
        char c = type[i];
        char prev = i > 0 ? type[i - 1] : '\0';
        char next = i + 1 < type.size() ? type[i + 1] : '\0';
        if (c == '<' && prev != '<' && next != '<' && next != '=') {
            ++nested;
        }
        else if (c == '>' && prev != '>' && prev != '=' && next != '>' && next != '=') {
            --nested;
        }
        if (c == ',' && nested == 0) {
            args.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    args.push_back(trim(current));
    return args;
}

static std::vector<std::string> templateArgsFor(const std::string& type, const std::string& prefix)
{
    std::vector<std::string> args;
    if (type.rfind(prefix + "<", 0) != 0 || type.empty() || type.back() != '>') {
        return args;
    }
    int depth = 0;
    std::string current;
    auto start = prefix.size() + 1;
    for (size_t i = start; i + 1 < type.size(); ++i) {
        char c = type[i];
        char prev = i > 0 ? type[i - 1] : '\0';
        char next = i + 1 < type.size() ? type[i + 1] : '\0';
        if (c == '<' && prev != '<' && next != '<' && next != '=') {
            ++depth;
        }
        else if (c == '>' && prev != '>' && prev != '=' && next != '>' && next != '=') {
            --depth;
        }
        if (c == ',' && depth == 0) {
            args.push_back(trim(current));
            current.clear();
        }
        else {
            current += c;
        }
    }
    args.push_back(trim(current));
    return args;
}

static bool scalarMemory(const std::string& type)
{
    auto args = memoryArgs(type);
    return args.size() == 3 && trim(args[1]) == "1";
}

static bool memoryLikeType(const std::string& type)
{
    return type.rfind("memory<", 0) == 0 || type.rfind("array<", 0) == 0;
}

static bool scheduledMemoryType(const std::string& type)
{
    return type.rfind("memory<", 0) == 0;
}

static std::string logicWidth(const std::string& type)
{
    auto t = trim(type);
    if (t.rfind("reg<", 0) == 0 && t.back() == '>') {
        t = t.substr(4, t.size() - 5);
    }
    if (t.rfind("logic<", 0) == 0 && t.back() == '>') {
        return t.substr(6, t.size() - 7);
    }
    if (t.rfind("u", 0) == 0 && t.size() > 1 &&
        std::all_of(t.begin() + 1, t.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        return t.substr(1);
    }
    return "";
}

static std::string unwrapRegType(const std::string& type)
{
    auto t = trim(type);
    if (t.rfind("reg<", 0) == 0 && t.back() == '>') {
        return t.substr(4, t.size() - 5);
    }
    return t;
}

static std::string truthyExpr(const std::string& expr, const std::string& width)
{
    auto value = trim(expr);
    while (value.size() >= 2 && value.front() == '(' && value.back() == ')') {
        value = trim(value.substr(1, value.size() - 2));
    }
    auto parseLiteral = [](const std::string& text, uint64_t& out) {
        auto s = trim(text);
        if (s.empty()) {
            return false;
        }
        int base = 10;
        size_t start = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            start = 2;
        }
        else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            start = 2;
        }
        uint64_t result = 0;
        for (size_t i = start; i < s.size(); ++i) {
            auto c = s[i];
            if (c == '_') {
                continue;
            }
            unsigned digit = 0;
            if (c >= '0' && c <= '9') {
                digit = unsigned(c - '0');
            }
            else if (c >= 'a' && c <= 'f') {
                digit = unsigned(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F') {
                digit = unsigned(c - 'A' + 10);
            }
            else {
                return false;
            }
            if (digit >= unsigned(base)) {
                return false;
            }
            result = result * unsigned(base) + digit;
        }
        out = result;
        return true;
    };
    if (!width.empty() && std::all_of(width.begin(), width.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        auto w = std::stoul(width);
        uint64_t literal = 0;
        if (w > 0 && w < 64 && parseLiteral(value, literal)) {
            auto masked = literal & ((1ull << w) - 1ull);
            return masked ? "true" : "false";
        }
        if (w == 1) {
            return "((bool)(" + expr + "))";
        }
        if (w > 0 && w < 64) {
            return "(((uint64_t)(" + expr + ") & ((1ull << " + width + ") - 1ull)) != 0)";
        }
    }
    return "((bool)(" + expr + "))";
}

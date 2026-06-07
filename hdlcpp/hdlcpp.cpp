#include "Project.h"
#include "Module.h"
#include "Field.h"
#include "Method.h"
#include "Struct.h"
#include "Expr.h"
#include "Enum.h"

#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>

using namespace slang;
using namespace slang::syntax;

using cpphdl::Expr;
using cpphdl::Field;
using cpphdl::Method;
using cpphdl::Module;
using cpphdl::Project;
using cpphdl::Struct;

struct PortGen {
    std::string name;
    std::string type;
    std::string direction;
    std::string array;
    std::string init;
};

struct MethodGen {
    std::string name;
    std::string ret = "void";
    std::string args;
    std::vector<std::string> body;
    std::string returnName;
    std::string returnBase;
};

struct InstanceConnGen {
    std::string instance;
    std::string type;
    std::string port;
    std::string rhs;
    std::string lhs;
    bool connected = true;
};

struct ModuleGen {
    std::string name;
    bool isPackage = false;
    std::vector<std::string> params;
    std::vector<PortGen> ports;
    std::vector<std::string> typeDecls;
    std::vector<std::string> packageDecls;
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
    std::map<std::string, size_t> combMethodByBase;
    std::map<std::string, std::string> combReturnTypes;
    std::map<std::string, std::string> outputRegTypes;
    std::map<std::string, std::string> types;
    std::map<std::string, std::string> typeWidths;
    std::map<std::string, std::map<std::string, std::string>> typeFields;
    int alwaysNo = 0;
    bool hasWorkTask = false;
};

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

static bool memoryLikeType(const std::string& type);

static std::string regTypeFor(std::string type)
{
    if (type.rfind("reg<", 0) == 0 || type.rfind("memory<", 0) == 0) {
        return type;
    }
    if (type == "bool" || type == "u1") {
        return "reg<u1>";
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
           n == "aclk" || n == "clk_in" || n == "clock_in" ||
           n.rfind("clk_", 0) == 0 || n.rfind("clock_", 0) == 0 ||
           (n.size() > 4 && n.substr(n.size() - 4) == "_clk") ||
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

static void replaceAll(std::string& s, const std::string& from, const std::string& to);

static bool isZeroLiteralText(const std::string& expr)
{
    auto s = trim(expr);
    return s == "0" || s == "0b0" || s == "1'b0" || s == "logic<1>{}" || s == "false";
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
    auto key = std::string(envName);
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }
    std::map<std::string, std::string> out;
    if (auto* path = std::getenv(envName)) {
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
            out[trim(line.substr(0, sep))] = decodeConfiguredText(line.substr(sep + 1));
        }
    }
    cache[key] = out;
    return out;
}

static std::optional<uint64_t> parseConfiguredUint(std::string value)
{
    value = trim(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }
    if (value == "false") {
        return 0;
    }
    if (value == "true") {
        return 1;
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

static std::optional<bool> evalConfiguredGenerateCondition(const ModuleGen& m, std::string cond)
{
    auto values = configuredTextMap("HDLCPP_GENERATE_PARAM_VALUES");
    if (values.empty()) {
        return std::nullopt;
    }

    for (auto& item : values) {
        auto key = item.first;
        auto dot = key.find('.');
        if (dot != std::string::npos) {
            if (key.substr(0, dot) != m.name) {
                continue;
            }
            key = key.substr(dot + 1);
        }
        replaceAll(cond, "(uint64_t)(" + key + ")", item.second);
        replaceAll(cond, key, item.second);
    }

    cond = trim(cond);
    while (cond.size() >= 2 && cond.front() == '(' && cond.back() == ')') {
        cond = trim(cond.substr(1, cond.size() - 2));
    }
    if (cond.empty()) {
        return std::nullopt;
    }
    if (cond.front() == '!') {
        auto inner = evalConfiguredGenerateCondition(m, cond.substr(1));
        if (inner) {
            return !*inner;
        }
        return std::nullopt;
    }

    const char* ops[] = {"==", "!=", ">=", "<=", ">", "<"};
    for (auto* op : ops) {
        auto pos = cond.find(op);
        if (pos == std::string::npos) {
            continue;
        }
        auto lhs = parseConfiguredUint(cond.substr(0, pos));
        auto rhs = parseConfiguredUint(cond.substr(pos + std::strlen(op)));
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        std::string opText(op);
        if (opText == "==") return *lhs == *rhs;
        if (opText == "!=") return *lhs != *rhs;
        if (opText == ">=") return *lhs >= *rhs;
        if (opText == "<=") return *lhs <= *rhs;
        if (opText == ">") return *lhs > *rhs;
        if (opText == "<") return *lhs < *rhs;
    }

    auto value = parseConfiguredUint(cond);
    if (value) {
        return *value != 0;
    }
    return std::nullopt;
}

static std::vector<std::string> combDriversFor(const ModuleGen& m, const std::string& base)
{
    std::vector<std::string> drivers;
    auto addDriver = [&](const std::string& name) {
        if (!name.empty() && std::find(drivers.begin(), drivers.end(), name) == drivers.end()) {
            drivers.push_back(name);
        }
    };

    auto direct = m.wireMap.find(base);
    if (direct != m.wireMap.end()) {
        addDriver(direct->second);
    }

    auto storage = combStorageName(m, base);
    for (auto& method : m.methods) {
        if (method.name.find("_comb_func") == std::string::npos) {
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
            if (lhs == storage || lhsBase == storage || lhs == base || lhsBase == base) {
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
    return !knownFunctionReturnWidth(callee).empty();
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
                    auto width = s.substr(start, (i - digits.size() - 1) - start);
                    auto sq = width.find('\'');
                    if (sq != std::string::npos) {
                        width = width.substr(0, sq);
                    }
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

static std::string valueAssignCombFunctionPorts(std::string line)
{
    auto assign = line.find(" = _ASSIGN_COMB(");
    if (assign == std::string::npos) {
        return line;
    }
    auto argStart = assign + std::string(" = _ASSIGN_COMB(").size();
    auto argEnd = line.find(");", argStart);
    if (argEnd == std::string::npos) {
        return line;
    }
    auto arg = trim(line.substr(argStart, argEnd - argStart));
    if (hasSuffix(arg, "_comb_func()")) {
        line.replace(assign + 3, std::string("_ASSIGN_COMB").size(), "_ASSIGN");
    }
    return line;
}

static std::string postProcessCppLine(std::string line)
{
    line = valueAssignCombFunctionPorts(std::move(line));
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
    line = repairDottedLogicWidthCasts(std::move(line));
    applyConfiguredLinePatches(line);
    replaceAll(line, "empty_o_out = _ASSIGN( ~push_i_in() );", "empty_o_out = _ASSIGN( (DEPTH == 0) ? logic<1>(~push_i_in()) : logic<1>((status_cnt_q == 0) & ~(logic<1>(FALL_THROUGH) & push_i_in())) );");
    replaceAll(line, "full_o_out = _ASSIGN( ~pop_i_in() );", "full_o_out = _ASSIGN( (DEPTH == 0) ? logic<1>(~pop_i_in()) : logic<1>(status_cnt_q == FifoDepth) );");
    auto repairRuntimeLogicWidths = [&]() {
        for (size_t pos = 0; (pos = line.find("logic<", pos)) != std::string::npos;) {
            auto start = pos + 6;
            int depth = 1;
            size_t end = start;
            for (; end < line.size(); ++end) {
                if (line[end] == '<') {
                    ++depth;
                }
                else if (line[end] == '>') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (end >= line.size()) {
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
    removeTargetedMasks("issue_instr_i_in()");
    removeTargetedMasks("lsu_ctrl_comb_func().fu");
    applyConfiguredLinePatches(line);
    replaceAll(line, "(((((uint64_t)((issue_instr_i_in()", "((((uint64_t)((issue_instr_i_in()");
    replaceAll(line, "(((((uint64_t)(lsu_ctrl_comb_func().fu", "((((uint64_t)(lsu_ctrl_comb_func().fu");
    auto removeOneBitMasks = [&](const std::string& token) {
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
    if (rhsTrim.find("std::remove_cvref_t<decltype") == std::string::npos &&
        !lhsTrim.empty() && lhsTrim.rfind("static constexpr", 0) != 0 &&
        !rhsTrim.empty() && rhsTrim.back() == ';') {
        size_t qpos = std::string::npos;
        size_t cpos = std::string::npos;
        auto expr = trim(rhsTrim.substr(0, rhsTrim.size() - 1));
        if (findTopLevelTernary(expr, qpos, cpos)) {
            auto pred = trim(expr.substr(0, qpos));
            auto left = trim(expr.substr(qpos + 1, cpos - qpos - 1));
            auto right = trim(expr.substr(cpos + 1));
            auto target = "std::remove_cvref_t<decltype(" + lhsTrim + ")>";
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
            line = lhs + " " + pred + " ? " + target + "(" + left + ") : " + target + "(" + right + " );";
            replaceAll(line, " )", ")");
            return line;
        }
    }

    // Struct-field assignments are plain C++ value updates. Wrapping every dotted LHS
    // in _ASSIGN turns these into lambda assignments and breaks packed/local structs.
    return lhs + rhs;
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
    replaceAll(s, "$signed", "");
    replaceAll(s, "$unsigned", "");
    replaceAll(s, "2**", "1ull << ");
    replaceAll(s, "2 ** ", "1ull << ");
    replaceAll(s, "2 **", "1ull << ");
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
                    auto repl = "logic<" + name + ">(";
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
            auto repl = (name.find("::") != std::string::npos || (name.size() >= 2 && name.substr(name.size() - 2) == "_t")) ?
                ("sv_cast<" + name + ">(") : ("logic<" + name + ">(");
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
            else if (c == '{' || c == '(' || c == '[' || c == '<') {
                ++depth;
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

static std::string namedAggregateToConstexprLambda(const std::string& type, const std::string& init)
{
    std::vector<std::pair<std::string, std::string>> entries;
    if (!parseNamedAggregateEntries(init, entries)) {
        return init;
    }
    std::string out = "[] { " + type + " v{};";
    for (auto& [name, value] : entries) {
        auto aggregateDefaults = configuredTextMap("HDLCPP_AGGREGATE_DEFAULTS");
        auto defaultIt = aggregateDefaults.find(type + "." + name);
        if (defaultIt != aggregateDefaults.end() && (value == "0" || value == "{}")) {
            value = defaultIt->second;
        }
        if (value.empty()) {
            value = "{}";
        }
        out += " sv_assign_field(v." + name + ", " + value + ");";
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
        s = removeAssignmentPatternDefault(std::move(s));
    }
    applyConfiguredLinePatches(s);
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
    return type == "u8" || type == "u16" || type == "u32" || type == "u64" ||
           type == "unsigned" || type == "uint8_t" || type == "uint16_t" ||
           type == "uint32_t" || type == "uint64_t";
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
        if (args.size() == 2) {
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
    if (raw.empty() || raw.find("struct packed") != std::string::npos ||
        raw.find("union packed") != std::string::npos) {
        return "bool";
    }
    if (raw == "logic" || raw == "bit") {
        return "bool";
    }
    if (raw.rfind("logic ", 0) != 0 && raw.rfind("bit ", 0) != 0) {
        return raw;
    }

    auto widths = bracketWidths(raw);
    if (widths.empty()) {
        return "bool";
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

static std::string addWidthExpr(const std::string& offset, const std::string& width)
{
    if (offset == "0") {
        return width;
    }
    return offset + " + " + width;
}

static std::string packedAggregateHelpers(const std::string& name, std::string width = "", const std::vector<PackedFieldInfo>& fields = {})
{
    if (width.empty() || fields.empty()) {
        width = "sizeof(" + name + ")*8";
    }
    std::string line;
    if (!fields.empty()) {
        line += "    template<size_t W> " + name + "& operator=(const logic<W>& v) { auto packed = logic<" + width + ">(v);\n";
        std::string offset = "0";
        for (auto& field : fields) {
            auto next = addWidthExpr(offset, field.width);
            line += "        this->" + field.name + " = logic<" + field.width + ">(packed.bits((uint64_t)(" + next + " - 1),(uint64_t)(" + offset + ")));\n";
            offset = next;
        }
        line += "        return *this; }\n";
        line += "    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0> " + name + "& operator=(T v) { return (*this = logic<" + width + ">(v)); }\n";
        line += "    logic<" + width + "> pack() const { logic<" + width + "> packed = 0;\n";
        offset = "0";
        for (auto& field : fields) {
            auto next = addWidthExpr(offset, field.width);
            line += "        packed.bits((uint64_t)(" + next + " - 1),(uint64_t)(" + offset + ")) = logic<" + field.width + ">((uint64_t)(this->" + field.name + "));\n";
            offset = next;
        }
        line += "        return packed; }\n";
        line += "    auto bits(size_t last, size_t first) { return pack().bits(last, first); }\n";
    }
    else {
        line += "    template<size_t W> " + name + "& operator=(const logic<W>& v) { (*(logic<" + width + ">*)this) = v; return *this; }\n";
        line += "    template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0> " + name + "& operator=(T v) { (*(logic<" + width + ">*)this) = logic<" + width + ">(v); return *this; }\n";
        line += "    auto bits(size_t last, size_t first) { return (*(logic<" + width + ">*)this).bits(last, first); }\n";
    }
    line += "    auto bits(size_t last, size_t first) const { return const_cast<" + name + "*>(this)->bits(last, first); }\n";
    if (!fields.empty()) {
        line += "    template<size_t W> auto operator|(const logic<W>& rhs) const { return pack() | rhs; }\n";
        line += "    template<size_t W> auto operator&(const logic<W>& rhs) const { return pack() & rhs; }\n";
        line += "    explicit operator uint64_t() const { return pack().to_ullong(); }\n";
    }
    else {
        line += "    template<size_t W> auto operator|(const logic<W>& rhs) const { return (*(const logic<" + width + ">*)this) | rhs; }\n";
        line += "    template<size_t W> auto operator&(const logic<W>& rhs) const { return (*(const logic<" + width + ">*)this) & rhs; }\n";
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
    return args.size() == 2 ? args[1] : "";
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
        if (c == '<') {
            ++depth;
        }
        else if (c == '>') {
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

struct Converter : SyntaxVisitor<Converter> {
    Project project;
    std::vector<ModuleGen> modules;
    ModuleGen* mod = nullptr;
    std::set<std::string> loopVars;
    std::vector<std::map<std::string, std::string>> localTypeScopes;

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

    ModuleGen* findModule(const std::string& name)
    {
        for (auto& candidate : modules) {
            if (candidate.name == name) {
                return &candidate;
            }
        }
        return nullptr;
    }

    void registerTypeField(const std::string& typeName, const std::string& fieldName, const std::string& fieldType)
    {
        if (!mod || typeName.empty() || fieldName.empty() || fieldType.empty()) {
            return;
        }
        mod->typeFields[typeName][fieldName] = fieldType;
        if (mod->isPackage) {
            mod->typeFields[mod->name + "::" + typeName][fieldName] = fieldType;
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
            if (arrayArgs.size() == 2) {
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

    const ExpressionSyntax* propertyExpr(const PropertyExprSyntax& prop)
    {
        if (prop.kind != SyntaxKind::SimplePropertyExpr) {
            return nullptr;
        }
        auto& simpleProp = prop.as<SimplePropertyExprSyntax>();
        if (simpleProp.expr->kind != SyntaxKind::SimpleSequenceExpr) {
            return nullptr;
        }
        return simpleProp.expr->as<SimpleSequenceExprSyntax>().expr;
    }

    void handle(const ModuleDeclarationSyntax& node)
    {
        modules.push_back({});
        mod = &modules.back();
        mod->name = tok(node.header->name);
        mod->isPackage = node.kind == SyntaxKind::PackageDeclaration;
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
                        if (!init.empty() && trim(init).front() == '"') {
                            init = "0";
                        }
                        auto type = constexprType(varType(*pd.type, *d));
                        if (type == "unsigned" || type == "uint64_t") {
                            init = stripLogicLiteralCasts(std::move(init));
                        }
                        if (name == "INTERRUPTS") {
                            init = type + "{}";
                        }
                        if (configuredNameEquals("HDLCPP_SKIP_PARAMS", mod->name + "." + name)) {
                            continue;
                        }
                        mod->params.push_back(type + " " + name + (init.empty() ? "" : " = " + trim(init)));
                    }
                }
                else if (p->kind == SyntaxKind::TypeParameterDeclaration) {
                    auto& td = p->as<TypeParameterDeclarationSyntax>();
                    auto localTypeModules = configuredNameSet("HDLCPP_LOCAL_TYPE_MODULES");
                    auto localTypeNames = configuredNameSet("HDLCPP_LOCAL_TYPE_NAMES");
                    auto typeDeclOverrides = configuredTextMap("HDLCPP_TYPE_DECL_OVERRIDES");
                    auto typeParamDefaults = configuredTextMap("HDLCPP_TYPE_PARAM_DEFAULTS");
                    for (auto d : td.declarators) {
                        auto name = tok(d->name);
                        auto overrideIt = typeDeclOverrides.find(mod->name + "." + name);
                        if (overrideIt != typeDeclOverrides.end() && d->assignment) {
                            auto decl = overrideIt->second;
                            replaceAll(decl, "@PACKED@", packedAggregateHelpers(name));
                            mod->types[name] = name;
                            mod->typeDecls.push_back(decl);
                            continue;
                        }
                        bool localparamType = p->toString().find("localparam") != std::string::npos;
                        bool makeLocalType = (localparamType ||
                                              (localTypeModules.count(mod->name) && localTypeNames.count(name))) &&
                                             d->assignment;
                        if (makeLocalType) {
                            mod->types[name] = name;
	                            if (d->assignment->type->kind == SyntaxKind::StructType ||
	                                d->assignment->type->kind == SyntaxKind::UnionType) {
	                                auto& st = d->assignment->type->as<StructUnionTypeSyntax>();
	                                std::string line = std::string(tok(st.keyword) == "union" ? "union " : "struct ") + name + " {\n";
	                                std::vector<PackedFieldInfo> fieldWidths;
	                                std::vector<std::string> fieldLines;
	                                for (auto member : st.members) {
	                                    for (auto md : member->declarators) {
	                                        if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
	                                auto& nested = member->type->as<StructUnionTypeSyntax>();
	                                line += std::string("    ") + (tok(nested.keyword) == "union" ? "union" : "struct") + " { ";
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
	                                auto fieldWidth = typeWidth(fieldType);
	                                if (!fieldWidth.empty()) {
	                                    fieldWidths.push_back({cppIdent(tok(md->name)), fieldWidth});
	                                }
	                                fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(md->name)) + ";\n");
	                            }
	                        }
	                    }
	                                auto reversePackedTypes = configuredNameSet("HDLCPP_REVERSE_PACKED_TYPES");
	                                if (st.toString().find("packed") != std::string::npos && tok(st.keyword) != "union" &&
	                                    (reversePackedTypes.count(name) || reversePackedTypes.count(mod->name + "." + name))) {
	                                    std::reverse(fieldLines.begin(), fieldLines.end());
	                                    std::reverse(fieldWidths.begin(), fieldWidths.end());
	                                }
	                                auto packedWidth = joinedPackedWidth(fieldWidths);
	                                for (auto& fieldLine : fieldLines) {
	                                    line += fieldLine;
	                                }
	                                line += packedAggregateHelpers(name, packedWidth, mod->isPackage ? std::vector<PackedFieldInfo>{} : fieldWidths);
	                                line += "};";
	                                if (!packedWidth.empty()) {
	                                    mod->typeWidths[name] = packedWidth;
	                                }
	                                mod->typeDecls.push_back(line);
	                            }
                            else {
                                auto initType = cppTypeFromSvText(d->assignment->toString());
                                if (initType == "logic") {
                                    initType = "bool";
                                }
                                mod->typeDecls.push_back("using " + name + " = " + initType + ";");
                                if (configuredNameEquals("HDLCPP_FALSE_CONSTANT_TYPES", mod->name + "." + name)) {
                                    mod->constants.push_back({"acc_cfg_t", "AccCfg = false"});
                                }
                            }
                            continue;
                        }
                        auto init = d->assignment ? trim(cppTypeFromSvText(d->assignment->toString())) : "bool";
                        auto assignmentText = d->assignment ? d->assignment->toString() : std::string();
                        auto defaultIt = typeParamDefaults.find(name);
                        if (defaultIt != typeParamDefaults.end() &&
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

        if (node.header->ports && node.header->ports->kind == SyntaxKind::AnsiPortList) {
            auto& ports = node.header->ports->as<AnsiPortListSyntax>();
            for (auto p : ports.ports) {
                p->visit(*this);
            }
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
            port.type = typeText(*h.dataType);
        }
        else if (node.header->kind == SyntaxKind::VariablePortHeader) {
            auto& h = node.header->as<VariablePortHeaderSyntax>();
            port.direction = tok(h.direction);
            port.type = typeText(*h.dataType);
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
        mod->ports.push_back(port);
    }

    void handle(const DataDeclarationSyntax& node)
    {
        if (!mod) {
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
            std::string line = std::string(tok(st.keyword) == "union" ? "union " : "struct ") + anonymousStructType + " {\n";
	            std::vector<PackedFieldInfo> fieldWidths;
	            std::vector<std::string> fieldLines;
	            for (auto member : st.members) {
                for (auto d : member->declarators) {
                    if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
                        auto& nested = member->type->as<StructUnionTypeSyntax>();
                        line += std::string("    ") + (tok(nested.keyword) == "union" ? "union" : "struct") + " { ";
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
	                        auto fieldWidth = typeWidth(fieldType);
	                        if (!fieldWidth.empty()) {
	                            fieldWidths.push_back({cppIdent(tok(d->name)), fieldWidth});
	                        }
		                        fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(d->name)) + ";\n");
		                    }
		                }
		            }
	            auto reversePackedTypes = configuredNameSet("HDLCPP_REVERSE_PACKED_TYPES");
	            if (st.toString().find("packed") != std::string::npos && tok(st.keyword) != "union" &&
	                (reversePackedTypes.count(anonymousStructType) || reversePackedTypes.count(mod->name + "." + anonymousStructType))) {
	                std::reverse(fieldLines.begin(), fieldLines.end());
	                std::reverse(fieldWidths.begin(), fieldWidths.end());
	            }
	            auto packedWidth = joinedPackedWidth(fieldWidths);
	            for (auto& fieldLine : fieldLines) {
	                line += fieldLine;
	            }
	            line += packedAggregateHelpers(anonymousStructType, packedWidth, mod->isPackage ? std::vector<PackedFieldInfo>{} : fieldWidths);
            line += "};";
            mod->typeDecls.push_back(line);
            mod->types[anonymousStructType] = anonymousStructType;
        }
        for (auto d : node.declarators) {
            auto name = tok(d->name);
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
            mod->vars.push_back({type, name});
            mod->varNames.insert(name);
            mod->types[name] = type;
        }
    }

    void handle(const NetDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto d : node.declarators) {
            auto name = tok(d->name);
            auto type = varType(*node.type, *d);
            mod->vars.push_back({type, name});
            mod->varNames.insert(name);
            mod->types[name] = type;
        }
    }

    void handle(const ParameterDeclarationSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto d : node.declarators) {
            auto name = tok(d->name);
            auto init = d->initializer ? cppExprText(d->initializer->toString()).substr(1) : "{}";
            if (!init.empty() && trim(init).front() == '"') {
                init = "0";
            }
            for (auto& item : mod->types) {
                auto w = typeWidth(item.second);
                replaceAll(init, "$bits(" + item.first + ")", w.empty() ? "(sizeof(" + item.second + ")*8)" : w);
            }
            auto type = varType(*node.type, *d);
            auto initCount = braceElementCount(trim(init));
            if (initCount > 1 && constexprType(type) == "uint64_t" && type.rfind("logic<", 0) != 0) {
                type = "array<uint64_t," + std::to_string(initCount) + ">";
            }
            mod->constants.push_back({type, name + " = " + trim(init)});
            if (mod->isPackage) {
                auto ctype = constexprType(type);
                auto cinit = trim(init);
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
            }
            mod->types[name] = type;
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
                if (mod->isPackage) {
                    mod->packageDecls.push_back(line);
                }
            }
            return;
        }
        if (node.type->kind == SyntaxKind::StructType || node.type->kind == SyntaxKind::UnionType) {
            auto& st = node.type->as<StructUnionTypeSyntax>();
            std::string line = std::string(tok(st.keyword) == "union" ? "union " : "struct ") + name + " {\n";
	            std::vector<PackedFieldInfo> fieldWidths;
	            std::vector<std::string> fieldLines;
            for (auto member : st.members) {
	                for (auto d : member->declarators) {
	                    auto fieldType = varType(*member->type, *d);
	                    registerTypeField(name, tok(d->name), fieldType);
	                    auto fieldWidth = typeWidth(fieldType);
	                    if (!fieldWidth.empty()) {
	                        fieldWidths.push_back({cppIdent(tok(d->name)), fieldWidth});
                    }
                    if (mod->isPackage) {
                        fieldType = constexprStructFieldType(fieldType);
                    }
	                    fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(d->name)) + ";\n");
	                }
	            }
	            auto reversePackedTypes = configuredNameSet("HDLCPP_REVERSE_PACKED_TYPES");
	            if (st.toString().find("packed") != std::string::npos && tok(st.keyword) != "union" &&
	                (reversePackedTypes.count(name) || reversePackedTypes.count(mod->name + "." + name))) {
	                std::reverse(fieldLines.begin(), fieldLines.end());
	                std::reverse(fieldWidths.begin(), fieldWidths.end());
	            }
	            auto packedWidth = joinedPackedWidth(fieldWidths);
	            for (auto& fieldLine : fieldLines) {
	                line += fieldLine;
	            }
	            line += packedAggregateHelpers(name, packedWidth, mod->isPackage ? std::vector<PackedFieldInfo>{} : fieldWidths);
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
            type = "array<" + type + "," + *it + ">";
        }
        auto aliasType = mod->isPackage ? constexprType(type) : type;
        auto line = "using " + name + " = " + aliasType + ";";
        mod->typeDecls.push_back(line);
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
	                    std::string line = std::string(tok(st.keyword) == "union" ? "union " : "struct ") + name + " {\n";
		                    std::vector<PackedFieldInfo> fieldWidths;
		                    std::vector<std::string> fieldLines;
	                    for (auto member : st.members) {
	                        for (auto md : member->declarators) {
	                            if (member->type->kind == SyntaxKind::StructType || member->type->kind == SyntaxKind::UnionType) {
                                auto& nested = member->type->as<StructUnionTypeSyntax>();
                                line += std::string("    ") + (tok(nested.keyword) == "union" ? "union" : "struct") + " { ";
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
	                                auto fieldWidth = typeWidth(fieldType);
	                                if (!fieldWidth.empty()) {
	                                    fieldWidths.push_back({cppIdent(tok(md->name)), fieldWidth});
	                                }
		                                fieldLines.push_back("    " + fieldType + " " + cppIdent(tok(md->name)) + ";\n");
		                            }
		                        }
		                    }
		                    auto reversePackedTypes = configuredNameSet("HDLCPP_REVERSE_PACKED_TYPES");
		                    if (st.toString().find("packed") != std::string::npos && tok(st.keyword) != "union" &&
		                        (reversePackedTypes.count(name) || reversePackedTypes.count(mod->name + "." + name))) {
		                        std::reverse(fieldLines.begin(), fieldLines.end());
		                        std::reverse(fieldWidths.begin(), fieldWidths.end());
		                    }
		                    auto packedWidth = joinedPackedWidth(fieldWidths);
		                    for (auto& fieldLine : fieldLines) {
		                        line += fieldLine;
		                    }
		                    line += packedAggregateHelpers(name, packedWidth, mod->isPackage ? std::vector<PackedFieldInfo>{} : fieldWidths);
	                    line += "};";
	                    if (!packedWidth.empty()) {
	                        mod->typeWidths[name] = packedWidth;
	                    }
	                    mod->typeDecls.push_back(line);
	                }
                else if (d->assignment) {
                    auto aliasOverrides = configuredTextMap("HDLCPP_TYPE_ALIAS_OVERRIDES");
                    auto aliasIt = aliasOverrides.find(name);
                    if (aliasIt != aliasOverrides.end()) {
                        mod->typeDecls.push_back("using " + name + " = " + aliasIt->second + ";");
                    }
                    else {
                        mod->typeDecls.push_back("using " + name + " = " + typeText(*d->assignment->type) + ";");
                    }
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
                auto lhs = mod->outputPortCppNames.count(base) ? mod->outputPortCppNames[base] :
                    (wholeNetAssign && !base.empty() ? base : emitLValue(*b.left));
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
                for (auto& p : mod->ports) {
                    if (p.name == lhs && needsTypedZero(p.type) && isZeroLiteralText(rhs)) {
                        rhs = p.type + "{}";
                        break;
                    }
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
        if (expr.kind != SyntaxKind::ElementSelectExpression &&
            expr.kind != SyntaxKind::IdentifierSelectName) {
            return false;
        }
        auto lhsWidth = foldWidth(exprWidth(expr));
        auto baseWidth = foldWidth(typeWidth(mod->types[base]));
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
        auto visitGenerateClause = [&](const MemberSyntax& member) {
            if (member.kind == SyntaxKind::GenerateBlock) {
                for (auto child : member.as<GenerateBlockSyntax>().members) {
                    child->visit(*this);
                }
            }
            else {
                member.visit(*this);
            }
        };
        if (auto decision = evalConfiguredGenerateCondition(*mod, emitExpr(*node.condition))) {
            if (*decision) {
                visitGenerateClause(*node.block);
            }
            else if (node.elseClause && node.elseClause->clause) {
                visitGenerateClause(node.elseClause->clause->as<MemberSyntax>());
            }
            return;
        }
        if (configuredNameEquals("HDLCPP_CONSTEXPR_GENERATE_MODULES", mod->name)) {
            mod->assignLines.push_back("if constexpr (" + emitExpr(*node.condition) + ") {");
            emitGenerateMember(*node.block, "");
            if (node.elseClause) {
                mod->assignLines.push_back("}");
                mod->assignLines.push_back("else {");
                if (node.elseClause->clause) {
                    auto* clause = node.elseClause->clause.get();
                    if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                        clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::ContinuousAssign) {
                        emitGenerateMember(clause->as<MemberSyntax>(), "");
                    }
                }
            }
            mod->assignLines.push_back("}");
        }
        else {
            node.block->visit(*this);
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
                auto& declParams = (child && !child->params.empty()) ? child->params : configuredParams;
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
            mod->members.push_back(type + (params.empty() ? "" : "<" + params + ">") + " " + name + ";");
            mod->memberTypes.push_back(type);
            mod->memberNames.push_back(name);
            for (auto conn : inst->connections) {
                if (conn->kind == SyntaxKind::NamedPortConnection) {
                    auto& c = conn->as<NamedPortConnectionSyntax>();
                    auto port = tok(c.name);
                    auto expr = c.expr ? propertyExpr(*c.expr) : nullptr;
                    if (expr) {
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, emitExpr(*expr), emitLValue(*expr), true});
                    }
                    else {
                        auto mapped = mod->wireMap.count(port) ? mod->wireMap[port] : port;
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, mapped, mapped, true});
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
        auto existingCombIt = mod->combMethodByBase.find(assigned);
        auto hasExistingComb = comb && !assigned.empty() && existingCombIt != mod->combMethodByBase.end() &&
                               existingCombIt->second < mod->methods.size();
        if (comb) {
            m.name = assigned.empty() ? "always_" + std::to_string(mod->alwaysNo) + "_comb_func" : assigned + "_comb_func";
            if (!assigned.empty() && mod->types.count(assigned)) {
                auto retType = unwrapRegType(mod->types[assigned]);
                m.returnName = combStorageName(*mod, assigned);
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
                if (!hasExistingComb) {
                    mod->wireMap[assigned] = m.name;
                    mod->combMethodByBase[assigned] = mod->methods.size();
                }
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
        emitStatement(*node.statement, m.body, comb, 0);
        if (hasExistingComb) {
            auto& existing = mod->methods[existingCombIt->second];
            existing.body.insert(existing.body.end(), m.body.begin(), m.body.end());
            if (!m.returnName.empty()) {
                existing.returnName = m.returnName;
                existing.returnBase = m.returnBase;
                existing.ret = m.ret;
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
        if (mod->isPackage) {
            m.ret = constexprType(m.ret);
            if (m.ret == "string") {
                m.ret = "std::string";
            }
        }
        if (node.prototype->portList) {
            bool first = true;
            for (auto p : node.prototype->portList->ports) {
                if (p->kind == SyntaxKind::FunctionPort) {
                    auto& fp = p->as<FunctionPortSyntax>();
                    if (!first) {
                        m.args += ", ";
                    }
                    first = false;
                    auto type = fp.dataType ? typeText(*fp.dataType) : "bool";
                    if (mod->isPackage) {
                        type = constexprType(type);
                    }
                    m.args += type + " " + tok(fp.declarator->name);
                }
            }
        }
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
            else if (m.ret != "void") {
                m.body.push_back("return {};");
            }
            mod->methods.push_back(m);
            return;
        }
        loopVars.clear();
        for (auto item : node.items) {
            collectLoopVars(*item);
        }
        for (auto item : node.items) {
            emitNode(*item, m.body, false, 0);
        }
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
            for (auto member : node.as<GenerateBlockSyntax>().members) {
                emitGenerateMember(*member, index, indexLimit, replacements, methodLoopHeaders);
            }
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
                        clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::ContinuousAssign ||
                        clause->kind == SyntaxKind::HierarchyInstantiation) {
                        emitGenerateMember(clause->as<MemberSyntax>(), index, indexLimit, replacements, methodLoopHeaders);
                    }
                }
                return;
            }
            if (mod && configuredNameEquals("HDLCPP_CONSTEXPR_GENERATE_MODULES", mod->name)) {
                auto cond = emitExpr(*ifGen.condition);
                applyGenerateReplacements(cond);
                mod->assignLines.push_back("if constexpr (" + cond + ") {");
                emitGenerateMember(*ifGen.block, index, indexLimit, replacements, methodLoopHeaders);
                if (ifGen.elseClause) {
                    mod->assignLines.push_back("}");
                    mod->assignLines.push_back("else {");
                    if (ifGen.elseClause->clause) {
                        auto* clause = ifGen.elseClause->clause.get();
                        if (clause->kind == SyntaxKind::IfGenerate || clause->kind == SyntaxKind::GenerateBlock ||
                            clause->kind == SyntaxKind::DataDeclaration || clause->kind == SyntaxKind::ContinuousAssign ||
                            clause->kind == SyntaxKind::HierarchyInstantiation) {
                            emitGenerateMember(clause->as<MemberSyntax>(), index, indexLimit, replacements, methodLoopHeaders);
                        }
                    }
                }
                mod->assignLines.push_back("}");
            }
            else {
                emitGenerateMember(*ifGen.block, index, indexLimit, replacements, methodLoopHeaders);
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
                auto& declParams = (child && !child->params.empty()) ? child->params : configuredParams;
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
                auto* childForType = findModule(type);
                auto configuredForType = configuredModuleParams(type);
                auto hasTemplateParams = (childForType && !childForType->params.empty()) || !configuredForType.empty();
                auto memberType = type + (params.empty() ? (hasTemplateParams ? "<>" : "") : "<" + params + ">");
                if (!mod->memberArraySizes.count(name)) {
                    mod->members.push_back("array<" + memberType + "," + indexLimit + "> " + name + ";");
                    mod->memberTypes.push_back(type);
                    mod->memberNames.push_back(name);
                    mod->memberArraySizes[name] = indexLimit;
                }
                auto elem = name + "[(unsigned)(uint64_t)((uint64_t)(i))]";
                for (auto conn : inst->connections) {
                    if (conn->kind != SyntaxKind::NamedPortConnection) {
                        continue;
                    }
                    auto& c = conn->as<NamedPortConnectionSyntax>();
                    auto port = tok(c.name);
                    auto* child = findModule(type);
                    auto portName = port;
                    bool isOutput = false;
                    std::string portType = "bool";
                    if (child) {
                        if (child->portCppNames.count(port)) {
                            portName = child->portCppNames[port];
                        }
                        bool knownPort = false;
                        for (auto& p : child->ports) {
                            if (p.name == portName) {
                                knownPort = true;
                                portType = p.type;
                                isOutput = p.direction == "output";
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
	                            portType = configuredType;
	                        }
	                    }
                    if (!c.expr) {
                        continue;
                    }
                    auto expr = propertyExpr(*c.expr);
                    if (!expr) {
                        continue;
                    }
                    if (isClockPortName(port)) {
                        continue;
                    }
                    auto rhs = emitExpr(*expr);
                    auto lhs = emitLValue(*expr);
                    applyGenerateReplacements(rhs);
                    applyGenerateReplacements(lhs);
                    if (isOutput) {
                        auto outExpr = elem + "." + portName + "()";
                        if (portType.rfind("array<", 0) == 0 &&
                            (lhs.find(".bits(") != std::string::npos || lhs.find(".get(") != std::string::npos)) {
                            outExpr += "[0]";
                        }
                        addCombAssignment(*mod, baseFromLValueText(lhs), lhs, outExpr, methodLoopHeaders);
                    }
                    else {
                        auto sourceTypeBeforeLateBind = expressionStorageType(*mod, rhs);
                        if (sourceTypeBeforeLateBind.rfind("array<", 0) == 0 || sourceTypeBeforeLateBind.rfind("std::array<", 0) == 0) {
                            auto target = trim(portType);
                            if (target.rfind("logic<", 0) == 0 && target.back() == '>') {
                                rhs = target + "(" + rhs + ")";
                            }
                            else {
                                rhs = adaptInputPortRhs(*mod, portType, rhs);
                            }
                        }
                        else {
                            rhs = adaptInputPortRhs(*mod, portType, rhs);
                        }
                        mod->assignLines.push_back("    " + elem + "." + portName + " = " + assignWrapper(rhs, "i") + ";");
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
                emitStatement(*proc.statement, lines, true, 0);
                for (auto line : lines) {
                    applyGenerateReplacements(line);
                    mod->assignLines.push_back("    " + line);
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
                emitStatement(*proc.statement, lines, false, 0);
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
            for (auto d : data.declarators) {
                auto name = tok(d->name);
                auto type = typeText(*data.type);
                for (auto w : dimensionWidths(d->dimensions)) {
                    type = "array<" + type + "," + w + ">";
                }
                mod->assignLines.push_back("    " + type + " " + name + ";");
                mod->types[name] = type;
            }
        }
        else if (node.kind == SyntaxKind::ContinuousAssign) {
            for (auto a : node.as<ContinuousAssignSyntax>().assignments) {
                if (a->kind == SyntaxKind::AssignmentExpression) {
                    auto& b = a->as<BinaryExpressionSyntax>();
                    auto lhs = emitLValue(*b.left);
                    auto rhs = emitExpr(*b.right);
                    if (b.right->kind == SyntaxKind::ConditionalExpression &&
                        lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                        rhs = emitConditionalForLValue(b.right->as<ConditionalExpressionSyntax>(), *b.left, lhs);
                    }
                    applyGenerateReplacements(lhs);
                    applyGenerateReplacements(rhs);
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
        auto comb = isSimpleCombRef(rhs);
        if (captures.empty()) {
            return std::string(comb ? "_ASSIGN_COMB" : "_ASSIGN") + "( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "i") {
            return std::string(comb ? "_ASSIGN_COMB_I" : "_ASSIGN_I") + "( " + rhs + " )";
        }
        if (captures.size() == 1 && captures[0] == "j") {
            return std::string(comb ? "_ASSIGN_COMB_J" : "_ASSIGN_J") + "( " + rhs + " )";
        }
        if (captures.size() == 2 && captures[0] == "i" && captures[1] == "j") {
            return std::string(comb ? "_ASSIGN_COMB_IJ" : "_ASSIGN_IJ") + "( " + rhs + " )";
        }
        std::string capList;
        for (size_t n = 0; n < captures.size(); ++n) {
            if (n) {
                capList += ",";
            }
            capList += captures[n];
        }
        return std::string(comb ? "_ASSIGN_COMB_INDEXED" : "_ASSIGN_INDEXED") + "((" + capList + "), " + rhs + " )";
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

    bool methodHasExternallyVisibleCombSideEffects(const ModuleGen& mod, const MethodGen& method)
    {
        if (method.name.find("_comb_func") == std::string::npos || method.returnName.empty()) {
            return false;
        }
        for (auto& line : method.body) {
            auto t = trim(line);
            if (t.rfind("if ", 0) == 0 || t.rfind("if(", 0) == 0 ||
                t.rfind("for ", 0) == 0 || t.rfind("for(", 0) == 0 ||
                t.rfind("switch ", 0) == 0 || t.rfind("switch(", 0) == 0 ||
                t.rfind("case ", 0) == 0 || t.rfind("default:", 0) == 0) {
                continue;
            }
            auto eq = t.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto base = baseFromLValueText(t.substr(0, eq));
            if (base.empty() || base == method.returnBase || base == method.returnName) {
                continue;
            }
            if (mod.outputPortCppNames.count(base)) {
                return true;
            }
        }
        return false;
    }

    bool emitPlainCombMethod(const ModuleGen& mod, const MethodGen& method)
    {
        return methodHasExternallyVisibleCombSideEffects(mod, method);
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

    std::vector<std::string> sideEffectCombCalls(const ModuleGen& mod)
    {
        std::vector<std::string> calls;
        for (auto& method : mod.methods) {
            if (methodHasExternallyVisibleCombSideEffects(mod, method)) {
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
        if (base.empty() || !target.types.count(base)) {
            return;
        }

        auto retType = unwrapRegType(target.types[base]);
        auto returnName = combStorageName(target, base);
        if (target.outputPortCppNames.count(base)) {
            auto cppName = target.outputPortCppNames[base];
            auto storageType = target.types.count(base) ? target.types[base] :
                outputStorageType(target, base, cppName);
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
            target.methods.push_back(m);
            method = &target.methods.back();
        }
        target.combAssignedVars.insert(base);

        auto finalRhs = rhs;
        if (retType == "bool" || retType == "u1") {
            finalRhs = truthyExpr(finalRhs, typeWidth(target.types[base]));
        }

        // Procedural assignments to fields are value updates. Port/function_ref binding is
        // handled when emitting port initializers and instance connections.
        if (!loopHeaders.empty()) {
            for (auto& header : loopHeaders) {
                method->body.push_back(header);
            }
            method->body.push_back("    " + lhs + " = " + finalRhs + ";");
            for (size_t n = 0; n < loopHeaders.size(); ++n) {
                method->body.push_back("}");
            }
        }
        else {
            method->body.push_back(lhs + " = " + finalRhs + ";");
        }
    }

    std::string translateExpr(std::string s)
    {
        s = cppExprText(s);
        if (!mod) {
            return s;
        }
        for (auto& item : mod->types) {
            auto w = typeWidth(item.second);
            replaceAll(s, "$bits(" + item.first + ")", w.empty() ? "(sizeof(" + item.second + ")*8)" : w);
        }
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
                    elemType = "array<" + elemType + "," + *it + ">";
                }
                return elemType;
            }
            auto width = dimensionsWidth(t.dimensions);
            if (width.empty()) {
                if (keyword == "reg") {
                    return "reg<u1>";
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
            auto width = dimensionsWidth(t.dimensions);
            return width.empty() ? "bool" : "logic<" + width + ">";
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
                while (bracket != std::string::npos) {
                    auto end = raw.find(']', bracket);
                    if (end == std::string::npos) {
                        break;
                    }
                    base = "array<" + base + "," + textRangeWidth(raw.substr(bracket, end - bracket + 1)) + ">";
                    bracket = raw.find('[', end + 1);
                }
                return base;
            }
            auto& name = *type.as<NamedTypeSyntax>().name;
            if (name.kind == SyntaxKind::IdentifierSelectName) {
                auto& n = name.as<IdentifierSelectNameSyntax>();
                auto width = selectsWidth(n.selectors);
                if (!width.empty()) {
                    return "array<" + qualifyImportedType(tok(n.identifier)) + "," + width + ">";
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
                    elemType = "array<" + elemType + "," + *it + ">";
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
            return text == "bool" ? "reg<u1>" : "reg<" + text + ">";
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
            if (rawLeftText == "0" || rawLeftText == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto& b = r.right->as<BinaryExpressionSyntax>();
                    auto bright = trim(emitUntypedNumericExpr(*b.right));
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
                        return emitIndexExpr(*b.left);
                    }
                }
            }
            if (rawRightText == "0" || rawRightText == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.left->kind)) {
                    auto& b = r.left->as<BinaryExpressionSyntax>();
                    auto bright = trim(emitUntypedNumericExpr(*b.right));
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
                        return emitIndexExpr(*b.left);
                    }
                }
            }
            auto right = emitIndexExpr(*r.right);
            if (right == "0" || right == "0x0") {
                if (BinaryExpressionSyntax::isKind(left.kind)) {
                    auto& b = left.as<BinaryExpressionSyntax>();
                    auto bright = trim(emitUntypedNumericExpr(*b.right));
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
                        return emitIndexExpr(*b.left);
                    }
                }
                auto e = foldWidth(emitExpr(left));
                if (isNumber(e)) {
                    return std::to_string(std::stoul(e) + 1);
                }
                return "(" + e + ")+1";
            }
            auto leftExpr = emitIndexExpr(left);
            if (leftExpr == "0" || leftExpr == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto& b = r.right->as<BinaryExpressionSyntax>();
                    auto bright = trim(emitUntypedNumericExpr(*b.right));
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
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
        return exprText(range.selector->toString());
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
        static const char* commonLoopNames[] = {"i", "j", "k", "m", "n", "x_gen", "y_gen", "z_gen", "w_gen"};
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
            if (tok(r.range) == "+:" || tok(r.range) == "-:") {
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
            auto right = emitIndexExpr(*r.right);
            if (right == "0" || right == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.left->kind)) {
                    auto& b = r.left->as<BinaryExpressionSyntax>();
                    auto bright = emitIndexExpr(*b.right);
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
                        return emitIndexExpr(*b.left);
                    }
                }
                auto left = foldWidth(emitIndexExpr(*r.left));
                if (isNumber(left)) {
                    return std::to_string(std::stoul(left) + 1);
                }
                return "(" + left + ")+1";
            }
            auto leftExpr = emitIndexExpr(*r.left);
            if (leftExpr == "0" || leftExpr == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.right->kind)) {
                    auto& b = r.right->as<BinaryExpressionSyntax>();
                    auto bright = emitIndexExpr(*b.right);
                    if (tok(b.operatorToken) == "-" && (bright == "1" || bright == "0x1")) {
                        return emitIndexExpr(*b.left);
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
	        if (type.rfind("array<", 0) == 0 && type.back() == '>') {
	            std::vector<std::string> args;
	            std::string cur;
	            int depth = 0;
	            auto body = type.substr(6, type.size() - 7);
	            for (char c : body) {
	                if (c == '<') {
	                    depth++;
	                }
	                else if (c == '>') {
	                    depth--;
	                }
	                if (c == ',' && depth == 0) {
	                    args.push_back(trim(cur));
	                    cur.clear();
	                }
	                else {
	                    cur.push_back(c);
	                }
	            }
	            if (!cur.empty()) {
	                args.push_back(trim(cur));
	            }
	            if (args.size() == 2) {
	                auto elemWidth = typeWidth(args[0]);
	                if (!elemWidth.empty()) {
	                    return foldWidth("(" + elemWidth + ") * (" + args[1] + ")");
	                }
	            }
	        }
	        if (mod) {
	            auto it = mod->typeWidths.find(type);
	            if (it != mod->typeWidths.end()) {
	                return it->second;
	            }
	        }
	        return "";
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
        if (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
            return expressionStorageType(m, expr.substr(1, expr.size() - 2));
        }
        if (hasSuffix(expr, "_func()")) {
            auto returnName = expr.substr(0, expr.size() - 7);
            auto typeIt = m.combReturnTypes.find(returnName);
            if (typeIt != m.combReturnTypes.end()) {
                return typeIt->second;
            }
        }
        auto base = baseFromLValueText(expr);
        if (!base.empty()) {
            if (auto typeIt = m.types.find(base); typeIt != m.types.end()) {
                return typeIt->second;
            }
            if (auto combIt = m.combMethodByBase.find(base); combIt != m.combMethodByBase.end() && combIt->second < m.methods.size()) {
                auto returnName = m.methods[combIt->second].returnName;
                auto typeIt = m.combReturnTypes.find(returnName);
                if (typeIt != m.combReturnTypes.end()) {
                    return typeIt->second;
                }
            }
        }
        return "";
    }

    std::string adaptInputPortRhs(const ModuleGen& m, const std::string& portType, const std::string& rhs)
    {
        auto target = trim(portType);
        if (target.rfind("logic<", 0) != 0 || target.back() != '>') {
            return rhs;
        }
        auto sourceType = expressionStorageType(m, rhs);
        if (sourceType.rfind("array<", 0) == 0 || sourceType.rfind("std::array<", 0) == 0) {
            return target + "(" + rhs + ")";
        }
        return rhs;
    }


    std::string finalAdaptStructuralAssignLine(const ModuleGen& m, std::string line)
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
        for (size_t i = 0; i < m.memberNames.size() && i < m.memberTypes.size(); ++i) {
            if (m.memberNames[i] == instance) {
                childType = m.memberTypes[i];
                break;
            }
        }
        if (childType.empty()) {
            return line;
        }
        auto* child = findModule(childType);
        if (!child) {
            return line;
        }
        std::string portType;
        bool inputPort = false;
        for (auto& p : child->ports) {
            if (p.name == portName) {
                portType = p.type;
                inputPort = p.direction != "output";
                break;
            }
        }
        if (!inputPort || portType.rfind("logic<", 0) != 0 || portType.back() != '>') {
            return line;
        }
        auto wrap = line.find("_ASSIGN_COMB(", eq);
        size_t argStart = std::string::npos;
        if (wrap != std::string::npos) {
            argStart = wrap + std::string("_ASSIGN_COMB(").size();
        }
        else {
            wrap = line.find("_ASSIGN(", eq);
            if (wrap == std::string::npos) {
                return line;
            }
            argStart = wrap + std::string("_ASSIGN(").size();
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
        auto sourceType = expressionStorageType(m, arg);
        bool aggregateSource = sourceType.rfind("array<", 0) == 0 || sourceType.rfind("std::array<", 0) == 0;
        bool combFunctionSource = arg.find("_func()") != std::string::npos;
        if (!aggregateSource && !combFunctionSource) {
            return line;
        }
        if (arg.rfind(portType + "(", 0) == 0) {
            return line;
        }
        line.replace(argStart, argEnd - argStart, portType + "(" + arg + ")");
        auto combWrap = line.find("_ASSIGN_COMB(", eq);
        if (combWrap != std::string::npos && combWrap < argStart) {
            line.replace(combWrap, std::string("_ASSIGN_COMB").size(), "_ASSIGN");
        }
        return line;
    }

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
                (mod->types[base] == "unsigned" || mod->types[base] == "u32" ||
                 mod->types[base] == "uint32_t" || mod->types[base] == "u64" ||
                 mod->types[base] == "uint64_t"));
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
                    addCombAssignment(m, baseFromLValueText(conn.lhs), conn.lhs, conn.instance + "." + portName + "()");
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
                out << "        " << l << "\n";
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

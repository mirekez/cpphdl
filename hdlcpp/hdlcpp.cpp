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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
    std::vector<std::string> params;
    std::vector<PortGen> ports;
    std::vector<std::pair<std::string, std::string>> vars;
    std::vector<std::pair<std::string, std::string>> constants;
    std::vector<std::string> members;
    std::vector<std::string> memberTypes;
    std::vector<std::string> memberNames;
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
    std::map<std::string, std::string> assignExprByBase;
    std::map<std::string, std::string> portCppNames;
    std::map<std::string, std::string> outputPortCppNames;
    std::map<std::string, std::string> wireMap;
    std::map<std::string, size_t> combMethodByBase;
    std::map<std::string, std::string> combReturnTypes;
    std::map<std::string, std::string> outputRegTypes;
    std::map<std::string, std::string> types;
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
    auto text = std::string(token.valueText());
    if (text.empty()) {
        text = std::string(token.rawText());
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

static void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
        s.replace(pos, from.size(), to);
    }
}

static bool isNumber(const std::string& s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c); });
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
                    while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) {
                        char c = s[i++];
                        if (c != '_') {
                            digits += (c == 'x' || c == 'X' || c == 'z' || c == 'Z') ? '0' : c;
                        }
                    }
                    if (base == 'b') {
                        out += "0b" + digits;
                        continue;
                    }
                    if (base == 'h') {
                        out += "0x" + digits;
                        continue;
                    }
                    if (base == 'd') {
                        out += digits.empty() ? "0" : digits;
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

static std::string postProcessCppLine(std::string line)
{
    replaceAll(line, "<<<", "<<");
    replaceAll(line, ">>>", ">>");

    auto eq = line.find('=');
    if (eq == std::string::npos) {
        return line;
    }
    auto lhs = line.substr(0, eq + 1);
    auto rhs = line.substr(eq + 1);
    auto lhsTrim = trim(lhs.substr(0, lhs.size() - 1));

    if (lhsTrim.find('.') != std::string::npos && lhsTrim.find("._next") == std::string::npos &&
        lhsTrim.find(".data[") == std::string::npos &&
        lhsTrim.find(".bits(") == std::string::npos && lhsTrim.find(".get(") == std::string::npos &&
        rhs.find("_ASSIGN(") == std::string::npos && rhs.find("_ASSIGN_I(") == std::string::npos &&
        rhs.find("_ASSIGN_REG(") == std::string::npos && rhs.find("_ASSIGN_REG_I(") == std::string::npos &&
        rhs.find("_ASSIGN_COMB(") == std::string::npos && rhs.find("_ASSIGN_COMB_I(") == std::string::npos) {
        rhs = " _ASSIGN(" + trim(rhs);
        if (!rhs.empty() && rhs.back() == ';') {
            rhs.pop_back();
            rhs += ");";
        }
    }
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
    replaceAll(s, "$signed", "");
    replaceAll(s, "$unsigned", "");
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
            auto boolValue = trim(expr);
            if (boolValue.rfind("logic<1>(", 0) == 0 && boolValue.back() == ')') {
                boolValue = trim(boolValue.substr(9, boolValue.size() - 10));
            }
            return "((bool)(" + boolValue + "))";
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

    ModuleGen* findModule(const std::string& name)
    {
        for (auto& candidate : modules) {
            if (candidate.name == name) {
                return &candidate;
            }
        }
        return nullptr;
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

        project.modules.push_back({});
        project.modules.back().name = mod->name;

        if (node.header->parameters) {
            for (auto p : node.header->parameters->declarations) {
                if (p->kind == SyntaxKind::ParameterDeclaration) {
                    auto& pd = p->as<ParameterDeclarationSyntax>();
                    for (auto d : pd.declarators) {
                        auto name = tok(d->name);
                        auto init = d->initializer ? exprText(d->initializer->toString()).substr(1) : "";
                        mod->params.push_back("unsigned " + name + (init.empty() ? "" : " = " + trim(init)));
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
        if (svName == "clk" || svName == "reset") {
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
        for (auto d : node.declarators) {
            auto name = tok(d->name);
            auto type = varType(*node.type, *d);
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
            auto init = d->initializer ? exprText(d->initializer->toString()).substr(1) : "0";
            auto type = "unsigned";
            mod->constants.push_back({type, name + " = " + trim(init)});
            mod->types[name] = type;
        }
    }

    void handle(const ParameterDeclarationStatementSyntax& node)
    {
        if (!mod || node.parameter->kind != SyntaxKind::ParameterDeclaration) {
            return;
        }
        handle(node.parameter->as<ParameterDeclarationSyntax>());
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
                if (!base.empty() && wholeNetAssign) {
                    mod->assignExprByBase[base] = rhs;
                }
                mod->assigns.push_back({lhs, rhs});
                if (internalWholeNet) {
                    addCombAssignment(*mod, base, lhs, rhs);
                    continue;
                }
                mod->assignLines.push_back(lhs + " = " + assignWrapper(rhs, "") + ";");
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
        replaceAll(stop, id, "i");
        replaceAll(iter, id, "i");
        mod->assignLines.push_back("for (unsigned i = " + emitExpr(*node.initialExpr) + ";" + stop + ";" + iter + ") {");
        emitGenerateMember(*node.block, id);
        mod->assignLines.push_back("}");
    }

    void handle(const HierarchyInstantiationSyntax& node)
    {
        if (!mod) {
            return;
        }
        std::string params;
        if (node.parameters) {
            for (auto p : node.parameters->parameters) {
                if (!params.empty()) {
                    params += ",";
                }
                if (p->kind == SyntaxKind::OrderedParamAssignment) {
                    params += emitExpr(*p->as<OrderedParamAssignmentSyntax>().expr);
                }
                else if (p->kind == SyntaxKind::NamedParamAssignment && p->as<NamedParamAssignmentSyntax>().expr) {
                    params += emitExpr(*p->as<NamedParamAssignmentSyntax>().expr);
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
                        mod->instanceConns.push_back(InstanceConnGen{name, type, port, "0", mapped, false});
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
                mod->wireMap[assigned] = m.name;
                mod->combMethodByBase[assigned] = mod->methods.size();
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
                    m.args += type + " " + tok(fp.declarator->name);
                }
            }
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

    void emitGenerateMember(const MemberSyntax& node, const std::string& index)
    {
        if (node.kind == SyntaxKind::GenerateBlock) {
            for (auto member : node.as<GenerateBlockSyntax>().members) {
                emitGenerateMember(*member, index);
            }
        }
        else if (node.kind == SyntaxKind::ContinuousAssign) {
            for (auto a : node.as<ContinuousAssignSyntax>().assignments) {
                if (a->kind == SyntaxKind::AssignmentExpression) {
                    auto& b = a->as<BinaryExpressionSyntax>();
                    auto lhs = emitLValue(*b.left);
                    auto rhs = emitExpr(*b.right);
                    replaceAll(lhs, index, "i");
                    replaceAll(rhs, index, "i");
                    auto needsAssign = lhs.find('.') != std::string::npos && lhs.find(".bits(") == std::string::npos &&
                                       lhs.find(".get(") == std::string::npos;
                    mod->assignLines.push_back("    " + lhs + " = " + (needsAssign ? assignWrapper(rhs, index) : rhs) + ";");
                }
            }
        }
    }

    std::string assignWrapper(const std::string& rhs, const std::string& index)
    {
        auto suffix = index.empty() ? "" : "_I";
        if (isSimpleCombRef(rhs)) {
            return std::string("_ASSIGN_COMB") + suffix + "( " + rhs + " )";
        }
        return std::string("_ASSIGN") + suffix + "( " + rhs + " )";
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

    void addCombAssignment(ModuleGen& target, const std::string& svBase, const std::string& lhs, const std::string& rhs)
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

        auto needsAssign = lhs.find('.') != std::string::npos && lhs.find("._next") == std::string::npos &&
                           lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos;
        method->body.push_back(lhs + " = " + (needsAssign ? assignWrapper(rhs, "") : rhs) + ";");
    }

    std::string translateExpr(std::string s)
    {
        s = exprText(s);
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
                    s.replace(pos, p.size(), p + "()");
                    pos += p.size() + 2;
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
        if (IntegerTypeSyntax::isKind(type.kind)) {
            auto& t = type.as<IntegerTypeSyntax>();
            auto keyword = tok(t.keyword);
            auto width = dimensionsWidth(t.dimensions);
            if (width.empty()) {
                if (keyword == "reg") {
                    return "reg<u1>";
                }
                if (keyword == "logic" || keyword == "wire") {
                    return "bool";
                }
                if (keyword == "byte") {
                    return "u8";
                }
                if (keyword == "int" || keyword == "integer") {
                    return "u32";
                }
                if (keyword == "longint" || keyword == "time") {
                    return "u64";
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
        if (type.kind == SyntaxKind::NamedType) {
            auto& name = *type.as<NamedTypeSyntax>().name;
            if (name.kind == SyntaxKind::IdentifierSelectName) {
                auto& n = name.as<IdentifierSelectNameSyntax>();
                auto width = selectsWidth(n.selectors);
                if (!width.empty()) {
                    return "logic<" + width + ">";
                }
            }
            return exprText(type.as<NamedTypeSyntax>().name->toString());
        }
        return exprText(type.toString());
    }

    std::string varType(const DataTypeSyntax& type, const DeclaratorSyntax& decl)
    {
        if (IntegerTypeSyntax::isKind(type.kind)) {
            auto& t = type.as<IntegerTypeSyntax>();
            auto packed = dimensionWidths(t.dimensions);
            auto unpacked = dimensionWidths(decl.dimensions);
            if (packed.size() == 2 && unpacked.size() == 1 && packed[1] == "8") {
                return "memory<u8," + packed[0] + "," + unpacked[0] + ">";
            }
            if (unpacked.size() == 1) {
                return "array<" + typeText(type) + "," + unpacked[0] + ">";
            }
        }
        auto text = typeText(type);
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
            auto right = emitExpr(*r.right);
            if (right == "0" || right == "0x0") {
                if (BinaryExpressionSyntax::isKind(left.kind)) {
                    auto& b = left.as<BinaryExpressionSyntax>();
                    if (tok(b.operatorToken) == "-" && (emitExpr(*b.right) == "1" || emitExpr(*b.right) == "0x1")) {
                        return emitExpr(*b.left);
                    }
                }
                auto e = foldWidth(emitExpr(left));
                if (isNumber(e)) {
                    return std::to_string(std::stoul(e) + 1);
                }
                return "(" + e + ")+1";
            }
            auto l = foldWidth(emitExpr(left));
            right = foldWidth(right);
            if (isNumber(l) && isNumber(right)) {
                auto lv = std::stoul(l);
                auto rv = std::stoul(right);
                return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
            }
            return "(" + l + ")-(" + right + ")+1";
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

    std::string selectWidth(const ElementSelectSyntax& select)
    {
        if (!select.selector) {
            return "";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            return "1";
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            auto right = emitExpr(*r.right);
            if (right == "0" || right == "0x0") {
                if (BinaryExpressionSyntax::isKind(r.left->kind)) {
                    auto& b = r.left->as<BinaryExpressionSyntax>();
                    if (tok(b.operatorToken) == "-" && (emitExpr(*b.right) == "1" || emitExpr(*b.right) == "0x1")) {
                        return emitExpr(*b.left);
                    }
                }
                auto left = foldWidth(emitExpr(*r.left));
                if (isNumber(left)) {
                    return std::to_string(std::stoul(left) + 1);
                }
                return "(" + left + ")+1";
            }
            auto l = foldWidth(emitExpr(*r.left));
            right = foldWidth(right);
            if (isNumber(l) && isNumber(right)) {
                auto lv = std::stoul(l);
                auto rv = std::stoul(right);
                return std::to_string((lv > rv ? lv - rv : rv - lv) + 1);
            }
            return "(" + l + ")-(" + right + ")+1";
        }
        return "";
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
        if (type == "u64") {
            return "64";
        }
        if (type.rfind("reg<", 0) == 0 && type.back() == '>') {
            return typeWidth(type.substr(4, type.size() - 5));
        }
        return "";
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
        if (mod && mod->types.count(simple)) {
            auto w = typeWidth(mod->types[simple]);
            if (!w.empty()) {
                return foldWidth(w);
            }
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
                auto width = foldWidth(selectsWidth(n.selectors));
            if (!width.empty()) {
                return width;
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
            for (auto e : expr.as<ConcatenationExpressionSyntax>().expressions) {
                auto w = foldWidth(exprWidth(*e));
                if (w.empty() || !isNumber(w)) {
                    allConst = false;
                    break;
                }
                total += std::stoul(w);
            }
            if (allConst) {
                return std::to_string(total);
            }
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto& m = expr.as<MultipleConcatenationExpressionSyntax>();
            auto count = emitExpr(*m.expression);
            auto innerWidth = foldWidth(exprWidth(*m.concatenation));
            if (!count.empty() && !innerWidth.empty() &&
                isNumber(count) && isNumber(innerWidth)) {
                return std::to_string(std::stoul(count) * std::stoul(innerWidth));
            }
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            auto left = foldWidth(exprWidth(*c.left));
            auto right = foldWidth(exprWidth(*c.right));
            if (isNumber(left) && isNumber(right)) {
                return std::to_string(std::max(std::stoul(left), std::stoul(right)));
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

    std::string emitConcat(const ConcatenationExpressionSyntax& c)
    {
        std::vector<std::pair<std::string, std::string>> parts;
        size_t total = 0;
        bool numeric = true;
        for (auto e : c.expressions) {
            auto width = foldWidth(exprWidth(*e));
            parts.push_back({width, emitNumericExpr(*e)});
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
            return "cat(" + args + ")";
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
            out.push_back(pre + "if (" + emitPredicate(*c.predicate) + ") {");
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
            out.push_back(pre + "for (" + emitForInit(f) + ";" + (f.stopExpr ? emitExpr(*f.stopExpr) : "") + ";" + emitExprList(f.steps) + ") {");
            emitStatementBody(*f.statement, out, comb, indent + 1);
            out.push_back(pre + "}");
        }
        else if (st.kind == SyntaxKind::CaseStatement) {
            auto& c = st.as<CaseStatementSyntax>();
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
            out.push_back(pre + "switch (" + switchExpr + ") {");
            for (auto item : c.items) {
                if (item->kind == SyntaxKind::StandardCaseItem) {
                    auto& sci = item->as<StandardCaseItemSyntax>();
                    for (auto expr : sci.expressions) {
                        out.push_back(pre + "case " + translateExpr(expr->toString()) + ":");
                    }
                    emitCaseClause(*sci.clause, out, comb, indent + 1);
                    out.push_back(std::string((indent + 1) * 4, ' ') + "break;");
                }
                else if (item->kind == SyntaxKind::DefaultCaseItem) {
                    auto& dci = item->as<DefaultCaseItemSyntax>();
                    out.push_back(pre + "default:");
                    emitCaseClause(*dci.clause, out, comb, indent + 1);
                    out.push_back(std::string((indent + 1) * 4, ' ') + "break;");
                }
            }
            out.push_back(pre + "}");
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
            for (auto item : st.as<BlockStatementSyntax>().items) {
                emitNode(*item, out, comb, indent);
            }
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
            auto line = pre + type + " " + name;
            if (d->initializer) {
                auto s = d->initializer->toString();
                if (!s.empty() && s.front() == '=') {
                    s.erase(s.begin());
                }
                line += " = " + translateExpr(s);
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
                    if (!t.empty() && t.front() == '=') {
                        t.erase(t.begin());
                    }
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

    std::string emitPredicate(const ConditionalPredicateSyntax& p)
    {
        std::string s;
        for (auto c : p.conditions) {
            if (!s.empty()) {
                s += " && ";
            }
            s += emitExpr(*c->expr);
        }
        return s;
    }

    std::string emitStatementExpr(const ExpressionSyntax& expr, bool comb)
    {
        if (expr.kind == SyntaxKind::AssignmentExpression || expr.kind == SyntaxKind::NonblockingAssignmentExpression) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            auto lhs = emitLValue(*b.left);
            auto rhs = emitExpr(*b.right);
            auto base = assignedBase(*b.left);
            if (comb && mod->outputPortCppNames.count(base)) {
                auto storage = outputStorageName(*mod, base);
                if (lhs.rfind(storage, 0) == 0) {
                    lhs.replace(0, storage.size(), base);
                }
            }
            if (mod->types.count(base) &&
                (mod->types[base].rfind("reg<", 0) == 0 || mod->outputPortCppNames.count(base))) {
                if (comb && expr.kind != SyntaxKind::NonblockingAssignmentExpression) {
                    mod->combAssignedVars.insert(base);
                }
                else {
                    mod->seqAssignedVars.insert(base);
                }
            }
            if (mod->types.count(base) && (mod->types[base] == "bool" || mod->types[base] == "u1" || mod->types[base] == "reg<u1>")) {
                rhs = truthyExpr(rhs, exprWidth(*b.right));
            }
            auto sequentialStorageType = mod->types.count(base) ? mod->types[base] : std::string();
            if (mod->outputPortCppNames.count(base)) {
                sequentialStorageType = outputStorageType(*mod, base, mod->outputPortCppNames[base]);
            }
            if ((expr.kind == SyntaxKind::NonblockingAssignmentExpression || (!comb && mod->varNames.count(base))) &&
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
            if (lhs.find('.') != std::string::npos && lhs.find("._next") == std::string::npos &&
                lhs.find(".data[") == std::string::npos &&
                lhs.find(".bits(") == std::string::npos && lhs.find(".get(") == std::string::npos) {
                return lhs + " = _ASSIGN(" + rhs + ")";
            }
            return lhs + " = " + rhs;
        }
        return emitExpr(expr);
    }

    std::string emitLValue(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod->outputPortCppNames.count(name)) {
                if (isAssignOnlyOutput(*mod, name)) {
                    return mod->outputPortCppNames[name] + "()";
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
            auto base = assignedBase(*e.left);
            if (mod->types.count(base) && memoryLikeType(mod->types[base]) && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitMemoryRowAccess(base, emitLValue(*e.left), *e.select->selector->as<BitSelectSyntax>().expr);
            }
            return emitSelectOn(emitLValue(*e.left), *e.select, true);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitLValue(*e.left) + "." + tok(e.name);
        }
        return exprText(expr.toString());
    }

    std::string emitIndexExpr(const ExpressionSyntax& expr)
    {
        auto text = emitExpr(expr);
        return emitNumericExpr(expr, text);
    }

    std::string emitMemoryRowAccess(const std::string& memoryName, const std::string& baseExpr, const ExpressionSyntax& indexExpr)
    {
        return baseExpr + "[(unsigned)(" + emitIndexExpr(indexExpr) + ")]";
    }

    std::string emitNumericExpr(const ExpressionSyntax& expr, const std::string& emitted = "")
    {
        auto text = emitted.empty() ? emitExpr(expr) : emitted;
        auto width = foldWidth(exprWidth(expr));
        if (width == "1") {
            auto simple = trim(text);
            while (simple.size() > 2 && simple.front() == '(' && simple.back() == ')') {
                simple = trim(simple.substr(1, simple.size() - 2));
            }
            if (mod && mod->types.count(simple)) {
                auto knownWidth = typeWidth(mod->types[simple]);
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
        if (isNumber(width)) {
            auto w = std::stoul(width);
            if (w > 0 && w < 64) {
                return "((uint64_t)(" + text + ") & ((1ull << " + width + ") - 1ull))";
            }
        }
        return "(uint64_t)(" + text + ")";
    }

    std::string emitExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod->outputPortCppNames.count(name)) {
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
            auto memorySelect = mod->types.count(base) && memoryLikeType(mod->types[base]);
            auto memoryScalar = memorySelect && scalarMemory(mod->types[base]);
            for (auto sel : n.selectors) {
                s = emitSelectOn(s, *sel, false, memorySelect, memoryScalar);
                key = emitSelectOn(key, *sel, true);
                memorySelect = false;
                memoryScalar = false;
            }
            if (mod->wireMap.count(key)) {
                return mod->wireMap[key] + "()";
            }
            return s;
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            auto base = assignedBase(*e.left);
            if (mod->types.count(base) && memoryLikeType(mod->types[base]) && e.select->selector &&
                e.select->selector->kind == SyntaxKind::BitSelect) {
                return emitMemoryRowAccess(base, emitExpr(*e.left), *e.select->selector->as<BitSelectSyntax>().expr);
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
            return emitExpr(*e.left) + "." + tok(e.name);
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
            auto rhs = emitExpr(*b.right);
            if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" ||
                op == "<" || op == "<=" || op == ">" || op == ">=") {
                return emitNumericExpr(*b.left) + " " + op + " " + emitNumericExpr(*b.right, rhs);
            }
            if (op == "<<" || op == ">>") {
                rhs = "(unsigned)(" + emitNumericExpr(*b.right, rhs) + ")";
            }
            return emitExpr(*b.left) + " " + op + " " + rhs;
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            if (tok(u.operatorToken) == "|") {
                return truthyExpr(emitExpr(*u.operand), exprWidth(*u.operand));
            }
            return tok(u.operatorToken) + emitExpr(*u.operand);
        }
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return "(" + emitExpr(*expr.as<ParenthesizedExpressionSyntax>().expression) + ")";
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            auto width = foldWidth(exprWidth(expr));
            if (isNumber(width)) {
                if (width == "1") {
                    return emitPredicate(*c.predicate) + " ? " + truthyExpr(emitExpr(*c.left), exprWidth(*c.left)) + " : " + truthyExpr(emitExpr(*c.right), exprWidth(*c.right));
                }
                return emitPredicate(*c.predicate) + " ? logic<" + width + ">(" + emitNumericExpr(*c.left) + ") : logic<" + width + ">(" + emitNumericExpr(*c.right) + ")";
            }
            return emitPredicate(*c.predicate) + " ? " + emitExpr(*c.left) + " : " + emitExpr(*c.right);
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            return exprText(i.left->toString()) + (i.arguments ? exprText(i.arguments->toString()) : "()");
        }
        if (expr.kind == SyntaxKind::MultipleConcatenationExpression) {
            auto& m = expr.as<MultipleConcatenationExpressionSyntax>();
            auto count = emitExpr(*m.expression);
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
                appendInner();
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
                return "cat(" + args + ")";
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

    std::string emitPlusBitsCall(const std::string& base, const std::string& left, const std::string& width)
    {
        if (isOneExpr(width)) {
            return base + "[" + bitIndexArg(left) + "]";
        }
        return base + ".bits((" + left + ")+(" + width + ")-1," + left + ")";
    }

    std::string emitSelect(const ElementSelectSyntax& select, bool lvalue = false)
    {
        if (!select.selector) {
            return "[]";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            auto index = emitExpr(*select.selector->as<BitSelectSyntax>().expr);
            auto bits = "[" + bitIndexArg(index) + "]";
            return lvalue ? bits : truthyExpr("logic<1>(" + bits + ")", "1");
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            if (tok(r.range) == "+:") {
                auto left = emitExpr(*r.left);
                auto width = emitExpr(*r.right);
                auto bits = emitPlusBitsCall("", left, width);
                return lvalue ? bits : "logic<" + selectWidth(select) + ">(" + bits + ")";
            }
            auto bits = emitBitsCall("", emitExpr(*r.left), emitExpr(*r.right));
            return lvalue ? bits : "logic<" + selectWidth(select) + ">(" + bits + ")";
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
            auto index = emitExpr(*select.selector->as<BitSelectSyntax>().expr);
            auto bits = base + "[" + bitIndexArg(index) + "]";
            return lvalue ? bits : truthyExpr("logic<1>(" + bits + ")", "1");
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            std::string bits;
            if (tok(r.range) == "+:") {
                auto left = emitExpr(*r.left);
                auto width = emitExpr(*r.right);
                bits = emitPlusBitsCall(base, left, width);
            }
            else {
                bits = emitBitsCall(base, emitExpr(*r.left), emitExpr(*r.right));
            }
            return lvalue ? bits : "logic<" + selectWidth(select) + ">(" + bits + ")";
        }
        auto index = exprText(select.selector->toString());
        return base + (lvalue ? "[(unsigned)(uint64_t)(" + index + ")]" : ".get((unsigned)(uint64_t)(" + index + "))");
    }

    void write(const std::filesystem::path& input)
    {
        std::filesystem::create_directories("generated");
        auto stem = input.stem().string();
        std::ofstream h("generated/" + stem + ".h");

        h << "#pragma once\n\n#include \"cpphdl.h\"\n#include <print>\n\nusing namespace cpphdl;\n\n";

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
            if (!m.params.empty()) {
                h << "template<";
                for (size_t i = 0; i < m.params.size(); ++i) {
                    h << (i ? ", " : "") << m.params[i];
                }
                h << ">\n";
            }
            h << "class " << m.name << " : public Module\n{\npublic:\n";
            for (auto& p : m.ports) {
                std::string init = p.init;
                for (auto& out : m.outputPortCppNames) {
                    if (out.second == p.name) {
                        if (isCombOnlyOutput(m, out.first)) {
                            auto wf = m.wireMap.find(out.first);
                            if (wf != m.wireMap.end()) {
                                init = " = _ASSIGN_COMB( " + wf->second + "() )";
                            }
                        }
                        else if (isAssignOnlyOutput(m, out.first)) {
                            init.clear();
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
                h << "    _PORT(" << p.type << ") " << p.name << p.array << init << ";\n";
            }
            h << "\nprivate:\n";
            for (auto& c : m.constants) {
                h << "    static constexpr " << c.first << " " << c.second << ";\n";
            }
            for (auto& member : m.members) {
                h << "    " << member << "\n";
            }
            for (auto& p : m.outputPortCppNames) {
                if (isCombOnlyOutput(m, p.first) || isAssignOnlyOutput(m, p.first)) {
                    continue;
                }
                auto storageType = outputStorageType(m, p.first, p.second);
                h << "    " << storageType << " " << outputStorageName(m, p.first) << ";\n";
            }
            for (auto& v : m.vars) {
                if (m.bridgeAssignVars.count(v.second)) {
                    continue;
                }
                if (m.combMethodByBase.count(v.second) && !m.seqAssignedVars.count(v.second)) {
                    continue;
                }
                auto emittedType = (m.combAssignedVars.count(v.second) && !m.seqAssignedVars.count(v.second)) ? unwrapRegType(v.first) : v.first;
                if (isAssignDrivenVar(m, v.second)) {
                    emittedType = unwrapRegType(emittedType);
                    h << "    cpphdl::function_ref<" << emittedType << "> " << v.second;
                }
                else {
                    h << "    " << emittedType << " " << v.second;
                }
                h << ";\n";
            }
            h << "\n";
            for (auto& f : m.methods) {
                if (f.name.find("_comb_func") == std::string::npos) {
                    continue;
                }
                emitMethod(h, m, f);
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
                h << "        " << name << "._work(reset);\n";
            }
            for (auto& f : m.methods) {
                if (f.args == "bool reset" && f.name.rfind("always_", 0) == 0) {
                    for (auto& line : f.body) {
                        h << "        " << postProcessCppLine(lateBindCombRhs(m, f, line)) << "\n";
                    }
                }
            }
            h << "    }\n\n";
            h << "    void _strobe()\n    {\n";
            for (auto& name : m.memberNames) {
                h << "        " << name << "._strobe();\n";
            }
            for (auto& p : m.outputPortCppNames) {
                auto storageType = outputStorageType(m, p.first, p.second);
                if (storageType.rfind("reg<", 0) == 0) {
                    h << "        " << outputStorageName(m, p.first) << ".strobe();\n";
                }
            }
            for (auto& v : m.vars) {
                auto emittedType = (m.combAssignedVars.count(v.second) && !m.seqAssignedVars.count(v.second)) ? unwrapRegType(v.first) : v.first;
                if (emittedType.rfind("reg<", 0) == 0) {
                    h << "        " << v.second << ".strobe();\n";
                }
                else if (scheduledMemoryType(emittedType)) {
                    h << "        " << v.second << ".apply();\n";
                }
            }
            h << "    }\n\n    void _assign()\n    {\n";
            MethodGen assignMethod;
            for (auto& line : m.assignLines) {
                auto eq = line.find('=');
                if (eq != std::string::npos && m.bridgeAssignVars.count(baseFromLValueText(line.substr(0, eq)))) {
                    continue;
                }
                h << "        " << postProcessCppLine(lateBindCombRhs(m, assignMethod, line)) << "\n";
            }
            for (auto& name : m.memberNames) {
                h << "        " << name << "._assign();\n";
            }
            h << "    }\n};\n\n";
        }
    }

    void emitInstanceConnections(ModuleGen& m)
    {
        for (auto& conn : m.instanceConns) {
            auto* child = findModule(conn.type);
            if (!child) {
                continue;
            }
            auto portName = conn.port;
            if (child->portCppNames.count(conn.port)) {
                portName = child->portCppNames[conn.port];
            }
            bool knownPort = false;
            for (auto& p : child->ports) {
                if (p.name == portName) {
                    knownPort = true;
                    break;
                }
            }
            if (!knownPort) {
                continue;
            }
            bool isOutput = child->outputPortCppNames.count(conn.port) != 0;
            std::string portType = "bool";
            if (!isOutput) {
                for (auto& p : child->ports) {
                    if (p.name == portName) {
                        portType = p.type;
                        if (p.direction == "output") {
                            isOutput = true;
                        }
                        break;
                    }
                }
            }
            if (isOutput) {
                if (conn.connected) {
                    addCombAssignment(m, baseFromLValueText(conn.lhs), conn.lhs, conn.instance + "." + portName + "()");
                }
            }
            else {
                auto rhs = conn.connected ? conn.rhs : (portType == "bool" ? "false" : portType + "(0)");
                auto boundName = bridgeBoundName(m, rhs);
                auto bridge = !boundName.empty() && m.assignExprByBase.count(boundName) &&
                              isAssignDrivenVar(m, boundName);
                if (bridge) {
                    m.bridgeAssignVars.insert(boundName);
                    rhs = m.assignExprByBase[boundName];
                }
                rhs = lateBindExpr(m, rhs, "");
                auto wrapper = isSimpleCombRef(rhs) ? "_ASSIGN_COMB" : "_ASSIGN";
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
                        if (id != exclude && prev != '.' && next != '(' &&
                            (id == svName || id == cppName || id == oldReg || id == oldStorage || id == oldComb)) {
                            if (isCombOnlyOutput(mod, svName)) {
                                auto wf = mod.wireMap.find(svName);
                                if (wf != mod.wireMap.end()) {
                                    out += wf->second + "()";
                                    replacedOutput = true;
                                    break;
                                }
                            }
                            else if (isAssignOnlyOutput(mod, svName)) {
                                out += cppName + "()";
                                replacedOutput = true;
                                break;
                            }
                            else if (id == oldReg || id == oldStorage) {
                                out += outputStorageName(mod, svName);
                                replacedOutput = true;
                                break;
                            }
                        }
                    }
                    if (!replacedOutput) {
                        if (isAssignDrivenVar(mod, id) && id != exclude && prev != '.' && next != '(') {
                            out += id + "()";
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
            return lateBindExpr(mod, line, "");
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
        return lateBindExpr(mod, lhs, lhsBase) + "=" + lateBindExpr(mod, rhs, lhsBase);
    }

    void emitMethod(std::ofstream& out, const ModuleGen& mod, const MethodGen& m)
    {
        if (m.name.find("_comb_func") != std::string::npos && !m.returnName.empty()) {
            auto typeIt = mod.combReturnTypes.find(m.returnName);
            auto type = typeIt != mod.combReturnTypes.end() ? typeIt->second : std::string("auto");
            out << "    _LAZY_COMB(" << m.returnName << ", " << type << ")\n";
            for (auto& l : m.body) {
                out << "        " << postProcessCppLine(lateBindCombRhs(mod, m, l)) << "\n";
            }
            out << "        return " << m.returnName << ";\n";
            out << "    }\n\n";
            return;
        }
        out << "    " << m.ret << " " << m.name << "(" << m.args << ")\n    {\n";
        for (auto& l : m.body) {
            out << "        " << postProcessCppLine(lateBindCombRhs(mod, m, l)) << "\n";
        }
        if (!m.returnName.empty()) {
            out << "        return " << m.returnName;
            out << ";\n";
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

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
};

struct ModuleGen {
    std::string name;
    std::vector<std::string> params;
    std::vector<PortGen> ports;
    std::vector<std::pair<std::string, std::string>> vars;
    std::vector<std::string> members;
    std::vector<std::string> memberTypes;
    std::vector<std::string> memberNames;
    std::vector<MethodGen> methods;
    std::vector<std::pair<std::string, std::string>> assigns;
    std::vector<std::string> assignLines;
    std::set<std::string> portNames;
    std::set<std::string> varNames;
    std::map<std::string, std::string> wireMap;
    std::map<std::string, std::string> types;
    int alwaysNo = 0;
    bool hasWorkTask = false;
};

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

static void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
        s.replace(pos, from.size(), to);
    }
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
    replaceAll(s, "'h", "0x");
    replaceAll(s, "'d", "");
    replaceAll(s, "'b", "0b");
    replaceAll(s, "'0", "0");
    replaceAll(s, "'1", "1");
    replaceAll(s, "$clog2", "clog2");
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

struct Converter : SyntaxVisitor<Converter> {
    Project project;
    std::vector<ModuleGen> modules;
    ModuleGen* mod = nullptr;
    std::set<std::string> loopVars;

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
                        mod->params.push_back("size_t " + name + (init.empty() ? "" : " = " + trim(init)));
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
        if (port.name == "clk" || port.name == "reset") {
            return;
        }
        mod->portNames.insert(port.name);
        mod->types[port.name] = port.type;
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

    void handle(const ContinuousAssignSyntax& node)
    {
        if (!mod) {
            return;
        }
        for (auto a : node.assignments) {
            if (a->kind == SyntaxKind::AssignmentExpression) {
                auto& b = a->as<BinaryExpressionSyntax>();
                auto lhs = emitLValue(*b.left);
                auto rhs = emitExpr(*b.right);
                mod->assigns.push_back({lhs, rhs});
                mod->assignLines.push_back(lhs + " = " + assignWrapper(rhs, "") + ";");
            }
        }
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
        mod->assignLines.push_back("for (size_t i = " + emitExpr(*node.initialExpr) + ";" + stop + ";" + iter + ") {");
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
                    if (c.expr && port != "clk" && port != "reset") {
                        mod->wireMap[exprText(c.expr->toString())] = name + "." + port;
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
            m.name = assigned.empty() ? "always_" + std::to_string(mod->alwaysNo) + "_comb_func" : assigned + "_func";
            m.ret = assigned.empty() ? "void" : mod->types[assigned] + "&";
            m.returnName = assigned;
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
                    mod->assignLines.push_back("    " + lhs + " = " + assignWrapper(rhs, index) + ";");
                }
            }
        }
    }

    std::string assignWrapper(const std::string& rhs, const std::string& index)
    {
        auto suffix = index.empty() ? "" : "_I";
        return std::string(mod->varNames.count(rhs) || rhs.find("_func()") != std::string::npos ? "_BIND_VAR" : "_BIND") + suffix + "( " + rhs + " )";
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
                auto e = emitExpr(left);
                if (!e.empty() && std::all_of(e.begin(), e.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
                    return std::to_string(std::stoul(e) + 1);
                }
                return "(" + e + ")+1";
            }
            return "(" + emitExpr(left) + ")-(" + right + ")+1";
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
            return emitExpr(*select.selector->as<BitSelectSyntax>().expr);
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
                return "(" + emitExpr(*r.left) + ")+1";
            }
            return "(" + emitExpr(*r.left) + ")-(" + right + ")+1";
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
            out.push_back(pre + "{");
            for (auto item : st.as<BlockStatementSyntax>().items) {
                emitNode(*item, out, comb, indent + 1);
            }
            out.push_back(pre + "}");
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
            auto type = loopVars.count(name) ? "size_t" : typeText(*node.type);
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
                s += (d.type ? (loopVars.count(name) ? "size_t" : typeText(*d.type)) : "auto") + " " + name;
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
            if ((expr.kind == SyntaxKind::NonblockingAssignmentExpression || (!comb && mod->varNames.count(base))) &&
                mod->types[base].rfind("reg<", 0) == 0 &&
                mod->types[base].rfind("memory<", 0) != 0) {
                lhs += "._next";
            }
            return lhs + " = " + rhs;
        }
        return emitExpr(expr);
    }

    std::string emitLValue(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            return mod->wireMap.count(name) ? mod->wireMap[name] : name;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto s = tok(n.identifier);
            for (auto sel : n.selectors) {
                s += emitSelect(*sel);
            }
            return mod->wireMap.count(s) ? mod->wireMap[s] : s;
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            return emitLValue(*e.left) + emitSelect(*e.select);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitLValue(*e.left) + "." + tok(e.name);
        }
        return exprText(expr.toString());
    }

    std::string emitExpr(const ExpressionSyntax& expr)
    {
        if (expr.kind == SyntaxKind::IdentifierName) {
            auto name = tok(expr.as<IdentifierNameSyntax>().identifier);
            if (mod->wireMap.count(name)) {
                return mod->wireMap[name] + "()";
            }
            return mod->portNames.count(name) ? name + "()" : name;
        }
        if (expr.kind == SyntaxKind::IdentifierSelectName) {
            auto& n = expr.as<IdentifierSelectNameSyntax>();
            auto s = tok(n.identifier);
            for (auto sel : n.selectors) {
                s += emitSelect(*sel);
            }
            if (mod->wireMap.count(s)) {
                return mod->wireMap[s] + "()";
            }
            return mod->portNames.count(tok(n.identifier)) ? s + "()" : s;
        }
        if (expr.kind == SyntaxKind::ElementSelectExpression) {
            auto& e = expr.as<ElementSelectExpressionSyntax>();
            return emitExpr(*e.left) + emitSelect(*e.select);
        }
        if (expr.kind == SyntaxKind::MemberAccessExpression) {
            auto& e = expr.as<MemberAccessExpressionSyntax>();
            return emitExpr(*e.left) + "." + tok(e.name);
        }
        if (BinaryExpressionSyntax::isKind(expr.kind)) {
            auto& b = expr.as<BinaryExpressionSyntax>();
            return emitExpr(*b.left) + " " + tok(b.operatorToken) + " " + emitExpr(*b.right);
        }
        if (PrefixUnaryExpressionSyntax::isKind(expr.kind)) {
            auto& u = expr.as<PrefixUnaryExpressionSyntax>();
            return tok(u.operatorToken) + emitExpr(*u.operand);
        }
        if (expr.kind == SyntaxKind::ParenthesizedExpression) {
            return "(" + emitExpr(*expr.as<ParenthesizedExpressionSyntax>().expression) + ")";
        }
        if (expr.kind == SyntaxKind::ConditionalExpression) {
            auto& c = expr.as<ConditionalExpressionSyntax>();
            return emitPredicate(*c.predicate) + " ? " + emitExpr(*c.left) + " : " + emitExpr(*c.right);
        }
        if (expr.kind == SyntaxKind::InvocationExpression) {
            auto& i = expr.as<InvocationExpressionSyntax>();
            return exprText(i.left->toString()) + (i.arguments ? exprText(i.arguments->toString()) : "()");
        }
        return translateExpr(expr.toString());
    }

    std::string emitSelect(const ElementSelectSyntax& select)
    {
        if (!select.selector) {
            return "[]";
        }
        if (select.selector->kind == SyntaxKind::BitSelect) {
            return "[" + emitExpr(*select.selector->as<BitSelectSyntax>().expr) + "]";
        }
        if (RangeSelectSyntax::isKind(select.selector->kind)) {
            auto& r = select.selector->as<RangeSelectSyntax>();
            if (tok(r.range) == "+:") {
                auto left = emitExpr(*r.left);
                auto width = emitExpr(*r.right);
                return ".bits((" + left + ")+(" + width + ")-1," + left + ")";
            }
            return ".bits(" + emitExpr(*r.left) + "," + emitExpr(*r.right) + ")";
        }
        return "[" + exprText(select.selector->toString()) + "]";
    }

    void write(const std::filesystem::path& input)
    {
        std::filesystem::create_directories("generated");
        auto stem = input.stem().string();
        std::ofstream h("generated/" + stem + ".h");

        h << "#pragma once\n\n#include \"cpphdl.h\"\n#include <print>\n\nusing namespace cpphdl;\n\n";
        for (auto& m : modules) {
            for (auto& type : m.memberTypes) {
                if (type != m.name) {
                    h << "#include \"" << type << ".h\"\n";
                }
            }
            if (!m.memberTypes.empty()) {
                h << "\n";
            }
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
                h << "    _PORT(" << p.type << ") " << p.name << p.array << p.init << ";\n";
            }
            h << "\nprivate:\n";
            for (auto& member : m.members) {
                h << "    " << member << "\n";
            }
            for (auto& v : m.vars) {
                h << "    " << v.first << " " << v.second << ";\n";
            }
            h << "\n";
            for (auto& f : m.methods) {
                if (f.name.find("_comb_func") == std::string::npos && f.name.find("_func") == std::string::npos) {
                    continue;
                }
                emitMethod(h, f);
            }
            h << "public:\n";
            for (auto& f : m.methods) {
                if (f.name.find("_comb_func") != std::string::npos || f.name.find("_func") != std::string::npos) {
                    continue;
                }
                emitMethod(h, f);
            }
            if (!m.hasWorkTask) {
                h << "    void _work(bool reset)\n    {\n";
                for (auto& f : m.methods) {
                    if (f.args == "bool reset" && f.name.rfind("always_", 0) == 0) {
                        h << "        " << f.name << "(reset);\n";
                    }
                }
                h << "    }\n\n";
            }
            h << "    void _strobe()\n    {\n";
            for (auto& name : m.memberNames) {
                h << "        " << name << "._strobe();\n";
            }
            for (auto& v : m.vars) {
                if (v.first.rfind("reg<", 0) == 0) {
                    h << "        " << v.second << ".strobe();\n";
                }
                else if (v.first.rfind("memory<", 0) == 0) {
                    h << "        " << v.second << ".apply();\n";
                }
            }
            h << "    }\n\n    void _assign()\n    {\n";
            for (auto& line : m.assignLines) {
                h << "        " << line << "\n";
            }
            for (auto& name : m.memberNames) {
                h << "        " << name << "._assign();\n";
            }
            h << "    }\n};\n\n";
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
                    p.init = std::string(" = ") + (m.varNames.count(a.second) ? "_BIND_VAR( " : "_BIND( ") + rhs + " )";
                }
            }
        }
    }

    void emitMethod(std::ofstream& out, const MethodGen& m)
    {
        out << "    " << m.ret << " " << m.name << "(" << m.args << ")\n    {\n";
        for (auto& l : m.body) {
            out << "        " << l << "\n";
        }
        if (!m.returnName.empty()) {
            out << "        return " << m.returnName << ";\n";
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

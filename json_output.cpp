#include "json_output.h"

#include "Expr.h"
#include "Field.h"
#include "Method.h"
#include "Module.h"
#include "Project.h"
#include "Struct.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace cpphdl;

namespace
{

std::string jsonEscape(const std::string& text)
{
    std::string out;
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

bool parseSize(Expr expr, size_t& value)
{
    expr.flags |= Expr::FLAG_SPECVAL;
    std::string text = trim(expr.str());
    if (text == "false") {
        value = 0;
        return true;
    }
    if (text == "true") {
        value = 1;
        return true;
    }
    int base = 0;
    if (text.size() > 2 && text[0] == '\'' && (text[1] == 'h' || text[1] == 'H')) {
        text = text.substr(2);
        base = 16;
    } else if (text.size() > 2 && text[0] == '\'' && (text[1] == 'd' || text[1] == 'D')) {
        text = text.substr(2);
        base = 10;
    }

    char* end = nullptr;
    unsigned long long parsed = std::strtoull(text.c_str(), &end, base);
    if (end == text.c_str()) {
        return false;
    }
    while (end && *end && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (end && *end) {
        return false;
    }
    value = static_cast<size_t>(parsed);
    return true;
}

bool evalSizeExpr(const Expr& expr, size_t& value)
{
    if (expr.type == Expr::EXPR_BINARY && expr.sub.size() == 2) {
        size_t lhs = 0;
        size_t rhs = 0;
        if (!evalSizeExpr(expr.sub[0], lhs) || !evalSizeExpr(expr.sub[1], rhs)) {
            return false;
        }
        if (expr.value == "+") {
            value = lhs + rhs;
            return true;
        }
        if (expr.value == "-") {
            value = lhs - rhs;
            return true;
        }
        if (expr.value == "*") {
            value = lhs * rhs;
            return true;
        }
        if (expr.value == "/" && rhs != 0) {
            value = lhs / rhs;
            return true;
        }
        return false;
    }

    Expr tmp = expr;
    return parseSize(tmp, value);
}

size_t exprScalarWidth(Expr expr)
{
    if (expr.type == Expr::EXPR_TEMPLATE && expr.value == "cpphdl_reg" && !expr.sub.empty()) {
        return exprScalarWidth(expr.sub.front());
    }
    if (expr.type == Expr::EXPR_TEMPLATE && expr.value.find("cpphdl_") == 0 && expr.sub.size()) {
        size_t width = 0;
        if (evalSizeExpr(expr.sub[0], width)) {
            return width;
        }
    }

    if (expr.type == Expr::EXPR_TYPE) {
        if (expr.value == "cpphdl_logic" || expr.value == "bool" || expr.value == "_Bool") {
            return 1;
        }
        const char* prefixes[] = {"cpphdl_u", "cpphdl_i"};
        for (const char* prefix : prefixes) {
            const std::string p(prefix);
            if (expr.value.rfind(p, 0) == 0 && expr.value.size() > p.size()) {
                char* end = nullptr;
                unsigned long parsed = std::strtoul(expr.value.c_str() + p.size(), &end, 10);
                if (end && *end == '\0') {
                    return parsed;
                }
            }
        }
    }

    expr.flags |= Expr::FLAG_WIRE;
    expr.str();
    if (expr.declSize && expr.declSize != static_cast<size_t>(-1)) {
        return expr.declSize;
    }
    return 0;
}

size_t fieldWidth(Field field)
{
    if (field.bitwidth.type != Expr::EXPR_NONE) {
        size_t width = 0;
        if (parseSize(field.bitwidth, width)) {
            return std::max<size_t>(width, 1);
        }
    }
    if (field.definition.type != Struct::STRUCT_EMPTY) {
        std::ofstream sizeOnly;
        field.definition.print(sizeOnly);
        return std::max<size_t>(field.definition.declSize, 1);
    }
    size_t scalarWidth = exprScalarWidth(field.expr);
    if (scalarWidth) {
        return scalarWidth;
    }
    Expr expr = field.expr;
    const std::string typeName = expr.str();
    for (auto& candidate : currProject->structs) {
        if (candidate.name != typeName && candidate.origName != typeName && candidate.name != field.expr.value) {
            continue;
        }
        if (!candidate.declSize) {
            std::ofstream sizeOnly;
            candidate.print(sizeOnly);
        }
        if (candidate.declSize) {
            return candidate.declSize;
        }
    }
    return 1;
}

size_t fieldElementCount(const Field& field)
{
    size_t count = 1;
    for (auto dim : field.array) {
        size_t value = 0;
        if (!parseSize(dim, value)) {
            return count;
        }
        count *= value;
    }
    return std::max<size_t>(count, 1);
}

std::string portDirection(const Field& field)
{
    return str_ending(field.name, "_out") ? "output" : "input";
}

std::string aggregatePortDirection(const std::string& name)
{
    if (str_ending(name, "_out")) {
        return "output";
    }
    if (str_ending(name, "_in")) {
        return "input";
    }
    return "inout";
}

std::filesystem::path jsonPathFor(std::string filename)
{
    std::filesystem::path path(filename);
    if (path.extension() != ".json") {
        path += ".json";
    }
    return path;
}

void writeBits(std::ofstream& out, const std::vector<int>& bits)
{
    out << "[";
    for (size_t i = 0; i < bits.size(); ++i) {
        out << (i ? ", " : "") << bits[i];
    }
    out << "]";
}

struct JsonModule
{
    int nextBit = 2;
    std::map<std::string, std::vector<int>> nets;

    std::vector<int>& ensureNet(const std::string& name, size_t width)
    {
        auto it = nets.find(name);
        if (it != nets.end()) {
            return it->second;
        }

        std::vector<int> bits;
        bits.reserve(width);
        for (size_t i = 0; i < width; ++i) {
            bits.push_back(nextBit++);
        }
        auto [inserted, _] = nets.emplace(name, std::move(bits));
        return inserted->second;
    }
};

std::string directPortRefName(const Expr& expr)
{
    if (expr.type == Expr::EXPR_MEMBER && expr.sub.empty()) {
        return expr.value;
    }
    if (expr.type == Expr::EXPR_MEMBER
        && expr.sub.size() == 1
        && expr.sub[0].type == Expr::EXPR_NONE) {
        return expr.value;
    }
    if (expr.type == Expr::EXPR_MEMBER
        && expr.sub.size() == 1
        && expr.sub[0].type == Expr::EXPR_MEMBER
        && (expr.sub[0].sub.empty()
            || (expr.sub[0].sub.size() == 1 && expr.sub[0].sub[0].type == Expr::EXPR_NONE))) {
        return expr.sub[0].value + "__" + expr.value;
    }
    return {};
}

std::string assignmentRefName(const Expr& expr)
{
    if (const auto direct = directPortRefName(expr); !direct.empty()) {
        return direct;
    }

    const bool wrapper = expr.type == Expr::EXPR_BODY
        || expr.type == Expr::EXPR_RETURN
        || expr.type == Expr::EXPR_CAST
        || expr.type == Expr::EXPR_PAREN
        || expr.type == Expr::EXPR_UNARY
        || (expr.type == Expr::EXPR_OPERATORCALL && (expr.value == "()" || expr.value == "&"));
    if (wrapper && expr.sub.size() == 1) {
        return assignmentRefName(expr.sub[0]);
    }
    return {};
}

std::map<std::string, std::string> collectDirectPortAliases(const Module& mod)
{
    std::map<std::string, std::string> aliases;
    for (const auto& method : mod.methods) {
        for (const auto& stmt : method.statements) {
            Expr copy = stmt;
            copy.traverseIf([&](Expr& e) {
                if ((e.type != Expr::EXPR_OPERATORCALL && e.type != Expr::EXPR_BINARY)
                    || e.value != "="
                    || e.sub.size() != 2) {
                    return false;
                }
                const std::string lhs = assignmentRefName(e.sub[0]);
                const std::string rhs = assignmentRefName(e.sub[1]);
                const bool lhsChildPort = lhs.find("__") != std::string::npos;
                const bool rhsChildPort = rhs.find("__") != std::string::npos;
                if (!lhs.empty() && !rhs.empty() && lhs != rhs && lhsChildPort) {
                    aliases[lhs] = rhs;
                } else if (!lhs.empty() && !rhs.empty() && lhs != rhs && rhsChildPort) {
                    aliases[rhs] = lhs;
                }
                return false;
            });
        }
    }
    return aliases;
}

std::vector<int>& ensureMaybeAliasedNet(JsonModule& json, const std::map<std::string, std::string>& aliases,
    const std::string& name, size_t width)
{
    auto alias = aliases.find(name);
    if (alias == aliases.end()) {
        return json.ensureNet(name, width);
    }

    auto& bits = json.ensureNet(alias->second, width);
    json.nets[name] = bits;
    return json.nets[name];
}

std::string memberTypeName(const Field& member)
{
    Expr expr = member.expr;
    return expr.str();
}

struct LogicalPort
{
    std::string name;
    std::string direction;
    std::string kind = "signal";
    std::vector<const Field*> fields;
};

std::vector<LogicalPort> logicalPorts(const Project& project, const Module& mod)
{
    std::vector<LogicalPort> ports;
    std::map<std::string, size_t> aggregateIndices;
    for (const auto& field : mod.ports) {
        const auto separator = field.name.find("__");
        if (separator == std::string::npos) {
            Expr expr = field.expr;
            const bool isStruct = field.definition.type != Struct::STRUCT_EMPTY
                || std::any_of(project.structs.begin(), project.structs.end(), [&](const Struct& candidate) {
                    return candidate.name == expr.value || candidate.origName == expr.value;
                });
            ports.push_back({field.name, portDirection(field), isStruct ? "struct" : "signal", {&field}});
            continue;
        }

        const std::string aggregateName = field.name.substr(0, separator);
        auto found = aggregateIndices.find(aggregateName);
        if (found == aggregateIndices.end()) {
            const size_t index = ports.size();
            aggregateIndices.emplace(aggregateName, index);
            ports.push_back({aggregateName, aggregatePortDirection(aggregateName), "interface", {&field}});
        } else {
            ports[found->second].fields.push_back(&field);
        }
    }
    return ports;
}

size_t logicalPortWidth(const LogicalPort& port)
{
    size_t width = 0;
    for (const auto* field : port.fields) {
        width += fieldWidth(*field) * fieldElementCount(*field);
    }
    return std::max<size_t>(width, 1);
}

void writePortMap(std::ofstream& out, const Project& project, const Module& mod, JsonModule& json)
{
    out << "      \"ports\": {";
    bool first = true;
    for (const auto& port : logicalPorts(project, mod)) {
        const size_t width = logicalPortWidth(port);
        auto& bits = json.ensureNet(port.name, width);
        out << (first ? "\n" : ",\n");
        out << "        \"" << jsonEscape(port.name) << "\": {\"direction\": \""
            << port.direction << "\", \"kind\": \"" << port.kind << "\", \"bits\": ";
        writeBits(out, bits);
        out << "}";
        first = false;
    }
    if (!first) {
        out << "\n      ";
    }
    out << "},\n";
}

void writeCells(std::ofstream& out, const Project& project, const Module& mod, JsonModule& json)
{
    const auto aliases = collectDirectPortAliases(mod);
    out << "      \"cells\": {";
    bool firstCell = true;
    for (const auto& member : mod.members) {
        const std::string type = memberTypeName(member);
        const Module* child = nullptr;
        for (const auto& candidate : project.modules) {
            if (candidate.name == type) {
                child = &candidate;
                break;
            }
        }
        if (!child) {
            continue;
        }

        size_t instances = fieldElementCount(member);
        const auto childPorts = logicalPorts(project, *child);
        for (size_t index = 0; index < instances; ++index) {
            const bool indexed = instances > 1;
            const std::string cellName = indexed
                ? member.name + "[" + std::to_string(index) + "]"
                : member.name;

            out << (firstCell ? "\n" : ",\n");
            out << "        \"" << jsonEscape(cellName) << "\": {\n";
            out << "          \"hide_name\": 0,\n";
            out << "          \"type\": \"" << jsonEscape(type) << "\",\n";
            out << "          \"parameters\": {},\n";
            out << "          \"attributes\": {},\n";

            out << "          \"port_directions\": {";
            bool firstPort = true;
            for (const auto& port : childPorts) {
                out << (firstPort ? "\n" : ",\n");
                out << "            \"" << jsonEscape(port.name) << "\": \""
                    << port.direction << "\"";
                firstPort = false;
            }
            if (!firstPort) {
                out << "\n          ";
            }
            out << "},\n";

            out << "          \"connections\": {";
            firstPort = true;
            for (const auto& port : childPorts) {
                std::vector<int> bits;
                for (const auto* field : port.fields) {
                    const size_t width = fieldWidth(*field) * fieldElementCount(*field);
                    std::string netName = member.name + "__" + field->name;
                    if (indexed) {
                        netName += "[" + std::to_string(index) + "]";
                    }
                    const auto& fieldBits = ensureMaybeAliasedNet(json, aliases, netName, width);
                    bits.insert(bits.end(), fieldBits.begin(), fieldBits.end());
                }
                out << (firstPort ? "\n" : ",\n");
                out << "            \"" << jsonEscape(port.name) << "\": ";
                writeBits(out, bits);
                firstPort = false;
            }
            if (!firstPort) {
                out << "\n          ";
            }
            out << "}\n";
            out << "        }";
            firstCell = false;
        }
    }
    if (!firstCell) {
        out << "\n      ";
    }
    out << "},\n";
}

void writeDataMembers(std::ofstream& out, const Module& mod)
{
    out << "      \"data_members\": {";
    bool first = true;
    for (const auto& field : mod.vars) {
        Expr expr = field.expr;
        const bool isRegister = expr.traverseIf([](Expr& item) {
            return item.type == Expr::EXPR_TEMPLATE && item.value == "cpphdl_reg";
        });
        const std::string kind = isRegister ? "register"
            : (str_ending(field.name, "_comb") ? "combinational" : "signal");
        const size_t width = fieldWidth(field) * fieldElementCount(field);
        out << (first ? "\n" : ",\n");
        out << "        \"" << jsonEscape(field.name) << "\": {\"kind\": \""
            << kind << "\", \"width\": " << width << "}";
        first = false;
    }
    if (!first) {
        out << "\n      ";
    }
    out << "},\n";
}

void writeNetnames(std::ofstream& out, const JsonModule& json)
{
    out << "      \"netnames\": {";
    bool first = true;
    for (const auto& [name, bits] : json.nets) {
        out << (first ? "\n" : ",\n");
        out << "        \"" << jsonEscape(name) << "\": {\"hide_name\": 0, \"bits\": ";
        writeBits(out, bits);
        out << ", \"attributes\": {}}";
        first = false;
    }
    if (!first) {
        out << "\n      ";
    }
    out << "}\n";
}

void writeModule(std::ofstream& out, const Project& project, const Module& mod)
{
    JsonModule json;
    json.ensureNet("clk", 1);
    json.ensureNet("reset", 1);

    out << "    \"" << jsonEscape(mod.name) << "\": {\n";
    out << "      \"attributes\": {},\n";
    writePortMap(out, project, mod, json);
    writeCells(out, project, mod, json);
    writeDataMembers(out, mod);
    writeNetnames(out, json);
    out << "    }";
}

}

bool cpphdl::writeJsonOutput(const Project& project, const std::string& filename)
{
    const std::filesystem::path outPath = jsonPathFor(filename);
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path());
    }

    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "Failed to open '" << outPath << "' for writing\n";
        return false;
    }

    out << "{\n";
    out << "  \"creator\": \"cpphdl\",\n";
    out << "  \"modules\": {\n";
    std::set<std::string> seen;
    bool first = true;
    for (const auto& mod : project.modules) {
        if (!seen.insert(mod.name).second) {
            continue;
        }
        out << (first ? "" : ",\n");
        writeModule(out, project, mod);
        first = false;
    }
    out << "\n  }\n";
    out << "}\n";
    std::cout << "Generated: " << outPath << "\n";
    return true;
}

#include "Method.h"
#include "Field.h"
#include "Module.h"

#include <iostream>
#include <fstream>

using namespace cpphdl;

Method* currMethod = nullptr;

namespace
{

std::string indexedRootName(Expr expr)
{
    while (expr.type == Expr::EXPR_INDEX && expr.sub.size() == 2) {
        expr = expr.sub[0];
    }
    return expr.str();
}

std::string assignedWireName(Expr expr)
{
    if (expr.type == Expr::EXPR_MEMBER && expr.sub.size() && expr.sub[0].type == Expr::EXPR_INDEX) {
        return indexedRootName(expr.sub[0]) + "__" + expr.value;
    }
    if (expr.type == Expr::EXPR_INDEX) {
        return indexedRootName(expr);
    }
    if (expr.type == Expr::EXPR_ARRAY && expr.sub.size() > 1) {
        return expr.sub[1].str();
    }
    return expr.str();
}

bool exprContainsValue(const Expr& expr, const std::string& value)
{
    if (expr.value == value) {
        return true;
    }
    for (const auto& sub : expr.sub) {
        if (exprContainsValue(sub, value)) {
            return true;
        }
    }
    return false;
}

bool fieldCanNameWireOrPort(const Field& field, const std::string& name)
{
    // Interface fields are flattened as <interface>__<field> in SV ports.
    return field.name == name || name.find(field.name + "__") == 0;
}

std::string flippedInterfaceDirectionName(std::string name)
{
    if (str_ending(name, "_in")) {
        name.replace(name.length() - 3, 3, "_out");
    }
    else if (str_ending(name, "_out")) {
        name.replace(name.length() - 4, 4, "_in");
    }
    return name;
}

bool moduleHasAssignableName(const Module& module, const std::string& name)
{
    auto matches = [&](const Field& field) {
        return fieldCanNameWireOrPort(field, name);
    };
    auto flippedName = flippedInterfaceDirectionName(name);
    auto flippedMatches = [&](const Field& field) {
        return flippedName != name && fieldCanNameWireOrPort(field, flippedName);
    };

    return any_of(module.wires.begin(), module.wires.end(), matches) ||
        any_of(module.ports.begin(), module.ports.end(), matches) ||
        any_of(module.vars.begin(), module.vars.end(), matches) ||
        any_of(module.members.begin(), module.members.end(), matches) ||
        any_of(module.wires.begin(), module.wires.end(), flippedMatches) ||
        any_of(module.ports.begin(), module.ports.end(), flippedMatches) ||
        any_of(module.vars.begin(), module.vars.end(), flippedMatches) ||
        any_of(module.members.begin(), module.members.end(), flippedMatches);
}

const Expr* topLevelLocalDecl(const Expr& expr)
{
    if (expr.type == Expr::EXPR_DECL) {
        return &expr;
    }
    if (expr.type == Expr::EXPR_BODY && expr.sub.size() == 1 && expr.sub[0].type == Expr::EXPR_DECL) {
        return &expr.sub[0];
    }
    return nullptr;
}

bool isTopLevelLocalDeclWithType(const Expr& expr)
{
    const Expr* decl = topLevelLocalDecl(expr);
    return decl && decl->sub.size() >= 1 && decl->sub[0].type != Expr::EXPR_NUM;
}

bool isHiddenLocalDecl(const Expr& expr)
{
    return expr.value.find("__") == 0 || expr.value.find("_____") != std::string::npos;
}

std::string localDeclInitializer(const Expr& expr)
{
    const Expr* decl = topLevelLocalDecl(expr);
    if (!decl || decl->sub.size() < 2 || decl->sub[1].type == Expr::EXPR_NONE || isHiddenLocalDecl(*decl)) {
        return "";
    }

    Expr init = decl->sub[1];
    init.indent = 0;
    init.flags = expr.flags;
    std::string indent;
    for (int i = 0; i < expr.indent; ++i) {
        indent += "    ";
    }
    return indent + escapeIdentifier(decl->value) + " = " + init.str() + ";\n";
}

std::vector<std::string> formattedBufferNames(const std::vector<Expr>& statements)
{
    std::vector<std::string> names;
    for (const auto& statement : statements) {
        Expr copy = statement;
        copy.traverseIf([&](Expr& expr) {
            if (expr.type == Expr::EXPR_CALL && (expr.value == "sprintf" || expr.value == "snprintf") && !expr.sub.empty()) {
                const std::string name = expr.sub[0].type == Expr::EXPR_VAR
                    ? expr.sub[0].value : expr.sub[0].str();
                if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
            return false;
        });
    }
    return names;
}

}

bool Method::print(std::ofstream& out)
{
    currMethod = this;
    const std::vector<std::string> formatted_buffers = formattedBufferNames(statements);

    if (name == "_strobe") {
        return true;
    }

    // make strong comparison here
    if (str_ending(name, "_assign")) {  // important for inherited methods
        return printAssigns(out);
    }

    if (cpphdl_is_comb_func_name(name)) {
        return printComb(out);
    }

    size_t args_cnt = arguments.size();
    for (auto& arg : arguments) {
        if (arg.name == "clk") {
            --args_cnt;
        }
    }

    if (ret.size() == 0) {
        out << "    task " << escapeIdentifier(name) << " (" << (args_cnt > 1 ? "\n" : "");
    }
    else {
        out << "    function " << (ret[0].value=="std::string"?"string":ret[0].str()) << " " << escapeIdentifier(name) << " (" << (args_cnt > 1 ? "\n" : "");
    }

    bool first = true;
    for (auto& arg : arguments) {
        if (arg.name != "clk") {
//        out << "\n" << arg.expr.debug() << " " << arg.name << "\n";
            out << (args_cnt > 1 ? (first ? "        " : ",       ") : (first ? "" : ", "))
                << (str_ending(arg.name, "_out") ? "output " : "input ") << arg.expr.str() << " " << arg.name << (args_cnt > 1 ? "\n" : "");
            first = false;
        }
    }
    out << (args_cnt>1?"    ":"") << ");" << "\n";

    if (ret.size() == 0) {
        out << "    begin: " << name << "\n";
    }

    // Tasks and functions have the same SystemVerilog declaration-order rule
    // as always_comb blocks: every local declaration must precede the first
    // executable initializer or statement.
    for (auto& stmt : statements) {
        if (!isTopLevelLocalDeclWithType(stmt)) {
            continue;
        }
        Expr decl = *topLevelLocalDecl(stmt);
        decl.indent = 2;
        if (std::find(formatted_buffers.begin(), formatted_buffers.end(), decl.value) != formatted_buffers.end()) {
            // C character buffers written by sprintf-family calls become native SV strings.
            decl.sub[0] = Expr{"std::string", Expr::EXPR_TYPE};
        }
        if (decl.sub.size() > 1) {
            decl.sub[1] = Expr{};
        }
        auto s = decl.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }

    for (auto& stmt : statements) {
        stmt.indent = 2;
        if (isTopLevelLocalDeclWithType(stmt)) {
            out << localDeclInitializer(stmt);
            continue;
        }
//        out << stmt.debug() << "\n";
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }

    if (ret.size() == 0) {
        out << "    end\n";
        out << "    endtask\n";
    }
    else {
        out << "    endfunction\n";
    }
    return true;
}

bool Method::printAssigns(std::ofstream& out)
{
    currMethod = this;

    std::vector<std::string> vars;
    for (auto& stmt : statements) {  // collecting vars from for
        stmt.traverseIf( [&](Expr& e) {  // EXPR_FOR (=: EXPR_BINARY (j: EXPR_MEMBER
                if (e.type == Expr::EXPR_FOR
                    && e.sub.size() > 0 && (e.sub[0].type == Expr::EXPR_BINARY || e.sub[0].type == Expr::EXPR_OPERATORCALL)
                    && e.sub[0].sub.size() > 0 && (e.sub[0].sub[0].type == Expr::EXPR_MEMBER || e.sub[0].sub[0].type == Expr::EXPR_VAR)) {
                    for (auto& str : vars) {
                        if (str == e.sub[0].sub[0].value) {
                            return false;
                        }
                    }
                    vars.push_back(e.sub[0].sub[0].value);
                }
                if (e.type == Expr::EXPR_FOR
                    && e.sub.size() > 0 && e.sub[0].type == Expr::EXPR_BODY
                    && e.sub[0].sub.size() == 1 && e.sub[0].sub[0].type == Expr::EXPR_DECL) {
                    for (auto& str : vars) {
                        if (str == e.sub[0].sub[0].value) {
                            return false;
                        }
                    }
                    vars.push_back(e.sub[0].sub[0].value);
                }
                return false;
            } );
    }
    for (auto& stmt : statements) {  // replacing index names
        stmt.traverseIf( [&](Expr& e) {
                if (e.type == Expr::EXPR_MEMBER || e.type == Expr::EXPR_VAR || e.type == Expr::EXPR_DECL) {
                    for (auto& str : vars) {
                        if (str == e.value) {
                            e.value = std::string("g") + str;
                            return false;
                        }
                    }
                }
                return false;
            } );
    }

    out << "    generate  // " << name <<"\n";
    for (auto& var : vars) {
        out << "        genvar " << "g" << var << ";\n";
    }
    for (auto& stmt : statements) {
        if (exprContainsValue(stmt, "__inst_name")) {
            continue;
        }

//?        if (stmt.type == Expr::EXPR_IF) {
//?            out << stmt.str();
//?        }

        stmt.traverseIf( [&](Expr& e) {
                    if ((e.type == Expr::EXPR_OPERATORCALL && e.value == "=") ||
                        (e.type == Expr::EXPR_BINARY && e.value == "=")) {
                        std::string wname = assignedWireName(e.sub[0]);
                        if (!moduleHasAssignableName(*currModule, wname) && wname.length() > 2) {
                             std::cout << "!!! WARNING: can't find wire: " << wname << ": " << e.debug() << " in '" << currModule->name << "'\n";
                        }
                    }
                    if ((e.type == Expr::EXPR_OPERATORCALL || e.type == Expr::EXPR_MEMBERCALL ) && e.value == "assignIf" && e.sub.size() > 4) {  // Port structure
                        auto tmp = Expr{"gen", Expr::EXPR_BODY};
                        std::string left = e.sub[3].str();
                        if (e.sub[3].type == Expr::EXPR_MEMBER && e.sub[3].sub[0].type == Expr::EXPR_INDEX) {
                            left = indexedRootName(e.sub[3].sub[0]) + "__" + e.sub[3].value;
                        } else
                        if (e.sub[3].type == Expr::EXPR_INDEX) {
                            left = indexedRootName(e.sub[3]);
                        }
                        std::string right = e.sub[4].str();
                        if (e.sub[4].type == Expr::EXPR_MEMBER && e.sub[4].sub[0].type == Expr::EXPR_INDEX) {
                            right = indexedRootName(e.sub[4].sub[0]) + "__" + e.sub[4].value;
                        } else
                        if (e.sub[4].type == Expr::EXPR_INDEX) {
                            right = indexedRootName(e.sub[4]);
                        }
                        for (auto& wire : currModule->wires) {
                            if (wire.name.find(left) == 0 && str_ending(wire.name, "_in")) {
                                std::string left1 = e.sub[3].str();
                                std::string array1 = left1.find("[") != (size_t)-1 ? left1.substr(left1.find("[")) : "";
                                std::string right1 = e.sub[4].str();
                                std::string array2 = right1.find("[") != (size_t)-1 ? right1.substr(right1.find("[")) : "";
                                std::string peer = wire.name;
                                str_replace(peer, left.c_str(), right.c_str());
                                peer.replace(peer.length() - 3, 3, "_out");
                                tmp.sub.push_back(Expr{"=", Expr::EXPR_BINARY, {Expr{wire.name + array1, Expr::EXPR_VAR}, Expr{peer + array2, Expr::EXPR_VAR}}});
                            }
                            if (wire.name.find(left) == 0 && str_ending(wire.name, "_out")) {
                                std::string left1 = e.sub[3].str();
                                std::string array1 = left1.find("[") != (size_t)-1 ? left1.substr(left1.find("[")) : "";
                                std::string right1 = e.sub[4].str();
                                std::string array2 = right1.find("[") != (size_t)-1 ? right1.substr(right1.find("[")) : "";
                                std::string peer = wire.name;
                                str_replace(peer, left.c_str(), right.c_str());
                                peer.replace(peer.length() - 4, 4, "_in");
                                tmp.sub.push_back(Expr{"=", Expr::EXPR_BINARY, {Expr{peer + array2, Expr::EXPR_VAR}, Expr{wire.name + array1, Expr::EXPR_VAR}}});
                            }
                        }
                        e = tmp;
                    }
                    return false;
                } );

        stmt.indent = 2;
        stmt.flags = Expr::FLAG_ASSIGN;
//        out << stmt.debug() << "\n";
        auto s = stmt.str("assign ");
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }
    out << "    endgenerate\n";

    return true;
}

bool Method::printComb(std::ofstream& out)
{
    currMethod = this;

    out << "    always_comb begin : " << escapeIdentifier(name) << "  // " << name <<"\n";
    for (auto& stmt : statements) {
        if (!isTopLevelLocalDeclWithType(stmt)) {
            continue;
        }

        // SystemVerilog requires block declarations before executable
        // statements. C++ default construction appears as declaration plus an
        // initializer, so print only the declaration here and emit the
        // initializer later as a normal assignment.
        Expr decl = *topLevelLocalDecl(stmt);
        decl.indent = 2;
        decl.flags = Expr::FLAG_COMB;
        if (decl.sub.size() > 1) {
            decl.sub[1] = Expr{};
        }
        auto s = decl.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }
    for (size_t i = 0; i < statements.size(); ++i) {
        auto& stmt = statements[i];
//        out << stmt.debug() << "\n";
        stmt.indent = 2;
        stmt.flags = Expr::FLAG_COMB;
        if (isTopLevelLocalDeclWithType(stmt)) {
            out << localDeclInitializer(stmt);
            continue;
        }
        if (i + 1 == statements.size() && stmt.type == Expr::EXPR_RETURN) {
            stmt.flags |= Expr::FLAG_COMB_TERMINAL_RETURN;
        }
        auto s = stmt.str();
        if (!s.empty() && s.back() != '\n') {
            s += ";\n";
        }
        out << s;
    }
    out << "    end\n";

    return true;
}

#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace hdlcpp {

struct CombExtractionPlan {
    std::vector<std::string> independent;
    std::vector<std::string> combined;
};

inline bool isIdentifierChar(char c);

struct ProjectedMemberAccess {
    size_t begin = 0;
    size_t end = 0;
    std::string indices;
    std::string field;
};

inline std::vector<ProjectedMemberAccess> projectedMemberAccesses(const std::string& text,
                                                                 const std::string& baseCall)
{
    std::vector<ProjectedMemberAccess> accesses;
    if (baseCall.empty()) {
        return accesses;
    }
    auto matchingBracket = [&](size_t open) {
        int depth = 0;
        for (size_t pos = open; pos < text.size(); ++pos) {
            if (text[pos] == '[') {
                ++depth;
            }
            else if (text[pos] == ']' && --depth == 0) {
                return pos;
            }
        }
        return std::string::npos;
    };
    for (size_t search = 0; (search = text.find(baseCall, search)) != std::string::npos;) {
        auto before = search == 0 ? '\0' : text[search - 1];
        if (isIdentifierChar(before)) {
            search += baseCall.size();
            continue;
        }
        size_t accessBegin = search;
        size_t groupingDepth = 0;
        while (accessBegin > 0 && text[accessBegin - 1] == '(') {
            auto open = accessBegin - 1;
            auto beforeOpen = open == 0 ? '\0' : text[open - 1];
            if (isIdentifierChar(beforeOpen) || beforeOpen == ')' || beforeOpen == ']' ||
                beforeOpen == '>') {
                break;
            }
            accessBegin = open;
            ++groupingDepth;
        }
        size_t pos = search + baseCall.size();
        std::string indices;
        size_t closedGroups = 0;
        for (;;) {
            bool advanced = false;
            while (pos < text.size() && text[pos] == '[') {
                auto close = matchingBracket(pos);
                if (close == std::string::npos) {
                    break;
                }
                indices += text.substr(pos, close - pos + 1);
                pos = close + 1;
                advanced = true;
            }
            if (closedGroups < groupingDepth && pos < text.size() && text[pos] == ')') {
                ++pos;
                ++closedGroups;
                advanced = true;
                continue;
            }
            if (!advanced) {
                break;
            }
        }
        if (closedGroups != groupingDepth) {
            // A control construct such as `if (value.field)` places an opening
            // parenthesis immediately before the base without grouping the base.
            // Retry from the exact base so that parenthesis cannot hide the field.
            accessBegin = search;
            pos = search + baseCall.size();
            indices.clear();
            while (pos < text.size() && text[pos] == '[') {
                auto close = matchingBracket(pos);
                if (close == std::string::npos) {
                    break;
                }
                indices += text.substr(pos, close - pos + 1);
                pos = close + 1;
            }
        }
        std::string field;
        size_t fieldEnd = pos;
        while (pos < text.size() && text[pos] == '.') {
            auto dot = pos++;
            auto start = pos;
            if (start >= text.size() ||
                (!std::isalpha(static_cast<unsigned char>(text[start])) && text[start] != '_')) {
                pos = dot;
                break;
            }
            while (pos < text.size() && isIdentifierChar(text[pos])) {
                ++pos;
            }
            if (pos < text.size() && text[pos] == '(') {
                pos = dot;
                break;
            }
            if (!field.empty()) {
                field += ".";
            }
            field += text.substr(start, pos - start);
            fieldEnd = pos;
        }
        if (!field.empty()) {
            accesses.push_back({accessBegin, fieldEnd, indices, field});
            search = fieldEnd;
        }
        else {
            search += baseCall.size();
        }
    }
    return accesses;
}

inline std::string projectedFieldIdentifier(std::string field)
{
    for (auto& ch : field) {
        if (!isIdentifierChar(ch)) {
            ch = '_';
        }
    }
    return field;
}

inline std::string trimCombText(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

inline bool isIdentifierChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

inline bool containsIdentifier(const std::string& text, const std::string& ident)
{
    if (ident.empty()) {
        return false;
    }
    for (size_t pos = 0; (pos = text.find(ident, pos)) != std::string::npos;) {
        auto end = pos + ident.size();
        bool leftOk = pos == 0 || !isIdentifierChar(text[pos - 1]);
        bool rightOk = end >= text.size() || !isIdentifierChar(text[end]);
        if (leftOk && rightOk) {
            return true;
        }
        pos = end;
    }
    return false;
}

inline size_t topLevelAssignPos(const std::string& line)
{
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '(') {
            ++paren;
        }
        else if (c == ')') {
            --paren;
        }
        else if (c == '[') {
            ++bracket;
        }
        else if (c == ']') {
            --bracket;
        }
        else if (c == '{') {
            ++brace;
        }
        else if (c == '}') {
            --brace;
        }
        else if (c == '=' && paren == 0 && bracket == 0 && brace == 0) {
            char before = i ? line[i - 1] : '\0';
            char after = i + 1 < line.size() ? line[i + 1] : '\0';
            if (before != '=' && before != '!' && before != '<' && before != '>' && after != '=') {
                return i;
            }
        }
    }
    return std::string::npos;
}

inline std::string assignmentBase(const std::string& line)
{
    auto eq = topLevelAssignPos(line);
    if (eq == std::string::npos) {
        return {};
    }
    auto lhs = trimCombText(line.substr(0, eq));
    if (lhs.empty() || !isIdentifierChar(lhs.front()) || std::isdigit(static_cast<unsigned char>(lhs.front()))) {
        return {};
    }
    size_t end = 0;
    while (end < lhs.size() && isIdentifierChar(lhs[end])) {
        ++end;
    }
    auto rest = trimCombText(lhs.substr(end));
    if (!rest.empty() && rest.rfind("()", 0) != 0 && rest.front() != '[' && rest.front() != '.') {
        return {};
    }
    return lhs.substr(0, end);
}

inline std::string assignmentRhs(const std::string& line)
{
    auto eq = topLevelAssignPos(line);
    if (eq == std::string::npos) {
        return {};
    }
    auto rhs = trimCombText(line.substr(eq + 1));
    if (!rhs.empty() && rhs.back() == ';') {
        rhs.pop_back();
        rhs = trimCombText(rhs);
    }
    return rhs;
}

inline std::string declarationName(const std::string& line)
{
    auto text = trimCombText(line);
    if (text.empty() || text == "{" || text == "}" || text.rfind("if ", 0) == 0 ||
        text.rfind("if(", 0) == 0 || text.rfind("for ", 0) == 0 ||
        text.rfind("for(", 0) == 0 || text.rfind("while ", 0) == 0 ||
        text.rfind("switch ", 0) == 0 || text.rfind("case ", 0) == 0 ||
        text.rfind("return ", 0) == 0) {
        return {};
    }
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trimCombText(text);
    }
    auto eq = topLevelAssignPos(text);
    auto lhs = trimCombText(eq == std::string::npos ? text : text.substr(0, eq));
    if (lhs.find_first_of(" \t\r\n") == std::string::npos) {
        return {};
    }
    while (!lhs.empty() && (lhs.back() == '&' || lhs.back() == '*')) {
        lhs.pop_back();
        lhs = trimCombText(lhs);
    }
    auto end = lhs.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(lhs[end - 1]))) {
        --end;
    }
    auto begin = end;
    while (begin > 0 && isIdentifierChar(lhs[begin - 1])) {
        --begin;
    }
    if (begin == end) {
        return {};
    }
    auto name = lhs.substr(begin, end - begin);
    if (name == "if" || name == "for" || name == "while" || name == "switch" ||
        name == "return") {
        return {};
    }
    return name;
}

inline std::string declarationType(const std::string& line)
{
    auto name = declarationName(line);
    if (name.empty()) {
        return {};
    }
    auto text = trimCombText(line);
    if (!text.empty() && text.back() == ';') {
        text.pop_back();
        text = trimCombText(text);
    }
    auto eq = topLevelAssignPos(text);
    auto lhs = trimCombText(eq == std::string::npos ? text : text.substr(0, eq));
    auto pos = lhs.rfind(name);
    if (pos == std::string::npos) {
        return {};
    }
    return trimCombText(lhs.substr(0, pos));
}

inline bool rewriteLhsBase(std::string& line, const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) {
        return false;
    }
    auto eq = topLevelAssignPos(line);
    if (eq == std::string::npos) {
        return false;
    }
    size_t begin = 0;
    while (begin < eq && std::isspace(static_cast<unsigned char>(line[begin]))) {
        ++begin;
    }
    auto matches = [&](const std::string& token, size_t& len) {
        if (line.compare(begin, token.size(), token) != 0) {
            return false;
        }
        auto next = begin + token.size();
        if (next < eq && isIdentifierChar(line[next])) {
            return false;
        }
        len = token.size();
        return true;
    };
    size_t len = 0;
    if (matches(from + "()", len) || matches(from, len)) {
        line.replace(begin, len, to);
        return true;
    }
    return false;
}

inline bool replaceIdentifier(std::string& text, const std::string& from, const std::string& to)
{
    if (from.empty() || from == to) {
        return false;
    }
    bool changed = false;
    for (size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos;) {
        auto end = pos + from.size();
        bool leftOk = pos == 0 || (!isIdentifierChar(text[pos - 1]) && text[pos - 1] != '.');
        if (pos >= 2 && text[pos - 1] == ':' && text[pos - 2] == ':') {
            leftOk = false;
        }
        bool rightOk = end >= text.size() || !isIdentifierChar(text[end]);
        if (leftOk && rightOk) {
            text.replace(pos, from.size(), to);
            pos += to.size();
            changed = true;
        }
        else {
            pos = end;
        }
    }
    return changed;
}

inline bool replaceExactMember(std::string& text,
                               const std::string& base,
                               const std::string& field,
                               const std::string& to)
{
    if (base.empty() || field.empty() || to.empty()) {
        return false;
    }
    const auto from = base + "." + field;
    bool changed = false;
    for (size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos;) {
        auto end = pos + from.size();
        bool leftOk = pos == 0 || (!isIdentifierChar(text[pos - 1]) && text[pos - 1] != '.');
        if (pos >= 2 && text[pos - 1] == ':' && text[pos - 2] == ':') {
            leftOk = false;
        }
        bool rightOk = end >= text.size() ||
                       (!isIdentifierChar(text[end]) && text[end] != '.');
        if (leftOk && rightOk) {
            text.replace(pos, from.size(), to);
            pos += to.size();
            changed = true;
        }
        else {
            pos = end;
        }
    }
    return changed;
}

inline bool replaceMemberPrefix(std::string& text,
                                const std::string& base,
                                const std::string& field,
                                const std::string& to)
{
    // Projecting an aggregate field rebases both the field itself and every nested
    // member beneath it. Unlike exact leaf replacement, a following dot therefore
    // remains a valid boundary and is preserved after replacing the common prefix.
    if (base.empty() || field.empty() || to.empty()) {
        return false;
    }
    const auto from = base + "." + field;
    bool changed = false;
    for (size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos;) {
        auto end = pos + from.size();
        bool leftOk = pos == 0 || (!isIdentifierChar(text[pos - 1]) && text[pos - 1] != '.');
        if (pos >= 2 && text[pos - 1] == ':' && text[pos - 2] == ':') {
            leftOk = false;
        }
        bool rightOk = end >= text.size() || !isIdentifierChar(text[end]);
        if (leftOk && rightOk) {
            text.replace(pos, from.size(), to);
            pos += to.size();
            changed = true;
        }
        else {
            pos = end;
        }
    }
    return changed;
}

inline std::string localCombNameFor(const std::string& base)
{
    if (base.rfind("__comb_local_", 0) == 0) {
        return base;
    }
    return "__comb_local_" + base;
}

inline int braceDelta(const std::string& line);
inline bool startsControlBlock(const std::string& text);

inline std::set<std::string> targetDependencyVariables(const std::vector<std::string>& lines,
                                                       const std::vector<std::string>& variables,
                                                       const std::string& target)
{
    std::set<std::string> deps{target};
    std::set<std::string> candidates(variables.begin(), variables.end());
    for (const auto& line : lines) {
        auto base = assignmentBase(line);
        if (!base.empty()) {
            candidates.insert(base);
        }
        auto decl = declarationName(line);
        if (!decl.empty()) {
            candidates.insert(decl);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        int depth = 0;
        std::vector<std::pair<int, std::string>> controlStack;
        for (const auto& line : lines) {
            auto text = trimCombText(line);
            while (!controlStack.empty() && depth <= controlStack.back().first) {
                controlStack.pop_back();
            }
            auto base = assignmentBase(line);
            auto decl = declarationName(line);
            auto targetName = !base.empty() ? base : decl;
            if (!targetName.empty() && deps.count(targetName)) {
                auto rhs = assignmentRhs(line);
                if (rhs.empty() && !decl.empty()) {
                    auto eq = topLevelAssignPos(line);
                    if (eq != std::string::npos) {
                        rhs = line.substr(eq + 1);
                    }
                }
                auto dependencyText = !base.empty() ? line : rhs;
                for (const auto& variable : candidates) {
                    if (!deps.count(variable) && containsIdentifier(dependencyText, variable)) {
                        deps.insert(variable);
                        changed = true;
                    }
                }
                for (const auto& control : controlStack) {
                    for (const auto& variable : candidates) {
                        if (!deps.count(variable) && containsIdentifier(control.second, variable)) {
                            deps.insert(variable);
                            changed = true;
                        }
                    }
                }
            }
            if (startsControlBlock(text) && text.find('{') != std::string::npos) {
                controlStack.push_back({depth, line});
            }
            depth += braceDelta(line);
            if (targetName.empty() || !deps.count(targetName)) {
                continue;
            }
        }
    }
    return deps;
}

inline int braceDelta(const std::string& line)
{
    int delta = 0;
    for (char c : line) {
        if (c == '{') {
            ++delta;
        }
        else if (c == '}') {
            --delta;
        }
    }
    return delta;
}

inline bool startsControlBlock(const std::string& text)
{
    return text.rfind("if ", 0) == 0 || text.rfind("if(", 0) == 0 ||
           text.rfind("if constexpr", 0) == 0 ||
           text.rfind("else", 0) == 0 ||
           text.rfind("for ", 0) == 0 || text.rfind("for(", 0) == 0 ||
           text.rfind("switch ", 0) == 0 || text.rfind("switch(", 0) == 0 ||
           text == "{";
}

inline size_t matchingBlockEnd(const std::vector<std::string>& lines, size_t header)
{
    int depth = 0;
    for (size_t i = header; i < lines.size(); ++i) {
        depth += braceDelta(lines[i]);
        if (depth == 0 && i > header) {
            return i;
        }
    }
    return header;
}

inline bool isIfChainHeader(const std::string& text)
{
    return text.rfind("if ", 0) == 0 || text.rfind("if(", 0) == 0 ||
           text.rfind("if constexpr", 0) == 0;
}

inline bool isElseHeader(const std::string& text)
{
    return text.rfind("else", 0) == 0;
}

inline bool isForHeader(const std::string& text)
{
    return text.rfind("for ", 0) == 0 || text.rfind("for(", 0) == 0;
}

inline bool isLoopMaintenanceLine(const std::string& text)
{
    if (text == "break;" || text == "continue;") {
        return true;
    }
    if (text.find("break;") != std::string::npos &&
        (text.rfind("if ", 0) == 0 || text.rfind("if(", 0) == 0)) {
        return true;
    }
    if (text.size() >= 3 && text.back() == ';') {
        auto body = trimCombText(text.substr(0, text.size() - 1));
        if (body.size() >= 2 &&
            (body.ends_with("++") || body.ends_with("--") ||
             body.starts_with("++") || body.starts_with("--"))) {
            return true;
        }
        if (body.find("+=") != std::string::npos || body.find("-=") != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::vector<std::string> indexedAccessesForBase(const std::string& text,
                                                       const std::string& base)
{
    std::vector<std::string> indices;
    for (size_t search = 0; (search = text.find(base, search)) != std::string::npos;) {
        const auto end = search + base.size();
        const bool leftOk = search == 0 || !isIdentifierChar(text[search - 1]);
        const bool rightOk = end >= text.size() || !isIdentifierChar(text[end]);
        if (!leftOk || !rightOk || end >= text.size() || text[end] != '[') {
            search = end;
            continue;
        }
        int depth = 0;
        size_t close = std::string::npos;
        for (size_t pos = end; pos < text.size(); ++pos) {
            if (text[pos] == '[') {
                ++depth;
            }
            else if (text[pos] == ']' && --depth == 0) {
                close = pos;
                break;
            }
        }
        if (close == std::string::npos) {
            break;
        }
        indices.push_back(text.substr(end + 1, close - end - 1));
        search = close + 1;
    }
    return indices;
}

inline bool loopVariableHasPositiveOffset(const std::string& expression,
                                          const std::string& variable)
{
    for (size_t pos = 0; (pos = expression.find(variable, pos)) != std::string::npos;) {
        const auto end = pos + variable.size();
        const bool leftOk = pos == 0 || !isIdentifierChar(expression[pos - 1]);
        const bool rightOk = end >= expression.size() || !isIdentifierChar(expression[end]);
        if (!leftOk || !rightOk) {
            pos = end;
            continue;
        }
        auto next = end;
        while (next < expression.size() &&
               (std::isspace(static_cast<unsigned char>(expression[next])) ||
                expression[next] == ')')) {
            ++next;
        }
        if (next < expression.size() && expression[next] == '+') {
            return true;
        }
        pos = end;
    }
    return false;
}

inline bool parseAscendingLoopHeader(const std::string& header,
                                     std::string& declaration,
                                     std::string& variable,
                                     std::string& bound)
{
    auto text = trimCombText(header);
    if (!isForHeader(text)) {
        return false;
    }
    const auto open = text.find('(');
    if (open == std::string::npos) {
        return false;
    }
    std::vector<size_t> semicolons;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t pos = open + 1; pos < text.size(); ++pos) {
        if (text[pos] == '(') {
            ++depth;
        }
        else if (text[pos] == ')') {
            if (depth == 0) {
                close = pos;
                break;
            }
            --depth;
        }
        else if (text[pos] == ';' && depth == 0) {
            semicolons.push_back(pos);
        }
    }
    if (close == std::string::npos || semicolons.size() != 2) {
        return false;
    }
    auto init = trimCombText(text.substr(open + 1, semicolons[0] - open - 1));
    auto condition = trimCombText(text.substr(semicolons[0] + 1,
                                              semicolons[1] - semicolons[0] - 1));
    auto increment = trimCombText(text.substr(semicolons[1] + 1,
                                              close - semicolons[1] - 1));
    auto eq = init.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    auto beforeEq = trimCombText(init.substr(0, eq));
    auto initial = trimCombText(init.substr(eq + 1));
    size_t nameBegin = beforeEq.size();
    while (nameBegin > 0 && isIdentifierChar(beforeEq[nameBegin - 1])) {
        --nameBegin;
    }
    variable = beforeEq.substr(nameBegin);
    declaration = trimCombText(beforeEq.substr(0, nameBegin));
    if (variable.empty() || declaration.empty() || initial != "0") {
        return false;
    }
    auto compactIncrement = increment;
    compactIncrement.erase(std::remove_if(compactIncrement.begin(), compactIncrement.end(),
        [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); }),
        compactIncrement.end());
    if (compactIncrement != variable + "++" && compactIncrement != "++" + variable) {
        return false;
    }
    size_t less = std::string::npos;
    for (size_t pos = 0; pos < condition.size(); ++pos) {
        if (condition[pos] == '<' &&
            (pos + 1 >= condition.size() || condition[pos + 1] != '<' && condition[pos + 1] != '=') &&
            containsIdentifier(condition.substr(0, pos), variable)) {
            less = pos;
            break;
        }
    }
    if (less == std::string::npos ||
        !containsIdentifier(condition.substr(0, less), variable)) {
        return false;
    }
    bound = trimCombText(condition.substr(less + 1));
    return !bound.empty();
}

inline std::vector<std::string> dependencyOrderedContinuousLoopHeaders(
    const std::vector<std::string>& headers,
    const std::string& target,
    const std::string& lhs,
    const std::string& rhs)
{
    auto normalizeSelfReference = [&](std::string expression) {
        // Earlier expression lowering may turn a read of the net currently being
        // generated into its comb getter or storage name. Recover the SV net name
        // here so loop dependency analysis sees the original indexed self-read.
        for (const auto& alias : {target + "_comb_func()", target + "_comb"}) {
            for (size_t pos = 0; (pos = expression.find(alias, pos)) != std::string::npos;) {
                const auto end = pos + alias.size();
                const bool leftOk = pos == 0 || !isIdentifierChar(expression[pos - 1]);
                const bool rightOk = end == expression.size() || !isIdentifierChar(expression[end]);
                if (leftOk && rightOk) {
                    expression.replace(pos, alias.size(), target);
                    pos += target.size();
                }
                else {
                    pos = end;
                }
            }
        }
        return expression;
    };
    const auto normalizedLhs = normalizeSelfReference(lhs);
    const auto normalizedRhs = normalizeSelfReference(rhs);
    if (!containsIdentifier(normalizedRhs, target)) {
        return headers;
    }
    const auto lhsIndices = indexedAccessesForBase(normalizedLhs, target);
    const auto rhsIndices = indexedAccessesForBase(normalizedRhs, target);
    if (lhsIndices.empty() || rhsIndices.empty()) {
        return headers;
    }
    auto ordered = headers;
    for (auto& header : ordered) {
        std::string declaration;
        std::string variable;
        std::string bound;
        if (!parseAscendingLoopHeader(header, declaration, variable, bound)) {
            continue;
        }
        const bool lhsMovesForward = std::any_of(lhsIndices.begin(), lhsIndices.end(),
            [&](const std::string& index) {
                return loopVariableHasPositiveOffset(index, variable);
            });
        const bool rhsReadsLaterIteration = std::any_of(rhsIndices.begin(), rhsIndices.end(),
            [&](const std::string& index) {
                return loopVariableHasPositiveOffset(index, variable);
            });
        if (rhsReadsLaterIteration && !lhsMovesForward) {
            header = "for (" + declaration + " " + variable + " = (unsigned)(" + bound + "); " +
                     variable + "-- > 0;) {";
        }
    }
    return ordered;
}

inline std::vector<std::string> pruneTargetCombLinesRange(const std::vector<std::string>& lines,
                                                          const std::set<std::string>& deps,
                                                          size_t begin,
                                                          size_t end,
                                                          bool inForLoop = false)
{
    std::vector<std::string> out;
    for (size_t i = begin; i < end;) {
        auto text = trimCombText(lines[i]);
        if (inForLoop && isLoopMaintenanceLine(text)) {
            out.push_back(lines[i]);
            ++i;
            continue;
        }
        auto base = assignmentBase(lines[i]);
        auto decl = declarationName(lines[i]);
        if (!base.empty() || !decl.empty()) {
            auto name = !base.empty() ? base : decl;
            if (deps.count(name)) {
                out.push_back(lines[i]);
            }
            ++i;
            continue;
        }

        if (isIfChainHeader(text) && text.find('{') != std::string::npos) {
            struct Branch {
                std::string header;
                std::vector<std::string> body;
                std::string close;
                bool relevant = false;
            };
            std::vector<Branch> branches;
            size_t j = i;
            bool first = true;
            while (j < end) {
                auto headerText = trimCombText(lines[j]);
                if ((!first && !isElseHeader(headerText)) ||
                    (first && !isIfChainHeader(headerText)) ||
                    headerText.find('{') == std::string::npos) {
                    break;
                }
                auto close = matchingBlockEnd(lines, j);
                if (close <= j || close >= end) {
                    break;
                }
                auto body = pruneTargetCombLinesRange(lines, deps, j + 1, close, inForLoop);
                branches.push_back({lines[j], body, lines[close], !body.empty()});
                j = close + 1;
                first = false;
                if (j >= end || !isElseHeader(trimCombText(lines[j]))) {
                    break;
                }
            }
            bool anyRelevant = false;
            for (const auto& branch : branches) {
                anyRelevant = anyRelevant || branch.relevant;
            }
            if (anyRelevant) {
                for (const auto& branch : branches) {
                    out.push_back(branch.header);
                    out.insert(out.end(), branch.body.begin(), branch.body.end());
                    out.push_back(branch.close);
                }
            }
            i = branches.empty() ? i + 1 : j;
            continue;
        }

        if (startsControlBlock(text) && text.find('{') != std::string::npos) {
            auto close = matchingBlockEnd(lines, i);
            if (close > i && close < end) {
                auto body = pruneTargetCombLinesRange(lines, deps, i + 1, close,
                                                      inForLoop || isForHeader(text));
                if (!body.empty()) {
                    out.push_back(lines[i]);
                    out.insert(out.end(), body.begin(), body.end());
                    out.push_back(lines[close]);
                }
                i = close + 1;
                continue;
            }
        }

        if (text == "}" || text == "};" || text.empty() || text == "break;" || text == "continue;") {
            ++i;
            continue;
        }
        for (const auto& dep : deps) {
            if (containsIdentifier(lines[i], dep)) {
                out.push_back(lines[i]);
                break;
            }
        }
        ++i;
    }
    return out;
}

inline std::vector<std::string> pruneTargetCombLines(const std::vector<std::string>& lines,
                                                     const std::set<std::string>& deps)
{
    return pruneTargetCombLinesRange(lines, deps, 0, lines.size());
}

inline std::set<std::string> targetDependencyVariablesWithPrunedControls(const std::vector<std::string>& lines,
                                                                         const std::vector<std::string>& variables,
                                                                         const std::string& target)
{
    auto deps = targetDependencyVariables(lines, variables, target);
    std::set<std::string> candidates(variables.begin(), variables.end());
    for (const auto& line : lines) {
        auto base = assignmentBase(line);
        if (!base.empty()) {
            candidates.insert(base);
        }
        auto decl = declarationName(line);
        if (!decl.empty()) {
            candidates.insert(decl);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        auto pruned = pruneTargetCombLines(lines, deps);
        for (const auto& line : pruned) {
            for (const auto& variable : candidates) {
                if (!deps.count(variable) && containsIdentifier(line, variable)) {
                    deps.insert(variable);
                    changed = true;
                }
            }
        }
    }
    return deps;
}

inline std::vector<std::string> extractTargetCombLines(const std::vector<std::string>& lines,
                                                       const std::vector<std::string>& variables,
                                                       const std::string& target)
{
    std::vector<std::string> out;
    auto deps = targetDependencyVariablesWithPrunedControls(lines, variables, target);
    auto prunedLines = pruneTargetCombLines(lines, deps);
    std::vector<std::string> locals;
    for (const auto& variable : variables) {
        if (variable != target && deps.count(variable)) {
            locals.push_back(variable);
        }
    }
    std::sort(locals.begin(), locals.end(), [](const auto& a, const auto& b) {
        return a.size() > b.size();
    });

    for (auto line : prunedLines) {
        auto base = assignmentBase(line);
        if (!base.empty() && !deps.count(base)) {
            continue;
        }
        for (const auto& local : locals) {
            replaceIdentifier(line, local, localCombNameFor(local));
        }
        out.push_back(line);
    }
    return out;
}

inline bool canExtractIndependentComb(const std::vector<std::string>& lines,
                                      const std::vector<std::string>& variables,
                                      const std::string& target)
{
    bool sawTargetAssign = false;
    bool targetAssignedInControl = false;
    unsigned targetAssignCount = 0;
    std::set<std::string> assignedBases;
    for (const auto& line : lines) {
        auto base = assignmentBase(line);
        if (!base.empty()) {
            assignedBases.insert(base);
        }
    }
    assignedBases.insert(variables.begin(), variables.end());
    int braceDepth = 0;
    for (const auto& line : lines) {
        auto eq = topLevelAssignPos(line);
        if (eq == std::string::npos && containsIdentifier(line, target)) {
            // A call can update an SV output/inout argument without an assignment
            // operator in the statement. Keep the complete target dependency body
            // so that this side effect is not discarded as an unrelated read.
            return false;
        }
        if (eq != std::string::npos) {
            auto lhsBase = assignmentBase(line);
            if (lhsBase == target) {
                sawTargetAssign = true;
                ++targetAssignCount;
                if (braceDepth > 0) {
                    targetAssignedInControl = true;
                }
                auto rhs = line.substr(eq + 1);
                for (const auto& other : assignedBases) {
                    if (other != target && containsIdentifier(rhs, other)) {
                        return false;
                    }
                }
            }
        }
        for (char c : line) {
            if (c == '{') {
                ++braceDepth;
            }
            else if (c == '}' && braceDepth > 0) {
                --braceDepth;
            }
        }
    }
    (void)targetAssignCount;
    if (targetAssignedInControl) {
        return false;
    }
    return sawTargetAssign;
}

inline std::vector<std::string> extractIndependentCombLines(const std::vector<std::string>& lines,
                                                            const std::string& target)
{
    std::vector<std::string> out;
    for (const auto& line : lines) {
        auto base = assignmentBase(line);
        auto decl = declarationName(line);
        if (base == target || decl == target) {
            out.push_back(line);
        }
    }
    return out;
}

struct FieldLValueIndex {
    size_t fieldOffset = 0;
    std::string expression;
};

inline bool parseBaseFieldLValue(const std::string& lhs, const std::string& base,
                                 std::vector<FieldLValueIndex>& indices,
                                 std::string& field)
{
    auto text = trimCombText(lhs);
    if (base.empty() || text.compare(0, base.size(), base) != 0) {
        return false;
    }
    auto boundary = base.size();
    if (boundary < text.size() && isIdentifierChar(text[boundary])) {
        return false;
    }
    size_t pos = boundary;
    indices.clear();
    field.clear();
    auto parseIndices = [&](size_t fieldOffset) {
        while (pos < text.size() && text[pos] == '[') {
            int depth = 0;
            auto begin = pos + 1;
            size_t end = std::string::npos;
            for (; pos < text.size(); ++pos) {
                if (text[pos] == '[') {
                    ++depth;
                }
                else if (text[pos] == ']' && --depth == 0) {
                    end = pos;
                    break;
                }
            }
            if (end == std::string::npos) {
                return false;
            }
            indices.push_back({fieldOffset,
                               trimCombText(text.substr(begin, end - begin))});
            pos = end + 1;
        }
        return true;
    };

    // Preserve every packed selector and where it occurs in the member path.
    // A projected field can itself be a multidimensional packed array, so dropping
    // a later selector changes both the destination and its required value type.
    if (!parseIndices(0)) {
        return false;
    }
    if (pos >= text.size() || text[pos] != '.') {
        return false;
    }
    std::string path;
    while (pos < text.size() && text[pos] == '.') {
        ++pos;
        auto start = pos;
        if (pos >= text.size() || (!std::isalpha(static_cast<unsigned char>(text[pos])) && text[pos] != '_')) {
            return false;
        }
        ++pos;
        while (pos < text.size() && isIdentifierChar(text[pos])) {
            ++pos;
        }
        if (!path.empty()) {
            path += ".";
        }
        path += text.substr(start, pos - start);
        if (pos < text.size() && text[pos] == '(') {
            // Generated C++ represents a packed SV part-select as `.bits(hi, lo)`.
            // Keep that callable selector attached to the rebased field path; dropping
            // its arguments turns a legal slice assignment into `.bits = value`.
            auto callBegin = pos;
            int depth = 0;
            size_t callEnd = std::string::npos;
            for (; pos < text.size(); ++pos) {
                if (text[pos] == '(') {
                    ++depth;
                }
                else if (text[pos] == ')' && --depth == 0) {
                    callEnd = pos;
                    break;
                }
            }
            if (callEnd == std::string::npos) {
                return false;
            }
            path += text.substr(callBegin, callEnd - callBegin + 1);
            pos = callEnd + 1;
        }
        if (!parseIndices(path.size())) {
            return false;
        }
    }
    field = path;
    return !field.empty() && pos == text.size();
}

inline std::vector<std::string> extractTargetFieldCombLinesRange(const std::vector<std::string>& lines,
                                                                 const std::string& base,
                                                                 const std::string& field,
                                                                 const std::string& resultName,
                                                                 const std::string& indexName,
                                                                 size_t begin,
                                                                 size_t end,
                                                                 bool inForLoop = false)
{
    std::vector<std::string> out;
    for (size_t i = begin; i < end;) {
        auto text = trimCombText(lines[i]);
        if (inForLoop && isLoopMaintenanceLine(text)) {
            out.push_back(lines[i]);
            ++i;
            continue;
        }
        auto decl = declarationName(lines[i]);
        if (!decl.empty()) {
            out.push_back(lines[i]);
            ++i;
            continue;
        }
        auto eq = topLevelAssignPos(lines[i]);
        if (eq != std::string::npos) {
            auto lhs = trimCombText(lines[i].substr(0, eq));
            auto assigned = assignmentBase(lines[i]);
            if (assigned != base) {
                out.push_back(lines[i]);
                ++i;
                continue;
            }
            std::vector<FieldLValueIndex> lhsIndices;
            std::string lhsField;
            auto parsedField = parseBaseFieldLValue(lhs, base, lhsIndices, lhsField);
            auto projectedTarget = [&](const std::string& suffix, bool consumeFirstIndex) {
                std::vector<std::string> inserts(suffix.size() + 1);
                auto beginIndex = consumeFirstIndex && !lhsIndices.empty() ? size_t{1} : size_t{0};
                for (size_t index = beginIndex; index < lhsIndices.size(); ++index) {
                    auto relativeOffset = lhsIndices[index].fieldOffset > field.size()
                        ? std::min(lhsIndices[index].fieldOffset - field.size(), suffix.size())
                        : size_t{0};
                    inserts[relativeOffset] += "[" + lhsIndices[index].expression + "]";
                }
                std::string target = resultName;
                for (size_t offset = 0; offset <= suffix.size(); ++offset) {
                    target += inserts[offset];
                    if (offset < suffix.size()) {
                        target += suffix[offset];
                    }
                }
                return target;
            };
            if (parsedField &&
                (lhsField == field || lhsField.rfind(field + ".", 0) == 0)) {
                auto rhs = assignmentRhs(lines[i]);
                auto suffix = lhsField == field ? std::string() : lhsField.substr(field.size());
                auto consumesIndex = !indexName.empty() && !lhsIndices.empty();
                auto target = projectedTarget(suffix, consumesIndex);
                auto assign = target + " = " + rhs + ";";
                if (consumesIndex) {
                    out.push_back("if ((uint64_t)(" + lhsIndices.front().expression +
                                  ") == (uint64_t)(" + indexName + ")) {");
                    out.push_back("    " + assign);
                    out.push_back("}");
                }
                else {
                    out.push_back(assign);
                }
            }
            else if (parsedField &&
                     !lhsField.empty() && field.rfind(lhsField + ".", 0) == 0) {
                auto rhs = assignmentRhs(lines[i]);
                auto descendant = field.substr(lhsField.size() + 1);
                auto projectedRhs = rhs == "0" || rhs == "0b0" || rhs == "0x0" || rhs == "{}"
                    ? std::string("{}")
                    : "(" + rhs + ")." + descendant;
                auto consumesIndex = !indexName.empty() && !lhsIndices.empty();
                auto target = projectedTarget("", consumesIndex);
                auto assign = target + " = " + projectedRhs + ";";
                if (consumesIndex) {
                    out.push_back("if ((uint64_t)(" + lhsIndices.front().expression +
                                  ") == (uint64_t)(" + indexName + ")) {");
                    out.push_back("    " + assign);
                    out.push_back("}");
                }
                else {
                    out.push_back(assign);
                }
            }
            else if (lhs == base) {
                auto rhs = assignmentRhs(lines[i]);
                auto rhsTrimmed = trimCombText(rhs);
                if (rhsTrimmed == "0" || rhsTrimmed == "0b0" || rhsTrimmed == "{}") {
                    out.push_back(resultName + " = {};");
                }
                else {
                    out.push_back(resultName + " = (" + rhs + ")." + field + ";");
                }
            }
            ++i;
            continue;
        }

        if (isIfChainHeader(text) && text.find('{') != std::string::npos) {
            struct Branch {
                std::string header;
                std::vector<std::string> body;
                std::string close;
            };
            std::vector<Branch> branches;
            size_t j = i;
            bool first = true;
            while (j < end) {
                auto headerText = trimCombText(lines[j]);
                if ((!first && !isElseHeader(headerText)) ||
                    (first && !isIfChainHeader(headerText)) ||
                    headerText.find('{') == std::string::npos) {
                    break;
                }
                auto close = matchingBlockEnd(lines, j);
                if (close <= j || close >= end) {
                    break;
                }
                auto body = extractTargetFieldCombLinesRange(lines, base, field, resultName, indexName,
                                                             j + 1, close, inForLoop);
                branches.push_back({lines[j], body, lines[close]});
                j = close + 1;
                first = false;
                if (j >= end || !isElseHeader(trimCombText(lines[j]))) {
                    break;
                }
            }
            bool anyRelevant = false;
            for (const auto& branch : branches) {
                anyRelevant = anyRelevant || !branch.body.empty();
            }
            if (anyRelevant) {
                for (const auto& branch : branches) {
                    out.push_back(branch.header);
                    out.insert(out.end(), branch.body.begin(), branch.body.end());
                    out.push_back(branch.close);
                }
            }
            i = branches.empty() ? i + 1 : j;
            continue;
        }

        if (startsControlBlock(text) && text.find('{') != std::string::npos) {
            auto close = matchingBlockEnd(lines, i);
            if (close > i && close < end) {
                auto body = extractTargetFieldCombLinesRange(lines, base, field, resultName, indexName,
                                                             i + 1, close, inForLoop || isForHeader(text));
                if (!body.empty()) {
                    out.push_back(lines[i]);
                    out.insert(out.end(), body.begin(), body.end());
                    out.push_back(lines[close]);
                }
                i = close + 1;
                continue;
            }
        }

        ++i;
    }
    return out;
}

inline std::vector<std::string> extractTargetFieldCombLines(const std::vector<std::string>& lines,
                                                            const std::string& base,
                                                            const std::string& field,
                                                            const std::string& resultName,
                                                            const std::string& indexName)
{
    auto projected = extractTargetFieldCombLinesRange(
        lines, base, field, resultName, indexName, 0, lines.size());
    std::vector<std::string> variables{resultName};
    for (const auto& line : projected) {
        auto name = assignmentBase(line);
        if (name.empty()) {
            name = declarationName(line);
        }
        if (!name.empty() &&
            std::find(variables.begin(), variables.end(), name) == variables.end()) {
            variables.push_back(name);
        }
    }
    // Field projection initially retains non-aggregate assignments because they may
    // be temporaries feeding the selected member. Slice that projected program once
    // more so unrelated temporary calculations cannot create false comb dependencies.
    return extractTargetCombLines(projected, variables, resultName);
}

inline std::vector<std::string> extractProjectedArrayFieldCombLinesRange(
    const std::vector<std::string>& lines,
    const std::string& base,
    const std::string& field,
    const std::string& resultName,
    size_t begin,
    size_t end,
    bool inForLoop = false)
{
    std::vector<std::string> out;
    std::vector<std::pair<std::string, std::string>> deferredDeclarations;
    auto generatedElementFieldUpdate = [&](const std::string& line)
        -> std::optional<std::string> {
        // Packed-array field writes are emitted as one read-modify-write lambda.
        // Recover the outer array selection before inspecting the local field write,
        // allowing a projected comb to discard the aggregate seed read entirely.
        size_t outerAssign = std::string::npos;
        size_t outerCursor = std::string::npos;
        size_t scanBegin = 0;
        size_t scanEnd = 0;
        std::string indices;
        auto topAssign = topLevelAssignPos(line);
        if (topAssign != std::string::npos) {
            auto outerLhs = trimCombText(line.substr(0, topAssign));
            if (outerLhs.rfind(base, 0) == 0 &&
                (outerLhs.size() == base.size() || outerLhs[base.size()] == '[')) {
                auto cursor = base.size();
                while (cursor < outerLhs.size() && outerLhs[cursor] == '[') {
                    int depth = 0;
                    auto beginIndex = cursor;
                    for (; cursor < outerLhs.size(); ++cursor) {
                        if (outerLhs[cursor] == '[') ++depth;
                        else if (outerLhs[cursor] == ']' && --depth == 0) {
                            ++cursor;
                            break;
                        }
                    }
                    if (depth != 0) {
                        return std::nullopt;
                    }
                    indices += outerLhs.substr(beginIndex, cursor - beginIndex);
                }
                if (cursor == outerLhs.size()) {
                    outerAssign = topAssign;
                    outerCursor = line.size();
                    scanBegin = topAssign + 1;
                    scanEnd = line.size();
                }
            }
        }
        for (size_t search = 0;
             outerAssign == std::string::npos &&
             (search = line.find(base, search)) != std::string::npos;
             search += base.size()) {
            if ((search > 0 && isIdentifierChar(line[search - 1])) ||
                (search + base.size() < line.size() &&
                 isIdentifierChar(line[search + base.size()]))) {
                continue;
            }
            auto cursor = search + base.size();
            std::string candidateIndices;
            while (cursor < line.size() && line[cursor] == '[') {
                int depth = 0;
                auto beginIndex = cursor;
                for (; cursor < line.size(); ++cursor) {
                    if (line[cursor] == '[') ++depth;
                    else if (line[cursor] == ']' && --depth == 0) {
                        ++cursor;
                        break;
                    }
                }
                if (depth != 0) {
                    return std::nullopt;
                }
                candidateIndices += line.substr(beginIndex, cursor - beginIndex);
            }
            while (cursor < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[cursor]))) {
                ++cursor;
            }
            if (cursor >= line.size() || line[cursor] != '=') {
                continue;
            }
            auto rhsBegin = cursor + 1;
            while (rhsBegin < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[rhsBegin]))) {
                ++rhsBegin;
            }
            auto rhsEnd = line.find(';', rhsBegin);
            if (rhsEnd == std::string::npos ||
                trimCombText(line.substr(rhsBegin, rhsEnd - rhsBegin)) != "__cpphdl_elem") {
                continue;
            }
            outerAssign = cursor;
            outerCursor = search;
            indices = std::move(candidateIndices);
            scanBegin = 0;
            scanEnd = search;
            break;
        }
        if (outerAssign == std::string::npos) {
            return std::nullopt;
        }

        const std::string elementPrefix = "__cpphdl_elem.";
        bool sawElementUpdate = false;
        for (size_t memberPos = scanBegin;
             (memberPos = line.find(elementPrefix, memberPos)) != std::string::npos &&
             memberPos < scanEnd;) {
            sawElementUpdate = true;
            auto memberStart = memberPos + elementPrefix.size();
            auto memberEnd = memberStart;
            while (memberEnd < line.size() &&
                   (isIdentifierChar(line[memberEnd]) || line[memberEnd] == '.')) {
                ++memberEnd;
            }
            auto member = line.substr(memberStart, memberEnd - memberStart);
            auto cursor = memberEnd;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
                ++cursor;
            }
            const bool writesRequestedField =
                member == field || member.rfind(field + ".", 0) == 0;
            const bool writesRequestedAncestor =
                field.rfind(member + ".", 0) == 0;
            if ((!writesRequestedField && !writesRequestedAncestor) ||
                cursor >= line.size() || line[cursor] != '=') {
                memberPos = memberEnd;
                continue;
            }
            auto rhsStart = cursor + 1;
            while (rhsStart < line.size() && std::isspace(static_cast<unsigned char>(line[rhsStart]))) {
                ++rhsStart;
            }
            int paren = 0;
            int bracket = 0;
            int brace = 0;
            auto rhsEnd = rhsStart;
            for (; rhsEnd < line.size(); ++rhsEnd) {
                auto ch = line[rhsEnd];
                if (ch == '(') ++paren;
                else if (ch == ')' && paren > 0) --paren;
                else if (ch == '[') ++bracket;
                else if (ch == ']' && bracket > 0) --bracket;
                else if (ch == '{') ++brace;
                else if (ch == '}' && brace > 0) --brace;
                else if (ch == ';' && paren == 0 && bracket == 0 && brace == 0) break;
            }
            if (rhsEnd >= line.size()) {
                return std::string{};
            }
            auto target = resultName + indices;
            auto projectedRhs = trimCombText(line.substr(rhsStart, rhsEnd - rhsStart));
            if (writesRequestedField && member != field) {
                target += member.substr(field.size());
            }
            else if (writesRequestedAncestor) {
                auto descendant = field.substr(member.size() + 1);
                projectedRhs = projectedRhs == "0" || projectedRhs == "0b0" ||
                               projectedRhs == "0x0" || projectedRhs == "{}"
                    ? std::string("{}")
                    : "(" + projectedRhs + ")." + descendant;
            }
            return target + " = " + projectedRhs + ";";
        }
        return sawElementUpdate
            ? std::optional<std::string>(std::string{})
            : std::nullopt;
    };
    for (size_t i = begin; i < end;) {
        auto text = trimCombText(lines[i]);
        if (inForLoop && isLoopMaintenanceLine(text)) {
            out.push_back(lines[i++]);
            continue;
        }
        if (auto declaration = declarationName(lines[i]); !declaration.empty()) {
            deferredDeclarations.push_back({declaration, lines[i]});
            ++i;
            continue;
        }
        if (auto update = generatedElementFieldUpdate(lines[i]); update.has_value()) {
            if (!update->empty()) {
                out.push_back(std::move(*update));
            }
            ++i;
            continue;
        }
        auto eq = topLevelAssignPos(lines[i]);
        if (eq != std::string::npos) {
            auto lhs = trimCombText(lines[i].substr(0, eq));
            auto accesses = projectedMemberAccesses(lhs, base);
            if (accesses.size() == 1 && accesses.front().begin == 0 &&
                accesses.front().end == lhs.size()) {
                const auto& access = accesses.front();
                if (access.field == field || access.field.rfind(field + ".", 0) == 0) {
                    auto target = resultName + access.indices;
                    if (access.field != field) {
                        target += access.field.substr(field.size());
                    }
                    out.push_back(target + " = " + assignmentRhs(lines[i]) + ";");
                }
                else if (!access.field.empty() && field.rfind(access.field + ".", 0) == 0) {
                    auto rhs = assignmentRhs(lines[i]);
                    auto descendant = field.substr(access.field.size() + 1);
                    auto projectedRhs = rhs == "0" || rhs == "0b0" || rhs == "0x0" || rhs == "{}"
                        ? std::string("{}")
                        : "(" + rhs + ")." + descendant;
                    out.push_back(resultName + access.indices + " = " + projectedRhs + ";");
                }
            }
            else if (lhs.rfind(base, 0) == 0 &&
                     (lhs.size() == base.size() || lhs[base.size()] == '[')) {
                auto cursor = base.size();
                std::string indices;
                while (cursor < lhs.size() && lhs[cursor] == '[') {
                    auto start = cursor;
                    int depth = 0;
                    for (; cursor < lhs.size(); ++cursor) {
                        if (lhs[cursor] == '[') {
                            ++depth;
                        }
                        else if (lhs[cursor] == ']' && --depth == 0) {
                            ++cursor;
                            break;
                        }
                    }
                    indices += lhs.substr(start, cursor - start);
                }
                if (cursor == lhs.size()) {
                    auto rhs = assignmentRhs(lines[i]);
                    auto projectedRhs = rhs == "0" || rhs == "0b0" || rhs == "0x0" || rhs == "{}"
                        ? std::string("{}")
                        : "(" + rhs + ")." + field;
                    out.push_back(resultName + indices + " = " + projectedRhs + ";");
                }
            }
            ++i;
            continue;
        }
        if (isIfChainHeader(text) && text.find('{') != std::string::npos) {
            struct Branch {
                std::string header;
                std::vector<std::string> body;
                std::string close;
            };
            std::vector<Branch> branches;
            size_t next = i;
            bool first = true;
            while (next < end) {
                auto header = trimCombText(lines[next]);
                if ((first && !isIfChainHeader(header)) || (!first && !isElseHeader(header)) ||
                    header.find('{') == std::string::npos) {
                    break;
                }
                auto close = matchingBlockEnd(lines, next);
                if (close <= next || close >= end) {
                    break;
                }
                auto body = extractProjectedArrayFieldCombLinesRange(
                    lines, base, field, resultName, next + 1, close, inForLoop);
                branches.push_back({lines[next], std::move(body), lines[close]});
                next = close + 1;
                first = false;
                if (next >= end || !isElseHeader(trimCombText(lines[next]))) {
                    break;
                }
            }
            bool relevant = std::any_of(branches.begin(), branches.end(),
                                        [](const auto& branch) { return !branch.body.empty(); });
            if (relevant) {
                for (const auto& branch : branches) {
                    out.push_back(branch.header);
                    out.insert(out.end(), branch.body.begin(), branch.body.end());
                    out.push_back(branch.close);
                }
            }
            i = branches.empty() ? i + 1 : next;
            continue;
        }
        if (startsControlBlock(text) && text.find('{') != std::string::npos) {
            auto close = matchingBlockEnd(lines, i);
            if (close > i && close < end) {
                auto body = extractProjectedArrayFieldCombLinesRange(
                    lines, base, field, resultName, i + 1, close,
                    inForLoop || isForHeader(text));
                if (!body.empty()) {
                    out.push_back(lines[i]);
                    out.insert(out.end(), body.begin(), body.end());
                    out.push_back(lines[close]);
                }
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
    bool addedDeclaration = true;
    std::set<size_t> selectedDeclarations;
    while (addedDeclaration) {
        addedDeclaration = false;
        for (size_t index = 0; index < deferredDeclarations.size(); ++index) {
            if (selectedDeclarations.count(index)) {
                continue;
            }
            const auto& declaration = deferredDeclarations[index];
            bool used = std::any_of(out.begin(), out.end(), [&](const std::string& line) {
                return containsIdentifier(line, declaration.first);
            });
            if (!used) {
                for (auto selected : selectedDeclarations) {
                    if (containsIdentifier(deferredDeclarations[selected].second, declaration.first)) {
                        used = true;
                        break;
                    }
                }
            }
            if (used) {
                selectedDeclarations.insert(index);
                addedDeclaration = true;
            }
        }
    }
    std::vector<std::string> declarations;
    for (size_t index = 0; index < deferredDeclarations.size(); ++index) {
        if (selectedDeclarations.count(index)) {
            declarations.push_back(deferredDeclarations[index].second);
        }
    }
    out.insert(out.begin(), declarations.begin(), declarations.end());
    return out;
}

inline std::vector<std::string> extractProjectedArrayFieldCombLines(
    const std::vector<std::string>& lines,
    const std::string& base,
    const std::string& field,
    const std::string& resultName)
{
    return extractProjectedArrayFieldCombLinesRange(
        lines, base, field, resultName, 0, lines.size());
}

inline std::set<std::string> controlCoupledCombVariables(const std::vector<std::string>& lines,
                                                         const std::vector<std::string>& variables)
{
    std::set<std::string> variableSet(variables.begin(), variables.end());
    std::set<std::string> coupled;
    std::vector<std::set<std::string>> blockAssigned;

    auto finishBlock = [&]() {
        if (blockAssigned.empty()) {
            return;
        }
        auto assigned = blockAssigned.back();
        blockAssigned.pop_back();
        if (assigned.size() > 1) {
            coupled.insert(assigned.begin(), assigned.end());
        }
        if (!blockAssigned.empty()) {
            blockAssigned.back().insert(assigned.begin(), assigned.end());
        }
    };

    for (const auto& line : lines) {
        auto base = assignmentBase(line);
        if (!base.empty() && variableSet.count(base) && !blockAssigned.empty()) {
            for (auto& block : blockAssigned) {
                block.insert(base);
            }
        }

        for (char c : line) {
            if (c == '{') {
                blockAssigned.emplace_back();
            }
            else if (c == '}') {
                finishBlock();
            }
        }
    }
    while (!blockAssigned.empty()) {
        finishBlock();
    }
    return coupled;
}

inline CombExtractionPlan planCombExtraction(const std::vector<std::string>& lines,
                                             const std::vector<std::string>& variables)
{
    CombExtractionPlan plan;
    for (const auto& variable : variables) {
        if (canExtractIndependentComb(lines, variables, variable)) {
            plan.independent.push_back(variable);
        }
        else {
            plan.independent.push_back(variable);
        }
    }
    return plan;
}

} // namespace hdlcpp

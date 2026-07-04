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

inline std::string localCombNameFor(const std::string& base)
{
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

inline bool parseBaseFieldLValue(const std::string& lhs, const std::string& base,
                                 std::string& indexExpr, std::string& field)
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
    indexExpr.clear();
    field.clear();
    if (pos < text.size() && text[pos] == '[') {
        int depth = 0;
        auto begin = pos + 1;
        size_t end = std::string::npos;
        for (; pos < text.size(); ++pos) {
            if (text[pos] == '[') {
                ++depth;
            }
            else if (text[pos] == ']') {
                --depth;
                if (depth == 0) {
                    end = pos;
                    break;
                }
            }
        }
        if (end == std::string::npos) {
            return false;
        }
        indexExpr = trimCombText(text.substr(begin, end - begin));
        pos = end + 1;
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
        if (pos < text.size() && text[pos] == '[') {
            int depth = 0;
            auto begin = pos + 1;
            size_t end = std::string::npos;
            for (; pos < text.size(); ++pos) {
                if (text[pos] == '[') {
                    ++depth;
                }
                else if (text[pos] == ']') {
                    --depth;
                    if (depth == 0) {
                        end = pos;
                        break;
                    }
                }
            }
            if (end == std::string::npos) {
                return false;
            }
            if (indexExpr.empty()) {
                indexExpr = trimCombText(text.substr(begin, end - begin));
            }
            pos = end + 1;
        }
    }
    field = path;
    return !field.empty();
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
            std::string lhsIndex;
            std::string lhsField;
            if (parseBaseFieldLValue(lhs, base, lhsIndex, lhsField) && lhsField == field) {
                auto rhs = assignmentRhs(lines[i]);
                auto assign = resultName + " = " + rhs + ";";
                if (!indexName.empty() && !lhsIndex.empty()) {
                    out.push_back("if ((uint64_t)(" + lhsIndex + ") == (uint64_t)(" + indexName + ")) {");
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
                else if (rhs.find("_comb_func()") != std::string::npos ||
                         rhs.find("sv_cast<") != std::string::npos) {
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
    return extractTargetFieldCombLinesRange(lines, base, field, resultName, indexName, 0, lines.size());
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

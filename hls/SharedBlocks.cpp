#include "SharedBlocks.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace cpphdl::hls {
namespace {
bool identifierStart(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$'; }
bool identifierChar(unsigned char c) { return identifierStart(c) || std::isdigit(c); }

struct Normalizer {
    const std::map<std::string, BlockSymbol>& symbols;
    std::map<std::string, size_t> positions;
    std::vector<BlockSymbol> parameters;
    std::vector<std::string> arguments;
    std::ostringstream key;
    const std::map<std::string, std::pair<std::string, std::string>>* callPrefixes = nullptr;
    const std::map<std::string, const SharedBlock*>* valueFunctions = nullptr;

    std::string rewrite(const std::string& text) {
        std::string result;
        for (size_t i = 0; i < text.size();) {
            size_t first = i;
            if (text[i] == '"') {
                for (++i; i < text.size();) {
                    char c = text[i++];
                    if (c == '\\' && i < text.size()) ++i;
                    else if (c == '"') break;
                }
            } else if (text.compare(i, 2, "//") == 0) {
                auto end = text.find('\n', i);
                i = end == std::string::npos ? text.size() : end;
            } else if (text.compare(i, 2, "/*") == 0) {
                auto end = text.find("*/", i + 2);
                i = end == std::string::npos ? text.size() : end + 2;
            } else if (identifierStart(text[i])) {
                while (i < text.size() && identifierChar(text[i])) ++i;
                // Scratch members are registered as qualified symbols. Do not
                // replace substrings of other identifiers or member paths.
                while (i + 1 < text.size() && text[i] == '.' && identifierStart(text[i + 1])) {
                    ++i;
                    while (i < text.size() && identifierChar(text[i])) ++i;
                }
                auto token = text.substr(first, i - first);
                if (valueFunctions && i < text.size() && text[i] == '(') {
                    auto function = valueFunctions->find(token);
                    if (function != valueFunctions->end()) {
                        // Generated outlined calls have identifier arguments;
                        // retain balanced expressions for numeric successor IDs.
                        std::vector<std::string> arguments;
                        size_t begin = ++i;
                        unsigned depth = 1;
                        for (; i < text.size(); ++i) {
                            if (text[i] == '(') ++depth;
                            if (text[i] == ')') --depth;
                            if (depth == 0 || (depth == 1 && text[i] == ',')) {
                                auto arg = text.substr(begin, i - begin);
                                auto start = arg.find_first_not_of(" \t\n");
                                if (start != std::string::npos)
                                    arguments.push_back(arg.substr(start, arg.find_last_not_of(" \t\n") - start + 1));
                                begin = i + 1;
                                if (depth == 0) { ++i; break; }
                            }
                        }
                        auto call = valueFunctionCall(*function->second, arguments);
                        result += call; key << call; continue;
                    }
                }
                if (callPrefixes) {
                    auto call = callPrefixes->find(token);
                    if (call != callPrefixes->end()) {
                        const auto& [before, after] = call->second;
                        if (text.compare(first, before.size(), before) == 0) {
                            result += after; key << after; i = first + before.size(); continue;
                        }
                    }
                }
                auto found = symbols.find(token);
                if (found != symbols.end()) {
                    auto [position, inserted] = positions.emplace(token, parameters.size());
                    size_t index = position->second;
                    if (inserted) {
                        auto parameter = found->second;
                        parameter.name = "p_" + parameter.name + "_" + std::to_string(index);
                        parameters.push_back(std::move(parameter));
                        arguments.push_back(token);
                    }
                    result += parameters[index].name;
                    key << "@" << index << ";";
                    continue;
                }
            } else ++i;
            auto token = text.substr(first, i - first);
            result += token;
            key << token;
        }
        key << '\n';
        return result;
    }
};
}

BlockCall BlockSharing::add(const ScheduledBlock& block, const std::set<std::string>* liveOutputs) {
    Normalizer normalize{symbols};
    SharedBlock shared;
    shared.body = block;
    for (auto& statement : shared.body.statements) statement = normalize.rewrite(statement);
    shared.body.condition = normalize.rewrite(block.condition);
    if (liveOutputs)
        for (size_t i = 0; i < normalize.parameters.size(); ++i)
            normalize.parameters[i].writable &= liveOutputs->count(normalize.arguments[i]) != 0;
    normalize.key << '\n' << block.method << ':' << (block.yes >= 0) << ':' << (block.no >= 0) << ':' << block.suspend << ':' << block.combinational << ':' << block.memoryBoundary << ':' << block.recursiveBoundary << ':' << block.sharedCallBoundary;
    for (const auto& parameter : normalize.parameters)
        normalize.key << ':' << parameter.width << ':' << parameter.writable;
    auto [found, inserted] = keys.emplace(normalize.key.str(), functions.size());
    if (inserted) {
        shared.body.name = block.method + "__shared_" + std::to_string(found->second);
        shared.parameters = std::move(normalize.parameters);
        functions.push_back(std::move(shared));
    }
    ++functions[found->second].uses;
    return {found->second, std::move(normalize.arguments)};
}

SharedBlocks shareBlocks(const std::vector<ScheduledBlock>& blocks,
                         const std::map<std::string, BlockSymbol>& symbols) {
    SharedBlocks result;
    BlockSharing sharing(symbols);
    for (const auto& block : blocks) result.calls.push_back(sharing.add(block));
    result.functions = std::move(sharing.functions);
    return result;
}

void pruneFunctionState(SharedBlocks& shared, const std::vector<std::string>& storage) {
    std::vector<std::string> methodState = storage;
    methodState.insert(methodState.end(), {"fault", "heap_next", "operation_in", "index_in", "value_in"});
    std::vector<std::string> continuationState = storage;
    continuationState.insert(continuationState.end(), {"operation_in", "index_in", "value_in", "phase", "fault",
        "result", "heap_next", "pending", "booting", "active", "next_block", "else_block"});
    auto prefix = [](const SharedBlock& function, const std::vector<std::string>& state) {
        std::string text = function.body.name + "(";
        for (const auto& name : state) { if (text.back() != '(') text += ", "; text += name; }
        if (function.parameters.empty()) text += ")";
        else if (!state.empty()) text += ", ";
        return text;
    };
    std::map<std::string, std::pair<std::string, std::string>> substitutions;
    const std::map<std::string, BlockSymbol> noSymbols;
    std::map<std::string, const SharedBlock*> callees;
    for (auto& function : shared.functions) {
        auto& body = function.body;
        const auto& state = body.combinational ? methodState : continuationState;
        // Outlining creates callees before callers. Prefixes are generated by
        // outlineCall, not parsed from arbitrary user SystemVerilog.
        Normalizer calls{noSymbols};
        calls.callPrefixes = &substitutions;
        for (auto& text : body.statements) text = calls.rewrite(text);
        std::map<std::string, BlockSymbol> symbols;
        for (const auto& name : state) symbols.emplace(name, BlockSymbol{0, name, false});
        Normalizer used{symbols};
        for (const auto& text : body.statements) used.rewrite(text);
        used.rewrite(body.condition);
        if (!body.combinational) {
            if (!body.statements.empty() || body.yes >= 0) used.rewrite("fault");
            if (body.yes >= 0) used.rewrite(body.suspend ? "phase next_block" : "active next_block");
            if (body.no >= 0) used.rewrite("active else_block");
        }
        function.stateArguments.clear();
        for (const auto& name : state)
            if (used.positions.count(name)) function.stateArguments.push_back(name);
        std::set<std::string> written;
        for (const auto& name : state) {
            // Common-state lvalues have a fixed generator-owned form. A
            // spurious match in a comment only keeps an unnecessary output.
            std::regex assignment("\\b" + name + "\\s*(\\[[^]]+\\])?\\s*=[^=]");
            std::regex packedAssignment("\\{[^};]*\\b" + name + "\\b[^};]*\\}\\s*=");
            for (const auto& text : body.statements)
                if (std::regex_search(text, assignment) || std::regex_search(text, packedAssignment)) written.insert(name);
        }
        std::map<std::string, BlockSymbol> calleeSymbols;
        for (const auto& [name, _] : callees) calleeSymbols.emplace(name, BlockSymbol{0, name, false});
        Normalizer called{calleeSymbols};
        for (const auto& text : body.statements) called.rewrite(text);
        for (const auto& [name, _] : called.positions)
            for (auto i : functionOutputs(*callees.at(name)))
                if (i < callees.at(name)->stateArguments.size()) written.insert(callees.at(name)->stateArguments[i]);
        if (body.yes >= 0) written.insert(body.suspend ? "phase" : "active");
        if (body.no >= 0) written.insert("active");
        function.readOnlyState.clear();
        for (const auto& name : function.stateArguments)
            if (!written.count(name)) function.readOnlyState.insert(name);
        substitutions.emplace(body.name, std::make_pair(prefix(function, state), prefix(function, function.stateArguments)));
        callees.emplace(body.name, &function);
    }
}

std::vector<size_t> functionOutputs(const SharedBlock& function) {
    std::vector<size_t> outputs;
    for (size_t i = 0; i < function.stateArguments.size(); ++i) {
        const auto& name = function.stateArguments[i];
        if (name != "operation_in" && name != "index_in" && name != "value_in" &&
            name != "next_block" && name != "else_block" && !function.readOnlyState.count(name)) outputs.push_back(i);
    }
    for (size_t i = 0; i < function.parameters.size(); ++i)
        if (function.parameters[i].writable) outputs.push_back(function.stateArguments.size() + i);
    return outputs;
}

std::string valueFunctionCall(const SharedBlock& function, const std::vector<std::string>& arguments) {
    auto outputs = functionOutputs(function);
    auto separator = [](size_t i) { return i % 8 == 0 ? ",\n        " : ", "; };
    std::string text = function.body.name + "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i) text += separator(i);
        text += arguments[i];
    }
    if (!arguments.empty()) text += ", ";
    text += "'0)";
    if (!outputs.empty()) {
        std::string targets = "{";
        for (size_t i = 0; i < outputs.size(); ++i) {
            if (i) targets += separator(i);
            targets += arguments.at(outputs[i]);
        }
        targets += "}";
        return "hls_call_result = CALL_RESULT_BITS'(" + text + "); " +
            targets + " = $bits(" + targets + ")'(hls_call_result)";
    }
    return text;
}

void lowerValueFunctionCalls(SharedBlocks& shared) {
    std::map<std::string, const SharedBlock*> functions;
    for (const auto& function : shared.functions) functions.emplace(function.body.name, &function);
    const std::map<std::string, BlockSymbol> noSymbols;
    for (auto& function : shared.functions) {
        Normalizer calls{noSymbols};
        calls.valueFunctions = &functions;
        for (auto& statement : function.body.statements) statement = calls.rewrite(statement);
    }
}

size_t callResultScratchWidth(const ScheduledBlock& body,
                             const std::map<std::string, size_t>& resultWidths) {
    std::map<std::string, BlockSymbol> symbols;
    for (const auto& [name, width] : resultWidths) symbols.emplace(name, BlockSymbol{0, name, false});
    Normalizer referenced{symbols};
    for (const auto& text : body.statements) referenced.rewrite(text);
    size_t width = 1;
    for (const auto& [name, position] : referenced.positions) width = std::max(width, resultWidths.at(name));
    return width;
}
}

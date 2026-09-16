#include "SharedBlocks.h"
#include <cctype>
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

BlockCall BlockSharing::add(const ScheduledBlock& block) {
    Normalizer normalize{symbols};
    SharedBlock shared;
    shared.body = block;
    for (auto& statement : shared.body.statements) statement = normalize.rewrite(statement);
    shared.body.condition = normalize.rewrite(block.condition);
    normalize.key << '\n' << block.method << ':' << (block.yes >= 0) << ':' << (block.no >= 0) << ':' << block.suspend << ':' << block.combinational;
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
}

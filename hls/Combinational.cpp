#include "Combinational.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>

namespace cpphdl::hls {
namespace {
using Copies = std::map<std::string, std::string>;
using Symbols = std::map<std::string, BlockSymbol>;

// These are generated expressions, not C++ source. Only exact registered
// scalar identifiers are candidates; casts, calls and field offsets stay intact.
std::string symbolsIn(const std::string& text, const Symbols& symbols,
                      const std::function<std::string(const std::string&)>& replace) {
    std::string result;
    for (size_t i = 0; i < text.size();) {
        size_t start = i;
        if (text[i] == '"') {
            for (++i; i < text.size();) {
                char c = text[i++];
                if (c == '\\' && i < text.size()) ++i;
                else if (c == '"') break;
            }
        } else if (text.compare(i, 2, "//") == 0 || text.compare(i, 2, "/*") == 0) {
            bool line = text[i + 1] == '/';
            auto end = text.find(line ? "\n" : "*/", i + 2);
            i = end == std::string::npos ? text.size() : end + (line ? 0 : 2);
        } else if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_' || text[i] == '.')) ++i;
            auto name = text.substr(start, i - start);
            if (symbols.count(name)) { result += replace(name); continue; }
        } else ++i;
        result += text.substr(start, i - start);
    }
    return result;
}

bool assignment(const std::string& statement, const Symbols& symbols, std::string& target, std::string& rhs) {
    auto equal = statement.find(" = ");
    if (equal == std::string::npos || statement.back() != ';') return false;
    target = statement.substr(0, equal);
    if (!symbols.count(target) || !symbols.at(target).writable) return false;
    rhs = statement.substr(equal + 3, statement.size() - equal - 4);
    return true;
}

std::string copySource(const std::string& target, const std::string& rhs, const Symbols& symbols) {
    auto prefix = std::to_string(symbols.at(target).width) + "'(";
    std::string source = rhs;
    if (rhs.compare(0, prefix.size(), prefix) == 0 && rhs.back() == ')')
        source = rhs.substr(prefix.size(), rhs.size() - prefix.size() - 1);
    auto found = symbols.find(source);
    return found != symbols.end() && found->second.width == symbols.at(target).width ? source : "";
}

void propagateCopies(std::vector<ScheduledBlock>& blocks, int entry, int exit,
                     const std::map<int, std::set<int>>& region, const Symbols& symbols,
                     const std::set<std::string>& preserved, BlockUses* index) {
    std::map<int, unsigned> incoming;
    std::map<int, Copies> inputs;
    for (const auto& [n, _] : region) if (n != exit)
        for (int next : {blocks[n].yes, blocks[n].no}) if (next >= 0) ++incoming[next];
    std::set<int> ready{entry};
    while (!ready.empty()) {
        int n = *ready.begin(); ready.erase(n);
        if (n == exit) continue;
        auto copies = inputs[n];
        auto rewrite = [&](const std::string& text) {
            return symbolsIn(text, symbols, [&](const std::string& name) {
                auto it = copies.find(name); return it == copies.end() ? name : it->second;
            });
        };
        for (auto& s : blocks[n].statements) {
            std::string target, rhs;
            if (!assignment(s, symbols, target, rhs)) {
                // An outlined call can modify its inout arguments. Do not
                // substitute aliases into that call or keep them afterwards.
                copies.clear(); continue;
            }
            rhs = rewrite(rhs);
            s = target + " = " + rhs + ";";
            for (auto it = copies.begin(); it != copies.end();)
                if (it->first == target || it->second == target) it = copies.erase(it); else ++it;
            auto source = copySource(target, rhs, symbols);
            if (!source.empty() && source != target) copies[target] = source;
        }
        blocks[n].condition = rewrite(blocks[n].condition);
        for (int next : {blocks[n].yes, blocks[n].no}) if (next >= 0) {
            auto [it, first] = inputs.emplace(next, copies);
            if (!first) {
                for (auto item = it->second.begin(); item != it->second.end();)
                    if (!copies.count(item->first) || copies.at(item->first) != item->second)
                        item = it->second.erase(item);
                    else ++item;
            }
            if (--incoming[next] == 0) ready.insert(next);
        }
    }

    // Delete only copies whose destination is no longer read, including by
    // callers, another branch, or a clocked value. Other operations can fault.
    std::set<std::string> outside = preserved;
    auto collect = [&](const std::string& text, std::set<std::string>& into) {
        symbolsIn(text, symbols, [&](const std::string& name) { into.insert(name); return name; });
    };
    if (index) {
        auto external = index->outside(region, exit);
        outside.insert(external.begin(), external.end());
    } else {
        for (size_t n = 0; n < blocks.size(); ++n) if (int(n) == exit || !region.count(n)) {
            for (const auto& s : blocks[n].statements) collect(s, outside);
            collect(blocks[n].condition, outside);
        }
    }
    bool changed;
    do {
        auto used = outside;
        for (const auto& [n, _] : region) if (n != exit) {
            for (const auto& s : blocks[n].statements) {
                std::string target, rhs;
                collect(assignment(s, symbols, target, rhs) ? rhs : s, used);
            }
            collect(blocks[n].condition, used);
        }
        changed = false;
        for (const auto& [n, _] : region) if (n != exit) {
            auto& statements = blocks[n].statements;
            statements.erase(std::remove_if(statements.begin(), statements.end(), [&](const std::string& s) {
                std::string target, rhs;
                if (!assignment(s, symbols, target, rhs)) return false;
                auto source = copySource(target, rhs, symbols);
                bool remove = !source.empty() && (!used.count(target) || source == target);
                changed |= remove; return remove;
            }), statements.end());
        }
    } while (changed);
}
}

BlockUses::BlockUses(const std::vector<ScheduledBlock>& blocks, const Symbols& symbols)
    : symbols(symbols), names(blocks.size()) {
    for (size_t i = 0; i < blocks.size(); ++i) update(i, blocks[i]);
}

void BlockUses::update(int index, const ScheduledBlock& block) {
    for (const auto& name : names[index]) users[name].erase(index);
    names[index].clear();
    auto collect = [&](const std::string& text) {
        symbolsIn(text, symbols, [&](const std::string& name) {
            names[index].insert(name); users[name].insert(index); return name;
        });
    };
    for (const auto& s : block.statements) collect(s);
    collect(block.condition);
}

std::set<std::string> BlockUses::outside(const std::map<int, std::set<int>>& region, int exit) const {
    std::set<std::string> candidates, result;
    for (const auto& [n, _] : region) if (n != exit)
        candidates.insert(names[n].begin(), names[n].end());
    for (const auto& name : candidates)
        for (int user : users.at(name)) if (user == exit || !region.count(user)) {
            result.insert(name); break;
        }
    return result;
}

bool outlineCall(std::vector<ScheduledBlock>& blocks, int entry, int exit,
                 const std::string& callerMethod, BlockSharing& sharing,
                 const std::set<std::string>& preserved, BlockUses* uses) {
    std::map<int, std::set<int>> postdominators;
    std::set<int> visiting;
    postdominators[exit] = {exit};
    std::function<bool(int)> visit = [&](int n) {
        if (postdominators.count(n)) return true;
        if (n < 0 || blocks[n].suspend || !visiting.insert(n).second) return false;
        const auto& b = blocks[n];
        if (!visit(b.yes) || (b.no >= 0 && !visit(b.no))) return false;
        auto common = postdominators.at(b.yes);
        if (b.no >= 0) {
            std::set<int> intersection;
            const auto& other = postdominators.at(b.no);
            std::set_intersection(common.begin(), common.end(), other.begin(), other.end(),
                                  std::inserter(intersection, intersection.end()));
            common = std::move(intersection);
        }
        common.insert(n);
        postdominators[n] = std::move(common);
        visiting.erase(n);
        return true;
    };
    if (entry == exit || !visit(entry)) return false;
    propagateCopies(blocks, entry, exit, postdominators, sharing.symbolTable(), preserved, uses);

    // Restore structured branches at their nearest common postdominator. A
    // join is emitted once, after both arms, rather than copied into each arm.
    std::function<std::string(int, int, unsigned)> render = [&](int n, int stop, unsigned depth) {
        std::ostringstream out;
        std::string pad(depth * 2, ' ');
        while (n != stop) {
            const auto& b = blocks[n];
            for (const auto& s : b.statements) out << pad << "if (fault == 0) begin " << s << " end\n";
            if (b.no >= 0) {
                int join = exit;
                for (int candidate : postdominators.at(b.yes))
                    if (postdominators.at(b.no).count(candidate) &&
                        postdominators.at(candidate).size() > postdominators.at(join).size()) join = candidate;
                out << pad << "if (fault == 0) begin\n" << pad << "  if (" << b.condition << ") begin\n"
                    << render(b.yes, join, depth + 2) << pad << "  end else begin\n"
                    << render(b.no, join, depth + 2) << pad << "  end\n" << pad << "end\n";
                n = join;
            } else n = b.yes;
        }
        return out.str();
    };
    ScheduledBlock body = blocks[entry];
    body.statements = {render(entry, exit, 3)};
    body.condition.clear(); body.yes = body.no = -1;
    body.combinational = true;
    auto call = sharing.add(body);
    std::string invocation = sharing.functions[call.function].body.name +
        "(storage, fault, heap_next, operation_in, index_in, value_in";
    for (const auto& argument : call.arguments) invocation += ", " + argument;
    invocation += ");";
    auto& wrapper = blocks[entry];
    wrapper.statements = {std::move(invocation)};
    wrapper.condition.clear(); wrapper.yes = exit; wrapper.no = -1;
    wrapper.method = callerMethod;
    if (uses) for (const auto& [n, _] : postdominators) if (n != exit) uses->update(n, blocks[n]);
    return true;
}

void coalesceBlocks(std::vector<ScheduledBlock>& blocks, int resetEntry, int commandEntry) {
    bool changed;
    do {
        changed = false;
        std::set<int> live;
        std::vector<unsigned> incoming(blocks.size());
        std::function<void(int)> visit = [&](int n) {
            if (n < 0 || !live.insert(n).second) return;
            for (int next : {blocks[n].yes, blocks[n].no}) if (next >= 0) {
                ++incoming[next]; visit(next);
            }
        };
        visit(resetEntry); visit(commandEntry);
        for (int n : live) {
            auto& b = blocks[n];
            int next = b.yes;
            if (b.suspend || !b.condition.empty() || b.no >= 0 || next < 0 || next == n ||
                next == resetEntry || next == commandEntry || incoming[next] != 1) continue;
            auto& following = blocks[next];
            b.statements.insert(b.statements.end(), following.statements.begin(), following.statements.end());
            b.condition = following.condition; b.yes = following.yes; b.no = following.no;
            b.suspend = following.suspend;
            following = {};
            changed = true;
            break;
        }
    } while (changed);
}

}

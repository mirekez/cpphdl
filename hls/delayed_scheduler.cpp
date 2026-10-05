#include "delayed_scheduler.h"
#include "SharedBlocks.h"
#include "Combinational.h"
#include "StorageBanks.h"
#include "BlockRam.h"
#include "FunctionOverrides.h"
#include "MemoryEffects.h"
#include "../synth/ScheduledGraph.h"
#include "../Module.h"
#include "../Field.h"
#include "../Method.h"
#include "../Project.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/Builtins.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace cpphdl::hls {
namespace {
using namespace clang;
using E = clang::Expr;

struct Value {
    std::string text;
    QualType type;
    bool location = false;
    std::string name;
};
using Block = ScheduledBlock;
// Conversion-time bindings for one statically elaborated source call.
struct FunctionScope {
    std::map<const ValueDecl*, Value> variables;
    std::string self;
    Value result;
    int continuation = -1;
    bool referenceResult = false;
    const VarDecl* nrvo = nullptr;
    std::vector<std::vector<Value>> scopes;
    std::vector<std::pair<std::string, size_t>> referenceReturns;
    std::map<const LabelDecl*, int> labels;
    std::set<const LabelDecl*> visitedLabels;
    std::vector<std::vector<Value>> temporaryCleanups;
};

bool runtimeCall(const Stmt* stmt) {
    if (!stmt) return false;
    if (isa<CallExpr>(stmt) || isa<CXXConstructExpr>(stmt)) return true;
    for (const auto* child : stmt->children()) if (child && runtimeCall(child)) return true;
    return false;
}

class DelayedScheduler {
    ASTContext& ctx;
    Sema& sema;
    FunctionOverrides overrides;
    std::set<const FunctionDecl*> replacing;
    std::vector<Block> blocks;
    std::vector<FunctionScope> contexts;
    struct CallRegion { int entry, exit; std::string callerMethod; };
    std::vector<CallRegion> callRegions;
    std::map<const FunctionDecl*, unsigned> calling;
    struct RecursiveBody {
        std::vector<Value> parameters;
        Value result, caller;
        int entry, dispatch;
        unsigned callers = 0;
    };
    using EnclosingDepths = std::vector<std::pair<const FunctionDecl*, unsigned>>;
    std::map<std::tuple<const FunctionDecl*, unsigned, std::string, EnclosingDepths>, RecursiveBody> recursiveBodies;
    decltype(recursiveBodies) clockedBodies;
    std::map<const FunctionDecl*, bool> recursiveFunctions;
    std::map<const VarDecl*, Value> constants;
    struct Loop { int first, second; size_t scopes; };
    std::vector<Loop> loops;
    std::vector<std::string> traces;
    std::map<const FieldDecl*, std::string> fieldOffsets;
    std::vector<std::string> layouts;
    unsigned highWater = 4112, constantBytes = 16, tempCount = 0;
    unsigned recursionLimit = 0;
    unsigned addressWidth = 16;
    unsigned heapBytes = 4096;
    unsigned callCount = 0, symbolCount = 0;
    std::string scope = "hls_object", method = "hls_object";
    std::map<std::string, BlockSymbol> blockSymbols;
    std::map<std::string, std::string> symbolLabels;
    std::set<unsigned> storageReadWidths, storageWriteWidths;
    bool heap = false;
    bool sharedMemory = false;
    bool blockRam = false;
    bool pipelineMode = false;
    unsigned portBytes = 1;
    int current = 0;
    std::ostringstream symbols;
    struct SourceObject {
        QualType type;
        std::string name, label;
        bool memory = false;
    };
    struct Access {
        std::string address, data;
        QualType type;
        bool write = false, checked = true;
    };
    std::map<std::string, SourceObject> objects;
    struct ObjectLocation { std::string object; int64_t offset; };
    std::map<std::string, ObjectLocation> locations;
    std::string persistentObject;
    std::vector<Access> accesses;
    std::map<int, std::vector<size_t>> sourceWrites;
    std::set<size_t> elidedAccesses;
    std::set<std::string> clockedValues;
    MemoryEffects effects;
    struct TemporaryAssignment { std::string target, source; unsigned width; };
    std::map<std::string, TemporaryAssignment> temporaryAssignments;
    unsigned effectSerial = 0, reusedLoads = 0;

    std::vector<std::string> memoryState() const {
        return sharedMemory ? std::vector<std::string>{"memory_address", "memory_write_data", "memory_size",
            "memory_write", "memory_read", "memory_read_data"} : storageBankNames();
    }
    std::string memoryArguments() const {
        std::string result;
        for (const auto& name : memoryState()) { if (!result.empty()) result += ", "; result += name; }
        return result;
    }

    static std::string identifier(const std::string& source) {
        std::string result;
        for (unsigned char ch : source)
            result += std::isalnum(ch) || ch == '_' ? char(ch) : '_';
        return result.empty() ? "unnamed" : result;
    }
    static std::string qualifiedIdentifier(const NamedDecl* decl) {
        std::string name = identifier(decl->getNameAsString());
        for (auto* parent = decl->getDeclContext(); parent; parent = parent->getParent())
            if (auto* named = dyn_cast<NamedDecl>(parent))
                name = identifier(named->getNameAsString()) + "__" + name;
        return name;
    }
    std::string addressLiteral(unsigned address) const {
        return std::to_string(addressWidth) + "'d" + std::to_string(address);
    }
    std::string addressCast(const std::string& value) const {
        return std::to_string(addressWidth) + "'(" + value + ")";
    }
    std::string addressSymbol(const std::string& name, unsigned address) {
        std::string symbol = name + "_" + std::to_string(symbolCount++);
        symbols << "  localparam logic [" << addressWidth - 1 << ":0] " << symbol << " = " << addressLiteral(address) << ";\n";
        graphConstants[symbol] = graph::constant(address, addressWidth);
        return symbol;
    }

    [[noreturn]] void reject(const Stmt* stmt, const std::string& why) {
        std::string where = stmt ? stmt->getBeginLoc().printToString(ctx.getSourceManager()) : "object layout";
        throw std::runtime_error(where + ": " + why + (stmt ? " (" + std::string(stmt->getStmtClassName()) + ")" : ""));
    }
    unsigned bytes(QualType type) {
        type = type.getNonReferenceType();
        if (type->isVoidType()) return 0;
        if (type->isDependentType() || type->isIncompleteType()) reject(nullptr, "incomplete or dependent type " + type.getAsString());
        return ctx.getTypeSizeInChars(type).getQuantity();
    }
    unsigned bits(QualType type) {
        if (type->isReferenceType() || type->isPointerType()) return addressWidth;
        if (type->isBooleanType()) return 1;
        return bytes(type) * 8;
    }
    Value slot(QualType type, const std::string& source = "temporary") {
        std::string name = scope + "__" + identifier(source);
        std::string id = std::to_string(symbolCount++);
        Value result{name + "_addr_" + id, type.getNonReferenceType(), true, name};
        objects.emplace(result.text, SourceObject{result.type, name + "_" + id, identifier(source), false});
        locations.emplace(result.text, ObjectLocation{result.text, 0});
        symbolLabels[name] = identifier(source);
        return result;
    }
    std::string offsetAddress(const std::string& base, int64_t offset, const std::string& symbol = "") {
        auto text = "(" + base + (offset < 0 ? " - " : " + ") +
            (symbol.empty() ? addressLiteral(unsigned(offset < 0 ? -offset : offset)) : symbol) + ")";
        auto found = locations.find(base);
        if (found != locations.end()) locations[text] = {found->second.object, found->second.offset + offset};
        return text;
    }
    const ObjectLocation* directLocation(const Access& item) {
        auto found = locations.find(item.address);
        if (found == locations.end()) return nullptr;
        const auto& location = found->second;
        auto objectType = objects.at(location.object).type;
        if (location.offset < 0 || uint64_t(location.offset) + bytes(item.type) > bytes(objectType))
            return nullptr;
        if (objectType->isScalarType() && !(location.offset == 0 && bits(objectType) == bits(item.type)) &&
            uint64_t(location.offset) * 8 + bytes(item.type) * 8 > bits(objectType)) return nullptr;
        return &location;
    }
    std::string access(Access value) {
        if (value.write) sourceWrites[current].push_back(accesses.size());
        accesses.push_back(std::move(value));
        return "@hls_access_" + std::to_string(accesses.size() - 1) + "@";
    }
    void exposeAddresses(const std::string& text) {
        for (size_t i = 0; i < text.size();) {
            if (!std::isalpha(static_cast<unsigned char>(text[i])) && text[i] != '_') { ++i; continue; }
            size_t begin = i++;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
            auto found = objects.find(text.substr(begin, i - begin));
            if (found != objects.end()) found->second.memory = true;
        }
    }
    std::string resolveAccesses(const std::string& text, std::set<std::string>& uses, std::set<std::string>& definitions,
                               std::vector<std::string>& prefix) {
        std::string result;
        size_t cursor = 0;
        while (cursor < text.size()) {
            size_t begin = text.find("@hls_access_", cursor);
            if (begin == std::string::npos) { result += text.substr(cursor); break; }
            result += text.substr(cursor, begin - cursor);
            size_t end = text.find('@', begin + 12);
            if (end == std::string::npos) reject(nullptr, "unterminated source access handle");
            auto index = std::stoul(text.substr(begin + 12, end - begin - 12));
            if (elidedAccesses.count(index)) { cursor = end + 1; continue; }
            const auto& item = accesses.at(index);
            auto location = directLocation(item);
            auto object = location ? objects.find(location->object) : objects.end();
            auto address = resolveAccesses(item.address, uses, definitions, prefix);
            auto data = resolveAccesses(item.data, uses, definitions, prefix);
            bool local = object != objects.end() && !object->second.memory;
            MemoryEffects::Address key{local ? effects.atom(location->object) : effects.expression(address, addressWidth),
                local ? location->offset : 0, bytes(item.type), bits(item.type), local};
            bool reusable = !item.type.isVolatileQualified();
            if (!reusable) effects.state = {};
            if (!local && !item.write && reusable) {
                if (auto* available = effects.find(key)) {
                    result += available->expression;
                    ++reusedLoads;
                    cursor = end + 1;
                    continue;
                }
            }
            if (item.write) effects.write(key);
            if (object != objects.end() && !object->second.memory) {
                auto name = "values." + object->second.name;
                unsigned width = bits(object->second.type);
                unsigned accessWidth = object->second.type->isScalarType() && location->offset == 0 &&
                    bits(object->second.type) == bits(item.type) ? width : bytes(item.type) * 8;
                unsigned low = unsigned(location->offset) * 8;
                bool whole = low == 0 && accessWidth == width;
                auto selected = whole ? name : name + "[" + std::to_string(low) + " +: " + std::to_string(accessWidth) + "]";
                if (item.write) {
                    result += selected + " = " + std::to_string(accessWidth) + "'(" + castTo(item.type, data) + ");";
                    if (reusable) effects.remember(key, effects.expression(castTo(item.type, data), bits(item.type)), "");
                    if (whole) definitions.insert(name);
                    else if (!definitions.count(name)) uses.insert(name);
                } else {
                    if (!definitions.count(name)) uses.insert(name);
                    auto* known = reusable ? effects.find(key) : nullptr;
                    auto value = known ? known->value : effects.atom("load" + std::to_string(effectSerial++), bits(item.type));
                    effects.bind(selected, value);
                    if (reusable) effects.remember(key, value, "");
                    result += castTo(item.type, selected);
                }
            } else {
                // Keep host-layout access sizes, but do not retain the unused
                // upper pointer bits in temporaries or cross-clock registers.
                unsigned width = bytes(item.type) * 8;
                unsigned valueWidth = item.type->isPointerType() ? bits(item.type) : width;
                if (sharedMemory) {
                    effects.clock();
                    portBytes = std::max(portBytes, std::min(8u, bytes(item.type)));
                    auto savedAddress = "scratch.port_address_" + std::to_string(tempCount++);
                    auto saved = std::string(item.type->isPointerType() ? "scratch.port_pointer_value_" : "scratch.port_value_") + std::to_string(tempCount++);
                    blockSymbols[savedAddress] = {addressWidth, "port_address", true};
                    blockSymbols[saved] = {valueWidth, "port_value", true};
                    prefix.push_back(savedAddress + " = " + addressCast(address) + ";");
                    if (item.write) prefix.push_back(saved + " = " + std::to_string(valueWidth) + "'(" + castTo(item.type, data) + ");");
                    for (unsigned offset = 0; offset < bytes(item.type); offset += 8) {
                        auto count = std::min(8u, bytes(item.type) - offset);
                        prefix.push_back("memory_address = " + savedAddress + " + " + addressLiteral(offset) + ";");
                        // Validate the entire remaining access before any byte
                        // is written. An invalid wide store must not write a prefix.
                        prefix.push_back("memory_size = " + std::to_string(bytes(item.type) - offset) + ";");
                        if (item.write) {
                            prefix.push_back("memory_write_data = " + std::to_string(count * 8) + "'(" + saved + " >> " + std::to_string(offset * 8) + ");");
                            prefix.push_back("memory_write = 1;");
                        } else prefix.push_back("memory_read = 1;");
                        prefix.push_back("@memory_clock@");
                        if (!item.write) prefix.push_back(saved + " = " + (offset ? saved + " | " : "") +
                            "(" + std::to_string(valueWidth) + "'(" + std::to_string(count * 8) + "'(memory_read_data)) << " +
                            std::to_string(offset * 8) + ");");
                    }
                    if (!item.write) {
                        auto loaded = item.type->isPointerType() ? addressCast(saved) : saved;
                        auto value = effects.atom("load" + std::to_string(effectSerial++), valueWidth);
                        effects.bind(saved, value);
                        // A multi-beat object contains bytes sampled on older
                        // edges. Only a single-response sample belongs wholly
                        // to the current zero-time region.
                        if (reusable && bytes(item.type) <= 8) effects.remember(key, value, loaded);
                        result += loaded;
                    }
                    cursor = end + 1;
                    continue;
                }
                if (item.write) {
                    storageWriteWidths.insert(bytes(item.type));
                    auto savedAddress = "scratch.write_address_" + std::to_string(tempCount++);
                    auto savedData = "scratch.write_value_" + std::to_string(tempCount++);
                    blockSymbols[savedAddress] = {addressWidth, "write_address", true};
                    blockSymbols[savedData] = {valueWidth, "write_value", true};
                    prefix.push_back(savedAddress + " = " + addressCast(address) + ";");
                    prefix.push_back(savedData + " = " + std::to_string(valueWidth) + "'(" + castTo(item.type, data) + ");");
                    auto value = effects.expression(castTo(item.type, data), valueWidth);
                    effects.bind(savedData, value);
                    if (reusable) effects.remember(key, value, savedData);
                    prefix.push_back("if (fault == 0 && !hls_storage_address_valid(" + savedAddress + ", " + std::to_string(bytes(item.type)) + ")) fault = 3;");
                    for (unsigned bank = 0; bank < 8; ++bank) {
                        auto name = storageBankNames()[bank];
                        result += name + " = hls_bank_write_" + std::to_string(width) + "_" + std::to_string(bank) +
                            "(" + name + ", " + savedAddress + ", " + savedData + ", fault); ";
                    }
                } else {
                    storageReadWidths.insert(bytes(item.type));
                    std::string loaded = "hls_storage_" + std::string(item.checked ? "read_" : "load_") +
                        std::to_string(width) + "(" + storageBankArguments() + ", " + address + (item.checked ? ", fault)" : ")");
                    if (item.checked) {
                        std::string value = "scratch.read_value_" + std::to_string(tempCount++);
                        blockSymbols[value] = {valueWidth, "read_value", true};
                        prefix.push_back("hls_call_result = CALL_RESULT_BITS'(" + loaded + "); fault = 32'(hls_call_result >> " +
                            std::to_string(width) + "); " + value + " = " + std::to_string(valueWidth) + "'(hls_call_result);");
                        loaded = value;
                    }
                    auto value = effects.atom("load" + std::to_string(effectSerial++), valueWidth);
                    // Unchecked loads are expressions, not captured samples.
                    // Do not reuse one after an intervening memory mutation.
                    effects.bind(loaded, value);
                    auto readResult = item.type->isPointerType() ? addressCast(loaded) : loaded;
                    if (reusable && item.checked) effects.remember(key, value, readResult);
                    result += readResult;
                }
            }
            cursor = end + 1;
        }
        return result;
    }
    void lowerSourceValues(int resetEntry, int commandEntry) {
        // Known object/subobject accesses stay direct. Only escaping addresses
        // or accesses without a statically known target require byte storage.
        for (size_t i = 0; i < accesses.size(); ++i) {
            if (elidedAccesses.count(i)) continue;
            const auto& item = accesses[i];
            if (!directLocation(item)) exposeAddresses(item.address);
            exposeAddresses(item.data);
        }
        for (const auto& b : blocks) {
            for (const auto& s : b.statements) exposeAddresses(s);
            exposeAddresses(b.condition);
        }
        highWater = (constantBytes + 15) & ~15u;
        for (const auto& [address, object] : objects) {
            if (object.memory) {
                unsigned alignment = ctx.getTypeAlignInChars(object.type).getQuantity();
                highWater = (highWater + alignment - 1) / alignment * alignment;
                symbols << "  localparam logic [" << addressWidth - 1 << ":0] " << address << " = " << addressLiteral(highWater) << ";\n";
                graphConstants[address] = graph::constant(highWater, addressWidth);
                highWater += bytes(object.type);
                blockSymbols[address] = {addressWidth, object.label + "_addr", false};
            } else blockSymbols["values." + object.name] = {bits(object.type), object.label, true};
        }
        if (!objects.at(persistentObject).memory)
            clockedValues.insert("values." + objects.at(persistentObject).name);
        // Optimize a memory-writing helper independently of its callers, so
        // caller-specific aliases/samples do not multiply outlined bodies or
        // add different cached-load outputs at each call site. This is an
        // analysis boundary only, not an extra clock. Read-only helpers remain
        // transparent, including helpers with ordinary private local writes.
        std::set<int> memoryWriters, callEffectBoundaries;
        for (const auto& [block, writes] : sourceWrites) for (auto index : writes) {
            if (elidedAccesses.count(index)) continue;
            auto location = directLocation(accesses[index]);
            if (!location || objects.at(location->object).memory) memoryWriters.insert(block);
        }
        for (const auto& call : callRegions) {
            std::set<int> visited;
            std::function<bool(int)> writes = [&](int n) {
                if (n < 0 || n == call.exit || !visited.insert(n).second) return false;
                return memoryWriters.count(n) || writes(blocks[n].yes) || writes(blocks[n].no);
            };
            if (writes(call.entry)) {
                callEffectBoundaries.insert(call.entry);
                callEffectBoundaries.insert(call.exit);
            }
        }
        std::vector<std::set<std::string>> uses(blocks.size()), definitions(blocks.size()), liveIn(blocks.size());
        // Propagate only through acyclic, zero-time edges. Clock entries start
        // with no knowledge, and joins retain facts true on every predecessor.
        std::vector<std::vector<int>> predecessors(blocks.size());
        std::vector<unsigned> incoming(blocks.size());
        std::set<int> reachable;
        std::function<void(int)> visit = [&](int n) {
            if (n < 0 || !reachable.insert(n).second) return;
            visit(blocks[n].yes); visit(blocks[n].no);
        };
        visit(resetEntry); visit(commandEntry);
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (!reachable.count(i)) continue;
            const auto& b = blocks[i];
            for (int next : {b.yes, b.no}) if (next >= 0) {
                predecessors[next].push_back(i);
                if (!b.suspend) ++incoming[next];
            }
        }
        std::set<int> ready;
        for (size_t i = 0; i < blocks.size(); ++i) if (!incoming[i]) ready.insert(i);
        std::vector<int> order;
        while (!ready.empty()) {
            int n = *ready.begin(); ready.erase(n); order.push_back(n);
            if (reachable.count(n) && !blocks[n].suspend)
                for (int next : {blocks[n].yes, blocks[n].no}) if (next >= 0 && --incoming[next] == 0) ready.insert(next);
        }
        if (order.size() != blocks.size()) reject(nullptr, "cycle in memory-effect analysis");
        std::vector<MemoryEffects::State> outgoing(blocks.size());
        for (int i : order) {
            auto& b = blocks[i];
            effects.state = {};
            bool first = true;
            for (int previous : predecessors[i]) {
                auto state = blocks[previous].suspend ? MemoryEffects::State{} : outgoing[previous];
                effects.state = first ? std::move(state) : MemoryEffects::intersect(effects.state, state);
                first = false;
            }
            if (callEffectBoundaries.count(i)) effects.state = {};
            effects.enter(i);
            for (const auto& symbol : blockSymbols) if (!symbol.second.writable)
                effects.bind(symbol.first, effects.atom(symbol.first, symbol.second.width));
            std::vector<std::string> statements;
            for (auto& s : b.statements) {
                auto assignment = temporaryAssignments.find(s);
                if (assignment != temporaryAssignments.end()) {
                    const auto& a = assignment->second;
                    auto source = resolveAccesses(a.source, uses[i], definitions[i], statements);
                    effects.bind(a.target, effects.expression(source, a.width));
                    statements.push_back(a.target + " = " + std::to_string(a.width) + "'(" + source + ");");
                } else {
                    auto text = resolveAccesses(s, uses[i], definitions[i], statements);
                    statements.push_back(std::move(text));
                    // Unmodelled scheduler effects (allocation, faults, entry
                    // and return handshakes) must not carry available loads.
                    if (s.find("@hls_access_") == std::string::npos) effects.state = {};
                }
            }
            b.condition = resolveAccesses(b.condition, uses[i], definitions[i], statements);
            b.statements = std::move(statements);
            outgoing[i] = effects.state;
        }
        bool changed;
        do {
            changed = false;
            for (size_t i = blocks.size(); i-- > 0;) {
                auto live = uses[i];
                for (int next : {blocks[i].yes, blocks[i].no}) if (next >= 0)
                    for (const auto& name : liveIn[next]) if (!definitions[i].count(name)) live.insert(name);
                if (live != liveIn[i]) { liveIn[i] = std::move(live); changed = true; }
            }
        } while (changed);
        for (const auto& b : blocks) if (b.suspend && !b.sharedCallBoundary && b.yes >= 0)
            clockedValues.insert(liveIn[b.yes].begin(), liveIn[b.yes].end());
        if (sharedMemory) {
            // Accesses are the resource boundaries, not individual arithmetic
            // instructions. Preserve original block indices used by call regions.
            const auto originalSize = blocks.size();
            for (size_t n = 0; n < originalSize; ++n) {
                auto original = blocks[n];
                int part = n;
                blocks[part].statements.clear();
                for (const auto& s : original.statements) {
                    if (s != "@memory_clock@") { blocks[part].statements.push_back(s); continue; }
                    int next = blocks.size();
                    auto tail = original;
                    tail.statements.clear();
                    blocks.push_back(std::move(tail));
                    blocks[part].condition.clear(); blocks[part].yes = next; blocks[part].no = -1;
                    blocks[part].suspend = true;
                    blocks[part].memoryBoundary = true;
                    blocks[part].recursiveBoundary = false;
                    blocks[part].sharedCallBoundary = false;
                    part = next;
                }
            }
            releaseSharedCallBoundaries(blocks);
            auto live = liveAcrossClocks(blocks, blockSymbols);
            clockedValues.insert(live.begin(), live.end());
        }
        // Pure, cycle-local values need no fault-enable mux. Keep guards on
        // effects and values retained across clocks; memory helpers also check
        // bounds and preserve the first fault internally.
        for (auto& b : blocks) for (auto& s : b.statements) {
            auto equal = s.find(" = ");
            auto target = s.substr(0, equal);
            auto symbol = blockSymbols.find(target);
            bool local = equal != std::string::npos && symbol != blockSymbols.end() &&
                symbol->second.writable && !clockedValues.count(target);
            if (!local) s = "if (fault == 0) begin " + s + " end";
        }
    }
    std::string temporary(unsigned width, const std::string& text, const std::string& source = "") {
        std::string name = (source.empty() ? scope + "__temporary" : source) + "_" + std::to_string(tempCount++);
        name = "scratch." + name;
        auto label = symbolLabels.find(source);
        blockSymbols[name] = {width, label == symbolLabels.end() ? "temporary" : label->second, true};
        auto statement = name + " = " + std::to_string(width) + "'(" + text + ");";
        temporaryAssignments.emplace(statement, TemporaryAssignment{name, text, width});
        emit(statement);
        return name;
    }
    void emit(const std::string& text) { blocks[current].statements.push_back(text); }
    int block(const Stmt* stmt = nullptr) {
        Block result;
        result.name = scope + "__step_" + std::to_string(blocks.size());
        result.method = method;
        if (stmt) result.source = stmt->getBeginLoc().printToString(ctx.getSourceManager());
        blocks.push_back(std::move(result));
        return blocks.size() - 1;
    }
    void jump(int target, bool suspend = false) {
        blocks[current].yes = target; blocks[current].suspend = suspend;
    }
    void branch(const std::string& condition, int yes, int no) {
        blocks[current].condition = condition;
        blocks[current].yes = yes; blocks[current].no = no;
    }
    void compactBlocks(int& resetEntry, int& commandEntry) {
        // Calls and returns create forwarding blocks. Removing those blocks
        // does not cross a loop's clock boundary or discard executable code.
        auto destination = [&](int n) {
            for (size_t hops = 0; n >= 0; ++hops) {
                const auto& b = blocks[n];
                if (!b.statements.empty() || !b.condition.empty() || b.suspend || b.yes < 0) break;
                if (hops >= blocks.size()) reject(nullptr, "cycle in empty source blocks");
                n = b.yes;
            }
            return n;
        };
        for (auto& b : blocks) {
            b.yes = destination(b.yes);
            b.no = destination(b.no);
        }
        resetEntry = destination(resetEntry);
        commandEntry = destination(commandEntry);
        std::set<int> live;
        std::function<void(int)> visit = [&](int n) {
            if (n < 0 || !live.insert(n).second) return;
            visit(blocks[n].yes); visit(blocks[n].no);
        };
        visit(resetEntry); visit(commandEntry);
        std::map<int, int> ids;
        std::vector<Block> compact;
        for (int n : live) {
            ids[n] = compact.size();
            compact.push_back(std::move(blocks[n]));
        }
        for (auto& b : compact) {
            if (b.yes >= 0) b.yes = ids.at(b.yes);
            if (b.no >= 0) b.no = ids.at(b.no);
        }
        resetEntry = ids.at(resetEntry); commandEntry = ids.at(commandEntry);
        blocks = std::move(compact);
    }
    std::string castTo(QualType type, const std::string& text) {
        if (type->isBooleanType()) return "(" + text + " != 0)";
        return std::to_string(bits(type)) + "'(" + text + ")";
    }
    std::string read(Value value) {
        std::string text = value.text;
        if (value.location) {
            text = temporary(bits(value.type), access({value.text, {}, value.type}), value.name);
        }
        if (value.type->isSignedIntegerOrEnumerationType()) return "$signed(" + text + ")";
        return text;
    }
    std::string savedPointer(Value address) {
        // References used after a loop must read their selected target there.
        return access({address.text, {}, ctx.VoidPtrTy, false, false});
    }
    Value capture(Value value) {
        if (value.type->isVoidType()) return value;
        if (value.location) return reference(value);
        if (value.type->isPointerType() && locations.count(value.text)) return value;
        Value saved = slot(value.type); store(saved, value); return saved;
    }
    Value reference(Value value) {
        if (!value.location) {
            Value saved = slot(value.type); store(saved, value); value = saved;
        }
        if (locations.count(value.text)) return value;
        // A reference must retain its address if its index variable changes.
        Value address = slot(ctx.getPointerType(value.type));
        store(address, {value.text, address.type});
        return {savedPointer(address), value.type, true, value.name};
    }
    void store(Value target, Value value) {
        if (!target.location) reject(nullptr, "assignment needs an lvalue: " + target.type.getAsString() + " " + target.text);
        std::string data = read(value);
        emit(access({target.text, data, target.type, true}));
    }
    void emitStorageHelpers(std::ostream& out) {
        out << R"SV(  function static logic hls_storage_address_valid(input logic [ADDR_BITS-1:0] address, input int unsigned count);
    hls_storage_address_valid = count <= MEM_BYTES && address >= ADDR_BITS'(16) && address <= ADDR_BITS'(MEM_BYTES) - ADDR_BITS'(count);
  endfunction
)SV";
        if (sharedMemory) {
            if (blockRam) return;
            emitBankLoad(out, portBytes);
            for (unsigned bank = 0; bank < 8; ++bank) emitBankWrite(out, portBytes, bank, true);
            return;
        }
        for (unsigned count : storageReadWidths) {
            unsigned width = count * 8;
            emitBankLoad(out, count);
            out << "  function static logic [" << width + 31 << ":0] hls_storage_read_" << width << "(\n";
            for (const auto& bank : storageBankNames()) out << "    input bank_t " << bank << ",\n";
            out << "    input logic [ADDR_BITS-1:0] address, input logic [31:0] fault);\n"
                << "    hls_storage_read_" << width << " = {\n"
                << "      fault != 0 ? fault : (hls_storage_address_valid(address, " << count << ") ? 32'd0 : 32'd3),\n"
                << "      fault == 0 && hls_storage_address_valid(address, " << count << ") ? hls_storage_load_" << width
                << "(" << storageBankArguments() << ", address) : " << width << "'('0)};\n  endfunction\n";
        }
        for (unsigned count : storageWriteWidths) {
            for (unsigned bank = 0; bank < 8; ++bank) emitBankWrite(out, count, bank);
        }
    }
    Value member(Value base, const FieldDecl* field) {
        if (field->isBitField()) reject(nullptr, "bit-field object storage not implemented");
        const auto& layout = ctx.getASTRecordLayout(field->getParent());
        unsigned offset = layout.getFieldOffset(field->getFieldIndex()) / 8;
        auto found = fieldOffsets.find(field);
        if (found == fieldOffsets.end()) {
            layouts.push_back(field->getQualifiedNameAsString() + " byte offset " + std::to_string(offset));
            found = fieldOffsets.emplace(field, addressSymbol("hls_field_" + qualifiedIdentifier(field) + "_offset", offset)).first;
        }
        Value result{offsetAddress(base.text, offset, found->second), field->getType(), true,
            scope + "__" + identifier(field->getNameAsString())};
        symbolLabels[result.name] = identifier(field->getNameAsString());
        if (result.type->isReferenceType()) {
            result.type = ctx.getPointerType(result.type->getPointeeType());
            return {read(result), field->getType()->getPointeeType(), true, result.name};
        }
        return result;
    }
    void destroy(Value value) {
        if (const auto* record = value.type->getAsCXXRecordDecl()) {
            if (record->hasTrivialDestructor()) return;
            auto* dtor = record->getDestructor();
            if (!dtor) reject(nullptr, "missing destructor");
            invoke(dtor, {}, value.text);
        } else if (const auto* array = ctx.getAsConstantArrayType(value.type)) {
            if (array->getElementType().isDestructedType() != QualType::DK_none)
                reject(nullptr, "array of nontrivially destructible objects");
        }
    }
    void cleanup(size_t keep) {
        // Invoking a destructor pushes contexts; do not hold references to them.
        auto scopes = contexts.back().scopes;
        for (size_t i = scopes.size(); i > keep; --i)
            for (auto it = scopes[i - 1].rbegin(); it != scopes[i - 1].rend(); ++it) destroy(*it);
    }
    void cleanupTemporaries() {
        auto values = contexts.back().temporaryCleanups.back();
        contexts.back().temporaryCleanups.pop_back();
        for (auto it = values.rbegin(); it != values.rend(); ++it) destroy(*it);
    }
    const Stmt* definition(FunctionDecl*& fn) {
        if (!fn->hasBody() && fn->getTemplateInstantiationPattern())
            sema.InstantiateFunctionDefinition(fn->getLocation(), fn, false, false, true);
        const FunctionDecl* actual = nullptr;
        const Stmt* body = fn->getBody(actual);
        if (actual) fn = const_cast<FunctionDecl*>(actual);
        return body;
    }
    bool pointerParameterReadOnly(const FunctionDecl* function, const Stmt* body, const ParmVarDecl* parameter) {
        bool readOnly = true;
        std::function<void(const Stmt*, const Stmt*)> visit = [&](const Stmt* stmt, const Stmt* parent) {
            if (!stmt) return;
            if (auto* ref = dyn_cast<DeclRefExpr>(stmt); ref && ref->getDecl() == parameter) {
                auto* cast = dyn_cast_or_null<ImplicitCastExpr>(parent);
                if (!cast || cast->getCastKind() != CK_LValueToRValue) readOnly = false;
            }
            for (auto* child : stmt->children()) visit(child, stmt);
        };
        visit(body, nullptr);
        if (auto* constructor = dyn_cast<CXXConstructorDecl>(function))
            for (auto* init : constructor->inits()) visit(init->getInit(), nullptr);
        return readOnly;
    }

    Value floatingOperation(const std::string& operation, QualType resultType, const std::vector<Value>& args) {
        auto typeName = [&](QualType type) {
            return std::string(type->isFloatingType() ? "f" : type->isBooleanType() ? "b" :
                type->isSignedIntegerType() ? "i" : "u") + std::to_string(bits(type));
        };
        std::string name = "@float." + operation;
        std::vector<QualType> types;
        for (const auto& arg : args) { name += "." + typeName(arg.type); types.push_back(arg.type); }
        name += "." + typeName(resultType);
        auto* replacement = overrides.operation(name, resultType, types);
        std::vector<Value> converted;
        for (unsigned i = 0; i < args.size(); ++i)
            converted.push_back({read(args[i]), replacement->getParamDecl(i)->getType()});
        traces.push_back("override " + name + " -> " + replacement->getQualifiedNameAsString());
        Value result = invoke(replacement, converted);
        return {read(result), resultType};
    }

    bool canShareRecursion(FunctionDecl* fn, const Stmt* body) {
        if (!sharedMemory || !body || isa<CXXConstructorDecl>(fn) || isa<CXXDestructorDecl>(fn) ||
            !(fn->getReturnType()->isVoidType() || fn->getReturnType()->isScalarType())) return false;
        for (auto* param : fn->parameters())
            if (!param->getType()->isScalarType() || param->getType().isVolatileQualified()) return false;
        auto* canonical = fn->getCanonicalDecl();
        auto found = recursiveFunctions.find(canonical);
        if (found != recursiveFunctions.end()) return found->second;
        bool recursive = false;
        std::function<void(const Stmt*)> visit = [&](const Stmt* stmt) {
            if (!stmt || isa<LambdaExpr>(stmt)) return;
            if (auto* call = dyn_cast<CallExpr>(stmt))
                if (auto* callee = call->getDirectCallee())
                    recursive |= callee->getCanonicalDecl() == canonical;
            for (auto* child : stmt->children()) visit(child);
        };
        visit(body);
        return recursiveFunctions[canonical] = recursive;
    }

    bool canSharePointerWriter(FunctionDecl* fn, const Stmt* body, const std::vector<Value>& args) {
        if (!sharedMemory || !body || !fn->getReturnType()->isVoidType() ||
            isa<CXXConstructorDecl>(fn) || isa<CXXDestructorDecl>(fn)) return false;
        if (auto* method = dyn_cast<CXXMethodDecl>(fn); method && !method->isStatic()) return false;
        bool pointer = false;
        for (auto* param : fn->parameters()) {
            if (!param->getType()->isScalarType() || param->getType().isVolatileQualified()) return false;
            pointer |= param->getType()->isPointerType();
        }
        if (!pointer) return false;
        // Preserve direct-object promotion and constant-address specialization.
        // Sharing such calls would turn register accesses into arena traffic.
        for (const auto& arg : args)
            if (arg.type->isPointerType() && !arg.location && locations.count(arg.text)) return false;
        bool writes = false;
        std::function<void(const Stmt*)> visit = [&](const Stmt* s) {
            if (!s || isa<LambdaExpr>(s)) return;
            const E* target = nullptr;
            if (auto* assignment = dyn_cast<BinaryOperator>(s); assignment && assignment->isAssignmentOp())
                target = assignment->getLHS()->IgnoreParenImpCasts();
            if (auto* update = dyn_cast<UnaryOperator>(s); update && update->isIncrementDecrementOp())
                target = update->getSubExpr()->IgnoreParenImpCasts();
            if (target) {
                if (auto* member = dyn_cast<MemberExpr>(target)) writes |= member->isArrow();
                if (auto* deref = dyn_cast<UnaryOperator>(target)) writes |= deref->getOpcode() == UO_Deref;
                writes |= isa<ArraySubscriptExpr>(target);
            }
            for (const auto* child : s->children()) visit(child);
        };
        visit(body);
        return writes;
    }

    Value invokeSharedBody(FunctionDecl* fn, const Stmt* body, const std::vector<Value>& args,
                           const std::string& self, unsigned depth, bool recursive) {
        // Other active functions can call back into this one. Preserve their
        // bounds too; a body expanded inside g() cannot assume g is inactive.
        EnclosingDepths enclosing;
        for (const auto& [active, count] : calling)
            if (count && active != fn->getCanonicalDecl()) enclosing.emplace_back(active, count);
        const auto key = std::make_tuple(fn->getCanonicalDecl(), depth, self, enclosing);
        auto& bodies = recursive ? recursiveBodies : clockedBodies;
        auto [it, inserted] = bodies.try_emplace(key);
        auto& shared = it->second;
        const auto callerScope = scope, callerMethod = method;
        const int callSite = current, continuation = block();
        if (inserted) {
            method = "hls_" + qualifiedIdentifier(fn);
            scope = method + "__shared_" + std::to_string(callCount++) + "_depth_" + std::to_string(depth);
            shared.entry = block(body);
            shared.dispatch = block();
            shared.caller = slot(ctx.UnsignedIntTy, "caller");
            if (!fn->getReturnType()->isVoidType()) shared.result = slot(fn->getReturnType(), "return_value");
            for (auto* param : fn->parameters()) shared.parameters.push_back(slot(param->getType(), param->getNameAsString()));
            FunctionScope binding;
            binding.self = self;
            binding.result = shared.result;
            binding.continuation = shared.dispatch;
            for (unsigned i = 0; i < args.size(); ++i) binding.variables[fn->getParamDecl(i)] = shared.parameters[i];
            if (auto* compound = dyn_cast<CompoundStmt>(body))
                for (auto* stmt : compound->body())
                    if (auto* label = dyn_cast<LabelStmt>(stmt)) binding.labels[label->getDecl()] = block(label);
            contexts.push_back(binding);
            auto savedLoops = std::move(loops); loops.clear();
            current = shared.entry;
            statement(body);
            jump(binding.continuation);
            contexts.pop_back(); loops = std::move(savedLoops);
            scope = callerScope; method = callerMethod;
        }
        current = callSite;
        // Capture all arguments before overwriting the reused depth's inputs.
        std::vector<Value> captured;
        for (const auto& arg : args) captured.push_back({temporary(bits(arg.type), read(arg)), arg.type});
        for (unsigned i = 0; i < args.size(); ++i) store(shared.parameters[i], captured[i]);
        store(shared.caller, {std::to_string(shared.callers++), ctx.UnsignedIntTy});
        // Recursive edges keep their clocks. Ordinary shared calls initially
        // get a boundary too; after memory lowering, remove it if the shared
        // body's existing boundaries already prevent combinational feedback.
        jump(shared.entry, true);
        blocks[current].recursiveBoundary = recursive;
        blocks[current].sharedCallBoundary = !recursive;
        current = shared.dispatch;
        blocks[current].statements.clear();
        int next = block();
        branch(read(shared.caller) + " == " + std::to_string(shared.callers - 1), continuation, next);
        shared.dispatch = next;
        current = next; emit("fault = 4;");
        current = continuation;
        if (fn->getReturnType()->isVoidType()) return {"0", ctx.VoidTy};
        Value result = slot(fn->getReturnType(), "recursive_result");
        store(result, shared.result);
        return result;
    }

    Value invoke(FunctionDecl* fn, const std::vector<Value>& args, std::string self = "", Value constructed = {}) {
        std::string name = fn->getQualifiedNameAsString();
        if (pipelineMode) {
            auto check = [&](const Decl* declaration) {
                for (const auto* attr : declaration->specific_attrs<AnnotateAttr>())
                    if (attr->getAnnotation() == "CPPHDL_ONE_CLOCK" || attr->getAnnotation().starts_with("CPPHDL_KEEP_BOX="))
                        reject(fn->getBody(), "ClockedPipeline cannot yet preserve function timing/keep-box constraints: " + name);
            };
            check(fn);
            if (auto* method = dyn_cast<CXXMethodDecl>(fn)) check(method->getParent());
        }
        std::vector<QualType> actualTypes;
        for (const auto& arg : args) actualTypes.push_back(arg.type);
        if (auto* replacement = overrides.find(fn, actualTypes)) {
            if (!replacing.insert(fn->getCanonicalDecl()).second) reject(nullptr, "cyclic HLS override: " + name);
            traces.push_back("override " + name + " -> " + replacement->getQualifiedNameAsString());
            std::vector<Value> converted = args;
            for (unsigned i = 0; i < converted.size(); ++i)
                if (converted[i].type->isFloatingType()) converted[i] = {read(converted[i]), replacement->getParamDecl(i)->getType()};
            Value result = invoke(replacement, converted);
            replacing.erase(fn->getCanonicalDecl());
            if (!fn->getReturnType()->isVoidType() && !fn->getReturnType()->isReferenceType())
                result = {read(result), fn->getReturnType()};
            return result;
        }
        switch (fn->getBuiltinID()) {
        case Builtin::BIforward: case Builtin::BIforward_like:
        case Builtin::BImove: case Builtin::BImove_if_noexcept:
            return reference(args.at(0));
        case Builtin::BIaddressof: case Builtin::BI__addressof:
            return {reference(args.at(0)).text, fn->getReturnType()};
        default: break;
        }
        if ((fn->isOverloadedOperator() && fn->getOverloadedOperator() == OO_New) || name == "__builtin_operator_new") {
            bool aligned = args.size() == 2 && args[1].type->isEnumeralType() &&
                args[1].type->getAs<EnumType>()->getDecl()->getName() == "align_val_t";
            if (args.size() != 1 && !aligned) reject(nullptr, "custom operator new is not supported");
            heap = true;
            auto size = read(args.at(0));
            if (aligned) {
                auto alignment = read(args[1]);
                emit("if (" + alignment + " == 0 || " + alignment + " > 4096 || (" + alignment + " & (" + alignment + " - 1)) != 0) fault = 1;");
                auto alignedNext = temporary(std::max(14u, addressWidth + 1), "(" + std::to_string(std::max(14u, addressWidth + 1)) + "'(heap_next) + " + alignment + " - 1) & ~(" + alignment + " - 1)", scope + "__aligned_address");
                emit("if (" + alignedNext + " > MEM_BYTES) fault = 1;");
                emit("heap_next = " + addressCast(alignedNext) + ";");
            }
            Value result = slot(fn->getReturnType());
            emit("if (" + size + " > HEAP_BYTES || heap_next > MEM_BYTES || " + size + " > MEM_BYTES - heap_next) fault = 1;");
            store(result, {"heap_next", fn->getReturnType()});
            emit("heap_next = (heap_next + " + addressCast(size) + " + " + addressLiteral(15) + ") & ~" + addressLiteral(15) + ";");
            return result;
        }
        if ((fn->isOverloadedOperator() && fn->getOverloadedOperator() == OO_Delete) || name == "__builtin_operator_delete") return {"0", ctx.VoidTy};
        if (name.find("__throw_") != std::string::npos || name.find("__glibcxx_assert_fail") != std::string::npos || name == "__assert_fail" || name == "__builtin_trap" || name == "__builtin_unreachable") {
            emit("fault = 1;"); return {"0", ctx.UnsignedIntTy};
        }
        if (name == "__builtin_is_constant_evaluated") return {"1'b0", ctx.BoolTy};
        if (name == "__builtin_mul_overflow" || name == "__builtin_add_overflow" || name == "__builtin_sub_overflow") {
            QualType resultType = args.at(2).type->getPointeeType();
            unsigned wide = 2 * std::max({bits(args[0].type), bits(args[1].type), bits(resultType)}) + 2;
            auto widen = [&](Value value) { return "$signed(" + std::to_string(wide) + "'(" + read(value) + "))"; };
            std::string op = name == "__builtin_mul_overflow" ? " * " : name == "__builtin_add_overflow" ? " + " : " - ";
            std::string full = temporary(wide, widen(args[0]) + op + widen(args[1]));
            Value dest{read(args[2]), resultType, true}; store(dest, {full, resultType});
            return {"(" + full + " != " + widen(dest) + ")", ctx.BoolTy};
        }
        if (name == "__builtin_expect") return args.at(0);
        if (name == "__builtin_assume_aligned") return {read(args.at(0)), fn->getReturnType()};
        if (name == "__builtin_addressof") return {reference(args.at(0)).text, fn->getReturnType()};
        if (name == "__builtin_memmove" || name == "__builtin_memcpy" || name == "__builtin_memset") {
            Value dst = slot(ctx.VoidPtrTy), src = slot(name == "__builtin_memset" ? ctx.UnsignedLongLongTy : ctx.VoidPtrTy), count = slot(ctx.UnsignedLongLongTy);
            store(dst, args.at(0)); store(src, args.at(1)); store(count, args.at(2));
            Value i = slot(ctx.UnsignedLongLongTy); store(i, {"0", i.type});
            int test = block(), body = block(), after = block();
            jump(test, true); current = test;
            branch("(" + read(i) + " < " + read(count) + ")", body, after);
            current = body;
            std::string index = read(i);
            if (name == "__builtin_memmove")
                index = "(" + read(dst) + " > " + read(src) + " ? " + read(count) + " - 1 - " + index + " : " + index + ")";
            Value to{"(" + read(dst) + " + " + index + ")", ctx.UnsignedCharTy, true};
            Value from = name == "__builtin_memset" ? Value{read(src), ctx.UnsignedCharTy}
                : Value{"(" + read(src) + " + " + index + ")", ctx.UnsignedCharTy, true};
            store(to, from); store(i, {"(" + read(i) + " + 1)", i.type});
            jump(test, true); current = after;
            return dst;
        }
        unsigned& depth = calling[fn->getCanonicalDecl()];
        if (depth && !recursionLimit) reject(fn->getBody(), "recursive clocked call needs a declared depth bound: " + name);
        if (depth >= recursionLimit && recursionLimit) {
            emit("fault = 5; /* declared recursion bound exceeded: " + name + " */");
            if (fn->getReturnType()->isReferenceType()) return {addressLiteral(0), fn->getReturnType()->getPointeeType(), true};
            return {"'0", fn->getReturnType()};
        }
        ++depth;
        if (contexts.size() >= 64) reject(fn->getBody(), "clocked call depth exceeds 64");
        if (args.size() != fn->getNumParams()) reject(fn->getBody(), "argument count mismatch: " + name);
        const Stmt* body = definition(fn);
        auto* ctor = dyn_cast<CXXConstructorDecl>(fn);
        if (!body && !(fn->isDefaulted() && (ctor || isa<CXXDestructorDecl>(fn)))) reject(nullptr, "missing instantiated body: " + name + " builtin=" + std::to_string(fn->getBuiltinID()) + " at " + fn->getLocation().printToString(ctx.getSourceManager()));
        traces.push_back(name + " at " + fn->getLocation().printToString(ctx.getSourceManager()));
        if (canShareRecursion(fn, body)) {
            auto result = invokeSharedBody(fn, body, args, self, depth, true);
            --depth;
            return result;
        }
        if (canSharePointerWriter(fn, body, args)) {
            auto result = invokeSharedBody(fn, body, args, self, depth, false);
            --depth;
            return result;
        }
        // Identity returns carry no work or local lifetime. Keep their selected
        // object/value directly instead of creating call/return copy blocks.
        bool trivialParameters = std::all_of(fn->param_begin(), fn->param_end(), [](const ParmVarDecl* param) {
            auto type = param->getType();
            return (type->isReferenceType() || type->isScalarType()) && !type.getNonReferenceType().isVolatileQualified();
        });
        if (auto* compound = dyn_cast_or_null<CompoundStmt>(body); compound && compound->size() == 1 && trivialParameters) {
            if (auto* ret = dyn_cast<ReturnStmt>(*compound->body_begin())) {
                const E* value = ret->getRetValue();
                if (value) {
                    value = value->IgnoreParens();
                    while (auto* cast = dyn_cast<ImplicitCastExpr>(value)) {
                        if (cast->getCastKind() != CK_NoOp && cast->getCastKind() != CK_LValueToRValue) break;
                        value = cast->getSubExpr()->IgnoreParens();
                    }
                    if (isa<CXXThisExpr>(value)) {
                        --depth;
                        return {self, fn->getReturnType()};
                    }
                    if (auto* deref = dyn_cast<UnaryOperator>(value); deref && deref->getOpcode() == UO_Deref &&
                        isa<CXXThisExpr>(deref->getSubExpr()->IgnoreParenImpCasts()) && fn->getReturnType()->isReferenceType()) {
                        --depth;
                        return {self, fn->getReturnType().getNonReferenceType(), true};
                    }
                    if (auto* decl = dyn_cast<DeclRefExpr>(value))
                        for (unsigned i = 0; i < fn->getNumParams(); ++i)
                            if (decl->getDecl() == fn->getParamDecl(i) && args[i].type->isScalarType() &&
                                !fn->getParamDecl(i)->getType().getNonReferenceType().isVolatileQualified() &&
                                (!fn->getReturnType()->isReferenceType() || fn->getParamDecl(i)->getType()->isReferenceType())) {
                                --depth;
                                return fn->getReturnType()->isReferenceType() ? reference(args[i]) : Value{read(args[i]), fn->getReturnType()};
                            }
                }
            }
        }
        std::string callerScope = scope;
        std::string callerMethod = method;
        int continuation = block();
        method = "hls_" + qualifiedIdentifier(fn);
        scope = method + "__call_" + std::to_string(callCount++) + "_depth_" + std::to_string(depth);
        int entry = block(body);
        jump(entry); current = entry;
        FunctionScope binding;
        binding.referenceResult = fn->getReturnType()->isReferenceType();
        if (!fn->getReturnType()->isVoidType())
            binding.result = !ctor && constructed.location ? constructed :
                slot(fn->getReturnType()->isReferenceType() ? ctx.VoidPtrTy : fn->getReturnType(), "return_value");
        binding.self = self;
        binding.continuation = continuation;
        if (auto* compound = dyn_cast_or_null<CompoundStmt>(body))
            for (const auto* stmt : compound->body())
                if (const auto* label = dyn_cast<LabelStmt>(stmt)) binding.labels[label->getDecl()] = block(label);
        // Elide a named result only when all returns select the same object.
        // A different return path must still destroy its non-returned locals.
        bool uniformReturn = true;
        std::function<void(const Stmt*)> inspectReturns = [&](const Stmt* s) {
            if (!s || isa<LambdaExpr>(s)) return;
            if (const auto* ret = dyn_cast<ReturnStmt>(s)) {
                const auto* candidate = ret->getNRVOCandidate();
                if (!candidate || (binding.nrvo && binding.nrvo != candidate)) uniformReturn = false;
                else binding.nrvo = candidate;
                return;
            }
            for (auto* child : s->children()) inspectReturns(child);
        };
        if (fn->getReturnType()->isRecordType()) inspectReturns(body);
        if (!uniformReturn) binding.nrvo = nullptr;
        for (unsigned i = 0; i < args.size(); ++i) {
            auto* param = fn->getParamDecl(i);
            Value local;
            if (param->getType()->isReferenceType()) {
                local = reference(args[i]);
            } else if (param->getType()->isPointerType() && !param->getType().isVolatileQualified() &&
                       !args[i].location && locations.count(args[i].text) && pointerParameterReadOnly(fn, body, param)) {
                local = {args[i].text, param->getType()};
            } else { local = slot(param->getType(), param->getNameAsString()); store(local, args[i]); }
            binding.variables[param] = local;
        }
        contexts.push_back(binding);
        auto savedLoops = std::move(loops); loops.clear();
        if (ctor) {
            if (ctor->isCopyOrMoveConstructor() && ctor->isTrivial()) store(constructed, args.at(0));
            else {
                for (auto* init : ctor->inits()) {
                    Value target = constructed;
                    const FieldDecl* field = nullptr;
                    if (init->isIndirectMemberInitializer()) {
                        for (auto* declaration : init->getIndirectMember()->chain()) {
                            if (field) target = member(target, field);
                            field = cast<FieldDecl>(declaration);
                        }
                    } else if (init->isMemberInitializer()) field = init->getMember();
                    if (field) {
                        if (field->getType()->isReferenceType()) {
                            unsigned offset = ctx.getASTRecordLayout(field->getParent()).getFieldOffset(field->getFieldIndex()) / 8;
                            Value ref = reference(expression(init->getInit()));
                            store({offsetAddress(target.text, offset), ctx.VoidPtrTy, true}, {ref.text, ctx.VoidPtrTy});
                            continue;
                        }
                        target = member(target, field);
                    } else if (init->isBaseInitializer()) {
                        auto* base = init->getBaseClass()->getAsCXXRecordDecl();
                        auto offset = ctx.getASTRecordLayout(ctor->getParent()).getBaseClassOffset(base).getQuantity();
                        target = {offsetAddress(self, offset), QualType(init->getBaseClass(), 0), true};
                    } else if (!init->isDelegatingInitializer()) reject(init->getInit(), "unsupported constructor initializer");
                    initialize(target, init->getInit());
                }
            }
        }
        if (body) statement(body);
        if (auto* dtor = dyn_cast<CXXDestructorDecl>(fn)) {
            jump(binding.continuation); current = binding.continuation;
            binding.continuation = block();
            Value object{binding.self, ctx.getRecordType(dtor->getParent()), true};
            std::vector<const FieldDecl*> fields;
            for (auto* field : dtor->getParent()->fields()) fields.push_back(field);
            for (auto it = fields.rbegin(); it != fields.rend(); ++it)
                if (!(*it)->getType()->isReferenceType()) destroy(member(object, *it));
            for (auto it = dtor->getParent()->bases_end(); it != dtor->getParent()->bases_begin();) {
                --it;
                if (it->isVirtual()) reject(nullptr, "virtual base destruction");
                auto offset = ctx.getASTRecordLayout(dtor->getParent()).getBaseClassOffset(it->getType()->getAsCXXRecordDecl()).getQuantity();
                destroy({offsetAddress(object.text, offset), it->getType(), true});
            }
        }
        jump(binding.continuation); current = binding.continuation;
        callRegions.push_back({entry, binding.continuation, callerMethod});
        const auto returnedReferences = contexts.back().referenceReturns;
        contexts.pop_back(); loops = std::move(savedLoops); --calling[fn->getCanonicalDecl()];
        scope = callerScope;
        method = callerMethod;
        if (ctor) return constructed;
        if (fn->getReturnType()->isReferenceType()) {
            // A reference that always denotes the same subobject is a binding,
            // not a runtime pointer. Do not spill its address into storage.
            if (!returnedReferences.empty()) {
                auto target = locations.find(returnedReferences.front().first);
                bool same = target != locations.end();
                for (const auto& returned : returnedReferences) {
                    auto found = locations.find(returned.first);
                    same = same && found != locations.end() &&
                        found->second.object == target->second.object && found->second.offset == target->second.offset;
                }
                if (same) {
                    for (const auto& returned : returnedReferences) elidedAccesses.insert(returned.second);
                    return {returnedReferences.front().first, fn->getReturnType()->getPointeeType(), true};
                }
            }
            auto address = savedPointer(binding.result);
            return {address, fn->getReturnType()->getPointeeType(), true, binding.result.name};
        }
        return fn->getReturnType()->isVoidType() ? Value{"0", ctx.VoidTy} : binding.result;
    }

    void initialize(Value target, const E* init) {
        target = reference(target);
        init = init->IgnoreParens();
        if (auto* cast = dyn_cast<CastExpr>(init); cast &&
            (cast->getCastKind() == CK_ConstructorConversion || cast->getCastKind() == CK_NoOp) &&
            init->isPRValue() && init->getType()->isRecordType()) {
            initialize(target, cast->getSubExpr()); return;
        }
        if (auto* clean = dyn_cast<ExprWithCleanups>(init)) {
            contexts.back().temporaryCleanups.emplace_back();
            initialize(target, clean->getSubExpr());
            cleanupTemporaries(); return;
        }
        if (auto* bind = dyn_cast<CXXBindTemporaryExpr>(init)) {
            // A class prvalue initializes the destination itself (C++17 copy
            // elision). Its destructor belongs to that object's lifetime.
            initialize(target, bind->getSubExpr()); return;
        }
        if (auto* call = dyn_cast<CallExpr>(init); call && init->isPRValue() && init->getType()->isRecordType()) {
            callExpression(call, target); return;
        }
        if (auto* construct = dyn_cast<CXXConstructExpr>(init)) {
            std::vector<Value> args;
            for (auto* arg : construct->arguments()) args.push_back(capture(expression(arg)));
            if (construct->requiresZeroInitialization()) store(target, {"'0", target.type});
            invoke(construct->getConstructor(), args, target.text, target); return;
        }
        if (auto* list = dyn_cast<InitListExpr>(init)) {
            if (list->isSyntacticForm() && list->getSemanticForm()) list = list->getSemanticForm();
            store(target, {"'0", target.type});
            unsigned n = 0;
            if (const auto* array = ctx.getAsConstantArrayType(target.type)) {
                for (auto* element : list->inits()) {
                    Value to{offsetAddress(target.text, n++ * bytes(array->getElementType())), array->getElementType(), true};
                    initialize(to, element);
                }
            } else if (const auto* record = target.type->getAsCXXRecordDecl()) {
                // The semantic initializer list contains bases first, then fields.
                for (const auto& base : record->bases()) {
                    if (n == list->getNumInits()) break;
                    if (base.isVirtual()) reject(init, "virtual aggregate base initialization");
                    auto offset = ctx.getASTRecordLayout(record).getBaseClassOffset(base.getType()->getAsCXXRecordDecl()).getQuantity();
                    initialize({offsetAddress(target.text, offset), base.getType(), true}, list->getInit(n++));
                }
                for (auto* field : record->fields()) {
                    if (n == list->getNumInits()) break;
                    if (field->getType()->isReferenceType()) {
                        Value ref = reference(expression(list->getInit(n++)));
                        unsigned offset = ctx.getASTRecordLayout(record).getFieldOffset(field->getFieldIndex()) / 8;
                        store({offsetAddress(target.text, offset), ctx.VoidPtrTy, true}, {ref.text, ctx.VoidPtrTy});
                    } else initialize(member(target, field), list->getInit(n++));
                }
            } else if (list->getNumInits() == 1) initialize(target, list->getInit(0));
            return;
        }
        store(target, expression(init));
    }

    void initializeConstant(Value target, const APValue& value) {
        if (value.isInt()) {
            llvm::SmallString<32> number;
            static_cast<const llvm::APInt&>(value.getInt()).toString(number, 16, false);
            store(target, {std::to_string(bits(target.type)) + "'h" + number.str().str(), target.type});
            return;
        }
        if (value.isStruct()) {
            const auto* record = target.type->getAsCXXRecordDecl();
            unsigned i = 0;
            for (const auto& base : record->bases()) {
                if (base.isVirtual()) reject(nullptr, "virtual constant base");
                auto offset = ctx.getASTRecordLayout(record).getBaseClassOffset(base.getType()->getAsCXXRecordDecl()).getQuantity();
                initializeConstant({offsetAddress(target.text, offset), base.getType(), true}, value.getStructBase(i++));
            }
            i = 0;
            for (const auto* field : record->fields()) {
                if (field->isMutable()) reject(nullptr, "mutable global constant object is not supported");
                initializeConstant(member(target, field), value.getStructField(i++));
            }
            return;
        }
        if (value.isArray()) {
            auto element = ctx.getAsConstantArrayType(target.type)->getElementType();
            for (unsigned i = 0; i < value.getArraySize(); ++i) {
                const auto& item = i < value.getArrayInitializedElts() ? value.getArrayInitializedElt(i) : value.getArrayFiller();
                initializeConstant({offsetAddress(target.text, i * bytes(element)), element, true}, item);
            }
            return;
        }
        if (value.isLValue() && value.isNullPointer()) {
            store(target, {"'0", target.type});
            return;
        }
        reject(nullptr, "unsupported constant object value: " + target.type.getAsString());
    }

    Value expression(const E* expr) {
        QualType type = expr->getType();
        if (type.isVolatileQualified() || type->isAtomicType()) reject(expr, "volatile/atomic access is not supported");
        if (type->isFloatingType() && !overrides.hasFloatingHooks()) reject(expr, "floating-point AST lowering not implemented");
        if (auto* literal = dyn_cast<FloatingLiteral>(expr)) {
            llvm::SmallString<32> number;
            literal->getValue().bitcastToAPInt().toString(number, 16, false);
            return {std::to_string(bits(type)) + "'h" + number.str().str(), type};
        }
        if (!expr->isValueDependent() && !runtimeCall(expr) && !expr->HasSideEffects(ctx) && type->isIntegralOrEnumerationType()) {
            E::EvalResult result;
            if (expr->EvaluateAsInt(result, ctx)) {
                llvm::SmallString<32> number; static_cast<const llvm::APInt&>(result.Val.getInt()).toString(number, 16, false);
                return {std::to_string(bits(type)) + "'h" + number.str().str(), type};
            }
        }
        if (auto* p = dyn_cast<ParenExpr>(expr)) return expression(p->getSubExpr());
        if (auto* p = dyn_cast<CXXRewrittenBinaryOperator>(expr)) return expression(p->getSemanticForm());
        if (auto* p = dyn_cast<ExprWithCleanups>(expr)) {
            contexts.back().temporaryCleanups.emplace_back();
            Value result = expression(p->getSubExpr());
            if (type->isScalarType() && !expr->isGLValue() && !contexts.back().temporaryCleanups.back().empty()) {
                Value saved = slot(type); store(saved, result); result = saved;
            }
            cleanupTemporaries(); return result;
        }
        if (auto* p = dyn_cast<MaterializeTemporaryExpr>(expr)) {
            if (p->getStorageDuration() != SD_FullExpression && type.isDestructedType() != QualType::DK_none)
                reject(expr, "lifetime-extended nontrivial temporary is not supported");
            return reference(expression(p->getSubExpr()));
        }
        if (auto* p = dyn_cast<CXXBindTemporaryExpr>(expr)) {
            Value result = expression(p->getSubExpr());
            if (!p->getTemporary()->getDestructor()->isTrivial()) {
                if (contexts.back().temporaryCleanups.empty()) reject(expr, "temporary needs a full-expression cleanup boundary");
                contexts.back().temporaryCleanups.back().push_back(reference(result));
            }
            return result;
        }
        if (auto* p = dyn_cast<CXXDefaultArgExpr>(expr)) return expression(p->getExpr());
        if (auto* p = dyn_cast<CXXDefaultInitExpr>(expr)) return expression(p->getExpr());
        if (auto* p = dyn_cast<SubstNonTypeTemplateParmExpr>(expr)) return expression(p->getReplacement());
        if (isa<ImplicitValueInitExpr>(expr) || isa<CXXScalarValueInitExpr>(expr) || isa<CXXNullPtrLiteralExpr>(expr)) return {"'0", type};
        if (auto* literal = dyn_cast<StringLiteral>(expr)) {
            if (!literal->isOrdinary()) reject(expr, "wide string literal");
            auto key = literal->getBytes().str();
            Value result = slot(type);
            store(result, {"'0", type});
            for (unsigned i = 0; i < key.size(); ++i)
                store({offsetAddress(result.text, i), ctx.CharTy, true},
                    {std::to_string(static_cast<unsigned char>(key[i])), ctx.CharTy});
            return result;
        }
        if (isa<CXXThisExpr>(expr)) return {contexts.back().self, type};
        if (auto* decl = dyn_cast<DeclRefExpr>(expr)) {
            if (isa<FunctionDecl>(decl->getDecl())) reject(expr, "indirect call / function pointer value is not supported");
            auto it = contexts.back().variables.find(decl->getDecl());
            if (it == contexts.back().variables.end()) {
                auto* var = dyn_cast<VarDecl>(decl->getDecl());
                if (!var || !var->isConstexpr() || !var->hasGlobalStorage() || !var->hasInit() ||
                    var->getType().isDestructedType() != QualType::DK_none)
                    reject(expr, "unbound declaration: " + decl->getDecl()->getNameAsString());
                auto key = var->getCanonicalDecl();
                auto found = constants.find(key);
                Value storage;
                if (found == constants.end()) {
                    unsigned alignment = ctx.getTypeAlignInChars(var->getType()).getQuantity();
                    constantBytes = (constantBytes + alignment - 1) / alignment * alignment;
                    std::string name = "hls_constant_" + qualifiedIdentifier(var);
                    storage = {addressSymbol(name + "_addr", constantBytes), var->getType(), true, name};
                    constantBytes += bytes(var->getType());
                    if (constantBytes > 4112) reject(expr, "constant object storage exceeds 4 KiB");
                } else storage = found->second;
                constants[key] = storage;
                // Constant objects retain their address across call sites. Initialize
                // on every read so no branch depends on another branch executing first.
                const APValue* evaluated = var->evaluateValue();
                if (!evaluated) reject(expr, "constant object initializer did not evaluate");
                store(storage, {"'0", storage.type});
                initializeConstant(storage, *evaluated);
                return storage;
            }
            Value result = it->second;
            if (result.type->isReferenceType()) {
                QualType pointee = result.type->getPointeeType(); result.type = ctx.VoidPtrTy;
                return {read(result), pointee, true, result.name};
            }
            return result;
        }
        if (auto* fieldExpr = dyn_cast<MemberExpr>(expr)) {
            auto* field = dyn_cast<FieldDecl>(fieldExpr->getMemberDecl());
            if (!field) reject(expr, "non-field member value");
            Value base = expression(fieldExpr->getBase());
            if (fieldExpr->isArrow()) base.text = read(base);
            return member(base, field);
        }
        if (auto* cast = dyn_cast<CastExpr>(expr)) {
            Value sub = expression(cast->getSubExpr());
            switch (cast->getCastKind()) {
            case CK_NoOp: case CK_ConstructorConversion: return sub;
            case CK_LValueToRValue: return {read(sub), type};
            case CK_IntegralToFloating: case CK_FloatingToIntegral: case CK_FloatingCast: case CK_FloatingToBoolean:
                return floatingOperation("cast", type, {sub});
            case CK_ArrayToPointerDecay: return {sub.text, type};
            case CK_DerivedToBase: case CK_UncheckedDerivedToBase: case CK_BaseToDerived: {
                bool downcast = cast->getCastKind() == CK_BaseToDerived;
                QualType original = downcast ? type : cast->getSubExpr()->getType();
                bool pointer = original->isPointerType();
                if (pointer) original = original->getPointeeType();
                const auto* record = original->getAsCXXRecordDecl();
                unsigned offset = 0;
                for (auto path : cast->path()) {
                    const auto* base = path->getType()->getAsCXXRecordDecl();
                    if (path->isVirtual()) reject(expr, "virtual base object layout");
                    offset += ctx.getASTRecordLayout(record).getBaseClassOffset(base).getQuantity(); record = base;
                }
                std::string addr = pointer ? read(sub) : sub.text;
                if (!offset) return {addr, type, !pointer};
                std::string adjusted = offsetAddress(addr, downcast ? -int64_t(offset) : int64_t(offset));
                if (pointer && offset && !locations.count(addr)) adjusted = "(" + addr + " == 0 ? " + addressLiteral(0) + " : " + adjusted + ")";
                return {adjusted, type, !pointer};
            }
            case CK_ToVoid: return {"0", ctx.VoidTy};
            case CK_IntegralCast: case CK_IntegralToBoolean: case CK_PointerToBoolean:
            case CK_IntegralToPointer: case CK_PointerToIntegral: case CK_NullToPointer:
                return {castTo(type, read(sub)), type};
            case CK_BitCast: return {sub.location && !type->isPointerType() ? sub.text : read(sub), type, sub.location && !type->isPointerType()};
            default: reject(expr, "unsupported cast " + std::string(cast->getCastKindName()));
            }
        }
        if (auto* index = dyn_cast<ArraySubscriptExpr>(expr)) {
            Value base = capture(expression(index->getBase())), i = expression(index->getIdx());
            return {addressCast("(" + read(base) + " + " + addressCast(read(i)) + " * " + addressLiteral(bytes(type)) + ")"), type, true};
        }
        if (auto* unary = dyn_cast<UnaryOperator>(expr)) {
            Value sub = expression(unary->getSubExpr());
            if (unary->getOpcode() == UO_AddrOf) return {reference(sub).text, type};
            if (unary->getOpcode() == UO_Deref) return {read(sub), type, true};
            if (sub.type->isFloatingType()) return floatingOperation(UnaryOperator::getOpcodeStr(unary->getOpcode()).str(), type, {sub});
            if (unary->isIncrementDecrementOp()) {
                auto old = read(sub); unsigned step = type->isPointerType() ? bytes(type->getPointeeType()) : 1;
                std::string value = "(" + old + (unary->isIncrementOp() ? " + " : " - ") + (type->isPointerType() ? addressLiteral(step) : std::to_string(step)) + ")";
                store(sub, {value, type});
                return unary->isPostfix() ? Value{old, type} : sub;
            }
            return {"(" + UnaryOperator::getOpcodeStr(unary->getOpcode()).str() + read(sub) + ")", type};
        }
        if (auto* conditional = dyn_cast<ConditionalOperator>(expr)) {
            Value cond = expression(conditional->getCond());
            int yes = block(expr), no = block(expr), done = block(expr);
            Value result = slot(conditional->isGLValue() ? ctx.VoidPtrTy : type);
            branch(read(cond), yes, no);
            current = yes; Value a = expression(conditional->getTrueExpr());
            store(result, conditional->isGLValue() ? Value{reference(a).text, ctx.VoidPtrTy} : a); jump(done);
            current = no; Value b = expression(conditional->getFalseExpr());
            store(result, conditional->isGLValue() ? Value{reference(b).text, ctx.VoidPtrTy} : b); jump(done);
            current = done;
            return conditional->isGLValue() ? Value{read(result), type, true} : result;
        }
        if (auto* binary = dyn_cast<BinaryOperator>(expr)) {
            if (binary->getOpcode() == BO_Comma) { expression(binary->getLHS()); return expression(binary->getRHS()); }
            if (binary->getOpcode() == BO_LAnd || binary->getOpcode() == BO_LOr) {
                Value result = slot(ctx.BoolTy); Value lhs = expression(binary->getLHS()); store(result, lhs);
                int rhs = block(expr), done = block(expr);
                branch(read(result), binary->getOpcode() == BO_LAnd ? rhs : done, binary->getOpcode() == BO_LAnd ? done : rhs);
                current = rhs; store(result, expression(binary->getRHS())); jump(done); current = done; return result;
            }
            Value rhs = capture(expression(binary->getRHS())), lhs = expression(binary->getLHS());
            if (binary->getOpcode() == BO_Assign) { store(lhs, rhs); return lhs; }
            if (lhs.type->isFloatingType() || rhs.type->isFloatingType()) {
                if (binary->isCompoundAssignmentOp()) reject(expr, "floating-point compound assignment requires explicit source expansion");
                return floatingOperation(binary->getOpcodeStr().str(), type, {lhs, rhs});
            }
            std::string a = read(lhs), b = read(rhs), op = binary->getOpcodeStr().str();
            if (binary->isCompoundAssignmentOp()) op.pop_back();
            bool pointer = lhs.type->isPointerType();
            if (pointer && !rhs.type->isPointerType() && (op == "+" || op == "-"))
                b = "(" + addressCast(b) + " * " + addressLiteral(bytes(lhs.type->getPointeeType())) + ")";
            if (!pointer && rhs.type->isPointerType() && op == "+")
                a = "(" + addressCast(a) + " * " + addressLiteral(bytes(rhs.type->getPointeeType())) + ")";
            if (op == ">>" && lhs.type->isSignedIntegerType()) op = ">>>";
            std::string result = "(" + a + " " + op + " " + b + ")";
            if (pointer && rhs.type->isPointerType() && op == "-") {
                // Subtract zero-extended addresses as signed values, so a negative
                // ptrdiff_t is preserved even when pointers are narrower than it.
                auto width = std::to_string(std::max(bits(type), addressWidth) + 1);
                result = "(($signed(" + width + "'({1'b0, " + a + "})) - $signed(" + width + "'({1'b0, " + b + "}))) / " + width + "'sd" + std::to_string(bytes(lhs.type->getPointeeType())) + ")";
            }
            if (binary->isCompoundAssignmentOp()) { store(lhs, {result, type}); return lhs; }
            return {castTo(type, result), type};
        }
        if (auto* call = dyn_cast<CallExpr>(expr)) return callExpression(call);
        if (auto* fresh = dyn_cast<CXXNewExpr>(expr)) {
            Value pointer;
            if (fresh->getNumPlacementArgs() == 1) pointer = capture(expression(fresh->getPlacementArg(0)));
            else if (fresh->getNumPlacementArgs() == 0 && !fresh->isArray()) {
                std::vector<Value> args{{"64'd" + std::to_string(bytes(fresh->getAllocatedType())), ctx.UnsignedLongTy}};
                if (fresh->passAlignment()) args.push_back({"64'd" + std::to_string(ctx.getTypeAlignInChars(fresh->getAllocatedType()).getQuantity()), fresh->getOperatorNew()->getParamDecl(1)->getType()});
                pointer = invoke(fresh->getOperatorNew(), args);
            } else reject(expr, "array new or custom placement allocation");
            std::string addr = read(pointer);
            if (fresh->hasInitializer()) initialize({addr, fresh->getAllocatedType(), true}, fresh->getInitializer());
            pointer.type = type;
            return pointer;
        }
        if (auto* deletion = dyn_cast<CXXDeleteExpr>(expr)) {
            if (deletion->isArrayForm()) reject(expr, "array delete is not supported");
            auto* release = deletion->getOperatorDelete();
            if (isa<CXXMethodDecl>(release) || release->hasBody())
                reject(expr, "custom delete is not supported");
            QualType object = deletion->getDestroyedType();
            if (auto* record = object->getAsCXXRecordDecl(); record && record->getDestructor()->isVirtual())
                reject(expr, "virtual delete is not supported");
            Value pointer = capture(expression(deletion->getArgument()));
            int destroyBlock = block(expr), done = block(expr);
            branch(read(pointer) + " != " + addressLiteral(0), destroyBlock, done);
            current = destroyBlock;
            destroy({read(pointer), object, true});
            // The bounded arena is monotonic, as for std::allocator deallocate.
            // Destruction has effects; releasing the allocation does not reuse it.
            jump(done); current = done;
            return {"0", ctx.VoidTy};
        }
        if (isa<CXXConstructExpr>(expr) || isa<InitListExpr>(expr)) {
            Value result = slot(type); initialize(result, expr); return result;
        }
        reject(expr, "unsupported source expression");
    }

    Value callExpression(const CallExpr* call, Value destination = {}) {
        if (auto* dtor = dyn_cast<CXXPseudoDestructorExpr>(call->getCallee()->IgnoreParenImpCasts())) {
            expression(dtor->getBase());
            return {"0", ctx.VoidTy};
        }
        FunctionDecl* fn = const_cast<FunctionDecl*>(call->getDirectCallee());
        if (!fn) reject(call, "indirect call");
        if (auto* method = dyn_cast<CXXMethodDecl>(fn); method && method->isVirtual()) reject(call, "virtual call is not supported");
        const auto name = fn->getQualifiedNameAsString();
        if (name.find("__throw_") != std::string::npos || name.find("__glibcxx_assert_fail") != std::string::npos || name == "__assert_fail") return invoke(fn, {});
        std::vector<Value> args; std::string self;
        unsigned first = 0;
        if (auto* method = dyn_cast<CXXMethodDecl>(fn); method && !method->isStatic()) {
            Value object;
            if (auto* memberCall = dyn_cast<CXXMemberCallExpr>(call)) object = expression(memberCall->getImplicitObjectArgument());
            else { object = expression(call->getArg(0)); first = 1; }
            auto address = object.type->isPointerType() ? read(object) : object.text;
            if (locations.count(address)) self = address;
            else {
                Value pointer = slot(ctx.VoidPtrTy);
                store(pointer, {address, ctx.VoidPtrTy});
                self = savedPointer(pointer);
            }
        }
        for (unsigned n = first; n < call->getNumArgs(); ++n) args.push_back(capture(expression(call->getArg(n))));
        return invoke(fn, args, self, destination);
    }

    void statement(const Stmt* stmt) {
        if (!stmt) return;
        if (auto* jumpStmt = dyn_cast<GotoStmt>(stmt)) {
            const auto* label = jumpStmt->getLabel();
            if (!contexts.back().labels.count(label) || contexts.back().visitedLabels.count(label))
                reject(stmt, "only forward goto to a function-body label is supported; use loops for back edges");
            cleanup(1);
            jump(contexts.back().labels.at(label)); current = block(stmt); return;
        }
        if (auto* label = dyn_cast<LabelStmt>(stmt)) {
            if (!contexts.back().labels.count(label->getDecl())) reject(stmt, "nested goto label is not supported");
            int target = contexts.back().labels.at(label->getDecl());
            jump(target); current = target;
            contexts.back().visitedLabels.insert(label->getDecl());
            statement(label->getSubStmt()); return;
        }
        if (auto* compound = dyn_cast<CompoundStmt>(stmt)) {
            contexts.back().scopes.emplace_back();
            for (auto* s : compound->body()) statement(s);
            cleanup(contexts.back().scopes.size() - 1);
            contexts.back().scopes.pop_back(); return;
        }
        if (auto* decls = dyn_cast<DeclStmt>(stmt)) {
            for (auto* d : decls->decls()) {
                if (isa<StaticAssertDecl>(d) || isa<TypedefNameDecl>(d) || isa<CXXRecordDecl>(d)) continue;
                auto* var = dyn_cast<VarDecl>(d);
                if (!var || var->isStaticLocal()) reject(stmt, "nonautomatic local declaration");
                if (var->getType()->isReferenceType()) {
                    if (!var->hasInit()) reject(stmt, "reference requires an initializer");
                    contexts.back().variables[var] = reference(expression(var->getInit()));
                    continue;
                }
                Value storage = var == contexts.back().nrvo ? contexts.back().result : slot(var->getType(), var->getNameAsString());
                contexts.back().variables[var] = storage;
                if (var->hasInit()) {
                    initialize(storage, var->getInit());
                }
                if (var != contexts.back().nrvo && !var->getType()->isReferenceType() && var->getType().isDestructedType() != QualType::DK_none) {
                    if (contexts.back().scopes.empty()) reject(stmt, "object lifetime outside compound scope");
                    contexts.back().scopes.back().push_back(storage);
                }
            }
            return;
        }
        if (auto* ret = dyn_cast<ReturnStmt>(stmt)) {
            if (ret->getRetValue()) {
                if (!contexts.back().referenceResult && ret->getRetValue()->getType()->isRecordType()) {
                    if (!contexts.back().nrvo || ret->getNRVOCandidate() != contexts.back().nrvo)
                        initialize(contexts.back().result, ret->getRetValue());
                } else {
                    Value value = expression(ret->getRetValue());
                    // Reference-return slots contain the pointee address.
                    if (contexts.back().referenceResult) {
                        auto target = reference(value).text;
                        store(contexts.back().result, {target, ctx.VoidPtrTy});
                        contexts.back().referenceReturns.emplace_back(target, accesses.size() - 1);
                    }
                    else if (!value.type->isVoidType()) store(contexts.back().result, value);
                }
            }
            cleanup(0);
            jump(contexts.back().continuation); current = block(stmt); return;
        }
        if (auto* conditional = dyn_cast<IfStmt>(stmt)) {
            if (conditional->isConsteval()) {
                statement(conditional->isNegatedConsteval() ? conditional->getThen() : conditional->getElse());
                return;
            }
            statement(conditional->getInit()); statement(conditional->getConditionVariableDeclStmt());
            if (conditional->isConstexpr()) {
                auto selected = conditional->getNondiscardedCase(ctx);
                if (!selected) reject(stmt, "dependent constexpr if");
                statement(*selected); return;
            }
            E::EvalResult known;
            if (!runtimeCall(conditional->getCond()) && !conditional->getCond()->HasSideEffects(ctx) && conditional->getCond()->EvaluateAsInt(known, ctx)) {
                statement(known.Val.getInt().getBoolValue() ? conditional->getThen() : conditional->getElse()); return;
            }
            Value cond = expression(conditional->getCond());
            int yes = block(stmt), no = block(stmt), done = block(stmt);
            branch(read(cond), yes, no);
            current = yes; statement(conditional->getThen()); jump(done);
            current = no; statement(conditional->getElse()); jump(done);
            current = done; return;
        }
        if (isa<ForStmt>(stmt) || isa<WhileStmt>(stmt) || isa<DoStmt>(stmt)) {
            const E* condition = nullptr; const E* increment = nullptr; const Stmt* body = nullptr;
            bool doLoop = isa<DoStmt>(stmt);
            if (auto* loop = dyn_cast<ForStmt>(stmt)) { statement(loop->getInit()); condition = loop->getCond(); increment = loop->getInc(); body = loop->getBody(); }
            if (auto* loop = dyn_cast<WhileStmt>(stmt)) { if (loop->getConditionVariable()) reject(stmt, "while condition declaration"); condition = loop->getCond(); body = loop->getBody(); }
            if (auto* loop = dyn_cast<DoStmt>(stmt)) { condition = loop->getCond(); body = loop->getBody(); }
            E::EvalResult known;
            if (doLoop && !runtimeCall(condition) && !condition->HasSideEffects(ctx) && condition->EvaluateAsInt(known, ctx) && !known.Val.getInt().getBoolValue()) {
                int after = block(stmt);
                loops.push_back({after, after, contexts.back().scopes.size()}); statement(body); jump(after);
                loops.pop_back(); current = after; return;
            }
            int test = block(stmt), bodyId = block(body), step = block(stmt), after = block(stmt);
            jump(doLoop ? bodyId : test, true);
            current = test; std::string cond = condition ? read(expression(condition)) : "1'b1";
            if (doLoop) {
                int again = block(stmt); branch(cond, again, after); current = again; jump(bodyId, true);
            } else branch(cond, bodyId, after);
            loops.push_back({after, step, contexts.back().scopes.size()});
            current = bodyId; statement(body); jump(step);
            current = step; if (increment) expression(increment); jump(test, !doLoop);
            loops.pop_back(); current = after; return;
        }
        if (isa<BreakStmt>(stmt) || isa<ContinueStmt>(stmt)) {
            if (loops.empty()) reject(stmt, "break/continue outside supported loop");
            cleanup(loops.back().scopes);
            jump(isa<BreakStmt>(stmt) ? loops.back().first : loops.back().second); current = block(stmt); return;
        }
        if (isa<NullStmt>(stmt)) return;
        if (auto* expr = dyn_cast<E>(stmt)) { expression(expr); return; }
        reject(stmt, "unsupported source statement");
    }
public:
    DelayedScheduler(ASTContext& context, Sema& sema) : ctx(context), sema(sema), overrides(context) { block(); }
    std::map<std::string, graph::Value> graphPorts;
    std::map<std::string, graph::Value> graphConstants;
    std::string generate(const CXXRecordDecl* wrapper, const std::string& name, graph::Graph* graph = nullptr,
                         synth::ScheduledDesign* pipeline = nullptr) {
        pipelineMode = pipeline != nullptr;
        if (auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(wrapper); specialization && !pipeline) {
            const auto& args = specialization->getTemplateArgs();
            if (args.size() > 1) recursionLimit = args[1].getAsIntegral().getLimitedValue();
            if (recursionLimit > 16) reject(nullptr, "MAX_RECURSION must be in 0..16");
            if (args.size() > 2) addressWidth = args[2].getAsIntegral().getLimitedValue();
            if (addressWidth < 8 || addressWidth > 64) reject(nullptr, "ADDRESS_BITS must be in 8..64");
            if (args.size() > 3) heapBytes = args[3].getAsIntegral().getLimitedValue();
            if (heapBytes < 16 || heapBytes > 16777216 || heapBytes % 16)
                reject(nullptr, "HEAP_BYTES must be a multiple of 16 in 16..16777216");
            if (args.size() > 4) sharedMemory = args[4].getAsIntegral().getBoolValue();
            if (args.size() > 5) blockRam = args[5].getAsIntegral().getBoolValue();
            if (blockRam && !sharedMemory) reject(nullptr, "BLOCK_RAM requires SHARED_MEMORY");
        }
        const FieldDecl* object = nullptr;
        for (auto* field : wrapper->fields()) if (field->getName() == "object") object = field;
        if (!object) reject(nullptr, "clocked wrapper has no object");
        Value state = slot(object->getType(), object->getNameAsString());
        persistentObject = state.text;
        auto* record = object->getType()->getAsCXXRecordDecl();
        CXXMethodDecl* command = nullptr;
        for (auto* method : record->methods()) if (method->getNameAsString() == "command") {
            if (command) reject(nullptr, "overloaded command entry"); command = method;
        }
        if (!command || command->getNumParams() != 3 || !command->getReturnType()->isUnsignedIntegerType() || bits(command->getReturnType()) != 64)
            reject(nullptr, "clocked object requires uint64_t command(uint32_t, uint32_t, uint32_t)");
        for (auto* param : command->parameters()) if (!param->getType()->isUnsignedIntegerType() || bits(param->getType()) != 32)
            reject(nullptr, "clocked command arguments must be uint32_t");
        contexts.push_back({});
        int resetEntry = current;
        if (!object->getInClassInitializer() && object->hasInClassInitializer()) {
            auto init = sema.BuildCXXDefaultInitExpr(object->getLocation(), const_cast<FieldDecl*>(object));
            if (init.isInvalid()) reject(nullptr, "cannot instantiate Clocked object initializer");
        }
        if (!object->getInClassInitializer()) reject(nullptr, "Clocked object initializer is unavailable");
        initialize(state, object->getInClassInitializer());
        emit("booting = 0; phase = 0;");
        current = block(); int commandEntry = current;
        Value result = invoke(command, {{"operation_in", ctx.UnsignedIntTy}, {"index_in", ctx.UnsignedIntTy}, {"value_in", ctx.UnsignedIntTy}}, state.text);
        emit("result = " + read(result) + "; pending = 1; phase = 0;");
        contexts.pop_back();
        lowerSourceValues(resetEntry, commandEntry);
        if (graph || pipeline) {
            compactBlocks(resetEntry, commandEntry);
            unsigned heapBase = (highWater + 15) & ~15u;
            unsigned memoryBytes = heapBase + (heap ? heapBytes : 0);
            if (addressWidth < 64 && uint64_t(memoryBytes) >= (uint64_t(1) << addressWidth))
                reject(nullptr, "clocked storage and its end address do not fit ADDRESS_BITS");
            synth::ScheduledDesign design{blocks, blockSymbols, graphConstants, clockedValues,
                resetEntry, commandEntry, addressWidth, memoryBytes, heapBase, heapBytes, portBytes, sharedMemory, blockRam};
            if (pipeline) {
                if (heap || std::any_of(objects.begin(), objects.end(), [](const auto& item) { return item.second.memory; }))
                    reject(nullptr, "ClockedPipeline memory/escaping pointers require a memory schedule");
                for (const auto& b : blocks) if (b.suspend)
                    reject(nullptr, "ClockedPipeline cannot achieve II=1 for loops or suspended calls; use ClockedDelayer");
                if (record->isEmpty()) design.clocked.clear();
                *pipeline = std::move(design);
                return {};
            }
            graphPorts = synth::exportScheduledGraph(*graph, design, name);
            return {};
        }
        BlockSharing sharing(blockSymbols);
        BlockUses uses(blocks, blockSymbols);
        unsigned outlinedCalls = 0;
        for (const auto& call : callRegions)
            if (outlineCall(blocks, call.entry, call.exit, call.callerMethod, sharing, clockedValues, &uses, memoryArguments())) ++outlinedCalls;
        coalesceBlocks(blocks, resetEntry, commandEntry);
        compactBlocks(resetEntry, commandEntry);
        std::set<int> reachable;
        std::function<void(int)> visit = [&](int n) {
            if (n < 0 || !reachable.insert(n).second) return;
            visit(blocks[n].yes); visit(blocks[n].no);
        };
        visit(resetEntry); visit(commandEntry);
        std::vector<unsigned> incoming(blocks.size());
        std::set<int> continuations{resetEntry};
        for (int n : reachable) {
            if (blocks[n].suspend) continuations.insert(blocks[n].yes);
            else if (blocks[n].yes >= 0) ++incoming[blocks[n].yes];
            if (blocks[n].no >= 0 && blocks[n].no != blocks[n].yes) ++incoming[blocks[n].no];
        }
        std::set<int> ready; std::vector<int> order;
        for (int n : reachable) if (!incoming[n]) ready.insert(n);
        while (!ready.empty()) {
            int n = *ready.begin(); ready.erase(n); order.push_back(n);
            const auto& b = blocks[n];
            if (!b.suspend && b.yes >= 0 && --incoming[b.yes] == 0) ready.insert(b.yes);
            if (b.no >= 0 && b.no != b.yes && --incoming[b.no] == 0) ready.insert(b.no);
        }
        if (order.size() != reachable.size()) reject(nullptr, "unscheduled cycle in source graph");
        SharedBlocks shared;
        BlockUses blockUses(blocks, blockSymbols);
        for (size_t n = 0; n < blocks.size(); ++n) {
            auto liveOutputs = blockUses.outside({{int(n), {int(n)}}}, -1);
            liveOutputs.insert(clockedValues.begin(), clockedValues.end());
            shared.calls.push_back(sharing.add(blocks[n], &liveOutputs));
        }
        shared.functions = std::move(sharing.functions);
        pruneFunctionState(shared, memoryState());
        lowerValueFunctionCalls(shared);
        std::set<std::string> usedValues = clockedValues;
        for (const auto& call : shared.calls)
            usedValues.insert(call.arguments.begin(), call.arguments.end());
        auto signalName = [](std::string name) {
            auto dot = name.find('.');
            if (dot != std::string::npos) name.replace(dot, 1, "__");
            return name;
        };
        auto registerName = [&](const std::string& name) {
            return name.compare(0, 7, "values.") == 0 ? name.substr(7) : signalName(name);
        };
        unsigned heapBase = (highWater + 15) & ~15u;
        unsigned memoryBytes = heapBase + (heap ? heapBytes : 0);
        if (addressWidth < 64 && uint64_t(memoryBytes) >= (uint64_t(1) << addressWidth))
            reject(nullptr, "clocked storage and its end address do not fit ADDRESS_BITS");
        std::ostringstream out;
        out << "// AST clocked instantiation. No compiler IR or external compiler invocation.\n";
        out << "// Shared schedule: " << blocks.size() << " blocks, " << shared.functions.size() << " function bodies\n";
        out << "// Shared lowering: " << blocks.size() + outlinedCalls << " bodies, " << shared.functions.size() << " function bodies\n";
        out << "// Whole same-clock calls: " << outlinedCalls << "\n";
        for (const auto& [key, body] : recursiveBodies)
            out << "// Recursive body: " << qualifiedIdentifier(std::get<0>(key))
                << " depth " << std::get<1>(key) << ", " << body.callers << " callers\n";
        for (const auto& [key, body] : clockedBodies)
            out << "// Shared clocked body: " << qualifiedIdentifier(std::get<0>(key))
                << " depth " << std::get<1>(key) << ", " << body.callers << " callers\n";
        unsigned directCount = 0;
        for (const auto& [address, object] : objects) if (!object.memory) ++directCount;
        out << "// Source values: " << directCount << " direct values, " << clockedValues.size() << " live across clocks\n";
        out << "// Memory effects: " << reusedLoads << " redundant loads reused within clock regions\n";
        for (const auto& trace : traces) out << "// instantiated: " << trace << "\n";
        for (const auto& layout : layouts) out << "// field: " << layout << "\n";
        out << "module " << name << R"SV((
    input wire clk, reset,
    input wire command_valid_in,
    input wire [31:0] operation_in, index_in, value_in,
    output wire command_ready_out,
    input wire response_ready_in,
    output wire response_valid_out,
    output wire [63:0] result_out,
    output wire [31:0] fault_out
);
)SV";
        out << "  localparam int ADDR_BITS = " << addressWidth << ";\n" << symbols.str();
        out << "  localparam int HEAP_BYTES = " << heapBytes << ";\n";
        std::map<std::string, size_t> stateBits{
            {"fault", 32}, {"heap_next", addressWidth},
            {"operation_in", 32}, {"index_in", 32}, {"value_in", 32}, {"phase", 32},
            {"result", 64}, {"pending", 1}, {"booting", 1},
            {"active", blocks.size()}, {"next_block", 32}, {"else_block", 32}};
        for (const auto& bank : storageBankNames()) stateBits[bank] = memoryBytes;
        if (sharedMemory) {
            stateBits["memory_address"] = addressWidth;
            stateBits["memory_write_data"] = stateBits["memory_read_data"] = portBytes * 8;
            stateBits["memory_size"] = 32;
            stateBits["memory_read"] = stateBits["memory_write"] = 1;
        }
        // Shared-port methods return scalar state, never the whole arena.
        // Oversizing this scratch value multiplies frontend work at every call.
        size_t resultBits = sharedMemory ? 1 : memoryBytes * 8 + 32;
        std::map<std::string, size_t> functionResultBits;
        for (unsigned count : storageReadWidths) resultBits = std::max(resultBits, size_t(count * 8 + 32));
        for (const auto& function : shared.functions) {
            size_t width = 0;
            for (auto i : functionOutputs(function))
                width += i < function.stateArguments.size() ? stateBits.at(function.stateArguments[i]) :
                    function.parameters[i - function.stateArguments.size()].width;
            resultBits = std::max(resultBits, width);
            functionResultBits[function.body.name] = width;
        }
        out << "  localparam int CALL_RESULT_BITS = " << resultBits << ";\n";
        out << "  localparam int MEM_BYTES = " << memoryBytes << ";\n"
            << "  localparam int INDEX_BITS = $clog2(MEM_BYTES);\n"
            << "  localparam int STATE_BITS = $clog2(" << std::max(size_t(2), blocks.size()) << ");\n"
            << "  typedef logic [MEM_BYTES-1:0][7:0] storage_t;\n"
            << "  localparam int BANK_BYTES = MEM_BYTES / 8;\n"
            << "  localparam int BANK_INDEX_BITS = $clog2(BANK_BYTES);\n"
            << "  typedef logic [BANK_BYTES-1:0][7:0] bank_t;\n"
            << "  logic [" << blocks.size() - 1 << ":0] active;\n"
            << "  logic [31:0] phase, phase_reg, fault, fault_reg;\n"
            << "  logic [63:0] result, result_reg;\n"
            << "  logic [ADDR_BITS-1:0] heap_next, heap_next_reg;\n"
            << "  logic pending, pending_reg, booting, booting_reg;\n";
        if (!blockRam) for (const auto& bank : storageBankNames()) out << "  bank_t " << bank << ", " << bank << "_reg;\n";
        if (sharedMemory) {
            out << "  logic [ADDR_BITS-1:0] memory_address;\n"
                << "  logic [" << portBytes * 8 - 1 << ":0] memory_write_data, memory_read_data;\n"
                << "  logic [31:0] memory_size;\n  logic memory_read, memory_write;\n";
            for (const std::string pin : {"operation_in", "index_in", "value_in"})
                out << "  logic [31:0] hls_accepted_" << pin << ";\n"
                    << "  wire [31:0] hls_command_" << pin << " = command_valid_in && command_ready_out ? "
                    << pin << " : hls_accepted_" << pin << ";\n";
        }
        if (blockRam) emitBlockRamDeclarations(out);
        // These are independent combinational source values, not an aggregate
        // data object. Avoid huge unused struct comparison operators in host RTL.
        for (const auto& name : usedValues) if (name.find('.') != std::string::npos)
            out << "  logic [" << blockSymbols.at(name).width - 1 << ":0] " << signalName(name) << ";\n";
        out << "  typedef struct packed {\n";
        for (const auto& name : clockedValues)
            out << "    logic [" << blockSymbols.at(name).width - 1 << ":0] " << registerName(name) << ";\n";
        out << "    logic unused_bit;\n  } clocked_values_t;\n  clocked_values_t values_reg;\n";
        emitStorageHelpers(out);
        std::map<std::string, std::string> stateWidths{
            {"fault", "32"}, {"heap_next", "ADDR_BITS"},
            {"operation_in", "32"}, {"index_in", "32"}, {"value_in", "32"}, {"phase", "32"},
            {"result", "64"}, {"pending", "1"}, {"booting", "1"},
            {"active", std::to_string(blocks.size())}, {"next_block", "32"}, {"else_block", "32"}};
        for (const auto& bank : storageBankNames()) stateWidths[bank] = "BANK_BYTES*8";
        if (sharedMemory) for (const auto& name : memoryState()) stateWidths[name] = std::to_string(stateBits.at(name));
        for (const auto& function : shared.functions) {
            const auto& body = function.body;
            std::vector<std::string> formals = function.stateArguments;
            std::vector<std::string> widths;
            for (const auto& state : function.stateArguments) widths.push_back(stateWidths.at(state));
            for (const auto& parameter : function.parameters) {
                formals.push_back(parameter.name);
                widths.push_back(std::to_string(parameter.width));
            }
            auto outputs = functionOutputs(function);
            std::string returnWidth;
            for (auto i : outputs) {
                if (!returnWidth.empty()) returnWidth += "+";
                returnWidth += widths[i];
            }
            out << "  // " << body.source << "\n  // Reused by " << function.uses << " scheduled blocks\n"
                << (body.combinational ? "  // Whole same-clock method\n" : "  // Clocked continuation\n")
                << "  function static " << (outputs.empty() ? "void" : "logic [" + returnWidth + "-1:0]")
                << " " << body.name << "(\n";
            for (size_t i = 0; i < formals.size(); ++i) {
                if (i) out << ",\n";
                // The function-local active vector is indexed by STATE_BITS.
                // Padding avoids artificial non-local bounds-check temporaries
                // in older Verilator; returned state keeps its original width.
                out << "    input " << (formals[i].find("storage_lane") == 0 ? "bank_t" :
                    formals[i] == "active" ? "logic [(1 << STATE_BITS)-1:0]" : "logic [" + widths[i] + "-1:0]")
                    << " " << formals[i];
            }
            if (!formals.empty()) out << ",\n";
            // A zero input used as scratch avoids static local state in
            // Verilator's non-inlined functions. It is never returned/stored.
            size_t scratchBits = sharedMemory ? callResultScratchWidth(body, functionResultBits) : resultBits;
            out << "    input logic [" << (sharedMemory ? std::to_string(scratchBits) : "CALL_RESULT_BITS")
                << "-1:0] hls_call_result);\n    begin\n";
            if (sharedMemory) out << "      localparam int CALL_RESULT_BITS = " << scratchBits << ";\n";
            for (const auto& text : body.statements) out << "      " << text << "\n";
            if (body.yes >= 0) {
                out << "      if (fault == 0) begin ";
                if (!body.condition.empty()) out << "if (" << body.condition << ") ";
                if (body.suspend) out << "phase = next_block + 32'd1; // "
                    << (body.memoryBoundary ? "memory port" : body.recursiveBoundary ? "recursive call" :
                        body.sharedCallBoundary ? "shared call" : "loop") << " boundary\n";
                else out << "active[STATE_BITS'(next_block)] = 1;";
                if (body.no >= 0) out << " else active[STATE_BITS'(else_block)] = 1;";
                out << " end\n";
            }
            if (!outputs.empty()) {
                out << "      " << body.name << " = {";
                for (size_t i = 0; i < outputs.size(); ++i) {
                    if (i) out << (i % 8 == 0 ? ",\n        " : ", ");
                    auto index = outputs[i];
                    out << (formals[index] == "active" ? widths[index] + "'(active)" : formals[index]);
                }
                out << "};\n";
            }
            out << "    end\n  endfunction\n";
        }
        out << R"SV(  assign command_ready_out = !booting_reg && !pending_reg && phase_reg == 0 && fault_reg == 0;
  assign response_valid_out = pending_reg;
  assign result_out = result_reg;
  assign fault_out = fault_reg;
  always_comb begin
    heap_next = heap_next_reg;
    phase = phase_reg; fault = fault_reg; result = result_reg;
    pending = pending_reg; booting = booting_reg; active = '0;
)SV";
        if (!blockRam) for (const auto& bank : storageBankNames()) out << "    " << bank << " = " << bank << "_reg;\n";
        if (sharedMemory) out << "    memory_address = '0; memory_write_data = '0; memory_size = 0; memory_read = 0; memory_write = 0;\n";
        for (const auto& name : usedValues) if (name.find('.') != std::string::npos)
            out << "    " << signalName(name) << " = '0;\n";
        for (const auto& name : clockedValues)
            out << "    " << signalName(name) << " = values_reg." << registerName(name) << ";\n";
        out << "    if (reset) begin\n      phase = " << resetEntry + 1 << "; fault = 0; pending = 0; result = 0; booting = 1; heap_next = " << heapBase << ";\n"
            << "    end else if (fault == 0) begin\n      if (pending && response_ready_in) pending = 0;\n"
            << "      if (command_valid_in && command_ready_out) active[" << commandEntry << "] = 1;\n"
            << "      else case (phase_reg)\n";
        for (int n : continuations) out << "        " << n + 1 << ": active[" << n << "] = 1;\n";
        out << "        0: begin end\n        default: fault = 4;\n      endcase\n    end else if (response_ready_in) pending = 0;\n  end\n";
        // Give each same-clock block its own combinational process. Successive
        // versions carry blocking-assignment semantics without making synthesis
        // prune every temporary in one enormous, deeply nested process.
        std::map<std::string, std::string> versions;
        if (sharedMemory) for (const std::string pin : {"operation_in", "index_in", "value_in"})
            versions[pin] = "hls_command_" + pin;
        auto current = [&](const std::string& value) {
            auto found = versions.find(value);
            return found == versions.end() ? value : found->second;
        };
        for (int n : order) {
            const auto& call = shared.calls[n];
            const auto& function = shared.functions[call.function];
            std::vector<std::string> arguments;
            std::vector<size_t> widths;
            for (const auto& state : function.stateArguments) {
                arguments.push_back(state == "next_block" ? std::to_string(std::max(0, blocks[n].yes)) :
                                    state == "else_block" ? std::to_string(std::max(0, blocks[n].no)) : state);
                widths.push_back(stateBits.at(state));
            }
            for (const auto& value : call.arguments) {
                arguments.push_back(signalName(value));
                widths.push_back(blockSymbols.at(value).width);
            }
            const auto outputs = functionOutputs(function);
            std::vector<std::string> previous;
            for (const auto& argument : arguments) previous.push_back(current(argument));
            std::string enabled = current("active") + "[" + std::to_string(n) + "] && " + current("fault") + " == 0";
            for (auto i : outputs) {
                std::string next = "hls_step_" + std::to_string(n) + "__" + arguments[i];
                out << "  logic [" << widths[i] - 1 << ":0] " << next << ";\n";
                versions[arguments[i]] = next;
            }
            for (auto& argument : arguments) argument = current(argument);
            out << "  always_comb begin : hls_step_" << n << "\n";
            if (sharedMemory) out << "    localparam int CALL_RESULT_BITS = " << std::max(size_t(1), functionResultBits.at(function.body.name)) << ";\n";
            out << "    logic [CALL_RESULT_BITS-1:0] hls_call_result;\n    hls_call_result = '0;\n";
            for (auto i : outputs) out << "    " << arguments[i] << " = " << previous[i] << ";\n";
            out << "    if (" << enabled << ") begin " << valueFunctionCall(function, arguments) << "; end\n  end\n";
        }
        std::string commitFault = current("fault");
        if (sharedMemory) {
            out << "  wire [31:0] memory_commit_fault = " << current("fault") << " != 0 ? " << current("fault")
                << " : ((" << current("memory_read") << " || " << current("memory_write")
                << ") && !hls_storage_address_valid(" << current("memory_address") << ", " << current("memory_size")
                << ") ? 32'd3 : 32'd0);\n";
            commitFault = "memory_commit_fault";
        }
        if (blockRam) emitBlockRamPorts(out, name, current("memory_address"), current("memory_write_data"),
            current("memory_size"), current("memory_read"), current("memory_write"), commitFault);
        out << "  always_ff @(posedge clk) begin\n";
        if (sharedMemory) for (const std::string pin : {"operation_in", "index_in", "value_in"})
            out << "    if (reset) hls_accepted_" << pin << " <= 0;\n"
                << "    else if (command_valid_in && command_ready_out) hls_accepted_" << pin << " <= " << pin << ";\n";
        out << "    values_reg.unused_bit <= 0;\n";
        for (const auto& name : clockedValues)
            out << "    values_reg." << registerName(name) << " <= reset ? '0 : " << current(signalName(name)) << ";\n";
        if (blockRam) {
            emitBlockRamTick(out, current("memory_address"));
        } else if (sharedMemory) {
            out << "    if (!reset && " << commitFault << " == 0) begin\n"
                << "      if (" << current("memory_read") << ") memory_read_data <= hls_storage_load_" << portBytes * 8 << "(";
            for (const auto& bank : storageBankNames()) out << bank << "_reg, ";
            out << current("memory_address") << ");\n";
            for (unsigned bank = 0; bank < 8; ++bank)
                out << "      if (" << current("memory_write") << ") storage_lane" << bank << "_reg <= hls_bank_write_port_" << bank
                    << "(storage_lane" << bank << "_reg, " << current("memory_address") << ", " << current("memory_write_data")
                    << ", 32'd0, " << current("memory_size") << ");\n";
            out << "    end\n";
        } else for (const auto& bank : storageBankNames())
            out << "    if (!reset) " << bank << "_reg <= " << current(bank) << ";\n";
        for (const std::string value : {"phase", "fault", "result", "pending", "booting", "heap_next"}) {
            out << "    " << value << "_reg <= ";
            if (value == "phase" || value == "pending" || value == "booting")
                out << "(" << commitFault << " != 0 && fault_reg == 0) ? " << (value == "pending" ? "1" : "0") << " : ";
            out << (value == "fault" ? commitFault : current(value)) << ";\n";
        }
        out << "  end\nendmodule\n";
        if (blockRam) emitBlockRamModule(out, name);
        return out.str();
    }
};
}

std::string generateDelayed(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, const std::string& name) {
    return DelayedScheduler(ctx, sema).generate(record, name);
}
std::map<std::string, graph::Value> exportDelayedGraph(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record, graph::Graph& graph, const std::string& scope) {
    DelayedScheduler scheduler(ctx, sema);
    scheduler.generate(record, scope, &graph);
    return std::move(scheduler.graphPorts);
}
synth::ScheduledDesign lowerPipelineSource(clang::ASTContext& ctx, clang::Sema& sema,
    const clang::CXXRecordDecl* record) {
    synth::ScheduledDesign design;
    DelayedScheduler(ctx, sema).generate(record, "", nullptr, &design);
    return design;
}
}

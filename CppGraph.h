#pragma once

#include "include/cpphdl_graph.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/Stack.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Scope.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/Support/SaveAndRestore.h"
#include <memory>
#include <exception>

namespace cpphdl::cpp_graph {
using namespace clang;
using namespace cpphdl::graph;
using clang::Expr;

// This frontend consumes the ordinary C++ model, not the original RTL. Keep
// logical bit layout separate from C++ object layout: padding, proxy objects
// and function_ref closures are implementation details, not hardware wires.
class Lowering {
    struct Closure;
    struct Item {
        Value bits;
        std::string key;
        QualType type;
        Value offset = constant(0, 64);
        unsigned extent = 0;
        unsigned selectedWidth = 0;
        Value rangeMask, rangeLow;
        bool bitSelection = false;
        bool readOnlyWideRange = false;
        std::shared_ptr<Closure> closure;
        std::optional<size_t> memory;
        Value memoryAddress;
    };
    using Locals = std::map<const ValueDecl*, Item>;
    struct Closure { const LambdaExpr* expression; Item object; Locals locals; };
    // Branch snapshots share immutable bit vectors. Copying every wide bus at
    // each nested condition makes frontend memory grow with call depth.
    struct Environment : std::map<std::string, std::shared_ptr<const Value>> {
        const Value& at(const std::string& key) const { return *std::map<std::string, std::shared_ptr<const Value>>::at(key); }
        void set(const std::string& key, Value bits) { (*this)[key] = std::make_shared<const Value>(std::move(bits)); }
    };
    ASTContext& context;
    Sema& sema;
    Graph graph;
    Item object;
    Locals locals;
    Environment cells;
    std::map<std::string, Value> originals, results;
    std::map<std::string, std::shared_ptr<Closure>> bindings;
    std::map<std::string, Value> registers;
    std::map<std::string, Value> blockingStates;
    std::map<std::string, const FieldDecl*> moduleFields;
    std::set<std::string> combinationalWrites;
    struct GetterReads {
        Environment fields;
        std::set<std::shared_ptr<const GetterReads>> producers;
    };
    std::map<std::string, std::shared_ptr<GetterReads>> getterReads;
    Environment originalReadVersions;
    std::vector<std::string> activeGetters;
    std::map<QualType, unsigned> widths;
    std::vector<std::string> stack;
    unsigned serial = 0;
    std::vector<std::string> localNames;
    bool structural = false;
    bool committing = false;
    bool working = false;
    bool concatenating = false;
    std::vector<std::pair<Value, bool>> effectPath;
    Value previousEffect;
    unsigned effectCount = 0;
    std::set<std::string> effectfulResults;
    std::set<std::string> committed;
    std::map<std::string, size_t> memories;
    std::set<size_t> appliedMemories;
    std::set<std::string> memoryGetters;
    std::set<const CXXRecordDecl*> validatedModules;
    struct NetWrites { std::string key; std::vector<unsigned> counts; };
    std::vector<NetWrites> netWrites;
    std::vector<std::string> producerStorage;
    // Exit predicates are relative to entry into a statement. In particular,
    // break belongs to the nearest loop/switch, whereas return crosses both.
    struct Flow {
        Value normal{1}, breaks{0}, continues{0}, returns{0};
        std::optional<Item> result;
    };

    [[noreturn]] void fail(const std::string& message, const Stmt* statement = nullptr) {
        std::string location;
        if (statement) location = " at " + statement->getBeginLoc().printToString(context.getSourceManager());
        for (const auto& entry : stack) location += "\n  in " + entry;
        throw std::runtime_error(message + location);
    }
    QualType clean(QualType type) { return type.getNonReferenceType().getUnqualifiedType().getCanonicalType(); }
    const ClassTemplateSpecializationDecl* specialization(QualType type) {
        return dyn_cast_or_null<ClassTemplateSpecializationDecl>(clean(type)->getAsCXXRecordDecl());
    }
    std::string templateName(QualType type) {
        auto record = specialization(type);
        return record ? record->getSpecializedTemplate()->getQualifiedNameAsString() : "";
    }
    bool module(QualType type) {
        if (type.isNull()) return false;
        auto record = clean(type)->getAsCXXRecordDecl();
        if (!record || !record->hasDefinition()) return false;
        record = record->getDefinition();
        if (record->getQualifiedNameAsString() == "cpphdl::Module") return true;
        for (const auto& base : record->bases()) if (module(base.getType())) return true;
        return false;
    }
    bool port(QualType type) { return templateName(type) == "cpphdl::function_ref"; }
    bool packedArray(QualType type) {
        return templateName(type) == "cpphdl::array" &&
            specialization(type)->getTemplateArgs()[2].getAsIntegral().getBoolValue();
    }
    std::optional<unsigned> integerWrapperWidth(QualType type) {
        if (templateName(type) == "cpphdl::u")
            return specialization(type)->getTemplateArgs()[0].getAsIntegral().getLimitedValue();
        auto record = clean(type)->getAsCXXRecordDecl();
        if (!record) return {};
        static const std::map<std::string, unsigned> wrappers{
            {"cpphdl::u1", 8}, {"cpphdl::u8", 8}, {"cpphdl::u16", 16},
            {"cpphdl::u32", 32}, {"cpphdl::u64", 64}, {"cpphdl::i8", 8},
            {"cpphdl::i16", 16}, {"cpphdl::i32", 32}, {"cpphdl::i64", 64}};
        auto found = wrappers.find(record->getQualifiedNameAsString());
        return found == wrappers.end() ? std::optional<unsigned>{} : found->second;
    }
    QualType payload(QualType type) {
        auto name = templateName(type);
        if (name == "cpphdl::function_ref" || name == "cpphdl::reg")
            return specialization(type)->getTemplateArgs()[0].getAsType();
        return clean(type);
    }
    bool containsModule(QualType type) {
        if (type.isNull()) return false;
        type = payload(type);
        if (module(type)) return true;
        auto name = templateName(type);
        if (name == "cpphdl::array") return containsModule(specialization(type)->getTemplateArgs()[1].getAsType());
        if (name == "std::array") return containsModule(specialization(type)->getTemplateArgs()[0].getAsType());
        if (auto array = context.getAsConstantArrayType(type)) return containsModule(array->getElementType());
        return false;
    }
    std::optional<uint64_t> evaluated(const Expr* expression) {
        Expr::EvalResult result;
        if (!expression || expression->isValueDependent() || expression->isInstantiationDependent()) return {};
        if (expression->EvaluateAsInt(result, context) && !result.HasSideEffects && result.Val.isInt() &&
            result.Val.getInt().getBitWidth() <= 64)
            return result.Val.getInt().getLimitedValue();
        return {};
    }
    void instantiate(const FunctionDecl* declaration) {
        // External host functions have no template body to instantiate. Reject
        // them explicitly rather than asking Clang to instantiate a null pattern.
        // Lowering requests each reachable body itself. Recursive instantiation
        // also materializes pending std::function/runtime implementations that
        // graph primitives bypass; on CVA6 those unused ASTs cost over a GiB.
        if (!declaration->getBody() && declaration->getTemplateInstantiationPattern())
            sema.InstantiateFunctionDefinition(declaration->getLocation(), const_cast<FunctionDecl*>(declaration), false, false, true);
        if (!declaration->getBody()) fail("missing instantiated C++ body: " + declaration->getQualifiedNameAsString());
    }
    void validateModule(QualType type) {
        auto record = clean(type)->getAsCXXRecordDecl();
        if (!validatedModules.insert(record).second) return;
        for (auto constructor : record->ctors()) if (constructor->isUserProvided() && constructor->isDefaultConstructor()) {
            // We check constructors, but never execute them. Instantiating an
            // empty template constructor also instantiates every child object's
            // initialization, which is unnecessary for graph elaboration.
            auto definition = constructor;
            if (!definition->getBody()) {
                if (auto pattern = dyn_cast_or_null<CXXConstructorDecl>(constructor->getTemplateInstantiationPattern()))
                    definition = pattern;
            }
            instantiate(definition);
            auto body = dyn_cast<CompoundStmt>(definition->getBody());
            if (!body || !body->body_empty()) fail("nonempty module constructor unsupported", definition->getBody());
            for (auto initializer : definition->inits()) if (initializer->isWritten()) fail("explicit module initialization unsupported");
        }
        for (auto candidate : record->methods()) {
            auto name = candidate->getNameAsString();
            if (name == "_work_neg" || name == "_strobe_neg") {
                instantiate(candidate);
                auto body = dyn_cast<CompoundStmt>(candidate->getBody());
                if (!body || !body->body_empty()) fail("negative-phase C++ lifecycle unsupported", candidate->getBody());
            }
        }
    }
    unsigned width(QualType type) {
        type = clean(type);
        if (auto found = widths.find(type); found != widths.end()) return found->second;
        if (type->isBooleanType()) return 1;
        if (type->isIntegerType() || type->isEnumeralType()) return context.getTypeSize(type);
        if (type->isPointerType()) return width(type->getPointeeType());
        if (auto count = integerWrapperWidth(type)) return *count;
        auto name = templateName(type);
        auto spec = specialization(type);
        unsigned result = 0;
        if (name == "cpphdl::logic") result = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue();
        else if (name == "cpphdl::cat") {
            for (const auto& argument : spec->getTemplateArgs()[0].pack_elements()) result += argument.getAsIntegral().getLimitedValue();
        }
        else if (name == "cpphdl::reg" || name == "cpphdl::function_ref") result = width(payload(type));
        else if (name == "cpphdl::array") result = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue() * width(spec->getTemplateArgs()[1].getAsType());
        else if (name == "cpphdl::memory_row") result = memoryRowWidth(type);
        else if (name == "std::array") result = spec->getTemplateArgs()[1].getAsIntegral().getLimitedValue() * width(spec->getTemplateArgs()[0].getAsType());
        else if (auto array = context.getAsConstantArrayType(type)) result = array->getSize().getLimitedValue() * width(array->getElementType());
        else if (auto record = type->getAsCXXRecordDecl()) {
            for (auto method : record->methods()) if (method->getNameAsString() == "_size_bits") {
                instantiate(method);
                auto saved = object;
                object.type = type;
                auto returned = invoke(method, object, {});
                result = integer(returned);
                object = saved;
                break;
            }
        }
        if (!result || result > 1048576) fail("unsupported C++ hardware type: " + type.getAsString());
        widths[type] = result;
        return result;
    }
    unsigned valueWidth(QualType type) {
        // SV permits a zero-count replication as an empty concat operand.
        // hdlcpp represents it with temporary logic<0> values (including the
        // repeat() result). Never relax width() for addressable storage/ports.
        if (concatenating && templateName(type) == "cpphdl::logic" &&
            specialization(type)->getTemplateArgs()[0].getAsIntegral() == 0)
            return 0;
        return width(type);
    }
    Item value(Value bits, QualType type = {}) { Item result; result.bits = std::move(bits); result.type = type; return result; }
    unsigned memoryRowWidth(QualType type) {
        auto spec = specialization(type);
        auto element = spec->getTemplateArgs()[0].getAsType();
        auto count = spec->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
        auto bits = width(element);
        if (templateName(element) != "cpphdl::logic" || context.getTypeSize(element) != bits)
            fail("C++ memory requires unpadded logic elements");
        if (!count || count > 1048576 / bits) fail("unsupported C++ memory row width");
        return count * bits;
    }
    size_t memoryId(const Item& item) {
        if (item.key.empty() || item.key.starts_with("$")) fail("C++ memory must be module storage");
        if (auto found = memories.find(item.key); found != memories.end()) return found->second;
        auto depth = specialization(item.type)->getTemplateArgs()[2].getAsIntegral().getLimitedValue();
        if (!depth) fail("zero-depth C++ memory unsupported");
        auto index = graph.memories.size();
        graph.memories.push_back({item.key, memoryRowWidth(item.type), depth});
        memories[item.key] = index;
        return index;
    }
    Value executionGuard() {
        Value enabled{1};
        for (const auto& [condition, positive] : effectPath)
            enabled = graph.binary("and", enabled, positive ? condition : graph.unary("not", condition), 1);
        return enabled;
    }
    Item memoryRow(Item base, Item index, QualType type, bool pending) {
        if (structural || committing) fail("C++ memory access outside evaluation");
        if (pending && !working) fail("pending C++ memory read requires a work transaction");
        auto identity = memoryId(base);
        auto address = resize(read(index), 64);
        auto count = graph.memories[identity].width;
        auto enabled = executionGuard();
        graph.memoryAccesses.push_back({identity, address, enabled, working});
        for (const auto& getter : activeGetters) memoryGetters.insert(getter);
        Value bits;
        for (unsigned offset = 0; offset < count; offset += 64) {
            auto part = graph.add("memory_read", std::min(64u, count - offset), address,
                                  constant(identity, 64), constant(offset / 64, 64));
            bits.insert(bits.end(), part.begin(), part.end());
        }
        // pending() forwards queued whole rows in program order; ordinary
        // operator[] reads always see committed storage, even after a write.
        if (pending) for (const auto& write : graph.memoryWrites) if (write.memory == identity) {
            auto match = graph.binary("and", write.enabled, graph.binary("eq", address, write.address, 1), 1);
            bits = graph.mux(match, write.data, bits);
        }
        Item result; result.key = "$memoryrow" + std::to_string(++serial); result.type = type;
        result.memory = identity; result.memoryAddress = address;
        originals[result.key] = bits; cells.set(result.key, bits); localNames.push_back(result.key);
        return result;
    }
    void queueMemory(const Item& row) {
        if (!working || structural || committing || !producerStorage.empty())
            fail("C++ memory write requires a work transaction outside combinational getters");
        if (!row.memory) fail("C++ memory row lost its destination");
        graph.memoryWrites.push_back({*row.memory, row.memoryAddress, read(row), executionGuard()});
        ++effectCount;
    }
    uint64_t packedOffset(QualType type, const FieldDecl* field) {
        auto record = clean(type)->getAsCXXRecordDecl();
        for (auto declaration : record->decls()) if (auto variable = dyn_cast<VarDecl>(declaration))
            if (variable->getNameAsString() == "__hdlcpp_offset_" + field->getNameAsString()) {
                if (!variable->getInit()) sema.InstantiateVariableDefinition(variable->getLocation(), variable, true);
                if (auto offset = evaluated(variable->getInit())) return *offset;
            }
        fail("missing packed field metadata: " + field->getNameAsString() + " in " + type.getAsString());
    }
    Item constantObject(const APValue& constantValue, QualType type) {
        if (constantValue.isInt()) {
            const auto& integer = constantValue.getInt();
            Value bits(width(type), 0);
            for (unsigned bit = 0; bit < bits.size(); ++bit)
                bits[bit] = bit < integer.getBitWidth() ? integer[bit] : integer.isSigned() && integer.isNegative();
            return value(bits, type);
        }
        if (constantValue.isArray()) {
            auto array = context.getAsConstantArrayType(type);
            if (!array) fail("unsupported C++ constant array: " + type.getAsString());
            Value bits;
            for (unsigned index = 0; index < constantValue.getArraySize(); ++index) {
                const auto& element = index < constantValue.getArrayInitializedElts() ?
                    constantValue.getArrayInitializedElt(index) : constantValue.getArrayFiller();
                auto part = constantObject(element, array->getElementType()).bits;
                bits.insert(bits.end(), part.begin(), part.end());
            }
            if (bits.size() != width(type)) fail("constant array size mismatch");
            return value(bits, type);
        }
        if (constantValue.isStruct()) {
            auto record = clean(type)->getAsCXXRecordDecl();
            auto name = templateName(type);
            bool storageWrapper = name == "cpphdl::logic" || name == "cpphdl::array" ||
                name == "std::array" || integerWrapperWidth(type).has_value();
            Value result(width(type), 0);
            std::vector<bool> covered(result.size(), false);
            unsigned index = 0;
            for (auto member : record->fields()) {
                auto part = constantObject(constantValue.getStructField(index++), member->getType()).bits;
                if (storageWrapper) {
                    if (index != 1 || constantValue.getStructNumFields() != 1 || part.size() < result.size())
                        fail("unsupported C++ constant storage: " + type.getAsString());
                    return value(resize(part, result.size()), type);
                }
                auto offset = packedOffset(type, member);
                if (offset > result.size() || part.size() > result.size() - offset)
                    fail("constant packed field out of bounds: " + member->getNameAsString());
                for (unsigned bit = 0; bit < part.size(); ++bit) {
                    if (covered[offset + bit]) fail("overlapping constant packed fields");
                    result[offset + bit] = part[bit]; covered[offset + bit] = true;
                }
            }
            if (record->getNumBases() || std::find(covered.begin(), covered.end(), false) != covered.end())
                fail("incomplete constant packed layout: " + type.getAsString());
            return value(result, type);
        }
        fail("unsupported C++ constant object: " + type.getAsString());
    }
    Value initial(const Item& item) {
        if (!originals.count(item.key)) {
            auto count = item.extent ? item.extent : width(item.type);
            bool state = templateName(item.type) == "cpphdl::reg";
            originals[item.key] = graph.wire(count, item.key, state ? "state" : "wire");
            if (item.key.starts_with("$")) localNames.push_back(item.key);
            if (state) registers[item.key] = originals[item.key];
        }
        return originals.at(item.key);
    }
    Value read(Item item) {
        if (item.closure) return read(invokeClosure(item.closure));
        if (!item.rangeMask.empty()) {
            auto mask = std::move(item.rangeMask); item.rangeMask.clear();
            return graph.binary("shr", graph.binary("and", read(item), mask, mask.size()), item.rangeLow, mask.size());
        }
        if (item.key.empty()) return item.bits;
        if (port(item.type)) {
            if (bindings.count(item.key)) return read(boundPort(item));
        }
        auto source = cells.count(item.key) ? cells.at(item.key) : initial(item);
        if (moduleFields.count(item.key) && !activeGetters.empty()) {
            auto current = cells.find(item.key);
            if (current == cells.end() && !originalReadVersions.count(item.key)) originalReadVersions.set(item.key, source);
            auto version = current != cells.end() ? current->second : originalReadVersions.find(item.key)->second;
            getterReads.at(activeGetters.back())->fields[item.key] = version;
        }
        unsigned count = item.selectedWidth ? item.selectedWidth : width(item.type);
        if (auto offset = number(graph.resolved(item.offset))) return slice(source, *offset, count);
        Value result(count, 0);
        for (unsigned offset = 0; offset + count <= source.size(); offset += count)
            result = graph.mux(graph.binary("eq", item.offset, constant(offset, 64), 1), slice(source, offset, count), result);
        return result;
    }
    uint64_t integer(Item item) {
        auto known = number(graph.resolved(read(item)));
        if (!known) fail("expected a static C++ value");
        return *known;
    }
    Item cast(Item item, QualType type) {
        auto bits = read(item);
        if (clean(type)->isBooleanType()) bits = graph.unary("any", bits);
        else bits = resize(bits, valueWidth(type), !item.type.isNull() && clean(item.type)->isSignedIntegerOrEnumerationType());
        auto result = value(bits, type);
        if (templateName(type) == "cpphdl::memory_row") {
            result.memory = item.memory; result.memoryAddress = item.memoryAddress;
        }
        return result;
    }
    Item boundPort(const Item& portItem) {
        auto result = invokeClosure(bindings.at(portItem.key));
        auto type = payload(portItem.type);
        // function_ref<A> converts value-returning bindings to A. Do this
        // before selecting primitives (e.g. logic::bits), not just on write.
        // Pointer-backed bindings already return their pointee Item; retain
        // its reference/storage metadata when it has the declared value type.
        if (context.hasSameType(clean(result.type), clean(type))) return result;
        return cast(result, type);
    }
    Value wideShiftRight(Value bits, const Value& amount) {
        // Only the >64-bit slice path calls this. Constant shifts are wiring;
        // mux() splits each barrel stage into native nodes of at most 64 bits.
        const unsigned count = bits.size();
        for (unsigned shift = 1, bit = 0; shift < count; shift <<= 1, ++bit)
            bits = graph.mux(slice(amount, bit, 1), slice(bits, shift, count), bits);
        // Do not alias oversized offsets by ignoring their upper bits.
        return graph.mux(graph.binary("lt", amount, constant(count, 64), 1), bits, Value(count, 0));
    }
    Value wideSliceRead(Value source, const Value& high, const Value& low) {
        const unsigned count = source.size();
        auto valid = graph.binary("and", graph.binary("lt", high, constant(count, 64), 1),
            graph.unary("not", graph.binary("lt", high, low, 1)), 1);
        auto last = graph.binary("sub", high, low, 64);
        auto padding = graph.binary("sub", constant(count - 1, 64), last, 64);
        auto mask = wideShiftRight(Value(count, 1), padding);
        auto selected = wideShiftRight(std::move(source), low);
        return graph.mux(valid, graph.binary("and", selected, mask, count), Value(count, 0));
    }
    void write(Item target, Item source) {
        if (target.readOnlyWideRange)
            fail("wide or nested dynamic C++ bit range unsupported: write to a wide dynamic slice");
        if (source.closure) {
            if (!structural || target.key.empty() || !port(target.type)) fail("closure assignment outside structural port binding");
            bindings[target.key] = source.closure;
            return;
        }
        if (target.key.empty()) fail("assignment to non-addressable C++ value");
        if (!target.rangeMask.empty()) {
            for (const auto& net : netWrites) if (net.key == target.key) fail("dynamic destination in concurrent C++ net");
            auto mask = std::move(target.rangeMask); target.rangeMask.clear();
            auto incoming = resize(read(source), mask.size(),
                !source.type.isNull() && clean(source.type)->isSignedIntegerOrEnumerationType());
            incoming = graph.binary("shl", incoming, target.rangeLow, mask.size());
            auto kept = graph.binary("and", read(target), graph.unary("not", mask), mask.size());
            write(target, value(graph.binary("or", kept, graph.binary("and", incoming, mask, mask.size()), mask.size())));
            return;
        }
        if (!producerStorage.empty() && !target.key.starts_with("$") && target.key != producerStorage.back())
            fail("combinational getter mutates another field: " + target.key);
        if (templateName(target.type) == "cpphdl::reg" || registers.count(target.key))
            fail("direct current-state C++ mutation unsupported: " + target.key);
        if (moduleFields.count(target.key)) {
            if (working && producerStorage.empty()) {
                if (combinationalWrites.count(target.key)) fail("mixed work/combinational C++ writes: " + target.key);
                if (moduleFields.at(target.key)->hasInClassInitializer()) fail("initialized C++ work storage unsupported: " + target.key);
                if (!blockingStates.count(target.key)) {
                    // Blocking writes in _work are retained transaction state,
                    // not undriven combinational scratch. SSA reads within the
                    // transaction see writes immediately; outputs settle after commit.
                    auto previous = initial(target);
                    auto state = graph.wire(previous.size(), target.key, "state");
                    graph.connect(previous, state);
                    originals[target.key] = state;
                    originalReadVersions.erase(target.key);
                    blockingStates[target.key] = state;
                }
            } else if (!structural && !committing) {
                if (blockingStates.count(target.key)) fail("mixed work/combinational C++ writes: " + target.key);
                combinationalWrites.insert(target.key);
            } else fail("module storage write outside C++ work/getter: " + target.key);
        }
        auto incoming = target.bitSelection ? resize(read(source), 1) :
            target.selectedWidth ? resize(read(source), target.selectedWidth,
                !source.type.isNull() && clean(source.type)->isSignedIntegerOrEnumerationType()) :
            read(cast(source, target.type));
        auto previous = cells.count(target.key) ? cells.at(target.key) : initial(target);
        auto offset = number(graph.resolved(target.offset));
        if (offset) {
            if (*offset + incoming.size() > previous.size()) fail("C++ write out of bounds");
            for (auto& net : netWrites) if (net.key == target.key)
                for (unsigned index = 0; index < incoming.size(); ++index)
                    if (graph.resolve(incoming[index]) != graph.resolve(previous[*offset + index])) ++net.counts[*offset + index];
            std::copy(incoming.begin(), incoming.end(), previous.begin() + *offset);
        } else {
            for (const auto& net : netWrites) if (net.key == target.key) fail("dynamic destination in concurrent C++ net");
            for (unsigned start = 0; start + incoming.size() <= previous.size(); start += incoming.size()) {
                auto replacement = graph.mux(graph.binary("eq", target.offset, constant(start, 64), 1), incoming, slice(previous, start, incoming.size()));
                std::copy(replacement.begin(), replacement.end(), previous.begin() + start);
            }
        }
        cells.set(target.key, std::move(previous));
    }
    void merge(const Value& condition, const Environment& before,
               const Environment& yes, const Environment& no) {
        std::set<std::string> keys;
        for (const auto& [key, bits] : yes) keys.insert(key);
        for (const auto& [key, bits] : no) keys.insert(key);
        for (const auto& key : keys) {
            auto yesValue = yes.find(key), noValue = no.find(key);
            if (yesValue != yes.end() && noValue != no.end() && yesValue->second == noValue->second) {
                cells[key] = yesValue->second;
                continue;
            }
            const auto& fallback = before.count(key) ? before.at(key) : originals.at(key);
            cells.set(key, graph.mux(condition, yes.count(key) ? yes.at(key) : fallback, no.count(key) ? no.at(key) : fallback));
        }
    }
    Item field(Item base, const ValueDecl* declaration) {
        auto name = declaration->getNameAsString();
        if (auto variable = dyn_cast<VarDecl>(declaration)) {
            if (auto known = evaluated(variable->getInit())) return value(constant(*known, width(variable->getType())), variable->getType());
            if (variable->hasInit()) return expr(variable->getInit());
        }
        auto member = dyn_cast<FieldDecl>(declaration);
        if (!member) fail("unsupported member " + name);
        if (module(base.type)) {
            validateModule(base.type);
            Item result; result.key = base.key + "." + name; result.type = member->getType();
            moduleFields[result.key] = member;
            if (templateName(result.type) == "cpphdl::reg" && member->hasInClassInitializer()) fail("initialized C++ register unsupported");
            if (templateName(result.type) == "cpphdl::memory" && member->hasInClassInitializer()) fail("initialized C++ memory unsupported");
            if (port(result.type) && !bindings.count(result.key) && member->hasInClassInitializer()) {
                if (!member->getInClassInitializer() &&
                    sema.BuildCXXDefaultInitExpr(member->getLocation(), const_cast<FieldDecl*>(member)).isInvalid())
                    fail("cannot instantiate C++ port initializer");
                auto saved = object; object = base;
                auto initializer = expr(member->getInClassInitializer());
                object = saved;
                if (initializer.closure) bindings[result.key] = initializer.closure;
                else fail("non-closure port initializer");
            }
            return result;
        }
        if (name == "_next" && templateName(base.type) == "cpphdl::reg") {
            initial(base);
            base.key += "._next"; base.type = member->getType(); base.extent = width(base.type);
            return base;
        }
        auto record = payload(base.type)->getAsCXXRecordDecl();
        if (templateName(base.type) == "cpphdl::reg") initial(base);
        if (!record) fail("field of non-record");
        auto offset = packedOffset(payload(base.type), member);
        if (base.key.empty()) return value(slice(read(base), offset, width(member->getType())), member->getType());
        if (!base.extent) base.extent = width(base.type);
        base.offset = graph.binary("add", base.offset, constant(offset, 64), 64);
        base.type = member->getType();
        return base;
    }
    Item index(Item base, Item indexValue, QualType resultType) {
        auto sourceType = payload(base.type);
        if (templateName(base.type) == "cpphdl::reg") initial(base);
        auto name = templateName(sourceType);
        if (name == "cpphdl::memory") return memoryRow(base, indexValue, resultType, false);
        unsigned count = 0, elementWidth = 1;
        QualType elementType = context.BoolTy;
        if (name == "cpphdl::array") {
            auto spec = specialization(sourceType);
            count = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue();
            elementType = spec->getTemplateArgs()[1].getAsType();
        } else if (name == "cpphdl::memory_row") {
            auto spec = specialization(sourceType);
            count = spec->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
            elementType = spec->getTemplateArgs()[0].getAsType();
        } else if (name == "std::array") {
            auto spec = specialization(sourceType);
            count = spec->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
            elementType = spec->getTemplateArgs()[0].getAsType();
        } else if (auto array = context.getAsConstantArrayType(sourceType)) {
            count = array->getSize().getLimitedValue(); elementType = array->getElementType();
        } else if (name == "cpphdl::logic" || integerWrapperWidth(sourceType)) { count = width(sourceType); base.bitSelection = true; }
        else fail("unsupported C++ indexing: " + sourceType.getAsString());
        // An intermediate dimension of a module array is still hierarchy,
        // not a packed value whose element needs a bit width.
        if (containsModule(elementType)) {
            base.key += "[" + std::to_string(integer(indexValue)) + "]";
            base.type = elementType;
            return base;
        }
        elementWidth = width(elementType);
        auto offset = graph.binary("mul", resize(read(indexValue), 64), constant(elementWidth, 64), 64);
        if (base.key.empty()) {
            auto source = read(base);
            if (auto known = number(graph.resolved(offset))) return value(slice(source, *known, elementWidth), elementType);
            Value result(elementWidth, 0);
            for (unsigned index = 0; index < count; ++index)
                result = graph.mux(graph.binary("eq", read(indexValue), constant(index, read(indexValue).size()), 1), slice(source, index * elementWidth, elementWidth), result);
            return value(result, elementType);
        }
        if (!base.extent) base.extent = count * elementWidth;
        base.offset = graph.binary("add", base.offset, offset, 64);
        base.type = elementType;
        return base;
    }
    Item binary(std::string op, Item left, Item right, QualType type) {
        auto lhs = read(left), rhs = read(right);
        unsigned count = width(type);
        if (op == "&&" || op == "||") return value(graph.binary(op == "&&" ? "and" : "or", graph.unary("any", lhs), graph.unary("any", rhs), 1), type);
        bool invert = op == "!=" || op == ">=" || op == "<=";
        if (op == ">" || op == "<=") { std::swap(lhs, rhs); op = "<"; }
        if (op == ">=") op = "<";
        if (op == "!=") op = "==";
        static const std::map<std::string, std::string> names{{"+","add"},{"-","sub"},{"*","mul"},{"/","div"},{"%","mod"},{"&","and"},{"|","or"},{"^","xor"},{"<<","shl"},{">>","shr"},{"==","eq"},{"<","lt"}};
        if (!names.count(op)) fail("unsupported C++ operator " + op);
        auto operation = names.at(op);
        bool sign = !left.type.isNull() && clean(left.type)->isSignedIntegerOrEnumerationType();
        if (sign && operation == "lt") operation = "slt";
        if (sign && operation == "shr") operation = "sar";
        if (sign && (operation == "div" || operation == "mod")) fail("signed dynamic division unsupported");
        auto result = graph.binary(operation, lhs, rhs, count);
        if (invert) result = graph.unary("not", result);
        return value(result, type);
    }
    void bindArguments(const FunctionDecl* function, const std::vector<Item>& arguments) {
        if (function->getNumParams() != arguments.size()) fail("missing C++ call argument");
        for (unsigned index = 0; index < arguments.size(); ++index) {
            auto parameter = function->getParamDecl(index);
            auto type = parameter->getType();
            if (type->isReferenceType() || type->isPointerType() || arguments[index].closure) locals[parameter] = arguments[index];
            else {
                Item local; local.key = "$local" + std::to_string(++serial); local.type = type;
                write(local, arguments[index]);
                local.memory = arguments[index].memory; local.memoryAddress = arguments[index].memoryAddress;
                locals[parameter] = local;
            }
        }
    }
    Item invokeClosure(std::shared_ptr<Closure> closure, const FunctionDecl* function = nullptr,
                       const std::vector<Item>& arguments = {}) {
        if (!function) function = closure->expression->getCallOperator();
        instantiate(function);
        if (stack.size() > 256) fail("C++ call nesting limit");
        auto savedObject = object; auto savedLocals = locals;
        auto localBegin = localNames.size();
        object = closure->object; locals = closure->locals;
        bindArguments(function, arguments);
        stack.push_back("lambda");
        auto result = statement(function->getBody()).result;
        stack.pop_back();
        object = savedObject; locals = std::move(savedLocals);
        if (!structural) while (localNames.size() > localBegin) {
            cells.erase(localNames.back()); originals.erase(localNames.back()); localNames.pop_back();
        }
        if (!result && !function->getReturnType()->isVoidType()) fail("port closure has no return");
        return result.value_or(Item{});
    }
    const FieldDecl* returnedField(const FunctionDecl* function) {
        const FieldDecl* result = nullptr;
        bool valid = true;
        std::function<void(const Stmt*)> visit = [&](const Stmt* body) {
            if (!body) return;
            // A nested lambda's return belongs to its own call operator, not
            // to the enclosing cached getter's returned storage.
            if (isa<LambdaExpr>(body)) return;
            if (auto returned = dyn_cast<ReturnStmt>(body)) {
                auto expression = returned->getRetValue();
                auto member = expression ? dyn_cast<MemberExpr>(expression->IgnoreParenImpCasts()) : nullptr;
                auto field = member ? dyn_cast<FieldDecl>(member->getMemberDecl()) : nullptr;
                if (!field || !isa<CXXThisExpr>(member->getBase()->IgnoreParenImpCasts()) || (result && result != field)) valid = false;
                else result = field;
                return;
            }
            for (auto child : body->children()) visit(child);
        };
        visit(function->getBody());
        return valid ? result : nullptr;
    }
    bool staleGetter(const std::shared_ptr<const GetterReads>& reads) {
        if (blockingStates.empty()) return false;
        std::vector<const GetterReads*> pending{reads.get()};
        std::set<const GetterReads*> visited;
        while (!pending.empty()) {
            auto currentReads = pending.back(); pending.pop_back();
            if (!visited.insert(currentReads).second) continue;
            for (const auto& [field, version] : currentReads->fields) {
                if (!blockingStates.count(field)) continue;
                const auto& current = cells.count(field) ? cells.at(field) : originals.at(field);
                if (graph.resolved(*version) != graph.resolved(current)) return true;
            }
            for (const auto& producer : currentReads->producers) pending.push_back(producer.get());
        }
        return false;
    }
    void inheritGetterReads(const std::string& key) {
        if (activeGetters.empty()) return;
        std::shared_ptr<const GetterReads> reads = getterReads.at(key);
        // Forward references to an active net see its current dependencies,
        // not a back-edge into a mutable record (which would create a cycle).
        if (std::find(activeGetters.begin(), activeGetters.end(), key) != activeGetters.end())
            reads = std::make_shared<GetterReads>(*reads);
        getterReads.at(activeGetters.back())->producers.insert(std::move(reads));
    }
    Item invoke(const FunctionDecl* function, Item receiver, std::vector<Item> arguments) {
        if (module(receiver.type)) validateModule(receiver.type);
        instantiate(function);
        auto name = function->getNameAsString();
        std::string key = receiver.key + "::" + name;
        auto returnStorage = function->getReturnType()->isReferenceType() ? returnedField(function) : nullptr;
        bool cache = isa<CXXMethodDecl>(function) && module(receiver.type) && function->param_empty() && returnStorage;
        bool refresh = false;
        if (cache && results.count(key)) {
            if (effectfulResults.count(key)) fail("repeated effectful C++ getter unsupported: " + key);
            // Bounds guards and pending-row forwarding belong to this call's
            // path and write history. Re-lower them; pure read nodes still CSE.
            refresh = memoryGetters.count(key);
            // Share pure producers until an observed blocking-state version
            // changes. Rebuilding only those getters preserves sequential
            // visibility without duplicating the whole combinational graph.
            refresh |= staleGetter(getterReads.at(key));
            if (!refresh) {
                inheritGetterReads(key);
                return value(results.at(key), function->getReturnType());
            }
        }
        auto effectsBefore = effectCount;
        if (stack.size() > 256) fail("C++ call nesting limit");
        auto savedObject = object; auto savedLocals = locals;
        auto localBegin = localNames.size();
        object = receiver; locals.clear();
        bindArguments(function, arguments);
        Value output;
        if (cache) {
            output = graph.wire(width(function->getReturnType()), key); results[key] = output;
            // Store direct reads and shared producer snapshots, not a flattened
            // transitive map per ancestor. Flattening uses quadratic memory in
            // long producer chains. Replacing, rather than clearing, preserves
            // the dependency versions of parents that used an earlier result.
            getterReads[key] = std::make_shared<GetterReads>();
            auto storage = receiver.key + "." + returnStorage->getNameAsString();
            if (refresh && combinationalWrites.count(storage)) {
                // A new SSA version must not reconnect the old getter's net
                // aliases: earlier uses still describe the pre-write value.
                originals[storage] = graph.wire(width(returnStorage->getType()), storage);
                originalReadVersions.erase(storage);
                cells.erase(storage);
            }
        }
        bool concurrent = false;
        std::string storageName;
        if (cache && name.ends_with("_func")) {
            storageName = name.substr(0, name.size() - 5);
            auto record = clean(receiver.type)->getAsCXXRecordDecl();
            for (auto declaration : record->decls()) if (auto variable = dyn_cast<VarDecl>(declaration))
                if (variable->getNameAsString() == "__cpphdl_net_" + storageName) {
                    if (!variable->getInit()) sema.InstantiateVariableDefinition(variable->getLocation(), variable, true);
                    concurrent = evaluated(variable->getInit()).value_or(0) == 1;
                }
            if (concurrent) netWrites.push_back({receiver.key + "." + storageName, std::vector<unsigned>(output.size(), 0)});
        }
        stack.push_back(key);
        if (cache) activeGetters.push_back(key);
        if (cache) producerStorage.push_back(receiver.key + "." + returnStorage->getNameAsString());
        auto result = statement(function->getBody()).result;
        if (cache) producerStorage.pop_back();
        if (concurrent) {
            auto net = std::move(netWrites.back()); netWrites.pop_back();
            if (std::any_of(net.counts.begin(), net.counts.end(), [](unsigned count) { return count > 1; }))
                fail("multiple effective writes to concurrent C++ net: " + net.key);
            // Only explicitly declared nets allow forward references. Complete,
            // single-driver writes and the backend's acyclic schedule are both
            // required; an ordinary imperative getter receives no such license.
            const auto& original = originals.at(net.key);
            const auto& final = cells.at(net.key);
            for (unsigned index = 0; index < net.counts.size(); ++index)
                if (net.counts[index]) graph.connect({original[index]}, {final[index]});
        }
        if (cache) {
            if (!result) fail("combinational C++ method has no return");
            graph.connect(output, read(cast(*result, function->getReturnType())));
            if (effectCount != effectsBefore) effectfulResults.insert(key);
            activeGetters.pop_back();
            inheritGetterReads(key);
        }
        stack.pop_back(); object = savedObject; locals = std::move(savedLocals);
        if (!structural) while (localNames.size() > localBegin) {
            cells.erase(localNames.back()); originals.erase(localNames.back()); localNames.pop_back();
        }
        if (cache) return value(output, function->getReturnType());
        return result.value_or(Item{});
    }
    Item expr(const Expr* expression) {
        if (!expression) return {};
        if (!clang::isStackNearlyExhausted()) return exprImpl(expression);
        // Generated casts and producer calls can exhaust the host stack before
        // reaching our call-depth limit. Use Clang's split-stack mechanism;
        // propagate diagnostics on this thread rather than across its worker.
        Item result;
        std::exception_ptr error;
        sema.runWithSufficientStackSpace(expression->getExprLoc(), [&] {
            try { result = exprImpl(expression); }
            catch (...) { error = std::current_exception(); }
        });
        if (error) std::rethrow_exception(error);
        return result;
    }
    Item exprImpl(const Expr* expression) {
        if (auto wrapper = dyn_cast<ExprWithCleanups>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<MaterializeTemporaryExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<CXXBindTemporaryExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<ParenExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<CXXDefaultArgExpr>(expression)) return expr(wrapper->getExpr());
        if (auto wrapper = dyn_cast<CXXDefaultInitExpr>(expression)) return expr(wrapper->getExpr());
        if (auto wrapper = dyn_cast<CXXStdInitializerListExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto lambda = dyn_cast<LambdaExpr>(expression)) {
            if (lambda->isMutable()) fail("mutable C++ lambda unsupported", expression);
            auto captured = locals;
            for (const auto& capture : lambda->captures())
                if (capture.capturesVariable() && capture.getCaptureKind() == LCK_ByCopy && captured.count(capture.getCapturedVar())) {
                    auto& item = captured.at(capture.getCapturedVar());
                    item = value(read(item), item.type);
                }
            Item result; result.closure = std::make_shared<Closure>(Closure{lambda, object, std::move(captured)}); return result;
        }
        if (isa<CXXThisExpr>(expression)) return object;
        if (expression->getType()->isIntegralOrEnumerationType())
            if (auto known = evaluated(expression)) return value(constant(*known, width(expression->getType())), expression->getType());
        if (auto substitution = dyn_cast<SubstNonTypeTemplateParmExpr>(expression)) return expr(substitution->getReplacement());
        if (auto reference = dyn_cast<DeclRefExpr>(expression)) {
            if (locals.count(reference->getDecl())) return locals.at(reference->getDecl());
            if (auto parameter = dyn_cast<TemplateParamObjectDecl>(reference->getDecl())) return constantObject(parameter->getValue(), parameter->getType());
            if (auto known = evaluated(expression)) return value(constant(*known, width(expression->getType())), expression->getType());
            if (auto variable = dyn_cast<VarDecl>(reference->getDecl()); variable && variable->hasInit() && variable->isConstexpr()) {
                // Clang has already executed constexpr initializers. Serialize
                // their values using logical field offsets, not C++ padding or
                // a second interpreter for their generic helper lambdas.
                if (auto known = variable->evaluateValue()) return constantObject(*known, variable->getType());
                fail("unevaluated C++ constexpr object: " + variable->getNameAsString(), expression);
            }
            fail("unbound C++ declaration: " + reference->getDecl()->getNameAsString(), expression);
        }
        if (auto member = dyn_cast<MemberExpr>(expression)) return field(expr(member->getBase()), member->getMemberDecl());
        if (auto castExpr = dyn_cast<CastExpr>(expression)) {
            auto result = expr(castExpr->getSubExpr());
            if (castExpr->getCastKind() == CK_ToVoid) return {};
            if (result.closure || castExpr->getCastKind() == CK_NoOp || castExpr->getCastKind() == CK_LValueToRValue || castExpr->getCastKind() == CK_DerivedToBase || castExpr->getCastKind() == CK_UncheckedDerivedToBase) return result;
            return cast(result, castExpr->getType());
        }
        if (auto construct = dyn_cast<CXXConstructExpr>(expression)) {
            if (templateName(construct->getType()) == "cpphdl::cat") {
                auto count = width(construct->getType()); // An all-empty concat is invalid.
                llvm::SaveAndRestore scope(concatenating, true);
                std::vector<Item> parts;
                // Evaluate in C++ braced-initializer order, even for empty
                // operands, then pack least-significant bits first.
                for (auto argument : construct->arguments()) parts.push_back(expr(argument));
                Value bits;
                bits.reserve(count);
                for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
                    // Reference arguments are read by cat's constructor, after
                    // all argument expressions have finished evaluating.
                    auto incoming = read(*part);
                    bits.insert(bits.end(), incoming.begin(), incoming.end());
                }
                return value(bits, construct->getType());
            }
            if (!construct->getNumArgs()) return value(Value(valueWidth(construct->getType()), 0), construct->getType());
            auto result = expr(construct->getArg(0));
            if (result.closure) return result;
            return cast(result, construct->getType());
        }
        if (auto known = evaluated(expression)) return value(constant(*known, width(expression->getType())), expression->getType());
        if (auto init = dyn_cast<InitListExpr>(expression)) {
            Value result(width(expression->getType()), 0);
            if (init->getNumInits()) {
                auto type = clean(expression->getType());
                if (context.getAsConstantArrayType(type) || templateName(type) == "std::array") {
                    unsigned offset = 0;
                    for (auto element : init->inits()) {
                        auto bits = read(expr(element));
                        if (offset + bits.size() > result.size()) fail("aggregate initializer too wide", expression);
                        std::copy(bits.begin(), bits.end(), result.begin() + offset); offset += bits.size();
                    }
                } else if (auto record = type->getAsCXXRecordDecl()) {
                    Item temporary; temporary.key = "$aggregate" + std::to_string(++serial); temporary.type = type;
                    originals[temporary.key] = result; cells.set(temporary.key, result);
                    localNames.push_back(temporary.key);
                    unsigned index = 0;
                    for (auto member : record->fields()) {
                        if (index >= init->getNumInits()) break;
                        write(field(temporary, member), expr(init->getInit(index++)));
                    }
                    if (index != init->getNumInits()) fail("unsupported aggregate base initialization", expression);
                    result = read(temporary);
                } else fail("nonconstant aggregate initialization unsupported", expression);
            }
            return value(result, expression->getType());
        }
        if (isa<ImplicitValueInitExpr>(expression) || isa<CXXScalarValueInitExpr>(expression)) return value(Value(width(expression->getType()), 0), expression->getType());
        if (auto unary = dyn_cast<UnaryOperator>(expression)) {
            auto operand = expr(unary->getSubExpr());
            auto op = unary->getOpcode();
            if (op == UO_AddrOf || op == UO_Deref || op == UO_Plus) return operand;
            if (unary->isIncrementDecrementOp()) {
                auto old = value(read(operand), operand.type);
                auto next = binary(unary->isIncrementOp() ? "+" : "-", old, value(constant(1, width(operand.type)), operand.type), operand.type);
                write(operand, next); return unary->isPostfix() ? old : next;
            }
            if (op == UO_LNot) return value(graph.unary("not", graph.unary("any", read(operand))), expression->getType());
            if (op == UO_Not) return value(graph.unary("not", read(operand)), expression->getType());
            if (op == UO_Minus) return binary("-", value(Value(width(expression->getType()), 0), expression->getType()), operand, expression->getType());
            fail("unsupported unary C++ operation", expression);
        }
        if (auto operation = dyn_cast<BinaryOperator>(expression)) {
            auto left = expr(operation->getLHS());
            if (operation->isLogicalOp()) {
                auto condition = graph.unary("any", read(left));
                bool conjunction = operation->getOpcode() == BO_LAnd;
                if (auto known = number(graph.resolved(condition))) {
                    if (bool(*known) != conjunction) return value(constant(*known, 1), operation->getType());
                    return value(graph.unary("any", read(expr(operation->getRHS()))), operation->getType());
                }
                auto before = cells;
                llvm::SaveAndRestore path(effectPath);
                effectPath.emplace_back(condition, conjunction);
                auto right = graph.unary("any", read(expr(operation->getRHS())));
                auto after = cells;
                merge(condition, before, conjunction ? after : before, conjunction ? before : after);
                return value(graph.binary(conjunction ? "and" : "or", condition, right, 1), operation->getType());
            }
            auto right = expr(operation->getRHS());
            if (operation->getOpcode() == BO_Comma) return right;
            auto op = operation->getOpcodeStr().str();
            if (operation->isAssignmentOp()) {
                if (operation->isCompoundAssignmentOp()) right = binary(op.substr(0, op.size()-1), left, right, operation->getType());
                write(left, right); return left;
            }
            return binary(op, left, right, operation->getType());
        }
        if (auto conditional = dyn_cast<ConditionalOperator>(expression)) {
            auto condition = graph.unary("any", read(expr(conditional->getCond())));
            if (auto known = number(graph.resolved(condition))) return expr(*known ? conditional->getTrueExpr() : conditional->getFalseExpr());
            auto before = cells;
            llvm::SaveAndRestore path(effectPath);
            effectPath.emplace_back(condition, true);
            auto yes = read(cast(expr(conditional->getTrueExpr()), expression->getType()));
            auto yesCells = cells; cells = before;
            effectPath.back().second = false;
            auto no = read(cast(expr(conditional->getFalseExpr()), expression->getType()));
            auto noCells = cells;
            merge(condition, before, yesCells, noCells);
            return value(graph.mux(condition, yes, no), expression->getType());
        }
        if (auto subscript = dyn_cast<ArraySubscriptExpr>(expression)) return index(expr(subscript->getBase()), expr(subscript->getIdx()), expression->getType());
        if (auto call = dyn_cast<CallExpr>(expression)) return callExpr(call);
        fail(std::string("unsupported C++ expression: ") + expression->getStmtClassName(), expression);
    }
    Item callExpr(const CallExpr* call) {
        auto function = call->getDirectCallee();
        if (!function) fail("indirect C++ call", call);
        std::string name = function->getNameAsString();
        Item receiver;
        unsigned start = 0;
        if (auto member = dyn_cast<CXXMemberCallExpr>(call)) receiver = expr(member->getImplicitObjectArgument());
        if (auto operation = dyn_cast<CXXOperatorCallExpr>(call)) {
            if (isa<CXXMethodDecl>(function)) { receiver = expr(call->getArg(0)); start = 1; }
            auto op = operation->getOperator();
            if (op == OO_Call && receiver.closure) {
                // Clang resolves generic helper specializations. Bind their
                // arguments like ordinary calls, retaining reference aliases;
                // closure objects themselves are not hardware port storage.
                std::vector<Item> arguments;
                for (unsigned index = 1; index < call->getNumArgs(); ++index) arguments.push_back(expr(call->getArg(index)));
                return invokeClosure(receiver.closure, function, arguments);
            }
            if (op == OO_Call && port(receiver.type)) {
                if (bindings.count(receiver.key)) return boundPort(receiver);
                return value(read(receiver), payload(receiver.type));
            }
            if (op == OO_Subscript) return index(receiver, expr(call->getArg(start)), call->getType());
            if (op == OO_Equal) {
                write(receiver, expr(call->getArg(start)));
                if (templateName(receiver.type) == "cpphdl::memory_row") queueMemory(receiver);
                return receiver;
            }
            if (op == OO_Amp || op == OO_Pipe || op == OO_Caret || op == OO_Plus || op == OO_Minus || op == OO_Star || op == OO_Slash || op == OO_Percent || op == OO_LessLess || op == OO_GreaterGreater || op == OO_EqualEqual || op == OO_ExclaimEqual || op == OO_Less || op == OO_Greater || op == OO_LessEqual || op == OO_GreaterEqual) {
                auto left = start ? receiver : expr(call->getArg(0));
                return binary(getOperatorSpelling(op), left, expr(call->getArg(1)), call->getType());
            }
            if (op == OO_Exclaim || op == OO_Tilde) return value(graph.unary("not", op == OO_Exclaim ? graph.unary("any", read(receiver)) : read(receiver)), call->getType());
        }
        if (isa<CXXConversionDecl>(function)) return cast(receiver, call->getType());
        std::vector<Item> arguments;
        for (unsigned index = start; index < call->getNumArgs(); ++index) arguments.push_back(expr(call->getArg(index)));
        auto qualified = function->getQualifiedNameAsString();
        if (!receiver.type.isNull() && templateName(receiver.type) == "cpphdl::memory") {
            if (name == "pending" && arguments.size() == 1)
                return memoryRow(receiver, arguments[0], call->getType(), true);
            if (name == "apply" && arguments.empty()) {
                if (!committing) fail("C++ memory apply requires the strobe phase", call);
                if (!appliedMemories.insert(memoryId(receiver)).second) fail("repeated C++ memory apply unsupported", call);
                return {};
            }
            fail("unsupported C++ memory method: " + name, call);
        }
        if (qualified == "debug_tick" && function->isExternC() && !function->hasBody()) {
            const unsigned sizes[] = {8, 8, 32, 32, 32, 8, 8, 32, 32};
            const bool outputs[] = {true, false, true, true, true, false, true, false, false};
            bool signature = function->getNumParams() == 9 && !function->isVariadic() &&
                context.hasSameType(function->getReturnType(), context.IntTy) && width(context.IntTy) == 32;
            for (unsigned index = 0; signature && index < 9; ++index) {
                auto type = sizes[index] == 8 ? context.UnsignedCharTy : context.IntTy;
                signature = context.hasSameType(function->getParamDecl(index)->getType(),
                    outputs[index] ? context.getPointerType(type) : type);
            }
            if (!signature) fail("unsupported debug_tick C ABI", call);
            if (!working || structural || committing) fail("debug_tick requires a C++ work transaction", call);
            Value inputs;
            for (unsigned index = 0; index < 9; ++index) {
                if (outputs[index]) {
                    if (arguments[index].key.empty() || width(arguments[index].type) != sizes[index] ||
                        arguments[index].selectedWidth || arguments[index].bitSelection)
                        fail("debug_tick requires addressable scalar outputs", call);
                    auto offset = number(graph.resolved(arguments[index].offset));
                    if (!offset) fail("dynamic debug_tick output address unsupported", call);
                    for (unsigned previous = 0; previous < index; ++previous)
                        if (outputs[previous] && arguments[previous].key == arguments[index].key) {
                            auto previousOffset = *number(graph.resolved(arguments[previous].offset));
                            if (*offset < previousOffset + sizes[previous] && previousOffset < *offset + sizes[index])
                                fail("aliased debug_tick output addresses unsupported", call);
                        }
                }
                auto bits = resize(read(arguments[index]), sizes[index]);
                inputs.insert(inputs.end(), bits.begin(), bits.end());
            }
            // One effect owns the callback; word-sized projections expose its
            // outputs without splitting the call or widening every graph node.
            auto result = hostEffect("host_debug_tick", 32, inputs);
            for (unsigned index = 0; index < 9; ++index) if (outputs[index]) {
                auto bits = graph.add("host_debug_output", sizes[index], {result.front()}, {}, constant(index, 4));
                write(arguments[index], value(bits, sizes[index] == 8 ? context.UnsignedCharTy : context.IntTy));
            }
            return value(result, call->getType());
        }
        if (qualified == "jtag_tick" && function->isExternC() && !function->hasBody()) {
            bool signature = function->getNumParams() == 5 && !function->isVariadic() &&
                context.hasSameType(function->getReturnType(), context.IntTy) && width(context.IntTy) == 32;
            for (unsigned index = 0; signature && index < 5; ++index)
                signature = context.hasSameType(function->getParamDecl(index)->getType(),
                    index < 4 ? context.getPointerType(context.UnsignedCharTy) : context.UnsignedCharTy);
            if (!signature) fail("unsupported jtag_tick C ABI", call);
            if (!working || structural || committing) fail("jtag_tick requires a C++ work transaction", call);
            Value inputs;
            for (unsigned index = 0; index < 5; ++index) {
                if (index < 4) {
                    if (arguments[index].key.empty() || width(arguments[index].type) != 8 ||
                        arguments[index].selectedWidth || arguments[index].bitSelection)
                        fail("jtag_tick requires addressable byte outputs", call);
                    auto offset = number(graph.resolved(arguments[index].offset));
                    if (!offset) fail("dynamic jtag_tick output address unsupported", call);
                    for (unsigned previous = 0; previous < index; ++previous)
                        if (arguments[previous].key == arguments[index].key) {
                            auto previousOffset = *number(graph.resolved(arguments[previous].offset));
                            if (*offset < previousOffset + 8 && previousOffset < *offset + 8)
                                fail("aliased jtag_tick output addresses unsupported", call);
                        }
                }
                auto bits = resize(read(arguments[index]), 8);
                inputs.insert(inputs.end(), bits.begin(), bits.end());
            }
            auto result = hostEffect("host_jtag_tick", 64, inputs);
            for (unsigned index = 0; index < 4; ++index)
                write(arguments[index], value(slice(result, 32 + 8 * index, 8), context.UnsignedCharTy));
            return value(slice(result, 0, 32), call->getType());
        }
        if (qualified == "random" && function->isExternC() && !function->hasBody() &&
            function->param_empty() && !function->isVariadic() &&
            context.hasSameType(function->getReturnType(), context.LongTy)) {
            if (!working || structural || committing)
                fail("random() requires a C++ work transaction", call);
            // Host state is not a combinational producer. Keep each call's
            // execution predicate and ordering even when its value is dead.
            return value(hostEffect("host_random", width(call->getType())), call->getType());
        }
        if (name == "bits" && !receiver.type.isNull() && arguments.size() == 2 &&
            (templateName(payload(receiver.type)) == "cpphdl::logic" || packedArray(payload(receiver.type)) ||
             templateName(receiver.type) == "cpphdl::memory_row" ||
             integerWrapperWidth(payload(receiver.type)))) {
            // Packed arrays share logic's bit layout, independent of byte or
            // element storage. Lower the public slice without entering .data.
            auto count = receiver.selectedWidth ? receiver.selectedWidth : width(receiver.type);
            auto highBits = resize(read(arguments[0]), 64), lowBits = resize(read(arguments[1]), 64);
            auto highValue = number(graph.resolved(highBits)), lowValue = number(graph.resolved(lowBits));
            if (!highValue || !lowValue) {
                if (count > 64 && receiver.rangeMask.empty()) {
                    auto result = value(wideSliceRead(read(receiver), highBits, lowBits), call->getType());
                    // A value is sufficient for reads (including a mutable
                    // source's logic_bits proxy), but never discard writes.
                    result.readOnlyWideRange = true;
                    return result;
                }
                // A runtime slice is a masked word update, not an array of
                // aligned elements. Keep both endpoints so unaligned slices
                // preserve all neighboring bits without quadratic mux trees.
                if (count > 64 || !receiver.rangeMask.empty()) fail("wide or nested dynamic C++ bit range unsupported", call);
                auto mask = graph.binary("and",
                    graph.binary("shl", Value(count, 1), lowBits, count),
                    graph.binary("shr", Value(count, 1), graph.binary("sub", constant(count - 1, 64), highBits, 64), count), count);
                auto valid = graph.binary("and", graph.binary("lt", highBits, constant(count, 64), 1),
                    graph.unary("not", graph.binary("lt", highBits, lowBits, 1)), 1);
                mask = graph.mux(valid, mask, Value(count, 0));
                if (receiver.key.empty())
                    return value(graph.binary("shr", graph.binary("and", read(receiver), mask, count), lowBits, count), call->getType());
                initial(receiver);
                if (!receiver.extent) receiver.extent = width(receiver.type);
                receiver.selectedWidth = count;
                receiver.rangeMask = mask; receiver.rangeLow = lowBits;
                receiver.type = call->getType();
                return receiver;
            }
            auto high = *highValue, low = *lowValue;
            if (low > high || high >= count) fail("invalid C++ bit range", call);
            if (receiver.key.empty()) return value(slice(read(receiver), low, high - low + 1), call->getType());
            initial(receiver);
            if (!receiver.extent) receiver.extent = width(receiver.type);
            receiver.offset = graph.binary("add", receiver.offset, constant(low, 64), 64);
            receiver.selectedWidth = high - low + 1;
            receiver.type = call->getType();
            return receiver;
        }
        if (qualified == "cpphdl::byteswap") {
            // Like concatenation, byte reversal only rewires logical bits.
            // Do not interpret logic's private byte storage as packed fields.
            auto count = width(call->getType());
            auto source = read(cast(arguments.at(0), call->getType()));
            Value result(count, 0);
            auto bytes = (count + 7) / 8;
            for (unsigned bit = 0; bit < count; ++bit) {
                auto destination = (bytes - 1 - bit / 8) * 8 + bit % 8;
                if (destination < count) result[destination] = source[bit];
            }
            return value(result, call->getType());
        }
        if (qualified == "cpphdl::repeat") {
            auto part = read(arguments.at(0));
            auto count = valueWidth(call->getType());
            if (part.empty() || count % part.size()) fail("invalid C++ replication", call);
            Value result;
            while (result.size() < count) result.insert(result.end(), part.begin(), part.end());
            return value(result, call->getType());
        }
        if (qualified == "cpphdl::reduce_and" || qualified == "cpphdl::reduce_or" || qualified == "cpphdl::reduce_xor")
            return value(graph.unary(name == "reduce_and" ? "all" : name == "reduce_or" ? "any" : "parity", read(arguments.at(0))), call->getType());
        if (qualified == "cpphdl::sv_bits") {
            auto high = integer(arguments.at(1)), low = integer(arguments.at(2));
            auto count = high >= low ? std::min<uint64_t>(high - low + 1, width(call->getType())) : 0;
            return value(resize(slice(read(arguments.at(0)), low, count), width(call->getType())), call->getType());
        }
        // A bound value can be sliced without constructing a logic_bits proxy.
        // Keep this read-only, constant-index path independent of storage-range
        // handling: temporary values have no writeback destination.
        if (name == "bits" && !receiver.type.isNull() && receiver.key.empty() &&
            templateName(payload(receiver.type)) == "cpphdl::logic" && arguments.size() == 2) {
            auto high = number(graph.resolved(read(arguments[0])));
            auto low = number(graph.resolved(read(arguments[1])));
            if (high && low) {
                if (*low > *high || *high >= width(receiver.type)) fail("invalid C++ bit range", call);
                return value(slice(read(receiver), *low, *high - *low + 1), call->getType());
            }
        }
        if (qualified == "cpphdl::sv_insert_field") {
            auto parameters = function->getTemplateSpecializationArgs();
            auto offset = (*parameters)[0].getAsIntegral().getLimitedValue();
            auto target = arguments.at(0);
            if (!target.extent) target.extent = width(target.type);
            target.offset = graph.binary("add", target.offset, constant(offset, 64), 64);
            target.type = arguments.at(1).type;
            write(target, arguments.at(1)); return {};
        }
        if (name == "strobe" && !receiver.type.isNull() && templateName(receiver.type) == "cpphdl::reg") {
            if (!committing || !arguments.empty()) fail("unsupported register commit", call);
            initial(receiver);
            committed.insert(receiver.key);
            return {};
        }
        if (qualified == "cpphdl::sv_assign_field") { write(arguments.at(0), arguments.at(1)); return {}; }
        if (qualified == "cpphdl::sv_assign_bit") { write(index(arguments.at(0), arguments.at(1), context.BoolTy), arguments.at(2)); return {}; }
        if (qualified == "cpphdl::pack_value" || qualified == "cpphdl::unpack_value" || qualified == "cpphdl::convert_packed" || qualified == "cpphdl::sv_cast" || qualified == "cpphdl::sv_unsigned" || name == "__hdlcpp_array_cast") return cast(arguments.at(0), call->getType());
        if (name == "pack" && !receiver.type.isNull()) return cast(receiver, call->getType());
        if (name == "to_ullong" && !receiver.type.isNull() && templateName(receiver.type) == "cpphdl::memory_row")
            return cast(receiver, call->getType());
        if (name == "slice" && !receiver.type.isNull()) {
            auto params = function->getTemplateSpecializationArgs();
            if (!params || params->size() < 2) fail("dynamic C++ slice unsupported", call);
            auto low = (*params)[1].getAsIntegral().getLimitedValue();
            return value(slice(read(receiver), low, width(call->getType())), call->getType());
        }
        if ((name == "r_or" || name == "r_and" || name == "r_xor") && !receiver.type.isNull())
            return value(graph.unary(name == "r_or" ? "any" : name == "r_and" ? "all" : "parity", read(receiver)), call->getType());
        if (qualified == "std::move" || qualified == "std::forward") return arguments.at(0);
        return invoke(function, receiver, arguments);
    }
    Value hostEffect(const std::string& operation, unsigned count, Value inputs = {}) {
        auto enabled = executionGuard();
        auto result = graph.add(operation, count, previousEffect, inputs, enabled);
        previousEffect = {result.front()};
        ++effectCount;
        return result;
    }
    bool memoStatement(const Stmt* statement) {
        const Expr* expression = dyn_cast<Expr>(statement);
        if (auto branch = dyn_cast<IfStmt>(statement)) expression = branch->getCond();
        if (!expression) return false;
        auto operation = dyn_cast<BinaryOperator>(expression->IgnoreParenImpCasts());
        if (!operation) return false;
        auto member = dyn_cast<MemberExpr>(operation->getLHS()->IgnoreParenImpCasts());
        auto clock = dyn_cast<DeclRefExpr>(operation->getRHS()->IgnoreParenImpCasts());
        return member && clock && member->getMemberDecl()->getNameAsString().starts_with("__prev__system_clock_") && clock->getDecl()->getNameAsString() == "_system_clock";
    }
    bool dead(const Value& predicate) {
        return number(graph.resolved(predicate)) == std::optional<uint64_t>{0};
    }
    Value both(const Value& a, const Value& b) { return graph.binary("and", a, b, 1); }
    Value either(const Value& a, const Value& b) { return graph.binary("or", a, b, 1); }
    void collectExits(Flow& into, const Flow& from, const Value& enabled) {
        into.breaks = either(into.breaks, both(enabled, from.breaks));
        into.continues = either(into.continues, both(enabled, from.continues));
        auto returning = both(enabled, from.returns);
        if (from.result && !dead(returning)) {
            if (!into.result) into.result = from.result;
            else if (!from.result->type.isNull()) {
                if (from.result->closure || into.result->closure || containsModule(from.result->type))
                    fail("dynamic C++ object return unsupported");
                into.result = value(graph.mux(returning, read(*from.result), read(*into.result)),
                                    from.result->type);
            }
        }
        into.returns = either(into.returns, returning);
    }
    // Gate both SSA writes and transactional effects, never just the return
    // value. This also prevents dead suffixes from invoking unsupported calls.
    Flow guardedStatement(const Stmt* body, const Value& enabled) {
        if (dead(enabled)) return {};
        if (number(graph.resolved(enabled)) == std::optional<uint64_t>{1}) return statement(body);
        if (structural || committing) fail("dynamic structural/commit C++ condition", body);
        auto before = cells;
        llvm::SaveAndRestore path(effectPath);
        effectPath.emplace_back(enabled, true);
        auto flow = statement(body);
        auto after = cells;
        cells = before;
        merge(enabled, before, after, before);
        return flow;
    }
    void appendStatement(Flow& flow, const Stmt* body) {
        auto enabled = flow.normal;
        if (dead(enabled)) return;
        auto next = guardedStatement(body, enabled);
        collectExits(flow, next, enabled);
        flow.normal = both(enabled, next.normal);
    }
    Flow statement(const Stmt* body) {
        if (!body || isa<NullStmt>(body)) return {};
        if (auto attributed = dyn_cast<AttributedStmt>(body)) return statement(attributed->getSubStmt());
        if (isa<BreakStmt>(body)) {
            Flow flow; flow.normal = {0}; flow.breaks = {1}; return flow;
        }
        if (isa<ContinueStmt>(body)) {
            Flow flow; flow.normal = {0}; flow.continues = {1}; return flow;
        }
        // _LAZY_COMB's timestamp is a runtime cache, not retained hardware.
        // Only its exact clock-guard/update shape is removed here.
        if (memoStatement(body)) return {};
        if (auto compound = dyn_cast<CompoundStmt>(body)) {
            Flow flow;
            for (auto child : compound->body()) appendStatement(flow, child);
            return flow;
        }
        if (auto returned = dyn_cast<ReturnStmt>(body)) {
            auto result = expr(returned->getRetValue());
            if (!structural && !containsModule(result.type) && !result.closure && !result.type.isNull()) {
                auto frozen = value(read(result), result.type);
                frozen.memory = result.memory; frozen.memoryAddress = result.memoryAddress;
                result = frozen;
            }
            Flow flow; flow.normal = {0}; flow.returns = {1}; flow.result = result;
            return flow;
        }
        if (auto declarations = dyn_cast<DeclStmt>(body)) {
            for (auto declaration : declarations->decls()) if (auto variable = dyn_cast<VarDecl>(declaration)) {
                Item item; item.key = "$local" + std::to_string(++serial); item.type = variable->getType();
                if (variable->getType()->isReferenceType()) item = expr(variable->getInit());
                else if (variable->hasInit()) {
                    auto initializer = expr(variable->getInit());
                    if (initializer.closure) item = initializer;
                    else {
                        write(item, initializer);
                        item.memory = initializer.memory; item.memoryAddress = initializer.memoryAddress;
                    }
                }
                locals[variable] = item;
            }
            return {};
        }
        if (auto branch = dyn_cast<IfStmt>(body)) {
            statement(branch->getInit());
            statement(branch->getConditionVariableDeclStmt());
            auto condition = graph.unary("any", read(expr(branch->getCond())));
            if (auto known = number(graph.resolved(condition))) return statement(*known ? branch->getThen() : branch->getElse());
            if (structural || committing) fail("dynamic structural/commit C++ condition", body);
            auto before = cells;
            llvm::SaveAndRestore path(effectPath);
            effectPath.emplace_back(condition, true);
            auto yes = statement(branch->getThen()); auto yesCells = cells;
            cells = before;
            effectPath.back().second = false;
            auto no = statement(branch->getElse()); auto noCells = cells;
            merge(condition, before, yesCells, noCells);
            Flow flow;
            flow.normal = graph.mux(condition, yes.normal, no.normal);
            collectExits(flow, yes, condition);
            collectExits(flow, no, graph.unary("not", condition));
            return flow;
        }
        if (auto selection = dyn_cast<SwitchStmt>(body)) {
            statement(selection->getInit());
            statement(selection->getConditionVariableDeclStmt());
            // Evaluate the selector exactly once, even if a case changes its source.
            auto selected = expr(selection->getCond());
            auto selector = value(read(selected), selected.type);
            auto compound = dyn_cast<CompoundStmt>(selection->getBody());
            if (!compound) fail("unsupported C++ switch body", body);
            struct Part { const Stmt* body; Value match; bool label = false, fallback = false; };
            std::vector<Part> parts;
            Value matched{0};
            for (auto child : compound->body()) {
                while (auto label = dyn_cast<SwitchCase>(child)) {
                    Value condition{0}; bool fallback = isa<DefaultStmt>(label);
                    if (auto item = dyn_cast<CaseStmt>(label)) {
                        if (item->getRHS()) fail("C++ case ranges unsupported", item);
                        condition = read(binary("==", selector, expr(item->getLHS()), context.BoolTy));
                        matched = either(matched, condition);
                    }
                    parts.push_back({nullptr, condition, true, fallback});
                    child = label->getSubStmt();
                }
                parts.push_back({child, {}});
            }
            Flow flow; flow.normal = {0};
            for (const auto& part : parts) {
                if (part.label) {
                    auto match = part.fallback ? graph.unary("not", matched) : part.match;
                    flow.normal = either(flow.normal, match);
                } else appendStatement(flow, part.body);
            }
            // Consume only this switch's breaks. No-match and end-of-body paths
            // also continue; returns and enclosing-loop continues do not.
            flow.normal = graph.unary("not", either(flow.returns, flow.continues));
            flow.breaks = {0};
            return flow;
        }
        if (auto loop = dyn_cast<ForStmt>(body)) {
            statement(loop->getInit());
            // Separate the compile-time unrolling cursor from the observable
            // local. A runtime break must leave i at the breaking iteration,
            // not at the final statically unrolled bound.
            std::string inductionKey;
            Value inductionValue;
            if (loop->getInc()) {
                const auto* increment = loop->getInc()->IgnoreParenImpCasts();
                const Expr* destination = nullptr;
                if (auto unary = dyn_cast<UnaryOperator>(increment))
                    if (unary->isIncrementDecrementOp()) destination = unary->getSubExpr();
                if (auto binary = dyn_cast<BinaryOperator>(increment)) {
                    if (binary->isAssignmentOp()) destination = binary->getLHS();
                    if (binary->getRHS()->HasSideEffects(context))
                        fail("effectful C++ static loop increment unsupported", loop->getInc());
                }
                auto reference = destination ? dyn_cast<DeclRefExpr>(destination->IgnoreParenImpCasts()) : nullptr;
                if (!reference || !locals.count(reference->getDecl()) ||
                    !locals.at(reference->getDecl()).key.starts_with("$local"))
                    fail("C++ static loop requires a local induction increment", loop->getInc());
                inductionKey = locals.at(reference->getDecl()).key;
                inductionValue = cells.at(inductionKey);
            }
            Flow flow;
            Value finished{0};
            for (unsigned iteration = 0; ; ++iteration) {
                if (dead(flow.normal)) break;
                if (iteration >= 65536) fail("C++ static loop limit", body);
                if (loop->getCond() && !integer(expr(loop->getCond()))) break;
                auto enabled = flow.normal;
                auto next = guardedStatement(loop->getBody(), enabled);
                if (!inductionKey.empty())
                    inductionValue = graph.mux(enabled, cells.at(inductionKey), inductionValue);
                finished = either(finished, both(enabled, next.breaks));
                collectExits(flow, next, enabled);
                flow.normal = both(enabled, either(next.normal, next.continues));
                if (dead(flow.normal)) break;
                // The existing graph loop model statically unrolls induction.
                // Keep its constant increment separate from runtime exit masks.
                // Side-effecting/dynamic loop increments are not static loops.
                if (loop->getInc()) {
                    auto effects = effectCount;
                    expr(loop->getInc());
                    if (effectCount != effects) fail("effectful C++ static loop increment unsupported", loop->getInc());
                    inductionValue = graph.mux(flow.normal, cells.at(inductionKey), inductionValue);
                }
            }
            if (!inductionKey.empty()) cells.set(inductionKey, std::move(inductionValue));
            flow.normal = either(finished, flow.normal);
            flow.breaks = {0}; flow.continues = {0};
            return flow;
        }
        if (auto expression = dyn_cast<Expr>(body)) { expr(expression); return {}; }
        fail(std::string("unsupported C++ statement: ") + body->getStmtClassName(), body);
    }
    const CXXMethodDecl* method(QualType type, const std::string& name) {
        auto record = clean(type)->getAsCXXRecordDecl();
        for (auto candidate : record->methods()) if (candidate->getNameAsString() == name) return candidate;
        for (const auto& base : record->bases()) if (auto found = method(base.getType(), name)) return found;
        return nullptr;
    }
public:
    Lowering(ASTContext& context, Sema& sema) : context(context), sema(sema) {}
    void statistics() const {
        size_t directReads = 0, producerEdges = 0;
        for (const auto& [key, reads] : getterReads) {
            directReads += reads->fields.size();
            producerEdges += reads->producers.size();
        }
        llvm::errs() << "C++ graph: nodes=" << graph.nodes.size() << " cells=" << cells.size()
                     << " methods=" << results.size() << " registers=" << registers.size()
                     << " getter_reads=" << directReads << " getter_edges=" << producerEdges
                     << " ast_bytes=" << context.getASTAllocatedMemory() << '\n';
    }
    void run(const VarDecl* root, const std::string& output) {
        object.key = root->getNameAsString(); object.type = root->getType();
        // An extern template root need not have been instantiated by parsing.
        // Complete its type before inspecting bases, without constructing it.
        if (sema.RequireCompleteType(root->getLocation(), object.type, diag::err_incomplete_type))
            fail("incomplete C++ graph root type");
        if (!module(object.type)) fail("C++ graph root must derive from cpphdl::Module");
        auto rootObject = object;
        auto record = clean(root->getType())->getAsCXXRecordDecl();
        std::vector<std::pair<std::string, Item>> outputs;
        for (auto member : record->fields()) if (port(member->getType())) {
            auto item = field(object, member);
            auto name = member->getNameAsString();
            auto projection = name.find("__field_");
            auto directionEnd = name.size();
            if (projection != std::string::npos) {
                auto projectedBase = std::string_view(name).substr(0, projection);
                if (projectedBase.ends_with("_in") || projectedBase.ends_with("_out")) directionEnd = projection;
            }
            auto baseName = std::string_view(name).substr(0, directionEnd);
            bool outputPort = baseName.ends_with("_out");
            bool inputPort = baseName.ends_with("_in");
            if (!outputPort && !inputPort) fail("top C++ ports require _in/_out direction suffix");
            name.erase(directionEnd - (outputPort ? 4 : 3), outputPort ? 4 : 3);
            if (inputPort) {
                auto bits = graph.wire(width(item.type), name, "input");
                originals[item.key] = bits; cells.set(item.key, bits);
                graph.ports.push_back({name, bits, true});
            } else outputs.emplace_back(name, item);
        }
        structural = true;
        if (auto assign = method(rootObject.type, "_assign")) invoke(assign, rootObject, {});
        structural = false;
        for (auto& [name, item] : outputs) graph.ports.push_back({name, read(item), false});
        auto reset = graph.wire(1, "work_reset", "input");
        graph.ports.push_back({"work_reset", reset, true});
        working = true;
        if (auto work = method(rootObject.type, "_work")) invoke(work, rootObject, {value(reset, context.BoolTy)});
        working = false;
        committing = true;
        if (auto strobe = method(rootObject.type, "_strobe")) invoke(strobe, rootObject, {});
        committing = false;
        // The ordinary C++ lifecycle is explicit: eval(true) executes one
        // _work/_strobe transaction; step() additionally settles its outputs.
        // Do not invent SV clocks or asynchronous events removed by conversion.
        for (const auto& [key, bits] : registers) {
            if (!committed.count(key)) continue;
            auto next = cells.find(key + "._next");
            if (next == cells.end()) fail("register has no C++ next-state assignment: " + key);
            graph.states.push_back({bits, *next->second, {1}});
        }
        for (const auto& [key, bits] : blockingStates)
            graph.states.push_back({bits, cells.count(key) ? cells.at(key) : bits, {1}});
        for (const auto& write : graph.memoryWrites)
            if (!appliedMemories.count(write.memory)) fail("C++ memory writes without strobe apply: " + graph.memories[write.memory].name);
        graph.writeCpp(output);
    }
};

class Consumer : public ASTConsumer {
    CompilerInstance& compiler;
    std::string root, output;
    bool& success;
public:
    Consumer(CompilerInstance& compiler, std::string root, std::string output, bool& success)
        : compiler(compiler), root(std::move(root)), output(std::move(output)), success(success) {}
    void HandleTranslationUnit(ASTContext& context) override {
        if (compiler.getDiagnostics().hasErrorOccurred()) return;
        // Parsing has popped the TU scope. Late template instantiation can
        // synthesize builtins (such as memcpy for implicit copy assignment),
        // whose declarations still need a live semantic insertion scope.
        Scope translationScope(nullptr, Scope::DeclScope, compiler.getDiagnostics());
        translationScope.setEntity(context.getTranslationUnitDecl());
        llvm::SaveAndRestore<Scope*> scope(compiler.getSema().TUScope, &translationScope);
        try {
            const VarDecl* found = nullptr;
            for (auto declaration : context.getTranslationUnitDecl()->decls())
                if (auto variable = dyn_cast<VarDecl>(declaration); variable && variable->getNameAsString() == root) found = variable;
            if (!found) throw std::runtime_error("C++ root variable not found: " + root);
            Lowering lowering(context, compiler.getSema());
            try { lowering.run(found, output); }
            catch (...) { lowering.statistics(); throw; }
            lowering.statistics();
            success = !compiler.getDiagnostics().hasErrorOccurred();
        } catch (const std::exception& error) { llvm::errs() << "C++ graph: " << error.what() << '\n'; }
    }
};
class Action : public ASTFrontendAction {
    std::string root, output;
    bool& success;
public:
    Action(std::string root, std::string output, bool& success) : root(std::move(root)), output(std::move(output)), success(success) {}
    bool BeginSourceFileAction(CompilerInstance& compiler) override {
        compiler.getDiagnostics().setSeverity(diag::err_cannot_open_file, diag::Severity::Ignored, SourceLocation());
        return true;
    }
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& compiler, llvm::StringRef) override {
        return std::make_unique<Consumer>(compiler, root, output, success);
    }
};
class Factory : public tooling::FrontendActionFactory {
    std::string root, output;
    bool& success;
public:
    Factory(std::string root, std::string output, bool& success) : root(std::move(root)), output(std::move(output)), success(success) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<Action>(root, output, success); }
};
inline int run(int argc, const char** argv, std::vector<std::string> includes) {
    if (argc < 6 || std::string_view(argv[5]) != "--") {
        llvm::errs() << "usage: cpphdl --lower-cpp-graph source.cc graph.cc root_variable -- compiler flags\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(argv[2]) || std::filesystem::exists(argv[3])) {
        llvm::errs() << "C++ graph requires an existing source and a new output file\n";
        return 2;
    }
    for (int index = 6; index < argc; ++index) includes.emplace_back(argv[index]);
    tooling::FixedCompilationDatabase database(".", includes);
    tooling::ClangTool tool(database, {argv[2]});
    bool success = false;
    Factory factory(argv[4], argv[3], success);
    int status = tool.run(&factory);
    if (!success || status) { std::filesystem::remove(argv[3]); return 1; }
    return 0;
}
}

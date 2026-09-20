#pragma once

#include "include/cpphdl_graph.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Sema/Sema.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include <memory>

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
        bool bitSelection = false;
        std::shared_ptr<Closure> closure;
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
    std::map<QualType, unsigned> widths;
    std::vector<std::string> stack;
    unsigned serial = 0;
    std::vector<std::string> localNames;
    bool structural = false;
    bool committing = false;
    std::set<std::string> committed;
    std::set<const CXXRecordDecl*> validatedModules;
    struct NetWrites { std::string key; std::vector<unsigned> counts; };
    std::vector<NetWrites> netWrites;
    std::vector<std::string> producerStorage;
    unsigned dynamicDepth = 0;
    std::vector<unsigned> breakDepths;
    struct Break {};

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
        if (!record) return false;
        if (record->getQualifiedNameAsString() == "cpphdl::Module") return true;
        for (const auto& base : record->bases()) if (module(base.getType())) return true;
        return false;
    }
    bool port(QualType type) { return templateName(type) == "cpphdl::function_ref"; }
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
        return false;
    }
    std::optional<uint64_t> evaluated(const Expr* expression) {
        Expr::EvalResult result;
        if (!expression || expression->isValueDependent() || expression->isInstantiationDependent()) return {};
        if (expression->EvaluateAsInt(result, context) && !result.HasSideEffects && result.Val.isInt())
            return result.Val.getInt().getLimitedValue();
        return {};
    }
    void instantiate(const FunctionDecl* declaration) {
        if (!declaration->getBody())
            sema.InstantiateFunctionDefinition(declaration->getLocation(), const_cast<FunctionDecl*>(declaration), true, false, true);
        if (!declaration->getBody()) fail("missing instantiated C++ body: " + declaration->getQualifiedNameAsString());
    }
    void validateModule(QualType type) {
        auto record = clean(type)->getAsCXXRecordDecl();
        if (!validatedModules.insert(record).second) return;
        for (auto constructor : record->ctors()) if (constructor->isUserProvided() && constructor->isDefaultConstructor()) {
            instantiate(constructor);
            auto body = dyn_cast<CompoundStmt>(constructor->getBody());
            if (!body || !body->body_empty()) fail("nonempty module constructor unsupported", constructor->getBody());
            for (auto initializer : constructor->inits()) if (initializer->isWritten()) fail("explicit module initialization unsupported");
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
        auto name = templateName(type);
        auto spec = specialization(type);
        unsigned result = 0;
        if (name == "cpphdl::logic") result = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue();
        else if (name == "cpphdl::cat") {
            for (const auto& argument : spec->getTemplateArgs()[0].pack_elements()) result += argument.getAsIntegral().getLimitedValue();
        }
        else if (name == "cpphdl::reg" || name == "cpphdl::function_ref") result = width(payload(type));
        else if (name == "cpphdl::array") result = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue() * width(spec->getTemplateArgs()[1].getAsType());
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
    Item value(Value bits, QualType type = {}) { Item result; result.bits = std::move(bits); result.type = type; return result; }
    Item constantObject(const APValue& constantValue, QualType type) {
        if (constantValue.isInt()) return value(constant(constantValue.getInt().getLimitedValue(), width(type)), type);
        if (templateName(type) == "cpphdl::logic" && constantValue.isStruct()) {
            auto record = clean(type)->getAsCXXRecordDecl();
            unsigned index = 0;
            for (auto member : record->fields()) {
                if (member->getNameAsString() == "bytes") {
                    const auto& bytes = constantValue.getStructField(index);
                    Value result(width(type), 0);
                    for (unsigned bit = 0; bit < result.size(); ++bit) {
                        const auto& byte = bit / 8 < bytes.getArrayInitializedElts() ? bytes.getArrayInitializedElt(bit / 8) : bytes.getArrayFiller();
                        result[bit] = (byte.getInt().getLimitedValue() >> (bit % 8)) & 1;
                    }
                    return value(result, type);
                }
                ++index;
            }
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
        if (item.key.empty()) return item.bits;
        if (port(item.type)) {
            if (bindings.count(item.key)) return read(invokeClosure(bindings.at(item.key)));
        }
        auto source = cells.count(item.key) ? cells.at(item.key) : initial(item);
        unsigned count = width(item.type);
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
        else bits = resize(bits, width(type), !item.type.isNull() && clean(item.type)->isSignedIntegerOrEnumerationType());
        return value(bits, type);
    }
    void write(Item target, Item source) {
        if (source.closure) {
            if (!structural || target.key.empty() || !port(target.type)) fail("closure assignment outside structural port binding");
            bindings[target.key] = source.closure;
            return;
        }
        if (target.key.empty()) fail("assignment to non-addressable C++ value");
        if (!producerStorage.empty() && !target.key.starts_with("$") && target.key != producerStorage.back())
            fail("combinational getter mutates another field: " + target.key);
        if (templateName(target.type) == "cpphdl::reg" || registers.count(target.key))
            fail("direct current-state C++ mutation unsupported: " + target.key);
        auto incoming = target.bitSelection ? resize(read(source), 1) : read(cast(source, target.type));
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
            if (templateName(result.type) == "cpphdl::reg" && member->hasInClassInitializer()) fail("initialized C++ register unsupported");
            if (port(result.type) && !bindings.count(result.key) && member->hasInClassInitializer()) {
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
        std::optional<uint64_t> offset;
        for (auto child : record->decls()) if (auto variable = dyn_cast<VarDecl>(child))
            if (variable->getNameAsString() == "__hdlcpp_offset_" + name) {
                if (!variable->getInit()) sema.InstantiateVariableDefinition(variable->getLocation(), variable, true);
                offset = evaluated(variable->getInit());
            }
        if (!offset) fail("missing packed field metadata: " + name + " in " + base.type.getAsString());
        if (base.key.empty()) return value(slice(read(base), *offset, width(member->getType())), member->getType());
        if (!base.extent) base.extent = width(base.type);
        base.offset = graph.binary("add", base.offset, constant(*offset, 64), 64);
        base.type = member->getType();
        return base;
    }
    Item index(Item base, Item indexValue, QualType resultType) {
        auto sourceType = payload(base.type);
        if (templateName(base.type) == "cpphdl::reg") initial(base);
        auto name = templateName(sourceType);
        unsigned count = 0, elementWidth = 1;
        QualType elementType = context.BoolTy;
        if (name == "cpphdl::array") {
            auto spec = specialization(sourceType);
            count = spec->getTemplateArgs()[0].getAsIntegral().getLimitedValue();
            elementType = spec->getTemplateArgs()[1].getAsType();
        } else if (name == "std::array") {
            auto spec = specialization(sourceType);
            count = spec->getTemplateArgs()[1].getAsIntegral().getLimitedValue();
            elementType = spec->getTemplateArgs()[0].getAsType();
        } else if (auto array = context.getAsConstantArrayType(sourceType)) {
            count = array->getSize().getLimitedValue(); elementType = array->getElementType();
        } else if (name == "cpphdl::logic") { count = width(sourceType); base.bitSelection = true; }
        else fail("unsupported C++ indexing: " + sourceType.getAsString());
        if (module(elementType)) {
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
    Item invokeClosure(std::shared_ptr<Closure> closure) {
        auto savedObject = object; auto savedLocals = locals;
        object = closure->object; locals = closure->locals;
        auto result = statement(closure->expression->getBody());
        object = savedObject; locals = std::move(savedLocals);
        if (!result) fail("port closure has no return");
        return *result;
    }
    const FieldDecl* returnedField(const FunctionDecl* function) {
        const FieldDecl* result = nullptr;
        bool valid = true;
        std::function<void(const Stmt*)> visit = [&](const Stmt* body) {
            if (!body) return;
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
    Item invoke(const FunctionDecl* function, Item receiver, std::vector<Item> arguments) {
        if (module(receiver.type)) validateModule(receiver.type);
        instantiate(function);
        auto name = function->getNameAsString();
        std::string key = receiver.key + "::" + name;
        auto returnStorage = function->getReturnType()->isReferenceType() ? returnedField(function) : nullptr;
        bool cache = isa<CXXMethodDecl>(function) && module(receiver.type) && function->param_empty() && returnStorage;
        if (cache && results.count(key)) return value(results.at(key), function->getReturnType());
        if (stack.size() > 256) fail("C++ call nesting limit");
        auto savedObject = object; auto savedLocals = locals;
        auto localBegin = localNames.size();
        object = receiver; locals.clear();
        for (unsigned index = 0; index < function->getNumParams(); ++index) {
            if (index >= arguments.size()) fail("missing C++ call argument");
            auto parameter = function->getParamDecl(index);
            auto type = parameter->getType();
            if (type->isReferenceType() || type->isPointerType() || arguments[index].closure) locals[parameter] = arguments[index];
            else {
                Item local; local.key = "$local" + std::to_string(++serial); local.type = type;
                write(local, arguments[index]); locals[parameter] = local;
            }
        }
        Value output;
        if (cache) { output = graph.wire(width(function->getReturnType()), key); results[key] = output; }
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
        if (cache) producerStorage.push_back(receiver.key + "." + returnStorage->getNameAsString());
        auto result = statement(function->getBody());
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
        if (auto wrapper = dyn_cast<ExprWithCleanups>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<MaterializeTemporaryExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<CXXBindTemporaryExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<ParenExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto wrapper = dyn_cast<CXXDefaultArgExpr>(expression)) return expr(wrapper->getExpr());
        if (auto wrapper = dyn_cast<CXXDefaultInitExpr>(expression)) return expr(wrapper->getExpr());
        if (auto wrapper = dyn_cast<CXXStdInitializerListExpr>(expression)) return expr(wrapper->getSubExpr());
        if (auto lambda = dyn_cast<LambdaExpr>(expression)) {
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
            if (auto variable = dyn_cast<VarDecl>(reference->getDecl()); variable && variable->hasInit() && variable->isConstexpr()) return expr(variable->getInit());
            fail("unbound C++ declaration: " + reference->getDecl()->getNameAsString(), expression);
        }
        if (auto member = dyn_cast<MemberExpr>(expression)) return field(expr(member->getBase()), member->getMemberDecl());
        if (auto castExpr = dyn_cast<CastExpr>(expression)) {
            auto result = expr(castExpr->getSubExpr());
            if (result.closure || castExpr->getCastKind() == CK_NoOp || castExpr->getCastKind() == CK_LValueToRValue || castExpr->getCastKind() == CK_DerivedToBase || castExpr->getCastKind() == CK_UncheckedDerivedToBase) return result;
            return cast(result, castExpr->getType());
        }
        if (auto construct = dyn_cast<CXXConstructExpr>(expression)) {
            if (templateName(construct->getType()) == "cpphdl::cat") {
                Value bits;
                for (unsigned index = construct->getNumArgs(); index > 0; --index) {
                    auto part = read(expr(construct->getArg(index - 1)));
                    bits.insert(bits.end(), part.begin(), part.end());
                }
                return value(bits, construct->getType());
            }
            if (!construct->getNumArgs()) return value(Value(width(construct->getType()), 0), construct->getType());
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
            auto yes = read(cast(expr(conditional->getTrueExpr()), expression->getType()));
            auto yesCells = cells; cells = before;
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
            if (op == OO_Call && port(receiver.type)) {
                if (bindings.count(receiver.key)) return invokeClosure(bindings.at(receiver.key));
                return value(read(receiver), payload(receiver.type));
            }
            if (op == OO_Subscript) return index(receiver, expr(call->getArg(start)), call->getType());
            if (op == OO_Equal) { write(receiver, expr(call->getArg(start))); return receiver; }
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
        if (qualified == "cpphdl::repeat") {
            auto part = read(arguments.at(0));
            auto count = width(call->getType());
            if (part.empty() || count % part.size()) fail("invalid C++ replication", call);
            Value result;
            while (result.size() < count) result.insert(result.end(), part.begin(), part.end());
            return value(result, call->getType());
        }
        if (qualified == "cpphdl::reduce_and" || qualified == "cpphdl::reduce_or" || qualified == "cpphdl::reduce_xor")
            return value(graph.unary(name == "reduce_and" ? "all" : name == "reduce_or" ? "any" : "parity", read(arguments.at(0))), call->getType());
        if (qualified == "cpphdl::sv_bits" || qualified == "cpphdl::sv_bits_runtime") {
            auto high = integer(arguments.at(1)), low = integer(arguments.at(2));
            auto count = high >= low ? std::min<uint64_t>(high - low + 1, width(call->getType())) : 0;
            return value(resize(slice(read(arguments.at(0)), low, count), width(call->getType())), call->getType());
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
    std::optional<Item> statement(const Stmt* body) {
        if (!body || isa<NullStmt>(body)) return {};
        if (isa<BreakStmt>(body)) {
            if (breakDepths.empty() || dynamicDepth != breakDepths.back()) fail("conditional C++ break unsupported", body);
            throw Break{};
        }
        // _LAZY_COMB's timestamp is a runtime cache, not retained hardware.
        // Only its exact clock-guard/update shape is removed here.
        if (memoStatement(body)) return {};
        if (auto compound = dyn_cast<CompoundStmt>(body)) {
            for (auto child : compound->body()) if (auto returned = statement(child)) return returned;
            return {};
        }
        if (auto returned = dyn_cast<ReturnStmt>(body)) {
            auto result = expr(returned->getRetValue());
            if (structural || containsModule(result.type)) return result;
            if (!result.closure && !result.type.isNull()) return value(read(result), result.type);
            return result;
        }
        if (auto declarations = dyn_cast<DeclStmt>(body)) {
            for (auto declaration : declarations->decls()) if (auto variable = dyn_cast<VarDecl>(declaration)) {
                Item item; item.key = "$local" + std::to_string(++serial); item.type = variable->getType();
                if (variable->getType()->isReferenceType()) item = expr(variable->getInit());
                else if (variable->hasInit()) write(item, expr(variable->getInit()));
                locals[variable] = item;
            }
            return {};
        }
        if (auto branch = dyn_cast<IfStmt>(body)) {
            statement(branch->getInit());
            auto condition = graph.unary("any", read(expr(branch->getCond())));
            if (auto known = number(graph.resolved(condition))) return statement(*known ? branch->getThen() : branch->getElse());
            if (structural || committing) fail("dynamic structural/commit C++ condition", body);
            // Both arms start from the same SSA environment. Connecting writes
            // incrementally would create false self-dependencies and latches.
            auto before = cells;
            ++dynamicDepth;
            auto yesReturn = statement(branch->getThen()); auto yes = cells;
            cells = before;
            auto noReturn = statement(branch->getElse()); auto no = cells;
            --dynamicDepth;
            if (yesReturn || noReturn) {
                if (!yesReturn || !noReturn) fail("conditional early C++ return unsupported", body);
                merge(condition, before, yes, no);
                return value(graph.mux(condition, read(*yesReturn), read(*noReturn)), yesReturn->type);
            }
            merge(condition, before, yes, no);
            return {};
        }
        if (auto selection = dyn_cast<SwitchStmt>(body)) {
            statement(selection->getInit());
            auto selector = expr(selection->getCond());
            auto compound = dyn_cast<CompoundStmt>(selection->getBody());
            if (!compound) fail("unsupported C++ switch body", body);
            struct Arm { Value condition; const Stmt* body; bool fallback; };
            std::vector<Arm> arms;
            for (auto child : compound->body()) {
                Value condition{0}; bool fallback = false;
                while (auto label = dyn_cast<SwitchCase>(child)) {
                    if (auto item = dyn_cast<CaseStmt>(label)) {
                        if (item->getRHS()) fail("C++ case ranges unsupported", item);
                        auto equal = binary("==", selector, expr(item->getLHS()), context.BoolTy);
                        condition = graph.binary("or", condition, read(equal), 1);
                    } else fallback = true;
                    child = label->getSubStmt();
                }
                if (!isa<CompoundStmt>(child)) fail("C++ case requires a braced body", child);
                arms.push_back({condition, child, fallback});
            }
            auto before = cells, merged = cells;
            auto evaluateArm = [&](const Arm& arm) {
                cells = before;
                bool terminated = false;
                breakDepths.push_back(dynamicDepth);
                try {
                    if (statement(arm.body)) fail("return inside C++ switch unsupported", arm.body);
                } catch (Break&) { terminated = true; }
                breakDepths.pop_back();
                if (!terminated) fail("C++ switch fallthrough unsupported", arm.body);
                auto after = cells;
                std::set<std::string> keys;
                for (const auto& [key, bits] : merged) keys.insert(key);
                for (const auto& [key, bits] : after) keys.insert(key);
                for (const auto& key : keys) {
                    auto afterValue = after.find(key), mergedValue = merged.find(key);
                    if (afterValue != after.end() && mergedValue != merged.end() && afterValue->second == mergedValue->second) continue;
                    const auto& previous = before.count(key) ? before.at(key) : originals.at(key);
                    merged.set(key, graph.mux(arm.fallback ? Value{1} : arm.condition,
                        after.count(key) ? after.at(key) : previous,
                        merged.count(key) ? merged.at(key) : previous));
                }
            };
            for (const auto& arm : arms) if (arm.fallback) evaluateArm(arm);
            for (const auto& arm : arms) if (!arm.fallback) evaluateArm(arm);
            cells = std::move(merged);
            return {};
        }
        if (auto loop = dyn_cast<ForStmt>(body)) {
            statement(loop->getInit());
            for (unsigned iteration = 0; ; ++iteration) {
                if (iteration >= 65536) fail("C++ static loop limit", body);
                if (!integer(expr(loop->getCond()))) break;
                if (auto returned = statement(loop->getBody())) return returned;
                expr(loop->getInc());
            }
            return {};
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
        llvm::errs() << "C++ graph: nodes=" << graph.nodes.size() << " cells=" << cells.size()
                     << " methods=" << results.size() << " registers=" << registers.size() << '\n';
    }
    void run(const VarDecl* root, const std::string& output) {
        object.key = root->getNameAsString(); object.type = root->getType();
        if (!module(object.type)) fail("C++ graph root must derive from cpphdl::Module");
        auto rootObject = object;
        auto record = clean(root->getType())->getAsCXXRecordDecl();
        std::vector<std::pair<std::string, Item>> outputs;
        for (auto member : record->fields()) if (port(member->getType())) {
            auto item = field(object, member);
            auto name = member->getNameAsString();
            bool outputPort = name.ends_with("_out");
            bool inputPort = name.ends_with("_in");
            if (!outputPort && !inputPort) fail("top C++ ports require _in/_out direction suffix");
            name.resize(name.size() - (outputPort ? 4 : 3));
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
        if (auto work = method(rootObject.type, "_work")) invoke(work, rootObject, {value(reset, context.BoolTy)});
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

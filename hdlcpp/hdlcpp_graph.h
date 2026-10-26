#pragma once

#include <filesystem>
#include <iostream>
#include "../include/cpphdl_graph.h"
#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/types/AllTypes.h"

namespace hdlcpp_native {
using namespace slang;
using namespace slang::ast;
using namespace cpphdl::graph;

// Elaborated types and symbol identities replace textual helper inference.
// Processes become SSA values; only nonblocking state survives into execution.
class Lowering {
    Graph graph;
    const InstanceSymbol& top;
    const Scope* scope;
    std::map<const ValueSymbol*, Value> signals;
    using Environment = std::map<const ValueSymbol*, Value>;
    Environment current, pending;
    std::set<const ValueSymbol*> locals, written;
    std::vector<const Symbol*> hardware;
    Value returned;
    Value lvalueReference;
    Value active{1};
    bool inFunction = false;
    bool inTask = false;
    unsigned controlDepth = 0;
    unsigned callDepth = 0;
    std::map<const ValueSymbol*, size_t> ram;
    std::map<size_t, const Symbol*> ramWriters;
    const Symbol* process = nullptr;
    bool sequentialProcess = false;
    Value processEnable{1};
    Value memoryActive{1};
    std::set<std::string> externalModules;
    struct ExternalPort { unsigned instance;std::string module,path,port,pin;bool input;unsigned bits; };
    std::vector<ExternalPort> externalPorts;
    unsigned externalCount=0;
    std::map<const ValueSymbol*,std::set<const SubroutineSymbol*>> references;
    std::set<size_t> undefinedFunctionStorage;
    std::set<const VariableSymbol*> taskInductionVariables;

    const ValueSymbol* memoryBase(const Expression& expression) const {
        if (expression.kind != ExpressionKind::NamedValue && expression.kind != ExpressionKind::HierarchicalValue)
            return nullptr;
        auto& symbol = expression.as<ValueExpressionBase>().symbol;
        return ram.count(&symbol) ? &symbol : nullptr;
    }
    Value memoryAddress(const ValueSymbol& symbol, Value index, bool signedIndex=false) {
        const auto range = symbol.getType().getFixedRange();
        // Runtime rows use increasing SV subscripts, independently of packed
        // bitstream order. Keep full selector width before the bounds check.
        index = resize(std::move(index),64,signedIndex);
        return graph.binary("sub",index,constant(uint64_t(range.lower()),64),64);
    }
    Value memoryRead(const ValueSymbol& symbol, Value index, bool signedIndex=false) {
        auto identity=ram.at(&symbol);
        auto address=memoryAddress(symbol,std::move(index),signedIndex);
        graph.memoryAccesses.push_back({identity,address,both(active,both(memoryActive,processEnable)),sequentialProcess});
        Value result;
        for(unsigned offset=0;offset<graph.memories[identity].width;offset+=64) {
            auto part=graph.add("memory_read",std::min(64u,graph.memories[identity].width-offset),address,
                                constant(identity,64),constant(offset/64,64));
            result.insert(result.end(),part.begin(),part.end());
        }
        return result;
    }
    void discoverMemories() {
        auto visitor=makeVisitor([&](auto& self,const AssignmentExpression& assignment) {
            if(assignment.isNonBlocking()) {
                const auto* base=&assignment.left();
                while(base->kind==ExpressionKind::ElementSelect || base->kind==ExpressionKind::RangeSelect) {
                    if(base->kind==ExpressionKind::ElementSelect)base=&base->as<ElementSelectExpression>().value();
                    else base=&base->as<RangeSelectExpression>().value();
                }
                if(base->kind==ExpressionKind::NamedValue || base->kind==ExpressionKind::HierarchicalValue) {
                    const auto& symbol=base->as<ValueExpressionBase>().symbol;
                    const auto& type=symbol.getType().getCanonicalType();
                    if(type.kind==SymbolKind::FixedSizeUnpackedArrayType && !ram.count(&symbol)) {
                        const auto& array=type.as<FixedSizeUnpackedArrayType>();
                        if(!array.elementType.isIntegral())fail("RAM rows must be packed integral values",base->sourceRange);
                        ram[&symbol]=graph.memories.size();
                        graph.memories.push_back({symbol.getHierarchicalPath(),width(array.elementType),uint64_t(array.range.width())});
                    }
                }
            }
            self.visitDefault(assignment);
        });
        for(auto symbol:hardware)if(symbol->kind==SymbolKind::ProceduralBlock)
            symbol->as<ProceduralBlockSymbol>().getBody().visit(visitor);
    }

    [[noreturn]] void fail(const std::string& message, SourceRange range = {}) const {
        auto& manager = *scope->getCompilation().getSourceManager();
        auto location = range.start();
        throw std::runtime_error(message + (location ? " at " + std::string(manager.getFileName(location)) + ":" + std::to_string(manager.getLineNumber(location)) : ""));
    }
    static unsigned width(const Type& type) {
        auto count = type.getBitstreamWidth();
        if (!count || count > 1048576) throw std::runtime_error("non-fixed hardware type");
        return unsigned(count);
    }
    Value bits(const ConstantValue& value) {
        Value result;
        if (value.isInteger()) {
            const auto& integer = value.integer();
            for (unsigned offset = 0; offset < integer.getBitWidth(); ++offset) {
                auto bit = integer[int32_t(offset)];
                result.push_back(bit.isUnknown() ? 0 : bit.value);
            }
        } else if (value.isUnpacked()) {
            auto elements = value.elements();
            for (size_t index = elements.size(); index > 0; --index) {
                auto part = bits(elements[index - 1]);
                result.insert(result.end(), part.begin(), part.end());
            }
        } else fail("unsupported constant value");
        return result;
    }
    Value signal(const ValueSymbol& symbol) {
        if (symbol.kind == SymbolKind::ModportPort) return signal(modportTarget(symbol));
        auto found = signals.find(&symbol);
        if (found != signals.end()) return found->second;
        auto result = graph.wire(width(symbol.getType()), symbol.getHierarchicalPath());
        signals[&symbol] = result;
        return result;
    }
    Value read(const ValueSymbol& symbol) {
        if (symbol.kind == SymbolKind::ModportPort) return read(modportTarget(symbol));
        if(ram.count(&symbol)) {
            const auto range=symbol.getType().getFixedRange();
            if(uint64_t(range.width())*graph.memories[ram.at(&symbol)].width>1048576)
                fail("whole RAM value is too large; index its rows");
            Value result;
            for(int64_t offset=0;offset<range.width();++offset) {
                auto index=range.left>=range.right?range.lower()+offset:range.upper()-offset;
                auto row=memoryRead(symbol,constant(uint64_t(index),64));
                result.insert(result.end(),row.begin(),row.end());
            }
            return result;
        }
        auto found = current.find(&symbol);
        return found == current.end() ? signal(symbol) : found->second;
    }
    const ValueSymbol& modportTarget(const ValueSymbol& symbol) {
        const auto& port = symbol.as<ModportPortSymbol>();
        if (port.explicitConnection || !port.internalSymbol || !port.internalSymbol->isValue())
            fail("explicit modport expression unsupported");
        return port.internalSymbol->as<ValueSymbol>();
    }
    int64_t integer(const Expression& expression) {
        auto value = expr(expression);
        auto known = number(value);
        if (!known) fail("expected static integer", expression.sourceRange);
        return expression.type->isSigned() ? Graph::signedNumber(*known, value.size()) : int64_t(*known);
    }
    Value boolean(Value value) { return graph.unary("any", std::move(value)); }
    Value invert(Value value) { return graph.unary("not", std::move(value)); }
    Value both(Value left, Value right) { return graph.binary("and", boolean(left), boolean(right), 1); }
    Value either(Value left, Value right) { return graph.binary("or", boolean(left), boolean(right), 1); }

    std::pair<int64_t, unsigned> selection(const Expression& expression) {
        if (expression.kind == ExpressionKind::MemberAccess) {
            const auto& member = expression.as<MemberAccessExpression>();
            if (member.member.kind != SymbolKind::Field || member.value().type->isUnpackedStruct())
                fail("unsupported member selection", expression.sourceRange);
            return {member.member.as<FieldSymbol>().bitOffset, width(*expression.type)};
        }
        if (expression.kind == ExpressionKind::ElementSelect) {
            const auto& element = expression.as<ElementSelectExpression>();
            auto range = element.value().type->getFixedRange();
            auto index = integer(element.selector());
            auto count = width(*expression.type);
            return {(range.left >= range.right ? index - range.right : range.right - index) * count, count};
        }
        const auto& rangeExpr = expression.as<RangeSelectExpression>();
        auto range = rangeExpr.value().type->getFixedRange();
        auto left = integer(rangeExpr.left()), right = integer(rangeExpr.right());
        auto element = rangeExpr.value().type->getArrayElementType();
        unsigned stride = element ? width(*element) : 1;
        int64_t lowest;
        if (rangeExpr.getSelectionKind() == RangeSelectionKind::Simple)
            lowest = range.left >= range.right ? std::min(left,right) - range.right : range.right - std::max(left,right);
        else {
            auto lowIndex = rangeExpr.getSelectionKind() == RangeSelectionKind::IndexedUp ? left : left - right + 1;
            lowest = range.left >= range.right ? lowIndex - range.right : range.right - (lowIndex + right - 1);
        }
        return {lowest * stride, width(*expression.type)};
    }
    Value binary(BinaryOperator op, Value left, Value right, unsigned resultWidth, bool sign) {
        using B = BinaryOperator;
        if (op == B::LogicalAnd) return both(left,right);
        if (op == B::LogicalOr) return either(left,right);
        if (op == B::Equality || op == B::CaseEquality || op == B::Inequality || op == B::CaseInequality) {
            auto result = invert(graph.unary("any", graph.binary("xor", left, right, left.size())));
            return op == B::Inequality || op == B::CaseInequality ? invert(result) : result;
        }
        if (op == B::BinaryXnor) return invert(graph.binary("xor",left,right,resultWidth));
        if (op == B::GreaterThan || op == B::GreaterThanEqual) std::swap(left,right);
        if (op == B::LessThan || op == B::GreaterThan || op == B::LessThanEqual || op == B::GreaterThanEqual) {
            if (op == B::LessThanEqual || op == B::GreaterThanEqual)
                return invert(graph.binary(sign ? "slt" : "lt", right,left,1));
            return graph.binary(sign ? "slt" : "lt",left,right,1);
        }
        static const std::map<B,std::string> operations{
            {B::Add,"add"},{B::Subtract,"sub"},{B::Multiply,"mul"},{B::Divide,"div"},{B::Mod,"mod"},
            {B::BinaryAnd,"and"},{B::BinaryOr,"or"},{B::BinaryXor,"xor"},
            {B::LogicalShiftLeft,"shl"},{B::ArithmeticShiftLeft,"shl"},
            {B::LogicalShiftRight,"shr"},{B::ArithmeticShiftRight,"sar"}};
        if (!operations.count(op)) fail("unsupported binary operator " + std::string(toString(op)));
        auto operation = operations.at(op);
        if (operation == "sar" && !sign) operation = "shr";
        if (sign && (operation == "div" || operation == "mod")) fail("signed division not supported");
        return graph.binary(operation,left,right,resultWidth);
    }
    Value expr(const Expression& expression) {
        if (expression.kind != ExpressionKind::Assignment) {
            EvalContext context(ASTContext(*scope, LookupLocation::max));
            auto value = expression.eval(context);
            if (value && (value.isInteger() || value.isUnpacked())) return bits(value);
        }
        using E = ExpressionKind;
        switch (expression.kind) {
        case E::NamedValue: case E::HierarchicalValue:
            return read(expression.as<ValueExpressionBase>().symbol);
        case E::LValueReference:
            if (lvalueReference.empty()) fail("unbound compound-assignment reference");
            return lvalueReference;
        case E::Conversion: {
            const auto& conversion = expression.as<ConversionExpression>();
            return resize(expr(conversion.operand()),width(*expression.type),conversion.operand().type->isSigned());
        }
        case E::MemberAccess: {
            auto [offset,count] = selection(expression);
            return slice(expr(expression.as<MemberAccessExpression>().value()),offset,count);
        }
        case E::RangeSelect: {
            auto [offset,count] = selection(expression);
            return slice(expr(expression.as<RangeSelectExpression>().value()),offset,count);
        }
        case E::ElementSelect: {
            const auto& element = expression.as<ElementSelectExpression>();
            auto index = expr(element.selector());
            if(auto symbol=memoryBase(element.value()))return memoryRead(*symbol,std::move(index),element.selector().type->isSigned());
            auto value = expr(element.value());
            if (number(index)) { auto [offset,count] = selection(expression); return slice(value,offset,count); }
            auto range = element.value().type->getFixedRange();
            auto count = width(*expression.type);
            Value result(count,0);
            for (int64_t item = range.lower(); item <= range.upper(); ++item) {
                auto offset = (range.left >= range.right ? item - range.right : range.right - item) * count;
                result = graph.mux(binary(BinaryOperator::Equality,index,constant(item,index.size()),1,false),slice(value,offset,count),result);
            }
            return result;
        }
        case E::Concatenation: {
            auto operands = expression.as<ConcatenationExpression>().operands();
            Value result;
            for (size_t index = operands.size(); index > 0; --index) {
                auto part = expr(*operands[index-1]); result.insert(result.end(),part.begin(),part.end());
            }
            return result;
        }
        case E::Replication: {
            const auto& replication = expression.as<ReplicationExpression>();
            auto count = integer(replication.count());
            auto part = expr(replication.concat());
            if (count < 0 || uint64_t(count)*part.size() > 1048576) fail("replication too large");
            Value result;
            for (int64_t index = 0; index < count; ++index) result.insert(result.end(),part.begin(),part.end());
            return result;
        }
        case E::BinaryOp: {
            const auto& operation = expression.as<BinaryExpression>();
            bool sign = operation.left().type->isSigned() &&
                        (OpInfo::isShift(operation.op) || operation.right().type->isSigned());
            return binary(operation.op,expr(operation.left()),expr(operation.right()),width(*expression.type),sign);
        }
        case E::UnaryOp: {
            const auto& operation = expression.as<UnaryExpression>();
            auto value = expr(operation.operand());
            using U = UnaryOperator;
            switch(operation.op) {
            case U::Plus: return value;
            case U::Minus: return graph.binary("sub",Value(value.size(),0),value,value.size());
            case U::BitwiseNot: return invert(value);
            case U::LogicalNot: return invert(boolean(value));
            case U::BitwiseAnd: return graph.unary("all",value);
            case U::BitwiseOr: return boolean(value);
            case U::BitwiseXor: return graph.unary("parity",value);
            case U::BitwiseNand: return invert(graph.unary("all",value));
            case U::BitwiseNor: return invert(boolean(value));
            case U::BitwiseXnor: return invert(graph.unary("parity",value));
            default: {
                bool increment = operation.op == U::Preincrement || operation.op == U::Postincrement;
                auto updated = graph.binary(increment ? "add" : "sub",value,constant(1,value.size()),value.size());
                assign(operation.operand(),updated,false);
                return operation.op == U::Preincrement || operation.op == U::Predecrement ? updated : value;
            }
            }
        }
        case E::ConditionalOp: {
            const auto& condition = expression.as<ConditionalExpression>();
            Value test{1};
            for (auto& item : condition.conditions) {
                if (item.pattern) fail("conditional patterns unsupported");
                test = both(test,expr(*item.expr));
            }
            if (auto known = number(test)) return expr(*known ? condition.left() : condition.right());
            return graph.mux(test,expr(condition.left()),expr(condition.right()));
        }
        case E::Assignment: {
            const auto& assignment = expression.as<AssignmentExpression>();
            if (assignment.timingControl) fail("timed assignment unsupported",expression.sourceRange);
            auto savedReference = lvalueReference;
            if (assignment.op) lvalueReference = expr(assignment.left());
            auto value = expr(assignment.right());
            lvalueReference = std::move(savedReference);
            assign(assignment.left(),value,assignment.isNonBlocking());
            return value;
        }
        case E::SimpleAssignmentPattern: case E::StructuredAssignmentPattern: case E::ReplicatedAssignmentPattern: {
            auto elements = static_cast<const AssignmentPatternExpressionBase&>(expression).elements();
            Value result;
            for (size_t index = elements.size(); index > 0; --index) {
                auto part = expr(*elements[index-1]); result.insert(result.end(),part.begin(),part.end());
            }
            return result;
        }
        case E::Call: {
            const auto& call = expression.as<CallExpression>();
            if (call.isSystemCall()) fail("nonconstant system call " + std::string(call.getSubroutineName()),expression.sourceRange);
            const auto& function = *std::get<0>(call.subroutine);
            if(!function.returnValVar) {
                if(call.hasOutputArgs())fail("task output/ref arguments are not supported",expression.sourceRange);
                if(++callDepth>64)fail("task expansion limit exceeded",expression.sourceRange);
                auto outerLocals=locals;
                auto outerInduction=std::move(taskInductionVariables);taskInductionVariables.clear();
                auto loops=makeVisitor([&](auto& self,const ForLoopStatement& loop){
                    for(auto variable:loop.loopVars)taskInductionVariables.insert(variable);
                    self.visitDefault(loop);
                });function.getBody().visit(loops);
                std::vector<Value> arguments;
                for(auto argument:call.arguments())arguments.push_back(expr(*argument));
                for(size_t i=0;i<arguments.size();++i) {
                    auto formal=function.getArguments()[i];locals.insert(formal);current[formal]=arguments[i];
                }
                bool outerTask=inTask;inTask=true;
                statement(function.getBody());
                inTask=outerTask;
                for(auto local:locals)if(!outerLocals.count(local)) {
                    current.erase(local);pending.erase(local);written.erase(local);
                }
                locals=std::move(outerLocals);taskInductionVariables=std::move(outerInduction);--callDepth;return {};
            }
            if (call.hasOutputArgs())
                fail("only value functions with input arguments are supported");
            if(function.defaultLifetime!=VariableLifetime::Automatic) {
                bool persistentInitializers=false;
                std::set<const VariableSymbol*> inductionVariables;
                auto loops=makeVisitor([&](auto& self,const ForLoopStatement& loop){
                    for(auto variable:loop.loopVars)inductionVariables.insert(variable);
                    self.visitDefault(loop);
                });
                function.getBody().visit(loops);
                auto visitor=makeVisitor([&](auto&,const VariableDeclStatement& d){
                    persistentInitializers|=d.symbol.getInitializer()!=nullptr &&
                        d.symbol.lifetime==VariableLifetime::Static && !inductionVariables.count(&d.symbol);
                });
                function.getBody().visit(visitor);
                if(persistentInitializers)fail("static function initializers require an automatic function: "+function.getHierarchicalPath(),expression.sourceRange);
            }
            if (++callDepth > 64) fail("function expansion limit exceeded");
            std::vector<Value> args;
            for (auto argument : call.arguments()) args.push_back(expr(*argument));
            auto savedCurrent = current, savedPending = pending;
            auto savedLocals = locals, savedWritten = written;
            auto savedActive = active, savedReturn = returned, savedMemoryActive = memoryActive;
            bool savedFunction = inFunction;
            auto savedControlDepth = controlDepth;
            auto outerUndefined=std::move(undefinedFunctionStorage);undefinedFunctionStorage.clear();
            controlDepth = 0;
            current.clear(); pending.clear(); locals.clear(); written.clear(); active={1};
            returned=Value(width(function.returnValVar->getType()),0); inFunction=true;
            memoryActive=both(savedMemoryActive,savedActive);
            for (size_t index = 0; index < args.size(); ++index) {
                current[function.getArguments()[index]] = args[index]; locals.insert(function.getArguments()[index]);
            }
            locals.insert(function.returnValVar);
            current[function.returnValVar] = graph.wire(width(function.returnValVar->getType()),function.getHierarchicalPath()+" return");
            for(auto bit:current[function.returnValVar])undefinedFunctionStorage.insert(Graph::owner(bit));
            statement(function.getBody());
            for (auto symbol : written) if (!locals.count(symbol)) {
                if(references[symbol]!=std::set<const SubroutineSymbol*>{&function})
                    fail("function writes observable module storage",expression.sourceRange);
                for(auto bit:signal(*symbol))if(bit>1)undefinedFunctionStorage.insert(Graph::owner(bit));
            }
            // Explicit returns capture their value when reached. Only paths still
            // active at function end use an implicit assignment to its name.
            auto result = graph.mux(active,current.at(function.returnValVar),returned);
            std::set<size_t> visited;
            std::function<void(Bit)> checkDefined=[&](Bit raw) {
                auto bit=graph.resolve(raw);if(bit<2)return;auto owner=Graph::owner(bit);
                if(!visited.insert(owner).second)return;
                if(undefinedFunctionStorage.count(owner))fail("function reads storage before defining its returned value",expression.sourceRange);
                const auto& n=graph.nodes[owner];for(auto* operands:{&n.left,&n.right,&n.select})for(auto input:*operands)checkDefined(input);
            };
            for(auto bit:result)checkDefined(bit);
            undefinedFunctionStorage=std::move(outerUndefined);
            current=std::move(savedCurrent); pending=std::move(savedPending); locals=std::move(savedLocals); written=std::move(savedWritten);
            active=savedActive; returned=savedReturn; memoryActive=savedMemoryActive; inFunction=savedFunction;
            controlDepth=savedControlDepth;
            --callDepth;
            return result;
        }
        default: fail("unsupported expression " + std::string(toString(expression.kind)),expression.sourceRange);
        }
    }
    void assign(const Expression& target, Value value, bool nonblocking, Value guard = {1}) {
        using E = ExpressionKind;
        const Expression* part=&target;
        while(part->kind==E::ElementSelect || part->kind==E::RangeSelect || part->kind==E::MemberAccess) {
            if(part->kind==E::ElementSelect) {
                const auto& element=part->as<ElementSelectExpression>();
                if(part!=&target && memoryBase(element.value()))
                    fail("partial RAM row writes require explicit whole-row merging",target.sourceRange);
                part=&element.value();
            }else if(part->kind==E::RangeSelect)part=&part->as<RangeSelectExpression>().value();
            else part=&part->as<MemberAccessExpression>().value();
        }
        if(target.kind==E::ElementSelect) {
            const auto& element=target.as<ElementSelectExpression>();
            if(auto symbol=memoryBase(element.value())) {
                if(!nonblocking || !sequentialProcess)fail("RAM writes require a clocked nonblocking assignment",target.sourceRange);
                auto identity=ram.at(symbol);
                auto [writer,fresh]=ramWriters.emplace(identity,process);
                if(!fresh && writer->second!=process)fail("RAM has multiple writing processes",target.sourceRange);
                graph.memoryWrites.push_back({identity,memoryAddress(*symbol,expr(element.selector()),element.selector().type->isSigned()),
                    resize(std::move(value),graph.memories[identity].width),both(processEnable,both(memoryActive,both(active,guard)))});
                return;
            }
        }
        if (target.kind == E::NamedValue || target.kind == E::HierarchicalValue) {
            const auto& rawSymbol = target.as<ValueExpressionBase>().symbol;
            const auto& symbol = rawSymbol.kind == SymbolKind::ModportPort ? modportTarget(rawSymbol) : rawSymbol;
            if(nonblocking&&inTask&&locals.count(&symbol))fail("task-local nonblocking storage is not supported",target.sourceRange);
            auto& environment = nonblocking ? pending : current;
            auto old = environment.count(&symbol) ? environment.at(&symbol) : read(symbol);
            // Function environments are private SSA temporaries. Updating them on
            // a returned path is harmless: that path's result was captured.
            // Guarding them by the live mask would introduce false reads of
            // uninitialized temporaries after an earlier conditional return.
            environment[&symbol] = graph.mux(both(inFunction?Value{1}:active,guard),resize(value,old.size()),old);
            written.insert(&symbol);
            return;
        }
        if (target.kind == E::Conversion) { assign(target.as<ConversionExpression>().operand(),value,nonblocking,guard); return; }
        if (target.kind == E::Concatenation) {
            auto operands = target.as<ConcatenationExpression>().operands();
            unsigned offset = 0;
            for (size_t index = operands.size(); index > 0; --index) {
                auto count = width(*operands[index-1]->type);
                assign(*operands[index-1],slice(value,offset,count),nonblocking,guard); offset += count;
            }
            return;
        }
        const Expression* base = nullptr;
        if (target.kind == E::MemberAccess) base = &target.as<MemberAccessExpression>().value();
        if (target.kind == E::RangeSelect) base = &target.as<RangeSelectExpression>().value();
        if (target.kind == E::ElementSelect) base = &target.as<ElementSelectExpression>().value();
        if (!base) fail("unsupported assignment target",target.sourceRange);
        // For NBA partial writes, preserve earlier scheduled updates but reads
        // of the RHS still use current (pre-edge) state.
        auto saved = current;
        if (nonblocking) for (auto& [symbol, update] : pending) current[symbol] = update;
        auto original = expr(*base);
        current = std::move(saved);
        auto replace = [&](int64_t offset, unsigned count, Value condition) {
            auto updated = original;
            for (unsigned index = 0; index < count; ++index)
                if (offset + index >= 0 && uint64_t(offset + index) < updated.size()) updated[offset+index] = index < value.size() ? value[index] : 0;
            assign(*base,updated,nonblocking,both(guard,condition));
        };
        if (target.kind == E::ElementSelect) {
            const auto& element = target.as<ElementSelectExpression>();
            auto selector = expr(element.selector());
            if (!number(selector)) {
                auto range = base->type->getFixedRange(); auto count = width(*target.type);
                for (int64_t item = range.lower(); item <= range.upper(); ++item) {
                    auto offset = (range.left >= range.right ? item-range.right : range.right-item)*count;
                    // Each guarded write must build on the previous one.
                    auto snapshot = current;
                    if (nonblocking) for (auto& [symbol, update] : pending) current[symbol] = update;
                    original = expr(*base); current = std::move(snapshot);
                    replace(offset,count,binary(BinaryOperator::Equality,selector,constant(item,selector.size()),1,false));
                }
                return;
            }
        }
        auto [offset,count] = selection(target); replace(offset,count,{1});
    }
    void declaration(const VariableSymbol& symbol) {
        locals.insert(&symbol);
        if(inTask&&!inFunction&&symbol.getInitializer()&&symbol.lifetime==VariableLifetime::Static&&!taskInductionVariables.count(&symbol))
            fail("static task initializer is not supported: "+symbol.getHierarchicalPath());
        if((inFunction||inTask)&&!symbol.getInitializer()) {
            auto value=graph.wire(width(symbol.getType()),symbol.getHierarchicalPath());
            if(inFunction)for(auto bit:value)undefinedFunctionStorage.insert(Graph::owner(bit));
            current[&symbol]=std::move(value);
        }else current[&symbol] = symbol.getInitializer() ? expr(*symbol.getInitializer()) : Value(width(symbol.getType()),0);
    }
    void branch(Value condition, const Statement& yes, const Statement* no) {
        if (auto known = number(condition)) { if (*known) statement(yes); else if (no) statement(*no); return; }
        // Merge complete branch results, not a sequence of guarded writes.
        // The latter retains a false read of the old output even with an else,
        // inventing a latch and defeating a static dependency schedule.
        auto before = current, beforePending = pending;
        auto outerActive=memoryActive, beforeLive=active, beforeReturn=returned;
        ++controlDepth;
        memoryActive=both(outerActive,condition);
        statement(yes);
        auto yesValues = current, yesPending = pending;
        auto yesLive=active,yesReturn=returned;
        current = before; pending = beforePending;active=beforeLive;returned=beforeReturn;
        memoryActive=both(outerActive,invert(condition));
        if (no) statement(*no);
        memoryActive=outerActive;
        --controlDepth;
        merge(condition,yesValues,yesPending);
        if(inFunction){active=graph.mux(condition,yesLive,active);returned=graph.mux(condition,yesReturn,returned);}
    }
    void merge(Value condition, const Environment& yes, const Environment& yesPending) {
        auto combine = [&](Environment& destination, const Environment& source, bool nba) {
            std::set<const ValueSymbol*> keys;
            for (auto& [symbol,value] : destination) keys.insert(symbol);
            for (auto& [symbol,value] : source) keys.insert(symbol);
            for (auto symbol : keys) {
                auto fallback = nba && current.count(symbol) ? current.at(symbol) : signal(*symbol);
                auto left = source.count(symbol) ? source.at(symbol) : fallback;
                auto right = destination.count(symbol) ? destination.at(symbol) : fallback;
                destination[symbol] = graph.mux(condition,left,right);
            }
        };
        combine(pending,yesPending,true);
        combine(current,yes,false);
    }
    void statement(const Statement& stmt) {
        // Unreachable statements (including subsequent loop iterations)
        // have no effects and need not be lowered.
        if(inFunction && number(active)==std::optional<uint64_t>(0))return;
        using S = StatementKind;
        switch(stmt.kind) {
        case S::Empty: return;
        case S::List: for (auto child : stmt.as<StatementList>().list) statement(*child); return;
        case S::Block:
            if (stmt.as<BlockStatement>().blockKind != StatementBlockKind::Sequential) fail("parallel block unsupported");
            statement(stmt.as<BlockStatement>().body); return;
        case S::VariableDeclaration: declaration(stmt.as<VariableDeclStatement>().symbol); return;
        case S::ExpressionStatement: expr(stmt.as<ExpressionStatement>().expr); return;
        case S::Conditional: {
            const auto& conditional = stmt.as<ConditionalStatement>(); Value test{1};
            for (auto& item : conditional.conditions) {
                if (item.pattern) fail("condition pattern unsupported");
                test = both(test,expr(*item.expr));
            }
            branch(test,conditional.ifTrue,conditional.ifFalse); return;
        }
        case S::Case: {
            const auto& cases = stmt.as<CaseStatement>();
            if (cases.condition != CaseStatementCondition::Normal) fail("wildcard case unsupported",stmt.sourceRange);
            auto value = expr(cases.expr);
            auto before = current, beforePending = pending;
            auto outerActive=memoryActive,beforeLive=active,beforeReturn=returned;
            ++controlDepth;
            std::vector<Value> conditions;
            for (auto& item : cases.items) {
                Value match{0};
                for (auto option : item.expressions) match = either(match,binary(BinaryOperator::Equality,value,expr(*option),1,false));
                conditions.push_back(match);
            }
            Value matched{0};
            for(const auto& condition:conditions)matched=either(matched,condition);
            memoryActive=both(outerActive,invert(matched));
            if (cases.defaultCase) statement(*cases.defaultCase);
            for (size_t index = cases.items.size(); index > 0; --index) {
                auto otherwise = current, otherwisePending = pending;
                auto otherwiseLive=active,otherwiseReturn=returned;
                current = before; pending = beforePending;active=beforeLive;returned=beforeReturn;
                Value earlier{0};
                for(size_t previous=0;previous+1<index;++previous)earlier=either(earlier,conditions[previous]);
                memoryActive=both(outerActive,both(conditions[index-1],invert(earlier)));
                statement(*cases.items[index-1].stmt);
                auto selected = current, selectedPending = pending;
                auto selectedLive=active,selectedReturn=returned;
                current = std::move(otherwise); pending = std::move(otherwisePending);
                active=otherwiseLive;returned=otherwiseReturn;
                merge(conditions[index-1],selected,selectedPending);
                if(inFunction) {
                    active=graph.mux(conditions[index-1],selectedLive,active);
                    returned=graph.mux(conditions[index-1],selectedReturn,returned);
                }
            }
            memoryActive=outerActive;
            --controlDepth;
            return;
        }
        case S::ForLoop: {
            const auto& loop = stmt.as<ForLoopStatement>();
            for (auto variable : loop.loopVars) declaration(*variable);
            for (auto initial : loop.initializers) expr(*initial);
            for (unsigned iteration = 0; iteration < 65536; ++iteration) {
                if (!loop.stopExpr) fail("unbounded for loop",stmt.sourceRange);
                auto stop = number(boolean(expr(*loop.stopExpr)));
                if (!stop) fail("nonstatic for loop",stmt.sourceRange);
                if (!*stop) return;
                ++controlDepth;
                statement(loop.body);
                --controlDepth;
                auto savedActive = active;
                active = {1};
                for (auto step : loop.steps) expr(*step);
                active = savedActive;
            }
            fail("for loop limit exceeded",stmt.sourceRange);
        }
        case S::Return: {
            if (!inFunction) fail("return outside value function unsupported",stmt.sourceRange);
            auto value = stmt.as<ReturnStatement>().expr;
            if (!value) fail("void return unsupported");
            returned=graph.mux(active,resize(expr(*value),returned.size()),returned);
            active={0};return;
        }
        default: fail("unsupported statement " + std::string(toString(stmt.kind)),stmt.sourceRange);
        }
    }
    Value trigger(const TimingControl& timing, bool resetEvent=false) {
        if (timing.kind == TimingControlKind::EventList) {
            Value result{0};
            bool first=true;
            for (auto event : timing.as<EventListControl>().events) {
                result = either(result,trigger(*event,!first));first=false;
            }
            return result;
        }
        if (timing.kind != TimingControlKind::SignalEvent) fail("unsupported sequential event");
        auto& event = timing.as<SignalEventControl>();
        if (event.iffCondition || (event.edge != EdgeKind::PosEdge && event.edge != EdgeKind::NegEdge))
            fail("unsupported sequential edge");
        auto value = boolean(expr(event.expr));
        const bool asynchronousReset=resetEvent && event.edge==EdgeKind::NegEdge;
        auto previous = graph.add("state",1,constant(event.edge == EdgeKind::NegEdge,1),{},{},
                                  asynchronousReset?"async reset history":"event history");
        graph.states.push_back({previous,value,{1}});
        if(asynchronousReset)graph.ports.push_back({"__cpphdl_async_reset_"+std::to_string(graph.states.size()),
                                                    Value{value[0],previous[0]},false});
        return event.edge == EdgeKind::PosEdge ? both(value,invert(previous)) : both(invert(value),previous);
    }
    void finish(bool sequential, Value enable = {1}) {
        for (auto symbol : written) {
            if (locals.count(symbol)) continue;
            auto target = signal(*symbol);
            if (sequential) {
                if(current.count(symbol)&&pending.count(symbol))
                    fail("mixed blocking/nonblocking module storage is not supported");
                // Blocking temporaries keep source-order SSA reads inside the
                // process. A queued NBA, when present, supplies the final
                // stored value. Dead module temporaries disappear normally.
                const auto& next = pending.count(symbol)?pending.at(symbol):current.at(symbol);
                for (size_t offset = 0; offset < target.size();) {
                    if (graph.resolve(next[offset]) == graph.resolve(target[offset])) { ++offset; continue; }
                    auto end = offset + 1;
                    while (end < target.size() && graph.resolve(next[end]) != graph.resolve(target[end])) ++end;
                    auto name=symbol->getHierarchicalPath();
                    if(!pending.count(symbol))name="SV blocking storage:"+name;
                    auto state = graph.wire(end-offset,name,"state");
                    graph.connect(slice(target,offset,end-offset),state);
                    graph.states.push_back({state,slice(next,offset,end-offset),enable});
                    offset = end;
                }
            } else {
                if (pending.count(symbol)) fail("nonblocking comb assignment unsupported");
                auto value = current.at(symbol);
                for (size_t index = 0; index < value.size(); ++index)
                    if (graph.resolve(value[index]) != graph.resolve(target[index])) graph.connect({target[index]},{value[index]});
            }
        }
    }
    void clear() { current.clear(); pending.clear(); locals.clear(); written.clear(); active={1}; }
    void collect(const Symbol& symbol) {
        if (symbol.kind == SymbolKind::Variable && symbol.as<VariableSymbol>().getInitializer())
            fail("hardware declaration initializer unsupported",symbol.as<VariableSymbol>().getInitializer()->sourceRange);
        if (symbol.kind == SymbolKind::Instance) {
            const auto& instance = symbol.as<InstanceSymbol>();
            hardware.push_back(&symbol);
            if(externalModules.count(std::string(instance.getDefinition().name)))return;
            for (const auto& member : instance.body.members()) collect(member);
        } else if (symbol.kind == SymbolKind::GenerateBlock) {
            if (!symbol.as<GenerateBlockSymbol>().isUninstantiated)
                for (const auto& member : symbol.as<GenerateBlockSymbol>().members()) collect(member);
        } else if (symbol.kind == SymbolKind::GenerateBlockArray || symbol.kind == SymbolKind::InstanceArray) {
            for (const auto& member : symbol.as<Scope>().members()) collect(member);
        } else if (symbol.kind == SymbolKind::Net && symbol.as<NetSymbol>().getInitializer())
            hardware.push_back(&symbol);
        else if (symbol.kind == SymbolKind::ContinuousAssign || symbol.kind == SymbolKind::ProceduralBlock)
            hardware.push_back(&symbol);
    }
public:
    explicit Lowering(const InstanceSymbol& instance,std::set<std::string> external={}) :
        top(instance), scope(&instance.body),externalModules(std::move(external)) {}
    void run(const std::string& output) {
        for (auto portSymbol : top.body.getPortList()) {
            if (portSymbol->kind != SymbolKind::Port) fail("top interface ports unsupported");
            auto& port = portSymbol->as<PortSymbol>();
            if (port.hasInitializer()) fail("top port initializer unsupported");
            if (!port.internalSymbol || !port.internalSymbol->isValue()) fail("complex top port unsupported");
            if (port.direction != ArgumentDirection::In && port.direction != ArgumentDirection::Out) fail("inout port unsupported");
            auto& symbol = port.internalSymbol->as<ValueSymbol>();
            auto value = graph.wire(width(port.getType()),std::string(port.name),port.direction == ArgumentDirection::In ? "input" : "wire");
            signals[&symbol] = value;
            graph.ports.push_back({std::string(port.name),value,port.direction == ArgumentDirection::In});
        }
        collect(top);
        const SubroutineSymbol* referencingFunction = nullptr;
        auto collector = makeVisitor(
            [&](auto& self, const SubroutineSymbol& value) {
                auto saved = referencingFunction;
                referencingFunction = &value;
                self.visitDefault(value);
                referencingFunction = saved;
            },
            [&](auto&, const NamedValueExpression& value) {
                references[&value.symbol].insert(referencingFunction);
            },
            [&](auto&, const HierarchicalValueExpression& value) {
                references[&value.symbol].insert(referencingFunction);
            });
        top.visit(collector);
        discoverMemories();
        for (auto symbol : hardware) {
            clear(); scope = symbol->getParentScope();
            process=symbol;sequentialProcess=false;processEnable={1};memoryActive={1};
            if (symbol->kind == SymbolKind::Instance) {
                auto& instance = symbol->as<InstanceSymbol>();
                if (&instance == &top) continue;
                const auto module=std::string(instance.getDefinition().name);
                const bool external=externalModules.count(module);
                unsigned externalId=externalCount;
                if(external)++externalCount;
                for (auto connection : instance.getPortConnections()) {
                    if (connection->port.kind == SymbolKind::InterfacePort) continue;
                    if (connection->port.kind != SymbolKind::Port) fail("complex instance port unsupported");
                    auto& port = connection->port.as<PortSymbol>();
                    if (port.direction == ArgumentDirection::Out && port.hasInitializer())
                        fail("output port initializer unsupported");
                    auto expression = connection->getExpression();
                    if (!expression) continue;
                    if (!port.internalSymbol || !port.internalSymbol->isValue()) fail("complex internal port unsupported");
                    auto& internal = port.internalSymbol->as<ValueSymbol>();
                    if(external) {
                        auto name="__external_"+std::to_string(externalId)+"_"+std::string(port.name);
                        const bool input=port.direction==ArgumentDirection::In;
                        auto value=signal(internal);
                        if(!input) {
                            auto drive=graph.wire(width(port.getType()),name,"input");
                            graph.connect(value,drive);value=drive;
                        }
                        graph.ports.push_back({name,value,!input});
                        externalPorts.push_back({externalId,module,instance.getHierarchicalPath(),std::string(port.name),name,input,width(port.getType())});
                    }
                    if (port.direction == ArgumentDirection::In) graph.connect(signal(internal),expr(*expression));
                    else if (port.direction == ArgumentDirection::Out) {
                        auto target = expression;
                        if (target->kind == ExpressionKind::Assignment) target = &target->as<AssignmentExpression>().left();
                        assign(*target,resize(signal(internal),width(*target->type),port.getType().isSigned()),false);
                        finish(false); clear();
                    } else fail("bidirectional instance port unsupported");
                }
            } else if(symbol->kind==SymbolKind::Net) {
                const auto& net=symbol->as<NetSymbol>();
                graph.connect(signal(net),resize(expr(*net.getInitializer()),width(net.getType()),net.getType().isSigned()));
            } else if (symbol->kind == SymbolKind::ContinuousAssign) {
                expr(symbol->as<ContinuousAssignSymbol>().getAssignment()); finish(false);
            } else {
                auto& block = symbol->as<ProceduralBlockSymbol>();
                if (block.isFromAssertion) fail("assertion process requires synthesis guard");
                if (block.procedureKind == ProceduralBlockKind::Initial || block.procedureKind == ProceduralBlockKind::Final)
                    fail("initial/final process unsupported",block.getBody().sourceRange);
                const Statement* body = &block.getBody();
                bool sequential = false; Value enable{1};
                if (body->kind == StatementKind::Timed) {
                    auto& timed = body->as<TimedStatement>();
                    if (timed.timing.kind != TimingControlKind::ImplicitEvent) { sequential=true; enable=trigger(timed.timing); }
                    body = &timed.stmt;
                }
                sequentialProcess=sequential;processEnable=enable;
                statement(*body); finish(sequential,enable);
            }
        }
        graph.writeCpp(output);
        if(!externalPorts.empty()) {
            std::ofstream metadata(output+".externals.tsv");
            for(const auto& p:externalPorts)metadata<<p.instance<<'\t'<<p.module<<'\t'<<p.path<<'\t'<<p.port<<'\t'<<p.pin<<'\t'<<p.input<<'\t'<<p.bits<<'\n';
        }
    }
};

inline int run(int argc, char** argv) {
    try {
        std::string output;
        std::set<std::string> external;
        std::vector<const char*> args{argv[0]};
        for (int index=2; index<argc; ++index) {
            if (std::string_view(argv[index]) == "--output") {
                if (++index == argc) throw std::runtime_error("missing output path");
                output=argv[index];
            } else if(std::string_view(argv[index])=="--external-module") {
                if(++index==argc)throw std::runtime_error("missing external module name");
                external.insert(argv[index]);
            } else args.push_back(argv[index]);
        }
        if (output.empty() || std::filesystem::exists(output)) throw std::runtime_error("--output must name a new C++ file");
        driver::Driver driver;
        driver.addStandardArgs();
        if (!driver.parseCommandLine(int(args.size()),args.data()) || !driver.processOptions() || !driver.parseAllSources()) return 1;
        auto compilation = driver.createCompilation();
        driver.reportCompilation(*compilation,true);
        if (!driver.reportDiagnostics(true)) return 1;
        auto& root = compilation->getRoot();
        if (root.topInstances.size() != 1) throw std::runtime_error("native graph requires exactly one --top");
        Lowering(*root.topInstances[0],std::move(external)).run(output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hdlcpp native graph: " << error.what() << '\n'; return 1;
    }
}
}

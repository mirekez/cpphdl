#include "AstClocked.h"
#include "SharedBlocks.h"
#include "Combinational.h"
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

namespace cpphdl::hls {
namespace {
using namespace clang;
using E = clang::Expr;
ASTContext* context;
Sema* semantics;
bool success = true;

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
};

bool runtimeCall(const Stmt* stmt) {
    if (!stmt) return false;
    if (isa<CallExpr>(stmt) || isa<CXXConstructExpr>(stmt)) return true;
    for (const auto* child : stmt->children()) if (child && runtimeCall(child)) return true;
    return false;
}

class Scheduler {
    ASTContext& ctx;
    Sema& sema;
    std::vector<Block> blocks;
    std::vector<FunctionScope> contexts;
    struct CallRegion { int entry, exit; std::string callerMethod; };
    std::vector<CallRegion> callRegions;
    std::map<const FunctionDecl*, unsigned> calling;
    std::map<const VarDecl*, Value> constants;
    struct Loop { int first, second; size_t scopes; };
    std::vector<Loop> loops;
    std::vector<std::string> traces;
    std::map<const FieldDecl*, std::string> fieldOffsets;
    std::vector<std::string> layouts;
    unsigned highWater = 4112, constantBytes = 16, tempCount = 0;
    unsigned recursionLimit = 0;
    unsigned callCount = 0, symbolCount = 0;
    std::string scope = "hls_object", method = "hls_object";
    std::map<std::string, BlockSymbol> blockSymbols;
    std::map<std::string, std::string> symbolLabels;
    std::set<unsigned> storageReadWidths, storageWriteWidths;
    bool heap = false;
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
    std::vector<Access> accesses;
    std::set<std::string> clockedValues;

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
    std::string addressSymbol(const std::string& name, unsigned address) {
        std::string symbol = name + "_" + std::to_string(symbolCount++);
        symbols << "  localparam logic [63:0] " << symbol << " = 64'd" << address << ";\n";
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
        if (type->isReferenceType()) return 64;
        if (type->isBooleanType()) return 1;
        return bytes(type) * 8;
    }
    Value slot(QualType type, const std::string& source = "temporary") {
        std::string name = scope + "__" + identifier(source);
        std::string id = std::to_string(symbolCount++);
        Value result{name + "_addr_" + id, type.getNonReferenceType(), true, name};
        objects.emplace(result.text, SourceObject{result.type, name + "_" + id, identifier(source), !result.type->isScalarType()});
        symbolLabels[name] = identifier(source);
        return result;
    }
    std::string access(Access value) {
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
    std::string resolveAccesses(const std::string& text, std::set<std::string>& uses, std::set<std::string>& definitions) {
        std::string result;
        size_t cursor = 0;
        while (cursor < text.size()) {
            size_t begin = text.find("@hls_access_", cursor);
            if (begin == std::string::npos) { result += text.substr(cursor); break; }
            result += text.substr(cursor, begin - cursor);
            size_t end = text.find('@', begin + 12);
            if (end == std::string::npos) reject(nullptr, "unterminated source access handle");
            const auto& item = accesses.at(std::stoul(text.substr(begin + 12, end - begin - 12)));
            auto object = objects.find(item.address);
            auto address = resolveAccesses(item.address, uses, definitions);
            auto data = resolveAccesses(item.data, uses, definitions);
            if (object != objects.end() && !object->second.memory) {
                auto name = "values." + object->second.name;
                if (item.write) {
                    result += name + " = " + castTo(item.type, data) + ";";
                    definitions.insert(name);
                } else {
                    if (!definitions.count(name)) uses.insert(name);
                    result += name;
                }
            } else {
                unsigned width = bytes(item.type) * 8;
                if (item.write) {
                    storageWriteWidths.insert(bytes(item.type));
                    result += "hls_storage_write_" + std::to_string(width) + "(storage, " + address + ", " +
                        std::to_string(width) + "'(" + castTo(item.type, data) + "), fault);";
                } else {
                    storageReadWidths.insert(bytes(item.type));
                    result += "hls_storage_" + std::string(item.checked ? "read_" : "load_") +
                        std::to_string(width) + "(storage, " + address + (item.checked ? ", fault)" : ")");
                }
            }
            cursor = end + 1;
        }
        return result;
    }
    void lowerSourceValues() {
        // Only an actual address use needs memory. Reading or assigning a known
        // scalar becomes direct data movement, with no virtual load/store.
        for (const auto& item : accesses) {
            if (!objects.count(item.address)) exposeAddresses(item.address);
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
                symbols << "  localparam logic [63:0] " << address << " = 64'd" << highWater << ";\n";
                highWater += bytes(object.type);
                blockSymbols[address] = {64, object.label + "_addr", false};
            } else blockSymbols["values." + object.name] = {bits(object.type), object.label, true};
        }
        std::vector<std::set<std::string>> uses(blocks.size()), definitions(blocks.size()), liveIn(blocks.size());
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto& b = blocks[i];
            for (auto& s : b.statements) s = resolveAccesses(s, uses[i], definitions[i]);
            b.condition = resolveAccesses(b.condition, uses[i], definitions[i]);
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
        for (const auto& b : blocks) if (b.suspend && b.yes >= 0)
            clockedValues.insert(liveIn[b.yes].begin(), liveIn[b.yes].end());
    }
    std::string temporary(unsigned width, const std::string& text, const std::string& source = "") {
        std::string name = (source.empty() ? scope + "__temporary" : source) + "_" + std::to_string(tempCount++);
        name = "scratch." + name;
        auto label = symbolLabels.find(source);
        blockSymbols[name] = {width, label == symbolLabels.end() ? "temporary" : label->second, true};
        emit(name + " = " + std::to_string(width) + "'(" + text + ");");
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
    std::string storageBytes(const std::string& address, unsigned count) {
        if (count != 8) reject(nullptr, "source pointer width is not 64 bits");
        return access({address, {}, ctx.VoidPtrTy, false, false});
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
        return storageBytes(address.text, 8);
    }
    Value capture(Value value) {
        if (value.type->isVoidType()) return value;
        if (value.location) return reference(value);
        Value saved = slot(value.type); store(saved, value); return saved;
    }
    Value reference(Value value) {
        if (!value.location) {
            Value saved = slot(value.type); store(saved, value); value = saved;
        }
        if (objects.count(value.text)) return value;
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
        out << R"SV(  function static logic hls_storage_address_valid(input logic [63:0] address, input int unsigned count);
    return count <= MEM_BYTES && address >= 64'd16 && address <= 64'(MEM_BYTES) - 64'(count);
  endfunction
)SV";
        for (unsigned count : storageReadWidths) {
            unsigned width = count * 8;
            out << "  function static logic [" << width - 1 << ":0] hls_storage_load_" << width
                << "(input storage_t storage, input logic [63:0] address);\n"
                << "    return {";
            for (unsigned lane = count; lane-- > 0;) {
                if (lane != count - 1) out << ", ";
                out << "storage[INDEX_BITS'(address + 64'd" << lane << ")]";
            }
            out << "};\n  endfunction\n"
                << "  function static logic [" << width - 1 << ":0] hls_storage_read_" << width
                << "(input storage_t storage, input logic [63:0] address, inout logic [31:0] fault);\n"
                << "    if (fault == 0) begin\n"
                << "      if (hls_storage_address_valid(address, " << count << ")) return hls_storage_load_" << width << "(storage, address);\n"
                << "      fault = 3;\n    end\n    return '0;\n  endfunction\n";
        }
        for (unsigned count : storageWriteWidths) {
            unsigned width = count * 8;
            out << "  function static void hls_storage_write_" << width
                << "(inout storage_t storage, input logic [63:0] address, input logic [" << width - 1 << ":0] value, inout logic [31:0] fault);\n"
                << "    if (fault == 0) begin\n"
                << "      if (hls_storage_address_valid(address, " << count << ")) begin\n"
                << "        for (int unsigned lane = 0; lane < " << count << "; ++lane)\n"
                << "          storage[INDEX_BITS'(address + 64'(lane))] = value[lane * 8 +: 8];\n"
                << "      end else fault = 3;\n    end\n  endfunction\n";
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
        Value result{"(" + base.text + " + " + found->second + ")", field->getType(), true,
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
    const Stmt* definition(FunctionDecl*& fn) {
        if (!fn->hasBody() && fn->getTemplateInstantiationPattern())
            sema.InstantiateFunctionDefinition(fn->getLocation(), fn, false, false, true);
        const FunctionDecl* actual = nullptr;
        const Stmt* body = fn->getBody(actual);
        if (actual) fn = const_cast<FunctionDecl*>(actual);
        return body;
    }

    Value invoke(FunctionDecl* fn, const std::vector<Value>& args, std::string self = "", Value constructed = {}) {
        std::string name = fn->getQualifiedNameAsString();
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
                auto alignment = temporary(64, read(args[1]), scope + "__allocation_alignment");
                emit("if (" + alignment + " == 0 || " + alignment + " > 4096 || (" + alignment + " & (" + alignment + " - 1)) != 0) fault = 1;");
                emit("heap_next = (heap_next + " + alignment + " - 1) & ~(" + alignment + " - 1);");
            }
            Value result = slot(fn->getReturnType());
            emit("if (" + size + " > 4096 || heap_next + 64'(" + size + ") > 64'(MEM_BYTES)) fault = 1;");
            store(result, {"heap_next", fn->getReturnType()});
            emit("heap_next = (heap_next + 64'(" + size + ") + 15) & ~64'd15;");
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
            Value dst = slot(ctx.VoidPtrTy), src = slot(ctx.UnsignedLongLongTy), count = slot(ctx.UnsignedLongLongTy);
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
            if (fn->getReturnType()->isReferenceType()) return {"64'd0", fn->getReturnType()->getPointeeType(), true};
            return {"'0", fn->getReturnType()};
        }
        ++depth;
        if (contexts.size() >= 64) reject(fn->getBody(), "clocked call depth exceeds 64");
        if (args.size() != fn->getNumParams()) reject(fn->getBody(), "argument count mismatch: " + name);
        const Stmt* body = definition(fn);
        auto* ctor = dyn_cast<CXXConstructorDecl>(fn);
        if (!body && !(fn->isDefaulted() && (ctor || isa<CXXDestructorDecl>(fn)))) reject(nullptr, "missing instantiated body: " + name + " builtin=" + std::to_string(fn->getBuiltinID()) + " at " + fn->getLocation().printToString(ctx.getSourceManager()));
        traces.push_back(name + " at " + fn->getLocation().printToString(ctx.getSourceManager()));
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
                            store({"(" + target.text + " + 64'd" + std::to_string(offset) + ")", ctx.VoidPtrTy, true}, {ref.text, ctx.VoidPtrTy});
                            continue;
                        }
                        target = member(target, field);
                    } else if (init->isBaseInitializer()) {
                        auto* base = init->getBaseClass()->getAsCXXRecordDecl();
                        auto offset = ctx.getASTRecordLayout(ctor->getParent()).getBaseClassOffset(base).getQuantity();
                        target = {"(" + self + " + 64'd" + std::to_string(offset) + ")", QualType(init->getBaseClass(), 0), true};
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
                destroy({"(" + object.text + " + 64'd" + std::to_string(offset) + ")", it->getType(), true});
            }
        }
        jump(binding.continuation); current = binding.continuation;
        callRegions.push_back({entry, binding.continuation, callerMethod});
        contexts.pop_back(); loops = std::move(savedLoops); --calling[fn->getCanonicalDecl()];
        scope = callerScope;
        method = callerMethod;
        if (ctor) return constructed;
        if (fn->getReturnType()->isReferenceType()) {
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
        if (auto* clean = dyn_cast<ExprWithCleanups>(init)) { initialize(target, clean->getSubExpr()); return; }
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
                    Value to{"(" + target.text + " + 64'd" + std::to_string(n++ * bytes(array->getElementType())) + ")", array->getElementType(), true};
                    initialize(to, element);
                }
            } else if (const auto* record = target.type->getAsCXXRecordDecl()) {
                // The semantic initializer list contains bases first, then fields.
                for (const auto& base : record->bases()) {
                    if (n == list->getNumInits()) break;
                    if (base.isVirtual()) reject(init, "virtual aggregate base initialization");
                    auto offset = ctx.getASTRecordLayout(record).getBaseClassOffset(base.getType()->getAsCXXRecordDecl()).getQuantity();
                    initialize({"(" + target.text + " + 64'd" + std::to_string(offset) + ")", base.getType(), true}, list->getInit(n++));
                }
                for (auto* field : record->fields()) {
                    if (n == list->getNumInits()) break;
                    if (field->getType()->isReferenceType()) {
                        Value ref = reference(expression(list->getInit(n++)));
                        unsigned offset = ctx.getASTRecordLayout(record).getFieldOffset(field->getFieldIndex()) / 8;
                        store({"(" + target.text + " + 64'd" + std::to_string(offset) + ")", ctx.VoidPtrTy, true}, {ref.text, ctx.VoidPtrTy});
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
                initializeConstant({"(" + target.text + " + 64'd" + std::to_string(offset) + ")", base.getType(), true}, value.getStructBase(i++));
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
                initializeConstant({"(" + target.text + " + 64'd" + std::to_string(i * bytes(element)) + ")", element, true}, item);
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
        if (type->isFloatingType()) reject(expr, "floating-point AST lowering not implemented");
        if (!expr->isValueDependent() && !runtimeCall(expr) && !expr->HasSideEffects(ctx) && type->isIntegralOrEnumerationType()) {
            E::EvalResult result;
            if (expr->EvaluateAsInt(result, ctx)) {
                llvm::SmallString<32> number; static_cast<const llvm::APInt&>(result.Val.getInt()).toString(number, 16, false);
                return {std::to_string(bits(type)) + "'h" + number.str().str(), type};
            }
        }
        if (auto* p = dyn_cast<ParenExpr>(expr)) return expression(p->getSubExpr());
        if (auto* p = dyn_cast<CXXRewrittenBinaryOperator>(expr)) return expression(p->getSemanticForm());
        if (auto* p = dyn_cast<ExprWithCleanups>(expr)) return expression(p->getSubExpr());
        if (auto* p = dyn_cast<MaterializeTemporaryExpr>(expr)) return reference(expression(p->getSubExpr()));
        if (auto* p = dyn_cast<CXXBindTemporaryExpr>(expr)) {
            if (!p->getTemporary()->getDestructor()->isTrivial()) reject(expr, "nontrivial temporary cleanup not implemented");
            return expression(p->getSubExpr());
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
                store({"(" + result.text + " + 64'd" + std::to_string(i) + ")", ctx.CharTy, true},
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
                std::string adjusted = "(" + addr + (downcast ? " - 64'd" : " + 64'd") + std::to_string(offset) + ")";
                if (pointer && offset) adjusted = "(" + addr + " == 0 ? 64'd0 : " + adjusted + ")";
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
            return {"(" + read(base) + " + 64'(" + read(i) + ") * 64'd" + std::to_string(bytes(type)) + ")", type, true};
        }
        if (auto* unary = dyn_cast<UnaryOperator>(expr)) {
            Value sub = expression(unary->getSubExpr());
            if (unary->getOpcode() == UO_AddrOf) return {reference(sub).text, type};
            if (unary->getOpcode() == UO_Deref) return {read(sub), type, true};
            if (unary->isIncrementDecrementOp()) {
                auto old = read(sub); unsigned step = type->isPointerType() ? bytes(type->getPointeeType()) : 1;
                std::string value = "(" + old + (unary->isIncrementOp() ? " + " : " - ") + std::to_string(step) + ")";
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
            std::string a = read(lhs), b = read(rhs), op = binary->getOpcodeStr().str();
            if (binary->isCompoundAssignmentOp()) op.pop_back();
            bool pointer = lhs.type->isPointerType();
            if (pointer && !rhs.type->isPointerType() && (op == "+" || op == "-"))
                b = "(64'(" + b + ") * 64'd" + std::to_string(bytes(lhs.type->getPointeeType())) + ")";
            if (op == ">>" && lhs.type->isSignedIntegerType()) op = ">>>";
            std::string result = "(" + a + " " + op + " " + b + ")";
            if (pointer && rhs.type->isPointerType() && op == "-") result = "($signed(" + result + ") / 64'd" + std::to_string(bytes(lhs.type->getPointeeType())) + ")";
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
            Value pointer = slot(ctx.VoidPtrTy);
            store(pointer, {object.type->isPointerType() ? read(object) : object.text, ctx.VoidPtrTy});
            self = savedPointer(pointer);
        }
        for (unsigned n = first; n < call->getNumArgs(); ++n) args.push_back(capture(expression(call->getArg(n))));
        return invoke(fn, args, self, destination);
    }

    void statement(const Stmt* stmt) {
        if (!stmt) return;
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
                    if (contexts.back().referenceResult)
                        store(contexts.back().result, {reference(value).text, ctx.VoidPtrTy});
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
    Scheduler(ASTContext& context, Sema& sema) : ctx(context), sema(sema) { block(); }
    std::string generate(const CXXRecordDecl* wrapper, const std::string& name) {
        if (auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(wrapper)) {
            const auto& args = specialization->getTemplateArgs();
            if (args.size() > 1) recursionLimit = args[1].getAsIntegral().getLimitedValue();
            if (recursionLimit > 16) reject(nullptr, "MAX_RECURSION must be in 0..16");
        }
        const FieldDecl* object = nullptr;
        for (auto* field : wrapper->fields()) if (field->getName() == "object") object = field;
        if (!object) reject(nullptr, "clocked wrapper has no object");
        Value state = slot(object->getType(), object->getNameAsString());
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
        initialize(state, object->getInClassInitializer());
        emit("booting = 0; phase = 0;");
        current = block(); int commandEntry = current;
        Value result = invoke(command, {{"operation_in", ctx.UnsignedIntTy}, {"index_in", ctx.UnsignedIntTy}, {"value_in", ctx.UnsignedIntTy}}, state.text);
        emit("result = " + read(result) + "; pending = 1; phase = 0;");
        contexts.pop_back();
        lowerSourceValues();
        BlockSharing sharing(blockSymbols);
        BlockUses uses(blocks, blockSymbols);
        unsigned outlinedCalls = 0;
        for (const auto& call : callRegions)
            if (outlineCall(blocks, call.entry, call.exit, call.callerMethod, sharing, clockedValues, &uses)) ++outlinedCalls;
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
        for (const auto& b : blocks) shared.calls.push_back(sharing.add(b));
        shared.functions = std::move(sharing.functions);
        std::set<std::string> usedValues = clockedValues;
        for (const auto& call : shared.calls)
            usedValues.insert(call.arguments.begin(), call.arguments.end());
        auto signalName = [](std::string name) {
            auto dot = name.find('.');
            if (dot != std::string::npos) name.replace(dot, 1, "__");
            return name;
        };
        unsigned heapBase = (highWater + 15) & ~15u;
        unsigned memoryBytes = heapBase + (heap ? 4096 : 0);
        std::ostringstream out;
        out << "// AST clocked instantiation. No compiler IR or external compiler invocation.\n";
        out << "// Shared schedule: " << blocks.size() << " blocks, " << shared.functions.size() << " function bodies\n";
        out << "// Shared lowering: " << blocks.size() + outlinedCalls << " bodies, " << shared.functions.size() << " function bodies\n";
        out << "// Whole same-clock calls: " << outlinedCalls << "\n";
        unsigned directCount = 0;
        for (const auto& [address, object] : objects) if (!object.memory) ++directCount;
        out << "// Source values: " << directCount << " direct scalars, " << clockedValues.size() << " live across clocks\n";
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
        out << symbols.str();
        out << "  localparam int MEM_BYTES = " << memoryBytes << ";\n"
            << "  localparam int INDEX_BITS = $clog2(MEM_BYTES);\n"
            << "  localparam int STATE_BITS = $clog2(" << std::max(size_t(2), blocks.size()) << ");\n"
            << "  typedef logic [7:0] storage_t [MEM_BYTES];\n"
            << "  storage_t storage, storage_reg;\n"
            << "  logic [" << blocks.size() - 1 << ":0] active;\n"
            << "  logic [31:0] phase, phase_reg, fault, fault_reg;\n"
            << "  logic [63:0] result, result_reg, heap_next, heap_next_reg;\n"
            << "  logic pending, pending_reg, booting, booting_reg;\n";
        // These are independent combinational source values, not an aggregate
        // data object. Avoid huge unused struct comparison operators in host RTL.
        for (const auto& name : usedValues) if (name.find('.') != std::string::npos)
            out << "  logic [" << blockSymbols.at(name).width - 1 << ":0] " << signalName(name) << ";\n";
        out << "  typedef struct {\n";
        for (const auto& [address, object] : objects) if (clockedValues.count("values." + object.name))
            out << "    logic [" << bits(object.type) - 1 << ":0] " << object.name << ";\n";
        out << "    logic unused_bit;\n  } clocked_values_t;\n  clocked_values_t values_reg;\n";
        emitStorageHelpers(out);
        for (const auto& function : shared.functions) {
            const auto& body = function.body;
            out << "  // " << body.source << "\n  // Reused by " << function.uses << " scheduled blocks\n"
                << (body.combinational ? "  // Whole same-clock method\n" : "  // Clocked continuation\n")
                << "  function static void " << body.name << "(\n";
            if (body.combinational) out << "    inout storage_t storage,\n"
                << "    inout logic [31:0] fault,\n"
                << "    inout logic [63:0] heap_next,\n"
                << "    input logic [31:0] operation_in, index_in, value_in";
            else out << "    inout storage_t storage,\n"
                << "    input logic [31:0] operation_in, index_in, value_in,\n"
                << "    inout logic [31:0] phase, fault,\n"
                << "    inout logic [63:0] result, heap_next,\n"
                << "    inout logic pending, booting,\n"
                << "    inout logic [" << blocks.size() - 1 << ":0] active,\n"
                << "    input logic [31:0] next_block, else_block";
            for (const auto& parameter : function.parameters)
                // Each writable symbol has one distinct formal, so inout
                // copies cannot alias when a shared body returns.
                out << ",\n    " << (parameter.writable ? "inout" : "input") << " logic [" << parameter.width - 1 << ":0] " << parameter.name;
            out << ");\n    begin\n";
            for (const auto& text : body.statements)
                if (body.combinational) out << text;
                else out << "      if (fault == 0) begin " << text << " end\n";
            if (body.yes >= 0) {
                out << "      if (fault == 0) begin ";
                if (!body.condition.empty()) out << "if (" << body.condition << ") ";
                if (body.suspend) out << "phase = next_block + 32'd1; // loop boundary\n";
                else out << "active[STATE_BITS'(next_block)] = 1;";
                if (body.no >= 0) out << " else active[STATE_BITS'(else_block)] = 1;";
                out << " end\n";
            }
            out << "    end\n  endfunction\n";
        }
        out << R"SV(  assign command_ready_out = !booting_reg && !pending_reg && phase_reg == 0 && fault_reg == 0;
  assign response_valid_out = pending_reg;
  assign result_out = result_reg;
  assign fault_out = fault_reg;
  always_comb begin
    storage = storage_reg; heap_next = heap_next_reg;
    phase = phase_reg; fault = fault_reg; result = result_reg;
    pending = pending_reg; booting = booting_reg; active = '0;
)SV";
        for (const auto& name : usedValues) if (name.find('.') != std::string::npos)
            out << "    " << signalName(name) << " = '0;\n";
        for (const auto& [address, object] : objects) if (clockedValues.count("values." + object.name))
            out << "    values__" << object.name << " = values_reg." << object.name << ";\n";
        out << "    if (reset) begin\n      phase = " << resetEntry + 1 << "; fault = 0; pending = 0; result = 0; booting = 1; heap_next = " << heapBase << ";\n"
            << "    end else if (fault == 0) begin\n      if (pending && response_ready_in) pending = 0;\n"
            << "      if (command_valid_in && command_ready_out) active[" << commandEntry << "] = 1;\n"
            << "      else case (phase_reg)\n";
        for (int n : continuations) out << "        " << n + 1 << ": active[" << n << "] = 1;\n";
        out << "        0: begin end\n        default: fault = 4;\n      endcase\n    end else if (response_ready_in) pending = 0;\n";
        for (int n : order) {
            const auto& call = shared.calls[n];
            out << "    if (active[" << n << "] && fault == 0) " << shared.functions[call.function].body.name
                << "(storage, operation_in, index_in, value_in, phase, fault, result, heap_next, pending, booting, active, "
                << std::max(0, blocks[n].yes) << ", " << std::max(0, blocks[n].no);
            for (const auto& argument : call.arguments) out << ", " << signalName(argument);
            out << ");\n";
        }
        out << R"SV(    if (fault != 0 && fault_reg == 0) begin pending = 1; phase = 0; booting = 0; end
  end
  always_ff @(posedge clk) begin
)SV";
        out << "    values_reg.unused_bit <= 0;\n";
        for (const auto& [address, object] : objects) if (clockedValues.count("values." + object.name))
            out << "    values_reg." << object.name << " <= reset ? '0 : values__" << object.name << ";\n";
        out << R"SV(
    if (!reset) storage_reg <= storage;
    phase_reg <= phase; fault_reg <= fault; result_reg <= result;
    pending_reg <= pending; booting_reg <= booting; heap_next_reg <= heap_next;
  end
endmodule
)SV";
        return out.str();
    }
};
}

void prepareClocked(clang::ASTContext& ctx, clang::Sema& sema) { context = &ctx; semantics = &sema; }
bool isClocked(const clang::CXXRecordDecl* record) {
    for (const auto* attr : record->specific_attrs<clang::AnnotateAttr>())
        if (attr->getAnnotation() == "CPPHDL_HLS_CLOCKED") return true;
    return false;
}
std::string clockedName(const clang::CXXRecordDecl* record, const std::string& base) {
    if (auto* specialization = dyn_cast<ClassTemplateSpecializationDecl>(record)) {
        const auto& args = specialization->getTemplateArgs();
        if (args.size() > 1 && args[1].getKind() == TemplateArgument::Integral) {
            auto bound = args[1].getAsIntegral().getLimitedValue();
            if (bound) return base + "_R" + std::to_string(bound);
        }
    }
    return base;
}
bool exportClocked(const clang::CXXRecordDecl* record, cpphdl::Module& module) {
    try {
        if (!context || !semantics) throw std::runtime_error("Clocked<T> requires --hls");
        if (!currProject->clocks.empty()) throw std::runtime_error("Clocked<T> currently requires the default clk, not named/CDC clocks");
        Scheduler scheduler(*context, *semantics);
        module.replacement = scheduler.generate(record, module.name);
        llvm::outs() << "HLS: source AST scheduled for " << module.name << "\n";
    } catch (const std::exception& error) {
        llvm::errs() << "HLS AST error: " << error.what() << "\n"; success = false;
    }
    return true;
}
bool clockedSucceeded() { return success; }
}

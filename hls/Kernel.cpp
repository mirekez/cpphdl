#include "Kernel.h"
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/LowerMemIntrinsics.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cpphdl::hls {
namespace {
using namespace llvm;

unsigned width(Type* type) {
    if (type->isPointerTy() && type->getPointerAddressSpace() == 0) return 64;
    if (type->isIntegerTy() && type->getIntegerBitWidth() <= 64)
        return type->getIntegerBitWidth();
    throw std::runtime_error("only scalar integers up to 64 bits and local pointers are supported");
}

// Inline method bodies, never substitute an algorithm according to container name.
void normalize(Function& root) {
    std::map<Function*, unsigned> visited;
    std::function<void(Function*)> checkCalls = [&](Function* function) {
        if (visited[function] == 1) throw std::runtime_error("recursive kernel needs bounded unfolding");
        if (visited[function] == 2) return;
        visited[function] = 1;
        for (auto& i : instructions(function)) if (auto* call = dyn_cast<CallBase>(&i)) {
            auto* callee = call->getCalledFunction();
            if (!callee) throw std::runtime_error("indirect call needs bounded target analysis");
            if (!callee->isDeclaration()) checkCalls(callee);
        }
        visited[function] = 2;
    };
    checkCalls(&root);
    unsigned inlined = 0;
    for (;;) {
        CallBase* candidate = nullptr;
        for (auto& instruction : instructions(root)) {
            auto* call = dyn_cast<CallBase>(&instruction);
            if (!call) continue;
            auto* callee = call->getCalledFunction();
            if (!callee) throw std::runtime_error("indirect call needs bounded target analysis");
            if (callee == &root) throw std::runtime_error("recursive kernel needs bounded unfolding");
            if (!callee->isDeclaration()) { candidate = call; break; }
        }
        if (!candidate) break;
        if (++inlined > 2048) throw std::runtime_error("method inlining exceeded bound (possible recursion)");
        InlineFunctionInfo info;
        if (!InlineFunction(*candidate, info).isSuccess())
            throw std::runtime_error("could not inline a reachable method");
    }
    SmallVector<Instruction*> remove;
    TargetTransformInfo target(root.getDataLayout());
    SmallVector<MemIntrinsic*> memory;
    for (auto& instruction : instructions(root)) {
        if (auto* mem = dyn_cast<MemIntrinsic>(&instruction)) memory.push_back(mem);
        else if (auto* intrinsic = dyn_cast<IntrinsicInst>(&instruction)) {
            switch (intrinsic->getIntrinsicID()) {
            case Intrinsic::lifetime_start: case Intrinsic::lifetime_end:
            case Intrinsic::experimental_noalias_scope_decl:
                remove.push_back(intrinsic); break;
            default: break;
            }
        }
    }
    for (auto* mem : memory) {
        if (mem->isVolatile()) throw std::runtime_error("volatile bulk memory access is unsupported");
        if (auto* copy = dyn_cast<MemCpyInst>(mem)) expandMemCpyAsLoop(copy, target);
        else if (auto* move = dyn_cast<MemMoveInst>(mem)) {
            if (!expandMemMoveAsLoop(move, target)) throw std::runtime_error("cannot lower memmove");
        } else if (auto* set = dyn_cast<MemSetInst>(mem)) expandMemSetAsLoop(set);
        else throw std::runtime_error("unsupported memory intrinsic");
        remove.push_back(mem);
    }
    for (auto* instruction : remove) instruction->eraseFromParent();
}

class Emitter {
    Function& root;
    const DataLayout& layout;
    std::map<Value*, std::string> names;
    std::map<BasicBlock*, unsigned> blocks;
    std::map<Instruction*, unsigned> states;
    std::map<AllocaInst*, uint64_t> locals;
    struct Region {
        Instruction* first;
        Instruction* last;
        LoadInst* resumedLoad;
    };
    std::vector<Region> regions{{nullptr, nullptr, nullptr}};
    std::vector<unsigned> order;
    std::set<std::pair<BasicBlock*, BasicBlock*>> backEdges;
    uint64_t bytes;
    std::ostringstream declarations, defaults, commits, sramCommits, sramTieoffs, body;

    std::string value(Value* v) {
        if (auto* c = dyn_cast<ConstantInt>(v)) {
            SmallString<32> number;
            c->getValue().toString(number, 16, false);
            return std::to_string(width(v->getType())) + "'h" + number.str().str();
        }
        if (isa<ConstantPointerNull>(v) || isa<UndefValue>(v))
            return std::to_string(width(v->getType())) + "'d0";
        auto found = names.find(v);
        if (found == names.end()) throw std::runtime_error("unsupported constant, global address, or value");
        return found->second;
    }
    unsigned entry(BasicBlock* b) { return states.at(&b->front()); }
    std::string jump(BasicBlock* from, BasicBlock* to) {
        std::string result = "pred = " + std::to_string(blocks.at(from)) + "; ";
        if (backEdges.count({from, to}))
            return result + "pc = " + std::to_string(entry(to)) + "; // loop back-edge\n";
        return result + "active[" + std::to_string(entry(to)) + "] = 1;";
    }
    std::string fault(const std::string& code) {
        return "fault = " + code + "; response_valid = 1; pc = 0; booting = 0;";
    }
    std::string gep(GetElementPtrInst& g) {
        SmallMapVector<Value*, APInt, 4> variables;
        APInt constant(64, 0);
        if (!g.collectOffset(layout, 64, variables, constant))
            throw std::runtime_error("cannot resolve pointer offset");
        std::string result = "(" + value(g.getPointerOperand()) + " + 64'd" + std::to_string(constant.getZExtValue());
        for (auto& pair : variables)
            result += " + (64'($signed(" + value(pair.first) + ")) * 64'd" + std::to_string(pair.second.getZExtValue()) + ")";
        return result + ")";
    }
    std::string binary(BinaryOperator& b) {
        const char* op = nullptr;
        bool sign = false;
        switch (b.getOpcode()) {
        case Instruction::Add: op = "+"; break;
        case Instruction::Sub: op = "-"; break;
        case Instruction::Mul: op = "*"; break;
        case Instruction::And: op = "&"; break;
        case Instruction::Or: op = "|"; break;
        case Instruction::Xor: op = "^"; break;
        case Instruction::Shl: op = "<<"; break;
        case Instruction::LShr: op = ">>"; break;
        case Instruction::AShr: op = ">>>"; sign = true; break;
        case Instruction::UDiv: op = "/"; break;
        case Instruction::URem: op = "%"; break;
        case Instruction::SDiv: op = "/"; sign = true; break;
        case Instruction::SRem: op = "%"; sign = true; break;
        default: throw std::runtime_error("unsupported integer operation");
        }
        auto a = value(b.getOperand(0)), c = value(b.getOperand(1));
        if (sign) { a = "$signed(" + a + ")"; c = "$signed(" + c + ")"; }
        return "(" + a + " " + op + " " + c + ")";
    }
    std::string compare(ICmpInst& c) {
        const char* op = nullptr;
        switch (c.getPredicate()) {
        case CmpInst::ICMP_EQ: op = "=="; break;
        case CmpInst::ICMP_NE: op = "!="; break;
        case CmpInst::ICMP_ULT: case CmpInst::ICMP_SLT: op = "<"; break;
        case CmpInst::ICMP_ULE: case CmpInst::ICMP_SLE: op = "<="; break;
        case CmpInst::ICMP_UGT: case CmpInst::ICMP_SGT: op = ">"; break;
        case CmpInst::ICMP_UGE: case CmpInst::ICMP_SGE: op = ">="; break;
        default: throw std::runtime_error("unsupported comparison");
        }
        auto a = value(c.getOperand(0)), b = value(c.getOperand(1));
        if (c.isSigned()) { a = "$signed(" + a + ")"; b = "$signed(" + b + ")"; }
        return "(" + a + op + b + ")";
    }
    void instruction(Instruction& i) {
        body << "      if (fault == 0) begin // " << i.getOpcodeName() << "\n";
        if (isa<PHINode>(i)) {
            // Evaluate all incoming values before updating any loop-carried PHI.
            for (auto& p : i.getParent()->phis()) {
                body << "        case (pred)\n";
                for (unsigned n = 0; n < p.getNumIncomingValues(); ++n)
                    body << "          " << blocks.at(p.getIncomingBlock(n)) << ": phi_" << value(&p) << " = " << value(p.getIncomingValue(n)) << ";\n";
                body << "          default: begin " << fault("32'd4") << " end\n        endcase\n";
            }
            for (auto& p : i.getParent()->phis())
                body << "        " << value(&p) << " = phi_" << value(&p) << ";\n";
        } else if (auto* branch = dyn_cast<BranchInst>(&i)) {
            if (branch->isConditional()) body << "        if (" << value(branch->getCondition()) << ") begin " << jump(i.getParent(), branch->getSuccessor(0)) << " end else begin " << jump(i.getParent(), branch->getSuccessor(1)) << " end\n";
            else body << "        " << jump(i.getParent(), branch->getSuccessor(0)) << "\n";
        } else if (auto* sw = dyn_cast<SwitchInst>(&i)) {
            body << "        case (" << value(sw->getCondition()) << ")\n";
            for (auto item : sw->cases()) body << "          " << value(item.getCaseValue()) << ": begin " << jump(i.getParent(), item.getCaseSuccessor()) << " end\n";
            body << "          default: begin " << jump(i.getParent(), sw->getDefaultDest()) << " end\n        endcase\n";
        } else if (auto* ret = dyn_cast<ReturnInst>(&i)) {
            body << "        result = " << value(ret->getReturnValue()) << "; pc = 0;\n"
                    "        if (booting) booting = 0; else response_valid = 1;\n";
        } else if (isa<UnreachableInst>(i)) body << "        " << fault("32'd4") << "\n";
        else if (isa<LoadInst>(i) || isa<StoreInst>(i)) {
            bool store = isa<StoreInst>(i);
            auto* address = store ? cast<StoreInst>(i).getPointerOperand() : cast<LoadInst>(i).getPointerOperand();
            auto* type = store ? cast<StoreInst>(i).getValueOperand()->getType() : i.getType();
            if ((store && (cast<StoreInst>(i).isVolatile() || cast<StoreInst>(i).isAtomic())) ||
                (!store && (cast<LoadInst>(i).isVolatile() || cast<LoadInst>(i).isAtomic())))
                throw std::runtime_error("atomic/volatile accesses require an explicit protocol");
            unsigned count = (width(type) + 7) / 8;
            body << "        if (" << value(address) << " < 64'd16 || " << value(address) << " > 64'(MEM_BYTES - " << count << ")) begin " << fault("32'd3") << " end\n"
                    "        else if (SRAM) begin\n"
                    "          mem_addr = 32'(" << value(address) << "); mem_count = " << count << "; mem_index = 0; mem_acc = 0;\n"
                    "          mem_write = 1'b" << store << "; mem_store = 64'(" << (store ? value(cast<StoreInst>(i).getValueOperand()) : "64'd0") << "); mem_phase = 1;\n"
                    "          pc = " << states.at(i.getNextNode()) << ";\n        end else begin\n";
            // The full pointer has already passed the bounds check. Keeping
            // only the physical bit index avoids oversized variable selectors.
            std::string slice = "storage[BIT_INDEX_BITS'(" + value(address) + " << 3) +: " + std::to_string(count * 8) + "]";
            if (store) body << "          " << slice << " = " << count * 8 << "'(" << value(cast<StoreInst>(i).getValueOperand()) << ");\n";
            else body << "          " << value(&i) << " = " << width(type) << "'(" << slice << ");\n";
            body << "          active[" << states.at(i.getNextNode()) << "] = 1;\n        end\n";
        } else if (auto* call = dyn_cast<CallBase>(&i)) {
            auto name = call->getCalledFunction()->getName();
            if (name == "cpphdl_hls_fault") body << "        " << fault(value(call->getArgOperand(0))) << "\n";
            else if (name == "_ZSt20__throw_length_errorPKc" || name == "_ZSt17__throw_bad_allocv") body << "        " << fault("32'd1") << "\n";
            else if (auto* intr = dyn_cast<IntrinsicInst>(call)) {
                switch (intr->getIntrinsicID()) {
                case Intrinsic::assume:
                    body << "        if (!" << value(call->getArgOperand(0)) << ") begin " << fault("32'd4") << " end\n"; break;
                case Intrinsic::umax: case Intrinsic::umin: case Intrinsic::smax: case Intrinsic::smin: {
                    auto a = value(call->getArgOperand(0)), b = value(call->getArgOperand(1));
                    bool sign = intr->getIntrinsicID() == Intrinsic::smax || intr->getIntrinsicID() == Intrinsic::smin;
                    bool maximum = intr->getIntrinsicID() == Intrinsic::smax || intr->getIntrinsicID() == Intrinsic::umax;
                    body << "        " << value(&i) << " = ((" << (sign ? "$signed(" + a + ")" : a) << (maximum ? ">" : "<") << (sign ? "$signed(" + b + ")" : b) << ") ? " << a << " : " << b << ");\n"; break;
                }
                default: throw std::runtime_error("unsupported intrinsic: " + name.str());
                }
            } else throw std::runtime_error("external implementation missing: " + name.str());
        } else {
            std::string expression;
            if (auto* b = dyn_cast<BinaryOperator>(&i)) expression = binary(*b);
            else if (auto* c = dyn_cast<ICmpInst>(&i)) expression = compare(*c);
            else if (auto* g = dyn_cast<GetElementPtrInst>(&i)) expression = gep(*g);
            else if (auto* a = dyn_cast<AllocaInst>(&i)) expression = "64'd" + std::to_string(locals.at(a));
            else if (auto* s = dyn_cast<SelectInst>(&i)) expression = "(" + value(s->getCondition()) + " ? " + value(s->getTrueValue()) + " : " + value(s->getFalseValue()) + ")";
            else if (auto* cast = dyn_cast<CastInst>(&i)) {
                switch (cast->getOpcode()) {
                case Instruction::SExt: expression = "$signed(" + value(cast->getOperand(0)) + ")"; break;
                case Instruction::ZExt: case Instruction::Trunc: case Instruction::PtrToInt:
                case Instruction::IntToPtr: case Instruction::BitCast: expression = value(cast->getOperand(0)); break;
                default: throw std::runtime_error("unsupported cast");
                }
            } else throw std::runtime_error(std::string("unsupported instruction: ") + i.getOpcodeName());
            body << "        " << value(&i) << " = " << width(i.getType()) << "'(" << expression << ");\n";
        }
        body << "      end\n";
    }

    void schedule() {
        // Removing DFS back-edges leaves an acyclic graph. Each remaining path
        // executes in one cycle, including diamonds and joins, without cloning.
        std::map<BasicBlock*, unsigned> visited;
        std::function<void(BasicBlock*)> visit = [&](BasicBlock* b) {
            visited[b] = 1;
            for (auto* target : successors(b)) {
                if (visited[target] == 1) backEdges.insert({b, target});
                else if (!visited[target]) visit(target);
            }
            visited[b] = 2;
        };
        for (auto& b : root) if (!visited[&b]) visit(&b);
        std::vector<std::set<unsigned>> edges(regions.size());
        std::vector<unsigned> indegree(regions.size());
        for (unsigned n = 1; n < regions.size(); ++n) {
            auto* tail = regions[n].last;
            if (isa<LoadInst>(tail) || isa<StoreInst>(tail))
                edges[n].insert(states.at(tail->getNextNode()));
            else for (auto* target : successors(tail->getParent()))
                if (!backEdges.count({tail->getParent(), target}))
                    edges[n].insert(entry(target));
            for (unsigned to : edges[n]) ++indegree[to];
        }
        std::set<unsigned> ready;
        for (unsigned n = 1; n < regions.size(); ++n) if (!indegree[n]) ready.insert(n);
        while (!ready.empty()) {
            unsigned n = *ready.begin(); ready.erase(ready.begin());
            order.push_back(n);
            for (unsigned to : edges[n]) if (--indegree[to] == 0) ready.insert(to);
        }
        if (order.size() + 1 != regions.size()) throw std::runtime_error("cyclic combinational region graph");
    }

    std::pair<std::set<Value*>, std::set<Value*>> savedValues() {
        // Only values live across a loop edge or SRAM wait need registers.
        // PHI operands are simultaneous reads, including self/back-edge reads.
        using Values = std::set<Value*>;
        std::vector<Values> uses(regions.size()), defs(regions.size()), live(regions.size());
        std::vector<std::set<unsigned>> edges(regions.size());
        for (unsigned id = 1; id < regions.size(); ++id) {
            const auto& region = regions[id];
            auto use = [&](Value* v) {
                if ((isa<Instruction>(v) || isa<Argument>(v)) && !defs[id].count(v)) uses[id].insert(v);
            };
            if (isa<PHINode>(region.first)) {
                for (auto& phi : region.first->getParent()->phis())
                    for (Value* operand : phi.operands()) use(operand);
                for (auto& phi : region.first->getParent()->phis()) defs[id].insert(&phi);
            }
            for (auto* i = region.first;; i = i->getNextNode()) {
                if (!isa<PHINode>(i)) {
                    for (Value* operand : i->operands()) use(operand);
                    if (!i->getType()->isVoidTy()) defs[id].insert(i);
                }
                if (i == region.last) break;
            }
            if (isa<LoadInst>(region.last) || isa<StoreInst>(region.last))
                edges[id].insert(states.at(region.last->getNextNode()));
            else for (auto* target : successors(region.last->getParent())) edges[id].insert(entry(target));
        }
        bool changed;
        do {
            changed = false;
            for (unsigned id = regions.size() - 1; id != 0; --id) {
                Values incoming = uses[id];
                for (unsigned next : edges[id])
                    for (Value* v : live[next]) if (!defs[id].count(v)) incoming.insert(v);
                if (incoming != live[id]) { live[id] = std::move(incoming); changed = true; }
            }
        } while (changed);
        Values loops, memory;
        for (auto edge : backEdges) {
            const auto& incoming = live[entry(edge.second)];
            loops.insert(incoming.begin(), incoming.end());
        }
        for (unsigned id = 1; id < regions.size(); ++id) {
            auto* tail = regions[id].last;
            if (isa<LoadInst>(tail) || isa<StoreInst>(tail)) {
                const auto& incoming = live[states.at(tail->getNextNode())];
                memory.insert(incoming.begin(), incoming.end());
            }
        }
        return {loops, memory};
    }
public:
    Emitter(Function& f, uint64_t stateBytes) : root(f), layout(f.getDataLayout()), bytes(16 + stateBytes) {
        if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64)
            throw std::runtime_error("kernel backend currently requires little-endian 64-bit Clang layout");
        if (f.arg_size() != 4 || !f.getReturnType()->isIntegerTy(64) || !f.getArg(0)->getType()->isPointerTy())
            throw std::runtime_error("entry ABI must be uint64_t(State*, uint32_t, uint32_t, uint32_t)");
        for (unsigned n = 1; n < 4; ++n) if (!f.getArg(n)->getType()->isIntegerTy(32))
            throw std::runtime_error("entry command arguments must be uint32_t");
        unsigned n = 0;
        for (auto& arg : f.args()) names[&arg] = "v" + std::to_string(n++);
        for (auto& block : f) {
            blocks[&block] = blocks.size();
            unsigned region = regions.size();
            regions.push_back({&block.front(), nullptr, nullptr});
            for (auto& i : block) {
                states[&i] = region;
                regions[region].last = &i;
                if (!i.getType()->isVoidTy()) names[&i] = "v" + std::to_string(n++);
                if (auto* a = dyn_cast<AllocaInst>(&i)) {
                    auto* count = dyn_cast<ConstantInt>(a->getArraySize());
                    if (!count || count->getValue().getActiveBits() > 20) throw std::runtime_error("dynamic or oversized stack allocation");
                    uint64_t alignment = a->getAlign().value();
                    bytes = (bytes + alignment - 1) / alignment * alignment;
                    locals[a] = bytes;
                    bytes += count->getZExtValue() * layout.getTypeAllocSize(a->getAllocatedType());
                }
                if (isa<LoadInst>(i) || isa<StoreInst>(i)) {
                    region = regions.size();
                    regions.push_back({i.getNextNode(), nullptr, dyn_cast<LoadInst>(&i)});
                }
            }
        }
        bytes = (bytes + 7) / 8 * 8;
        if (bytes > 65536) throw std::runtime_error("kernel local memory exceeds 64 KiB experimental limit");
        schedule();
        auto saved = savedValues();
        auto declare = [&](const std::string& name, unsigned bits, bool persistent = true, bool sramOnly = false) {
            if (!persistent) {
                declarations << "  logic [" << bits - 1 << ":0] " << name << ";\n";
                defaults << "    " << name << " = 0;\n";
                return;
            }
            declarations << "  logic [" << bits - 1 << ":0] " << name << ", " << name << "_reg;\n";
            defaults << "    " << name << " = " << name << "_reg;\n";
            if (sramOnly) {
                sramCommits << "    " << name << "_reg <= " << name << ";\n";
                sramTieoffs << "    assign " << name << "_reg = '0;\n";
            } else commits << "    " << name << "_reg <= " << name << ";\n";
        };
        for (auto& arg : f.args()) declare(names.at(&arg), width(arg.getType()));
        for (auto& i : instructions(f)) if (!i.getType()->isVoidTy()) {
            bool acrossLoop = saved.first.count(&i), acrossMemory = saved.second.count(&i);
            declare(names.at(&i), width(i.getType()), acrossLoop || acrossMemory, !acrossLoop && acrossMemory);
            if (isa<PHINode>(i)) {
                declarations << "  logic [" << width(i.getType()) - 1 << ":0] phi_" << names.at(&i) << ";\n";
                defaults << "    phi_" << names.at(&i) << " = 0;\n";
            }
        }
        for (const char* name : {"pc", "pred", "fault", "mem_addr", "mem_count", "mem_index"}) declare(name, 32);
        for (const char* name : {"booting", "start_boot", "response_valid", "mem_write"}) declare(name, 1);
        for (const char* name : {"result", "mem_acc", "mem_store", "mem_word"}) declare(name, 64);
        declare("mem_phase", 3);
        for (unsigned id : order) {
            auto& region = regions[id];
            body << "    if (active[" << id << "] && fault == 0) begin // acyclic region\n";
            if (region.resumedLoad)
                body << "      if (SRAM) " << value(region.resumedLoad) << " = " << width(region.resumedLoad->getType()) << "'(mem_acc);\n";
            for (auto* i = region.first;; i = i->getNextNode()) {
                if (!isa<PHINode>(i) || i == &i->getParent()->front()) instruction(*i);
                if (i == region.last) break;
            }
            body << "    end\n";
        }
    }

    std::string emit() {
        std::ostringstream out;
        out << "// Grouped schedule from LLVM method bodies of " << root.getName().str() << ".\n"
R"SV(module ScheduledKernel #(parameter bit SRAM = 0) (
    input wire clk, reset,
    input wire command_valid_in,
    output wire command_ready_out,
    input wire [31:0] operation_in, index_in, value_in,
    output wire response_valid_out,
    input wire response_ready_in,
    output wire [63:0] result_out,
    output wire [31:0] fault_out,
    output wire memory_out__valid_out, memory_out__write_out,
    output wire [31:0] memory_out__addr_out,
    output wire [63:0] memory_out__data_out,
    input wire memory_out__ready_in, memory_out__valid_in,
    input wire [63:0] memory_out__data_in,
    output wire memory_out__ready_out
);
)SV";
        out << "  localparam int MEM_BYTES = " << bytes << ";\n"
               "  localparam int BIT_INDEX_BITS = $clog2(MEM_BYTES*8);\n"
               "  localparam int REGIONS = " << regions.size() - 1 << ";\n"
               "  // Only loop back-edges and external-memory waits cross clocks.\n"
            << declarations.str();
        out << R"SV(  logic [REGIONS:1] active;
  logic [MEM_BYTES*8-1:0] storage;
  wire [MEM_BYTES*8-1:0] storage_reg;
  wire [31:0] byte_address = mem_addr_reg + mem_index_reg;
  wire [31:0] lane = (byte_address & 7) * 8;
  wire [63:0] write_word = (mem_word_reg & ~(64'hff << lane)) |
                          (((mem_store_reg >> (mem_index_reg * 8)) & 64'hff) << lane);
  wire byte_done = mem_index_reg + 1 == mem_count_reg;
  assign command_ready_out = pc_reg == 0 && !booting_reg && !start_boot_reg && !response_valid_reg && fault_reg == 0;
  assign response_valid_out = response_valid_reg;
  assign result_out = result_reg;
  assign fault_out = fault_reg;
  assign memory_out__valid_out = SRAM && (mem_phase_reg == 1 || mem_phase_reg == 3);
  assign memory_out__write_out = mem_phase_reg == 3;
  assign memory_out__addr_out = byte_address >> 3;
  assign memory_out__data_out = write_word;
  assign memory_out__ready_out = SRAM && (mem_phase_reg == 2 || mem_phase_reg == 4);
  generate if (!SRAM) begin: registers
    logic [MEM_BYTES*8-1:0] contents;
    assign storage_reg = contents;
    always_ff @(posedge clk) begin
      if (!reset) contents <= storage;
    end
  end else begin: external_memory
    assign storage_reg = '0;
  end endgenerate
  always_comb begin
)SV";
        out << defaults.str();
        out << R"SV(    active = '0;
    storage = storage_reg;
    if (reset) begin
      pc = 0; pred = 0; booting = 1; start_boot = 1;
      response_valid = 0; result = 0; fault = 0;
      mem_phase = 0; mem_addr = 0; mem_count = 0; mem_index = 0;
      mem_acc = 0; mem_store = 0; mem_word = 0; mem_write = 0;
    end else if (mem_phase_reg != 0) begin
      case (mem_phase_reg)
      1: if (memory_out__ready_in) mem_phase = 2;
      2: if (memory_out__valid_in) begin
        if (mem_write_reg) begin mem_word = memory_out__data_in; mem_phase = 3; end
        else begin
          mem_acc = mem_acc_reg | (((memory_out__data_in >> lane) & 64'hff) << (mem_index_reg*8));
          if (byte_done) mem_phase = 0;
          else begin mem_index = mem_index_reg + 1; mem_phase = 1; end
        end
      end
      3: if (memory_out__ready_in) mem_phase = 4;
      4: if (memory_out__valid_in) begin
        if (byte_done) mem_phase = 0;
        else begin mem_index = mem_index_reg + 1; mem_phase = 1; end
      end
      default: begin mem_phase = 0; fault = 4; response_valid = 1; pc = 0; end
      endcase
    end else if (fault_reg == 0) begin
      if (pc_reg == 0) begin
        if (response_valid_reg && response_ready_in) response_valid = 0;
)SV";
        out << "        if (start_boot_reg || (command_valid_in && command_ready_out)) begin\n"
               "          start_boot = 0; active[" << entry(&root.getEntryBlock()) << "] = 1;\n"
               "          v0 = 64'd16; v1 = start_boot_reg ? 32'hffffffff : operation_in;\n"
               "          v2 = index_in; v3 = value_in;\n        end\n"
               "      end else begin\n        case (pc_reg)\n";
        std::set<unsigned> loopEntries, memoryEntries;
        for (auto edge : backEdges) loopEntries.insert(entry(edge.second));
        for (unsigned id = 1; id < regions.size(); ++id) {
            auto* tail = regions[id].last;
            if (isa<LoadInst>(tail) || isa<StoreInst>(tail))
                memoryEntries.insert(states.at(tail->getNextNode()));
        }
        for (unsigned id = 1; id < regions.size(); ++id) {
            if (loopEntries.count(id))
                out << "          " << id << ": active[" << id << "] = 1;\n";
            else if (memoryEntries.count(id))
                out << "          " << id << ": begin if (SRAM) active[" << id << "] = 1; else begin "
                    << fault("32'd4") << " end end\n";
        }
        out << "          default: begin " << fault("32'd4") << " end\n"
               "        endcase\n      end\n"
               "    end else if (response_valid_reg && response_ready_in) response_valid = 0;\n"
            << body.str()
            << "  end\n  always_ff @(posedge clk) begin\n" << commits.str()
            << "  end\n";
        if (!sramCommits.str().empty())
            out << "  generate if (SRAM) begin: memory_continuations\n"
                   "    always_ff @(posedge clk) begin\n" << sramCommits.str()
                << "    end\n  end else begin: no_memory_continuations\n"
                << sramTieoffs.str() << "  end endgenerate\n";
        out << "endmodule\n";
        return out.str();
    }
};

class Action : public clang::EmitLLVMOnlyAction {
    std::string entryName, directory;
    bool& success;
public:
    Action(const std::string& entry, const std::string& dir, bool& result)
        : entryName(entry), directory(dir), success(result) {}
    void EndSourceFileAction() override {
        clang::EmitLLVMOnlyAction::EndSourceFileAction();
        auto module = takeModule();
        if (!module) return;
        try {
            std::filesystem::create_directories(directory);
            auto* root = module->getFunction(entryName);
            auto* size = module->getGlobalVariable("cpphdl_hls_state_bytes");
            auto* number = size && size->hasInitializer() ? dyn_cast<ConstantInt>(size->getInitializer()) : nullptr;
            auto* alignment = module->getGlobalVariable("cpphdl_hls_state_align");
            auto* alignNumber = alignment && alignment->hasInitializer() ? dyn_cast<ConstantInt>(alignment->getInitializer()) : nullptr;
            if (!root || root->isDeclaration() || !number || number->getValue().getActiveBits() > 16)
                throw std::runtime_error("missing entry definition or bounded cpphdl_hls_state_bytes constant");
            if (!alignNumber || !alignNumber->getValue().isPowerOf2() || alignNumber->getValue().ugt(16))
                throw std::runtime_error("cpphdl_hls_state_align must be a power of two up to 16");
            std::error_code error;
            raw_fd_ostream before(directory + "/kernel-source.ll", error);
            if (error) throw std::runtime_error(error.message());
            module->print(before, nullptr);
            normalize(*root);
            raw_fd_ostream lowered(directory + "/kernel-lowered.ll", error);
            if (error) throw std::runtime_error(error.message());
            module->print(lowered, nullptr);
            Emitter emitter(*root, number->getZExtValue());
            auto rtl = emitter.emit();
            std::ofstream output(directory + "/ScheduledKernel.sv");
            output << rtl;
            if (!output) throw std::runtime_error("cannot write scheduled RTL");
            success = true;
            errs() << "HLS: translated actual method bodies into " << directory << "/ScheduledKernel.sv\n";
        } catch (const std::exception& error) {
            std::filesystem::remove(directory + "/ScheduledKernel.sv");
            errs() << "HLS kernel error: " << error.what() << "\n";
        }
    }
};
}

int compileKernel(clang::tooling::ClangTool& tool, const std::string& entry,
                  const std::string& directory) {
    std::error_code removeError;
    std::filesystem::remove(directory + "/ScheduledKernel.sv", removeError);
    bool success = false;
    class Factory : public clang::tooling::FrontendActionFactory {
        const std::string& entry; const std::string& directory; bool& success;
    public:
        Factory(const std::string& e, const std::string& d, bool& s) : entry(e), directory(d), success(s) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<Action>(entry, directory, success);
        }
    } factory(entry, directory, success);
    tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
        {"-O1", "-fno-exceptions", "-fno-rtti", "-fno-vectorize", "-fno-slp-vectorize", "-DCPPHDL_HLS_COMPILING"},
        clang::tooling::ArgumentInsertPosition::END));
    int result = tool.run(&factory);
    return result ? result : success ? 0 : 1;
}
}

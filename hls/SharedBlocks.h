#pragma once
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cpphdl::hls {

struct ScheduledBlock {
    std::vector<std::string> statements;
    std::string condition, source, name, method;
    int yes = -1, no = -1;
    bool suspend = false;
    bool combinational = false;
    bool memoryBoundary = false;
    bool recursiveBoundary = false;
    bool sharedCallBoundary = false;
};

struct BlockSymbol {
    unsigned width;
    std::string name;
    bool writable;
};

struct SharedBlock {
    ScheduledBlock body;
    std::vector<BlockSymbol> parameters;
    unsigned uses = 0;
    std::vector<std::string> stateArguments;
    std::set<std::string> readOnlyState;
};

struct BlockCall {
    std::size_t function;
    std::vector<std::string> arguments;
};

struct SharedBlocks {
    std::vector<SharedBlock> functions;
    std::vector<BlockCall> calls;
};

class BlockSharing {
    const std::map<std::string, BlockSymbol>& symbols;
    std::map<std::string, std::size_t> keys;
public:
    std::vector<SharedBlock> functions;
    explicit BlockSharing(const std::map<std::string, BlockSymbol>& symbols) : symbols(symbols) {}
    BlockCall add(const ScheduledBlock& block, const std::set<std::string>* liveOutputs = nullptr);
    const std::map<std::string, BlockSymbol>& symbolTable() const { return symbols; }
};

// Only registered scheduler symbols become parameters. Literals, operations,
// widths, alias relationships, and clock boundaries remain part of the key.
SharedBlocks shareBlocks(const std::vector<ScheduledBlock>& blocks,
                         const std::map<std::string, BlockSymbol>& symbols);

// Remove unused common state from signatures and nested calls, bottom-up.
// In particular a scalar helper must not copy the entire storage array.
void pruneFunctionState(SharedBlocks& shared, const std::vector<std::string>& storage = {"storage"});

// Updated values are returned together; no function output/inout arguments.
std::vector<size_t> functionOutputs(const SharedBlock& function);
std::string valueFunctionCall(const SharedBlock& function, const std::vector<std::string>& arguments);
void lowerValueFunctionCalls(SharedBlocks& shared);

// Scratch holds one direct callee's result at a time, not all design results.
size_t callResultScratchWidth(const ScheduledBlock& body,
                             const std::map<std::string, size_t>& resultWidths);

}

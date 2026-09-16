#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace cpphdl::hls {

struct ScheduledBlock {
    std::vector<std::string> statements;
    std::string condition, source, name, method;
    int yes = -1, no = -1;
    bool suspend = false;
    bool combinational = false;
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
    BlockCall add(const ScheduledBlock& block);
    const std::map<std::string, BlockSymbol>& symbolTable() const { return symbols; }
};

// Only registered scheduler symbols become parameters. Literals, operations,
// widths, alias relationships, and clock boundaries remain part of the key.
SharedBlocks shareBlocks(const std::vector<ScheduledBlock>& blocks,
                         const std::map<std::string, BlockSymbol>& symbols);

}

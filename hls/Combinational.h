#pragma once
#include "SharedBlocks.h"
#include <set>

namespace cpphdl::hls {

class BlockUses {
    const std::map<std::string, BlockSymbol>& symbols;
    std::map<std::string, std::set<int>> users;
    std::vector<std::set<std::string>> names;
public:
    BlockUses(const std::vector<ScheduledBlock>& blocks, const std::map<std::string, BlockSymbol>& symbols);
    void update(int index, const ScheduledBlock& block);
    std::set<std::string> outside(const std::map<int, std::set<int>>& region, int exit) const;
};

// Outline a single-entry call only when every path reaches exit without a clock.
bool outlineCall(std::vector<ScheduledBlock>& blocks, int entry, int exit,
                 const std::string& callerMethod, BlockSharing& sharing,
                 const std::set<std::string>& preserved = {}, BlockUses* uses = nullptr);
void coalesceBlocks(std::vector<ScheduledBlock>& blocks, int resetEntry, int commandEntry);

}

#pragma once
#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

namespace cpphdl::hls {

// Logical byte addresses are unchanged. Each state value holds every eighth
// byte, so a write does not shift or select the complete address space.
inline std::vector<std::string> storageBankNames() {
    std::vector<std::string> names;
    for (unsigned i = 0; i < 8; ++i) names.push_back("storage_lane" + std::to_string(i));
    return names;
}

inline std::string storageBankArguments() {
    std::string text;
    for (const auto& name : storageBankNames()) {
        if (!text.empty()) text += ", ";
        text += name;
    }
    return text;
}

inline void emitBankLoad(std::ostream& out, unsigned count) {
    unsigned groups = (count + 7) / 8;
    out << "  function static logic [" << count * 8 - 1 << ":0] hls_storage_load_" << count * 8 << "(\n";
    for (const auto& name : storageBankNames()) out << "    input bank_t " << name << ",\n";
    out << "    input logic [ADDR_BITS-1:0] address);\n";
    for (unsigned group = 0; group < groups; ++group) {
        // Rotating only this eight-byte window avoids a whole-arena selector.
        std::string word = "{";
        for (unsigned bank = 8; bank-- > 0;) {
            if (bank != 7) word += ", ";
            word += "storage_lane" + std::to_string(bank) + "[BANK_INDEX_BITS'((address >> 3) + " +
                std::to_string(group) + " + (address[2:0] > 3'd" + std::to_string(bank) + "))]";
        }
        word += "}";
        unsigned width = std::min(8u, count - group * 8) * 8;
        out << "    hls_storage_load_" << count * 8 << "[" << group * 64 << " +: " << width << "] = "
            << width << "'({" << word << ", " << word << "} >> {address[2:0], 3'b000});\n";
    }
    out << "  endfunction\n";
}

inline void emitBankWrite(std::ostream& out, unsigned count, unsigned bank, bool port = false) {
    auto name = "hls_bank_write_" + std::string(port ? "port" : std::to_string(count * 8)) + "_" + std::to_string(bank);
    out << "  function static bank_t " << name << "(input bank_t old_bank,\n"
        << "    input logic [ADDR_BITS-1:0] address, input logic [" << count * 8 - 1
        << ":0] value, input logic [31:0] fault" << (port ? ", input int unsigned valid_bytes" : "") << ");\n"
        << "    " << name << " = old_bank;\n";
    for (unsigned group = 0; group < (count + 7) / 8; ++group) {
        std::string lane = "3'(3'd" + std::to_string(bank) + " - address[2:0])";
        std::string offset = "(32'(" + lane + ") + " + std::to_string(group * 8) + ")";
        out << "    if (fault == 0";
        if (port) out << " && " << offset << " < valid_bytes";
        else if (group * 8 + 8 > count) out << " && " << offset << " < " << count;
        out << ") " << name << "[BANK_INDEX_BITS'((address >> 3) + " << group
            << " + (address[2:0] > 3'd" << bank << "))] = 8'(value >> (" << offset << " * 8));\n";
    }
    out << "  endfunction\n";
}
}

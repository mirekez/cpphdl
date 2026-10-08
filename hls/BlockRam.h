#pragma once
#include <ostream>
#include <string>

namespace cpphdl::hls {

inline void emitBlockRamDeclarations(std::ostream& out) {
    for (unsigned lane = 0; lane < 8; ++lane) {
        out << "  wire [7:0] hls_ram_read_lane" << lane << ";\n";
    }
    out << "  logic [2:0] hls_ram_read_offset;\n"
        << "  wire [63:0] hls_ram_read_word = {";
    for (int lane = 7; lane >= 0; --lane) {
        if (lane != 7) out << ", ";
        out << "hls_ram_read_lane" << lane;
    }
    out << "};\n  assign memory_read_data = 64'({2{hls_ram_read_word}} >> {hls_ram_read_offset, 3'b000});\n";
}

inline void emitBlockRamPorts(std::ostream& out, const std::string& moduleName, const std::string& address,
                             const std::string& data, const std::string& size,
                             const std::string& read, const std::string& write,
                             const std::string& fault) {
    // A scheduled memory access is either a read or a write. Explicitly gate
    // the enables so no_rw_check never discards observable collision behavior.
    out << "  wire hls_ram_write_enable = !reset && " << fault << " == 0 && " << write << ";\n"
        << "  wire hls_ram_read_enable = !reset && " << fault << " == 0 && " << read
        << " && !hls_ram_write_enable;\n";
    for (unsigned lane = 0; lane < 8; ++lane) {
        out << "  wire [BANK_INDEX_BITS-1:0] hls_ram_row_" << lane << " = BANK_INDEX_BITS'((" << address
            << " >> 3) + (" << address << "[2:0] > 3'd" << lane << "));\n"
            << "  wire [2:0] hls_ram_byte_" << lane << " = 3'd" << lane << " - " << address << "[2:0];\n"
            << "  wire hls_ram_write_lane" << lane << " = hls_ram_write_enable && 32'(hls_ram_byte_"
            << lane << ") < " << size << ";\n"
            << "  wire [7:0] hls_ram_write_data" << lane << " = 8'(" << data << " >> {hls_ram_byte_"
            << lane << ", 3'b000});\n"
            << "  " << moduleName << "__HlsRamByte #(.DEPTH(BANK_BYTES)) storage_lane" << lane << "_ram (\n"
            << "    .clk(clk), .address(hls_ram_row_" << lane << "), .read_enable(hls_ram_read_enable),\n"
            << "    .write_enable(hls_ram_write_lane" << lane << "), .write_data(hls_ram_write_data" << lane << "),\n"
            << "    .read_data(hls_ram_read_lane" << lane << "));\n";
    }
}

inline void emitBlockRamTick(std::ostream& out, const std::string& address) {
    // Register raw bank reads first, then align combinationally. Registering a
    // mux of asynchronous reads would prevent synchronous block-RAM inference.
    out << "    if (hls_ram_read_enable) hls_ram_read_offset <= " << address << "[2:0];\n";
}

inline void emitBlockRamModule(std::ostream& out, const std::string& moduleName) {
    // Infer the RAM before flattening to keep memory collision analysis local.
    out << "\nmodule " << moduleName << R"SV(__HlsRamByte #(parameter int DEPTH = 2) (
    input wire clk, read_enable, write_enable,
    input wire [$clog2(DEPTH)-1:0] address,
    input wire [7:0] write_data,
    output logic [7:0] read_data
);
  (* ram_style = "block", no_rw_check *) logic [7:0] data [0:DEPTH-1];
  always_ff @(posedge clk) begin
    if (read_enable) read_data <= data[address];
    if (write_enable) data[address] <= write_data;
  end
endmodule
)SV";
}
}

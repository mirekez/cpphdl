package TypedefRamTypes;
    typedef logic [127:0] word_t;
endpackage

module TypedefRam #(
    parameter int AddrBits = 9,
    parameter int Words = 1,
    parameter int Depth = 1 << AddrBits
) (
    input logic clk,
    input logic cs,
    input logic we,
    input logic [AddrBits-1:0] addr,
    input logic [Words-1:0][127:0] wdata,
    input logic [Words-1:0][15:0] wbyteenable,
    output logic [Words-1:0][127:0] rdata
);
`ifdef PACKAGE_ROW
    typedef TypedefRamTypes::word_t [Words-1:0] row_t;
`else
    typedef logic [Words-1:0][127:0] row_t;
`endif
    typedef row_t mem_t [Depth];
    typedef mem_t storage_t;
    storage_t mem;
    always_ff @(posedge clk) begin
        if (cs) begin
            if (we) begin
`ifdef FULL_WRITE
                if (wbyteenable[0][0]) mem[addr] <= wdata;
`else
                for (int word_index = 0; word_index < Words; word_index++)
                    for (int byte_index = 0; byte_index < 16; byte_index++)
                        if (wbyteenable[word_index][byte_index])
                            mem[addr][word_index][byte_index * 8 +: 8] <=
                                wdata[word_index][byte_index * 8 +: 8];
`endif
            end else rdata <= mem[addr];
        end
    end
endmodule

module OddWidthRam (
    input logic clk,
    input logic cs,
    input logic we,
    input logic [3:0] addr,
    input logic [0:0][12:0] wdata,
    output logic [0:0][12:0] rdata
);
    typedef logic [0:0][12:0] mem_t [16];
    mem_t mem;
    always_ff @(posedge clk)
        if (cs) begin
            if (we) mem[addr] <= wdata;
            else rdata <= mem[addr];
        end
endmodule

module PackedBank (
    input logic clk,
    input logic we,
    input logic [3:0] addr,
    input logic [127:0] wdata,
    output logic [127:0] rdata
);
    typedef logic [15:0][127:0] bank_t;
    bank_t bank;
    always_ff @(posedge clk)
        if (we) bank[addr] <= wdata;
    assign rdata = bank[addr];
endmodule

module ResetBank (
    input logic clk,
    input logic rst_n,
    input logic we,
    input logic [3:0] addr,
    input logic [12:0] wdata,
    output logic [12:0] rdata
);
    typedef logic [12:0] bank_t [16];
    bank_t bank;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) bank <= '0;
        else if (we) bank[addr] <= wdata;
    assign rdata = bank[addr];
endmodule

module StructBank (
    input logic clk,
    input logic we,
    input logic [3:0] addr,
    input logic [12:0] wdata,
    output logic [12:0] rdata
);
    typedef struct packed {
        logic [12:0] payload;
    } entry_t;
    typedef entry_t bank_t [16];
    bank_t bank;
    always_ff @(posedge clk)
        if (we) bank[addr].payload <= wdata;
    assign rdata = bank[addr].payload;
endmodule

module ArrayReadBank (
    input logic clk,
    input logic we,
    input logic [3:0] addr,
    input logic [12:0] wdata,
    output logic [12:0] rdata [16]
);
    typedef logic [12:0] bank_t [16];
    bank_t bank;
    always_ff @(posedge clk)
        if (we) bank[addr] <= wdata;
    assign rdata = bank;
endmodule

module AsyncIndexBank (
    input logic clk,
    input logic rst_n,
    input logic we,
    input logic [3:0] addr,
    input logic [12:0] wdata,
    output logic [12:0] rdata
);
    typedef logic [12:0] bank_t [16];
    bank_t bank;
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) begin
            for (int index = 0; index < 16; index++) bank[index] <= '0;
        end else if (we) bank[addr] <= wdata;
    assign rdata = bank[addr];
endmodule

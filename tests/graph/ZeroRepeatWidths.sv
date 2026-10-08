module ZeroRepeatWidths #(
    parameter int XLEN = 32,
    parameter int VLEN = 32
) (
    input  logic [VLEN-1:0] addr_i,
    output logic [XLEN-1:0] result_o,
    output logic [XLEN-1:0] tail_o,
    output logic [XLEN+7:0] middle_o,
    output logic [XLEN-1:0] nested_o,
    output logic [XLEN+39:0] wide_o
);
    assign result_o = {{XLEN-VLEN{addr_i[VLEN-1]}}, addr_i};
    assign tail_o = {addr_i, {XLEN-VLEN{1'b1}}};
    assign middle_o = {8'ha5, {XLEN-VLEN{addr_i[VLEN-1]}}, addr_i};
    assign nested_o = {{{0{2'b11}}, {0{addr_i[1:0]}}, addr_i}, {XLEN-VLEN{1'b1}}};
    assign wide_o = {8'ha5, {XLEN-VLEN{addr_i[VLEN-1]}}, addr_i, 32'h89abcdef};
endmodule

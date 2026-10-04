`define NESTED_REPLY struct packed { \
    struct packed { logic all; logic [WIDTH-1:0] idx; } inv; \
    logic [3:0] tail; \
}

`ifdef NESTED_PACKAGE
package NestedPackedTypes;
    localparam int WIDTH = 8;
    typedef `NESTED_REPLY reply_t;
endpackage
`endif

module NestedPacked #(
    parameter int WIDTH = 8
`ifdef NESTED_HEADER
    , localparam type reply_t = `NESTED_REPLY
`endif
`ifdef NESTED_DEFAULT
    , parameter type reply_t = `NESTED_REPLY
`endif
) (
    input logic all_i,
    input logic [WIDTH-1:0] idx_i,
    input logic [WIDTH+4:0] packed_i,
    output logic [WIDTH+4:0] packed_o,
    output logic [31:0] bits_o,
    output logic all_o,
    output logic [WIDTH-1:0] idx_o,
    output logic [3:0] tail_o
);
`ifdef NESTED_LOCAL
    localparam type reply_t = `NESTED_REPLY;
`elsif NESTED_PACKAGE
    typedef NestedPackedTypes::reply_t reply_t;
`elsif NESTED_HEADER
`elsif NESTED_DEFAULT
`elsif NESTED_ANONYMOUS
`else
    typedef `NESTED_REPLY reply_t;
`endif
`ifdef NESTED_ANONYMOUS
    `NESTED_REPLY reply, decoded;
`else
    reply_t reply, decoded;
`endif
    always_comb begin
        reply = '0;
        reply.inv.all = all_i;
        reply.inv.idx = idx_i;
        reply.tail = 4'ha;
        decoded = packed_i;
    end
    assign packed_o = reply;
    assign bits_o = $bits(reply);
    assign all_o = decoded.inv.all;
    assign idx_o = decoded.inv.idx;
    assign tail_o = decoded.tail;
endmodule
`undef NESTED_REPLY

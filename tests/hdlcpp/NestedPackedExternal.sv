`ifdef NESTED_PACKAGE
package NestedPackedExternalTypes;
    typedef struct packed {
        struct packed { logic [15:8] window; } inner;
        logic flag;
    } packet_t;
endpackage
module NestedPackedExternalDeclaration;
endmodule
`else
module NestedPackedExternal (
    input NestedPackedExternalTypes::packet_t packet_i,
    input logic [2:0] index_i,
    output logic bit_o
);
    assign bit_o = packet_i.inner.window[{1'b1, index_i}];
endmodule
`endif

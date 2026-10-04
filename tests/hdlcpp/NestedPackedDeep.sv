module NestedPackedDeep #(parameter int WIDTH = 4) (
    input logic [4*WIDTH+14:0] raw_i,
    input logic [3:0] hi_i,
    output logic [4*WIDTH+14:0] packed_o,
    output logic [WIDTH-1:0] field_o,
    output logic zero_o,
    output logic [31:0] width_o
);
    typedef struct packed {
        logic [2:0] head;
        struct packed {
            struct packed {
                logic [WIDTH-1:0] hi;
                logic [1:0] lo;
            } [3:2] pair;
            logic flag;
        } left, right;
        logic [1:0] tail;
    } packet_t;
    packet_t packet, empty;
    logic [3:0] hi;
    assign hi = hi_i;
    always_comb begin
        packet = raw_i;
        packet.left.pair[3].hi = packet.left.pair[3].hi ^ hi;
        packet.left.pair[3].hi[0] = ~packet.left.pair[3].hi[0];
        empty = '0;
    end
    assign packed_o = packet;
    assign field_o = packet.left.pair[3].hi;
    assign zero_o = empty.left.flag;
    assign width_o = $bits(packet_t);
endmodule

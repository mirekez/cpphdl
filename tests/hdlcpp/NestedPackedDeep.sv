module NestedPackedDeep #(
    parameter int WIDTH = 4,
    parameter type pair_t = logic [1:0][3:0]
) (
    input logic [4*WIDTH+14:0] raw_i,
    input logic [3:0] hi_i,
    output logic [4*WIDTH+14:0] packed_o,
    output logic [WIDTH-1:0] field_o,
    output logic zero_o,
    output logic [31:0] width_o,
    output logic [7:0] lanes_o,
    output logic [3:0] selected_lane_o,
    output logic [1:0] lane_bits_o,
    output logic [7:0] repeated_lane_o
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
    typedef struct packed {
        logic [1:0][3:0] lanes;
        logic [7:0] tag;
    } byte_packet_t;
    byte_packet_t bytes;
    pair_t typed_lanes;
    logic [3:0] hi;
    assign hi = hi_i;
    always_comb begin
        packet = raw_i;
        packet.left.pair[3].hi = packet.left.pair[3].hi ^ hi;
        packet.left.pair[3].hi[0] = ~packet.left.pair[3].hi[0];
        empty = '0;
        bytes = raw_i[15:0];
        typed_lanes = raw_i[15:8];
    end
    assign packed_o = packet;
    assign field_o = packet.left.pair[3].hi;
    assign zero_o = empty.left.flag;
    assign width_o = $bits(packet_t);
    // Array element selects are four bits; subsequent vector bit selects are
    // one bit. Check both constant and runtime element positions in concatenation.
    assign lanes_o = {bytes.lanes[0], bytes.lanes[1]};
    assign selected_lane_o = {bytes.lanes[hi_i[0]]};
    assign lane_bits_o = {bytes.lanes[0][1], bytes.lanes[1][2]};
    assign repeated_lane_o = {2{typed_lanes[hi_i[0]]}};
endmodule

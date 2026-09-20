module packed_struct_array_expr (
    input logic [87:0] left_i,
    input logic [87:0] right_i,
    output logic [87:0] merged_o,
    output logic [43:0] narrow_o
);
    typedef struct packed {
        logic [4:0] flags;
        logic [5:0] tag;
    } entry_t;
    entry_t [7:0] left_entries, right_entries, merged;
    entry_t [3:0] narrow;
    assign left_entries = left_i;
    assign right_entries = right_i;
    assign merged = left_entries | right_entries;
    always_comb narrow = left_entries[3:0] | right_entries[3:0];
    assign merged_o = merged;
    assign narrow_o = narrow;
endmodule

module ArrayWriteBounds #(
    parameter int COUNT = 8,
    parameter int LOWER = 0,
    parameter bit ZERO_ID = 0
) (
    input logic clk_i,
    input logic rst_ni,
    input logic [7:0] trans_id_i,
    input logic [63:0] index_i,
    input logic wt_valid_i,
    input logic value_i,
    input logic [5*COUNT-1:0] raw_i,
    output logic [5*COUNT-1:0] result_o,
    output logic [5*COUNT-1:0] direct_o,
    output logic [5*COUNT-1:0] cast_o,
    output logic [5*COUNT-1:0] wrap_o,
    output logic [5*COUNT-1:0] registered_o
);
    typedef struct packed {
        logic valid;
        logic is_double_rd_macro_instr;
        logic is_macro_instr;
        logic is_last_macro_instr;
    } scoreboard_entry_t;
    typedef struct packed {
        logic issued;
        scoreboard_entry_t sbe;
    } sb_mem_t;
`ifdef BOUNDS_UNPACKED
    sb_mem_t mem_q [6:4], mem_n [6:4], direct [6:4], casted [6:4], wrapped [6:4];
    for (genvar entry = 0; entry < COUNT; entry++) begin
        assign mem_q[entry+LOWER] = raw_i[5*entry +: 5];
        assign result_o[5*entry +: 5] = mem_n[entry+LOWER];
        assign direct_o[5*entry +: 5] = direct[entry+LOWER];
        assign cast_o[5*entry +: 5] = casted[entry+LOWER];
        assign wrap_o[5*entry +: 5] = wrapped[entry+LOWER];
    end
`else
    sb_mem_t [LOWER+COUNT-1:LOWER] mem_q, mem_n, direct, casted, wrapped;
    assign mem_q = raw_i;
    assign result_o = mem_n;
    assign direct_o = direct;
    assign cast_o = casted;
    assign wrap_o = wrapped;
`endif
    sb_mem_t [COUNT-1:0] registered_value;
    assign registered_o = registered_value;
    wire [7:0] trans_id = ZERO_ID ? 8'(LOWER) : 8'(LOWER + trans_id_i);
    wire [4:0] pattern = value_i ? raw_i[4:0] : ~raw_i[4:0];
    always_comb begin
        mem_n = mem_q;
        if (wt_valid_i && mem_q[trans_id].issued) begin
            if (mem_q[trans_id].sbe.is_double_rd_macro_instr && mem_q[trans_id].sbe.is_macro_instr) begin
                if (mem_q[trans_id].sbe.is_last_macro_instr) begin
                    mem_n[trans_id].sbe.valid = 1'b1;
                    mem_n[8'(trans_id)-1].sbe.valid = 1'b1;
                end
            end
        end
    end
    always_comb begin
        direct = mem_q;
`ifdef BOUNDS_RTL_SAFE_INDEX
        if (wt_valid_i && index_i >= 64'(LOWER) && index_i < 64'(LOWER+COUNT))
            direct[32'(index_i)].sbe.valid = value_i;
`else
        if (wt_valid_i) direct[index_i].sbe.valid = ZERO_ID ? (value_i ? 1'b1 : 1'b0) : value_i;
`endif
    end
    always_comb begin
        casted = mem_q;
`ifdef BOUNDS_RTL_SAFE_INDEX
        if (wt_valid_i && index_i >= 64'(LOWER) && index_i < 64'(LOWER+COUNT))
            casted[32'(index_i)] = sb_mem_t'(value_i ? raw_i[4:0] : ~raw_i[4:0]);
`else
        if (wt_valid_i) begin
            if (ZERO_ID) casted[index_i] = '{pattern[4], '{pattern[3], pattern[2], pattern[1], pattern[0]}};
            else if (value_i) casted[index_i] = sb_mem_t'(raw_i[4:0]);
            else casted[index_i] = ~raw_i[4:0];
        end
`endif
    end
    always_comb begin
        wrapped = mem_q;
        if (wt_valid_i) wrapped[32'(index_i) + 32'd1].sbe.valid = value_i;
    end
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int entry = 0; entry < COUNT; entry++) registered_value[entry] <= '0;
        end
`ifdef BOUNDS_RTL_SAFE_INDEX
        else if (wt_valid_i && index_i >= 64'(LOWER) && index_i < 64'(LOWER+COUNT))
            registered_value[32'(index_i) - LOWER].sbe.valid <= value_i;
`else
        else if (wt_valid_i) registered_value[index_i - LOWER].sbe.valid <= value_i;
`endif
    end
endmodule

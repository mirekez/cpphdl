module NativeNarrow(input logic signed [7:0] wide, output logic signed [3:0] narrow);
    assign narrow = wide;
endmodule

module NativeGraph (
    input logic clk, reset_n, enable,
    input logic [2:0] index,
    input logic [1:0] mode,
    input logic [6:0] shift,
    input logic [4:0][31:0] inputs,
    output logic [4:0][31:0] bank_out,
    output logic [31:0] count_out, selected_out, mixed_out,
    output logic signed [8:0] shifted,
    output logic [4:0] equal_flags,
    output logic [11:0] extended,
    output logic [3:0] reversed,
    output logic [7:0] unpacked_select
);
    typedef struct packed {
        logic [6:0] tag;
        logic signed [8:0] amount;
        logic [15:0] payload;
    } word_t;
    word_t [4:0] words;
    word_t selected;
    logic [4:0][31:0] bank;
    logic [31:0] count;
    logic [0:7] ascending;
    logic [7:0] unpacked_values [2:6];
    assign words = inputs;
    assign selected = words[index];
    assign selected_out = selected;
    assign shifted = selected.amount >>> shift;
    assign bank_out = bank;
    assign count_out = count;
    assign ascending = selected[7:0];
    assign reversed = {ascending[7],ascending[6],ascending[5],ascending[4]};
    assign unpacked_select = unpacked_values[index];
    NativeNarrow narrow_instance (.wide(selected[7:0]), .narrow(extended));

    function automatic logic [31:0] transform(input logic [31:0] value);
        transform = {value[7:0], value[31:8]};
        transform[15:8] ^= 8'h5a;
    endfunction

    always_comb begin
        case (mode)
            0: mixed_out = transform(selected);
            1: mixed_out = selected ^ 32'h01234567;
            2: begin
                if (enable) mixed_out = selected + 32'd9;
                else mixed_out = selected - 32'd11;
            end
            default: mixed_out = '0;
        endcase
        for (int offset = 0; offset < 4; offset++) begin
            mixed_out[offset * 8 +: 3] = selected.payload[offset * 3 +: 3];
        end
    end

    for (genvar lane = 0; lane < 5; lane++) begin
        assign unpacked_values[lane+2] = inputs[lane][7:0];
        assign equal_flags[lane] = bank[lane] == inputs[lane];
        always_ff @(posedge clk or negedge reset_n) begin
            if (!reset_n) bank[lane] <= 32'h89abcdef + lane;
            else if (enable && index == lane) begin
                bank[lane] <= transform(inputs[lane]);
                bank[lane][7:0] <= bank[lane][31:24];
                bank[lane][15:8] <= inputs[lane][7:0];
            end
        end
    end
    always_ff @(posedge clk or negedge reset_n) begin
        if (!reset_n) count <= 32'hfffffff0;
        else if (enable) count <= count + 32'd1;
    end
endmodule

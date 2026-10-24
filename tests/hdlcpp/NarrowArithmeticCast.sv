module NarrowGeneratedWord #(
    parameter int unsigned Mask = 0
) (
    input logic [31:0] input_i,
    output logic [31:0] output_o
);
    assign output_o = input_i ^ Mask;
endmodule

module NarrowArithmeticCast #(
    parameter int unsigned Width = 2,
    parameter int unsigned Ways = 1 << Width
) (
    input logic [31:0] index_i,
    input logic [31:0] other_i,
    output logic [63:0] result_o,
    output logic [63:0] product_o,
    output logic [63:0] mask_o,
    output logic [63:0] local_o,
    output logic [63:0] generated_o
);
    typedef logic [Width-1:0] narrow_t;
    typedef struct packed { logic [31:0] ways; logic [31:0] width; } cfg_t;
    localparam cfg_t Cfg = '{ways: Ways, width: Width};
    localparam logic [63:0] Deleg = 64'hdbf7314ac2e59a6d;
    logic [31:0] generated_data [Ways][2];
    for (genvar lane = 0; lane < (1 << Width); lane++) begin : banks
        for (genvar word_idx = 0; word_idx <= 1; word_idx++) begin : words
            NarrowGeneratedWord #(.Mask(7)) word_i (
                .input_i(index_i), .output_o(generated_data[lane][word_idx])
            );
        end
    end

    function automatic narrow_t way_word(input logic [31:0] index);
        return narrow_t'(index % Cfg.ways);
    endfunction

    function automatic narrow_t quotient(input logic [31:0] dividend, divisor);
        return narrow_t'(dividend / divisor);
    endfunction

    assign result_o = {
        8'(way_word(index_i)),
        8'(narrow_t'(index_i % Ways)),
        8'(narrow_t'(index_i / Ways)),
        8'(narrow_t'(index_i % 32'd4)),
        8'(narrow_t'(index_i / 32'd4)),
        8'(narrow_t'((index_i + other_i) / Ways)),
        8'(narrow_t'(index_i / (other_i | 32'd4))),
        8'(narrow_t'(narrow_t'(index_i) / 32'd4))
    };
    assign product_o = index_i * other_i;
    assign mask_o = (Deleg[Cfg.width-1:0] | Cfg.width'(index_i)) & ~Cfg.width'(other_i);
    assign local_o = quotient(index_i, other_i | 32'd4);
    assign generated_o = generated_data[Ways-1][1];
endmodule

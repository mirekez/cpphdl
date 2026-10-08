module DeclaredRanges #(
    parameter int DEPTH = 1,
    parameter int LEFT = -3,
    parameter int RIGHT = 1
) (
    input logic clk_i,
    input logic rst_ni,
    input logic [31:0] data_i,
    output logic [31:0] data_o,
    output logic [31:0] ascending_o,
    output logic [31:0] general_o,
    output logic [31:0] literal_o,
    output logic [31:0] width_o,
    output logic [31:0] general_width_o,
    output logic [31:0] matrix_width_o,
    output logic [31:0] registered_o,
    output logic [31:0] matrix_o
);
    localparam int PTR_WIDTH = $clog2(DEPTH);
    typedef logic [PTR_WIDTH-1:0] pointer_t;
    typedef logic [0:PTR_WIDTH-1] ascending_t;
    typedef logic [LEFT:RIGHT] general_t;
    typedef logic [-1:0] literal_t;
    typedef logic [1:0][PTR_WIDTH-1:0] matrix_t;
    pointer_t pointer;
    ascending_t ascending;
    general_t general_value;
    literal_t literal_value;
    pointer_t registered_value;
    matrix_t matrix_value;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            registered_value <= '0;
            matrix_value <= '0;
        end else begin
            registered_value <= data_i;
            matrix_value <= data_i;
        end
    end
    assign registered_o = registered_value;
    assign matrix_o = matrix_value;
    assign pointer = data_i;
    assign ascending = data_i;
    assign general_value = data_i;
    assign literal_value = data_i;
    assign data_o = pointer;
    assign ascending_o = ascending;
    assign general_o = general_value;
    assign literal_o = literal_value;
    assign width_o = $bits(pointer_t);
    assign general_width_o = $bits(general_t);
    assign matrix_width_o = $bits(matrix_t);
endmodule

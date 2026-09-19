module indexed_comb_proof #(
    parameter int Width = 8
) (
    input logic [Width-1:0] data_i,
    input logic [2:0] index_i,
    output logic [Width-1:0] copied_o,
    output logic [Width-1:0] partial_o,
    output logic [Width-1:0] cyclic_o,
    output logic [Width-1:0] prefix_o,
    output logic [Width-1:0] dynamic_o
);
    logic [Width-1:0] cyclic;
    logic [Width-1:0] prefix;
    for (genvar bit_index = 0; bit_index < Width; bit_index++) begin
        assign copied_o[bit_index] = data_i[bit_index];
        if (bit_index < Width-1) assign partial_o[bit_index] = data_i[bit_index];
        assign cyclic[bit_index] = cyclic[(bit_index+1)%Width];
        if (bit_index == 0) assign prefix[bit_index] = data_i[bit_index];
        else assign prefix[bit_index] = prefix[bit_index-1] ^ data_i[bit_index];
    end
    assign dynamic_o[index_i] = data_i[0];
    assign cyclic_o = cyclic;
    assign prefix_o = prefix;
endmodule

module IndexedWord #(
    parameter int WIDTH = 128
) (
    input logic [WIDTH-1:0] data,
    input logic [63:0] index,
    input logic enable,
    input logic bit_value,
    output logic selected,
    output logic selected_changed,
    output logic [WIDTH-1:0] changed
);
    always_comb begin
        changed = data;
        if (enable && index < WIDTH)
            changed[index] = bit_value;
    end
    assign selected = (index < WIDTH) ? data[index] : 1'b0;
    assign selected_changed = (index < WIDTH) ? changed[index] : 1'b0;
endmodule

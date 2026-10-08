module ZeroRepeat (
    input wire [7:0] data,
    input wire [7:0] selector,
    output wire [15:0] result
);
    localparam int COUNT = 0;
    assign result = {{COUNT{8'hff}}, data, {COUNT{selector}}, selector, {COUNT{data}}};
endmodule

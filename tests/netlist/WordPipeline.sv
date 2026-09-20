module WordPipeline (
    input logic clk,
    input logic reset,
    input logic enable,
    input logic [2:0] select,
    input logic [519:0] payload,
    output logic [64:0] selected,
    output logic [64:0] history,
    output logic [129:0] pair,
    output logic signed [64:0] shifted
);
    always_comb begin
        selected = '0;
        for (int lane = 0; lane < 8; lane++) begin
            if (select == lane) selected = payload[lane * 65 +: 65];
        end
        pair = '1;
        pair[64:0] = selected;
        pair[129:65] = history;
    end
    assign shifted = $signed(selected) >>> select;
    always_ff @(posedge clk) begin
        if (reset) history <= '0;
        else if (enable) history <= history ^ selected;
    end
endmodule

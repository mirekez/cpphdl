module BlockingHold #(
    parameter bit Enabled = 1
) (
    input logic clock,
    input logic reset,
    input logic capture,
    input logic [7:0] data,
    output logic [7:0] held,
    output logic [7:0] after_write,
    output logic [7:0] direct,
    output logic [7:0] count
);
    logic [7:0] retained;
    logic [7:0] observed;
    logic [7:0] counter;

    assign held = retained;
    assign after_write = observed;
    assign count = counter;

    always @(posedge clock) begin
        if (reset)
            counter = 0;
        else begin
            counter = counter + 1;
            if (Enabled && capture) begin
                retained = data;
                observed = retained ^ 8'h5a;
                direct = data ^ 8'ha5;
            end
        end
    end
endmodule

module MultiplyAddReplacement #(parameter LATENCY=2)(
    input wire clk, reset, command_valid_in, response_ready_in,
    input wire [31:0] operation_in, index_in, value_in,
    output wire command_ready_out, response_valid_out,
    output wire [63:0] result_out,
    output wire [31:0] fault_out
);
    initial begin
        if (LATENCY < 2) $fatal(1, "MultiplyAddReplacement requires LATENCY >= 2");
    end
    reg [LATENCY-1:0] valid;
    reg [63:0] result [0:LATENCY-1];
    wire advance = !valid[LATENCY-1] || response_ready_in;
    wire [31:0] sum = operation_in * value_in + index_in;
    assign command_ready_out = advance;
    assign response_valid_out = valid[LATENCY-1];
    assign result_out = result[LATENCY-1];
    assign fault_out = 0;
    integer i;
    always @(posedge clk) begin
        if (reset) begin
            valid <= 0;
            for (i=0; i<LATENCY; i=i+1) result[i] <= 0;
        end else if (advance) begin
            valid[0] <= command_valid_in;
            result[0] <= {index_in,sum};
            for (i=1; i<LATENCY; i=i+1) begin
                valid[i] <= valid[i-1];
                result[i] <= result[i-1];
            end
        end
    end
endmodule

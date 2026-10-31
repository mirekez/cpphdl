// Generated from hls/math/Fp32MacRtlPipeline.h by cpphdl.
`default_nettype none




module Fp32MacRtlPipeline #(
    parameter LATENCY = 'h2
 )
 (
    input wire clk
,   input wire reset
,   input wire command_valid_in
,   input wire response_ready_in
,   input wire[31:0] operation_in
,   input wire[31:0] index_in
,   input wire[31:0] value_in
,   output wire command_ready_out
,   output wire response_valid_out
,   output wire[31:0] result_out
,   output wire[31:0] fault_out
);


    // regs and combs
    reg valid[LATENCY];
    reg[32-1:0] delay[(unsigned'(32'(LATENCY)) > 32'h2) ? (LATENCY - 32'h2) : (32'h1)];

    // members
    wire[31:0] mac__a_in;
    wire[31:0] mac__b_in;
    wire[31:0] mac__c_in;
    wire mac__en_in;
    wire[31:0] mac__result_out;
    Fp32Mac      mac (
        .clk(clk)
,       .reset(reset)
,       .a_in(mac__a_in)
,       .b_in(mac__b_in)
,       .c_in(mac__c_in)
,       .en_in(mac__en_in)
,       .result_out(mac__result_out)
    );

    // tmp variables
    logic valid_tmp[LATENCY];
    logic[32-1:0] delay_tmp[(unsigned'(32'(LATENCY)) > 32'h2) ? (LATENCY - 32'h2) : (32'h1)];


    generate  // _assign
        assign command_ready_out = ((!((valid[(LATENCY - 32'h1)]) != '0) || response_ready_in) != '0);
        assign response_valid_out = ((valid[LATENCY - 32'h1]) != '0);
        assign fault_out = 'h0;
        assign mac__a_in = index_in;
        assign mac__b_in = value_in;
        assign mac__c_in = operation_in;
        assign mac__en_in = ((command_ready_out) != '0);
        if (LATENCY == 32'h2) begin
            assign result_out = mac__result_out;
        end
        else begin
            assign result_out = unsigned'(32'(delay[LATENCY - 32'h3]));
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (command_ready_out) begin
            valid_tmp['h0] = command_valid_in;
            for (logic[31:0] i = 32'h1;i < LATENCY;i=i+1) begin
                valid_tmp[i] = ((valid[i - 32'h1]) != '0);
            end
            if (LATENCY > 32'h2) begin
                delay_tmp['h0] = mac__result_out;
                for (logic[31:0] i = 32'h1;i < (LATENCY - 32'h2);i=i+1) begin
                    delay_tmp[i] = delay[i - 32'h1];
                end
            end
        end
        if (reset) begin
            for (logic[31:0] i = 32'h0;i < LATENCY;i=i+1) begin
                valid_tmp[i] = '0;
            end
            for (logic[31:0] i = 32'h0;i < (((LATENCY > 32'h2)) ? ((LATENCY - 32'h2)) : (32'h1));i=i+1) begin
                delay_tmp[i] = '0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        valid_tmp = valid;
        delay_tmp = delay;

            _work(reset);

        valid <= valid_tmp;
        delay <= delay_tmp;
    end


endmodule

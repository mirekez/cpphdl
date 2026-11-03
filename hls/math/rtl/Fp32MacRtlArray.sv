// Generated from hls/math/Fp32MacRtlPipeline.h by cpphdl.
`default_nettype none




module Fp32MacRtlArray #(
    parameter Rows = 'h4
,   parameter Cols = 'h4
,   parameter LATENCY = 'h2
 )
 (
    input wire clk
,   input wire reset
,   input wire command_valid_in
,   input wire response_ready_in
,   input wire[64'(32'(((((((32'h2*Rows)*Cols) + Rows) + Cols) + 32'h4))*32'h20))-1:0] operation_in
,   input wire[64'(32'(((((((32'h2*Rows)*Cols) + Rows) + Cols) + 32'h4))*32'h20))-1:0] index_in
,   input wire[64'(32'(((((((32'h2*Rows)*Cols) + Rows) + Cols) + 32'h4))*32'h20))-1:0] value_in
,   output wire command_ready_out
,   output wire response_valid_out
,   output wire[64'(32'((((Rows*Cols) + 32'h1))*32'h20))-1:0] result_out
,   output wire[31:0] fault_out
);
    localparam  N = Rows*Cols;
    localparam  Words = (((32'h2*N) + Rows) + Cols) + 32'h4;


    // regs and combs
    reg[32-1:0] tags[LATENCY];
    reg[unsigned'(64'(N))-1:0] masks[LATENCY];
    reg[64'(32'(N*32'h20))-1:0] initials[LATENCY];
    logic[unsigned'(64'(N))-1:0] mask_comb;
    logic[64'(32'(((N + 32'h1))*32'h20))-1:0] result_comb;

    // members
    genvar __i, __j;
    wire lanes__command_valid_in[Rows][Cols];
    wire lanes__response_ready_in[Rows][Cols];
    wire[31:0] lanes__operation_in[Rows][Cols];
    wire[31:0] lanes__index_in[Rows][Cols];
    wire[31:0] lanes__value_in[Rows][Cols];
    wire lanes__command_ready_out[Rows][Cols];
    wire lanes__response_valid_out[Rows][Cols];
    wire[31:0] lanes__result_out[Rows][Cols];
    wire[31:0] lanes__fault_out[Rows][Cols];
    generate
    for (__i=0; __i < Rows; __i = __i + 1) begin
        for (__j=0; __j < Cols; __j = __j + 1) begin
            Fp32MacRtlPipeline #(
        LATENCY
            ) lanes (
                .clk(clk)
            ,           .reset(reset)
            ,           .command_valid_in(lanes__command_valid_in[__i][__j])
            ,           .response_ready_in(lanes__response_ready_in[__i][__j])
            ,           .operation_in(lanes__operation_in[__i][__j])
            ,           .index_in(lanes__index_in[__i][__j])
            ,           .value_in(lanes__value_in[__i][__j])
            ,           .command_ready_out(lanes__command_ready_out[__i][__j])
            ,           .response_valid_out(lanes__response_valid_out[__i][__j])
            ,           .result_out(lanes__result_out[__i][__j])
            ,           .fault_out(lanes__fault_out[__i][__j])
            );
        end
    end
    endgenerate

    // tmp variables
    logic[32-1:0] tags_tmp[LATENCY];
    logic[unsigned'(64'(N))-1:0] masks_tmp[LATENCY];
    logic[64'(32'(N*32'h20))-1:0] initials_tmp[LATENCY];


    always_comb begin : mask_comb_func  // mask_comb_func
        logic[64'(32'(Words*32'h20))-1:0] _input;
        _input = operation_in;
        mask_comb = 'h0;
        for (logic[31:0] i = 32'h0;i < Rows;i=i+1) begin
            for (logic[31:0] j = 32'h0;j < Cols;j=j+1) begin
                mask_comb[(i*Cols) + j +:1] = 64'((i < unsigned'(32'(_input[(((Words - 32'h4))*32'h20) +:32]))) && (j < unsigned'(32'(_input[(((Words - 32'h3))*32'h20) +:32]))));
            end
        end
    end

    always_comb begin : result_comb_func  // result_comb_func
        logic[unsigned'(64'(N))-1:0] mask;
        logic[64'(32'(N*32'h20))-1:0] _initial;
        mask = unsigned'((unsigned'(64'(N)))'(masks[LATENCY - 32'h1]));
        _initial = unsigned'((64'(32'(N*32'h20)))'(initials[LATENCY - 32'h1]));
        result_comb = 'h0;
        result_comb[N*32'h20 +:32] = tags[LATENCY - 32'h1];
        for (logic[31:0] i = 32'h0;i < Rows;i=i+1) begin
            for (logic[31:0] j = 32'h0;j < Cols;j=j+1) begin
                result_comb[(((i*Cols) + j))*32'h20 +:32] = 64'(unsigned'(32'((mask[(i*Cols) + j +:1]) ? (lanes__result_out[i][j]) : (unsigned'(32'(_initial[(((i*Cols) + j))*32'h20 +:32]))))));
            end
        end
    end

    generate  // _assign
        genvar gi;
        genvar gj;
        for (gi = 32'h0;gi < Rows;gi=gi+1) begin
            for (gj = 32'h0;gj < Cols;gj=gj+1) begin
                assign lanes__command_valid_in[gi][gj] = ((command_valid_in) != '0);
                assign lanes__response_ready_in[gi][gj] = ((response_ready_in) != '0);
                assign lanes__operation_in[gi][gj] = unsigned'(32'(operation_in[((((N + Cols) + (gi*Cols)) + gj))*32'h20 +:32]));
                assign lanes__index_in[gi][gj] = (unsigned'(32'(operation_in[(((Words - 32'h1))*32'h20) +:32])) == 32'h0) ? (unsigned'(32'(operation_in[((((32'h2*N) + Cols) + gi))*32'h20 +:32]))) : (unsigned'(32'(operation_in[(((gi*Cols) + gj))*32'h20 +:32])));
                assign lanes__value_in[gi][gj] = unsigned'(32'(operation_in[((N + gj))*32'h20 +:32]));
            end
        end
        assign command_ready_out = ((lanes__command_ready_out['h0]['h0]) != '0);
        assign response_valid_out = ((lanes__response_valid_out['h0]['h0]) != '0);
        assign result_out = result_comb;
        assign fault_out = 'h0;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (command_ready_out) begin
            tags_tmp['h0] = operation_in[((Words - 32'h2))*32'h20 +:32];
            masks_tmp['h0] = mask_comb;
            initials_tmp['h0] = operation_in[((N + Cols))*32'h20 +:((32'h2*N)*32'h20) - (N*32'h20)];
            for (logic[31:0] i = 32'h1;i < LATENCY;i=i+1) begin
                tags_tmp[i] = tags[i - 32'h1];
                masks_tmp[i] = unsigned'((unsigned'(64'(N)))'(masks[i - 32'h1]));
                initials_tmp[i] = unsigned'((64'(32'(N*32'h20)))'(initials[i - 32'h1]));
            end
        end
        if (reset) begin
            for (logic[31:0] i = 32'h0;i < LATENCY;i=i+1) begin
                tags_tmp[i] = '0;
                masks_tmp[i] = '0;
                initials_tmp[i] = '0;
            end
        end
        for (logic[31:0] i = 32'h0;i < Rows;i=i+1) begin
            for (logic[31:0] j = 32'h0;j < Cols;j=j+1) begin
            end
        end
    end
    endtask

    always @(posedge clk) begin
        tags_tmp = tags;
        masks_tmp = masks;
        initials_tmp = initials;

            _work(reset);

        tags <= tags_tmp;
        masks <= masks_tmp;
        initials <= initials_tmp;
    end


endmodule

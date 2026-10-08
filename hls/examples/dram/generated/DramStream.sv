`default_nettype none

import Predef_pkg::*;
import DramCompletion_pkg::*;


module DramStream (
    input wire clk
,   input wire reset
,   output wire memory_out__valid_out
,   output wire memory_out__write_out
,   output wire[unsigned'(64'(32))-1:0] memory_out__addr_out
,   output wire[7:0] memory_out__size_out
,   output wire[63:0] memory_out__data_out
,   input wire memory_out__ready_in
,   input wire memory_out__valid_in
,   input wire[63:0] memory_out__data_in
,   input wire memory_out__error_in
,   output wire memory_out__ready_out
,   input wire valid_in
,   output wire ready_out
,   input wire[31:0] index_in
,   input wire[31:0] count_in
,   input wire[31:0] scale_in
,   input wire[31:0] tag_in
,   input wire ready_in
,   output wire valid_out
,   output wire[63:0] data_out
,   output wire[31:0] tag_out
,   output wire[31:0] error_out
,   output wire[31:0] fault_out
);


    // regs and combs
    reg[2-1:0] head_reg;
    reg[2-1:0] tail_reg;
    reg[3-1:0] count_reg;
    reg[4-1:0][32-1:0] tags_reg;
    reg[4-1:0][32-1:0] scales_reg;
    DramCompletion completion_comb;
    logic available_comb;
    logic[31:0] fault_comb;

    // members
    genvar __i;
    wire loaders__memory_out__valid_out[4];
    wire loaders__memory_out__write_out[4];
    wire[unsigned'(64'(32))-1:0] loaders__memory_out__addr_out[4];
    wire[7:0] loaders__memory_out__size_out[4];
    wire[63:0] loaders__memory_out__data_out[4];
    wire loaders__memory_out__ready_in[4];
    wire loaders__memory_out__valid_in[4];
    wire[63:0] loaders__memory_out__data_in[4];
    wire loaders__memory_out__error_in[4];
    wire loaders__memory_out__ready_out[4];
    wire loaders__command_valid_in[4];
    wire[31:0] loaders__operation_in[4];
    wire[31:0] loaders__index_in[4];
    wire[31:0] loaders__value_in[4];
    wire loaders__command_ready_out[4];
    wire loaders__response_ready_in[4];
    wire loaders__response_valid_out[4];
    wire[63:0] loaders__result_out[4];
    wire[31:0] loaders__fault_out[4];
    generate
    for (__i=0; __i < 4; __i = __i + 1) begin
        cpphdl_hls_ClockedMemoryDramLoad_A32          loaders (
            .clk(clk)
        ,           .reset(reset)
        ,           .memory_out__valid_out(loaders__memory_out__valid_out[__i])
        ,           .memory_out__write_out(loaders__memory_out__write_out[__i])
        ,           .memory_out__addr_out(loaders__memory_out__addr_out[__i])
        ,           .memory_out__size_out(loaders__memory_out__size_out[__i])
        ,           .memory_out__data_out(loaders__memory_out__data_out[__i])
        ,           .memory_out__ready_in(loaders__memory_out__ready_in[__i])
        ,           .memory_out__valid_in(loaders__memory_out__valid_in[__i])
        ,           .memory_out__data_in(loaders__memory_out__data_in[__i])
        ,           .memory_out__error_in(loaders__memory_out__error_in[__i])
        ,           .memory_out__ready_out(loaders__memory_out__ready_out[__i])
        ,           .command_valid_in(loaders__command_valid_in[__i])
        ,           .operation_in(loaders__operation_in[__i])
        ,           .index_in(loaders__index_in[__i])
        ,           .value_in(loaders__value_in[__i])
        ,           .command_ready_out(loaders__command_ready_out[__i])
        ,           .response_ready_in(loaders__response_ready_in[__i])
        ,           .response_valid_out(loaders__response_valid_out[__i])
        ,           .result_out(loaders__result_out[__i])
        ,           .fault_out(loaders__fault_out[__i])
        );
    end
    endgenerate
    wire arbiter__clients_in__valid_in[4];
    wire arbiter__clients_in__write_in[4];
    wire[unsigned'(64'(32))-1:0] arbiter__clients_in__addr_in[4];
    wire[7:0] arbiter__clients_in__size_in[4];
    wire[63:0] arbiter__clients_in__data_in[4];
    wire arbiter__clients_in__ready_out[4];
    wire arbiter__clients_in__valid_out[4];
    wire[63:0] arbiter__clients_in__data_out[4];
    wire arbiter__clients_in__error_out[4];
    wire arbiter__clients_in__ready_in[4];
    wire arbiter__memory_out__valid_out;
    wire arbiter__memory_out__write_out;
    wire[unsigned'(64'(32))-1:0] arbiter__memory_out__addr_out;
    wire[7:0] arbiter__memory_out__size_out;
    wire[63:0] arbiter__memory_out__data_out;
    wire arbiter__memory_out__ready_in;
    wire arbiter__memory_out__valid_in;
    wire[63:0] arbiter__memory_out__data_in;
    wire arbiter__memory_out__error_in;
    wire arbiter__memory_out__ready_out;
    ReadArbiter      arbiter (
        .clk(clk)
,       .reset(reset)
,       .clients_in__valid_in(arbiter__clients_in__valid_in)
,       .clients_in__write_in(arbiter__clients_in__write_in)
,       .clients_in__addr_in(arbiter__clients_in__addr_in)
,       .clients_in__size_in(arbiter__clients_in__size_in)
,       .clients_in__data_in(arbiter__clients_in__data_in)
,       .clients_in__ready_out(arbiter__clients_in__ready_out)
,       .clients_in__valid_out(arbiter__clients_in__valid_out)
,       .clients_in__data_out(arbiter__clients_in__data_out)
,       .clients_in__error_out(arbiter__clients_in__error_out)
,       .clients_in__ready_in(arbiter__clients_in__ready_in)
,       .memory_out__valid_out(arbiter__memory_out__valid_out)
,       .memory_out__write_out(arbiter__memory_out__write_out)
,       .memory_out__addr_out(arbiter__memory_out__addr_out)
,       .memory_out__size_out(arbiter__memory_out__size_out)
,       .memory_out__data_out(arbiter__memory_out__data_out)
,       .memory_out__ready_in(arbiter__memory_out__ready_in)
,       .memory_out__valid_in(arbiter__memory_out__valid_in)
,       .memory_out__data_in(arbiter__memory_out__data_in)
,       .memory_out__error_in(arbiter__memory_out__error_in)
,       .memory_out__ready_out(arbiter__memory_out__ready_out)
    );
    wire calculate__command_valid_in;
    wire[63:0] calculate__operation_in;
    wire[63:0] calculate__index_in;
    wire[63:0] calculate__value_in;
    wire calculate__command_ready_out;
    wire calculate__response_ready_in;
    wire calculate__response_valid_out;
    wire[128-1:0] calculate__result_out;
    wire[31:0] calculate__fault_out;
    cpphdl_hls_ClockedPipelineDramCalculate_logic63_0_logic128m1_0_P3      calculate (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(calculate__command_valid_in)
,       .operation_in(calculate__operation_in)
,       .index_in(calculate__index_in)
,       .value_in(calculate__value_in)
,       .command_ready_out(calculate__command_ready_out)
,       .response_ready_in(calculate__response_ready_in)
,       .response_valid_out(calculate__response_valid_out)
,       .result_out(calculate__result_out)
,       .fault_out(calculate__fault_out)
    );

    // tmp variables
    logic[2-1:0] head_reg_tmp;
    logic[2-1:0] tail_reg_tmp;
    logic[3-1:0] count_reg_tmp;
    logic[4-1:0][32-1:0] tags_reg_tmp;
    logic[4-1:0][32-1:0] scales_reg_tmp;


    always_comb begin : completion_comb_func  // completion_comb_func
        logic[31:0] i;
        completion_comb = 0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (head_reg == 64'(i)) begin
                completion_comb.sum=loaders__result_out[i];
                completion_comb.error=loaders__fault_out[i];
                completion_comb.valid=(count_reg != 64'h0) && loaders__response_valid_out[i];
                completion_comb.tag=tags_reg[64'(i)];
                completion_comb.scale=scales_reg[64'(i)];
            end
        end
    end

    always_comb begin : available_comb_func  // available_comb_func
        logic[31:0] i;
        available_comb=0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (tail_reg == 64'(i)) begin
                available_comb=loaders__command_ready_out[i];
            end
        end
        available_comb=(available_comb && (count_reg < 64'h4)) && (fault_out == 32'h0);
    end

    always_comb begin : fault_comb_func  // fault_comb_func
        logic[31:0] i;
        fault_comb=calculate__fault_out;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            fault_comb|=loaders__fault_out[i];
        end
    end

    generate  // _assign
        genvar gi;
        for (gi=32'h0;gi < 32'h4;gi=gi+1) begin
            assign loaders__command_valid_in[gi] = (((valid_in && ready_out) && (tail_reg == 64'(gi))) != '0);
            assign loaders__operation_in[gi] = 'h1000;
            assign loaders__index_in[gi] = index_in;
            assign loaders__value_in[gi] = count_in;
            assign loaders__response_ready_in[gi] = ((((count_reg != 64'h0) && (head_reg == 64'(gi))) && calculate__command_ready_out) != '0);
            assign arbiter__clients_in__valid_in[gi]=loaders__memory_out__valid_out[gi];
            assign arbiter__clients_in__write_in[gi]=loaders__memory_out__write_out[gi];
            assign arbiter__clients_in__addr_in[gi]=loaders__memory_out__addr_out[gi];
            assign arbiter__clients_in__size_in[gi]=loaders__memory_out__size_out[gi];
            assign arbiter__clients_in__data_in[gi]=loaders__memory_out__data_out[gi];
            assign loaders__memory_out__ready_in[gi]=arbiter__clients_in__ready_out[gi];
            assign loaders__memory_out__valid_in[gi]=arbiter__clients_in__valid_out[gi];
            assign loaders__memory_out__data_in[gi]=arbiter__clients_in__data_out[gi];
            assign loaders__memory_out__error_in[gi]=arbiter__clients_in__error_out[gi];
            assign arbiter__clients_in__ready_in[gi]=loaders__memory_out__ready_out[gi];
        end
        assign memory_out__valid_out=arbiter__memory_out__valid_out;
        assign memory_out__write_out=arbiter__memory_out__write_out;
        assign memory_out__addr_out=arbiter__memory_out__addr_out;
        assign memory_out__size_out=arbiter__memory_out__size_out;
        assign memory_out__data_out=arbiter__memory_out__data_out;
        assign arbiter__memory_out__ready_in=memory_out__ready_in;
        assign arbiter__memory_out__valid_in=memory_out__valid_in;
        assign arbiter__memory_out__data_in=memory_out__data_in;
        assign arbiter__memory_out__error_in=memory_out__error_in;
        assign memory_out__ready_out=arbiter__memory_out__ready_out;
        assign calculate__command_valid_in = ((completion_comb.valid) != '0);
        assign calculate__operation_in = completion_comb.sum;
        assign calculate__index_in = 64'(completion_comb.tag) | ((64'(completion_comb.error) <<< 'h20));
        assign calculate__value_in = 64'(completion_comb.scale);
        assign calculate__response_ready_in = ((ready_in) != '0);
        assign ready_out = ((available_comb) != '0);
        assign valid_out = ((calculate__response_valid_out) != '0);
        assign data_out = unsigned'(64'(calculate__result_out));
        assign tag_out = unsigned'(32'(calculate__result_out >>> 32'sh40));
        assign error_out = unsigned'(32'(calculate__result_out >>> 32'sh60));
        assign fault_out = fault_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] i;
        logic push;
        logic pop;
        for (i=32'h0;i < 32'h4;i=i+1) begin
        end
        push=valid_in && ready_out;
        pop=calculate__command_valid_in && calculate__command_ready_out;
        if (reset) begin
            head_reg_tmp = '0;
            tail_reg_tmp = '0;
            count_reg_tmp = '0;
            tags_reg_tmp = '0;
            scales_reg_tmp = '0;
        end
        else begin
            if (push) begin
                for (i=32'h0;i < 32'h4;i=i+1) begin
                    if (tail_reg == 64'(i)) begin
                        tags_reg_tmp[64'(i)] = tag_in;
                        scales_reg_tmp[64'(i)] = scale_in;
                    end
                end
                tail_reg_tmp = 32'(unsigned'(32'(tail_reg)) + 'h1);
            end
            if (pop) begin
                head_reg_tmp = 32'(unsigned'(32'(head_reg)) + 'h1);
            end
            if (signed'(32'(push)) != signed'(32'(pop))) begin
                count_reg_tmp = 32'(unsigned'(32'(count_reg)) + ((push) ? ('h1) : (32'(-'h1))));
            end
        end
    end
    endtask

    always @(posedge clk) begin
        head_reg_tmp = head_reg;
        tail_reg_tmp = tail_reg;
        count_reg_tmp = count_reg;
        tags_reg_tmp = tags_reg;
        scales_reg_tmp = scales_reg;

        _work(reset);

        head_reg <= head_reg_tmp;
        tail_reg <= tail_reg_tmp;
        count_reg <= count_reg_tmp;
        tags_reg <= tags_reg_tmp;
        scales_reg <= scales_reg_tmp;
    end


endmodule

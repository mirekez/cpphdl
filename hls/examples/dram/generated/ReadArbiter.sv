`default_nettype none

import Predef_pkg::*;


module ReadArbiter (
    input wire clk
,   input wire reset
,   input wire clients_in__valid_in[4]
,   input wire clients_in__write_in[4]
,   input wire[unsigned'(64'(32))-1:0] clients_in__addr_in[4]
,   input wire[7:0] clients_in__size_in[4]
,   input wire[63:0] clients_in__data_in[4]
,   output wire clients_in__ready_out[4]
,   output wire clients_in__valid_out[4]
,   output wire[63:0] clients_in__data_out[4]
,   output wire clients_in__error_out[4]
,   input wire clients_in__ready_in[4]
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
);


    // regs and combs
    reg[2-1:0] turn_reg;
    reg[2-1:0] head_reg;
    reg[2-1:0] tail_reg;
    reg[3-1:0] count_reg;
    reg[4-1:0][2-1:0] owners_reg;
    logic[31:0] owner_comb;
    logic request_comb;
    logic response_ready_comb;
    logic[31:0] address_comb;

    // members

    // tmp variables
    logic[2-1:0] turn_reg_tmp;
    logic[2-1:0] head_reg_tmp;
    logic[2-1:0] tail_reg_tmp;
    logic[3-1:0] count_reg_tmp;
    logic[4-1:0][2-1:0] owners_reg_tmp;


    always_comb begin : owner_comb_func  // owner_comb_func
        logic[31:0] i;
        owner_comb=32'h0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (head_reg == 64'(i)) begin
                owner_comb=owners_reg[64'(i)];
            end
        end
    end

    always_comb begin : request_comb_func  // request_comb_func
        logic[31:0] i;
        request_comb=0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (turn_reg == 64'(i)) begin
                request_comb=clients_in__valid_in[i];
            end
        end
    end

    always_comb begin : address_comb_func  // address_comb_func
        logic[31:0] i;
        address_comb=32'h0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (turn_reg == 64'(i)) begin
                address_comb=clients_in__addr_in[i];
            end
        end
    end

    always_comb begin : response_ready_comb_func  // response_ready_comb_func
        logic[31:0] i;
        response_ready_comb=0;
        for (i=32'h0;i < 32'h4;i=i+1) begin
            if (owner_comb == i) begin
                response_ready_comb=clients_in__ready_in[i];
            end
        end
    end

    generate  // _assign
        genvar gi;
        assign memory_out__valid_out = (((count_reg < 64'h4) && request_comb) != '0);
        assign memory_out__write_out = ((0) != '0);
        assign memory_out__addr_out = address_comb;
        assign memory_out__size_out = 8'h8;
        assign memory_out__data_out = 64'h0;
        assign memory_out__ready_out = (((count_reg != 64'h0) && response_ready_comb) != '0);
        for (gi=32'h0;gi < 32'h4;gi=gi+1) begin
            assign clients_in__ready_out[gi] = ((((count_reg < 64'h4) && (turn_reg == 64'(gi))) && memory_out__ready_in) != '0);
            assign clients_in__valid_out[gi] = ((((count_reg != 64'h0) && (owner_comb == gi)) && memory_out__valid_in) != '0);
            assign clients_in__data_out[gi] = memory_out__data_in;
            assign clients_in__error_out[gi] = ((memory_out__error_in) != '0);
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] i;
        logic push;
        logic pop;
        push=memory_out__valid_out && memory_out__ready_in;
        pop=memory_out__valid_in && memory_out__ready_out;
        if (reset) begin
            turn_reg_tmp = '0;
            head_reg_tmp = '0;
            tail_reg_tmp = '0;
            count_reg_tmp = '0;
            owners_reg_tmp = '0;
        end
        else begin
            if (!request_comb || push) begin
                turn_reg_tmp = 32'(unsigned'(32'(turn_reg)) + 'h1);
            end
            if (push) begin
                for (i=32'h0;i < 32'h4;i=i+1) begin
                    if (tail_reg == 64'(i)) begin
                        owners_reg_tmp[64'(i)] = turn_reg;
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
        turn_reg_tmp = turn_reg;
        head_reg_tmp = head_reg;
        tail_reg_tmp = tail_reg;
        count_reg_tmp = count_reg;
        owners_reg_tmp = owners_reg;

        _work(reset);

        turn_reg <= turn_reg_tmp;
        head_reg <= head_reg_tmp;
        tail_reg <= tail_reg_tmp;
        count_reg <= count_reg_tmp;
        owners_reg <= owners_reg_tmp;
    end


endmodule

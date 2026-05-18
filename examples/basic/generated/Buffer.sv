`default_nettype none

import Predef_pkg::*;


module Buffer #(
    parameter WIDTH
,   parameter DEPTH
 )
 (
    input wire clk
,   input wire reset
,   input wire valid_in
,   input wire[WIDTH-1:0] data_in
,   output wire ready_out
,   output wire valid_out
,   output wire[WIDTH-1:0] data_out
,   input wire ready_in
);
    parameter  INDEX_BITS = (DEPTH<='h1) ? ('h1) : ($clog2(DEPTH));
    parameter  COUNT_BITS = $clog2(DEPTH + 'h1);


    // regs and combs
    reg[DEPTH-1:0][WIDTH-1:0] data_reg;
    reg[INDEX_BITS-1:0] head_reg;
    reg[INDEX_BITS-1:0] tail_reg;
    reg[COUNT_BITS-1:0] count_reg;
    logic ready_comb;
    logic valid_comb;
    logic[WIDTH-1:0] data_comb;

    // members

    // tmp variables
    logic[INDEX_BITS-1:0] head_reg_tmp;
    logic[INDEX_BITS-1:0] tail_reg_tmp;
    logic[COUNT_BITS-1:0] count_reg_tmp;


    always_comb begin : ready_comb_func  // ready_comb_func
        ready_comb=((unsigned'(32'(count_reg)) < DEPTH)) || ready_in;
    end

    always_comb begin : valid_comb_func  // valid_comb_func
        valid_comb=((unsigned'(32'(count_reg)) != 'h0)) || valid_in;
    end

    always_comb begin : data_comb_func  // data_comb_func
        if (unsigned'(32'(count_reg)) != 'h0) begin
            data_comb = data_reg[unsigned'(32'(head_reg))];
            disable data_comb_func;
        end
        data_comb = data_in;
    end

    task _work (input logic reset);
    begin: _work
        logic input_fire;
        logic output_fire;
        logic had_stored;
        logic[31:0] head;
        logic[31:0] tail;
        logic[31:0] count;
        if (reset) begin
            head_reg_tmp = 'h0;
            tail_reg_tmp = 'h0;
            count_reg_tmp = 'h0;
            disable _work;
        end
        input_fire=valid_in && ready_comb;
        output_fire=valid_comb && ready_in;
        head=unsigned'(32'(head_reg));
        tail=unsigned'(32'(tail_reg));
        count=unsigned'(32'(count_reg));
        had_stored=count != 'h0;
        if (output_fire && (count != 'h0)) begin
            head=((head + 'h1)) % DEPTH;
            --count;
        end
        if (input_fire) begin
            if (had_stored || !output_fire) begin
                data_reg[tail] <= data_in;
                tail=((tail + 'h1)) % DEPTH;
                count=count+1;
            end
        end
        head_reg_tmp = unsigned'(INDEX_BITS'(unsigned'(INDEX_BITS'(head))));
        tail_reg_tmp = unsigned'(INDEX_BITS'(unsigned'(INDEX_BITS'(tail))));
        count_reg_tmp = unsigned'(COUNT_BITS'(unsigned'(COUNT_BITS'(count))));
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        head_reg_tmp = head_reg;
        tail_reg_tmp = tail_reg;
        count_reg_tmp = count_reg;

        _work(reset);

        head_reg <= head_reg_tmp;
        tail_reg <= tail_reg_tmp;
        count_reg <= count_reg_tmp;
    end

    assign ready_out = ready_comb;

    assign valid_out = valid_comb;

    assign data_out = data_comb;


endmodule

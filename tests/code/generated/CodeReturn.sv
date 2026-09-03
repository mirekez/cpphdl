`default_nettype none

import Predef_pkg::*;


module CodeReturn (
    input wire clk
,   input wire reset
,   input wire early_in
,   input wire[32-1:0] first_in
,   input wire[32-1:0] second_in
,   output wire[32-1:0] value_out
,   output wire[32-1:0] task_value_out
,   output wire[32-1:0] function_value_out
);


    // regs and combs
    logic[32-1:0] value_comb;
    logic[32-1:0] task_value_comb;
    logic[32-1:0] function_value_comb;
    logic[32-1:0] function_return_cache;
    logic bool_return_cache;
    reg[32-1:0] indexed_reg[2];

    // members

    // tmp variables
    logic[32-1:0] indexed_reg_tmp[2];


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = first_in + unsigned'(32'(unsigned'(32'h1000)));
        if (early_in) begin
            value_comb = first_in + unsigned'(32'(unsigned'(32'h55)));
            disable value_comb_func;
        end
        value_comb = second_in + unsigned'(32'(unsigned'(32'hAA)));
    end

    task value_task (output logic[32-1:0] task_out);
    begin: value_task
        task_out = first_in + unsigned'(32'(unsigned'(32'h2000)));
        if (early_in) begin
            task_out = first_in + unsigned'(32'(unsigned'(32'h155)));
            disable value_task;
        end
        task_out = second_in + unsigned'(32'(unsigned'(32'h1AA)));
    end
    endtask

    always_comb begin : task_value_comb_func  // task_value_comb_func
        value_task(task_value_comb);
    end

    function logic bool_return_function (input logic value);
        bool_return_cache=value;
        return bool_return_cache;
    endfunction

    function logic[32-1:0] value_function (input logic[32-1:0] _context);
        if (bool_return_function(early_in)) begin
            function_return_cache = first_in + unsigned'(32'(unsigned'(32'h255)));
            return unsigned'(32'(function_return_cache));
        end
        function_return_cache = _context + unsigned'(32'(unsigned'(32'h2AA)));
        return unsigned'(32'(function_return_cache));
    endfunction

    always_comb begin : function_value_comb_func  // function_value_comb_func
        function_value_comb = value_function(unsigned'(32'(second_in)));
    end

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        if (reset) begin
            for (i='h0;i < 'h2;i=i+1) begin
                indexed_reg_tmp[i] = '0;
            end
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        indexed_reg_tmp = indexed_reg;

        _work(reset);

        indexed_reg <= indexed_reg_tmp;
    end

    assign value_out = value_comb;

    assign task_value_out = task_value_comb;

    assign function_value_out = function_value_comb;


endmodule

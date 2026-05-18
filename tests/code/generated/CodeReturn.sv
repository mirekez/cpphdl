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

    // members

    // tmp variables


    always_comb begin : value_comb_func  // value_comb_func
        value_comb = first_in + unsigned'(32'(unsigned'(32'('h1000))));
        if (early_in) begin
            value_comb = first_in + unsigned'(32'(unsigned'(32'('h55))));
            disable value_comb_func;
        end
        value_comb = second_in + unsigned'(32'(unsigned'(32'('hAA))));
    end

    task value_task (output logic[32-1:0] task_out);
    begin: value_task
        task_out = first_in + unsigned'(32'(unsigned'(32'('h2000))));
        if (early_in) begin
            task_out = first_in + unsigned'(32'(unsigned'(32'('h155))));
            disable value_task;
        end
        task_out = second_in + unsigned'(32'(unsigned'(32'('h1AA))));
    end
    endtask

    always_comb begin : task_value_comb_func  // task_value_comb_func
        value_task(task_value_comb);
    end

    function logic[32-1:0] value_function ();
        if (early_in) begin
            return unsigned'(32'(first_in + unsigned'(32'(unsigned'(32'('h255))))));
        end
        return unsigned'(32'(second_in + unsigned'(32'(unsigned'(32'('h2AA))))));
    endfunction

    always_comb begin : function_value_comb_func  // function_value_comb_func
        function_value_comb = value_function();
    end

    task _work (input logic reset);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

        _work(reset);

    end

    assign value_out = value_comb;

    assign task_value_out = task_value_comb;

    assign function_value_out = function_value_comb;


endmodule

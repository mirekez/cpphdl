`default_nettype none

import Predef_pkg::*;


module ProceduralComb (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic _input;
    logic first_cache;
    logic signed[63:0] first_clock;
    logic second_cache;
    logic signed[63:0] second_clock;
    reg[1-1:0] result;

    // members

    // tmp variables


    function logic first ();
        if (first_clock == $time) begin
            return first_cache;
        end
        first_clock=$time;
        first_cache=0;
        if (_input) begin
            first_cache=1;
        end
        return first_cache;
    endfunction

    function logic second ();
        if (second_clock == $time) begin
            return second_cache;
        end
        second_clock=$time;
        second_cache=first();
        return second_cache;
    endfunction

    task _work (input logic unused);
    begin: _work
        result <= second();
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

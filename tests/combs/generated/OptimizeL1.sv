`default_nettype none

import Predef_pkg::*;


module OptimizeL1 (
    input wire clk
,   input wire reset
);


    // regs and combs
    logic[16-1:0] _input;
    logic enable;
    logic[16-1:0] combined_cache;
    logic signed[63:0] combined_clock;
    reg[16-1:0] result;

    // members
    wire[16-1:0] leaf_storage__value_in;
    wire leaf_storage__enable_in;
    OptimizeL1Leaf      leaf_storage (
        .clk(clk)
,       .reset(reset)
,       .value_in(leaf_storage__value_in)
,       .enable_in(leaf_storage__enable_in)
    );
    wire[16-1:0] leaf__value_in;
    wire leaf__enable_in;
    OptimizeL1Leaf      leaf (
        .clk(clk)
,       .reset(reset)
,       .value_in(leaf__value_in)
,       .enable_in(leaf__enable_in)
    );

    // tmp variables


    function logic[16-1:0] combined ();
        if (combined_clock == $time) begin
            return combined_cache;
        end
        combined_clock=$time;
        combined_cache = leaf__equal_a + leaf__equal_b + optimize_l1_leaf_value(leaf) + optimize_l1_leaf_value(leaf) + optimize_l1_root_input();
        return combined_cache;
    endfunction

    function logic[16-1:0] _optimized_root_input_in_binding ();
        return _input;
    endfunction

    generate  // _assign
        assign leaf__value_in = _input;
        assign leaf__enable_in = enable;
    endgenerate

    task _work (input logic reset);
    begin: _work
        leaf__work_observed=enable;
        result <= combined();
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

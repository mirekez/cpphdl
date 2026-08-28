`default_nettype none

import Predef_pkg::*;
import StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request_pkg::*;


module StructuralNttpRoot (
    input wire clk
,   input wire reset
);

    typedef StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request structural_request_t;

    // regs and combs
    logic[3-1:0] _input;
    logic[3-1:0] _output;
    logic[3-1:0] work_value;
    logic[3-1:0] projected_value;
    logic[3-1:0] array_projected_value;
    StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request[2-1:0] array_input_comb;
    logic[1-1:0] discarded_child_value;

    // members
    StructuralNttpLeaf #(
    ) leaf (
        .clk(clk)
,       .reset(reset)
    );
    StructuralNttpProjectedLeafStructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_response #(
    ) projected_leaf (
        .clk(clk)
,       .reset(reset)
    );
    StructuralNttpArrayProjectedLeafStructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request #(
    ) array_projected_leaf (
        .clk(clk)
,       .reset(reset)
    );
    StructuralNttpDiscardedChildCallStructuralNttpMissingMethodLeaf #(
        0
    ) discarded_child_call (
        .clk(clk)
,       .reset(reset)
    );

    // tmp variables


    always_comb begin : array_input_comb_func  // array_input_comb_func
        StructuralNttpInterfaceStructuralNttpConfig{{{}_{1}}_3}_request value;
        value.value = _input;
        array_input_comb['h0] = value;
        array_input_comb['h1] = '{(unknown: }(CXXDefaultInitExpr))};
    end

    generate  // _assign
        assign leaf___input = _input;
        assign array_projected_leaf___input = array_input_comb;
    endgenerate

    task _work (input logic unused);
    begin: _work
        work_value = _output;
        projected_value = projected_leaf___output;
        array_projected_value = array_projected_leaf___output;
        discarded_child_value = discarded_child_call___output;
    end
    endtask

    always @(posedge clk) begin

        _work(reset);

    end


endmodule

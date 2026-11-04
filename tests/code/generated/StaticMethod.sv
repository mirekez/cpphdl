`default_nettype none

import Predef_pkg::*;
import StaticMethodObject_pkg::*;
import StaticMethodTemplateHelperStaticMethodConfig_pkg::*;
import StaticMethodConfig_pkg::*;
import StaticMethodPlainHelper_pkg::*;
import StaticMethodHolder_Nested_pkg::*;


module StaticMethod (
    input wire clk
,   input wire reset
,   input wire[16-1:0] value_in
,   output wire[16-1:0] value_out
);


    // regs and combs
    logic[16-1:0] value_comb;

    // members

    // tmp variables


    function logic[15:0] StaticMethodPlainHelper___mix (input logic[15:0] value);
        logic[15:0] rotated;
        rotated=(((signed'(32'(value)) <<< 'h3)) | ((signed'(32'(value)) >>> 32'shD)));
        return unsigned'(16'((signed'(32'(rotated)) ^ signed'(32'(unsigned'(16'h31))))));
    endfunction

    function logic[15:0] StaticMethodTemplateHelperStaticMethodConfig___fold (input logic[15:0] value);
        logic[15:0] masked;
        if (signed'(32'(unsigned'(8'h7))) > 32'sh4) begin
            masked=(32'(value) & (((('h1 <<< signed'(32'(unsigned'(8'h7))))) - 'h1)));
        end
        else begin
            masked=value;
        end
        return unsigned'(16'((signed'(32'(StaticMethodPlainHelper___mix(masked))) + signed'(32'(unsigned'(16'h155))))));
    endfunction

    function logic[15:0] StaticMethodHolder_Nested___spread (input logic[15:0] value);
        logic[31:0] wide;
        wide=value;
        return 16'(((((wide <<< 'h1)) ^ ((wide >>> 32'sh2))) ^ 'h4A5));
    endfunction

    function logic[15:0] StaticMethodObject___apply (
        input StaticMethodObject _this
,       input logic[15:0] value
    );
        logic[15:0] folded;
        folded=StaticMethodTemplateHelperStaticMethodConfig___fold(value);
        return unsigned'(16'((signed'(32'(folded)) + signed'(32'(_this.bias))) + signed'(32'(StaticMethodHolder_Nested___spread(_this.bias)))));
    endfunction

    always_comb begin : value_comb_func  // value_comb_func
        logic[15:0] a;
        logic[15:0] b;
        logic[15:0] c;
        StaticMethodObject object;
        object.bias=16'h27;
        a=StaticMethodObject___apply(object, unsigned'(16'(value_in)));
        a=StaticMethodPlainHelper___mix(a);
        b=StaticMethodTemplateHelperStaticMethodConfig___fold(a);
        c=StaticMethodHolder_Nested___spread(b);
        value_comb = 64'(c);
    end

    task _work (input logic unused);
    begin: _work
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin

            _work(reset);

    end

    assign value_out = value_comb;


endmodule

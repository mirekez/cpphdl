// One enabled FP32 addition stage. Match the HLS RNE/flush/canonical-NaN
// contract while allowing hdlcpp's native backend to use a CPU floating add.
module Fp32ReductionAdd(input wire clk,reset,en_in,
    input wire [31:0] a_in,b_in,output wire [31:0] result_out);
    reg [31:0] result;
    function automatic [31:0] input_bits(input [31:0] value);
        input_bits=value[30:23]==0 ? {value[31],31'b0} : value;
    endfunction
    function automatic [31:0] add(input [31:0] av,bv);
        real a,b,c,magnitude;
        reg [31:0] bits;
        begin
            a=$bitstoshortreal(input_bits(av));
            b=$bitstoshortreal(input_bits(bv));
            c = a + b;
            bits=$shortrealtobits(shortreal'(c));
            magnitude=c<0.0 ? -c : c;
            if(bits[30:23]==255 && bits[22:0]!=0)bits=32'h7fc00000;
            else if(bits[30:23]==0)bits={bits[31],31'b0};
            else if(bits[30:0]==31'h00800000 && magnitude<$bitstoreal(64'h380ffffff0000000))
                bits={bits[31],31'b0};
            add=bits;
        end
    endfunction
    always @(posedge clk)begin
        if(reset)result<=0;
        else if(en_in)result<=add(a_in,b_in);
    end
    assign result_out=result;
endmodule

// Simulation-only counterpart of Fp32Mac.h: two enabled stages, separate
// FP32 rounding, canonical NaNs and the HLS exponent-zero flush contract.
// Native hdlcpp lowers real arithmetic to host double operations.
module Fp32Mac(input wire clk, reset, en_in,
    input wire [31:0] a_in,b_in,c_in,output wire [31:0] result_out);
    reg [31:0] product,addend,result;
    function automatic [31:0] input_bits(input [31:0] value);
        input_bits=value[30:23]==0 ? {value[31],31'b0} : value;
    endfunction
    function automatic [31:0] round_fp32(input real value);
        reg [31:0] bits;
        real magnitude;
        begin
            bits=$shortrealtobits(shortreal'(value));
            magnitude=value<0.0 ? -value : value;
            if(bits[30:23]==255 && bits[22:0]!=0)bits=32'h7fc00000;
            else if(bits[30:23]==0)bits={bits[31],31'b0};
            // HLS rounds a normalized 24-bit significand before flushing
            // underflow. IEEE gradual underflow has a different midpoint
            // immediately below the smallest normal; preserve the HLS rule.
            else if(bits[30:0]==31'h00800000 && magnitude<$bitstoreal(64'h380ffffff0000000))
                bits={bits[31],31'b0};
            round_fp32=bits;
        end
    endfunction
    function automatic [31:0] multiply(input [31:0] av,bv);
        real a,b,c;
        begin
            a=$bitstoshortreal(input_bits(av));
            b=$bitstoshortreal(input_bits(bv));
            c = a * b;
            multiply=round_fp32(c);
        end
    endfunction
    function automatic [31:0] add(input [31:0] av,bv);
        real a,b,c;
        begin
            a=$bitstoshortreal(input_bits(av));
            b=$bitstoshortreal(input_bits(bv));
            c = a + b;
            add=round_fp32(c);
        end
    endfunction
    always @(posedge clk)begin
        if(reset)begin product<=0;addend<=0;result<=0;end
        else if(en_in)begin
            product<=multiply(a_in,b_in);addend<=c_in;
            result<=add(product,addend);
        end
    end
    assign result_out=result;
endmodule

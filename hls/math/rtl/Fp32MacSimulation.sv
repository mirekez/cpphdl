// Simulation-only counterpart of Fp32Mac.h: two enabled stages plus the optional
// internal-adder, alignment and raw-product latencies, separate
// FP32 rounding, canonical NaNs and the HLS exponent-zero flush contract.
// Native hdlcpp lowers real arithmetic to host double operations.
module Fp32Mac #(parameter PIPELINE_ADD=0, PIPELINE_PRODUCT=0, PIPELINE_ALIGN=0)(input wire clk, reset, en_in,
    input wire [31:0] a_in,b_in,c_in,output wire [31:0] result_out);
    reg [31:0] product,addend,result;
    // In Verilator, shortreal conversions are promoted to real without preserving
    // their 32-bit bit-cast semantics. Construct binary64 bits explicitly;
    // every normalized FP32 input is represented exactly. Inputs with a zero
    // exponent are flushed as in the hardware, including their sign.
    function automatic real decode_fp32(input [31:0] value);
        reg [10:0] exponent;
        reg [63:0] bits;
        begin
            exponent=value[30:23]==255 ? 11'd2047 : {3'b0,value[30:23]}+11'd896;
            bits=value[30:23]==0 ? {value[31],63'b0} :
                {value[31],exponent,value[22:0],29'b0};
            decode_fp32=$bitstoreal(bits);
        end
    endfunction
    function automatic [31:0] round_fp32(input real value);
        reg [63:0] bits;
        reg [24:0] significand;
        reg [7:0] packed_exponent;
        integer exponent;
        begin
            bits=$realtobits(value);
            if(bits[62:52]==2047)
                round_fp32=bits[51:0]!=0 ? 32'h7fc00000 : {bits[63],8'hff,23'b0};
            else if(bits[62:52]==0)round_fp32={bits[63],31'b0};
            else begin
                exponent=int'({1'b0,bits[62:52]})-1023;
                significand={1'b0,1'b1,bits[51:29]};
                // Round to nearest, ties to even, then apply the hardware's
                // exponent-zero flush. Rounding before flushing also handles
                // the midpoint immediately below the smallest normal value.
                if(bits[28] && ((|bits[27:0]) || significand[0]))
                    significand=significand+25'd1;
                if(significand[24])begin significand=significand>>1;exponent=exponent+1;end
                packed_exponent=8'(exponent+127);
                if(exponent>127)round_fp32={bits[63],8'hff,23'b0};
                else if(exponent< -126)round_fp32={bits[63],31'b0};
                else round_fp32={bits[63],packed_exponent,significand[22:0]};
            end
        end
    endfunction
    function automatic [31:0] multiply(input [31:0] av,bv);
        real a,b,c;
        begin
            a=decode_fp32(av);
            b=decode_fp32(bv);
            c = a * b;
            multiply=round_fp32(c);
        end
    endfunction
    function automatic [31:0] add(input [31:0] av,bv);
        real a,b,c;
        begin
            a=decode_fp32(av);
            b=decode_fp32(bv);
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
    // Overrides may be one-bit comparison expressions. Widen before adding
    // so enabling both optional stages cannot wrap 1 + 1 back to zero.
    localparam EXTRA_STAGES=32'd0+PIPELINE_ADD+PIPELINE_PRODUCT+PIPELINE_ALIGN;
    generate if(EXTRA_STAGES>0)begin: internal_latency
        reg [31:0] delayed_result[0:EXTRA_STAGES-1];
        integer i;
        always @(posedge clk)begin
            if(reset)begin
                for(i=0;i<EXTRA_STAGES;i=i+1)delayed_result[i]<=0;
            end else if(en_in)begin
                delayed_result[0]<=result;
                for(i=1;i<EXTRA_STAGES;i=i+1)delayed_result[i]<=delayed_result[i-1];
            end
        end
        assign result_out=delayed_result[EXTRA_STAGES-1];
    end else begin: original_latency
        assign result_out=result;
    end endgenerate
endmodule

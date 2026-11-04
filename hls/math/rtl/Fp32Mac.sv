// Generated from lib/math/Fp32Mac.h by cpphdl.
`default_nettype none




module Fp32Mac (
    input wire clk
,   input wire reset
,   input wire[31:0] a_in
,   input wire[31:0] b_in
,   input wire[31:0] c_in
,   input wire en_in
,   output wire[31:0] result_out
);


    // regs and combs
    reg[32-1:0] product;
    reg[32-1:0] addend;
    logic[31:0] multiply_comb;
    logic[31:0] result_comb;

    // members
    wire[32-1:0] adder__a_in;
    wire[32-1:0] adder__b_in;
    wire adder__en_in;
    wire[32-1:0] adder__c_out;
    FpAdd #(
        32
,       8
    ) adder (
        .clk(clk)
,       .reset(reset)
,       .a_in(adder__a_in)
,       .b_in(adder__b_in)
,       .en_in(adder__en_in)
,       .c_out(adder__c_out)
    );

    // tmp variables
    logic[32-1:0] product_tmp;
    logic[32-1:0] addend_tmp;


    always_comb begin : multiply_comb_func  // multiply_comb_func
        logic signed[31:0] exponent;
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] ea;
        logic[31:0] eb;
        logic[31:0] ma;
        logic[31:0] mb;
        logic[31:0] sign;
        logic[31:0] shift;
        logic[31:0] retained;
        logic[31:0] tail;
        logic[63:0] product_value;
        logic[63:0] mantissa;
        a=a_in;
        b=b_in;
        ea=((a >>> 32'sh17)) & 32'hFF;
        eb=((b >>> 32'sh17)) & 32'hFF;
        ma=a & 32'h7FFFFF;
        mb=b & 32'h7FFFFF;
        sign=((a ^ b)) & 'h80000000;
        product_value=64'h0;
        mantissa=64'h0;
        exponent='h0;
        shift=32'h14;
        retained=32'h0;
        tail=32'h0;
        multiply_comb=sign;
        if ((((((ea == 32'hFF) && ((ma) != '0))) || (((eb == 32'hFF) && ((mb) != '0)))) || (((ea == 32'hFF) && !((eb) != '0)))) || (((eb == 32'hFF) && !((ea) != '0)))) begin
            multiply_comb='h7FC00000;
        end
        else begin
            if ((ea == 32'hFF) || (eb == 32'hFF)) begin
                multiply_comb=sign | 'h7F800000;
            end
            else begin
                if (((ea) != '0) && ((eb) != '0)) begin
                    product_value=128'(64'(32'((ma | 'h800000))))*128'(64'(32'((mb | 'h800000))));
                    exponent=(signed'(32'(ea)) + signed'(32'(eb))) - 'h7F;
                    if (((product_value & ((64'h1 <<< 'h2F))) != '0)) begin
                        shift=32'h15;
                        exponent=exponent+1;
                    end
                    mantissa=((product_value >>> shift)) | 64'((((product_value & ((((64'h1 <<< shift)) - 64'h1)))) != 64'h0));
                    retained=mantissa >>> 32'sh3;
                    tail=32'(mantissa) & 32'h7;
                    if ((tail > 32'h4) || (((tail == 32'h4) && ((((retained & 32'h1))) != '0)))) begin
                        retained=retained+1;
                    end
                    if (retained>='h1000000) begin
                        retained>>='h1;
                        exponent=exponent+1;
                    end
                    if (exponent>=32'shFF) begin
                        multiply_comb=sign | 'h7F800000;
                    end
                    else begin
                        if (exponent > 32'sh0) begin
                            multiply_comb=(sign | ((unsigned'(32'(exponent)) <<< 'h17))) | ((retained & 32'h7FFFFF));
                        end
                    end
                end
            end
        end
    end

    always_comb begin : result_comb_func  // result_comb_func
        result_comb=adder__c_out;
        if ((((result_comb & 'h7F800000)) == 'h7F800000) && ((((result_comb & 'h7FFFFF))) != '0)) begin
            result_comb='h7FC00000;
        end
    end

    generate  // _assign
        assign adder__a_in = unsigned'(32'(product));
        assign adder__b_in = unsigned'(32'(addend));
        assign adder__en_in = ((en_in) != '0);
        assign result_out = result_comb;
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (en_in) begin
            product_tmp = multiply_comb;
            addend_tmp = c_in;
        end
        if (reset) begin
            product_tmp = '0;
            addend_tmp = '0;
        end
    end
    endtask

    always @(posedge clk) begin
        product_tmp = product;
        addend_tmp = addend;

            _work(reset);

        product <= product_tmp;
        addend <= addend_tmp;
    end


endmodule

// Generated from lib/math/Fp32Mac.h by cpphdl.
`default_nettype none




module Fp32Mac #(
    parameter PIPELINE_ADD = 0
,   parameter PIPELINE_PRODUCT = 0
,   parameter PIPELINE_ALIGN = 0
 )
 (
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
    reg[128-1:0] raw_product;
    reg[32-1:0] raw_addend;
    logic[128-1:0] raw_product_comb;
    logic[31:0] multiply_comb;
    logic[31:0] result_comb;

    // members
    wire[64'h20-1:0] adder__a_in;
    wire[64'h20-1:0] adder__b_in;
    wire adder__en_in;
    wire[64'h20-1:0] adder__c_out;
    FpAdd #(
        64'h20
,       64'h8
,       PIPELINE_ADD
,       PIPELINE_ALIGN
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
    logic[128-1:0] raw_product_tmp;
    logic[32-1:0] raw_addend_tmp;


    always_comb begin : raw_product_comb_func  // raw_product_comb_func
        logic[63:0] product_value;
        logic signed[31:0] exponent;
        logic special;
        logic[31:0] a;
        logic[31:0] b;
        logic[31:0] ea;
        logic[31:0] eb;
        logic[31:0] ma;
        logic[31:0] mb;
        logic[31:0] sign;
        logic[31:0] special_value;
        a=a_in;
        b=b_in;
        ea=((a >>> 32'sh17)) & 32'hFF;
        eb=((b >>> 32'sh17)) & 32'hFF;
        ma=a & 32'h7FFFFF;
        mb=b & 32'h7FFFFF;
        sign=((a ^ b)) & 'h80000000;
        product_value=128'(64'(32'((ma | 'h800000))))*128'(64'(32'((mb | 'h800000))));
        exponent=(signed'(32'(ea)) + signed'(32'(eb))) - 'h7F;
        special=1;
        special_value=sign;
        if ((((((ea == 32'hFF) && ((ma) != '0))) || (((eb == 32'hFF) && ((mb) != '0)))) || (((ea == 32'hFF) && !((eb) != '0)))) || (((eb == 32'hFF) && !((ea) != '0)))) begin
            special_value='h7FC00000;
        end
        else begin
            if ((ea == 32'hFF) || (eb == 32'hFF)) begin
                special_value=sign | 'h7F800000;
            end
            else begin
                if (((ea) != '0) && ((eb) != '0)) begin
                    special=0;
                end
            end
        end
        raw_product_comb = product_value;
        raw_product_comb |= 128'(unsigned'(32'(exponent))) << 'h30;
        raw_product_comb |= 128'(special_value) << 'h50;
        raw_product_comb |= 128'(special) << 'h70;
        raw_product_comb |= 128'(sign != 32'h0) << 'h71;
    end

    always_comb begin : multiply_comb_func  // multiply_comb_func
        logic[128-1:0] raw;
        logic signed[31:0] exponent;
        logic[31:0] sign;
        logic[31:0] shift;
        logic[31:0] retained;
        logic[31:0] tail;
        logic[63:0] product_value;
        logic[63:0] mantissa;
        if (PIPELINE_PRODUCT) begin
            raw = raw_product;
        end
        else begin
            raw = raw_product_comb;
        end
        product_value=raw['h0 +:48];
        exponent=32'(unsigned'(64'(raw['h30 +:32])));
        sign=(((unsigned'(64'(raw['h71 +:1]))) != '0)) ? ('h80000000) : (32'h0);
        mantissa=64'h0;
        shift=32'h14;
        retained=32'h0;
        tail=32'h0;
        multiply_comb=sign;
        if (((unsigned'(64'(raw['h70 +:1]))) != '0)) begin
            multiply_comb=unsigned'(64'(raw['h50 +:32]));
        end
        else begin
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
            if (PIPELINE_PRODUCT) begin
                raw_product_tmp = raw_product_comb;
                raw_addend_tmp = c_in;
                addend_tmp = raw_addend;
            end
            else begin
                addend_tmp = c_in;
            end
        end
        if (reset) begin
            product_tmp = '0;
            addend_tmp = '0;
            raw_product_tmp = '0;
            raw_addend_tmp = '0;
        end
    end
    endtask

    always @(posedge clk) begin
        product_tmp = product;
        addend_tmp = addend;
        raw_product_tmp = raw_product;
        raw_addend_tmp = raw_addend;

            _work(reset);

        product <= product_tmp;
        addend <= addend_tmp;
        raw_product <= raw_product_tmp;
        raw_addend <= raw_addend_tmp;
    end


endmodule

`default_nettype none

import Predef_pkg::*;


module VarArray (
    input wire clk
,   input wire reset
,   input wire[16-1:0] seed_in
,   output wire[16-1:0] comb_out
,   output wire[16-1:0] logic_out
,   output wire[16-1:0] reg_out
);


    // regs and combs
    logic[16-1:0] c_1d[3];
    logic[16-1:0] c_2d[2][3];
    logic[16-1:0] c_3d[2][2][2];
    logic[16-1:0] logic_1d[2];
    logic[16-1:0] logic_2d[2][2];
    logic[16-1:0] logic_3d[2][2][2];
    logic[16-1:0] cpp_1d[3];
    logic[16-1:0] cpp_2d[2][3];
    logic[16-1:0] cpp_3d[2][2][2];
    logic[16-1:0] mixed_c_cpp[2][2][2];
    reg[16-1:0] reg_1d[2];
    reg[16-1:0] reg_2d[2][2];
    reg[16-1:0] reg_3d[2][2][2];
    reg[2-1:0][16-1:0] reg_cpp[2];
    reg[2-1:0][16-1:0] reg_mixed[2][2];
    logic[16-1:0] comb_comb;
    logic[16-1:0] logic_comb;
    logic[16-1:0] reg_comb;

    // members
    genvar gi, gj, gk;

    // tmp variables
    logic[16-1:0] reg_1d_tmp[2];
    logic[16-1:0] reg_2d_tmp[2][2];
    logic[16-1:0] reg_3d_tmp[2][2][2];
    logic[2-1:0][16-1:0] reg_cpp_tmp[2];
    logic[2-1:0][16-1:0] reg_mixed_tmp[2][2];


    always_comb begin : comb_comb_func  // comb_comb_func
        c_1d['h0] = seed_in + unsigned'(16'('h1));
        c_1d['h1] = c_1d['h0] + unsigned'(16'('h2));
        c_1d['h2] = c_1d['h1] + unsigned'(16'('h3));
        c_2d['h0]['h0] = c_1d['h2] + unsigned'(16'('hA));
        c_2d['h0]['h1] = c_2d['h0]['h0] + unsigned'(16'('hB));
        c_2d['h0]['h2] = c_2d['h0]['h1] + unsigned'(16'('hC));
        c_2d['h1]['h0] = c_1d['h2] + unsigned'(16'('h14));
        c_2d['h1]['h1] = c_2d['h1]['h0] + unsigned'(16'('h15));
        c_2d['h1]['h2] = c_2d['h1]['h1] + unsigned'(16'('h16));
        c_3d['h0]['h0]['h0] = c_2d['h0]['h1] + unsigned'(16'('h64));
        c_3d['h0]['h0]['h1] = c_3d['h0]['h0]['h0] + unsigned'(16'('h65));
        c_3d['h0]['h1]['h0] = c_2d['h0]['h2] + unsigned'(16'('h6E));
        c_3d['h0]['h1]['h1] = c_3d['h0]['h1]['h0] + unsigned'(16'('h6F));
        c_3d['h1]['h0]['h0] = c_2d['h1]['h1] + unsigned'(16'('hC8));
        c_3d['h1]['h0]['h1] = c_3d['h1]['h0]['h0] + unsigned'(16'('hC9));
        c_3d['h1]['h1]['h0] = c_2d['h1]['h2] + unsigned'(16'('hD2));
        c_3d['h1]['h1]['h1] = c_3d['h1]['h1]['h0] + unsigned'(16'('hD3));
        cpp_1d['h0] = c_1d['h2] + unsigned'(16'('h1E));
        cpp_1d['h1] = cpp_1d['h0] + unsigned'(16'('h1F));
        cpp_1d['h2] = cpp_1d['h1] + unsigned'(16'('h20));
        cpp_2d['h0]['h0] = cpp_1d['h2] + unsigned'(16'('h28));
        cpp_2d['h0]['h1] = cpp_2d['h0]['h0] + unsigned'(16'('h29));
        cpp_2d['h0]['h2] = cpp_2d['h0]['h1] + unsigned'(16'('h2A));
        cpp_2d['h1]['h0] = cpp_1d['h2] + unsigned'(16'('h32));
        cpp_2d['h1]['h1] = cpp_2d['h1]['h0] + unsigned'(16'('h33));
        cpp_2d['h1]['h2] = cpp_2d['h1]['h1] + unsigned'(16'('h34));
        cpp_3d['h0]['h0]['h0] = cpp_2d['h0]['h1] + unsigned'(16'('h3C));
        cpp_3d['h0]['h0]['h1] = cpp_3d['h0]['h0]['h0] + unsigned'(16'('h3D));
        cpp_3d['h0]['h1]['h0] = cpp_2d['h0]['h2] + unsigned'(16'('h3E));
        cpp_3d['h0]['h1]['h1] = cpp_3d['h0]['h1]['h0] + unsigned'(16'('h3F));
        cpp_3d['h1]['h0]['h0] = cpp_2d['h1]['h1] + unsigned'(16'('h40));
        cpp_3d['h1]['h0]['h1] = cpp_3d['h1]['h0]['h0] + unsigned'(16'('h41));
        cpp_3d['h1]['h1]['h0] = cpp_2d['h1]['h2] + unsigned'(16'('h42));
        cpp_3d['h1]['h1]['h1] = cpp_3d['h1]['h1]['h0] + unsigned'(16'('h43));
        mixed_c_cpp['h0]['h0]['h0] = cpp_3d['h0]['h1]['h1] + unsigned'(16'('h46));
        mixed_c_cpp['h0]['h0]['h1] = mixed_c_cpp['h0]['h0]['h0] + unsigned'(16'('h47));
        mixed_c_cpp['h0]['h1]['h0] = cpp_3d['h1]['h0]['h1] + unsigned'(16'('h48));
        mixed_c_cpp['h0]['h1]['h1] = mixed_c_cpp['h0]['h1]['h0] + unsigned'(16'('h49));
        mixed_c_cpp['h1]['h0]['h0] = cpp_3d['h1]['h1]['h0] + unsigned'(16'('h4A));
        mixed_c_cpp['h1]['h0]['h1] = mixed_c_cpp['h1]['h0]['h0] + unsigned'(16'('h4B));
        mixed_c_cpp['h1]['h1]['h0] = cpp_3d['h1]['h1]['h1] + unsigned'(16'('h4C));
        mixed_c_cpp['h1]['h1]['h1] = mixed_c_cpp['h1]['h1]['h0] + unsigned'(16'('h4D));
        comb_comb = (c_3d['h1]['h1]['h1] + cpp_3d['h1]['h1]['h1]) + mixed_c_cpp['h1]['h1]['h1];
        disable comb_comb_func;
    end

    always_comb begin : logic_comb_func  // logic_comb_func
        logic_1d['h0] = seed_in + unsigned'(16'('h12C));
        logic_1d['h1] = logic_1d['h0] + 'h12D;
        logic_2d['h0]['h0] = logic_1d['h1] + 'h12E;
        logic_2d['h0]['h1] = logic_2d['h0]['h0] + 'h12F;
        logic_2d['h1]['h0] = logic_2d['h0]['h1] + 'h130;
        logic_2d['h1]['h1] = logic_2d['h1]['h0] + 'h131;
        logic_3d['h0]['h0]['h0] = logic_2d['h1]['h1] + 'h132;
        logic_3d['h0]['h0]['h1] = logic_3d['h0]['h0]['h0] + 'h133;
        logic_3d['h0]['h1]['h0] = logic_3d['h0]['h0]['h1] + 'h134;
        logic_3d['h0]['h1]['h1] = logic_3d['h0]['h1]['h0] + 'h135;
        logic_3d['h1]['h0]['h0] = logic_3d['h0]['h1]['h1] + 'h136;
        logic_3d['h1]['h0]['h1] = logic_3d['h1]['h0]['h0] + 'h137;
        logic_3d['h1]['h1]['h0] = logic_3d['h1]['h0]['h1] + 'h138;
        logic_3d['h1]['h1]['h1] = logic_3d['h1]['h1]['h0] + 'h139;
        logic_comb = logic_3d['h1]['h1]['h1];
        disable logic_comb_func;
    end

    always_comb begin : reg_comb_func  // reg_comb_func
        reg_comb = (((reg_1d['h1] + reg_2d['h1]['h1]) + reg_3d['h1]['h1]['h1]) + reg_cpp['h1]['h1]) + reg_mixed['h1]['h1]['h1];
        disable reg_comb_func;
    end

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            reg_1d_tmp['h0] = 'h0;
            reg_1d_tmp['h1] = 'h0;
            reg_2d_tmp['h0]['h0] = 'h0;
            reg_2d_tmp['h0]['h1] = 'h0;
            reg_2d_tmp['h1]['h0] = 'h0;
            reg_2d_tmp['h1]['h1] = 'h0;
            reg_3d_tmp['h0]['h0]['h0] = 'h0;
            reg_3d_tmp['h0]['h0]['h1] = 'h0;
            reg_3d_tmp['h0]['h1]['h0] = 'h0;
            reg_3d_tmp['h0]['h1]['h1] = 'h0;
            reg_3d_tmp['h1]['h0]['h0] = 'h0;
            reg_3d_tmp['h1]['h0]['h1] = 'h0;
            reg_3d_tmp['h1]['h1]['h0] = 'h0;
            reg_3d_tmp['h1]['h1]['h1] = 'h0;
            reg_cpp_tmp['h0]['h0] = 'h0;
            reg_cpp_tmp['h0]['h1] = 'h0;
            reg_cpp_tmp['h1]['h0] = 'h0;
            reg_cpp_tmp['h1]['h1] = 'h0;
            reg_mixed_tmp['h0]['h0]['h0] = 'h0;
            reg_mixed_tmp['h0]['h0]['h1] = 'h0;
            reg_mixed_tmp['h0]['h1]['h0] = 'h0;
            reg_mixed_tmp['h0]['h1]['h1] = 'h0;
            reg_mixed_tmp['h1]['h0]['h0] = 'h0;
            reg_mixed_tmp['h1]['h0]['h1] = 'h0;
            reg_mixed_tmp['h1]['h1]['h0] = 'h0;
            reg_mixed_tmp['h1]['h1]['h1] = 'h0;
            disable _work;
        end
        reg_1d_tmp['h0] = seed_in + unsigned'(16'('h5));
        reg_1d_tmp['h1] = reg_1d['h0] + unsigned'(16'('h6));
        reg_2d_tmp['h0]['h0] = reg_1d['h1] + unsigned'(16'('h7));
        reg_2d_tmp['h0]['h1] = reg_2d['h0]['h0] + unsigned'(16'('h8));
        reg_2d_tmp['h1]['h0] = reg_2d['h0]['h1] + unsigned'(16'('h9));
        reg_2d_tmp['h1]['h1] = reg_2d['h1]['h0] + unsigned'(16'('hA));
        reg_3d_tmp['h0]['h0]['h0] = reg_2d['h1]['h1] + unsigned'(16'('hB));
        reg_3d_tmp['h0]['h0]['h1] = reg_3d['h0]['h0]['h0] + unsigned'(16'('hC));
        reg_3d_tmp['h0]['h1]['h0] = reg_3d['h0]['h0]['h1] + unsigned'(16'('hD));
        reg_3d_tmp['h0]['h1]['h1] = reg_3d['h0]['h1]['h0] + unsigned'(16'('hE));
        reg_3d_tmp['h1]['h0]['h0] = reg_3d['h0]['h1]['h1] + unsigned'(16'('hF));
        reg_3d_tmp['h1]['h0]['h1] = reg_3d['h1]['h0]['h0] + unsigned'(16'('h10));
        reg_3d_tmp['h1]['h1]['h0] = reg_3d['h1]['h0]['h1] + unsigned'(16'('h11));
        reg_3d_tmp['h1]['h1]['h1] = reg_3d['h1]['h1]['h0] + unsigned'(16'('h12));
        reg_cpp_tmp['h0]['h0] = reg_3d['h1]['h1]['h1] + unsigned'(16'('h13));
        reg_cpp_tmp['h0]['h1] = reg_cpp['h0]['h0] + unsigned'(16'('h14));
        reg_cpp_tmp['h1]['h0] = reg_cpp['h0]['h1] + unsigned'(16'('h15));
        reg_cpp_tmp['h1]['h1] = reg_cpp['h1]['h0] + unsigned'(16'('h16));
        reg_mixed_tmp['h0]['h0]['h0] = reg_cpp['h1]['h1] + unsigned'(16'('h17));
        reg_mixed_tmp['h0]['h0]['h1] = reg_mixed['h0]['h0]['h0] + unsigned'(16'('h18));
        reg_mixed_tmp['h0]['h1]['h0] = reg_mixed['h0]['h0]['h1] + unsigned'(16'('h19));
        reg_mixed_tmp['h0]['h1]['h1] = reg_mixed['h0]['h1]['h0] + unsigned'(16'('h1A));
        reg_mixed_tmp['h1]['h0]['h0] = reg_mixed['h0]['h1]['h1] + unsigned'(16'('h1B));
        reg_mixed_tmp['h1]['h0]['h1] = reg_mixed['h1]['h0]['h0] + unsigned'(16'('h1C));
        reg_mixed_tmp['h1]['h1]['h0] = reg_mixed['h1]['h0]['h1] + unsigned'(16'('h1D));
        reg_mixed_tmp['h1]['h1]['h1] = reg_mixed['h1]['h1]['h0] + unsigned'(16'('h1E));
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        reg_1d_tmp = reg_1d;
        reg_2d_tmp = reg_2d;
        reg_3d_tmp = reg_3d;
        reg_cpp_tmp = reg_cpp;
        reg_mixed_tmp = reg_mixed;

        _work(reset);

        reg_1d <= reg_1d_tmp;
        reg_2d <= reg_2d_tmp;
        reg_3d <= reg_3d_tmp;
        reg_cpp <= reg_cpp_tmp;
        reg_mixed <= reg_mixed_tmp;
    end

    assign comb_out = comb_comb;

    assign logic_out = logic_comb;

    assign reg_out = reg_comb;


endmodule

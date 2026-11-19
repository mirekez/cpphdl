#!/usr/bin/env python3
"""Emit a latency-parameterized Verilog MAC replacement with both branches.

SYNTHESIS uses generated C++RTL FpAdd/Fp32Mac logic; simulation uses real
arithmetic with the same FP32 contract. Both use the original Verilog wrapper.
"""
from pathlib import Path
import sys


def primitive():
    root=Path(__file__).resolve().parent/'rtl'
    return ('`ifndef CPPHDL_FP32_MAC_RTL\n`define CPPHDL_FP32_MAC_RTL\n`ifdef SYNTHESIS\n'+
            '\n'.join((root/name).read_text() for name in ['FpAdd.sv','Fp32Mac.sv'])+
            '\n`else\n'+(root/'Fp32MacSimulation.sv').read_text()+'\n`endif\n`endif\n')


def pipeline():
    delay=(Path(__file__).resolve().parent/'rtl/EnabledDelay.sv').read_text()
    delay=delay.replace('CpphdlEnabledDelay','CpphdlMacEnabledDelay').replace('CPPHDL_ENABLED_DELAY','CPPHDL_MAC_ENABLED_DELAY')
    return primitive()+delay+r'''
`ifndef CPPHDL_FP32_MAC_PIPELINE
`define CPPHDL_FP32_MAC_PIPELINE
// Spend the first available extra cycle at the arithmetic input boundary.
// A trailing delay cannot separate a context-RAM output from the multiplier.
module CpphdlFp32MacBoundary #(parameter INPUT_STAGE=0, OUTPUT_STAGE=0, NORMALIZE_STAGE=0, PRODUCT_STAGE=0, ALIGN_STAGE=0)(
    input wire clk,reset,en_in,
    input wire [31:0] a_in,b_in,c_in,
    output wire [31:0] result_out
);
    wire [95:0] operands;
    generate if(INPUT_STAGE)begin: registered_input
        (* preserve *) reg [95:0] captured;
        always @(posedge clk)begin
            if(reset)captured<=0;
            else if(en_in)captured<={a_in,b_in,c_in};
        end
        assign operands=captured;
    end else begin: direct_input
        assign operands={a_in,b_in,c_in};
    end endgenerate
    wire [31:0] computed;
    Fp32Mac #(.PIPELINE_ADD(NORMALIZE_STAGE),.PIPELINE_PRODUCT(PRODUCT_STAGE),.PIPELINE_ALIGN(ALIGN_STAGE)) mac(.clk(clk),.reset(reset),.en_in(en_in),
        .a_in(operands[64+:32]),.b_in(operands[32+:32]),.c_in(operands[0+:32]),.result_out(computed));
    generate if(OUTPUT_STAGE)begin: registered_output
        (* preserve *) reg [31:0] captured;
        always @(posedge clk)begin
            if(reset)captured<=0;
            else if(en_in)captured<=computed;
        end
        assign result_out=captured;
    end else begin: direct_output
        assign result_out=computed;
    end endgenerate
endmodule
module Fp32MacPipeline #(parameter LATENCY=2)(
    input wire clk, reset, command_valid_in, response_ready_in,
    input wire [31:0] operation_in, index_in, value_in,
    output wire command_ready_out, response_valid_out,
    output wire [31:0] result_out, fault_out
);
    initial if (LATENCY < 2) $fatal(1, "FP32 MAC requires LATENCY >= 2");
    localparam INPUT_STAGE=(LATENCY>2), NORMALIZE_STAGE=(LATENCY>3), OUTPUT_STAGE=(LATENCY>4), PRODUCT_STAGE=(LATENCY>5), ALIGN_STAGE=(LATENCY>6);
    localparam CORE_LATENCY=2+INPUT_STAGE+NORMALIZE_STAGE+OUTPUT_STAGE+PRODUCT_STAGE+ALIGN_STAGE;
    reg [LATENCY-1:0] valid;
    wire advance = !valid[LATENCY-1] || response_ready_in;
    wire [31:0] computed;
    CpphdlFp32MacBoundary #(.INPUT_STAGE(INPUT_STAGE),.OUTPUT_STAGE(OUTPUT_STAGE),.NORMALIZE_STAGE(NORMALIZE_STAGE),.PRODUCT_STAGE(PRODUCT_STAGE),.ALIGN_STAGE(ALIGN_STAGE)) mac(.clk(clk),.reset(reset),.en_in(advance),
        .a_in(index_in),.b_in(value_in),.c_in(operation_in),.result_out(computed));
    assign command_ready_out=advance;
    assign response_valid_out=valid[LATENCY-1];
    assign fault_out=0;
    integer i;
    always @(posedge clk) begin
        if(reset)valid<=0;
        else if(advance)begin
            valid[0]<=command_valid_in;
            for(i=1;i<LATENCY;i=i+1)valid[i]<=valid[i-1];
        end
    end
    CpphdlMacEnabledDelay #(.WIDTH(32),.DEPTH(LATENCY-CORE_LATENCY)) delay_result(
        .clk(clk),.reset(reset),.advance(advance),.data_in(computed),.data_out(result_out));
endmodule
`endif

'''

def array(rows,cols):
    if not 1<=rows<=64 or not 1<=cols<=64:raise ValueError('MAC dimensions must be in 1..64')
    lanes=rows*cols;words=2*lanes+rows+cols+4
    return pipeline()+f'''
module Fp32MacArray_{rows}_{cols} #(parameter LATENCY=2)(
    input wire clk,reset,command_valid_in,response_ready_in,
    input wire [{words*32-1}:0] operation_in,index_in,value_in,
    output wire command_ready_out,response_valid_out,
    output wire [{(lanes+1)*32-1}:0] result_out,output wire [31:0] fault_out
);
    initial if(LATENCY<2)$fatal(1,"FP32 MAC array requires LATENCY >= 2");
    localparam INPUT_STAGE=(LATENCY>2), NORMALIZE_STAGE=(LATENCY>3), OUTPUT_STAGE=(LATENCY>4), PRODUCT_STAGE=(LATENCY>5), ALIGN_STAGE=(LATENCY>6);
    localparam CORE_LATENCY=2+INPUT_STAGE+NORMALIZE_STAGE+OUTPUT_STAGE+PRODUCT_STAGE+ALIGN_STAGE;
    // Mask at the arithmetic boundary, not after LATENCY cycles. Only the
    // selected payload and its tag occupy the deep, shared RAM delay.
    reg [31:0] tags[0:CORE_LATENCY-1];reg [{lanes-1}:0] masks[0:CORE_LATENCY-1];
    reg [{lanes*32-1}:0] initials[0:CORE_LATENCY-1];
    reg [LATENCY-1:0] valid;
    wire advance=!valid[LATENCY-1] || response_ready_in;
    wire [{lanes-1}:0] mask_in;
    wire [{(lanes+1)*32-1}:0] selected;
    assign command_ready_out=advance;assign response_valid_out=valid[LATENCY-1];
    assign selected[{lanes*32}+:32]=tags[CORE_LATENCY-1];assign fault_out=0;
    CpphdlMacEnabledDelay #(.WIDTH({(lanes+1)*32}),.DEPTH(LATENCY-CORE_LATENCY)) delay_result(
        .clk(clk),.reset(reset),.advance(advance),.data_in(selected),.data_out(result_out));
    genvar row,col;
    generate for(row=0;row<{rows};row=row+1)begin: rows
        for(col=0;col<{cols};col=col+1)begin: columns
            localparam LANE=row*{cols}+col;
            wire [31:0] computed;
            wire [31:0] a=operation_in[{(words-1)*32}+:32]==0 ?
                operation_in[{(2*lanes+cols)*32}+row*32+:32] : operation_in[LANE*32+:32];
            CpphdlFp32MacBoundary #(.INPUT_STAGE(INPUT_STAGE),.OUTPUT_STAGE(OUTPUT_STAGE),.NORMALIZE_STAGE(NORMALIZE_STAGE),.PRODUCT_STAGE(PRODUCT_STAGE),.ALIGN_STAGE(ALIGN_STAGE)) mac(.clk(clk),.reset(reset),.en_in(advance),
                .c_in(operation_in[{(lanes+cols)*32}+LANE*32+:32]),.a_in(a),
                .b_in(operation_in[{lanes*32}+col*32+:32]),.result_out(computed));
            assign mask_in[LANE]=(row<operation_in[{(words-4)*32}+:32] && col<operation_in[{(words-3)*32}+:32]);
            assign selected[LANE*32+:32]=masks[CORE_LATENCY-1][LANE] ? computed : initials[CORE_LATENCY-1][LANE*32+:32];
        end
    end endgenerate
    integer i;
    always @(posedge clk)begin
        if(reset)valid<=0;
        else if(advance)begin
            valid[0]<=command_valid_in;
            for(i=1;i<LATENCY;i=i+1)valid[i]<=valid[i-1];
            tags[0]<=operation_in[{(words-2)*32}+:32];masks[0]<=mask_in;
            initials[0]<=operation_in[{(lanes+cols)*32}+:{lanes*32}];
            for(i=1;i<CORE_LATENCY;i=i+1)begin
                tags[i]<=tags[i-1];masks[i]<=masks[i-1];initials[i]<=initials[i-1];
            end
        end
    end
endmodule
'''

if __name__=='__main__':
    if len(sys.argv)==1:print(pipeline())
    elif len(sys.argv)==3:print(array(int(sys.argv[1]),int(sys.argv[2])))
    else:raise SystemExit('usage: generate_mac_replacement.py [rows cols]')

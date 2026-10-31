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
    return primitive()+r'''
`ifndef CPPHDL_FP32_MAC_PIPELINE
`define CPPHDL_FP32_MAC_PIPELINE
module Fp32MacPipeline #(parameter LATENCY=2)(
    input wire clk, reset, command_valid_in, response_ready_in,
    input wire [31:0] operation_in, index_in, value_in,
    output wire command_ready_out, response_valid_out,
    output wire [31:0] result_out, fault_out
);
    initial if (LATENCY < 2) $fatal(1, "FP32 MAC requires LATENCY >= 2");
    reg [LATENCY-1:0] valid;
    wire advance = !valid[LATENCY-1] || response_ready_in;
    wire [31:0] computed;
    Fp32Mac mac(.clk(clk),.reset(reset),.en_in(advance),
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
    generate if(LATENCY==2)begin
        assign result_out=computed;
    end else if(LATENCY>2)begin
        reg [31:0] delay_data [0:LATENCY-3];
        integer j;
        always @(posedge clk) begin
            if(reset)for(j=0;j<LATENCY-2;j=j+1)delay_data[j]<=0;
            else if(advance)begin
                delay_data[0]<=computed;
                for(j=1;j<LATENCY-2;j=j+1)delay_data[j]<=delay_data[j-1];
            end
        end
        assign result_out=delay_data[LATENCY-3];
    end endgenerate
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
    reg [31:0] tags[0:LATENCY-1];reg [{lanes-1}:0] masks[0:LATENCY-1];
    reg [{lanes*32-1}:0] initials[0:LATENCY-1];
    wire advance=command_ready_out;wire [{lanes-1}:0] mask_in;
    assign result_out[{lanes*32}+:32]=tags[LATENCY-1];assign fault_out=0;
    genvar row,col;
    generate for(row=0;row<{rows};row=row+1)begin: rows
        for(col=0;col<{cols};col=col+1)begin: columns
            localparam LANE=row*{cols}+col;
            wire [31:0] computed;wire ready,valid;
            wire [31:0] a=operation_in[{(words-1)*32}+:32]==0 ?
                operation_in[{(2*lanes+cols)*32}+row*32+:32] : operation_in[LANE*32+:32];
            Fp32MacPipeline #(.LATENCY(LATENCY)) mac(
                .clk(clk),.reset(reset),.command_valid_in(command_valid_in),.response_ready_in(response_ready_in),
                .operation_in(operation_in[{(lanes+cols)*32}+LANE*32+:32]),.index_in(a),
                .value_in(operation_in[{lanes*32}+col*32+:32]),.command_ready_out(ready),
                .response_valid_out(valid),.result_out(computed),.fault_out());
            assign mask_in[LANE]=(row<operation_in[{(words-4)*32}+:32] && col<operation_in[{(words-3)*32}+:32]);
            assign result_out[LANE*32+:32]=masks[LATENCY-1][LANE] ? computed : initials[LATENCY-1][LANE*32+:32];
            if(row==0 && col==0)begin assign command_ready_out=ready;assign response_valid_out=valid;end
        end
    end endgenerate
    integer i;
    always @(posedge clk)begin
        if(reset)begin for(i=0;i<LATENCY;i=i+1)begin tags[i]<=0;masks[i]<=0;initials[i]<=0;end end
        else if(advance)begin
            tags[0]<=operation_in[{(words-2)*32}+:32];masks[0]<=mask_in;
            initials[0]<=operation_in[{(lanes+cols)*32}+:{lanes*32}];
            for(i=1;i<LATENCY;i=i+1)begin tags[i]<=tags[i-1];masks[i]<=masks[i-1];initials[i]<=initials[i-1];end
        end
    end
endmodule
'''

if __name__=='__main__':
    if len(sys.argv)==1:print(pipeline())
    elif len(sys.argv)==3:print(array(int(sys.argv[1]),int(sys.argv[2])))
    else:raise SystemExit('usage: generate_mac_replacement.py [rows cols]')

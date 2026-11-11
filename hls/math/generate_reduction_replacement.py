#!/usr/bin/env python3
"""Generate a stallable FP32 pairwise reduction, with native simulation math."""
from pathlib import Path
import sys


def minimum_latency(cols):
    if not 1 <= cols <= 64:
        raise ValueError('Reduction columns must be in 1..64')
    return (cols-1).bit_length()+1  # Includes the reference's final +0.


def primitive():
    rtl=Path(__file__).resolve().parent/'rtl'
    # A distinct hardware name permits linking independently generated MAC and
    # reduction replacements without duplicate FpAdd definitions.
    hardware=(rtl/'FpAdd.sv').read_text().replace('module FpAdd #(', 'module Fp32ReductionAdderHardware #(')
    return '''`ifndef CPPHDL_FP32_REDUCTION_ADD
`define CPPHDL_FP32_REDUCTION_ADD
`ifdef SYNTHESIS
'''+hardware+r'''
module Fp32ReductionAdd(input wire clk,reset,en_in,
    input wire [31:0] a_in,b_in, output wire [31:0] result_out);
    wire [31:0] raw_result;
    Fp32ReductionAdderHardware #(.W(32),.EW(8)) adder(
        .clk(clk),.reset(reset),.en_in(en_in),.a_in(a_in),.b_in(b_in),.c_out(raw_result));
    assign result_out=(raw_result[30:23]==255 && raw_result[22:0]!=0) ? 32'h7fc00000 : raw_result;
endmodule
`else
'''+(rtl/'Fp32ReductionAddSimulation.sv').read_text()+r'''
`endif
`endif
'''


def reduction(rows,cols):
    if not 1 <= rows <= 64:
        raise ValueError('Reduction rows must be in 1..64')
    stages=minimum_latency(cols);words=rows*cols+1
    delay=(Path(__file__).resolve().parent/'rtl/EnabledDelay.sv').read_text()
    delay=delay.replace('CpphdlEnabledDelay','CpphdlReductionEnabledDelay').replace('CPPHDL_ENABLED_DELAY','CPPHDL_REDUCTION_ENABLED_DELAY')
    body=[primitive(),delay,f'''
module Fp32Reduction_{rows}_{cols} #(parameter LATENCY={stages})(
    input wire clk,reset,command_valid_in,response_ready_in,
    input wire [{words*32-1}:0] operation_in,index_in,value_in,
    output wire command_ready_out,response_valid_out,
    output wire [{(rows+1)*32-1}:0] result_out,output wire [31:0] fault_out);
    initial if(LATENCY<{stages})$fatal(1,"FP32 reduction LATENCY is smaller than its addition tree");
    reg [LATENCY-1:0] valid;
    reg [31:0] tags[0:{stages-1}];
    wire [{(rows+1)*32-1}:0] selected;
    wire advance=!valid[LATENCY-1] || response_ready_in;
    assign command_ready_out=advance;
    assign response_valid_out=valid[LATENCY-1];
    assign fault_out=0;
    assign selected[{rows*32}+:32]=tags[{stages-1}];
    CpphdlReductionEnabledDelay #(.WIDTH({(rows+1)*32}),.DEPTH(LATENCY-{stages})) delay_result(
        .clk(clk),.reset(reset),.advance(advance),.data_in(selected),.data_out(result_out));
    integer tag_index;
    always @(posedge clk)begin
        if(reset)valid<=0;
        else if(advance)begin
            valid[0]<=command_valid_in;tags[0]<=operation_in[{rows*cols*32}+:32];
            for(tag_index=1;tag_index<LATENCY;tag_index=tag_index+1)begin
                valid[tag_index]<=valid[tag_index-1];
            end
            for(tag_index=1;tag_index<{stages};tag_index=tag_index+1)tags[tag_index]<=tags[tag_index-1];
        end
    end
''']
    for row in range(rows):
        inputs=[f'operation_in[{32*(row*cols+c)}+:32]' for c in range(cols)]
        level=0
        while len(inputs)>1:
            outputs=[]
            for col in range(0,len(inputs),2):
                name=f'row{row}_level{level}_{col//2}';outputs.append(name)
                if col+1<len(inputs):
                    body.append(f'wire [31:0] {name}; Fp32ReductionAdd {name}_add(.clk(clk),.reset(reset),.en_in(advance),.a_in({inputs[col]}),.b_in({inputs[col+1]}),.result_out({name}));\n')
                else:
                    body.append(f'reg [31:0] {name}; always @(posedge clk)begin if(reset){name}<=0;else if(advance){name}<={inputs[col]};end\n')
            inputs=outputs;level+=1
        body.append(f'''wire [31:0] row{row}_sum;
Fp32ReductionAdd row{row}_identity(.clk(clk),.reset(reset),.en_in(advance),.a_in(32'b0),.b_in({inputs[0]}),.result_out(row{row}_sum));
assign selected[{row*32}+:32]=row{row}_sum;
''')
    body.append('endmodule\n')
    return ''.join(body)


if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('usage: generate_reduction_replacement.py ROWS COLS')
    print(reduction(int(sys.argv[1]),int(sys.argv[2])))

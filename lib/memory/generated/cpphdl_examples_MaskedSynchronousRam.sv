
module cpphdl_examples_MaskedSynchronousRam #(parameter WORDS=16,parameter DEPTH=128)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS-1:0] write_mask_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 genvar lane;
 generate for(lane=0;lane<WORDS;lane=lane+1) begin: lanes
   (* ramstyle="M20K", ram_style="block" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane])words[write_address_in]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=words[read_address_in];
     end
   end
 end endgenerate
endmodule

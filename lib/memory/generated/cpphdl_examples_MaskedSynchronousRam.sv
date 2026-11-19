
module cpphdl_examples_MaskedSynchronousRam #(parameter WORDS=16,parameter DEPTH=128,parameter BLOCK=0)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS-1:0] write_mask_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 // Explicit physical indices keep Quartus from expanding the masked banks
 // into registers. The guards preserve invalid-write and invalid-read behavior.
 localparam ADDRESS_BITS=DEPTH>1?$clog2(DEPTH):1;
 wire [ADDRESS_BITS-1:0] read_index=read_address_in[ADDRESS_BITS-1:0];
 wire [ADDRESS_BITS-1:0] write_index=write_address_in[ADDRESS_BITS-1:0];
 genvar lane;
 generate for(lane=0;lane<WORDS;lane=lane+1) begin: lanes
 if(DEPTH<=64 && !BLOCK) begin: shallow
   (* ramstyle="MLAB", ram_style="distributed" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane] && write_address_in<DEPTH)words[write_index]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=read_address_in<DEPTH?words[read_index]:32'bx;
     end
   end
 end else begin: deep
   (* ramstyle="M20K", ram_style="block" *) reg [31:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in && write_mask_in[lane] && write_address_in<DEPTH)words[write_index]<=data_in[lane*32+:32];
       if(read_in)data_out[lane*32+:32]<=read_address_in<DEPTH?words[read_index]:32'bx;
     end
   end
 end
 end endgenerate
endmodule

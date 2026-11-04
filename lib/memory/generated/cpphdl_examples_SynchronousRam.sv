
module cpphdl_examples_SynchronousRam #(parameter WORDS=1,parameter DEPTH=64)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 (* ramstyle="M20K", ram_style="block" *) reg [WORDS*32-1:0] words[0:DEPTH-1];
 always @(posedge clk) begin
   if(!reset) begin
     if(write_in)words[write_address_in]<=data_in;
     if(read_in)data_out<=words[read_address_in];
   end
 end
endmodule

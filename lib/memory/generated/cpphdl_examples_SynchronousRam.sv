
module cpphdl_examples_SynchronousRam #(parameter WORDS=1,parameter DEPTH=64,parameter BLOCK=0)(
 input wire clk,reset,input wire read_in,write_in,
 input wire [31:0] read_address_in,write_address_in,
 input wire [WORDS*32-1:0] data_in,output reg [WORDS*32-1:0] data_out
);
 // Small banks default to MLAB; wide or heavily banked caches can explicitly
 // use M20K to keep their storage and output registers out of logic fabric.
 generate if(DEPTH<=64 && !BLOCK) begin: shallow
   (* ramstyle="MLAB", ram_style="distributed" *) reg [WORDS*32-1:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in)words[write_address_in]<=data_in;
       if(read_in)data_out<=words[read_address_in];
     end
   end
 end else begin: deep
   (* ramstyle="M20K", ram_style="block" *) reg [WORDS*32-1:0] words[0:DEPTH-1];
   always @(posedge clk) begin
     if(!reset) begin
       if(write_in)words[write_address_in]<=data_in;
       if(read_in)data_out<=words[read_address_in];
     end
   end
 end endgenerate
endmodule

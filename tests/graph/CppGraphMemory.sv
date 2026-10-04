module CppGraphMemory(
    input logic clk_i,
    input logic rst_ni,
    input logic [1:0] req_i,
    input logic [1:0] we_i,
    input logic [1:0][3:0] addr_i,
    input logic [1:0][63:0] wdata_i,
    input logic [1:0][7:0] be_i,
    output logic [1:0][63:0] rdata_o
);
  logic [63:0] sram [15:0];
  logic [1:0][3:0] retained;
  logic [1:0][0:0][63:0] rdata_q, rdata_d;
  always_comb begin
    for (int unsigned port = 0; port < 2; ++port) begin
      rdata_o[port] = rdata_q[port][0];
      rdata_d[port][0] = (req_i[port] && !we_i[port]) ? sram[addr_i[port]] : sram[retained[port]];
    end
  end
  always_ff @(posedge clk_i) begin
    if (!rst_ni) retained <= '0;
    else begin
      for (int unsigned port = 0; port < 2; ++port) begin
        rdata_q[port][0] <= rdata_d[port][0];
        if (req_i[port]) begin
          if (we_i[port]) begin
            for (int unsigned lane = 0; lane < 8; ++lane)
              if (be_i[port][lane]) sram[addr_i[port]][lane*8+:8] <= wdata_i[port][lane*8+:8];
          end else retained[port] <= addr_i[port];
        end
      end
    end
  end
endmodule

module RegfileBench (
    input logic clk_i,
    input logic rst_ni,
    input logic [9:0] read_addresses,
    input logic [9:0] write_addresses,
    input logic [63:0] write_data,
    input logic [1:0] enables,
    output logic [63:0] read_data
);
    function automatic config_pkg::cva6_cfg_t configuration();
        configuration = '0;
        configuration.NrCommitPorts = 2;
    endfunction
    localparam config_pkg::cva6_cfg_t Cfg = configuration();
    ariane_regfile #(.CVA6Cfg(Cfg), .DATA_WIDTH(32), .NR_READ_PORTS(2), .ZERO_REG_ZERO(1'b1)) dut (
        .clk_i(clk_i), .rst_ni(rst_ni), .test_en_i(1'b0),
        .raddr_i(read_addresses), .waddr_i(write_addresses),
        .wdata_i(write_data), .we_i(enables), .rdata_o(read_data)
    );
endmodule

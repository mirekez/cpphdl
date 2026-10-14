module TypedefBlockingBank (
    input logic clk,
    input logic we,
    input logic [3:0] addr,
    input logic [127:0] wdata,
    output logic [127:0] rdata
);
    typedef logic [127:0] bank_t [16];
    bank_t bank;
    always_ff @(posedge clk)
        if (we) bank[addr] = wdata;
    assign rdata = bank[addr];
endmodule

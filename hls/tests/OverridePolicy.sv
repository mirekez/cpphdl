module OverridePolicyChecks;
    logic clk = 0, reset = 0, valid = 0, ready = 0;
    logic [31:0] op = 0, index = 0, value = 0;
    wire command_ready, response_valid;
    wire [63:0] result;
    wire [31:0] fault;
    `MEMORY_DUT dut(.clk(clk), .reset(reset), .command_valid_in(valid),
        .operation_in(op), .index_in(index), .value_in(value),
        .response_ready_in(ready), .command_ready_out(command_ready),
        .response_valid_out(response_valid), .result_out(result), .fault_out(fault));
    integer elapsed;
    task tick;
        clk = 0; #1; clk = 1; #1; clk = 0; #1;
    endtask
    task check(input logic [31:0] operation, a, b, expected_fault,
               input logic [63:0] expected);
        reset = 1; valid = 0; ready = 0; tick(); reset = 0;
        elapsed = 0;
        while (!command_ready && elapsed < 200) begin tick(); elapsed++; end
        if (!command_ready || fault) $fatal(1, "policy reset failed");
        op = operation; index = a; value = b; valid = 1; tick(); valid = 0;
        index = 99; value = 99;
        elapsed = 0;
        while (!response_valid && elapsed < 200) begin tick(); elapsed++; end
        if (!response_valid || fault != expected_fault || (!fault && result != expected))
            $fatal(1, "policy op=%0d a=%h b=%h fault=%0d result=%h", operation, a, b, fault, result);
        if (expected_fault) begin
            ready = 1; tick(); ready = 0;
            repeat (3) begin tick(); if (command_ready || fault != 1) $fatal(1, "fault must remain sticky"); end
        end
    endtask
    initial begin
        check(0, 32'h40400000, 32'h40a00000, 0, 32'h41700000); // 3 * 5
        check(0, 0, 32'h3f800000, 0, 0);
        check(1, 32'h41000000, 32'h3f800000, 0, 32'h41000000);
        check(1, 32'h41000000, 32'h40000000, 1, 0); // load factor other than 1
        check(1, 32'h3fc00000, 32'h3f800000, 1, 0); // fractional size
        check(1, 32'hbf800000, 32'h3f800000, 1, 0); // negative size
        check(1, 32'h7fc00000, 32'h3f800000, 1, 0); // NaN
        check(2, 32'h1000000, 0, 0, 32'h4b800000);
        check(2, 32'h1000001, 0, 1, 0);
        check(3, 0, 0, 0, 0);
        check(3, 4096, 0, 0, 4099);
        check(3, 4097, 0, 1, 0);
        $display("override policy boundaries, faults and reset recovery passed");
        $finish;
    end
endmodule

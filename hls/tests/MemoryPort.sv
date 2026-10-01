module MemoryPortChecks;
    logic clk = 0, reset = 0, valid = 0, ready = 0;
    logic [31:0] op = 0, index = 0, value = 0;
    wire command_ready, response_valid;
    wire [63:0] result;
    wire [31:0] fault;
    `MEMORY_DUT dut(.clk(clk), .reset(reset), .command_valid_in(valid),
        .operation_in(op), .index_in(index), .value_in(value),
        .response_ready_in(ready), .command_ready_out(command_ready),
        .response_valid_out(response_valid), .result_out(result), .fault_out(fault));
    logic [`MEMORY_BYTES*8-1:0] before_fault;
    integer elapsed;

    function static logic [`MEMORY_BYTES*8-1:0] contents();
`ifdef HLS_BLOCK_RAM
        for (integer row = 0; row < `MEMORY_BYTES / 8; row = row + 1) begin
            contents[row*8 +: 8] = dut.storage_lane0_ram.data[row];
            contents[`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane1_ram.data[row];
            contents[2*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane2_ram.data[row];
            contents[3*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane3_ram.data[row];
            contents[4*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane4_ram.data[row];
            contents[5*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane5_ram.data[row];
            contents[6*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane6_ram.data[row];
            contents[7*`MEMORY_BYTES + row*8 +: 8] = dut.storage_lane7_ram.data[row];
        end
`else
        contents = {dut.storage_lane7_reg, dut.storage_lane6_reg,
            dut.storage_lane5_reg, dut.storage_lane4_reg, dut.storage_lane3_reg,
            dut.storage_lane2_reg, dut.storage_lane1_reg, dut.storage_lane0_reg};
`endif
    endfunction
    task tick;
        clk = 0; #1; clk = 1; #1; clk = 0; #1;
    endtask
    task restart;
        valid = 0; ready = 0; reset = 1; tick(); reset = 0;
        elapsed = 0;
        while (!command_ready && elapsed < 100) begin tick(); elapsed = elapsed + 1; end
        if (!command_ready || fault || response_valid) $fatal(1, "reset recovery failed");
    endtask
    task send(input logic [31:0] operation, address, data);
        if (!command_ready) $fatal(1, "command not ready");
        op = operation; index = address; value = data; valid = 1; tick(); valid = 0;
        op = 99; index = 0; value = 0;
        elapsed = 0;
        while (!response_valid && elapsed < 100) begin tick(); elapsed = elapsed + 1; end
        if (!response_valid) $fatal(1, "command timed out");
    endtask
    task consume;
        ready = 1; tick(); ready = 0;
    endtask
    initial begin
        restart();
        send(0, 17, 29);
        if (fault || result != 46) $fatal(1, "wide store/read mismatch");
        repeat (3) begin tick(); if (result != 46 || !response_valid) $fatal; end
        consume();
        before_fault = contents();
        // A 16-byte store with only eight bytes left must write nothing.
        send(2, `MEMORY_BYTES - 8, 123);
        if (fault != 3 || contents() !== before_fault) $fatal(1, "partial invalid wide write");
        consume();
        op = 0; index = 33; value = 44; valid = 1;
        repeat (3) tick();
        if (command_ready || fault != 3 || contents() !== before_fault) $fatal(1, "fault is not sticky");
        restart();
        send(0, 7, 11); consume();
        before_fault = contents();
        send(3, 0, 0);
        if (fault != 3 || contents() !== before_fault) $fatal(1, "invalid read modified memory");
        restart();
        send(0, 101, 103);
        if (fault || result != 204) $fatal(1, "recovery transaction failed");
        $display("shared port wide accesses, bounds, fault ordering and recovery passed");
        $finish;
    end
endmodule

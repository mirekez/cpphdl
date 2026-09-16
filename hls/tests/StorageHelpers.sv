module StorageHelpers;
    localparam int MEM_BYTES = `HLS_STORAGE_BYTES;
    typedef logic [7:0] probe_t [MEM_BYTES];
    probe_t probe, expected;
    logic [31:0] access_fault;
    logic [63:0] address, value;
    logic [127:0] wide_value;

    cpphdl_hls_ClockedReuseMethods dut (
        .clk(1'b0), .reset(1'b0), .command_valid_in(1'b0),
        .operation_in(32'd0), .index_in(32'd0), .value_in(32'd0),
        .command_ready_out(), .response_ready_in(1'b0),
        .response_valid_out(), .result_out(), .fault_out()
    );

    task static check_bytes(input probe_t probe, input probe_t expected);
        for (int unsigned i = 0; i < MEM_BYTES; ++i)
            if (probe[i] !== expected[i])
                $fatal(1, "storage byte %0d: got %h expected %h", i, probe[i], expected[i]);
    endtask

    initial begin
        for (int unsigned i = 0; i < MEM_BYTES; ++i) begin
            probe[i] = 8'(i ^ 8'ha5);
            expected[i] = probe[i];
        end
        access_fault = 0;
        dut.hls_storage_write_8(probe, 17, 8'hc3, access_fault);
        expected[17] = 8'hc3;
        if (dut.hls_storage_read_8(probe, 17, access_fault) !== 8'hc3) $fatal(1, "8-bit access");
        dut.hls_storage_write_16(probe, 19, 16'hb123, access_fault);
        expected[19] = 8'h23;
        expected[20] = 8'hb1;
        if (dut.hls_storage_read_16(probe, 19, access_fault) !== 16'hb123) $fatal(1, "16-bit access");
        dut.hls_storage_write_32(probe, 23, 32'h87654321, access_fault);
        for (int unsigned i = 0; i < 4; ++i) expected[23+i] = 8'(32'h87654321 >> (i*8));
        if (dut.hls_storage_read_32(probe, 23, access_fault) !== 32'h87654321) $fatal(1, "32-bit access");
        dut.hls_storage_write_64(probe, 29, 64'hfedcba9876543210, access_fault);
        for (int unsigned i = 0; i < 8; ++i) expected[29+i] = 8'(64'hfedcba9876543210 >> (i*8));
        if (dut.hls_storage_read_64(probe, 29, access_fault) !== 64'hfedcba9876543210) $fatal(1, "64-bit access");
        check_bytes(probe, expected);

        wide_value = 128'hfedcba9876543210_0123456789abcdef;
        dut.hls_storage_write_128(probe, 64, wide_value, access_fault);
        dut.hls_storage_write_128(probe, 68, dut.hls_storage_read_128(probe, 64, access_fault), access_fault);
        for (int unsigned i = 0; i < 16; ++i) expected[64+i] = 8'(wide_value >> (i*8));
        for (int unsigned i = 0; i < 16; ++i) expected[68+i] = 8'(wide_value >> (i*8));
        if (dut.hls_storage_read_128(probe, 68, access_fault) !== wide_value) $fatal(1, "overlapping copy");
        check_bytes(probe, expected);

        // The write overwrites its own saved pointer: snapshot the address once.
        dut.hls_storage_write_64(probe, 32, 64'd32, access_fault);
        dut.hls_storage_write_64(probe, dut.hls_storage_load_64(probe, 32), 64'hdeadbeef01234567, access_fault);
        for (int unsigned i = 0; i < 8; ++i) expected[32+i] = 8'(64'hdeadbeef01234567 >> (i*8));
        check_bytes(probe, expected);

        dut.hls_storage_write_64(probe, 64'(MEM_BYTES - 8), 64'h123456789abcdef0, access_fault);
        for (int unsigned i = 0; i < 8; ++i) expected[MEM_BYTES-8+i] = 8'(64'h123456789abcdef0 >> (i*8));
        if (dut.hls_storage_read_64(probe, 64'(MEM_BYTES-8), access_fault) !== 64'h123456789abcdef0)
            $fatal(1, "last valid address");
        if (access_fault != 0) $fatal(1, "valid access fault");
        check_bytes(probe, expected);

        for (int unsigned test_id = 0; test_id < 4; ++test_id) begin
            case (test_id)
                0: address = 15;
                1: address = 64'(MEM_BYTES - 7);
                2: address = 64'hffffffffffffffff;
                3: address = 64'h100000020;
            endcase
            access_fault = 0;
            dut.hls_storage_write_64(probe, address, '1, access_fault);
            if (access_fault != 3) $fatal(1, "invalid write not rejected");
            check_bytes(probe, expected);
            access_fault = 0;
            value = dut.hls_storage_read_64(probe, address, access_fault);
            if (access_fault != 3 || value != 0) $fatal(1, "invalid read not rejected");
        end
        access_fault = 5;
        dut.hls_storage_write_64(probe, 32, '1, access_fault);
        value = dut.hls_storage_read_64(probe, 32, access_fault);
        if (access_fault != 5 || value != 0) $fatal(1, "prior fault not preserved");
        check_bytes(probe, expected);
        $display("storage helper widths, byte order, bounds, overlap and fault checks passed");
        $finish;
    end
endmodule

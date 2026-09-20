// Verification-only replacement for the asynchronous read. The collision is
// with the write sampled at the last edge, not the writer's new selector.
// Preserve stored data so a later non-colliding read remains meaningful.
reg rdw_write = 0;
reg [$clog2(2*SIZE)-1:0] rdw_address = 0;
bit rdw_exercised = 0;
always @(posedge clk) begin
    rdw_write <= write_enable_in;
    rdw_address <= write_address_in;
end
wire rdw_collision = rdw_write && read_address_in == rdw_address;
// Use inversion rather than X, since Verilator simulates two-state values.
assign read_data_out = rdw_collision ? ~storage[read_address_in]
                                     : storage[read_address_in];
// Observe after the controller and write registers have updated. Every lane
// must encounter a collision, even though no valid output may depend on it.
always @(negedge clk) begin
    if (rdw_collision && !rdw_exercised) begin
        $display("MLAB_RDW_POISON_EXERCISED SIZE=%0d; lane=%m", SIZE);
        rdw_exercised = 1;
    end
end

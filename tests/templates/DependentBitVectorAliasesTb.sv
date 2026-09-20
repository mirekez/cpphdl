module DependentBitVectorAliasesTb;
    DependentBitVectorAliases defaults (.clk(1'b0), .reset(1'b0));
    DependentBitVectorAliases #(.AXI_DATA_WIDTH(32)) narrow (.clk(1'b0), .reset(1'b0));
    DependentBitVectorAliases #(.AXI_DATA_WIDTH(128)) wide (.clk(1'b0), .reset(1'b0));
    initial begin
        #1;
        if (defaults.AXI_DATA_WIDTH != 64 || narrow.AXI_DATA_WIDTH != 32 ||
            wide.AXI_DATA_WIDTH != 128)
            $fatal(1, "Module default or width override was lost");
        $display("Dependent aliases passed: default 64, overrides 32 and 128");
        $finish;
    end
endmodule

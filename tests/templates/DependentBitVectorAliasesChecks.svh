// Test-only instrumentation in the generated module's lexical scope.
// The simulator cannot resolve local typedefs in bind parameter expressions.
initial begin
    if ($bits(data_t) != AXI_DATA_WIDTH ||
        $bits(strb_t) != AXI_DATA_WIDTH / 8 ||
        $bits(expr_t) != AXI_DATA_WIDTH / 8 ||
        $bits(chained_t) != AXI_DATA_WIDTH ||
        $bits(unsigned_t) != AXI_DATA_WIDTH / 8)
        $fatal(1, "Dependent alias width mismatch for AXI_DATA_WIDTH=%0d", AXI_DATA_WIDTH);
end

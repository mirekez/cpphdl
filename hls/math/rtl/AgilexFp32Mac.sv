// Target implementation of the small annotated FP32 MAC, not an HLS engine.
// Five enabled DSP clocks. The wrapper supplies input/output boundary registers
// and preserves the HLS-selected total latency. UG-683037 multiply-add mode
// rounds the multiplication before addition; chainin is disabled.
`ifdef CPPHDL_TARGET_AGILEX7
module CpphdlAgilexFp32Mac(
    input wire clk, advance,
    input wire [31:0] a, b, c,
    output wire [31:0] result
);
    wire [31:0] raw_result;
    tennm_fp_mac #(
        .operation_mode("fp32_mult_add"), .use_chainin("false"), .clear_type("none"),
        .fp32_mult_a_clken("0"), .fp32_mult_b_clken("0"),
        .mult_pipeline_clken("0"), .mult_2nd_pipeline_clken("0"),
        .fp32_adder_a_clken("0"), .fp32_adder_a_chainin_pl_clken("0"),
        .fp32_adder_a_chainin_2nd_pl_clken("0"),
        .adder_input_clken("0"), .output_clken("0")
    ) dsp (
        .clk(clk), .ena({3{advance}}), .clr(2'b0),
        .fp32_mult_a(a), .fp32_mult_b(b), .fp32_adder_a(c),
        .fp32_result(raw_result)
    );
    // Match cpphdl's signed FTZ and canonical arithmetic NaN contract.
    assign result = raw_result[30:23] == 0 ? {raw_result[31],31'b0} :
        (&raw_result[30:23] && |raw_result[22:0]) ? 32'h7fc00000 : raw_result;
endmodule
`endif

`default_nettype none
module ReplacementParameterCheck;
    wire [31:0] inherited_value;
    wire [31:0] default_value;

    ArithmeticParent #(19, 5, 7) overridden (
        .clk(1'b0), .reset(1'b0), .value_out(inherited_value)
    );
    ArithmeticTop defaults (
        .clk(1'b0), .reset(1'b0), .value_out(default_value)
    );

    initial begin
        #1;
        if (inherited_value !== 32'd31)
            $fatal(1, "Child did not receive parent parameter overrides: %0d", inherited_value);
        if (default_value !== 32'd22)
            $fatal(1, "Child did not receive concrete C++ arguments: %0d", default_value);
        $finish;
    end
endmodule

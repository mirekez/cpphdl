module opaque_a #(parameter INPUT_BITS=128, OUTPUT_BITS=16)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    assign result = OUTPUT_BITS'(args[63:0] + args[127:64]);
endmodule
module opaque_c #(parameter INPUT_BITS=128, OUTPUT_BITS=16)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    assign result = OUTPUT_BITS'(args[63:0] * 5 + args[127:64]);
endmodule
module opaque_b #(parameter INPUT_BITS=128, OUTPUT_BITS=16)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    assign result = OUTPUT_BITS'(args[63:0] - args[127:64]);
endmodule

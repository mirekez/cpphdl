// Functional simulation only. Hardware users supply real implementations.
module llm_q48_divide #(parameter INPUT_BITS=128, OUTPUT_BITS=64)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    import "DPI-C" function longint unsigned llm_model_divide(input longint unsigned a, b);
    assign result = llm_model_divide(args[63:0], args[127:64]);
endmodule
module llm_q48_silu #(parameter INPUT_BITS=64, OUTPUT_BITS=64)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    import "DPI-C" function longint unsigned llm_model_silu(input longint unsigned a);
    assign result = llm_model_silu(args[63:0]);
endmodule
module llm_q48_exp_negative #(parameter INPUT_BITS=64, OUTPUT_BITS=64)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    import "DPI-C" function longint unsigned llm_model_exp(input longint unsigned a);
    assign result = llm_model_exp(args[63:0]);
endmodule
module llm_q48_inverse_sqrt #(parameter INPUT_BITS=64, OUTPUT_BITS=64)
    (input wire [INPUT_BITS-1:0] args, output wire [OUTPUT_BITS-1:0] result);
    import "DPI-C" function longint unsigned llm_model_inverse_sqrt(input longint unsigned a);
    assign result = llm_model_inverse_sqrt(args[63:0]);
endmodule

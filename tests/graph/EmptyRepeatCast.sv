// RTL reference for the SV -> C++ -> native-graph path. A zero repetition
// is legal inside a concatenation with a nonempty operand.
module EmptyRepeatCast #(parameter int Count = 0) (
    input logic [31:0] pc_in,
    output logic [31:0] result_out, narrow_out,
    output logic [63:0] scalar_out, concat_out, effects_out,
    output logic truth_out
);
    logic [Count:0] repeated;
    assign repeated = {{Count{pc_in[31]}}, 1'b0};
    assign scalar_out = repeated >> 1;
    assign result_out = scalar_out | pc_in;
    assign concat_out = (scalar_out << 32) | pc_in;
    assign narrow_out = scalar_out;
    assign truth_out = |scalar_out;

    always_comb begin
        logic [31:0] counter;
        logic [63:0] first_value, second_value;
        counter = pc_in & 15;
        counter = counter + 1;
        first_value = {{Count{counter[0]}}, 1'b0} >> 1;
        // The empty constructor still evaluates its argument.
        counter = counter + 1;
        second_value = 0;
        if (pc_in[4]) begin
            counter = counter + 1;
            second_value = {{Count{counter[0]}}, 1'b0} >> 1;
        end
        effects_out = (64'(counter) << 48) | (first_value << 16) | second_value;
    end
endmodule

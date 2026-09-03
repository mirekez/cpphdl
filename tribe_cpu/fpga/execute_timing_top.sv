`default_nettype none

import State_pkg::*;

// Register-to-register shell used only to characterize the otherwise
// combinational Execute block at the requested CPU clock.
module ExecuteTimingTop (
    input  wire         clk,
    input  wire         reset,
    input  wire State   stimulus,
    output logic [31:0] result,
    output logic [31:0] debug_a,
    output logic [31:0] debug_b,
    output logic        branch_taken,
    output logic [31:0] branch_target
);
    State state_reg;
    wire [31:0] dut_result;
    wire [31:0] dut_debug_a;
    wire [31:0] dut_debug_b;
    wire dut_branch_taken;
    wire [31:0] dut_branch_target;
    wire dut_multicycle_wait;

    Execute dut (
        .clk(clk),
        .l2_clock(clk),
        .reset(reset),
        .state_in(state_reg),
        .multicycle_state_in(state_reg),
        .alu_result_out(dut_result),
        .debug_alu_a_out(dut_debug_a),
        .debug_alu_b_out(dut_debug_b),
        .branch_taken_out(dut_branch_taken),
        .branch_target_out(dut_branch_target),
        .multicycle_wait_out(dut_multicycle_wait)
    );

    always_ff @(posedge clk) begin
        if (reset) begin
            state_reg <= '0;
            result <= '0;
            debug_a <= '0;
            debug_b <= '0;
            branch_taken <= '0;
            branch_target <= '0;
        end else begin
            state_reg <= stimulus;
            result <= dut_result;
            debug_a <= dut_debug_a;
            debug_b <= dut_debug_b;
            branch_taken <= dut_branch_taken;
            branch_target <= dut_branch_target;
        end
    end
endmodule

`default_nettype wire

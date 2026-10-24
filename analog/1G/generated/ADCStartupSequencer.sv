`default_nettype none

import Predef_pkg::*;


module ADCStartupSequencer #(
    parameter PrechargeClocks = 'h1000
,   parameter ReleaseClocks = 'h4
 )
 (
    input wire clk
,   input wire reset
,   input wire enable_in
,   output wire precharge_out
,   output wire clear_out
,   output wire ready_out
);


    // regs and combs
    reg[2-1:0] phase;
    reg[16-1:0] elapsed;

    // members

    // tmp variables
    logic[2-1:0] phase_tmp;
    logic[16-1:0] elapsed_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (phase == 64'h0) begin
            elapsed_tmp = elapsed + 64'h1;
            if (elapsed == 64'(32'((PrechargeClocks - 32'h1)))) begin
                phase_tmp = 64'h1;
                elapsed_tmp = 64'h0;
            end
        end
        else begin
            if (phase == 64'h1) begin
                elapsed_tmp = elapsed + 64'h1;
                if (elapsed == 64'(32'((ReleaseClocks - 32'h1)))) begin
                    phase_tmp = 64'h2;
                    elapsed_tmp = 64'h0;
                end
            end
        end
        if (reset || !enable_in) begin
            phase_tmp = 64'h0;
            elapsed_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        phase_tmp = phase;
        elapsed_tmp = elapsed;

            _work(reset);

        phase <= phase_tmp;
        elapsed <= elapsed_tmp;
    end

    assign precharge_out = ((enable_in && (phase == 64'h0)) != '0);

    assign clear_out = ((!enable_in || (phase != 64'h2)) != '0);

    assign ready_out = ((enable_in && (phase == 64'h2)) != '0);


endmodule

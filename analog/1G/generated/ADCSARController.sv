`default_nettype none

import Predef_pkg::*;


module ADCSARController (
    input wire clk
,   input wire reset
,   input wire start_in
,   input wire comparison_in
,   output wire[8-1:0] trial_out
,   output wire[8-1:0] data_out
,   output wire valid_out
,   output wire acquire_out
,   output wire convert_out
,   output wire busy_out
);


    // regs and combs
    reg[3-1:0] phase;
    reg[8-1:0] trial;
    reg[8-1:0] mask;
    reg[8-1:0] data;
    reg valid;
    reg acquire;
    reg convert;

    // members

    // tmp variables
    logic[3-1:0] phase_tmp;
    logic[8-1:0] trial_tmp;
    logic[8-1:0] mask_tmp;
    logic[8-1:0] data_tmp;
    logic valid_tmp;
    logic acquire_tmp;
    logic convert_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] accepted;
        accepted=32'h0;
        valid_tmp = (('h0) != '0);
        case (unsigned'(32'(phase)))
        32'h0: begin
            if (start_in) begin
                acquire_tmp = (('h0) != '0);
                convert_tmp = (('h0) != '0);
                trial_tmp = 64'h0;
                mask_tmp = 64'h0;
                phase_tmp = 64'h1;
            end
        end
        32'h1: begin
            acquire_tmp = (('h1) != '0);
            phase_tmp = 64'h2;
        end
        32'h2: begin
            phase_tmp = 64'h3;
        end
        32'h3: begin
            acquire_tmp = (('h0) != '0);
            phase_tmp = 64'h4;
        end
        32'h4: begin
            convert_tmp = (('h1) != '0);
            trial_tmp = 64'h80;
            mask_tmp = 64'h80;
            phase_tmp = 64'h5;
        end
        32'h5: begin
            accepted=(comparison_in) ? (unsigned'(32'(trial))) : (unsigned'(32'(trial & ~mask)));
            if (mask == 64'h1) begin
                trial_tmp = accepted;
                data_tmp = accepted;
                valid_tmp = (('h1) != '0);
                convert_tmp = (('h0) != '0);
                phase_tmp = 64'h0;
            end
            else begin
                mask_tmp = mask >>> 32'sh1;
                trial_tmp = 32'(accepted | unsigned'(32'((mask >>> 32'sh1))));
            end
        end
        default: begin
            acquire_tmp = (('h0) != '0);
            convert_tmp = (('h0) != '0);
            phase_tmp = 64'h0;
        end
        endcase
        if (reset) begin
            phase_tmp = 64'h0;
            trial_tmp = 64'h0;
            mask_tmp = 64'h0;
            data_tmp = 64'h0;
            valid_tmp = (('h0) != '0);
            acquire_tmp = (('h0) != '0);
            convert_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        phase_tmp = phase;
        trial_tmp = trial;
        mask_tmp = mask;
        data_tmp = data;
        valid_tmp = valid;
        acquire_tmp = acquire;
        convert_tmp = convert;

            _work(reset);

        phase <= phase_tmp;
        trial <= trial_tmp;
        mask <= mask_tmp;
        data <= data_tmp;
        valid <= valid_tmp;
        acquire <= acquire_tmp;
        convert <= convert_tmp;
    end

    assign trial_out = trial;

    assign data_out = data;

    assign valid_out = valid;

    assign acquire_out = acquire;

    assign convert_out = convert;

    assign busy_out = ((phase != 64'h0) != '0);


endmodule

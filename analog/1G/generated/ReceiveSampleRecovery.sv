`default_nettype none

import Predef_pkg::*;


module ReceiveSampleRecovery (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[16-1:0] tag_in
,   input wire valid_in
,   input wire track_in
,   input wire restart_in
,   input wire[25-1:0] initial_phase_in
,   input wire[16-1:0] origin_in
,   output wire[32-1:0] samples_out
,   output wire[16-1:0] tag_out
,   output wire[24-1:0] fraction_out
,   output wire valid_out
,   output wire fault_out
,   output wire clipped_out
,   output wire[25-1:0] interval_out
,   output wire[25-1:0] period_out
,   output wire[14-1:0] error_out
,   output wire correction_valid_out
);


    // regs and combs

    // members
    wire[32-1:0] interpolator__samples_in;
    wire[16-1:0] interpolator__tag_in;
    wire interpolator__valid_in;
    wire[25-1:0] interpolator__interval_in;
    wire[25-1:0] interpolator__period_in;
    wire[25-1:0] interpolator__initial_phase_in;
    wire[16-1:0] interpolator__origin_in;
    wire[32-1:0] interpolator__samples_out;
    wire[8-1:0] interpolator__early_out;
    wire[8-1:0] interpolator__late_out;
    wire[16-1:0] interpolator__tag_out;
    wire[24-1:0] interpolator__fraction_out;
    wire interpolator__valid_out;
    wire interpolator__fault_out;
    wire interpolator__clipped_out;
    ReceiveSampleInterpolator      interpolator (
        .clk(clk)
,       .reset(reset || restart_in)
,       .samples_in(interpolator__samples_in)
,       .tag_in(interpolator__tag_in)
,       .valid_in(interpolator__valid_in)
,       .interval_in(interpolator__interval_in)
,       .period_in(interpolator__period_in)
,       .initial_phase_in(interpolator__initial_phase_in)
,       .origin_in(interpolator__origin_in)
,       .samples_out(interpolator__samples_out)
,       .early_out(interpolator__early_out)
,       .late_out(interpolator__late_out)
,       .tag_out(interpolator__tag_out)
,       .fraction_out(interpolator__fraction_out)
,       .valid_out(interpolator__valid_out)
,       .fault_out(interpolator__fault_out)
,       .clipped_out(interpolator__clipped_out)
    );
    wire[8-1:0] timing__early_in;
    wire[8-1:0] timing__middle_in;
    wire[8-1:0] timing__late_in;
    wire timing__advance_in;
    wire timing__track_in;
    wire[25-1:0] timing__interval_out;
    wire[25-1:0] timing__period_out;
    wire[24-1:0] timing__phase_out;
    wire[14-1:0] timing__error_out;
    wire timing__activity_out;
    wire timing__valid_out;
    ReceiveTimingLoop      timing (
        .clk(clk)
,       .reset((reset || restart_in) || interpolator__fault_out)
,       .early_in(timing__early_in)
,       .middle_in(timing__middle_in)
,       .late_in(timing__late_in)
,       .advance_in(timing__advance_in)
,       .track_in(timing__track_in)
,       .interval_out(timing__interval_out)
,       .period_out(timing__period_out)
,       .phase_out(timing__phase_out)
,       .error_out(timing__error_out)
,       .activity_out(timing__activity_out)
,       .valid_out(timing__valid_out)
    );

    // tmp variables


    function logic[31:0] applied_interval ();
        logic signed[31:0] correction;
        correction=signed'(32'(unsigned'(32'(timing__interval_out)))) - signed'(32'(unsigned'(32'(timing__period_out))));
        correction=(correction < 32'sh0) ? (-signed'(32'(unsigned'(32'(-correction)) >>> 32'sh2))) : (signed'(32'(unsigned'(32'(correction)) >>> 32'sh2)));
        return unsigned'(32'(signed'(32'(unsigned'(32'(timing__period_out)))) + correction));
    endfunction

    generate  // _assign
        assign interpolator__samples_in = samples_in;
        assign interpolator__tag_in = tag_in;
        assign interpolator__valid_in = ((valid_in) != '0);
        assign interpolator__interval_in = 25'(unsigned'(64'(applied_interval())));
        assign interpolator__period_in = timing__period_out;
        assign interpolator__initial_phase_in = initial_phase_in;
        assign interpolator__origin_in = origin_in;
        assign timing__early_in = interpolator__early_out;
        assign timing__middle_in = unsigned'(8'(interpolator__samples_out));
        assign timing__late_in = interpolator__late_out;
        assign timing__advance_in = ((interpolator__valid_out && !interpolator__fault_out) != '0);
        assign timing__track_in = ((track_in) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
    end
    endtask

    always @(posedge clk) begin

            _work(reset);

    end

    assign samples_out = interpolator__samples_out;

    assign tag_out = interpolator__tag_out;

    assign fraction_out = interpolator__fraction_out;

    assign valid_out = ((interpolator__valid_out) != '0);

    assign fault_out = ((interpolator__fault_out) != '0);

    assign clipped_out = ((interpolator__clipped_out) != '0);

    assign interval_out = 25'(unsigned'(64'(applied_interval())));

    assign period_out = timing__period_out;

    assign error_out = timing__error_out;

    assign correction_valid_out = ((timing__valid_out) != '0);


endmodule

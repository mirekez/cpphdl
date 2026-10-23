`default_nettype none

import Predef_pkg::*;


module ReceiveTimingLoop (
    input wire clk
,   input wire reset
,   input wire[8-1:0] early_in
,   input wire[8-1:0] middle_in
,   input wire[8-1:0] late_in
,   input wire advance_in
,   input wire track_in
,   output wire[25-1:0] interval_out
,   output wire[25-1:0] period_out
,   output wire[24-1:0] phase_out
,   output wire[14-1:0] error_out
,   output wire activity_out
,   output wire valid_out
);


    // regs and combs
    reg[23-1:0] frequency;
    reg[25-1:0] interval;
    reg[25-1:0] period;
    reg[24-1:0] phase;
    reg[14-1:0] error;
    reg activity;
    reg valid;
    reg[9-1:0] difference;
    reg[8-1:0] middle;
    reg[18-1:0] product;
    reg[14-1:0] limited;
    reg[14-1:0] error_delay[3];
    reg[21-1:0] proportional;
    reg[25-1:0] nominal;
    reg[25-1:0] nominal_delay;
    reg[25-1:0] next_interval;
    reg[6-1:0] accepted;
    reg[6-1:0] tracking;
    reg[6-1:0] transitions;

    // members

    // tmp variables
    logic[23-1:0] frequency_tmp;
    logic[25-1:0] interval_tmp;
    logic[25-1:0] period_tmp;
    logic[24-1:0] phase_tmp;
    logic[14-1:0] error_tmp;
    logic activity_tmp;
    logic valid_tmp;
    logic[9-1:0] difference_tmp;
    logic[8-1:0] middle_tmp;
    logic[18-1:0] product_tmp;
    logic[14-1:0] limited_tmp;
    logic[14-1:0] error_delay_tmp[3];
    logic[21-1:0] proportional_tmp;
    logic[25-1:0] nominal_tmp;
    logic[25-1:0] nominal_delay_tmp;
    logic[25-1:0] next_interval_tmp;
    logic[6-1:0] accepted_tmp;
    logic[6-1:0] tracking_tmp;
    logic[6-1:0] transitions_tmp;


    function logic signed[31:0] signed_value (
        input logic[31:0] value
,       input logic[31:0] sign
    );
        return signed'(32'((value ^ sign))) - signed'(32'(sign));
    endfunction

    task _work (input logic reset);
    begin: _work
        logic[24-1:0] candidate;
        logic signed[31:0] delta;
        logic signed[31:0] observation;
        logic signed[31:0] accumulated;
        logic signed[31:0] drift;
        delta=signed'(32'(unsigned'(32'(early_in)))) - signed'(32'(unsigned'(32'(late_in))));
        difference_tmp = delta;
        middle_tmp = 32'(unsigned'(32'(middle_in)) ^ 32'h80);
        accepted_tmp = ((accepted <<< 'h1)) | 64'(unsigned'(32'(advance_in)));
        tracking_tmp = ((tracking <<< 'h1)) | 64'(32'((advance_in && track_in)));
        transitions_tmp = ((transitions <<< 'h1)) | 64'(32'(((delta < signed'(32'(-'h1))) || (delta > 32'sh1))));
        product_tmp = signed'(32'(signed_value(unsigned'(32'(difference)), 32'h100)*signed_value(unsigned'(32'(middle)), 32'h80)));
        observation=signed_value(unsigned'(32'(product)), 32'h20000);
        observation=(observation < signed'(32'(-'h1000))) ? (-'h1000) : ((observation > 32'sh1000) ? ('h1000) : (observation));
        limited_tmp = observation;
        observation=signed_value(unsigned'(32'(limited)), 32'h2000);
        accumulated=signed_value(unsigned'(32'(frequency)), 32'h400000);
        candidate = signed'(32'(accumulated + observation));
        if (((((accepted & 64'h4))) != '0) && ((((tracking & 64'h4))) != '0)) begin
            accumulated=signed_value(unsigned'(32'(candidate)), 32'h800000);
            frequency_tmp = signed'(32'((accumulated < signed'(32'(-'h20C49C))) ? (-'h20C49C) : ((accumulated > 32'sh20C49C) ? ('h20C49C) : (accumulated))));
        end
        error_delay_tmp['h0] = limited;
        error_delay_tmp['h1] = error_delay['h0];
        error_delay_tmp['h2] = error_delay['h1];
        accumulated=signed_value(unsigned'(32'(frequency)), 32'h400000);
        drift=(accumulated < 32'sh0) ? (-signed'(32'(unsigned'(32'(-accumulated)) >>> 32'sh6))) : (signed'(32'(unsigned'(32'(accumulated)) >>> 32'sh6)));
        nominal_tmp = signed'(32'('h1000000 - drift));
        proportional_tmp = signed'(32'(((((tracking & 64'h8)) != '0)) ? (signed_value(unsigned'(32'(error_delay['h0])), 32'h2000)*'hA0) : ('h0)));
        nominal_delay_tmp = nominal;
        next_interval_tmp = 32'(unsigned'(32'(nominal)) - unsigned'(32'(signed_value(unsigned'(32'(proportional)), 32'h100000))));
        valid_tmp = (('h0) != '0);
        activity_tmp = (('h0) != '0);
        if (((accepted & 64'h20) != '0)) begin
            period_tmp = nominal_delay;
            interval_tmp = next_interval;
            phase_tmp = 32'(unsigned'(32'(phase)) + unsigned'(32'(next_interval)));
            error_tmp = error_delay['h2];
            activity_tmp = ((((transitions >>> 32'sh5)) & 64'h1) != '0);
            valid_tmp = (('h1) != '0);
        end
        if (reset) begin
            frequency_tmp = 64'h0;
            interval_tmp = 64'h1000000;
            period_tmp = 64'h1000000;
            phase_tmp = 64'h0;
            error_tmp = 64'h0;
            activity_tmp = (('h0) != '0);
            valid_tmp = (('h0) != '0);
            accepted_tmp = 64'h0;
            tracking_tmp = 64'h0;
            transitions_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        frequency_tmp = frequency;
        interval_tmp = interval;
        period_tmp = period;
        phase_tmp = phase;
        error_tmp = error;
        activity_tmp = activity;
        valid_tmp = valid;
        difference_tmp = difference;
        middle_tmp = middle;
        product_tmp = product;
        limited_tmp = limited;
        error_delay_tmp = error_delay;
        proportional_tmp = proportional;
        nominal_tmp = nominal;
        nominal_delay_tmp = nominal_delay;
        next_interval_tmp = next_interval;
        accepted_tmp = accepted;
        tracking_tmp = tracking;
        transitions_tmp = transitions;

            _work(reset);

        frequency <= frequency_tmp;
        interval <= interval_tmp;
        period <= period_tmp;
        phase <= phase_tmp;
        error <= error_tmp;
        activity <= activity_tmp;
        valid <= valid_tmp;
        difference <= difference_tmp;
        middle <= middle_tmp;
        product <= product_tmp;
        limited <= limited_tmp;
        error_delay <= error_delay_tmp;
        proportional <= proportional_tmp;
        nominal <= nominal_tmp;
        nominal_delay <= nominal_delay_tmp;
        next_interval <= next_interval_tmp;
        accepted <= accepted_tmp;
        tracking <= tracking_tmp;
        transitions <= transitions_tmp;
    end

    assign interval_out = interval;

    assign period_out = period;

    assign phase_out = phase;

    assign error_out = error;

    assign activity_out = activity;

    assign valid_out = valid;


endmodule

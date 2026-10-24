`default_nettype none

import Predef_pkg::*;


module ReceiveAnalogGainControl (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire restart_in
,   input wire training_in
,   input wire advance_in
,   input wire adc_fault_in
,   input wire[8-1:0] sample_in
,   input wire[8-1:0] minimum_span_in
,   input wire[8-1:0] maximum_span_in
,   output wire[5-1:0] select_n_out
,   output wire[3-1:0] level_out
,   output wire ready_out
,   output wire changed_out
,   output wire weak_out
,   output wire overload_out
,   output wire fault_out
,   output wire[8-1:0] span_out
,   output wire[8-1:0] centre_out
,   output wire settled_out
);


    // regs and combs
    reg[2-1:0] state;
    reg[3-1:0] level;
    reg[5-1:0] selection;
    reg[12-1:0] count;
    reg[8-1:0] minimum;
    reg[8-1:0] maximum;
    reg[8-1:0] measured_span;
    reg[8-1:0] centre;
    reg[8-1:0] window_minimum;
    reg[8-1:0] window_maximum;
    reg evaluate_span;
    reg evaluate_gain;
    reg ready;
    reg changed;
    reg _weak;
    reg overload;
    reg fault;

    // members

    // tmp variables
    logic[2-1:0] state_tmp;
    logic[3-1:0] level_tmp;
    logic[5-1:0] selection_tmp;
    logic[12-1:0] count_tmp;
    logic[8-1:0] minimum_tmp;
    logic[8-1:0] maximum_tmp;
    logic[8-1:0] measured_span_tmp;
    logic[8-1:0] centre_tmp;
    logic[8-1:0] window_minimum_tmp;
    logic[8-1:0] window_maximum_tmp;
    logic evaluate_span_tmp;
    logic evaluate_gain_tmp;
    logic ready_tmp;
    logic changed_tmp;
    logic _weak_tmp;
    logic overload_tmp;
    logic fault_tmp;


    generate  // _assign
        assign select_n_out = selection;
        assign level_out = level;
        assign ready_out = ready;
        assign changed_out = changed;
        assign weak_out = _weak;
        assign overload_out = overload;
        assign fault_out = fault;
        assign span_out = measured_span;
        assign centre_out = centre;
        assign settled_out = (((state == 64'h1) && !((fault) != '0)) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic invalid_config;
        logic[31:0] low;
        logic[31:0] high;
        logic[31:0] span;
        logic[31:0] next_level;
        logic[31:0] sample;
        sample=sample_in;
        low=(sample < unsigned'(32'(minimum))) ? (sample) : (unsigned'(32'(minimum)));
        high=(sample > unsigned'(32'(maximum))) ? (sample) : (unsigned'(32'(maximum)));
        span=high - low;
        next_level=level;
        invalid_config=((minimum_span_in == 64'h0) || minimum_span_in>=maximum_span_in) || ((unsigned'(32'(maximum_span_in))*32'h7) < (unsigned'(32'(minimum_span_in))*32'h10));
        changed_tmp = (('h0) != '0);
        if (state == 64'h0) begin
            count_tmp = count + 64'h1;
            if (count == 64'hFFF) begin
                state_tmp = 64'h1;
                count_tmp = 64'h0;
                minimum_tmp = 64'hFF;
                maximum_tmp = 64'h0;
            end
        end
        else begin
            if ((state == 64'h1) && training_in) begin
                evaluate_span_tmp = (('h0) != '0);
                evaluate_gain_tmp = evaluate_span;
                if (((evaluate_span) != '0)) begin
                    measured_span_tmp = 32'(unsigned'(32'(window_maximum)) - unsigned'(32'(window_minimum)));
                    centre_tmp = 32'((((unsigned'(32'(window_minimum)) + unsigned'(32'(window_maximum))) + 32'h1)) >>> 32'sh1);
                end
                if (advance_in) begin
                    minimum_tmp = low;
                    maximum_tmp = high;
                    count_tmp = count + 64'h1;
                    if (count == 64'h3FF) begin
                        window_minimum_tmp = low;
                        window_maximum_tmp = high;
                        evaluate_span_tmp = (('h1) != '0);
                        count_tmp = 64'h0;
                        minimum_tmp = 64'hFF;
                        maximum_tmp = 64'h0;
                    end
                end
                if (((evaluate_gain) != '0)) begin
                    span=measured_span;
                    if ((span < unsigned'(32'(minimum_span_in))) && (level < 64'h4)) begin
                        next_level=unsigned'(32'(level)) + 32'h1;
                    end
                    else begin
                        if ((span > unsigned'(32'(maximum_span_in))) && (level > 64'h0)) begin
                            next_level=unsigned'(32'(level)) - 32'h1;
                        end
                    end
                    level_tmp = next_level;
                    selection_tmp = 32'(32'h1F ^ (('h1 <<< next_level)));
                    ready_tmp = span>=unsigned'(32'(minimum_span_in)) && span<=unsigned'(32'(maximum_span_in));
                    _weak_tmp = (span < unsigned'(32'(minimum_span_in))) && (level == 64'h4);
                    overload_tmp = (span > unsigned'(32'(maximum_span_in))) && (level == 64'h0);
                    if (next_level != unsigned'(32'(level))) begin
                        state_tmp = 64'h0;
                        count_tmp = 64'h0;
                        minimum_tmp = 64'hFF;
                        maximum_tmp = 64'h0;
                        evaluate_span_tmp = (('h0) != '0);
                        evaluate_gain_tmp = (('h0) != '0);
                        ready_tmp = (('h0) != '0);
                        changed_tmp = (('h1) != '0);
                    end
                end
            end
        end
        if ((invalid_config || adc_fault_in) || ((fault) != '0)) begin
            state_tmp = 64'h2;
            level_tmp = 64'h0;
            selection_tmp = 64'h1E;
            count_tmp = 64'h0;
            minimum_tmp = 64'hFF;
            maximum_tmp = 64'h0;
            measured_span_tmp = 64'h0;
            window_minimum_tmp = 64'hFF;
            window_maximum_tmp = 64'h0;
            evaluate_span_tmp = (('h0) != '0);
            evaluate_gain_tmp = (('h0) != '0);
            centre_tmp = 64'h80;
            ready_tmp = (('h0) != '0);
            changed_tmp = (('h0) != '0);
            _weak_tmp = (('h0) != '0);
            overload_tmp = (('h0) != '0);
            fault_tmp = (('h1) != '0);
        end
        if ((reset || restart_in) || !enable_in) begin
            state_tmp = 64'h0;
            level_tmp = 64'h0;
            selection_tmp = 64'h1E;
            count_tmp = 64'h0;
            minimum_tmp = 64'hFF;
            maximum_tmp = 64'h0;
            measured_span_tmp = 64'h0;
            window_minimum_tmp = 64'hFF;
            window_maximum_tmp = 64'h0;
            evaluate_span_tmp = (('h0) != '0);
            evaluate_gain_tmp = (('h0) != '0);
            centre_tmp = 64'h80;
            ready_tmp = (('h0) != '0);
            changed_tmp = (('h0) != '0);
            _weak_tmp = (('h0) != '0);
            overload_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        level_tmp = level;
        selection_tmp = selection;
        count_tmp = count;
        minimum_tmp = minimum;
        maximum_tmp = maximum;
        measured_span_tmp = measured_span;
        centre_tmp = centre;
        window_minimum_tmp = window_minimum;
        window_maximum_tmp = window_maximum;
        evaluate_span_tmp = evaluate_span;
        evaluate_gain_tmp = evaluate_gain;
        ready_tmp = ready;
        changed_tmp = changed;
        _weak_tmp = _weak;
        overload_tmp = overload;
        fault_tmp = fault;

            _work(reset);

        state <= state_tmp;
        level <= level_tmp;
        selection <= selection_tmp;
        count <= count_tmp;
        minimum <= minimum_tmp;
        maximum <= maximum_tmp;
        measured_span <= measured_span_tmp;
        centre <= centre_tmp;
        window_minimum <= window_minimum_tmp;
        window_maximum <= window_maximum_tmp;
        evaluate_span <= evaluate_span_tmp;
        evaluate_gain <= evaluate_gain_tmp;
        ready <= ready_tmp;
        changed <= changed_tmp;
        _weak <= _weak_tmp;
        overload <= overload_tmp;
        fault <= fault_tmp;
    end


endmodule

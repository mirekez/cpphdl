`default_nettype none

import Predef_pkg::*;


module ReceiveAnalogAcquisition (
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
,   input wire[8-1:0] target_span_in
,   output wire[5-1:0] select_n_out
,   output wire[3-1:0] level_out
,   output wire[8-1:0] offset_out
,   output wire[16-1:0] gain_out
,   output wire[8-1:0] span_out
,   output wire[8-1:0] centre_out
,   output wire ready_out
,   output wire busy_out
,   output wire settled_out
,   output wire weak_out
,   output wire overload_out
,   output wire fault_out
);


    // regs and combs
    reg launched;

    // members
    wire control__enable_in;
    wire control__restart_in;
    wire control__training_in;
    wire control__advance_in;
    wire control__adc_fault_in;
    wire[8-1:0] control__sample_in;
    wire[8-1:0] control__minimum_span_in;
    wire[8-1:0] control__maximum_span_in;
    wire[5-1:0] control__select_n_out;
    wire[3-1:0] control__level_out;
    wire control__ready_out;
    wire control__changed_out;
    wire control__weak_out;
    wire control__overload_out;
    wire control__fault_out;
    wire[8-1:0] control__span_out;
    wire[8-1:0] control__centre_out;
    wire control__settled_out;
    ReceiveAnalogGainControl      control (
        .clk(clk)
,       .reset(reset)
,       .enable_in(control__enable_in)
,       .restart_in(control__restart_in)
,       .training_in(control__training_in)
,       .advance_in(control__advance_in)
,       .adc_fault_in(control__adc_fault_in)
,       .sample_in(control__sample_in)
,       .minimum_span_in(control__minimum_span_in)
,       .maximum_span_in(control__maximum_span_in)
,       .select_n_out(control__select_n_out)
,       .level_out(control__level_out)
,       .ready_out(control__ready_out)
,       .changed_out(control__changed_out)
,       .weak_out(control__weak_out)
,       .overload_out(control__overload_out)
,       .fault_out(control__fault_out)
,       .span_out(control__span_out)
,       .centre_out(control__centre_out)
,       .settled_out(control__settled_out)
    );
    wire calibration__enable_in;
    wire calibration__start_in;
    wire[8-1:0] calibration__centre_in;
    wire[8-1:0] calibration__span_in;
    wire[8-1:0] calibration__target_span_in;
    wire[8-1:0] calibration__offset_out;
    wire[16-1:0] calibration__gain_out;
    wire calibration__ready_out;
    wire calibration__busy_out;
    wire calibration__fault_out;
    ReceiveRangeCalibration      calibration (
        .clk(clk)
,       .reset(reset)
,       .enable_in(calibration__enable_in)
,       .start_in(calibration__start_in)
,       .centre_in(calibration__centre_in)
,       .span_in(calibration__span_in)
,       .target_span_in(calibration__target_span_in)
,       .offset_out(calibration__offset_out)
,       .gain_out(calibration__gain_out)
,       .ready_out(calibration__ready_out)
,       .busy_out(calibration__busy_out)
,       .fault_out(calibration__fault_out)
    );

    // tmp variables
    logic launched_tmp;


    function logic launch ();
        return (((((enable_in && training_in) && !restart_in) && !adc_fault_in) && !((launched) != '0)) && control__ready_out) && !control__fault_out;
    endfunction

    generate  // _assign
        assign control__enable_in = ((enable_in) != '0);
        assign control__restart_in = ((restart_in) != '0);
        assign control__training_in = ((training_in && !((launched) != '0)) != '0);
        assign control__advance_in = ((advance_in) != '0);
        assign control__adc_fault_in = ((adc_fault_in || calibration__fault_out) != '0);
        assign control__sample_in = sample_in;
        assign control__minimum_span_in = minimum_span_in;
        assign control__maximum_span_in = maximum_span_in;
        assign calibration__enable_in = (((((enable_in && !restart_in) && !adc_fault_in) && !control__changed_out) && !control__fault_out) != '0);
        assign calibration__start_in = ((launch()) != '0);
        assign calibration__centre_in = control__centre_out;
        assign calibration__span_in = control__span_out;
        assign calibration__target_span_in = target_span_in;
        assign select_n_out = control__select_n_out;
        assign level_out = control__level_out;
        assign offset_out = calibration__offset_out;
        assign gain_out = calibration__gain_out;
        assign span_out = control__span_out;
        assign centre_out = control__centre_out;
        assign fault_out = ((control__fault_out || calibration__fault_out) != '0);
        assign ready_out = ((((((enable_in && !restart_in) && !adc_fault_in) && calibration__ready_out) && control__ready_out) && !fault_out) != '0);
        assign busy_out = ((calibration__busy_out) != '0);
        assign settled_out = ((control__settled_out) != '0);
        assign weak_out = ((control__weak_out) != '0);
        assign overload_out = ((control__overload_out) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (launch()) begin
            launched_tmp = (('h1) != '0);
        end
        if ((reset || restart_in) || !enable_in) begin
            launched_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        launched_tmp = launched;

            _work(reset);

        launched <= launched_tmp;
    end


endmodule

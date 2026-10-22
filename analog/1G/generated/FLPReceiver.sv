`default_nettype none

import Predef_pkg::*;


module FLPReceiver (
    input wire clk
,   input wire reset
,   input wire pulse_in
,   input wire enable_in
,   output wire[16-1:0] page_out
,   output wire valid_out
,   output wire fault_out
);


    // regs and combs
    (* ASYNC_REG = "TRUE" *)
    logic input_sync1;
    (* ASYNC_REG = "TRUE" *)
    logic input_sync2;
    reg previous;
    reg[6-1:0] width;
    reg[15-1:0] age;
    reg[4-1:0] _bit;
    reg[16-1:0] assembling;
    reg[16-1:0] page;
    reg active;
    reg data_seen;
    reg valid;
    reg fault;

    // members

    // tmp variables
    logic input_sync1_tmp;
    logic input_sync2_tmp;
    logic previous_tmp;
    logic[6-1:0] width_tmp;
    logic[15-1:0] age_tmp;
    logic[4-1:0] _bit_tmp;
    logic[16-1:0] assembling_tmp;
    logic[16-1:0] page_tmp;
    logic active_tmp;
    logic data_seen_tmp;
    logic valid_tmp;
    logic fault_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        input_sync1_tmp = pulse_in;
        input_sync2_tmp = input_sync1;
        previous_tmp = input_sync2;
        valid_tmp = (('h0) != '0);
        fault_tmp = (('h0) != '0);
        if (age < 64'h4E20) begin
            age_tmp = age + 64'h1;
        end
        else begin
            if (((active) != '0)) begin
                active_tmp = (('h0) != '0);
                fault_tmp = (('h1) != '0);
            end
        end
        if (((input_sync2) != '0)) begin
            if (width < 64'h3F) begin
                width_tmp = width + 64'h1;
            end
        end
        else begin
            width_tmp = 64'h0;
            if (((previous) != '0) && (width > 64'h20)) begin
                active_tmp = (('h0) != '0);
                fault_tmp = (('h1) != '0);
            end
            else begin
                if (((previous) != '0) && width>=64'h5) begin
                    if (!((active) != '0) || age>=64'h4E20) begin
                        active_tmp = (('h1) != '0);
                        _bit_tmp = 64'h0;
                        assembling_tmp = 64'h0;
                        data_seen_tmp = (('h0) != '0);
                        age_tmp = 64'h0;
                    end
                    else begin
                        if ((age>=64'h16F3 && age<=64'h2616) && !((data_seen) != '0)) begin
                            assembling_tmp = assembling | unsigned'(64'(signed'(32'((('h1 <<< unsigned'(32'(_bit))))))));
                            data_seen_tmp = (('h1) != '0);
                        end
                        else begin
                            if (age>=64'h3633 && age<=64'h43DF) begin
                                age_tmp = 64'h0;
                                data_seen_tmp = (('h0) != '0);
                                if (_bit == 64'hF) begin
                                    page_tmp = assembling;
                                    valid_tmp = (('h1) != '0);
                                    active_tmp = (('h0) != '0);
                                end
                                else begin
                                    _bit_tmp = _bit + 64'h1;
                                end
                            end
                            else begin
                                active_tmp = (('h0) != '0);
                                fault_tmp = (('h1) != '0);
                            end
                        end
                    end
                end
            end
        end
        if (reset || !enable_in) begin
            input_sync1_tmp = (('h0) != '0);
            input_sync2_tmp = (('h0) != '0);
            previous_tmp = (('h0) != '0);
            width_tmp = 64'h0;
            age_tmp = 64'h4E20;
            _bit_tmp = 64'h0;
            assembling_tmp = 64'h0;
            page_tmp = 64'h0;
            active_tmp = (('h0) != '0);
            data_seen_tmp = (('h0) != '0);
            valid_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        input_sync1_tmp = input_sync1;
        input_sync2_tmp = input_sync2;
        previous_tmp = previous;
        width_tmp = width;
        age_tmp = age;
        _bit_tmp = _bit;
        assembling_tmp = assembling;
        page_tmp = page;
        active_tmp = active;
        data_seen_tmp = data_seen;
        valid_tmp = valid;
        fault_tmp = fault;

            _work(reset);

        input_sync1 <= input_sync1_tmp;
        input_sync2 <= input_sync2_tmp;
        previous <= previous_tmp;
        width <= width_tmp;
        age <= age_tmp;
        _bit <= _bit_tmp;
        assembling <= assembling_tmp;
        page <= page_tmp;
        active <= active_tmp;
        data_seen <= data_seen_tmp;
        valid <= valid_tmp;
        fault <= fault_tmp;
    end

    assign page_out = page;

    assign valid_out = valid;

    assign fault_out = fault;


endmodule

`default_nettype none

import Predef_pkg::*;


module ReceiveTimingConfidence (
    input wire clk
,   input wire reset
,   input wire[14-1:0] error_in
,   input wire[25-1:0] period_in
,   input wire valid_in
,   input wire fault_in
,   output wire ready_out
);


    // regs and combs
    reg[10-1:0] count;
    reg[11-1:0] excited;
    reg[23-1:0] magnitude;
    reg[24-1:0] total;
    reg[21-1:0] partial;
    reg[25-1:0] first_period;
    reg failed;
    reg ready;

    // members

    // tmp variables
    logic[10-1:0] count_tmp;
    logic[11-1:0] excited_tmp;
    logic[23-1:0] magnitude_tmp;
    logic[24-1:0] total_tmp;
    logic[21-1:0] partial_tmp;
    logic[25-1:0] first_period_tmp;
    logic failed_tmp;
    logic ready_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic signed[31:0] e;
        logic signed[31:0] t;
        logic signed[31:0] p;
        logic signed[31:0] slew;
        logic bad;
        logic[31:0] next_excited;
        logic[31:0] next_magnitude;
        e = signed'(32'(unsigned'(32'(error_in))));
        t = signed'(32'(unsigned'(32'(total))));
        p = signed'(32'(unsigned'(32'(partial))));
        slew = signed'(32'(unsigned'(32'(period_in)))) - signed'(32'(unsigned'(32'(first_period))));
        bad = ((failed) != '0);
        if (e>=32'sh2000) begin
            e-='h4000;
        end
        if (t>=32'sh800000) begin
            t-='h1000000;
        end
        if (p>=32'sh100000) begin
            p-='h200000;
        end
        next_excited=unsigned'(32'(excited)) + 32'((e != 32'sh0));
        next_magnitude=unsigned'(32'(magnitude)) + unsigned'(32'(((e < 32'sh0)) ? (-e) : (e)));
        t+=e;
        p+=e;
        if (valid_in) begin
            excited_tmp = next_excited;
            magnitude_tmp = next_magnitude;
            if (count == 64'h0) begin
                first_period_tmp = period_in;
            end
            total_tmp = t;
            partial_tmp = p;
            count_tmp = count + 64'h1;
            if (((unsigned'(32'(count)) & 32'h7F)) == 32'h7F) begin
                bad=(bad || (p > 32'sh6B5F)) || (p < signed'(32'(-'h6B5F)));
                failed_tmp = bad;
                partial_tmp = 64'h0;
            end
            if (count == 64'h3FF) begin
                ready_tmp = (((((next_excited>=32'h80 && next_magnitude<=32'h200000) && !bad) && t<=32'sh6B5F) && t>=signed'(32'(-'h6B5F))) && slew<=32'sh219) && slew>=signed'(32'(-'h219));
                total_tmp = 64'h0;
                excited_tmp = 64'h0;
                magnitude_tmp = 64'h0;
                failed_tmp = (('h0) != '0);
            end
        end
        if (reset || fault_in) begin
            count_tmp = 64'h0;
            total_tmp = 64'h0;
            excited_tmp = 64'h0;
            magnitude_tmp = 64'h0;
            partial_tmp = 64'h0;
            first_period_tmp = 64'h0;
            failed_tmp = (('h0) != '0);
            ready_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        count_tmp = count;
        excited_tmp = excited;
        magnitude_tmp = magnitude;
        total_tmp = total;
        partial_tmp = partial;
        first_period_tmp = first_period;
        failed_tmp = failed;
        ready_tmp = ready;

            _work(reset);

        count <= count_tmp;
        excited <= excited_tmp;
        magnitude <= magnitude_tmp;
        total <= total_tmp;
        partial <= partial_tmp;
        first_period <= first_period_tmp;
        failed <= failed_tmp;
        ready <= ready_tmp;
    end

    assign ready_out = ((((ready) != '0) && !fault_in) != '0);


endmodule

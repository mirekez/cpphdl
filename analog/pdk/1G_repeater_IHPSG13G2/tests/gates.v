`timescale 1ns/1ps
module test;
    reg ref_clock = 0;
    always #4 ref_clock = ~ref_clock;
    reg reset, link_a_in, link_b_in, enable_in;
    reg [9:0] rx_a_in, rx_b_in;
    wire [9:0] tx_a_out, tx_b_out;
    wire active_out;
    supply0 VSS;
    supply1 VDD;
    RepeaterCore dut (.*);
    integer file, count, parsed, cycle, rst, a, b, la, lb, en, ta, tb, active;
    reg [2047:0] header;
    initial begin
        count = 0;
        file = $fopen("native_vectors.csv", "r");
        if (!file) $fatal(1, "missing independently checked native vectors");
        parsed = $fgets(header, file);
        while (!$feof(file)) begin
            parsed = $fscanf(file, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                              cycle, rst, a, b, la, lb, en, ta, tb, active);
            if (parsed != 10) $fatal(1, "truncated/malformed native vectors");
            @(negedge ref_clock);
            reset = rst;
            rx_a_in = a;
            rx_b_in = b;
            link_a_in = la;
            link_b_in = lb;
            enable_in = en;
            @(posedge ref_clock);
            #2;
            if (tx_a_out !== ta[9:0] || tx_b_out !== tb[9:0] || active_out !== active[0])
                $fatal(1, "gate/native disagreement at cycle %0d", cycle);
            count = count + 1;
        end
        if (count != 20513) $fatal(1, "insufficient gate comparison vectors");
        $display("PASS: %0d gate/native comparison cycles", count);
        $finish;
    end
endmodule

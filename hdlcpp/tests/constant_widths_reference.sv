module constant_widths_reference (
    output logic [63:0] values [0:2][0:5]
);
    constant_widths #(.DATA_WIDTH(64), .ID_WIDTH(4), .LARGE(64'h100000000), .NARROW(255)) first (
        .strobe_width_o(values[0][0]), .large_half_o(values[0][1]),
        .narrow_half_o(values[0][2]), .truncated_half_o(values[0][3]),
        .unsigned_sum_o(values[0][4]), .extended_o(values[0][5])
    );
    constant_widths #(.DATA_WIDTH(128), .ID_WIDTH(8), .LARGE(64'hffffffffffffffff), .NARROW(128)) second (
        .strobe_width_o(values[1][0]), .large_half_o(values[1][1]),
        .narrow_half_o(values[1][2]), .truncated_half_o(values[1][3]),
        .unsigned_sum_o(values[1][4]), .extended_o(values[1][5])
    );
    constant_widths #(.DATA_WIDTH(32), .ID_WIDTH(1), .LARGE(64'h8000000000000000), .NARROW(511)) third (
        .strobe_width_o(values[2][0]), .large_half_o(values[2][1]),
        .narrow_half_o(values[2][2]), .truncated_half_o(values[2][3]),
        .unsigned_sum_o(values[2][4]), .extended_o(values[2][5])
    );
endmodule

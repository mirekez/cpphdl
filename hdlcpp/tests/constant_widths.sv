module constant_widths #(
    parameter longint unsigned DATA_WIDTH = 64,
    parameter int unsigned ID_WIDTH = 4,
    parameter longint unsigned LARGE = 64'h100000000,
    parameter logic [7:0] NARROW = 8'hff,
    parameter int unsigned WORD = 32'hffffffff
) (
    output logic [63:0] strobe_width_o,
    output logic [63:0] large_half_o,
    output logic [63:0] narrow_half_o,
    output logic [63:0] truncated_half_o,
    output logic [63:0] unsigned_sum_o,
    output logic [63:0] extended_o
);
    localparam int unsigned STRB_WIDTH = DATA_WIDTH / 8;
    localparam logic [7:0] TRUNCATED = 9'h1ff;
    localparam int unsigned CLIPPED = 64'h100000008;
    localparam longint unsigned LARGE_HALF = LARGE / 2;
    localparam longint unsigned NARROW_HALF = NARROW / 2;
    localparam longint unsigned TRUNCATED_HALF = TRUNCATED / 2;
    localparam longint unsigned UNSIGNED_SUM = ID_WIDTH + 1;
    localparam longint unsigned EXTENDED = WORD + 1;
    typedef logic [STRB_WIDTH-1:0] strb_t;
    typedef logic [ID_WIDTH-1:0] id_t;
    typedef logic [(DATA_WIDTH / 8)-1:0] direct_t;
    typedef logic [TRUNCATED-1:0] truncated_t;
    typedef logic [CLIPPED-1:0] clipped_t;
    assign strobe_width_o = STRB_WIDTH;
    assign large_half_o = LARGE_HALF;
    assign narrow_half_o = NARROW_HALF;
    assign truncated_half_o = TRUNCATED_HALF;
    assign unsigned_sum_o = UNSIGNED_SUM;
    assign extended_o = EXTENDED;
endmodule

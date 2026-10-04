module ByteStringContexts #(
    parameter LABEL = "KEEP"
) (
    input logic [4:0] select_i,
    output logic [31:0] value_o,
    output logic [127:0] wide_o,
    output logic [31:0] math_o,
    output logic equal_o,
    output logic string_ok_o
);
    localparam logic [31:0] TAG = "CVA6";
    assign string_ok_o = LABEL == "KEEP";
    assign math_o = "A" + select_i;
    assign equal_o = select_i == "\004";
    always_comb begin
        byte empty = "";
        case (select_i[3:0])
            0: value_o = empty;
            1: value_o = "D";
            2: value_o = "AB";
            3: value_o = "\n";
            4: value_o = "\t";
            5: value_o = "\"";
            6: value_o = "\\";
            7: value_o = "\377";
            8: value_o = "\x41";
            9: value_o = "A\000B";
            10: value_o = ("M");
            11: value_o = select_i[4] ? "XY" : "";
            12: value_o = {"A", "BC"};
            13: value_o = 8'("AB");
            14: value_o = TAG;
            default: value_o = 4'("Z");
        endcase
        wide_o = select_i[4] ? "ABCDEFGHIJKL" : "";
    end
endmodule

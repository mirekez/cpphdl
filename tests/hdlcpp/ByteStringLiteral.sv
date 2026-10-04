module ByteStringLiteral (
    input logic [1:0] select_i,
    output logic [7:0] mode_o
);
    always_comb begin
        byte mode = "";
        case (select_i)
            0: mode = "D";
            1: mode = "M";
            2: mode = "S";
            default: mode = "U";
        endcase
        mode_o = mode;
    end
endmodule

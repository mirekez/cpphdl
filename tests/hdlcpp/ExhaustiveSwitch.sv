module ExhaustiveSwitch (
    input logic [3:0] selector,
    input logic [7:0] data,
    output logic [7:0] complete,
    output logic [7:0] sparse
);
    always_comb begin
        case (selector)
            0: complete = data ^ 8'h00;
            1: complete = data ^ 8'h11;
            2: complete = data ^ 8'h22;
            3: complete = data ^ 8'h33;
            4: complete = data ^ 8'h44;
            5: complete = data ^ 8'h55;
            6: complete = data ^ 8'h66;
            7: complete = data ^ 8'h77;
            8: complete = data ^ 8'h88;
            9: complete = data ^ 8'h99;
            10: complete = data ^ 8'haa;
            11: complete = data ^ 8'hbb;
            12: complete = data ^ 8'hcc;
            13: complete = data ^ 8'hdd;
            14: complete = data ^ 8'hee;
            15: complete = data ^ 8'hff;
        endcase
        case ((selector & 4'ha) | 4'h1)
            1: sparse = data ^ 8'h1a;
            3: sparse = data ^ 8'h3b;
            9: sparse = data ^ 8'h9c;
            11: sparse = data ^ 8'hbd;
        endcase
    end
endmodule

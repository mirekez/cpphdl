module GuardedFeedback (
    input logic [1:0] state_i,
    input logic [7:0] data_i,
    output logic [7:0] first_o,
    output logic [7:0] second_o
);
    assign first_o = state_i == 0 ? data_i ^ second_o : 8'd5;
    assign second_o = state_i == 1 ? data_i ^ first_o : 8'd7;
endmodule

module GuardedCoupled (
    input logic [1:0] state_i,
    input logic [1:0] other_i,
    output logic [7:0] first_o,
    output logic [7:0] second_o,
    output logic [7:0] third_o
);
    assign first_o = state_i == 0 ? second_o : (other_i == 0 ? third_o : 8'd5);
    assign second_o = state_i == 1 ? first_o : 8'd7;
    assign third_o = other_i == 1 ? first_o : 8'd9;
endmodule

module GuardedSelector (
    input logic [1:0] state_i,
    output logic [7:0] first_o,
    output logic [7:0] second_o
);
    assign first_o = state_i == 0 ? second_o : 8'd0;
    assign second_o = first_o ? (state_i == 0 ? 8'd7 : 8'd9) :
                              (state_i == 0 ? 8'd7 : 8'd11);
endmodule

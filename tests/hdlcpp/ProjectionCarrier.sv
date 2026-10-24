package ProjectionCarrierTypes;
    typedef struct packed {
        logic ar_valid;
        logic [7:0] ar;
    } req_t;
endpackage

module ProjectionCarrierChild #(
    parameter type Req = logic
) (
    input Req req_i,
    output logic [7:0] value_o,
    output Req [1:0] req_o
);
    assign value_o = req_i.ar;
    assign req_o[0] = req_i;
    assign req_o[1] = req_i;
endmodule

module ProjectionCarrier #(
    parameter type Req = ProjectionCarrierTypes::req_t
) (
    input Req req_i,
    output logic [7:0] value_o
);
    Req [1:0] child_req;
    Req copied;
    ProjectionCarrierChild #(.Req(Req)) child (.req_i(req_i), .value_o(), .req_o(child_req));
    always_comb begin
        copied.ar_valid = child_req[0].ar_valid;
        copied.ar = child_req[0].ar;
    end
    assign value_o = copied.ar;
endmodule

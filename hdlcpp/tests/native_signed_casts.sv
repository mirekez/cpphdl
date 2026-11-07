module native_signed_casts(input logic [7:0] a,b,
 output logic signed_less,unsigned_less,
 output logic [31:0] signed_extend,unsigned_extend,
 output logic [15:0] signed_sum,
 output logic [7:0] shifted,
 input logic [63:0] wide_a,wide_b,
 output logic wide_less,
 output logic [79:0] extra_signed,extra_unsigned);
 assign signed_less=$signed(a)<$signed(b);
 assign unsigned_less=$unsigned(a)<$unsigned(b);
 assign signed_extend=$signed(a);
 assign unsigned_extend=$unsigned(a);
 assign signed_sum=$signed(a)+$signed(b);
 assign shifted=$signed(a)>>>3;
 assign wide_less=$signed(wide_a)<$signed(wide_b);
 assign extra_signed=$signed(a);
 assign extra_unsigned=$unsigned(a);
endmodule

// Give hdlcpp the same module declarations Verilator receives. This preserves
// named parameter defaults and scalar/array port types without patch maps.
`include "cf_math_pkg.sv"
`include "lzc.sv"
`include "rr_arb_tree.sv"
`include "ArbiterBench.sv"

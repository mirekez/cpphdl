`include "axi/typedef.svh"
`include "axi/assign.svh"

// Same dimensions and latency configuration as the RV32 matmul testharness.
// Only boundary wiring and address-map constants live here; all switching,
// arbitration, ID tracking and buffering remain in the original PULP RTL.
module XbarBench #(parameter int INPUTS = 2) (
    input logic clk_i,
    input logic rst_ni,
    input logic [1:0][373:0] requests,
    input logic [9:0][147:0] responses,
    output logic [1:0][145:0] slave_outputs,
    output logic [9:0][375:0] master_outputs
);
    typedef logic [63:0] addr_t;
    typedef logic [63:0] data_t;
    typedef logic [7:0] strb_t;
    typedef logic [31:0] user_t;
    typedef logic [3:0] slv_id_t;
    typedef logic [4:0] mst_id_t;
    `AXI_TYPEDEF_AW_CHAN_T(slv_aw_t, addr_t, slv_id_t, user_t)
    `AXI_TYPEDEF_AW_CHAN_T(mst_aw_t, addr_t, mst_id_t, user_t)
    `AXI_TYPEDEF_W_CHAN_T(w_t, data_t, strb_t, user_t)
    `AXI_TYPEDEF_B_CHAN_T(slv_b_t, slv_id_t, user_t)
    `AXI_TYPEDEF_B_CHAN_T(mst_b_t, mst_id_t, user_t)
    `AXI_TYPEDEF_AR_CHAN_T(slv_ar_t, addr_t, slv_id_t, user_t)
    `AXI_TYPEDEF_AR_CHAN_T(mst_ar_t, addr_t, mst_id_t, user_t)
    `AXI_TYPEDEF_R_CHAN_T(slv_r_t, data_t, slv_id_t, user_t)
    `AXI_TYPEDEF_R_CHAN_T(mst_r_t, data_t, mst_id_t, user_t)
    `AXI_TYPEDEF_REQ_T(slv_req_t, slv_aw_t, w_t, slv_ar_t)
    `AXI_TYPEDEF_REQ_T(mst_req_t, mst_aw_t, w_t, mst_ar_t)
    `AXI_TYPEDEF_RESP_T(slv_resp_t, slv_b_t, slv_r_t)
    `AXI_TYPEDEF_RESP_T(mst_resp_t, mst_b_t, mst_r_t)
    localparam axi_pkg::xbar_cfg_t Cfg = '{
        NoSlvPorts: 2, NoMstPorts: 10, MaxMstTrans: 1, MaxSlvTrans: 1,
        FallThrough: 1'b0, LatencyMode: axi_pkg::NO_LATENCY,
        AxiIdWidthSlvPorts: 4, AxiIdUsedSlvPorts: 4, UniqueIds: 1'b0,
        AxiAddrWidth: 64, AxiDataWidth: 64, NoAddrRules: 10
    };
    slv_req_t [1:0] slv_reqs;
    slv_resp_t [1:0] slv_resps;
    mst_req_t [9:0] mst_reqs;
    mst_resp_t [9:0] mst_resps;
    axi_pkg::xbar_rule_64_t [9:0] rules;
    `include "AddressMap.svh"
    assign slv_reqs = requests;
    assign mst_resps = responses;
    assign slave_outputs = slv_resps;
    assign master_outputs = mst_reqs;
    AXI_BUS #(.AXI_ADDR_WIDTH(64), .AXI_DATA_WIDTH(64),
              .AXI_ID_WIDTH(4), .AXI_USER_WIDTH(32)) slave[1:0]();
    AXI_BUS #(.AXI_ADDR_WIDTH(64), .AXI_DATA_WIDTH(64),
              .AXI_ID_WIDTH(5), .AXI_USER_WIDTH(32)) master[9:0]();
    `include "BoundaryWiring.svh"
    axi_xbar_intf #(
        .Cfg(Cfg), .AXI_USER_WIDTH(32),
        .rule_t(axi_pkg::xbar_rule_64_t)
    ) dut (
        .clk_i(clk_i), .rst_ni(rst_ni), .test_i(1'b0),
        .slv_ports(slave), .mst_ports(master),
        .addr_map_i(rules), .en_default_mst_port_i('0), .default_mst_port_i('0)
    );
endmodule

`default_nettype none

import Predef_pkg::*;
import PayloadItem_pkg::*;
import PayloadChoice_pkg::*;
import PayloadBusData_pkg::*;
import ArrayPayload_pkg::*;


module ArrayInStruct (
    input wire clk
,   input wire reset
,   input wire[8-1:0] seed_in
,   input ArrayPayload payload_in
,   output ArrayPayload direct_out
,   output ArrayPayload state_out
);


    // regs and combs
    ArrayPayload direct_comb;
    ArrayPayload state_reg;

    // members
    genvar gi, gj, gk;

    // tmp variables
    ArrayPayload state_reg_tmp;


    always_comb begin : direct_comb_func  // direct_comb_func
        ArrayPayload in_payload; in_payload = payload_in;
        direct_comb = make_payload(seed_in);
        direct_comb.prefix^=in_payload.prefix;
        direct_comb.bytes['h0] = direct_comb.bytes['h0] ^ in_payload.bytes['h2];
        direct_comb.bytes['h1] = direct_comb.bytes['h1] ^ in_payload.bytes['h1];
        direct_comb.bytes['h2] = direct_comb.bytes['h2] ^ in_payload.bytes['h0];
        direct_comb.items['h0].lo^=in_payload.items['h1].hi;
        direct_comb.items['h0].hi^=in_payload.items['h1].lo;
        direct_comb.items['h1].lo^=in_payload.items['h0].hi;
        direct_comb.items['h1].hi^=in_payload.items['h0].lo;
        direct_comb.mid^=in_payload.mid;
        direct_comb.halfs['h0] = direct_comb.halfs['h0] ^ in_payload.halfs['h0];
        direct_comb.choices['h0].s.tag^=in_payload.choices['h1].s.tag;
        direct_comb.choices['h0].s.value^=in_payload.choices['h1].s.value;
        direct_comb.choices['h1].s.tag^=in_payload.choices['h0].s.tag;
        direct_comb.choices['h1].s.value^=in_payload.choices['h0].s.value;
        direct_comb.bus_data.values['h0].lo^=in_payload.bus_data.values['h1].hi;
        direct_comb.bus_data.values['h0].hi^=in_payload.bus_data.values['h1].lo;
        direct_comb.bus_data.values['h1].lo^=in_payload.bus_data.values['h0].hi;
        direct_comb.bus_data.values['h1].hi^=in_payload.bus_data.values['h0].lo;
        direct_comb.tail^=in_payload.tail;
        disable direct_comb_func;
    end

    function ArrayPayload make_payload (input logic[31:0] seed);
        ArrayPayload payload;
        payload.prefix=seed & 'hF;
        payload.bytes['h0] = unsigned'(8'(seed + 'h11));
        payload.bytes['h1] = unsigned'(8'(seed + 'h23));
        payload.bytes['h2] = unsigned'(8'(seed + 'h35));
        payload.items['h0].lo=((seed + 'h1)) & 'hF;
        payload.items['h0].hi=((seed + 'h2)) & 'hF;
        payload.items['h1].lo=((seed + 'h3)) & 'hF;
        payload.items['h1].hi=((seed + 'h4)) & 'hF;
        payload.mid=((seed >>> 'h2)) & 'h7;
        payload.halfs['h0] = unsigned'(16'(((seed <<< 'h8)) ^ 'h5AA5));
        payload.choices['h0].s.tag=((seed + 'h5)) & 'h7;
        payload.choices['h0].s.value=((seed + 'h6)) & 'h1F;
        payload.choices['h1].s.tag=((seed + 'h7)) & 'h7;
        payload.choices['h1].s.value=((seed + 'h8)) & 'h1F;
        payload.bus_data.values['h0].lo=((seed + 'h9)) & 'hF;
        payload.bus_data.values['h0].hi=((seed + 'hA)) & 'hF;
        payload.bus_data.values['h1].lo=((seed + 'hB)) & 'hF;
        payload.bus_data.values['h1].hi=((seed + 'hC)) & 'hF;
        payload.tail=((seed + 'h17)) & 'h1F;
        return payload;
    endfunction

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            state_reg_tmp = make_payload('h0);
        end
        else begin
            state_reg_tmp = direct_comb;
        end
    end
    endtask

    generate  // _assign
    endgenerate

    always @(posedge clk) begin
        state_reg_tmp = state_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
    end

    assign direct_out = direct_comb;

    assign state_out = state_reg;


endmodule

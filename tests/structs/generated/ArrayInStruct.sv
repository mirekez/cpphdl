`default_nettype none

import Predef_pkg::*;
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


    always @(*) begin  // direct_comb_func
        ArrayPayload in_payload; in_payload = payload_in;
        direct_comb = make_payload(seed_in);
        direct_comb.prefix^=in_payload.prefix;
        direct_comb.bytes['h0] = direct_comb.bytes['h0] ^ in_payload.bytes['h2];
        direct_comb.bytes['h1] = direct_comb.bytes['h1] ^ in_payload.bytes['h1];
        direct_comb.bytes['h2] = direct_comb.bytes['h2] ^ in_payload.bytes['h0];
        direct_comb.mid^=in_payload.mid;
        direct_comb.halfs['h0] = direct_comb.halfs['h0] ^ in_payload.halfs['h0];
        direct_comb.tail^=in_payload.tail;
    end

    function ArrayPayload make_payload (input logic[31:0] seed);
        ArrayPayload payload; payload = 0;
        payload.prefix=seed & 'hF;
        payload.bytes['h0] = unsigned'(8'(seed + 'h11));
        payload.bytes['h1] = unsigned'(8'(seed + 'h23));
        payload.bytes['h2] = unsigned'(8'(seed + 'h35));
        payload.mid=((seed >>> 'h2)) & 'h7;
        payload.halfs['h0] = unsigned'(16'(((seed <<< 'h8)) ^ 'h5AA5));
        payload.tail=((seed + 'h17)) & 'h1F;
        return payload;
    endfunction

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            state_reg_tmp = 0;
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

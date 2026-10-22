`default_nettype none

import Predef_pkg::*;


module PCSReceiveSynchronizer (
    input wire clk
,   input wire reset
,   input wire[12-1:0] symbols_in
,   input wire remote_master_in
,   input wire pma_ok_in
,   input wire check_idle_in
,   input wire restart_in
,   output wire locked_out
,   output wire[33-1:0] state_out
,   output wire[4-1:0] sx_out
,   output wire[4-1:0] sy_out
,   output wire[4-1:0] sg_out
,   output wire[4-1:0] low_mask_out
,   output wire[4-1:0] polarity_out
,   output wire remote_receiver_ok_out
);


    // regs and combs
    reg[33-1:0] state;
    reg[6-1:0] collected;
    reg[7-1:0] qualified;
    reg[2-1:0] phase_candidates;
    reg[4-1:0] known_polarity;
    reg[4-1:0] polarity;
    reg[4-1:0] sx_reg;
    reg[4-1:0] sy_reg;
    reg[4-1:0] sg_reg;
    reg[4-1:0] low_mask;
    reg locked;
    reg odd;
    reg remote_ok;

    // members

    // tmp variables
    logic[33-1:0] state_tmp;
    logic[6-1:0] collected_tmp;
    logic[7-1:0] qualified_tmp;
    logic[2-1:0] phase_candidates_tmp;
    logic[4-1:0] known_polarity_tmp;
    logic[4-1:0] polarity_tmp;
    logic[4-1:0] sx_reg_tmp;
    logic[4-1:0] sy_reg_tmp;
    logic[4-1:0] sg_reg_tmp;
    logic[4-1:0] low_mask_tmp;
    logic locked_tmp;
    logic odd_tmp;
    logic remote_ok_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[33-1:0] current;
        logic[33-1:0] predicted;
        logic[31:0] sy;
        logic[31:0] sx;
        logic[31:0] sg;
        logic[31:0] previous_sy;
        logic[31:0] raw;
        logic[31:0] point;
        logic[31:0] found_polarity;
        logic[31:0] found_known;
        logic[31:0] candidates;
        logic[31:0] even_mask;
        logic[31:0] odd_mask;
        logic[31:0] mask;
        logic idle;
        logic consistent;
        logic _bit;
        logic phase0;
        logic phase1;
        predicted = ((state <<< 'h1)) | ((((((state >>> 32'sh20)) ^ ((state >>> signed'(32'(((remote_master_in) ? ('hC) : ('h13)))))))) & 64'h1));
        raw=unsigned'(32'(symbols_in)) & 32'h7;
        _bit=(raw == 32'h0) || (raw == 32'h4);
        current = (((locked) != '0)) ? (predicted) : (33'(64'(((state <<< 'h1)) | 64'(32'(_bit)))));
        state_tmp = current;
        odd_tmp = !((odd) != '0);
        sy=current & 64'h1;
        sy=sy | ((((unsigned'(32'((((current >>> 32'sh3)) ^ ((current >>> 32'sh8))))) & 32'h1)) <<< 'h1));
        sy=sy | ((((unsigned'(32'((((current >>> 32'sh6)) ^ ((current >>> 32'sh10))))) & 32'h1)) <<< 'h2));
        sy=sy | ((((unsigned'(32'((((((current >>> 32'sh9)) ^ ((current >>> 32'shE))) ^ ((current >>> 32'sh13))) ^ ((current >>> 32'sh18))))) & 32'h1)) <<< 'h3));
        sx=unsigned'(32'((((current >>> 32'sh4)) ^ ((current >>> 32'sh6))))) & 32'h1;
        sx=sx | ((((unsigned'(32'((((((current >>> 32'sh7)) ^ ((current >>> 32'sh9))) ^ ((current >>> 32'shC))) ^ ((current >>> 32'shE))))) & 32'h1)) <<< 'h1));
        sx=sx | ((((unsigned'(32'((((((current >>> 32'shA)) ^ ((current >>> 32'shC))) ^ ((current >>> 32'sh14))) ^ ((current >>> 32'sh16))))) & 32'h1)) <<< 'h2));
        sx=sx | ((((unsigned'(32'((((((((((current >>> 32'shD)) ^ ((current >>> 32'shF))) ^ ((current >>> 32'sh12))) ^ ((current >>> 32'sh14))) ^ ((current >>> 32'sh17))) ^ ((current >>> 32'sh19))) ^ ((current >>> 32'sh1C))) ^ ((current >>> 32'sh1E))))) & 32'h1)) <<< 'h3));
        sg=unsigned'(32'((((current >>> 32'sh1)) ^ ((current >>> 32'sh5))))) & 32'h1;
        sg=sg | ((((unsigned'(32'((((((current >>> 32'sh4)) ^ ((current >>> 32'sh8))) ^ ((current >>> 32'sh9))) ^ ((current >>> 32'shD))))) & 32'h1)) <<< 'h1));
        sg=sg | ((((unsigned'(32'((((((current >>> 32'sh7)) ^ ((current >>> 32'shB))) ^ ((current >>> 32'sh11))) ^ ((current >>> 32'sh15))))) & 32'h1)) <<< 'h2));
        sg=sg | ((((unsigned'(32'((((((((((current >>> 32'shA)) ^ ((current >>> 32'shE))) ^ ((current >>> 32'shF))) ^ ((current >>> 32'sh13))) ^ ((current >>> 32'sh14))) ^ ((current >>> 32'sh18))) ^ ((current >>> 32'sh19))) ^ ((current >>> 32'sh1D))))) & 32'h1)) <<< 'h3));
        previous_sy=state & 64'h1;
        previous_sy=previous_sy | ((((unsigned'(32'((((state >>> 32'sh3)) ^ ((state >>> 32'sh8))))) & 32'h1)) <<< 'h1));
        previous_sy=previous_sy | ((((unsigned'(32'((((state >>> 32'sh6)) ^ ((state >>> 32'sh10))))) & 32'h1)) <<< 'h2));
        previous_sy=previous_sy | ((((unsigned'(32'((((((state >>> 32'sh9)) ^ ((state >>> 32'shE))) ^ ((state >>> 32'sh13))) ^ ((state >>> 32'sh18))))) & 32'h1)) <<< 'h3));
        sx_reg_tmp = sx;
        sy_reg_tmp = sy;
        sg_reg_tmp = sg;
        even_mask=sy;
        odd_mask=((((previous_sy ^ 32'hE)) & 32'hE)) | ((sy & 32'h1));
        mask=((((signed'(32'(((odd) != '0))) ^ signed'(32'(((phase_candidates == 64'h2)))))) != '0)) ? (odd_mask) : (even_mask);
        low_mask_tmp = mask;
        idle=1;
        point=32'h0;
        found_polarity=polarity;
        found_known=known_polarity;
        consistent=1;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            raw=((unsigned'(32'(symbols_in)) >>> ((32'h3*lane)))) & 32'h7;
            if (((raw != 32'h0) && (raw != 32'h2)) && (raw != 32'h4)) begin
                idle=0;
            end
            if ((raw == 32'h0) || (raw == 32'h4)) begin
                point=point | 32'((('h1 <<< lane)));
                _bit=signed'(32'(((raw == 32'h4)))) != signed'(32'(((((((sg >>> lane)) & 32'h1)) != 32'h0))));
                if (((((known_polarity & unsigned'(64'(signed'(32'((('h1 <<< lane))))))))) != '0) && (signed'(32'(_bit)) != signed'(32'(((((((polarity >>> lane)) & 64'h1)) != 64'h0)))))) begin
                    consistent=0;
                end
                found_polarity=((found_polarity & 32'(~(('h1 <<< lane))))) | ((32'(_bit) <<< lane));
                found_known=found_known | 32'((('h1 <<< lane)));
            end
        end
        if (!((locked) != '0)) begin
            if (collected < 64'h21) begin
                collected_tmp = collected + 64'h1;
            end
            if (collected>=64'h21) begin
                phase0=((point & 32'hA)) == ((((((odd) != '0)) ? (odd_mask) : (even_mask)) & 32'hA));
                phase1=((point & 32'hA)) == ((((((odd) != '0)) ? (even_mask) : (odd_mask)) & 32'hA));
                candidates=unsigned'(32'(phase_candidates)) & ((32'(phase0) | ((32'(phase1) <<< 'h1))));
                if ((((!idle || !consistent) || (current == 64'h0)) || (((current & 64'h1)) != ((predicted & 64'h1)))) || (candidates == 32'h0)) begin
                    qualified_tmp = 64'h0;
                    phase_candidates_tmp = 64'h3;
                    known_polarity_tmp = 64'h0;
                end
                else begin
                    phase_candidates_tmp = candidates;
                    known_polarity_tmp = found_known;
                    polarity_tmp = found_polarity;
                    if (qualified < 64'h40) begin
                        qualified_tmp = qualified + 64'h1;
                    end
                    if ((qualified>=64'h3F && (found_known == 32'hF)) && (((candidates == 32'h1) || (candidates == 32'h2)))) begin
                        locked_tmp = (('h1) != '0);
                    end
                end
            end
        end
        else begin
            if (check_idle_in) begin
                if ((!idle || !consistent) || (((point & 32'hB)) != ((mask & 32'hB)))) begin
                    locked_tmp = (('h0) != '0);
                    collected_tmp = 64'h0;
                    qualified_tmp = 64'h0;
                    phase_candidates_tmp = 64'h3;
                    known_polarity_tmp = 64'h0;
                end
                else begin
                    remote_ok_tmp = ((((point ^ mask)) & 32'h4)) != 32'h0;
                end
            end
        end
        if ((!pma_ok_in || restart_in) || reset) begin
            state_tmp = 64'h0;
            collected_tmp = 64'h0;
            qualified_tmp = 64'h0;
            phase_candidates_tmp = 64'h3;
            known_polarity_tmp = 64'h0;
            polarity_tmp = 64'h0;
            sx_reg_tmp = 64'h0;
            sy_reg_tmp = 64'h0;
            sg_reg_tmp = 64'h0;
            low_mask_tmp = 64'h0;
            locked_tmp = (('h0) != '0);
            odd_tmp = (('h0) != '0);
            remote_ok_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        collected_tmp = collected;
        qualified_tmp = qualified;
        phase_candidates_tmp = phase_candidates;
        known_polarity_tmp = known_polarity;
        polarity_tmp = polarity;
        sx_reg_tmp = sx_reg;
        sy_reg_tmp = sy_reg;
        sg_reg_tmp = sg_reg;
        low_mask_tmp = low_mask;
        locked_tmp = locked;
        odd_tmp = odd;
        remote_ok_tmp = remote_ok;

            _work(reset);

        state <= state_tmp;
        collected <= collected_tmp;
        qualified <= qualified_tmp;
        phase_candidates <= phase_candidates_tmp;
        known_polarity <= known_polarity_tmp;
        polarity <= polarity_tmp;
        sx_reg <= sx_reg_tmp;
        sy_reg <= sy_reg_tmp;
        sg_reg <= sg_reg_tmp;
        low_mask <= low_mask_tmp;
        locked <= locked_tmp;
        odd <= odd_tmp;
        remote_ok <= remote_ok_tmp;
    end

    assign locked_out = locked;

    assign state_out = state;

    assign sx_out = sx_reg;

    assign sy_out = sy_reg;

    assign sg_out = sg_reg;

    assign low_mask_out = low_mask;

    assign polarity_out = polarity;

    assign remote_receiver_ok_out = remote_ok;


endmodule

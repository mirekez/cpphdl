`default_nettype none

import Predef_pkg::*;


module PCSReceiveFramer (
    input wire clk
,   input wire reset
,   input wire symbol_enable_in
,   input wire[12-1:0] symbols_in
,   input wire[4-1:0] sg_in
,   input wire[4-1:0] sx_in
,   input wire[4-1:0] low_mask_in
,   input wire[4-1:0] polarity_in
,   input wire locked_in
,   input wire pma_ok_in
,   output wire[4-1:0] negate_out
,   output wire[7-1:0] control_out
,   output wire[20-1:0] tag_out
,   output wire restart_out
,   output wire remote_receiver_ok_out
,   output wire receiving_out
);


    // regs and combs
    reg[3-1:0] state;
    reg[3-1:0] good_idles;
    reg[3-1:0] bad_groups;
    reg[4-1:0] negate_reg;
    reg[7-1:0] control_reg;
    reg[20-1:0] tag_reg;
    reg restart_reg;
    reg error_dv;
    reg remote_ok;

    // members

    // tmp variables
    logic[3-1:0] state_tmp;
    logic[3-1:0] good_idles_tmp;
    logic[3-1:0] bad_groups_tmp;
    logic[4-1:0] negate_reg_tmp;
    logic[7-1:0] control_reg_tmp;
    logic[20-1:0] tag_reg_tmp;
    logic restart_reg_tmp;
    logic error_dv_tmp;
    logic remote_ok_tmp;


    function logic[31:0] special (input logic[31:0] symbols);
        case (symbols)
        32'h522: begin
            return 32'h8;
        end
        32'h91B: begin
            return 32'hA;
        end
        32'h8DC: begin
            return 32'hC;
        end
        32'h71C: begin
            return 32'hE;
        end
        32'h6A4: begin
            return 32'h9;
        end
        32'h8E2: begin
            return 32'hB;
        end
        32'h523: begin
            return 32'hD;
        end
        32'h51C: begin
            return 32'hF;
        end
        32'h120: begin
            return 32'h10;
        end
        32'h909: begin
            return 32'h12;
        end
        32'h84C: begin
            return 32'h14;
        end
        32'h30C: begin
            return 32'h16;
        end
        32'h224: begin
            return 32'h11;
        end
        32'h860: begin
            return 32'h13;
        end
        32'h121: begin
            return 32'h15;
        end
        32'h10C: begin
            return 32'h17;
        end
        32'h894: begin
            return 32'h18;
        end
        32'h6E4: begin
            return 32'h1A;
        end
        32'h723: begin
            return 32'h1C;
        end
        32'h8E3: begin
            return 32'h1E;
        end
        32'h714: begin
            return 32'h19;
        end
        32'h8D4: begin
            return 32'h1B;
        end
        32'h913: begin
            return 32'h1D;
        end
        32'h89C: begin
            return 32'h1F;
        end
        32'h804: begin
            return 32'h20;
        end
        32'h264: begin
            return 32'h22;
        end
        32'h321: begin
            return 32'h24;
        end
        32'h861: begin
            return 32'h26;
        end
        32'h304: begin
            return 32'h21;
        end
        32'h844: begin
            return 32'h23;
        end
        32'h901: begin
            return 32'h25;
        end
        32'h80C: begin
            return 32'h27;
        end
        32'h924: begin
            return 32'h38;
        end
        32'h124: begin
            return 32'h40;
        end
        32'h824: begin
            return 32'h48;
        end
        32'h904: begin
            return 32'h50;
        end
        32'h920: begin
            return 32'h58;
        end
        default: begin
            return 32'hFF;
        end
        endcase
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] normalized;
        logic[31:0] reversed;
        logic[31:0] raw;
        logic[31:0] point;
        logic[31:0] condition;
        logic[31:0] subset;
        logic[31:0] control;
        logic[31:0] tag;
        logic[31:0] sign;
        logic idle;
        logic valid_levels;
        logic bad;
        logic reversal;
        normalized=32'h0;
        reversed=32'h0;
        point=32'h0;
        idle=1;
        valid_levels=1;
        sign=unsigned'(32'(sg_in)) ^ unsigned'(32'(polarity_in));
        reversal=state>=64'h2 && state<=64'h5;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            raw=((unsigned'(32'(symbols_in)) >>> ((32'h3*lane)))) & 32'h7;
            if (raw > 32'h4) begin
                valid_levels=0;
            end
            if (((((sign >>> lane)) & 32'h1) != '0)) begin
                raw=32'h4 - raw;
            end
            normalized=normalized | ((((raw & 32'h7)) <<< ((32'h3*lane))));
            reversed=reversed | ((((((32'h4 - raw)) & 32'h7)) <<< ((32'h3*lane))));
            if ((raw != 32'h0) && (raw != 32'h2)) begin
                idle=0;
            end
            if (raw == 32'h0) begin
                point=point | 32'((('h1 <<< lane)));
            end
        end
        idle=idle && ((((point & 32'hB)) == ((unsigned'(32'(low_mask_in)) & 32'hB))));
        negate_reg_tmp = 32'(sign ^ unsigned'(32'(((reversal) ? ('hF) : ('h0)))));
        condition=special((reversal) ? (reversed) : (normalized));
        subset=condition & 32'h7;
        condition=condition >>> 32'sh3;
        control=32'h0;
        tag=32'h0;
        bad=0;
        restart_reg_tmp = (('h0) != '0);
        if ((state == 64'h0) || (state == 64'h7)) begin
            if (normalized == 32'h924) begin
                tag=32'h0;
                control=32'h0;
                state_tmp = 64'h1;
            end
            else begin
                if (idle) begin
                    bad_groups_tmp = 64'h0;
                    state_tmp = 64'h0;
                    remote_ok_tmp = ((((point ^ unsigned'(32'(low_mask_in)))) & 32'h4)) != 32'h0;
                end
                else begin
                    if (((state == 64'h7) && ((((point & 32'h8)) == ((unsigned'(32'(low_mask_in)) & 32'h8))))) && valid_levels) begin
                        tag=((((((point ^ unsigned'(32'(low_mask_in)))) & 32'h2)) != '0)) ? ('h21F) : ('h20F);
                    end
                    else begin
                        tag=32'h20E;
                        bad=1;
                        state_tmp = 64'h6;
                    end
                end
            end
        end
        else begin
            if (state == 64'h1) begin
                if (normalized == 32'h124) begin
                    tag=32'h80155;
                    control=32'h2;
                    state_tmp = 64'h2;
                end
                else begin
                    tag=32'h20E;
                    bad=1;
                    state_tmp = 64'h6;
                end
            end
            else begin
                if (state == 64'h2) begin
                    if (((condition == 32'h2) || (condition == 32'h3)) || (condition == 32'h4)) begin
                        control=32'h9 | ((subset <<< 'h4));
                        tag=(condition == 32'h2) ? ('h21F) : ((condition == 32'h3) ? ('h20F) : ('h0));
                        state_tmp = 64'h3;
                    end
                    else begin
                        if (condition == 32'h1) begin
                            control=32'h9 | ((subset <<< 'h4));
                            tag=32'h700;
                        end
                        else begin
                            if ((condition == 32'h1F) && valid_levels) begin
                                control=32'h1;
                                tag=32'h500;
                            end
                            else begin
                                tag=32'h300;
                                bad=1;
                                state_tmp = 64'h6;
                            end
                        end
                    end
                end
                else begin
                    if (state == 64'h3) begin
                        if (((condition == 32'h2) || (condition == 32'h3)) || (condition == 32'h4)) begin
                            control=32'hD | ((subset <<< 'h4));
                            tag=(condition == 32'h2) ? ('h21F) : ((condition == 32'h3) ? ('h20F) : ('h0));
                            state_tmp = 64'h4;
                        end
                        else begin
                            tag=32'h200;
                            bad=1;
                            state_tmp = 64'h6;
                        end
                    end
                    else begin
                        if (state == 64'h4) begin
                            if ((condition == 32'h7) || (condition == 32'hB)) begin
                                tag=(condition == 32'hB) ? ('h21F) : ('h0);
                                state_tmp = 64'h5;
                            end
                            else begin
                                tag=32'h200;
                                bad=1;
                                state_tmp = 64'h6;
                            end
                        end
                        else begin
                            if (state == 64'h5) begin
                                if (condition>=32'h8 && condition<=32'hB) begin
                                    tag=(condition == 32'hB) ? ('h21F) : ((condition == 32'hA) ? ('h20F) : ('h0));
                                    state_tmp = signed'(32'(((condition == 32'hA) || (condition == 32'hB)) ? ('h7) : ('h0)));
                                end
                                else begin
                                    tag=32'h200;
                                    bad=1;
                                    state_tmp = 64'h6;
                                end
                            end
                            else begin
                                tag=(((error_dv) != '0)) ? ('h300) : ('h20E);
                                if (idle) begin
                                    if (good_idles>=64'h3) begin
                                        tag=32'h0;
                                        good_idles_tmp = 64'h0;
                                        bad_groups_tmp = 64'h0;
                                        state_tmp = 64'h0;
                                    end
                                    else begin
                                        good_idles_tmp = good_idles + 64'h1;
                                    end
                                end
                                else begin
                                    good_idles_tmp = 64'h0;
                                    bad=1;
                                end
                            end
                        end
                    end
                end
            end
        end
        if (bad) begin
            if (state != 64'h6) begin
                error_dv_tmp = state == 64'h2;
            end
            good_idles_tmp = 64'h0;
            if (bad_groups < 64'h4) begin
                bad_groups_tmp = bad_groups + 64'h1;
            end
            else begin
                restart_reg_tmp = (('h1) != '0);
                remote_ok_tmp = (('h0) != '0);
            end
        end
        control_reg_tmp = control;
        tag_reg_tmp = 32'(tag | ((((((unsigned'(32'(sx_in)) <<< 'h4)) | unsigned'(32'(low_mask_in)))) <<< 'hB)));
        if ((((symbol_enable_in && !locked_in)) || !pma_ok_in) || reset) begin
            state_tmp = 64'h0;
            good_idles_tmp = 64'h0;
            bad_groups_tmp = 64'h0;
            negate_reg_tmp = 64'h0;
            control_reg_tmp = 64'h0;
            tag_reg_tmp = signed'(32'((!reset && (state != 64'h0)) ? ('h200) : ('h0)));
            restart_reg_tmp = (('h0) != '0);
            error_dv_tmp = (('h0) != '0);
            remote_ok_tmp = (('h0) != '0);
        end
        if ((!symbol_enable_in && !reset) && pma_ok_in) begin
            state_tmp = state;
            good_idles_tmp = good_idles;
            bad_groups_tmp = bad_groups;
            negate_reg_tmp = negate_reg;
            control_reg_tmp = control_reg;
            tag_reg_tmp = tag_reg;
            restart_reg_tmp = restart_reg;
            error_dv_tmp = error_dv;
            remote_ok_tmp = remote_ok;
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        good_idles_tmp = good_idles;
        bad_groups_tmp = bad_groups;
        negate_reg_tmp = negate_reg;
        control_reg_tmp = control_reg;
        tag_reg_tmp = tag_reg;
        restart_reg_tmp = restart_reg;
        error_dv_tmp = error_dv;
        remote_ok_tmp = remote_ok;

            _work(reset);

        state <= state_tmp;
        good_idles <= good_idles_tmp;
        bad_groups <= bad_groups_tmp;
        negate_reg <= negate_reg_tmp;
        control_reg <= control_reg_tmp;
        tag_reg <= tag_reg_tmp;
        restart_reg <= restart_reg_tmp;
        error_dv <= error_dv_tmp;
        remote_ok <= remote_ok_tmp;
    end

    assign negate_out = negate_reg;

    assign control_out = control_reg;

    assign tag_out = tag_reg;

    assign restart_out = restart_reg;

    assign remote_receiver_ok_out = remote_ok;

    assign receiving_out = (((state == 64'h1) || (state == 64'h2)) != '0);


endmodule

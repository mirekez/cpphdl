`default_nettype none

import Predef_pkg::*;


module Autonegotiator (
    input wire clk
,   input wire reset
,   input wire[16-1:0] receive_page_in
,   input wire receive_valid_in
,   input wire receive_fault_in
,   input wire transmit_burst_in
,   input wire tick_ms_in
,   input wire enable_in
,   input wire restart_in
,   input wire pma_link_in
,   input wire[11-1:0] local_up1_in
,   input wire[11-1:0] entropy_seed_in
,   output wire[16-1:0] transmit_page_out
,   output wire transmit_enable_out
,   output wire pma_enable_out
,   output wire link_out
,   output wire master_out
,   output wire fault_out
,   output wire[3-1:0] reason_out
,   output wire retry_out
);


    // regs and combs
    reg[3-1:0] state;
    reg[3-1:0] stage;
    reg[3-1:0] complete_count;
    reg[3-1:0] role_wait;
    reg[3-1:0] collisions;
    reg[3-1:0] reason;
    reg[11-1:0] milliseconds;
    reg[11-1:0] local_up1;
    reg[11-1:0] local_seed;
    reg[11-1:0] remote_up1;
    reg[11-1:0] remote_seed;
    reg[16-1:0] transmit_page;
    reg[16-1:0] candidate;
    reg[16-1:0] ability;
    reg[16-1:0] ack_word;
    reg[2-1:0] same_count;
    reg[2-1:0] ack_count;
    reg remote_np;
    reg remote_toggle;
    reg local_toggle;
    reg retry;

    // members
    wire[11-1:0] roles__local_up1_in;
    wire[11-1:0] roles__remote_up1_in;
    wire[11-1:0] roles__local_seed_in;
    wire[11-1:0] roles__remote_seed_in;
    wire roles__advance_in;
    wire roles__resolved_out;
    wire roles__master_out;
    wire roles__fault_out;
    wire roles__collision_out;
    MasterSlaveResolver      roles (
        .clk(clk)
,       .reset((reset || restart_in) || !enable_in)
,       .local_up1_in(roles__local_up1_in)
,       .remote_up1_in(roles__remote_up1_in)
,       .local_seed_in(roles__local_seed_in)
,       .remote_seed_in(roles__remote_seed_in)
,       .advance_in(roles__advance_in)
,       .resolved_out(roles__resolved_out)
,       .master_out(roles__master_out)
,       .fault_out(roles__fault_out)
,       .collision_out(roles__collision_out)
    );

    // tmp variables
    logic[3-1:0] state_tmp;
    logic[3-1:0] stage_tmp;
    logic[3-1:0] complete_count_tmp;
    logic[3-1:0] role_wait_tmp;
    logic[3-1:0] collisions_tmp;
    logic[3-1:0] reason_tmp;
    logic[11-1:0] milliseconds_tmp;
    logic[11-1:0] local_up1_tmp;
    logic[11-1:0] local_seed_tmp;
    logic[11-1:0] remote_up1_tmp;
    logic[11-1:0] remote_seed_tmp;
    logic[16-1:0] transmit_page_tmp;
    logic[16-1:0] candidate_tmp;
    logic[16-1:0] ability_tmp;
    logic[16-1:0] ack_word_tmp;
    logic[2-1:0] same_count_tmp;
    logic[2-1:0] ack_count_tmp;
    logic remote_np_tmp;
    logic remote_toggle_tmp;
    logic local_toggle_tmp;
    logic retry_tmp;


    generate  // _assign
        assign roles__local_up1_in = local_up1;
        assign roles__remote_up1_in = remote_up1;
        assign roles__local_seed_in = local_seed;
        assign roles__remote_seed_in = remote_seed;
        assign roles__advance_in = (((state == 64'h3) && stage>=64'h3) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] word;
        logic[31:0] failure;
        logic[31:0] following;
        logic[31:0] toggle;
        logic[31:0] accepted;
        logic wanted;
        logic acknowledgement_complete;
        word=unsigned'(32'(receive_page_in)) & 32'hBFFF;
        failure=32'h0;
        following=32'h0;
        toggle=32'h0;
        wanted=0;
        acknowledgement_complete=0;
        accepted=32'h0;
        retry_tmp = (('h0) != '0);
        if (tick_ms_in && (milliseconds < 64'h7D0)) begin
            milliseconds_tmp = milliseconds + 64'h1;
        end
        case (unsigned'(32'(state)))
        32'h0: begin
            if (tick_ms_in && milliseconds>=64'h513) begin
                milliseconds_tmp = 64'h0;
                local_up1_tmp = 32'(unsigned'(32'(local_up1_in)) & 32'h1F);
                local_seed_tmp = entropy_seed_in;
                transmit_page_tmp = 64'h8001;
                state_tmp = 64'h1;
                if (((local_up1_in & 64'h8)) == 64'h0) begin
                    failure=32'h4;
                end
            end
        end
        32'h1: begin
            if (receive_fault_in) begin
                same_count_tmp = 64'h0;
                ack_count_tmp = 64'h0;
            end
            else begin
                if (receive_valid_in) begin
                    wanted=(stage == 64'h0) || ((((((word >>> 32'shB)) & 32'h1)) != unsigned'(32'(remote_toggle))));
                    if (!wanted) begin
                        same_count_tmp = 64'h0;
                        ack_count_tmp = 64'h0;
                    end
                    else begin
                        ack_word_tmp = word;
                        ack_count_tmp = unsigned'(32'((((receive_page_in & 64'h4000)) == 64'h0) ? (32'h0) : ((64'(word) == candidate) ? (unsigned'(32'(ack_count)) + 32'h1) : (32'h1))));
                        if ((64'(word) == candidate) && same_count>=64'h2) begin
                            ability_tmp = word;
                            transmit_page_tmp = transmit_page | 64'h4000;
                            milliseconds_tmp = 64'h0;
                            state_tmp = 64'h2;
                            if ((stage == 64'h0) && (((((word & 32'h1F)) != 32'h1) || (((word & 32'h2000)) != 32'h0)))) begin
                                failure=32'h3;
                            end
                        end
                        else begin
                            candidate_tmp = word;
                            same_count_tmp = unsigned'(32'((64'(word) == candidate) ? (unsigned'(32'(same_count)) + 32'h1) : (32'h1)));
                        end
                    end
                end
            end
        end
        32'h2: begin
            if (receive_fault_in) begin
                ack_count_tmp = 64'h0;
            end
            else begin
                if (ack_count>=64'h3) begin
                    accepted=ack_word;
                    acknowledgement_complete=1;
                end
                else begin
                    if (receive_valid_in) begin
                        if (((receive_page_in & 64'h4000)) == 64'h0) begin
                            ack_count_tmp = 64'h0;
                        end
                        else begin
                            if ((64'(word) == ack_word) && ack_count>=64'h2) begin
                                accepted=word;
                                acknowledgement_complete=1;
                            end
                            else begin
                                ack_word_tmp = word;
                                ack_count_tmp = unsigned'(32'((64'(word) == ack_word) ? (unsigned'(32'(ack_count)) + 32'h1) : (32'h1)));
                            end
                        end
                    end
                end
            end
        end
        32'h3: begin
            if (role_wait < 64'h2) begin
                role_wait_tmp = role_wait + 64'h1;
            end
            if (stage>=64'h3 && role_wait>=64'h2) begin
                if (roles__fault_out) begin
                    failure=32'h1;
                end
                if (roles__collision_out) begin
                    failure=32'h2;
                end
            end
            if (transmit_burst_in) begin
                if (complete_count < 64'h6) begin
                    complete_count_tmp = complete_count + 64'h1;
                end
                else begin
                    if (stage>=64'h3 && !((remote_np) != '0)) begin
                        state_tmp = 64'h5;
                        milliseconds_tmp = 64'h0;
                        collisions_tmp = 64'h0;
                    end
                    else begin
                        following=(stage < 64'h4) ? (unsigned'(32'(stage)) + 32'h1) : (32'h4);
                        toggle=unsigned'(32'(local_toggle)) ^ 32'h1;
                        local_toggle_tmp = ((toggle) != '0);
                        stage_tmp = following;
                        transmit_page_tmp = unsigned'(32'((following == 32'h1) ? ((32'hB008 | ((toggle <<< 'hB)))) : ((following == 32'h2) ? (((32'h9000 | ((toggle <<< 'hB))) | unsigned'(32'(local_up1)))) : ((following == 32'h3) ? (((32'h1000 | ((toggle <<< 'hB))) | unsigned'(32'(local_seed)))) : ((32'h3001 | ((toggle <<< 'hB))))))));
                        same_count_tmp = 64'h0;
                        ack_count_tmp = 64'h0;
                        candidate_tmp = 64'h0;
                        ack_word_tmp = 64'h0;
                        milliseconds_tmp = 64'h0;
                        state_tmp = 64'h1;
                    end
                end
            end
        end
        32'h5: begin
            if (pma_link_in) begin
                state_tmp = 64'h6;
                milliseconds_tmp = 64'h0;
                reason_tmp = 64'h0;
            end
            else begin
                if (tick_ms_in && milliseconds>=64'h351) begin
                    failure=32'h5;
                end
            end
        end
        32'h6: begin
            if (!pma_link_in) begin
                failure=32'h5;
            end
        end
        default: begin
        end
        endcase
        if (acknowledgement_complete) begin
            if (64'(accepted) != ability) begin
                failure=32'h3;
            end
            else begin
                remote_np_tmp = ((((accepted >>> 32'shF)) & 32'h1) != '0);
                remote_toggle_tmp = ((((accepted >>> 32'shB)) & 32'h1) != '0);
                complete_count_tmp = 64'h0;
                role_wait_tmp = 64'h0;
                milliseconds_tmp = 64'h0;
                state_tmp = 64'h3;
                if ((stage == 64'h0) && (((accepted & 32'h8000)) == 32'h0)) begin
                    failure=32'h4;
                end
                if ((stage == 64'h1) && (((((accepted & 32'h27FF)) != 32'h2008) || (((accepted & 32'h8000)) == 32'h0)))) begin
                    failure=32'h4;
                end
                if (stage == 64'h2) begin
                    remote_up1_tmp = 32'(accepted & 32'h7FF);
                    if ((((accepted & 32'h2008)) != 32'h8) || (((accepted & 32'h8000)) == 32'h0)) begin
                        failure=32'h4;
                    end
                end
                if (stage == 64'h3) begin
                    remote_seed_tmp = 32'(accepted & 32'h7FF);
                    if (((accepted & 32'h2000)) != 32'h0) begin
                        failure=32'h4;
                    end
                end
            end
        end
        if (((state>=64'h1 && state<=64'h3) && tick_ms_in) && milliseconds>=64'h577) begin
            failure=32'h6;
        end
        if (failure != 32'h0) begin
            reason_tmp = failure;
            retry_tmp = (('h1) != '0);
            state_tmp = 64'h0;
            stage_tmp = 64'h0;
            milliseconds_tmp = 64'h0;
            complete_count_tmp = 64'h0;
            role_wait_tmp = 64'h0;
            same_count_tmp = 64'h0;
            ack_count_tmp = 64'h0;
            candidate_tmp = 64'h0;
            ability_tmp = 64'h0;
            ack_word_tmp = 64'h0;
            remote_np_tmp = (('h0) != '0);
            remote_toggle_tmp = (('h0) != '0);
            local_toggle_tmp = (('h0) != '0);
            transmit_page_tmp = 64'h8001;
            if (failure == 32'h2) begin
                collisions_tmp = collisions + 64'h1;
                if (collisions>=64'h6) begin
                    state_tmp = 64'h7;
                end
            end
            else begin
                collisions_tmp = 64'h0;
                if (failure == 32'h1) begin
                    state_tmp = 64'h7;
                end
            end
        end
        if ((reset || restart_in) || !enable_in) begin
            state_tmp = 64'h0;
            stage_tmp = 64'h0;
            complete_count_tmp = 64'h0;
            role_wait_tmp = 64'h0;
            collisions_tmp = 64'h0;
            reason_tmp = 64'h0;
            milliseconds_tmp = 64'h0;
            local_up1_tmp = 64'h0;
            local_seed_tmp = 64'h0;
            remote_up1_tmp = 64'h0;
            remote_seed_tmp = 64'h0;
            transmit_page_tmp = 64'h8001;
            candidate_tmp = 64'h0;
            ability_tmp = 64'h0;
            ack_word_tmp = 64'h0;
            same_count_tmp = 64'h0;
            ack_count_tmp = 64'h0;
            remote_np_tmp = (('h0) != '0);
            remote_toggle_tmp = (('h0) != '0);
            local_toggle_tmp = (('h0) != '0);
            retry_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        stage_tmp = stage;
        complete_count_tmp = complete_count;
        role_wait_tmp = role_wait;
        collisions_tmp = collisions;
        reason_tmp = reason;
        milliseconds_tmp = milliseconds;
        local_up1_tmp = local_up1;
        local_seed_tmp = local_seed;
        remote_up1_tmp = remote_up1;
        remote_seed_tmp = remote_seed;
        transmit_page_tmp = transmit_page;
        candidate_tmp = candidate;
        ability_tmp = ability;
        ack_word_tmp = ack_word;
        same_count_tmp = same_count;
        ack_count_tmp = ack_count;
        remote_np_tmp = remote_np;
        remote_toggle_tmp = remote_toggle;
        local_toggle_tmp = local_toggle;
        retry_tmp = retry;

            _work(reset);

        state <= state_tmp;
        stage <= stage_tmp;
        complete_count <= complete_count_tmp;
        role_wait <= role_wait_tmp;
        collisions <= collisions_tmp;
        reason <= reason_tmp;
        milliseconds <= milliseconds_tmp;
        local_up1 <= local_up1_tmp;
        local_seed <= local_seed_tmp;
        remote_up1 <= remote_up1_tmp;
        remote_seed <= remote_seed_tmp;
        transmit_page <= transmit_page_tmp;
        candidate <= candidate_tmp;
        ability <= ability_tmp;
        ack_word <= ack_word_tmp;
        same_count <= same_count_tmp;
        ack_count <= ack_count_tmp;
        remote_np <= remote_np_tmp;
        remote_toggle <= remote_toggle_tmp;
        local_toggle <= local_toggle_tmp;
        retry <= retry_tmp;
    end

    assign transmit_page_out = transmit_page;

    assign transmit_enable_out = ((((state == 64'h1) || (state == 64'h2)) || (state == 64'h3)) != '0);

    assign pma_enable_out = (((state == 64'h5) || (state == 64'h6)) != '0);

    assign link_out = ((state == 64'h6) != '0);

    assign master_out = (((((state == 64'h5) || (state == 64'h6))) && roles__master_out) != '0);

    assign fault_out = ((state == 64'h7) != '0);

    assign reason_out = reason;

    assign retry_out = retry;


endmodule

`default_nettype none

import Predef_pkg::*;


module PCSTrellisDecoder (
    input wire clk
,   input wire reset
,   input wire symbol_enable_in
,   input wire[18-1:0] metric_in[8]
,   input wire[6-1:0] point_in[8]
,   input wire advance_in
,   input wire start_in
,   input wire end_in
,   output wire[8-1:0] data_out
,   output wire valid_out
,   output wire[3-1:0] state_out
);
    localparam  DEPTH = 'h20;


    // regs and combs
    reg[21-1:0] costs[8];
    reg[8-1:0] reachable;
    reg[2-1:0] selectors[8];
    reg[8-1:0] selected_bytes[8];
    reg[3-1:0] pending_control;
    reg[6-1:0] pending_count;
    reg[256-1:0] survivors[8];
    reg[6-1:0] count;
    reg[8-1:0] data_reg;
    reg[3-1:0] state_reg;
    reg valid_reg;
    reg[256-1:0] drain;
    reg[6-1:0] drain_left;
    reg[31-1:0] drain_valid;

    // members

    // tmp variables
    logic[21-1:0] costs_tmp[8];
    logic[8-1:0] reachable_tmp;
    logic[2-1:0] selectors_tmp[8];
    logic[8-1:0] selected_bytes_tmp[8];
    logic[3-1:0] pending_control_tmp;
    logic[6-1:0] pending_count_tmp;
    logic[256-1:0] survivors_tmp[8];
    logic[6-1:0] count_tmp;
    logic[8-1:0] data_reg_tmp;
    logic[3-1:0] state_reg_tmp;
    logic valid_reg_tmp;
    logic[256-1:0] drain_tmp;
    logic[6-1:0] drain_left_tmp;
    logic[31-1:0] drain_valid_tmp;


    function logic better (
        input logic[31:0] candidate
,       input logic[31:0] current
,       input logic candidate_valid
,       input logic current_valid
    );
        logic lower_less;
        logic[31:0] a;
        logic[31:0] b;
        lower_less=0;
        a=32'h0;
        b=32'h0;
        for (logic[31:0] group = 32'h0;group < 32'h4;group=group+1) begin
            a=((candidate >>> ((group*32'h5)))) & 32'h1F;
            b=((current >>> ((group*32'h5)))) & 32'h1F;
            lower_less=(a < b) || (((a == b) && lower_less));
        end
        return candidate_valid && ((!current_valid || ((((((((candidate ^ current)) >>> 32'sh14)) & 32'h1)) != 32'(lower_less)))));
    endfunction

    function logic[31:0] add_metric (
        input logic[31:0] path
,       input logic[31:0] branch
    );
        logic[31:0] _initial;
        logic[31:0] propagate;
        logic[31:0] _generate;
        _initial=path ^ branch;
        propagate=_initial;
        _generate=path & branch;
        for (logic[31:0] shift = 32'h1;shift < 32'h15;shift=shift*32'h2) begin
            _generate=_generate | ((propagate & ((_generate <<< shift))));
            propagate=propagate & ((propagate <<< shift));
        end
        return ((_initial ^ ((_generate <<< 'h1)))) & 32'h1FFFFF;
    endfunction

    function logic branch_better (
        input logic[31:0] a
,       input logic[31:0] b
,       input logic[31:0] c
,       input logic[31:0] d
,       input logic candidate_valid
,       input logic current_valid
    );
        logic[31:0] x;
        logic[31:0] y;
        logic[31:0] z;
        logic[31:0] sum;
        logic[31:0] carry;
        x=a;
        y=b;
        z=~c;
        sum=(x ^ y) ^ z;
        carry=(((((x & y)) | ((x & z))) | ((y & z)))) <<< 'h1;
        x=sum;
        y=carry;
        z=~d;
        sum=(x ^ y) ^ z;
        carry=(((((x & y)) | ((x & z))) | ((y & z)))) <<< 'h1;
        x=sum;
        y=carry;
        z=32'h2;
        sum=(x ^ y) ^ z;
        carry=(((((x & y)) | ((x & z))) | ((y & z)))) <<< 'h1;
        return candidate_valid && ((!current_valid || (((((add_metric(sum, carry) >>> 32'sh14)) & 32'h1)) != 32'h0)));
    endfunction

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[8-1:0][31:0] next_cost;
        logic[8-1:0][256-1:0] next_survivor;
        logic[4-1:0] candidate_valid;
        logic[64'(32'(DEPTH - 32'h1))-1:0] pending_valid;
        logic[31:0] predecessor;
        logic[2-1:0] winner;
        logic[4-1:0][31:0] candidates;
        logic[4-1:0][31:0] candidate_bytes;
        logic[4-1:0][31:0] branch;
        logic[2-1:0][31:0] cost2;
        logic[2-1:0][31:0] state2;
        logic[2-1:0] valid2;
        logic[64-1:0] outranks;
        logic is_best;
        logic[64'(32'(DEPTH*32'h8))-1:0] pending;
        logic[64'(32'(DEPTH*32'h8))-1:0] byte_mask;
        logic[64'(32'(DEPTH*32'h8))-1:0] scheduled_byte;
        logic[31:0] subset;
        logic[31:0] best_state;
        logic[31:0] sd6;
        logic[31:0] sd7;
        logic[31:0] winning_byte;
        logic[31:0] next_reachable;
        logic win01;
        logic win02;
        logic win03;
        logic win12;
        logic win13;
        logic win23;
        logic choose0;
        logic choose1;
        logic choose2;
        logic choose3;
        is_best=0;
        for (logic[31:0] i = 32'h0;i < 32'h40;i=i+1) begin
            outranks[i]=0;
        end
        subset=32'h0;
        best_state=32'h0;
        sd6=32'h0;
        sd7=32'h0;
        winner = 64'h0;
        winning_byte=32'h0;
        byte_mask = 'h0;
        scheduled_byte = 'h0;
        next_reachable=32'h0;
        predecessor=32'h0;
        win01=0;
        win02=0;
        win03=0;
        win12=0;
        win13=0;
        win23=0;
        choose0=0;
        choose1=0;
        choose2=0;
        choose3=0;
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            candidates[i]=32'h0;
            branch[i]=32'h0;
            candidate_bytes[i]=32'h0;
            candidate_valid[i]=0;
        end
        for (logic[31:0] i = 32'h0;i < 32'h2;i=i+1) begin
            cost2[i]=32'h0;
            state2[i]=32'h0;
            valid2[i]=0;
        end
        for (logic[31:0] state = 32'h0;state < 32'h8;state=state+1) begin
            next_cost[state]=32'h0;
            next_survivor[state] = 'h0;
        end
        valid_reg_tmp = (('h0) != '0);
        pending_control_tmp = signed'(32'((((((advance_in && !start_in))) ? ('h1) : ('h0)) | ((start_in) ? ('h2) : ('h0))) | ((end_in) ? ('h4) : ('h0))));
        pending_count_tmp = count;
        pending = drain << 'h8;
        pending_valid = drain_valid <<< 'h1;
        if (drain_left != 64'h0) begin
            data_reg_tmp = unsigned'(32'(drain >> (((DEPTH - 32'h1))*32'h8)));
            valid_reg_tmp = ((((drain_valid >>> ((DEPTH - 32'h2)))) & 64'h1)) != 64'h0;
            drain_tmp = pending;
            drain_valid_tmp = pending_valid;
            drain_left_tmp = drain_left - 64'h1;
        end
        if (advance_in) begin
            for (logic[31:0] destination = 32'h0;destination < 32'h8;destination=destination+1) begin
                winner = 64'h0;
                winning_byte=32'h0;
                for (logic[31:0] choice = 32'h0;choice < 32'h4;choice=choice+1) begin
                    predecessor=(((destination & 32'h1))*32'h4) + choice;
                    sd6=((((destination >>> 32'sh1)) ^ predecessor)) & 32'h1;
                    sd7=((((destination >>> 32'sh2)) ^ ((predecessor >>> 32'sh1)))) & 32'h1;
                    subset=((sd6*32'h4) + (sd7*32'h2)) + ((destination & 32'h1));
                    branch[choice]=metric_in[subset];
                    candidates[choice]=add_metric(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + choice])), unsigned'(32'(metric_in[subset])));
                    candidate_valid[choice]=((((unsigned'(32'(reachable)) >>> (((((destination & 32'h1))*32'h4) + choice)))) & 32'h1)) != 32'h0;
                    candidate_bytes[choice]=(unsigned'(32'(point_in[subset])) | ((sd6 <<< 'h6))) | ((sd7 <<< 'h7));
                end
                win01=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h1])), branch['h1], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h0])), branch['h0], candidate_valid['h1], candidate_valid['h0]);
                win02=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h2])), branch['h2], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h0])), branch['h0], candidate_valid['h2], candidate_valid['h0]);
                win03=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h3])), branch['h3], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h0])), branch['h0], candidate_valid['h3], candidate_valid['h0]);
                win12=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h2])), branch['h2], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h1])), branch['h1], candidate_valid['h2], candidate_valid['h1]);
                win13=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h3])), branch['h3], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h1])), branch['h1], candidate_valid['h3], candidate_valid['h1]);
                win23=branch_better(unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h3])), branch['h3], unsigned'(32'(costs[(((destination & 32'h1))*32'h4) + 32'h2])), branch['h2], candidate_valid['h3], candidate_valid['h2]);
                choose1=(win01 && !win12) && !win13;
                choose2=(win02 && win12) && !win23;
                choose3=(win03 && win13) && win23;
                winner = signed'(32'((((choose1 || choose3)) ? ('h1) : ('h0)) | (((choose2 || choose3)) ? ('h2) : ('h0))));
                choose0=(!win01 && !win02) && !win03;
                next_cost[destination]=((((candidates['h0] & ((32'h0 - 32'(choose0))))) | ((candidates['h1] & ((32'h0 - 32'(choose1)))))) | ((candidates['h2] & ((32'h0 - 32'(choose2)))))) | ((candidates['h3] & ((32'h0 - 32'(choose3)))));
                winning_byte=candidate_bytes[unsigned'(32'(winner))];
                if (candidate_valid[unsigned'(32'(winner))]) begin
                    next_reachable=next_reachable | 32'((('h1 <<< destination)));
                end
                selectors_tmp[destination] = winner;
                selected_bytes_tmp[destination] = winning_byte;
            end
            reachable_tmp = next_reachable;
            for (logic[31:0] state = 32'h0;state < 32'h8;state=state+1) begin
                costs_tmp[state] = next_cost[state];
            end
            if (count < unsigned'(64'(DEPTH))) begin
                count_tmp = count + 64'h1;
            end
        end
        if (((pending_control & 64'h1)) != 64'h0) begin
            for (logic[31:0] destination = 32'h0;destination < 32'h8;destination=destination+1) begin
                next_survivor[destination] = (survivors[(((destination & 32'h1))*32'h4) + unsigned'(32'(selectors[destination]))] << 'h8) | unsigned'(32'(selected_bytes[destination]));
                survivors_tmp[destination] = next_survivor[destination];
            end
            for (logic[31:0] i = 32'h0;i < 32'h8;i=i+1) begin
                for (logic[31:0] j = 32'h0;j < 32'h8;j=j+1) begin
                    if (i < j) begin
                        outranks[(i*32'h8) + j]=better(unsigned'(32'(costs[j])), unsigned'(32'(costs[i])), ((((reachable >>> j)) & 64'h1)) != 64'h0, ((((reachable >>> i)) & 64'h1)) != 64'h0);
                    end
                end
            end
            best_state=32'h0;
            for (logic[31:0] i = 32'h0;i < 32'h8;i=i+1) begin
                is_best=((((reachable >>> i)) & 64'h1)) != 64'h0;
                for (logic[31:0] j = 32'h0;j < 32'h8;j=j+1) begin
                    if (j < i) begin
                        is_best=is_best && outranks[((j*32'h8) + i)];
                    end
                    if (j > i) begin
                        is_best=is_best && !outranks[((i*32'h8) + j)];
                    end
                end
                if (is_best) begin
                    best_state=best_state | i;
                end
            end
            if (pending_count>=64'(32'((DEPTH - 32'h1))) || (drain_left == 64'h0)) begin
                valid_reg_tmp = pending_count>=64'(32'(DEPTH - 32'h1));
                data_reg_tmp = unsigned'(32'(next_survivor[best_state] >> (((DEPTH - 32'h1))*32'h8)));
            end
            state_reg_tmp = best_state;
            if (((pending_control & 64'h4)) != 64'h0) begin
                for (logic[31:0] future = 32'h0;future < (DEPTH - 32'h1);future=future+1) begin
                    if (pending_count>=64'(32'((DEPTH - 32'h2) - future))) begin
                        byte_mask = 'hFF;
                        byte_mask = byte_mask << ((((DEPTH - 32'h1) - future))*32'h8);
                        scheduled_byte = unsigned'(32'((next_survivor['h0] >> (((((DEPTH - 32'h2) - future))*32'h8))))) & 32'hFF;
                        pending = (pending & $bits(byte_mask)'(~(byte_mask))) | (scheduled_byte << ((((DEPTH - 32'h1) - future))*32'h8));
                        pending_valid = pending_valid | unsigned'(64'(signed'(32'((('h1 <<< (((DEPTH - 32'h2) - future))))))));
                    end
                end
                drain_tmp = pending;
                drain_valid_tmp = pending_valid;
                drain_left_tmp = 32'(DEPTH - 32'h1);
                if (pending_count>=64'(32'((DEPTH - 32'h1))) || (drain_left == 64'h0)) begin
                    valid_reg_tmp = pending_count>=64'(32'(DEPTH - 32'h1));
                    data_reg_tmp = unsigned'(32'(next_survivor['h0] >> (((DEPTH - 32'h1))*32'h8)));
                end
                state_reg_tmp = 64'h0;
            end
        end
        if (start_in || reset) begin
            for (logic[31:0] state = 32'h0;state < 32'h8;state=state+1) begin
                costs_tmp[state] = 64'h0;
                survivors_tmp[state] = 'h0;
                selectors_tmp[state] = 64'h0;
                selected_bytes_tmp[state] = 64'h0;
            end
            count_tmp = 64'h0;
            reachable_tmp = 64'h1;
        end
        if ((((pending_control & 64'h2)) != 64'h0) || reset) begin
            state_reg_tmp = 64'h0;
            if (reset || (drain_left == 64'h0)) begin
                data_reg_tmp = 64'h0;
                valid_reg_tmp = (('h0) != '0);
            end
        end
        if (reset) begin
            drain_tmp = 'h0;
            drain_left_tmp = 64'h0;
            drain_valid_tmp = 64'h0;
            pending_control_tmp = 64'h0;
            pending_count_tmp = 64'h0;
        end
        if (!symbol_enable_in && !reset) begin
            for (logic[31:0] state = 32'h0;state < 32'h8;state=state+1) begin
                costs_tmp[state] = costs[state];
                survivors_tmp[state] = survivors[state];
                selectors_tmp[state] = selectors[state];
                selected_bytes_tmp[state] = selected_bytes[state];
            end
            count_tmp = count;
            reachable_tmp = reachable;
            pending_control_tmp = pending_control;
            pending_count_tmp = pending_count;
            data_reg_tmp = data_reg;
            state_reg_tmp = state_reg;
            valid_reg_tmp = valid_reg;
            drain_tmp = drain;
            drain_left_tmp = drain_left;
            drain_valid_tmp = drain_valid;
        end
    end
    endtask

    always @(posedge clk) begin
        costs_tmp = costs;
        reachable_tmp = reachable;
        selectors_tmp = selectors;
        selected_bytes_tmp = selected_bytes;
        pending_control_tmp = pending_control;
        pending_count_tmp = pending_count;
        survivors_tmp = survivors;
        count_tmp = count;
        data_reg_tmp = data_reg;
        state_reg_tmp = state_reg;
        valid_reg_tmp = valid_reg;
        drain_tmp = drain;
        drain_left_tmp = drain_left;
        drain_valid_tmp = drain_valid;

            _work(reset);

        costs <= costs_tmp;
        reachable <= reachable_tmp;
        selectors <= selectors_tmp;
        selected_bytes <= selected_bytes_tmp;
        pending_control <= pending_control_tmp;
        pending_count <= pending_count_tmp;
        survivors <= survivors_tmp;
        count <= count_tmp;
        data_reg <= data_reg_tmp;
        state_reg <= state_reg_tmp;
        valid_reg <= valid_reg_tmp;
        drain <= drain_tmp;
        drain_left <= drain_left_tmp;
        drain_valid <= drain_valid_tmp;
    end

    assign data_out = data_reg;

    assign valid_out = valid_reg;

    assign state_out = state_reg;


endmodule

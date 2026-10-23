`default_nettype none

import Predef_pkg::*;


module PipelinedPCSBranchMetrics (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[4-1:0] negate_in
,   input wire advance_in
,   output wire[18-1:0] metric_out[8]
,   output wire[6-1:0] point_out[8]
,   output wire valid_out
);


    // regs and combs
    reg[16-1:0] squared[20];
    reg[16-1:0] nearest[12];
    reg[16-1:0] fixed_costs[4];
    reg[12-1:0] low_choices;
    reg[18-1:0] group_costs[48];
    reg[18-1:0] metrics[8];
    reg[6-1:0] group_points[48];
    reg[6-1:0] points[8];
    reg[4-1:0] accepted;

    // members

    // tmp variables
    logic[16-1:0] squared_tmp[20];
    logic[16-1:0] nearest_tmp[12];
    logic[16-1:0] fixed_costs_tmp[4];
    logic[12-1:0] low_choices_tmp;
    logic[18-1:0] group_costs_tmp[48];
    logic[18-1:0] metrics_tmp[8];
    logic[6-1:0] group_points_tmp[48];
    logic[6-1:0] points_tmp[8];
    logic[4-1:0] accepted_tmp;


    function logic[31:0] base (input logic[31:0] index);
        case (index)
        32'h0: begin
            return 32'h492;
        end
        32'h6: begin
            return 32'h692;
        end
        32'hC: begin
            return 32'h6D2;
        end
        32'h12: begin
            return 32'h4D2;
        end
        32'h18: begin
            return 32'h4DA;
        end
        32'h1E: begin
            return 32'h6DA;
        end
        32'h24: begin
            return 32'h69A;
        end
        32'h2A: begin
            return 32'h49A;
        end
        32'h1: begin
            return 32'h6DB;
        end
        32'h7: begin
            return 32'h4DB;
        end
        32'hD: begin
            return 32'h49B;
        end
        32'h13: begin
            return 32'h69B;
        end
        32'h19: begin
            return 32'h693;
        end
        32'h1F: begin
            return 32'h493;
        end
        32'h25: begin
            return 32'h4D3;
        end
        32'h2B: begin
            return 32'h6D3;
        end
        32'h2: begin
            return 32'h494;
        end
        32'h8: begin
            return 32'h694;
        end
        32'hE: begin
            return 32'h6D4;
        end
        32'h14: begin
            return 32'h4D4;
        end
        32'h1A: begin
            return 32'h4DC;
        end
        32'h20: begin
            return 32'h6DC;
        end
        32'h26: begin
            return 32'h69C;
        end
        32'h2C: begin
            return 32'h49C;
        end
        32'h3: begin
            return 32'h512;
        end
        32'h9: begin
            return 32'h712;
        end
        32'hF: begin
            return 32'h51B;
        end
        32'h15: begin
            return 32'h71B;
        end
        32'h1B: begin
            return 32'h713;
        end
        32'h21: begin
            return 32'h513;
        end
        32'h27: begin
            return 32'h71A;
        end
        32'h2D: begin
            return 32'h51A;
        end
        32'h4: begin
            return 32'h4A2;
        end
        32'hA: begin
            return 32'h6A2;
        end
        32'h10: begin
            return 32'h6E2;
        end
        32'h16: begin
            return 32'h4E2;
        end
        32'h1C: begin
            return 32'h6A3;
        end
        32'h22: begin
            return 32'h4A3;
        end
        32'h28: begin
            return 32'h4E3;
        end
        32'h2E: begin
            return 32'h6E3;
        end
        32'h5: begin
            return 32'h892;
        end
        32'hB: begin
            return 32'h8DB;
        end
        32'h11: begin
            return 32'h89B;
        end
        32'h17: begin
            return 32'h8D2;
        end
        32'h1D: begin
            return 32'h8DA;
        end
        32'h23: begin
            return 32'h893;
        end
        32'h29: begin
            return 32'h8D3;
        end
        32'h2F: begin
            return 32'h89A;
        end
        default: begin
            return 32'h0;
        end
        endcase
    endfunction

    function logic less (
        input logic[31:0] a
,       input logic[31:0] b
    );
        return (((a >>> 32'shC)) < ((b >>> 32'shC))) || (((((a >>> 32'shC)) == ((b >>> 32'shC))) && (((((((a >>> 32'sh6)) & 32'h3F)) < ((((b >>> 32'sh6)) & 32'h3F))) || (((((((a >>> 32'sh6)) & 32'h3F)) == ((((b >>> 32'sh6)) & 32'h3F))) && (((a & 32'h3F)) < ((b & 32'h3F)))))))));
    endfunction

    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h8;gi=gi+1) begin
            assign metric_out[gi] = metrics[gi];
            assign point_out[gi] = points[gi];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[8-1:0] distance;
        logic choose_right;
        logic signed[31:0] sample;
        logic signed[31:0] difference;
        logic[17-1:0] left_sum;
        logic[17-1:0] right_sum;
        logic[31:0] choices;
        logic[31:0] _packed;
        logic[31:0] fixed;
        logic[31:0] level;
        logic[31:0] point;
        logic[31:0] bit_index;
        logic[4-1:0][31:0] coordinates;
        logic[3-1:0][31:0] pair_cost;
        logic[3-1:0][31:0] pair_point;
        logic[31:0] left_cost;
        logic[31:0] left_point;
        sample='h0;
        difference='h0;
        distance = 64'h0;
        left_sum = 64'h0;
        right_sum = 64'h0;
        choices=32'h0;
        _packed=32'h0;
        fixed=32'h0;
        level=32'h0;
        point=32'h0;
        bit_index=32'h0;
        left_cost=32'h0;
        left_point=32'h0;
        choose_right=0;
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            coordinates[i]=32'h0;
        end
        for (logic[31:0] i = 32'h0;i < 32'h3;i=i+1) begin
            pair_cost[i]=32'h0;
            pair_point[i]=32'h0;
        end
        accepted_tmp = 32'(((unsigned'(32'(accepted)) <<< 'h1)) | unsigned'(32'(((advance_in) ? ('h1) : ('h0)))));
        if (advance_in) begin
            for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                sample=signed'(32'((((unsigned'(32'(samples_in)) >>> ((32'h8*lane)))) & 32'hFF))) - 'h80;
                if (((((negate_in >>> lane)) & 64'h1) != '0)) begin
                    sample=-sample;
                end
                for (logic[31:0] code = 32'h0;code < 32'h5;code=code+1) begin
                    difference=sample - (((signed'(32'(code)) - 'h2))*'h20);
                    distance = unsigned'(32'((difference < 32'sh0) ? (-difference) : (difference)));
                    squared_tmp[(lane*32'h5) + code] = 32'(64'(unsigned'(32'(distance)))*64'(unsigned'(32'(distance))));
                end
            end
        end
        if (((accepted & 64'h1)) != 64'h0) begin
            for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                fixed_costs_tmp[lane] = squared[(lane*32'h5) + 32'h4];
                for (logic[31:0] pair = 32'h0;pair < 32'h3;pair=pair+1) begin
                    choose_right=squared[((lane*32'h5) + pair)] < squared[(((lane*32'h5) + pair) + 32'h2)];
                    nearest_tmp[(lane*32'h3) + pair] = (choose_right) ? (squared[(lane*32'h5) + pair]) : (squared[((lane*32'h5) + pair) + 32'h2]);
                    if (choose_right) begin
                        choices=choices | 32'((('h1 <<< (((lane*32'h3) + pair)))));
                    end
                end
            end
            low_choices_tmp = choices;
        end
        if (((accepted & 64'h2)) != 64'h0) begin
            for (logic[31:0] subset = 32'h0;subset < 32'h8;subset=subset+1) begin
                for (logic[31:0] group = 32'h0;group < 32'h6;group=group+1) begin
                    _packed=base((subset*32'h6) + group);
                    fixed=(group == 32'h2) ? ('h0) : ((group == 32'h3) ? ('h2) : ((group == 32'h4) ? ('h1) : ((group == 32'h5) ? ('h3) : ('h4))));
                    point=(group == 32'h0) ? (32'h0) : ((group == 32'h1) ? (32'h10) : (32'h20 + (((group - 32'h2))*32'h8)));
                    bit_index=32'h0;
                    for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                        level=((_packed >>> ((32'h3*lane)))) & 32'h7;
                        coordinates[lane]=fixed_costs[lane];
                        if (lane != fixed) begin
                            coordinates[lane]=nearest[((lane*32'h3) + level) - 32'h2];
                            if (((((low_choices >>> ((((lane*32'h3) + level) - 32'h2)))) & 64'h1) != '0)) begin
                                point=point | 32'((('h1 <<< bit_index)));
                            end
                            bit_index=bit_index + 32'h1;
                        end
                    end
                    left_sum = unsigned'(32'(coordinates['h0] + coordinates['h1]));
                    right_sum = unsigned'(32'(coordinates['h2] + coordinates['h3]));
                    group_costs_tmp[(subset*32'h6) + group] = 32'(unsigned'(32'(left_sum)) + unsigned'(32'(right_sum)));
                    group_points_tmp[(subset*32'h6) + group] = point;
                end
            end
        end
        if (((accepted & 64'h4)) != 64'h0) begin
            for (logic[31:0] subset = 32'h0;subset < 32'h8;subset=subset+1) begin
                for (logic[31:0] pair = 32'h0;pair < 32'h3;pair=pair+1) begin
                    choose_right=less(unsigned'(32'(group_costs[((subset*32'h6) + (32'h2*pair)) + 32'h1])), unsigned'(32'(group_costs[(subset*32'h6) + (32'h2*pair)])));
                    pair_cost[pair]=(choose_right) ? (unsigned'(32'(group_costs[((subset*32'h6) + (32'h2*pair)) + 32'h1]))) : (unsigned'(32'(group_costs[(subset*32'h6) + (32'h2*pair)])));
                    pair_point[pair]=(choose_right) ? (unsigned'(32'(group_points[((subset*32'h6) + (32'h2*pair)) + 32'h1]))) : (unsigned'(32'(group_points[(subset*32'h6) + (32'h2*pair)])));
                end
                choose_right=less(pair_cost['h1], pair_cost['h0]);
                left_cost=(choose_right) ? (pair_cost['h1]) : (pair_cost['h0]);
                left_point=(choose_right) ? (pair_point['h1]) : (pair_point['h0]);
                choose_right=less(pair_cost['h2], left_cost);
                metrics_tmp[subset] = unsigned'(32'((choose_right) ? (pair_cost['h2]) : (left_cost)));
                points_tmp[subset] = unsigned'(32'((choose_right) ? (pair_point['h2]) : (left_point)));
            end
        end
        if (reset) begin
            accepted_tmp = 64'h0;
            low_choices_tmp = 64'h0;
            for (logic[31:0] i = 32'h0;i < 32'h14;i=i+1) begin
                squared_tmp[i] = 64'h0;
            end
            for (logic[31:0] i = 32'h0;i < 32'hC;i=i+1) begin
                nearest_tmp[i] = 64'h0;
            end
            for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
                fixed_costs_tmp[i] = 64'h0;
            end
            for (logic[31:0] i = 32'h0;i < 32'h30;i=i+1) begin
                group_costs_tmp[i] = 64'h0;
                group_points_tmp[i] = 64'h0;
            end
            for (logic[31:0] i = 32'h0;i < 32'h8;i=i+1) begin
                metrics_tmp[i] = 64'h0;
                points_tmp[i] = 64'h0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        squared_tmp = squared;
        nearest_tmp = nearest;
        fixed_costs_tmp = fixed_costs;
        low_choices_tmp = low_choices;
        group_costs_tmp = group_costs;
        metrics_tmp = metrics;
        group_points_tmp = group_points;
        points_tmp = points;
        accepted_tmp = accepted;

            _work(reset);

        squared <= squared_tmp;
        nearest <= nearest_tmp;
        fixed_costs <= fixed_costs_tmp;
        low_choices <= low_choices_tmp;
        group_costs <= group_costs_tmp;
        metrics <= metrics_tmp;
        group_points <= group_points_tmp;
        points <= points_tmp;
        accepted <= accepted_tmp;
    end

    assign valid_out = ((((accepted & 64'h8)) != 64'h0) != '0);


endmodule

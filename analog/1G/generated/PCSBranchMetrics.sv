`default_nettype none

import Predef_pkg::*;


module PCSBranchMetrics (
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
    reg[18-1:0] metrics[8];
    reg[6-1:0] points[8];
    reg valid_reg;

    // members

    // tmp variables
    logic[18-1:0] metrics_tmp[8];
    logic[6-1:0] points_tmp[8];
    logic valid_reg_tmp;


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

    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h8;gi=gi+1) begin
            assign metric_out[gi] = metrics[gi];
            assign point_out[gi] = points[gi];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[20-1:0][16-1:0] squared;
        logic[8-1:0] distance;
        logic signed[31:0] sample;
        logic signed[31:0] difference;
        logic[31:0] _packed;
        logic[31:0] level;
        logic[31:0] cost;
        logic[31:0] alternative;
        logic[31:0] group_cost;
        logic[31:0] group_point;
        logic[31:0] _bit;
        logic[31:0] fixed;
        logic[31:0] best_cost;
        logic[31:0] best_point;
        valid_reg_tmp = advance_in;
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            sample=signed'(32'((((unsigned'(32'(samples_in)) >>> ((32'h8*lane)))) & 32'hFF))) - 'h80;
            if (((((negate_in >>> lane)) & 64'h1) != '0)) begin
                sample=-sample;
            end
            for (logic[31:0] code = 32'h0;code < 32'h5;code=code+1) begin
                difference=sample - (((signed'(32'(code)) - 'h2))*'h20);
                distance = unsigned'(32'((difference < 32'sh0) ? (-difference) : (difference)));
                squared[(lane*32'h5) + code] = 32'(64'(unsigned'(32'(distance)))*64'(unsigned'(32'(distance))));
            end
        end
        for (logic[31:0] subset = 32'h0;subset < 32'h8;subset=subset+1) begin
            best_cost=32'h3FFFF;
            best_point=32'h0;
            for (logic[31:0] group = 32'h0;group < 32'h6;group=group+1) begin
                _packed=base((subset*32'h6) + group);
                fixed=(group == 32'h2) ? ('h0) : ((group == 32'h3) ? ('h2) : ((group == 32'h4) ? ('h1) : ((group == 32'h5) ? ('h3) : ('h4))));
                group_point=(group == 32'h0) ? (32'h0) : ((group == 32'h1) ? (32'h10) : (32'h20 + (((group - 32'h2))*32'h8)));
                group_cost=32'h0;
                _bit=32'h0;
                for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                    level=((_packed >>> ((32'h3*lane)))) & 32'h7;
                    cost=squared[(lane*32'h5) + level];
                    if (lane != fixed) begin
                        alternative=squared[((lane*32'h5) + level) - 32'h2];
                        if (alternative < cost) begin
                            cost=alternative;
                            group_point=group_point | 32'((('h1 <<< _bit)));
                        end
                        _bit=_bit + 32'h1;
                    end
                    group_cost=group_cost + cost;
                end
                if (group_cost < best_cost) begin
                    best_cost=group_cost;
                    best_point=group_point;
                end
            end
            if (advance_in) begin
                metrics_tmp[subset] = best_cost;
                points_tmp[subset] = best_point;
            end
            if (reset) begin
                metrics_tmp[subset] = 64'h0;
                points_tmp[subset] = 64'h0;
            end
        end
        if (reset) begin
            valid_reg_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        metrics_tmp = metrics;
        points_tmp = points;
        valid_reg_tmp = valid_reg;

            _work(reset);

        metrics <= metrics_tmp;
        points <= points_tmp;
        valid_reg <= valid_reg_tmp;
    end

    assign valid_out = valid_reg;


endmodule

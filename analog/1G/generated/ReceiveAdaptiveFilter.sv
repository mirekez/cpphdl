`default_nettype none

import Predef_pkg::*;


module ReceiveAdaptiveFilter (
    input wire clk
,   input wire reset
,   input wire[8-1:0] sample_in
,   input wire[12-1:0] local_in
,   input wire[3-1:0] desired_in
,   input wire train_in
,   input wire advance_in
,   output wire[8-1:0] sample_out
,   output wire valid_out
,   output wire fault_out
,   output wire[18-1:0] coefficient_out[24]
);


    // regs and combs
    reg[18-1:0] weights[24];
    reg[8-1:0] history[24];
    reg[8-1:0] output_reg;
    reg valid_reg;
    reg fault_reg;

    // members

    // tmp variables
    logic[18-1:0] weights_tmp[24];
    logic[8-1:0] history_tmp[24];
    logic[8-1:0] output_reg_tmp;
    logic valid_reg_tmp;
    logic fault_reg_tmp;


    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h18;gi=gi+1) begin
            assign coefficient_out[gi] = weights[gi];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic signed[24-1:0][31:0] feature;
        logic signed[31:0] coefficient;
        logic signed[31:0] total;
        logic signed[31:0] filtered;
        logic signed[31:0] error;
        logic signed[31:0] product;
        logic signed[31:0] delta;
        logic signed[31:0] next_weight;
        logic signed[31:0] code;
        logic[31:0] energy;
        logic[31:0] shift;
        logic[31:0] magnitude;
        logic[31:0] invalid;
        total='h0;
        filtered='h0;
        error='h0;
        product='h0;
        delta='h0;
        next_weight='h0;
        coefficient='h0;
        code='h0;
        energy=32'h0;
        shift=32'h0;
        magnitude=32'h0;
        invalid=32'h0;
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            feature[i]='h0;
        end
        valid_reg_tmp = (('h0) != '0);
        if (advance_in && !((fault_reg) != '0)) begin
            feature['h0]=signed'(32'(unsigned'(32'(sample_in)))) - 'h80;
            for (logic[31:0] i = 32'h1;i < 32'h4;i=i+1) begin
                feature[i]=signed'(32'((unsigned'(32'(history[(i - 32'h1)])) ^ 32'h80))) - 'h80;
            end
            for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                code=((unsigned'(32'(local_in)) >>> ((lane*32'h3)))) & 32'h7;
                invalid=invalid | 32'((code > 32'sh4));
                feature[32'h4 + (lane*32'h5)]=(('h2 - code))*'h20;
                for (logic[31:0] tap = 32'h1;tap < 32'h5;tap=tap+1) begin
                    feature[(32'h4 + (lane*32'h5)) + tap]=signed'(32'((unsigned'(32'(history[((32'h3 + (lane*32'h5)) + tap)])) ^ 32'h80))) - 'h80;
                end
            end
            for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
                coefficient=signed'(32'((unsigned'(32'(weights[i])) ^ 32'h20000))) - 'h20000;
                total=total + (coefficient*feature[i]);
                energy=energy + unsigned'(32'((feature[i]*feature[i])));
                history_tmp[i] = feature[i];
            end
            filtered=(total < 32'sh0) ? (-signed'(32'(unsigned'(32'(-total)) >>> 32'shE))) : (signed'(32'(unsigned'(32'(total)) >>> 32'shE)));
            output_reg_tmp = signed'(32'((filtered < signed'(32'(-'h80))) ? ('h0) : ((filtered > 32'sh7F) ? ('hFF) : (filtered + 'h80))));
            valid_reg_tmp = (('h1) != '0);
            if (train_in) begin
                invalid=invalid | 32'((desired_in > 64'h4));
                error=(((signed'(32'(unsigned'(32'(desired_in)))) - 'h2))*'h20) - filtered;
                error=(error < signed'(32'(-'hFF))) ? (-'hFF) : ((error > 32'shFF) ? ('hFF) : (error));
                for (logic[31:0] _bit = 32'h0;_bit < 32'h12;_bit=_bit+1) begin
                    if (energy > (('h1 <<< _bit))) begin
                        shift=_bit + 32'h1;
                    end
                end
                for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
                    coefficient=signed'(32'((unsigned'(32'(weights[i])) ^ 32'h20000))) - 'h20000;
                    product=(error*feature[i])*'h4000;
                    magnitude=(product < 32'sh0) ? (-product) : (product);
                    delta=magnitude >>> shift;
                    if (product < 32'sh0) begin
                        delta=-delta;
                    end
                    next_weight=coefficient + delta;
                    next_weight=(next_weight < signed'(32'(-'h1FFFF))) ? (-'h1FFFF) : ((next_weight > 32'sh1FFFF) ? ('h1FFFF) : (next_weight));
                    weights_tmp[i] = next_weight;
                end
            end
            if (((invalid) != '0)) begin
                output_reg_tmp = 64'h80;
                valid_reg_tmp = (('h0) != '0);
                fault_reg_tmp = (('h1) != '0);
            end
        end
        if (reset) begin
            for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
                weights_tmp[i] = signed'(32'((i == 32'h0) ? ('h5555) : ((i == 32'h1) ? (-'h1C72) : ((i == 32'h2) ? ('h97B) : ((i == 32'h3) ? (-'h329) : ('h0))))));
                history_tmp[i] = 64'h0;
            end
            output_reg_tmp = 64'h80;
            valid_reg_tmp = (('h0) != '0);
            fault_reg_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        weights_tmp = weights;
        history_tmp = history;
        output_reg_tmp = output_reg;
        valid_reg_tmp = valid_reg;
        fault_reg_tmp = fault_reg;

            _work(reset);

        weights <= weights_tmp;
        history <= history_tmp;
        output_reg <= output_reg_tmp;
        valid_reg <= valid_reg_tmp;
        fault_reg <= fault_reg_tmp;
    end

    assign sample_out = output_reg;

    assign valid_out = valid_reg;

    assign fault_out = fault_reg;


endmodule

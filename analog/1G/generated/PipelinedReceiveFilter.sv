`default_nettype none

import Predef_pkg::*;


module PipelinedReceiveFilter (
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
    reg[8-1:0] history[4];
    reg[8-1:0] features[7][4];
    reg[3-1:0] local_history[20];
    reg[3-1:0] local_features[7][20];
    reg[17-1:0] product_low[24];
    reg[17-1:0] product_high[24];
    reg[26-1:0] products[24];
    reg[28-1:0] sum4[6];
    reg[29-1:0] sum8[3];
    reg[31-1:0] total;
    reg[16-1:0] squares[24];
    reg[11-1:0] correction_low[24];
    reg[11-1:0] correction_high[24];
    reg[24-1:0] product_negative;
    reg[26-1:0] correction_magnitudes[24];
    reg[18-1:0] adjustment_magnitudes[24];
    reg[24-1:0] correction_negative;
    reg[24-1:0] adjustment_negative;
    reg[18-1:0] energy4[6];
    reg[19-1:0] energy8[3];
    reg[20-1:0] energy;
    reg[20-1:0] energy_delay;
    reg[3-1:0] targets[6];
    reg[10-1:0] accepted;
    reg[10-1:0] training;
    reg[9-1:0] error_reg;
    reg[5-1:0] exponent;
    reg[5-1:0] correction_exponent;
    reg[5-1:0] adjustment_exponent;
    reg[8-1:0] output_reg;
    reg valid_reg;
    reg fault_reg;

    // members

    // tmp variables
    logic[18-1:0] weights_tmp[24];
    logic[8-1:0] history_tmp[4];
    logic[8-1:0] features_tmp[7][4];
    logic[3-1:0] local_history_tmp[20];
    logic[3-1:0] local_features_tmp[7][20];
    logic[17-1:0] product_low_tmp[24];
    logic[17-1:0] product_high_tmp[24];
    logic[26-1:0] products_tmp[24];
    logic[28-1:0] sum4_tmp[6];
    logic[29-1:0] sum8_tmp[3];
    logic[31-1:0] total_tmp;
    logic[16-1:0] squares_tmp[24];
    logic[11-1:0] correction_low_tmp[24];
    logic[11-1:0] correction_high_tmp[24];
    logic[24-1:0] product_negative_tmp;
    logic[26-1:0] correction_magnitudes_tmp[24];
    logic[18-1:0] adjustment_magnitudes_tmp[24];
    logic[24-1:0] correction_negative_tmp;
    logic[24-1:0] adjustment_negative_tmp;
    logic[18-1:0] energy4_tmp[6];
    logic[19-1:0] energy8_tmp[3];
    logic[20-1:0] energy_tmp;
    logic[20-1:0] energy_delay_tmp;
    logic[3-1:0] targets_tmp[6];
    logic[10-1:0] accepted_tmp;
    logic[10-1:0] training_tmp;
    logic[9-1:0] error_reg_tmp;
    logic[5-1:0] exponent_tmp;
    logic[5-1:0] correction_exponent_tmp;
    logic[5-1:0] adjustment_exponent_tmp;
    logic[8-1:0] output_reg_tmp;
    logic valid_reg_tmp;
    logic fault_reg_tmp;


    function logic signed[31:0] signed_value (
        input logic[31:0] value
,       input logic[31:0] sign
    );
        return signed'(32'((value ^ sign))) - signed'(32'(sign));
    endfunction

    generate  // _assign
        genvar gi;
        for (gi = 32'h0;gi < 32'h18;gi=gi+1) begin
            assign coefficient_out[gi] = weights[gi];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[20-1:0] candidate;
        logic signed[24-1:0][31:0] current;
        logic signed[24-1:0][31:0] stage0;
        logic signed[24-1:0][31:0] stage6;
        logic signed[31:0] a;
        logic signed[31:0] b;
        logic signed[31:0] c;
        logic signed[31:0] d;
        logic signed[31:0] value;
        logic signed[31:0] error;
        logic signed[31:0] product;
        logic signed[31:0] delta;
        logic signed[31:0] next;
        logic[31:0] code;
        logic[31:0] invalid;
        logic[31:0] shift;
        logic[31:0] magnitude;
        logic[8-1:0] absolute_error;
        logic[8-1:0] absolute_feature;
        logic[9-1:0] weight_low;
        logic[9-1:0] weight_high;
        logic[17-1:0] magnitude_low;
        logic[17-1:0] magnitude_high;
        a='h0;
        b='h0;
        c='h0;
        d='h0;
        value='h0;
        error='h0;
        product='h0;
        delta='h0;
        next='h0;
        code=32'h0;
        invalid=32'h0;
        shift=32'h0;
        magnitude=32'h0;
        candidate = 64'h0;
        absolute_error = 64'h0;
        absolute_feature = 64'h0;
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            current[i]='h0;
        end
        current['h0]=signed'(32'(unsigned'(32'(sample_in)))) - 'h80;
        for (logic[31:0] i = 32'h1;i < 32'h4;i=i+1) begin
            current[i]=signed_value(unsigned'(32'(history[i - 32'h1])), 32'h80);
        end
        for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
            code=((unsigned'(32'(local_in)) >>> ((lane*32'h3)))) & 32'h7;
            invalid=invalid | 32'((code > 32'h4));
            current[32'h4 + (lane*32'h5)]=(('h2 - signed'(32'(code))))*'h20;
            for (logic[31:0] tap = 32'h1;tap < 32'h5;tap=tap+1) begin
                current[(32'h4 + (lane*32'h5)) + tap]=(('h2 - signed'(32'(unsigned'(32'(local_history[(((lane*32'h5) + tap) - 32'h1)]))))))*'h20;
            end
        end
        invalid=unsigned'(32'(advance_in)) & ((invalid | 32'((train_in && (desired_in > 64'h4)))));
        accepted_tmp = ((accepted <<< 'h1)) | 64'(unsigned'(32'(advance_in)));
        training_tmp = ((training <<< 'h1)) | 64'(32'((advance_in && train_in)));
        targets_tmp['h0] = desired_in;
        for (logic[31:0] stage = 32'h1;stage < 32'h6;stage=stage+1) begin
            targets_tmp[stage] = targets[stage - 32'h1];
        end
        if ((advance_in && !((invalid) != '0)) && !((fault_reg) != '0)) begin
            for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
                history_tmp[i] = current[i];
                features_tmp['h0][i] = current[i];
            end
            for (logic[31:0] lane = 32'h0;lane < 32'h4;lane=lane+1) begin
                code=((unsigned'(32'(local_in)) >>> ((lane*32'h3)))) & 32'h7;
                local_history_tmp[lane*32'h5] = code;
                local_features_tmp['h0][lane*32'h5] = code;
                for (logic[31:0] tap = 32'h1;tap < 32'h5;tap=tap+1) begin
                    local_history_tmp[(lane*32'h5) + tap] = local_history[((lane*32'h5) + tap) - 32'h1];
                    local_features_tmp['h0][(lane*32'h5) + tap] = local_history[((lane*32'h5) + tap) - 32'h1];
                end
            end
        end
        for (logic[31:0] stage = 32'h1;stage < 32'h7;stage=stage+1) begin
            for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
                features_tmp[stage][i] = features[stage - 32'h1][i];
            end
            for (logic[31:0] i = 32'h0;i < 32'h14;i=i+1) begin
                local_features_tmp[stage][i] = local_features[stage - 32'h1][i];
            end
        end
        for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
            stage0[i]=signed_value(unsigned'(32'(features['h0][i])), 32'h80);
            stage6[i]=signed_value(unsigned'(32'(features['h6][i])), 32'h80);
        end
        for (logic[31:0] i = 32'h0;i < 32'h14;i=i+1) begin
            stage0[i + 32'h4]=(('h2 - signed'(32'(unsigned'(32'(local_features['h0][i]))))))*'h20;
            stage6[i + 32'h4]=(('h2 - signed'(32'(unsigned'(32'(local_features['h6][i]))))))*'h20;
        end
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            value=stage0[i];
            absolute_feature = unsigned'(32'((value < 32'sh0) ? (-value) : (value)));
            weight_low = 32'(unsigned'(32'(weights[i])) & 32'h1FF);
            a=signed_value(unsigned'(32'(weights[i])) >>> 32'sh9, 32'h100);
            weight_high = unsigned'(32'((a < 32'sh0) ? (-a) : (a)));
            if (i < 32'h4) begin
                magnitude_low = 32'(64'(unsigned'(32'(weight_low)))*64'(unsigned'(32'(absolute_feature))));
                magnitude_high = 32'(64'(unsigned'(32'(weight_high)))*64'(unsigned'(32'(absolute_feature))));
                squares_tmp[i] = 32'(64'(unsigned'(32'(absolute_feature)))*64'(unsigned'(32'(absolute_feature))));
            end
            else begin
                code=local_features['h0][i - 32'h4];
                magnitude_low = unsigned'(32'(((code == 32'h0) || (code == 32'h4)) ? (unsigned'(32'(weight_low)) <<< 'h6) : (((code == 32'h1) || (code == 32'h3)) ? (unsigned'(32'(weight_low)) <<< 'h5) : (32'h0))));
                magnitude_high = unsigned'(32'(((code == 32'h0) || (code == 32'h4)) ? (unsigned'(32'(weight_high)) <<< 'h6) : (((code == 32'h1) || (code == 32'h3)) ? (unsigned'(32'(weight_high)) <<< 'h5) : (32'h0))));
                squares_tmp[i] = signed'(32'(((code == 32'h0) || (code == 32'h4)) ? ('h1000) : (((code == 32'h1) || (code == 32'h3)) ? ('h400) : ('h0))));
            end
            product_low_tmp[i] = unsigned'(32'((value < 32'sh0) ? (-unsigned'(32'(magnitude_low))) : (unsigned'(32'(magnitude_low)))));
            product_high_tmp[i] = unsigned'(32'((signed'(32'(((a < 32'sh0)))) != signed'(32'(((value < 32'sh0))))) ? (-unsigned'(32'(magnitude_high))) : (unsigned'(32'(magnitude_high)))));
            products_tmp[i] = signed'(32'(signed_value(unsigned'(32'(product_low[i])), 32'h10000) + (signed_value(unsigned'(32'(product_high[i])), 32'h10000)*'h200)));
        end
        for (logic[31:0] group = 32'h0;group < 32'h6;group=group+1) begin
            a=signed_value(unsigned'(32'(products[group*32'h4])), 32'h2000000);
            b=signed_value(unsigned'(32'(products[(group*32'h4) + 32'h1])), 32'h2000000);
            c=signed_value(unsigned'(32'(products[(group*32'h4) + 32'h2])), 32'h2000000);
            d=signed_value(unsigned'(32'(products[(group*32'h4) + 32'h3])), 32'h2000000);
            sum4_tmp[group] = signed'(32'(((a + b)) + ((c + d))));
            energy4_tmp[group] = ((squares[(group*32'h4)] + squares[((group*32'h4) + 32'h1)])) + ((squares[((group*32'h4) + 32'h2)] + squares[((group*32'h4) + 32'h3)]));
        end
        for (logic[31:0] group = 32'h0;group < 32'h3;group=group+1) begin
            sum8_tmp[group] = signed'(32'(signed_value(unsigned'(32'(sum4[(group*32'h2)])), 32'h8000000) + signed_value(unsigned'(32'(sum4[((group*32'h2) + 32'h1)])), 32'h8000000)));
            energy8_tmp[group] = energy4[(group*32'h2)] + energy4[((group*32'h2) + 32'h1)];
        end
        total_tmp = signed'(32'(((signed_value(unsigned'(32'(sum8['h0])), 32'h10000000) + signed_value(unsigned'(32'(sum8['h1])), 32'h10000000))) + signed_value(unsigned'(32'(sum8['h2])), 32'h10000000)));
        energy_tmp = ((energy8['h0] + energy8['h1])) + energy8['h2];
        energy_delay_tmp = energy;
        value=signed_value(unsigned'(32'(total)), 32'h40000000);
        value=(value < 32'sh0) ? (-signed'(32'(unsigned'(32'(-value)) >>> 32'shE))) : (signed'(32'(unsigned'(32'(value)) >>> 32'shE)));
        error=(((signed'(32'(unsigned'(32'(targets['h5])))) - 'h2))*'h20) - value;
        error_reg_tmp = signed'(32'((error < signed'(32'(-'hFF))) ? (-'hFF) : ((error > 32'shFF) ? ('hFF) : (error))));
        for (logic[31:0] _bit = 32'h0;_bit < 32'h12;_bit=_bit+1) begin
            if (energy_delay > 64'(32'((('h1 <<< _bit))))) begin
                shift=_bit + 32'h1;
            end
        end
        exponent_tmp = shift;
        valid_reg_tmp = (('h0) != '0);
        if ((((((accepted & 64'h20))) != '0) && !((invalid) != '0)) && !((fault_reg) != '0)) begin
            output_reg_tmp = signed'(32'((value < signed'(32'(-'h80))) ? ('h0) : ((value > 32'sh7F) ? ('hFF) : (value + 'h80))));
            valid_reg_tmp = (('h1) != '0);
        end
        error=signed_value(unsigned'(32'(error_reg)), 32'h100);
        absolute_error = unsigned'(32'((error < 32'sh0) ? (-error) : (error)));
        product_negative_tmp = 64'h0;
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            value=stage6[i];
            absolute_feature = unsigned'(32'((value < 32'sh0) ? (-value) : (value)));
            if (i < 32'h4) begin
                correction_low_tmp[i] = ((absolute_error & 64'hF))*absolute_feature;
                correction_high_tmp[i] = ((absolute_error >>> 32'sh4))*absolute_feature;
            end
            else begin
                code=local_features['h6][i - 32'h4];
                correction_low_tmp[i] = unsigned'(32'(((code == 32'h0) || (code == 32'h4)) ? (((unsigned'(32'(absolute_error)) & 32'hF)) <<< 'h6) : (((code == 32'h1) || (code == 32'h3)) ? (((unsigned'(32'(absolute_error)) & 32'hF)) <<< 'h5) : (32'h0))));
                correction_high_tmp[i] = unsigned'(32'(((code == 32'h0) || (code == 32'h4)) ? (((unsigned'(32'(absolute_error)) >>> 32'sh4)) <<< 'h6) : (((code == 32'h1) || (code == 32'h3)) ? (((unsigned'(32'(absolute_error)) >>> 32'sh4)) <<< 'h5) : (32'h0))));
            end
            if (signed'(32'(((error < 32'sh0)))) != signed'(32'(((value < 32'sh0))))) begin
                product_negative_tmp = product_negative_tmp | ((24'(64'h1) <<< i));
            end
        end
        correction_exponent_tmp = exponent;
        correction_negative_tmp = product_negative;
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            correction_magnitudes_tmp[i] = 32'(((unsigned'(32'(correction_low[i])) + ((unsigned'(32'(correction_high[i])) <<< 'h4))))*32'h400);
        end
        adjustment_exponent_tmp = correction_exponent;
        adjustment_negative_tmp = correction_negative;
        for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
            adjustment_magnitudes_tmp[i] = 32'(unsigned'(32'(correction_magnitudes[i])) >>> unsigned'(32'(adjustment_exponent)));
        end
        if ((((((training & 64'h200))) != '0) && !((invalid) != '0)) && !((fault_reg) != '0)) begin
            for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
                delta=unsigned'(32'(adjustment_magnitudes[i]));
                if (((((adjustment_negative >>> i)) & 64'h1) != '0)) begin
                    delta=-delta;
                end
                candidate = signed'(32'(signed_value(unsigned'(32'(weights[i])), 32'h20000) + delta));
                next=signed_value(unsigned'(32'(candidate)), 32'h80000);
                weights_tmp[i] = signed'(32'((next < signed'(32'(-'h1FFFF))) ? (-'h1FFFF) : ((next > 32'sh1FFFF) ? ('h1FFFF) : (next))));
            end
        end
        if (((invalid) != '0) || ((fault_reg) != '0)) begin
            fault_reg_tmp = (('h1) != '0);
            accepted_tmp = 64'h0;
            training_tmp = 64'h0;
            output_reg_tmp = 64'h80;
            valid_reg_tmp = (('h0) != '0);
        end
        if (reset) begin
            for (logic[31:0] i = 32'h0;i < 32'h4;i=i+1) begin
                history_tmp[i] = 64'h0;
            end
            for (logic[31:0] i = 32'h0;i < 32'h14;i=i+1) begin
                local_history_tmp[i] = 64'h2;
            end
            for (logic[31:0] i = 32'h0;i < 32'h18;i=i+1) begin
                weights_tmp[i] = signed'(32'((i == 32'h0) ? ('h5555) : ((i == 32'h1) ? (-'h1C72) : ((i == 32'h2) ? ('h97B) : ((i == 32'h3) ? (-'h329) : ('h0))))));
            end
            accepted_tmp = 64'h0;
            training_tmp = 64'h0;
            output_reg_tmp = 64'h80;
            valid_reg_tmp = (('h0) != '0);
            fault_reg_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        weights_tmp = weights;
        history_tmp = history;
        features_tmp = features;
        local_history_tmp = local_history;
        local_features_tmp = local_features;
        product_low_tmp = product_low;
        product_high_tmp = product_high;
        products_tmp = products;
        sum4_tmp = sum4;
        sum8_tmp = sum8;
        total_tmp = total;
        squares_tmp = squares;
        correction_low_tmp = correction_low;
        correction_high_tmp = correction_high;
        product_negative_tmp = product_negative;
        correction_magnitudes_tmp = correction_magnitudes;
        adjustment_magnitudes_tmp = adjustment_magnitudes;
        correction_negative_tmp = correction_negative;
        adjustment_negative_tmp = adjustment_negative;
        energy4_tmp = energy4;
        energy8_tmp = energy8;
        energy_tmp = energy;
        energy_delay_tmp = energy_delay;
        targets_tmp = targets;
        accepted_tmp = accepted;
        training_tmp = training;
        error_reg_tmp = error_reg;
        exponent_tmp = exponent;
        correction_exponent_tmp = correction_exponent;
        adjustment_exponent_tmp = adjustment_exponent;
        output_reg_tmp = output_reg;
        valid_reg_tmp = valid_reg;
        fault_reg_tmp = fault_reg;

            _work(reset);

        weights <= weights_tmp;
        history <= history_tmp;
        features <= features_tmp;
        local_history <= local_history_tmp;
        local_features <= local_features_tmp;
        product_low <= product_low_tmp;
        product_high <= product_high_tmp;
        products <= products_tmp;
        sum4 <= sum4_tmp;
        sum8 <= sum8_tmp;
        total <= total_tmp;
        squares <= squares_tmp;
        correction_low <= correction_low_tmp;
        correction_high <= correction_high_tmp;
        product_negative <= product_negative_tmp;
        correction_magnitudes <= correction_magnitudes_tmp;
        adjustment_magnitudes <= adjustment_magnitudes_tmp;
        correction_negative <= correction_negative_tmp;
        adjustment_negative <= adjustment_negative_tmp;
        energy4 <= energy4_tmp;
        energy8 <= energy8_tmp;
        energy <= energy_tmp;
        energy_delay <= energy_delay_tmp;
        targets <= targets_tmp;
        accepted <= accepted_tmp;
        training <= training_tmp;
        error_reg <= error_reg_tmp;
        exponent <= exponent_tmp;
        correction_exponent <= correction_exponent_tmp;
        adjustment_exponent <= adjustment_exponent_tmp;
        output_reg <= output_reg_tmp;
        valid_reg <= valid_reg_tmp;
        fault_reg <= fault_reg_tmp;
    end

    assign sample_out = output_reg;

    assign valid_out = valid_reg;

    assign fault_out = fault_reg;


endmodule

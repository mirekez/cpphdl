`default_nettype none

import Predef_pkg::*;


module ReceiveSampleInterpolator (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire[16-1:0] tag_in
,   input wire valid_in
,   input wire[25-1:0] interval_in
,   input wire[25-1:0] period_in
,   input wire[25-1:0] initial_phase_in
,   input wire[16-1:0] origin_in
,   output wire[32-1:0] samples_out
,   output wire[8-1:0] early_out
,   output wire[8-1:0] late_out
,   output wire[16-1:0] tag_out
,   output wire[24-1:0] fraction_out
,   output wire valid_out
,   output wire fault_out
,   output wire clipped_out
);


    // regs and combs
    reg[32-1:0] history[7];
    reg[16-1:0] expected_tag;
    reg[16-1:0] center_tag;
    reg[16-1:0] tags[4];
    reg[16-1:0] output_tag;
    reg[24-1:0] center_fraction;
    reg[24-1:0] fractions[4];
    reg[24-1:0] output_fraction;
    reg[4-1:0] filled;
    reg[4-1:0] accepted;
    reg[8-1:0] fraction[3];
    reg[8-1:0] fraction_delay[3];
    reg[13-1:0] squared[3];
    reg[13-1:0] squared_delay[3];
    reg[13-1:0] cubed[3];
    reg[8-1:0] data[6][4];
    reg[8-1:0] data_delay[6][4];
    reg[24-1:0] products[6][4];
    reg[26-1:0] sums[6];
    reg[32-1:0] _output;
    reg[8-1:0] early;
    reg[8-1:0] late;
    reg valid;
    reg fault;
    reg clipped;

    // members

    // tmp variables
    logic[32-1:0] history_tmp[7];
    logic[16-1:0] expected_tag_tmp;
    logic[16-1:0] center_tag_tmp;
    logic[16-1:0] tags_tmp[4];
    logic[16-1:0] output_tag_tmp;
    logic[24-1:0] center_fraction_tmp;
    logic[24-1:0] fractions_tmp[4];
    logic[24-1:0] output_fraction_tmp;
    logic[4-1:0] filled_tmp;
    logic[4-1:0] accepted_tmp;
    logic[8-1:0] fraction_tmp[3];
    logic[8-1:0] fraction_delay_tmp[3];
    logic[13-1:0] squared_tmp[3];
    logic[13-1:0] squared_delay_tmp[3];
    logic[13-1:0] cubed_tmp[3];
    logic[8-1:0] data_tmp[6][4];
    logic[8-1:0] data_delay_tmp[6][4];
    logic[24-1:0] products_tmp[6][4];
    logic[26-1:0] sums_tmp[6];
    logic[32-1:0] _output_tmp;
    logic[8-1:0] early_tmp;
    logic[8-1:0] late_tmp;
    logic valid_tmp;
    logic fault_tmp;
    logic clipped_tmp;


    function logic signed[31:0] signed_value (
        input logic[31:0] value
,       input logic[31:0] sign
    );
        return signed'(32'((value ^ sign))) - signed'(32'(sign));
    endfunction

    task _work (input logic reset);
    begin: _work
        logic[8-1:0][31:0] words;
        logic[3-1:0][31:0] fraction_code;
        logic[31:0] value;
        logic[31:0] _packed;
        logic[31:0] bad;
        logic[31:0] request;
        logic[31:0] total;
        logic signed[3-1:0][31:0] location;
        logic signed[31:0] relative;
        logic signed[31:0] offset;
        logic signed[31:0] base;
        logic signed[12-1:0][31:0] coefficients;
        logic signed[31:0] sum;
        logic signed[31:0] rounded;
        bad=32'h0;
        request=32'h0;
        _packed=32'h0;
        total=32'h0;
        value=32'h0;
        relative='h0;
        offset='h0;
        base='h0;
        sum='h0;
        rounded='h0;
        for (logic[31:0] i = 32'h0;i < 32'h7;i=i+1) begin
            words[i]=history[32'h6 - i];
        end
        words['h7]=samples_in;
        for (logic[31:0] phase = 32'h0;phase < 32'h3;phase=phase+1) begin
            fraction_code[phase]=32'h0;
            location[phase]='h0;
            coefficients[(32'h4*phase) + 32'h0]=(-signed'(32'((unsigned'(32'(fraction_delay[phase])) <<< 'h4))) + ('h2*signed'(32'(unsigned'(32'(squared_delay[phase])))))) - signed'(32'(unsigned'(32'(cubed[phase]))));
            coefficients[(32'h4*phase) + 32'h1]=('h2000 - ('h5*signed'(32'(unsigned'(32'(squared_delay[phase])))))) + ('h3*signed'(32'(unsigned'(32'(cubed[phase])))));
            coefficients[(32'h4*phase) + 32'h2]=(signed'(32'((unsigned'(32'(fraction_delay[phase])) <<< 'h4))) + ('h4*signed'(32'(unsigned'(32'(squared_delay[phase])))))) - ('h3*signed'(32'(unsigned'(32'(cubed[phase])))));
            coefficients[(32'h4*phase) + 32'h3]=-signed'(32'(unsigned'(32'(squared_delay[phase])))) + signed'(32'(unsigned'(32'(cubed[phase]))));
            fraction_delay_tmp[phase] = fraction[phase];
            squared_delay_tmp[phase] = squared[phase];
            cubed_tmp[phase] = 32'((((64'(unsigned'(32'(squared[phase])))*64'(unsigned'(32'(fraction[phase])))) + 32'h80)) >>> 32'sh8);
        end
        for (logic[31:0] path = 32'h0;path < 32'h6;path=path+1) begin
            sum='h0;
            for (logic[31:0] tap = 32'h0;tap < 32'h4;tap=tap+1) begin
                data_delay_tmp[path][tap] = data[path][tap];
                products_tmp[path][tap] = signed'(32'(64'(signed'(32'(unsigned'(32'(data_delay[path][tap])))))*64'(coefficients[(32'(('h4*(((path == 32'h0)) ? ('h0) : (((path == 32'h5)) ? ('h2) : ('h1))))) + tap)])));
                sum+=signed_value(unsigned'(32'(products[path][tap])), 32'h800000);
            end
            sums_tmp[path] = sum;
        end
        for (logic[31:0] i = 32'h1;i < 32'h4;i=i+1) begin
            tags_tmp[i] = tags[i - 32'h1];
            fractions_tmp[i] = fractions[i - 32'h1];
        end
        valid_tmp = (('h0) != '0);
        clipped_tmp = (('h0) != '0);
        if (((((accepted & 64'h8))) != '0) && !((fault) != '0)) begin
            for (logic[31:0] path = 32'h0;path < 32'h6;path=path+1) begin
                sum=signed_value(unsigned'(32'(sums[path])), 32'h2000000);
                rounded=(sum < 32'sh0) ? (-signed'(32'(((unsigned'(32'(-sum)) + 32'h1000)) >>> 32'shD))) : (signed'(32'(((unsigned'(32'(sum)) + 32'h1000)) >>> 32'shD)));
                value=(rounded < 32'sh0) ? (32'h0) : ((rounded > 32'shFF) ? (32'hFF) : (unsigned'(32'(rounded))));
                if ((rounded < 32'sh0) || (rounded > 32'shFF)) begin
                    clipped_tmp = (('h1) != '0);
                end
                if (path == 32'h0) begin
                    early_tmp = value;
                end
                else begin
                    if (path == 32'h5) begin
                        late_tmp = value;
                    end
                    else begin
                        _packed|=value <<< ((32'h8*((path - 32'h1))));
                    end
                end
            end
            _output_tmp = _packed;
            output_tag_tmp = tags['h3];
            output_fraction_tmp = fractions['h3];
            valid_tmp = (('h1) != '0);
        end
        if (valid_in && !((fault) != '0)) begin
            bad=((((tag_in != expected_tag) || (period_in < 64'hFF7CEE)) || (period_in > 64'h1008312)) || (interval_in < 64'hF57CEE)) || (interval_in > 64'h10A8312);
            if (!((bad) != '0)) begin
                expected_tag_tmp = expected_tag + 64'h1;
                history_tmp['h0] = samples_in;
                for (logic[31:0] i = 32'h1;i < 32'h7;i=i+1) begin
                    history_tmp[i] = history[i - 32'h1];
                end
                if (filled < 64'h8) begin
                    filled_tmp = filled + 64'h1;
                end
                if (filled>=64'h7 && (16'(unsigned'(64'((tag_in - center_tag)))) == 64'h4)) begin
                    for (logic[31:0] phase = 32'h0;phase < 32'h3;phase=phase+1) begin
                        relative=signed'(32'(unsigned'(32'(center_fraction)))) + (((phase == 32'h0)) ? (-signed'(32'(unsigned'(32'(period_in))))) : (((phase == 32'h2)) ? (signed'(32'(unsigned'(32'(period_in))))) : ('h0)));
                        offset=(relative < 32'sh0) ? (-signed'(32'(((unsigned'(32'(-relative)) + 32'hFFFFFF)) >>> 32'sh18))) : ((relative >>> 32'sh18));
                        fraction_code[phase]=32'((relative - (offset*'h1000000))) >>> 32'sh10;
                        location[phase]='h2 + offset;
                        if ((signed'(32'(location[phase])) < 32'sh0) || (signed'(32'(location[phase])) > 32'sh4)) begin
                            bad=32'h1;
                        end
                    end
                    if (!((bad) != '0)) begin
                        request=32'h1;
                        for (logic[31:0] phase = 32'h0;phase < 32'h3;phase=phase+1) begin
                            fraction_tmp[phase] = fraction_code[phase];
                            squared_tmp[phase] = 32'((((fraction_code[phase]*fraction_code[phase]) + 32'h8)) >>> 32'sh4);
                        end
                        for (logic[31:0] path = 32'h0;path < 32'h6;path=path+1) begin
                            base=location[(path == 32'h0) ? ('h0) : ((path == 32'h5) ? ('h2) : ('h1))];
                            for (logic[31:0] tap = 32'h0;tap < 32'h4;tap=tap+1) begin
                                data_tmp[path][tap] = 32'(((words[(base + signed'(32'(tap)))] >>> ((32'h8*((((path == 32'h0) || (path == 32'h5))) ? (32'h0) : ((path - 32'h1))))))) & 32'hFF);
                            end
                        end
                        tags_tmp['h0] = center_tag;
                        fractions_tmp['h0] = center_fraction;
                        total=unsigned'(32'(center_fraction)) + (32'h2*unsigned'(32'(interval_in)));
                        center_fraction_tmp = total;
                        center_tag_tmp = center_tag + 64'(32'(((total >>> 32'sh18))));
                    end
                end
            end
            if (((bad) != '0)) begin
                fault_tmp = (('h1) != '0);
                valid_tmp = (('h0) != '0);
            end
        end
        accepted_tmp = ((accepted <<< 'h1)) | 64'(request);
        if (((bad) != '0) || ((fault) != '0)) begin
            accepted_tmp = 64'h0;
        end
        if (reset) begin
            expected_tag_tmp = origin_in;
            center_tag_tmp = (origin_in + 64'h4) + ((initial_phase_in >>> 32'sh18));
            center_fraction_tmp = initial_phase_in;
            filled_tmp = 64'h0;
            accepted_tmp = 64'h0;
            valid_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
            clipped_tmp = (('h0) != '0);
            _output_tmp = 64'h80808080;
            early_tmp = 64'h80;
            late_tmp = 64'h80;
            output_tag_tmp = 64'h0;
            output_fraction_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        history_tmp = history;
        expected_tag_tmp = expected_tag;
        center_tag_tmp = center_tag;
        tags_tmp = tags;
        output_tag_tmp = output_tag;
        center_fraction_tmp = center_fraction;
        fractions_tmp = fractions;
        output_fraction_tmp = output_fraction;
        filled_tmp = filled;
        accepted_tmp = accepted;
        fraction_tmp = fraction;
        fraction_delay_tmp = fraction_delay;
        squared_tmp = squared;
        squared_delay_tmp = squared_delay;
        cubed_tmp = cubed;
        data_tmp = data;
        data_delay_tmp = data_delay;
        products_tmp = products;
        sums_tmp = sums;
        _output_tmp = _output;
        early_tmp = early;
        late_tmp = late;
        valid_tmp = valid;
        fault_tmp = fault;
        clipped_tmp = clipped;

            _work(reset);

        history <= history_tmp;
        expected_tag <= expected_tag_tmp;
        center_tag <= center_tag_tmp;
        tags <= tags_tmp;
        output_tag <= output_tag_tmp;
        center_fraction <= center_fraction_tmp;
        fractions <= fractions_tmp;
        output_fraction <= output_fraction_tmp;
        filled <= filled_tmp;
        accepted <= accepted_tmp;
        fraction <= fraction_tmp;
        fraction_delay <= fraction_delay_tmp;
        squared <= squared_tmp;
        squared_delay <= squared_delay_tmp;
        cubed <= cubed_tmp;
        data <= data_tmp;
        data_delay <= data_delay_tmp;
        products <= products_tmp;
        sums <= sums_tmp;
        _output <= _output_tmp;
        early <= early_tmp;
        late <= late_tmp;
        valid <= valid_tmp;
        fault <= fault_tmp;
        clipped <= clipped_tmp;
    end

    assign samples_out = _output;

    assign early_out = early;

    assign late_out = late;

    assign tag_out = output_tag;

    assign fraction_out = output_fraction;

    assign valid_out = valid;

    assign fault_out = fault;

    assign clipped_out = clipped;


endmodule

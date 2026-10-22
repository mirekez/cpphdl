`default_nettype none

import Predef_pkg::*;


module ReceivePairAcquirer (
    input wire clk
,   input wire reset
,   input wire[32-1:0] samples_in
,   input wire enable_in
,   input wire restart_in
,   input wire signal_ok_in
,   input wire advance_in
,   input wire remote_master_in
,   output wire[32-1:0] samples_out
,   output wire valid_out
,   output wire locked_out
,   output wire ambiguous_out
,   output wire[2-1:0] state_out
,   output wire[8-1:0] mapping_out
,   output wire[20-1:0] delays_out
);


    // regs and combs
    reg[32-1:0] samples[17];
    reg[32-1:0] _output;
    reg[64-1:0] magnitude[4];
    reg[64-1:0] training_valid[4];
    reg[8-1:0] qualified[4];
    reg[8-1:0] consecutive;
    reg[6-1:0] collected;
    reg[2-1:0] stage;
    reg[2-1:0] pair_a;
    reg[2-1:0] test_physical;
    reg[2-1:0] test_logical;
    reg[5-1:0] test_delay;
    reg[5-1:0] delays[4];
    reg[2-1:0] mapping[4];
    reg phase;
    reg test_phase;
    reg test_invert;
    reg role;
    reg role_valid;
    reg valid;
    reg ambiguous;
    reg[2-1:0] _matches[2][3];
    reg[2-1:0] candidate_mapping[2][3];
    reg[5-1:0] candidate_delays[2][3];

    // members

    // tmp variables
    logic[32-1:0] samples_tmp[17];
    logic[32-1:0] _output_tmp;
    logic[64-1:0] magnitude_tmp[4];
    logic[64-1:0] training_valid_tmp[4];
    logic[8-1:0] qualified_tmp[4];
    logic[8-1:0] consecutive_tmp;
    logic[6-1:0] collected_tmp;
    logic[2-1:0] stage_tmp;
    logic[2-1:0] pair_a_tmp;
    logic[2-1:0] test_physical_tmp;
    logic[2-1:0] test_logical_tmp;
    logic[5-1:0] test_delay_tmp;
    logic[5-1:0] delays_tmp[4];
    logic[2-1:0] mapping_tmp[4];
    logic phase_tmp;
    logic test_phase_tmp;
    logic test_invert_tmp;
    logic role_tmp;
    logic role_valid_tmp;
    logic valid_tmp;
    logic ambiguous_tmp;
    logic[2-1:0] _matches_tmp[2][3];
    logic[2-1:0] candidate_mapping_tmp[2][3];
    logic[5-1:0] candidate_delays_tmp[2][3];


    function logic[31:0] side_sequence (input logic[31:0] value);
        return ((((value & 32'h1)) | ((((((((value >>> 32'sh3)) ^ ((value >>> 32'sh8)))) & 32'h1)) <<< 'h1))) | ((((((((value >>> 32'sh6)) ^ ((value >>> 32'sh10)))) & 32'h1)) <<< 'h2))) | ((((((((((value >>> 32'sh9)) ^ ((value >>> 32'shE))) ^ ((value >>> 32'sh13))) ^ ((value >>> 32'sh18)))) & 32'h1)) <<< 'h3));
    endfunction

    task next_candidate ();
    begin: next_candidate
        consecutive_tmp = 64'h0;
        if (test_delay != 64'h10) begin
            test_delay_tmp = test_delay + 64'h1;
        end
        else begin
            test_delay_tmp = 64'h0;
            if (test_physical != 64'h3) begin
                test_physical_tmp = test_physical + 64'h1;
            end
            else begin
                test_physical_tmp = 64'h0;
                if (test_logical != 64'h2) begin
                    test_logical_tmp = test_logical + 64'h1;
                end
                else begin
                    test_logical_tmp = 64'h0;
                    if (!((test_phase) != '0)) begin
                        test_phase_tmp = (('h1) != '0);
                    end
                    else begin
                        stage_tmp = 64'h2;
                    end
                end
            end
        end
    end
    endtask

    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic signed[31:0] centered;
        logic[33-1:0] history;
        logic[31:0] found;
        logic[31:0] selected;
        logic[31:0] active;
        logic[31:0] raw;
        logic[31:0] current;
        logic[31:0] previous;
        logic[31:0] expected;
        logic[31:0] observed;
        logic[31:0] hypotheses;
        logic[31:0] winner;
        logic[31:0] _packed;
        logic legal;
        logic consistent;
        logic polarity;
        logic _unique;
        logic anchor_ok;
        found=32'h0;
        selected=32'h0;
        anchor_ok=1;
        valid_tmp = (('h0) != '0);
        if (!((role_valid) != '0) && enable_in) begin
            role_tmp = remote_master_in;
            role_valid_tmp = (('h1) != '0);
        end
        if (advance_in) begin
            phase_tmp = !((phase) != '0);
            samples_tmp['h0] = samples_in;
            for (logic[31:0] i = 32'h1;i < 32'h11;i=i+1) begin
                samples_tmp[i] = samples[i - 32'h1];
            end
            if (collected != 64'h3F) begin
                collected_tmp = collected + 64'h1;
            end
            for (logic[31:0] p = 32'h0;p < 32'h4;p=p+1) begin
                centered=signed'(32'((((unsigned'(32'(samples_in)) >>> ((32'h8*p)))) & 32'hFF))) - 'h80;
                active=centered<=signed'(32'(-'h30)) || centered>=32'sh30;
                legal=(((centered>=signed'(32'(-'hC)) && centered<=32'shC)) || ((centered>=32'sh34 && centered<=32'sh4C))) || ((centered>=signed'(32'(-'h4C)) && centered<=signed'(32'(-'h34))));
                history = ((magnitude[p] <<< 'h1)) | 64'(active);
                consistent=(legal && (history != 64'h0)) && (active == 32'((((((magnitude[p] >>> 32'sh20)) ^ ((magnitude[p] >>> signed'(32'(((((role) != '0)) ? ('hC) : ('h13)))))))) & 64'h1)));
                magnitude_tmp[p] = ((magnitude[p] <<< 'h1)) | 64'(active);
                training_valid_tmp[p] = ((training_valid[p] <<< 'h1)) | 64'(32'(legal));
                if (stage == 64'h0) begin
                    if (collected>=64'h21 && consistent) begin
                        if (qualified[p] != 64'h80) begin
                            qualified_tmp[p] = qualified[p] + 64'h1;
                        end
                        if (qualified[p]>=64'h7F) begin
                            found=found+1;
                            selected=p;
                        end
                    end
                    else begin
                        qualified_tmp[p] = 64'h0;
                    end
                end
                else begin
                    if (((stage != 64'h3) && (64'(p) == pair_a)) && !consistent) begin
                        anchor_ok=0;
                    end
                end
            end
            if ((stage == 64'h0) && (found == 32'h1)) begin
                pair_a_tmp = selected;
                stage_tmp = 64'h1;
                test_phase_tmp = (('h0) != '0);
                test_logical_tmp = 64'h0;
                test_physical_tmp = 64'h0;
                test_delay_tmp = 64'h0;
                consecutive_tmp = 64'h0;
                for (logic[31:0] p = 32'h0;p < 32'h2;p=p+1) begin
                    for (logic[31:0] j = 32'h0;j < 32'h3;j=j+1) begin
                        _matches_tmp[p][j] = 64'h0;
                    end
                end
            end
            else begin
                if (stage == 64'h1) begin
                    current=side_sequence(unsigned'(32'(magnitude[unsigned'(32'(pair_a))] >>> 32'sh8)));
                    previous=side_sequence(unsigned'(32'(magnitude[unsigned'(32'(pair_a))] >>> 32'sh9)));
                    raw=((((signed'(32'(((phase) != '0))) ^ signed'(32'(((test_phase) != '0))))) != '0)) ? ((((((previous ^ 32'hE)) & 32'hE)) | ((current & 32'h1)))) : (current);
                    expected=((raw >>> ((unsigned'(32'(test_logical)) + 32'h1)))) & 32'h1;
                    observed=((magnitude[unsigned'(32'(test_physical))] >>> unsigned'(32'(test_delay)))) & 64'h1;
                    legal=((((training_valid[unsigned'(32'(test_physical))] >>> unsigned'(32'(test_delay)))) & 64'h1)) != 64'h0;
                    polarity=((expected ^ observed) != '0);
                    consistent=legal && (((test_logical == 64'h1)) ? ((((consecutive == 64'h0) || (signed'(32'(polarity)) == signed'(32'(((test_invert) != '0))))))) : (!polarity));
                    if ((test_physical == pair_a) || !consistent) begin
                        next_candidate();
                    end
                    else begin
                        if (consecutive == 64'h0) begin
                            test_invert_tmp = polarity;
                        end
                        if (consecutive == 64'h7F) begin
                            if (_matches[unsigned'(32'(test_phase))][unsigned'(32'(test_logical))] != 64'h2) begin
                                _matches_tmp[unsigned'(32'(test_phase))][unsigned'(32'(test_logical))] = _matches[unsigned'(32'(test_phase))][unsigned'(32'(test_logical))] + 64'h1;
                            end
                            candidate_mapping_tmp[unsigned'(32'(test_phase))][unsigned'(32'(test_logical))] = test_physical;
                            candidate_delays_tmp[unsigned'(32'(test_phase))][unsigned'(32'(test_logical))] = test_delay;
                            next_candidate();
                        end
                        else begin
                            consecutive_tmp = consecutive + 64'h1;
                        end
                    end
                end
                else begin
                    if (stage == 64'h2) begin
                        hypotheses=32'h0;
                        winner=32'h0;
                        for (logic[31:0] p = 32'h0;p < 32'h2;p=p+1) begin
                            _unique=(((((_matches[p]['h0] == 64'h1) && (_matches[p]['h1] == 64'h1)) && (_matches[p]['h2] == 64'h1)) && (candidate_mapping[p]['h0] != candidate_mapping[p]['h1])) && (candidate_mapping[p]['h0] != candidate_mapping[p]['h2])) && (candidate_mapping[p]['h1] != candidate_mapping[p]['h2]);
                            if (_unique) begin
                                hypotheses=hypotheses+1;
                                winner=p;
                            end
                        end
                        if (hypotheses == 32'h1) begin
                            mapping_tmp['h0] = pair_a;
                            delays_tmp['h0] = 64'h8;
                            for (logic[31:0] j = 32'h1;j < 32'h4;j=j+1) begin
                                mapping_tmp[j] = candidate_mapping[winner][j - 32'h1];
                                delays_tmp[j] = candidate_delays[winner][j - 32'h1];
                            end
                            stage_tmp = 64'h3;
                            ambiguous_tmp = (('h0) != '0);
                        end
                        else begin
                            stage_tmp = 64'h1;
                            ambiguous_tmp = hypotheses > 32'h1;
                            test_phase_tmp = (('h0) != '0);
                            test_logical_tmp = 64'h0;
                            test_physical_tmp = 64'h0;
                            test_delay_tmp = 64'h0;
                            consecutive_tmp = 64'h0;
                            for (logic[31:0] p = 32'h0;p < 32'h2;p=p+1) begin
                                for (logic[31:0] j = 32'h0;j < 32'h3;j=j+1) begin
                                    _matches_tmp[p][j] = 64'h0;
                                end
                            end
                        end
                    end
                    else begin
                        if (stage == 64'h3) begin
                            _packed=32'h0;
                            for (logic[31:0] j = 32'h0;j < 32'h4;j=j+1) begin
                                _packed|=((((unsigned'(32'(samples[unsigned'(32'(delays[j]))])) >>> ((32'h8*unsigned'(32'(mapping[j])))))) & 32'hFF)) <<< ((32'h8*j));
                            end
                            _output_tmp = _packed;
                            valid_tmp = (('h1) != '0);
                        end
                    end
                end
            end
            if (((stage != 64'h0) && (stage != 64'h3)) && !anchor_ok) begin
                stage_tmp = 64'h0;
                for (logic[31:0] p = 32'h0;p < 32'h4;p=p+1) begin
                    qualified_tmp[p] = 64'h0;
                end
            end
        end
        if (((reset || !enable_in) || restart_in) || !signal_ok_in) begin
            stage_tmp = 64'h0;
            collected_tmp = 64'h0;
            phase_tmp = (('h0) != '0);
            role_valid_tmp = (('h0) != '0);
            valid_tmp = (('h0) != '0);
            ambiguous_tmp = (('h0) != '0);
            _output_tmp = 64'h0;
            consecutive_tmp = 64'h0;
            for (logic[31:0] p = 32'h0;p < 32'h4;p=p+1) begin
                qualified_tmp[p] = 64'h0;
                magnitude_tmp[p] = 64'h0;
                training_valid_tmp[p] = 64'h0;
                mapping_tmp[p] = 64'h0;
                delays_tmp[p] = 64'h0;
            end
            for (logic[31:0] p = 32'h0;p < 32'h2;p=p+1) begin
                for (logic[31:0] j = 32'h0;j < 32'h3;j=j+1) begin
                    _matches_tmp[p][j] = 64'h0;
                    candidate_mapping_tmp[p][j] = 64'h0;
                    candidate_delays_tmp[p][j] = 64'h0;
                end
            end
            for (logic[31:0] i = 32'h0;i < 32'h11;i=i+1) begin
                samples_tmp[i] = 64'h0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        samples_tmp = samples;
        _output_tmp = _output;
        magnitude_tmp = magnitude;
        training_valid_tmp = training_valid;
        qualified_tmp = qualified;
        consecutive_tmp = consecutive;
        collected_tmp = collected;
        stage_tmp = stage;
        pair_a_tmp = pair_a;
        test_physical_tmp = test_physical;
        test_logical_tmp = test_logical;
        test_delay_tmp = test_delay;
        delays_tmp = delays;
        mapping_tmp = mapping;
        phase_tmp = phase;
        test_phase_tmp = test_phase;
        test_invert_tmp = test_invert;
        role_tmp = role;
        role_valid_tmp = role_valid;
        valid_tmp = valid;
        ambiguous_tmp = ambiguous;
        _matches_tmp = _matches;
        candidate_mapping_tmp = candidate_mapping;
        candidate_delays_tmp = candidate_delays;

            _work(reset);

        samples <= samples_tmp;
        _output <= _output_tmp;
        magnitude <= magnitude_tmp;
        training_valid <= training_valid_tmp;
        qualified <= qualified_tmp;
        consecutive <= consecutive_tmp;
        collected <= collected_tmp;
        stage <= stage_tmp;
        pair_a <= pair_a_tmp;
        test_physical <= test_physical_tmp;
        test_logical <= test_logical_tmp;
        test_delay <= test_delay_tmp;
        delays <= delays_tmp;
        mapping <= mapping_tmp;
        phase <= phase_tmp;
        test_phase <= test_phase_tmp;
        test_invert <= test_invert_tmp;
        role <= role_tmp;
        role_valid <= role_valid_tmp;
        valid <= valid_tmp;
        ambiguous <= ambiguous_tmp;
        _matches <= _matches_tmp;
        candidate_mapping <= candidate_mapping_tmp;
        candidate_delays <= candidate_delays_tmp;
    end

    assign samples_out = _output;

    assign valid_out = valid;

    assign locked_out = ((stage == 64'h3) != '0);

    assign ambiguous_out = ambiguous;

    assign state_out = stage;

    assign mapping_out = ((unsigned'(32'(mapping['h0])) | ((unsigned'(32'(mapping['h1])) <<< 'h2))) | ((unsigned'(32'(mapping['h2])) <<< 'h4))) | ((unsigned'(32'(mapping['h3])) <<< 'h6));

    assign delays_out = ((unsigned'(32'(delays['h0])) | ((unsigned'(32'(delays['h1])) <<< 'h5))) | ((unsigned'(32'(delays['h2])) <<< 'hA))) | ((unsigned'(32'(delays['h3])) <<< 'hF));


endmodule

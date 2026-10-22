`default_nettype none

import Predef_pkg::*;


module PCSTransmitBits (
    input wire clk
,   input wire reset
,   input wire[8-1:0] data_in
,   input wire enable_in
,   input wire error_in
,   input wire master_in
,   input wire link_in
,   input wire local_receiver_ok_in
,   input wire[2-1:0] mode_in
,   input wire seed_load_in
,   input wire[33-1:0] seed_in
,   output wire[6-1:0] point_out
,   output wire[3-1:0] subset_out
,   output wire[4-1:0] condition_out
,   output wire[4-1:0] negate_out
,   output wire fault_out
,   output wire[4-1:0] sx_out
,   output wire[4-1:0] sy_out
,   output wire[4-1:0] sg_out
,   output wire[9-1:0] encoded_out
,   output wire[3-1:0] trellis_out
);


    // regs and combs
    reg[33-1:0] scrambler;
    reg[4-1:0] enables;
    reg[4-1:0] errors;
    reg[4-1:0] previous_sy;
    reg[4-1:0] sx_reg;
    reg[4-1:0] sy_reg;
    reg[4-1:0] sg_reg;
    reg odd;
    reg armed;
    reg delayed_error;
    reg termination_bad;
    reg fault_reg;
    reg[3-1:0] trellis_reg;
    reg[3-1:0] subset_reg;
    reg[9-1:0] point_reg;
    reg[4-1:0] condition_reg;
    reg[4-1:0] negate_reg;

    // members

    // tmp variables
    logic[33-1:0] scrambler_tmp;
    logic[4-1:0] enables_tmp;
    logic[4-1:0] errors_tmp;
    logic[4-1:0] previous_sy_tmp;
    logic[4-1:0] sx_reg_tmp;
    logic[4-1:0] sy_reg_tmp;
    logic[4-1:0] sg_reg_tmp;
    logic odd_tmp;
    logic armed_tmp;
    logic delayed_error_tmp;
    logic termination_bad_tmp;
    logic fault_reg_tmp;
    logic[3-1:0] trellis_reg_tmp;
    logic[3-1:0] subset_reg_tmp;
    logic[9-1:0] point_reg_tmp;
    logic[4-1:0] condition_reg_tmp;
    logic[4-1:0] negate_reg_tmp;


    generate  // _assign
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[33-1:0] s;
        logic[31:0] sx;
        logic[31:0] sy;
        logic[31:0] sg;
        logic[31:0] sc;
        logic[31:0] sd;
        logic[31:0] cs;
        logic[31:0] subset;
        logic[31:0] condition;
        logic enabled;
        logic error;
        logic e1;
        logic e2;
        logic e3;
        logic e4;
        logic csreset;
        logic reversal;
        logic extension;
        logic extension_error;
        logic termination_error;
        s = scrambler;
        sy=s & 64'h1;
        sy=sy | ((((unsigned'(32'((((s >>> 32'sh3)) ^ ((s >>> 32'sh8))))) & 32'h1)) <<< 'h1));
        sy=sy | ((((unsigned'(32'((((s >>> 32'sh6)) ^ ((s >>> 32'sh10))))) & 32'h1)) <<< 'h2));
        sy=sy | ((((unsigned'(32'((((((s >>> 32'sh9)) ^ ((s >>> 32'shE))) ^ ((s >>> 32'sh13))) ^ ((s >>> 32'sh18))))) & 32'h1)) <<< 'h3));
        sx=unsigned'(32'((((s >>> 32'sh4)) ^ ((s >>> 32'sh6))))) & 32'h1;
        sx=sx | ((((unsigned'(32'((((((s >>> 32'sh7)) ^ ((s >>> 32'sh9))) ^ ((s >>> 32'shC))) ^ ((s >>> 32'shE))))) & 32'h1)) <<< 'h1));
        sx=sx | ((((unsigned'(32'((((((s >>> 32'shA)) ^ ((s >>> 32'shC))) ^ ((s >>> 32'sh14))) ^ ((s >>> 32'sh16))))) & 32'h1)) <<< 'h2));
        sx=sx | ((((unsigned'(32'((((((((((s >>> 32'shD)) ^ ((s >>> 32'shF))) ^ ((s >>> 32'sh12))) ^ ((s >>> 32'sh14))) ^ ((s >>> 32'sh17))) ^ ((s >>> 32'sh19))) ^ ((s >>> 32'sh1C))) ^ ((s >>> 32'sh1E))))) & 32'h1)) <<< 'h3));
        sg=unsigned'(32'((((s >>> 32'sh1)) ^ ((s >>> 32'sh5))))) & 32'h1;
        sg=sg | ((((unsigned'(32'((((((s >>> 32'sh4)) ^ ((s >>> 32'sh8))) ^ ((s >>> 32'sh9))) ^ ((s >>> 32'shD))))) & 32'h1)) <<< 'h1));
        sg=sg | ((((unsigned'(32'((((((s >>> 32'sh7)) ^ ((s >>> 32'shB))) ^ ((s >>> 32'sh11))) ^ ((s >>> 32'sh15))))) & 32'h1)) <<< 'h2));
        sg=sg | ((((unsigned'(32'((((((((((s >>> 32'shA)) ^ ((s >>> 32'shE))) ^ ((s >>> 32'shF))) ^ ((s >>> 32'sh13))) ^ ((s >>> 32'sh14))) ^ ((s >>> 32'sh18))) ^ ((s >>> 32'sh19))) ^ ((s >>> 32'sh1D))))) & 32'h1)) <<< 'h3));
        sx_reg_tmp = sx;
        sy_reg_tmp = sy;
        sg_reg_tmp = sg;
        previous_sy_tmp = sy;
        odd_tmp = !((odd) != '0);
        scrambler_tmp = ((s <<< 'h1)) | ((((((s >>> 32'sh20)) ^ ((s >>> signed'(32'(((master_in) ? ('hC) : ('h13)))))))) & 64'h1));
        enabled=((((armed) != '0) && link_in) && (mode_in == 64'h2)) && enable_in;
        error=((((armed) != '0) && link_in) && (mode_in == 64'h2)) && error_in;
        if (!link_in || (mode_in != 64'h2)) begin
            armed_tmp = (('h0) != '0);
        end
        else begin
            if (!enable_in && !error_in) begin
                armed_tmp = (('h1) != '0);
            end
        end
        e1=((enables & 64'h1)) != 64'h0;
        e2=((enables & 64'h2)) != 64'h0;
        e3=((enables & 64'h4)) != 64'h0;
        e4=((enables & 64'h8)) != 64'h0;
        enables_tmp = ((enables <<< 'h1)) | 64'(32'(enabled));
        errors_tmp = ((errors <<< 'h1)) | 64'(32'(error));
        csreset=e2 && !enabled;
        reversal=e2 || e4;
        sc=((e2) ? ((sx <<< 'h4)) : (32'h0)) | ((sy & 32'h1));
        if (mode_in == 64'h0) begin
            sc=sc & 32'hF0;
        end
        else begin
            sc=sc | ((((odd) != '0)) ? ((((unsigned'(32'(previous_sy)) ^ 32'hE)) & 32'hE)) : ((sy & 32'hE)));
        end
        sd=sc;
        if (e2) begin
            sd=sc ^ unsigned'(32'(data_in));
            if (csreset) begin
                sd=(((sd & 32'h3F)) | ((((unsigned'(32'(trellis_reg)) & 32'h1)) <<< 'h6))) | ((((unsigned'(32'(trellis_reg)) & 32'h2)) <<< 'h6));
            end
        end
        else begin
            sd=sd ^ unsigned'(32'(((local_receiver_ok_in) ? ('h4) : ('h0))));
            extension=(!enabled && error) && (data_in == 64'hF);
            extension_error=(!enabled && error) && (data_in != 64'hF);
            sd=(sd ^ 32'(extension)) ^ ((32'(extension_error) <<< 'h1));
        end
        cs=((unsigned'(32'(trellis_reg)) >>> 32'sh2)) & 32'h1;
        if (e2) begin
            cs=cs | ((((((((sd >>> 32'sh6)) ^ unsigned'(32'(trellis_reg)))) & 32'h1)) <<< 'h1));
            cs=cs | ((((((((sd >>> 32'sh7)) ^ ((unsigned'(32'(trellis_reg)) >>> 32'sh1)))) & 32'h1)) <<< 'h2));
        end
        sd=sd | ((((cs & 32'h1)) <<< 'h8));
        trellis_reg_tmp = cs;
        subset=(((((sd >>> 32'sh4)) & 32'h4)) | ((((sd >>> 32'sh6)) & 32'h2))) | ((((sd >>> 32'sh8)) & 32'h1));
        condition=32'hC;
        if (enabled && !e2) begin
            condition=(e1) ? ('h6) : ('h5);
            delayed_error_tmp = ((delayed_error) != '0) || error;
        end
        else begin
            if (enabled && e2) begin
                condition=((error || ((delayed_error) != '0))) ? ('h1) : ('h0);
                delayed_error_tmp = (('h0) != '0);
            end
            else begin
                if (csreset) begin
                    condition=(((termination_bad) != '0)) ? ('h2) : ((error) ? (((data_in == 64'hF) ? ('h3) : ('h2))) : ('h4));
                    if (error && (data_in != 64'hF)) begin
                        termination_bad_tmp = (('h1) != '0);
                    end
                end
                else begin
                    if (!e2 && e4) begin
                        termination_error=((termination_bad) != '0) || (((error && ((((errors & 64'h3)) == 64'h3))) && (data_in != 64'hF)));
                        if (e3) begin
                            condition=(termination_error) ? ('hB) : ('h7);
                        end
                        else begin
                            if (termination_error) begin
                                condition=32'hB;
                            end
                            else begin
                                if (error) begin
                                    condition=32'hA;
                                end
                                else begin
                                    condition=((((errors & 64'h1)) != '0)) ? ('h9) : ('h8);
                                end
                            end
                        end
                    end
                    else begin
                        termination_bad_tmp = (('h0) != '0);
                    end
                end
            end
        end
        if (mode_in == 64'h0) begin
            condition=32'hD;
        end
        if (((fault_reg) != '0) || (mode_in == 64'h3)) begin
            condition=32'hD;
        end
        point_reg_tmp = sd;
        subset_reg_tmp = subset;
        condition_reg_tmp = condition;
        negate_reg_tmp = 32'(sg ^ unsigned'(32'(((reversal) ? ('hF) : ('h0)))));
        fault_reg_tmp = ((fault_reg) != '0) || (mode_in == 64'h3);
        if (seed_load_in || reset) begin
            scrambler_tmp = (reset) ? (33'(64'h1)) : (seed_in);
            fault_reg_tmp = !reset && (seed_in == 64'h0);
            enables_tmp = 64'h0;
            errors_tmp = 64'h0;
            previous_sy_tmp = 64'h0;
            sx_reg_tmp = 64'h0;
            sy_reg_tmp = 64'h0;
            sg_reg_tmp = 64'h0;
            odd_tmp = (('h0) != '0);
            armed_tmp = (('h0) != '0);
            delayed_error_tmp = (('h0) != '0);
            termination_bad_tmp = (('h0) != '0);
            trellis_reg_tmp = 64'h0;
            subset_reg_tmp = 64'h0;
            point_reg_tmp = 64'h0;
            condition_reg_tmp = 64'hD;
            negate_reg_tmp = 64'h0;
        end
    end
    endtask

    always @(posedge clk) begin
        scrambler_tmp = scrambler;
        enables_tmp = enables;
        errors_tmp = errors;
        previous_sy_tmp = previous_sy;
        sx_reg_tmp = sx_reg;
        sy_reg_tmp = sy_reg;
        sg_reg_tmp = sg_reg;
        odd_tmp = odd;
        armed_tmp = armed;
        delayed_error_tmp = delayed_error;
        termination_bad_tmp = termination_bad;
        fault_reg_tmp = fault_reg;
        trellis_reg_tmp = trellis_reg;
        subset_reg_tmp = subset_reg;
        point_reg_tmp = point_reg;
        condition_reg_tmp = condition_reg;
        negate_reg_tmp = negate_reg;

            _work(reset);

        scrambler <= scrambler_tmp;
        enables <= enables_tmp;
        errors <= errors_tmp;
        previous_sy <= previous_sy_tmp;
        sx_reg <= sx_reg_tmp;
        sy_reg <= sy_reg_tmp;
        sg_reg <= sg_reg_tmp;
        odd <= odd_tmp;
        armed <= armed_tmp;
        delayed_error <= delayed_error_tmp;
        termination_bad <= termination_bad_tmp;
        fault_reg <= fault_reg_tmp;
        trellis_reg <= trellis_reg_tmp;
        subset_reg <= subset_reg_tmp;
        point_reg <= point_reg_tmp;
        condition_reg <= condition_reg_tmp;
        negate_reg <= negate_reg_tmp;
    end

    assign point_out = point_reg & 64'h3F;

    assign subset_out = subset_reg;

    assign condition_out = condition_reg;

    assign negate_out = negate_reg;

    assign fault_out = fault_reg;

    assign sx_out = sx_reg;

    assign sy_out = sy_reg;

    assign sg_out = sg_reg;

    assign encoded_out = point_reg;

    assign trellis_out = trellis_reg;


endmodule

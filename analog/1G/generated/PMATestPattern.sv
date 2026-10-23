`default_nettype none

import Predef_pkg::*;


module PMATestPattern (
    input wire clk
,   input wire reset
,   input wire enable_in
,   input wire[3-1:0] mode_in
,   input wire[11-1:0] seed_in
,   input wire restart_in
,   output wire[3-1:0] symbol_out
,   output wire valid_out
,   output wire slave_clock_out
,   output wire fault_out
);


    // regs and combs
    reg[11-1:0] state;
    reg[3-1:0] previous_mode;
    reg[3-1:0] symbol;
    reg valid;
    reg slave;
    reg fault;

    // members

    // tmp variables
    logic[11-1:0] state_tmp;
    logic[3-1:0] previous_mode_tmp;
    logic[3-1:0] symbol_tmp;
    logic valid_tmp;
    logic slave_tmp;
    logic fault_tmp;


    generate  // _assign
        assign symbol_out = symbol;
        assign valid_out = valid;
        assign slave_clock_out = slave;
        assign fault_out = fault;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] mode;
        logic[31:0] current;
        logic[31:0] code;
        logic[31:0] selector;
        logic[31:0] feedback;
        logic restart;
        logic failed;
        mode=mode_in;
        restart=restart_in || (mode != unsigned'(32'(previous_mode)));
        current=(restart) ? (((mode == 32'h4) ? (unsigned'(32'(seed_in))) : (32'h0))) : (unsigned'(32'(state)));
        failed=(restart) ? (0) : (((fault) != '0));
        code=32'h2;
        selector=32'h0;
        feedback=32'h0;
        previous_mode_tmp = mode;
        valid_tmp = (('h0) != '0);
        slave_tmp = (('h0) != '0);
        if ((mode>=32'h1 && mode<=32'h4) && !failed) begin
            if ((mode == 32'h4) && (current == 32'h0)) begin
                failed=1;
            end
            else begin
                if (mode == 32'h1) begin
                    if (current == 32'h0) begin
                        code=32'h4;
                    end
                    else begin
                        if (current == 32'h80) begin
                            code=32'h0;
                        end
                        else begin
                            if (current == 32'h100) begin
                                code=32'h3;
                            end
                            else begin
                                if (current == 32'h180) begin
                                    code=32'h1;
                                end
                                else begin
                                    if (current>=32'h200 && (current < 32'h400)) begin
                                        code=(((current & 32'h80)) == 32'h0) ? ('h4) : ('h0);
                                    end
                                end
                            end
                        end
                    end
                end
                else begin
                    if ((mode == 32'h2) || (mode == 32'h3)) begin
                        code=(((current & 32'h1)) == 32'h0) ? ('h4) : ('h0);
                    end
                    else begin
                        selector=(((current & 32'h1)) | (((((((current >>> 32'sh1)) ^ ((current >>> 32'sh4)))) & 32'h1)) <<< 'h1)) | (((((((current >>> 32'sh2)) ^ ((current >>> 32'sh4)))) & 32'h1)) <<< 'h2);
                        case (selector)
                        32'h0: begin
                            code=32'h2;
                        end
                        32'h4: begin
                            code=32'h2;
                        end
                        32'h1: begin
                            code=32'h3;
                        end
                        32'h5: begin
                            code=32'h3;
                        end
                        32'h2: begin
                            code=32'h4;
                        end
                        32'h3: begin
                            code=32'h1;
                        end
                        32'h7: begin
                            code=32'h1;
                        end
                        32'h6: begin
                            code=32'h0;
                        end
                        endcase
                        feedback=((((current >>> 32'shA)) ^ ((current >>> 32'sh8)))) & 32'h1;
                    end
                end
                state_tmp = unsigned'(32'((mode == 32'h4) ? (((current <<< 'h1)) | feedback) : (current + 32'h1)));
                valid_tmp = (('h1) != '0);
                slave_tmp = mode == 32'h3;
            end
        end
        else begin
            if (mode > 32'h4) begin
                failed=1;
            end
        end
        symbol_tmp = code;
        fault_tmp = failed;
        if ((reset || !enable_in) || (mode == 32'h0)) begin
            state_tmp = 64'h0;
            previous_mode_tmp = 64'h0;
            symbol_tmp = 64'h2;
            valid_tmp = (('h0) != '0);
            slave_tmp = (('h0) != '0);
            fault_tmp = (('h0) != '0);
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        previous_mode_tmp = previous_mode;
        symbol_tmp = symbol;
        valid_tmp = valid;
        slave_tmp = slave;
        fault_tmp = fault;

            _work(reset);

        state <= state_tmp;
        previous_mode <= previous_mode_tmp;
        symbol <= symbol_tmp;
        valid <= valid_tmp;
        slave <= slave_tmp;
        fault <= fault_tmp;
    end


endmodule

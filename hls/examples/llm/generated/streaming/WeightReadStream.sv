`default_nettype none

import Predef_pkg::*;


module WeightReadStream (
    input wire clk
,   input wire reset
,   output wire memory_out__valid_out
,   output wire[unsigned'(64'(32))-1:0] memory_out__addr_out
,   input wire memory_out__ready_in
,   input wire memory_out__valid_in
,   input wire[unsigned'(64'(512))-1:0] memory_out__data_in
,   input wire memory_out__error_in
,   output wire memory_out__ready_out
,   input wire command_in
,   input wire[31:0] base_in
,   input wire[31:0] words_in
,   input wire ready_in
,   output wire valid_out
,   output wire[63:0] data_out
,   output wire fault_out
,   output wire pending_out
);
    localparam  WINDOW = 'h8;
    localparam  INDEX_BITS = 'h3;


    // regs and combs
    reg[512-1:0] fifo[8];
    reg[3-1:0] head;
    reg[3-1:0] tail;
    reg[4-1:0] reserved;
    reg[4-1:0] buffered;
    reg[4-1:0] outstanding;
    reg[16-1:0] beats_left;
    reg[16-1:0] words_left;
    reg[32-1:0] address;
    reg[512-1:0] current;
    reg[3-1:0] lane;
    reg active;
    reg current_valid;
    reg fault;
    reg request_hold;

    // members

    // tmp variables
    logic[3-1:0] head_tmp;
    logic[3-1:0] tail_tmp;
    logic[4-1:0] reserved_tmp;
    logic[4-1:0] buffered_tmp;
    logic[4-1:0] outstanding_tmp;
    logic[16-1:0] beats_left_tmp;
    logic[16-1:0] words_left_tmp;
    logic[32-1:0] address_tmp;
    logic[512-1:0] current_tmp;
    logic[3-1:0] lane_tmp;
    logic active_tmp;
    logic current_valid_tmp;
    logic fault_tmp;
    logic request_hold_tmp;


    generate  // _assign
        assign memory_out__valid_out = ((((request_hold) != '0) || ((((((active) != '0) && !((fault) != '0)) && (beats_left != 64'h0)) && (reserved != unsigned'(64'(WINDOW)))))) != '0);
        assign memory_out__addr_out = unsigned'(32'(address));
        assign memory_out__ready_out = (((((active) != '0) && !((fault) != '0)) && (outstanding != 64'h0)) != '0);
        assign valid_out = (((((active) != '0) && ((current_valid) != '0)) && !((fault) != '0)) != '0);
        assign data_out = unsigned'(64'(current >> (unsigned'(32'(lane))*'h40)));
        assign fault_out = ((fault) != '0);
        assign pending_out = ((outstanding != 64'h0) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic request;
        logic response;
        logic consume;
        logic fetch;
        if (reset) begin
            head_tmp = '0;
            tail_tmp = '0;
            reserved_tmp = '0;
            buffered_tmp = '0;
            outstanding_tmp = '0;
            beats_left_tmp = '0;
            words_left_tmp = '0;
            address_tmp = '0;
            current_tmp = '0;
            lane_tmp = '0;
            active_tmp = '0;
            current_valid_tmp = '0;
            fault_tmp = '0;
            request_hold_tmp = '0;
        end
        else begin
            request=memory_out__valid_out && memory_out__ready_in;
            response=memory_out__valid_in && memory_out__ready_out;
            consume=valid_out && ready_in;
            fetch=((((active) != '0) && !((fault) != '0)) && (buffered != 64'h0)) && ((!((current_valid) != '0) || (((consume && (lane == 64'h7)) && (words_left != 64'h1)))));
            request_hold_tmp = memory_out__valid_out && !memory_out__ready_in;
            if ((command_in && !((active) != '0)) && !((fault) != '0)) begin
                if (((((((base_in & 'h3F))) != '0) || !((words_in) != '0)) || (words_in > 32'hFF00)) || (base_in > (('hFFFFFFFF) - ('hFF00*'h8)))) begin
                    fault_tmp = 1;
                end
                else begin
                    active_tmp = 1;
                    address_tmp = base_in;
                    beats_left_tmp = ((words_in + 'h7)) >>> 32'sh3;
                    words_left_tmp = words_in;
                end
            end
            if (request) begin
                address_tmp = 32'(unsigned'(32'(address)) + 'h40);
                beats_left_tmp = unsigned'(32'(beats_left)) - 'h1;
            end
            if (response) begin
                fifo[64'(unsigned'(32'(tail)))] <= memory_out__data_in;
                tail_tmp = 32'(unsigned'(32'(tail)) + 'h1);
                if (memory_out__error_in) begin
                    fault_tmp = 1;
                end
            end
            if (consume) begin
                words_left_tmp = unsigned'(32'(words_left)) - 'h1;
                lane_tmp = 32'(unsigned'(32'(lane)) + 'h1);
                if ((lane == 64'h7) || (words_left == 64'h1)) begin
                    current_valid_tmp = 0;
                end
                if (words_left == 64'h1) begin
                    active_tmp = 0;
                end
            end
            if (fetch) begin
                current_tmp = fifo[64'(unsigned'(32'(head)))];
                current_valid_tmp = 1;
                lane_tmp = 64'h0;
                head_tmp = 32'(unsigned'(32'(head)) + 'h1);
            end
            if (signed'(32'(request)) != signed'(32'(fetch))) begin
                reserved_tmp = 32'(unsigned'(32'(reserved)) + ((request) ? ('h1) : (32'(-'h1))));
            end
            if (signed'(32'(response)) != signed'(32'(fetch))) begin
                buffered_tmp = 32'(unsigned'(32'(buffered)) + ((response) ? ('h1) : (32'(-'h1))));
            end
            if (signed'(32'(request)) != signed'(32'(response))) begin
                outstanding_tmp = 32'(unsigned'(32'(outstanding)) + ((request) ? ('h1) : (32'(-'h1))));
            end
        end
    end
    endtask

    always @(posedge clk) begin
        head_tmp = head;
        tail_tmp = tail;
        reserved_tmp = reserved;
        buffered_tmp = buffered;
        outstanding_tmp = outstanding;
        beats_left_tmp = beats_left;
        words_left_tmp = words_left;
        address_tmp = address;
        current_tmp = current;
        lane_tmp = lane;
        active_tmp = active;
        current_valid_tmp = current_valid;
        fault_tmp = fault;
        request_hold_tmp = request_hold;

        _work(reset);

        head <= head_tmp;
        tail <= tail_tmp;
        reserved <= reserved_tmp;
        buffered <= buffered_tmp;
        outstanding <= outstanding_tmp;
        beats_left <= beats_left_tmp;
        words_left <= words_left_tmp;
        address <= address_tmp;
        current <= current_tmp;
        lane <= lane_tmp;
        active <= active_tmp;
        current_valid <= current_valid_tmp;
        fault <= fault_tmp;
        request_hold <= request_hold_tmp;
    end


endmodule

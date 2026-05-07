`default_nettype none

import Predef_pkg::*;
import L1CachePerf_pkg::*;


module L1Cache #(
    parameter TOTAL_CACHE_SIZE
,   parameter CACHE_LINE_SIZE
,   parameter WAYS
,   parameter ID
,   parameter ADDR_BITS
 )
 (
    input wire clk
,   input wire reset
,   input wire write_in
,   input wire[31:0] write_data_in
,   input wire[7:0] write_mask_in
,   input wire read_in
,   input wire[31:0] addr_in
,   output wire[31:0] read_data_out
,   output wire[31:0] read_addr_out
,   output wire read_valid_out
,   output wire busy_out
,   input wire stall_in
,   input wire flush_in
,   output wire mem_write_out
,   output wire[31:0] mem_write_data_out
,   output wire[7:0] mem_write_mask_out
,   output wire mem_read_out
,   output wire[31:0] mem_addr_out
,   input wire[31:0] mem_read_data_in
,   output L1CachePerf perf_out
,   input wire debugen_in
);
    parameter  LINE_WORDS = CACHE_LINE_SIZE/'h4;
    parameter  SETS = (TOTAL_CACHE_SIZE/CACHE_LINE_SIZE)/WAYS;
    parameter  SET_BITS = $clog2(SETS);
    parameter  WORD_BITS = $clog2(LINE_WORDS);
    parameter  LINE_BITS = $clog2(CACHE_LINE_SIZE);
    parameter  HALF_LINE_BITS = CACHE_LINE_SIZE*'h4;
    parameter  TAG_BITS = (ADDR_BITS - SET_BITS) - LINE_BITS;
    parameter  WAY_BITS = (WAYS<='h1) ? ('h1) : ($clog2(WAYS));
    parameter  ST_IDLE = 'h0;
    parameter  ST_LOOKUP = 'h1;
    parameter  ST_DONE = 'h2;
    parameter  ST_REFILL = 'h3;
    parameter  ST_INIT = 'h4;


    // regs and combs
    reg[3-1:0] state_reg;
    reg[32-1:0] req_addr_reg;
    reg req_read_reg;
    reg req_cacheable_reg;
    reg[WORD_BITS-1:0] refill_word_reg;
    reg[WAY_BITS-1:0] victim_reg;
    reg[SET_BITS-1:0] init_set_reg;
    reg[32-1:0] last_addr_reg;
    reg[32-1:0] last_data_reg;
    reg last_valid_reg;
    reg[HALF_LINE_BITS-1:0] refill_even_line_reg;
    reg[HALF_LINE_BITS-1:0] refill_odd_line_reg;
    logic[SET_BITS-1:0] req_set_comb;
;
    logic[TAG_BITS-1:0] req_tag_comb;
;
    logic[WORD_BITS-1:0] req_word_comb;
;
    logic[SET_BITS-1:0] input_set_comb;
;
    logic input_cacheable_comb;
;
    logic start_read_comb;
;
    logic issue_read_comb;
;
    logic[TAG_BITS + 'h1-1:0] refill_tag_comb;
;
    logic[HALF_LINE_BITS-1:0] refill_even_line_comb;
;
    logic[HALF_LINE_BITS-1:0] refill_odd_line_comb;
;
    logic[31:0] refill_data_comb;
;
    logic hit_comb;
;
    logic[31:0] cache_data_comb;
;
    logic[31:0] read_data_comb;
;
    logic[31:0] read_addr_comb;
;
    logic read_valid_comb;
;
    logic busy_comb;
;
    L1CachePerf perf_comb;
;
    logic mem_read_comb;
;
    logic[31:0] mem_addr_comb;
;

    // members
    genvar gi, gj, gk;
      wire[$clog2((SETS))-1:0] even_ram__addr_in[WAYS];
      wire[(HALF_LINE_BITS)-1:0] even_ram__data_in[WAYS];
      wire even_ram__wr_in[WAYS];
      wire even_ram__rd_in[WAYS];
      wire[(HALF_LINE_BITS)-1:0] even_ram__q_out[WAYS];
      wire signed[31:0] even_ram__id_in[WAYS];
    generate
    for (gi=0; gi < WAYS; gi = gi + 1) begin
        RAM1PORT #(
        HALF_LINE_BITS
,       SETS
    ) even_ram (
            .clk(clk)
,           .reset(reset)
,           .addr_in(even_ram__addr_in[gi])
,           .data_in(even_ram__data_in[gi])
,           .wr_in(even_ram__wr_in[gi])
,           .rd_in(even_ram__rd_in[gi])
,           .q_out(even_ram__q_out[gi])
,           .id_in(even_ram__id_in[gi])
        );
    end
    endgenerate
      wire[$clog2((SETS))-1:0] odd_ram__addr_in[WAYS];
      wire[(HALF_LINE_BITS)-1:0] odd_ram__data_in[WAYS];
      wire odd_ram__wr_in[WAYS];
      wire odd_ram__rd_in[WAYS];
      wire[(HALF_LINE_BITS)-1:0] odd_ram__q_out[WAYS];
      wire signed[31:0] odd_ram__id_in[WAYS];
    generate
    for (gi=0; gi < WAYS; gi = gi + 1) begin
        RAM1PORT #(
        HALF_LINE_BITS
,       SETS
    ) odd_ram (
            .clk(clk)
,           .reset(reset)
,           .addr_in(odd_ram__addr_in[gi])
,           .data_in(odd_ram__data_in[gi])
,           .wr_in(odd_ram__wr_in[gi])
,           .rd_in(odd_ram__rd_in[gi])
,           .q_out(odd_ram__q_out[gi])
,           .id_in(odd_ram__id_in[gi])
        );
    end
    endgenerate
      wire[$clog2((SETS))-1:0] tag_ram__addr_in[WAYS];
      wire[(TAG_BITS + 'h1)-1:0] tag_ram__data_in[WAYS];
      wire tag_ram__wr_in[WAYS];
      wire tag_ram__rd_in[WAYS];
      wire[(TAG_BITS + 'h1)-1:0] tag_ram__q_out[WAYS];
      wire signed[31:0] tag_ram__id_in[WAYS];
    generate
    for (gi=0; gi < WAYS; gi = gi + 1) begin
        RAM1PORT #(
        TAG_BITS + 'h1
,       SETS
    ) tag_ram (
            .clk(clk)
,           .reset(reset)
,           .addr_in(tag_ram__addr_in[gi])
,           .data_in(tag_ram__data_in[gi])
,           .wr_in(tag_ram__wr_in[gi])
,           .rd_in(tag_ram__rd_in[gi])
,           .q_out(tag_ram__q_out[gi])
,           .id_in(tag_ram__id_in[gi])
        );
    end
    endgenerate

    // tmp variables
    logic[3-1:0] state_reg_tmp;
    logic[32-1:0] req_addr_reg_tmp;
    logic req_read_reg_tmp;
    logic req_cacheable_reg_tmp;
    logic[WORD_BITS-1:0] refill_word_reg_tmp;
    logic[WAY_BITS-1:0] victim_reg_tmp;
    logic[SET_BITS-1:0] init_set_reg_tmp;
    logic[32-1:0] last_addr_reg_tmp;
    logic[32-1:0] last_data_reg_tmp;
    logic last_valid_reg_tmp;
    logic[HALF_LINE_BITS-1:0] refill_even_line_reg_tmp;
    logic[HALF_LINE_BITS-1:0] refill_odd_line_reg_tmp;


    always @(*) begin  // req_set_comb_func
        req_set_comb = unsigned'(4'((unsigned'(32'(req_addr_reg)) >>> LINE_BITS)));
    end

    always @(*) begin  // req_tag_comb_func
        req_tag_comb = unsigned'(4'((unsigned'(32'(req_addr_reg)) >>> ((LINE_BITS + SET_BITS)))));
    end

    always @(*) begin  // req_word_comb_func
        req_word_comb = unsigned'(3'((((unsigned'(32'(req_addr_reg)) >>> 'h2)) & ((LINE_WORDS - 'h1)))));
    end

    always @(*) begin  // input_set_comb_func
        input_set_comb = unsigned'(4'((addr_in >>> LINE_BITS)));
    end

    always @(*) begin  // input_cacheable_comb_func
        logic[31:0] word; word = ((addr_in >>> 'h2)) & ((LINE_WORDS - 'h1));
        input_cacheable_comb=!(addr_in & 'h1);
        if (((addr_in & 'h2)) && (word == (LINE_WORDS - 'h1))) begin
            input_cacheable_comb=0;
        end
    end

    always @(*) begin  // hit_comb_func
        logic[63:0] i;
        hit_comb=0;
        if (((state_reg == ST_LOOKUP) && req_read_reg) && req_cacheable_reg) begin
            for (i='h0;i < WAYS;i=i+1) begin
                if (tag_ram__q_out[i][TAG_BITS] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                    hit_comb=1;
                end
            end
        end
    end

    always @(*) begin  // start_read_comb_func
        start_read_comb=0;
        if (read_in && !stall_in) begin
            if (state_reg == ST_IDLE) begin
                start_read_comb=1;
            end
            if ((state_reg == ST_DONE) && (addr_in != unsigned'(32'(last_addr_reg)))) begin
                start_read_comb=1;
            end
            if ((((state_reg == ST_LOOKUP) && req_read_reg) && hit_comb) && (addr_in != unsigned'(32'(req_addr_reg)))) begin
                start_read_comb=1;
            end
        end
    end

    always @(*) begin  // issue_read_comb_func
        issue_read_comb=((flush_in && read_in)) || start_read_comb;
    end

    always @(*) begin  // refill_tag_comb_func
        refill_tag_comb = (((unsigned'(64'('h1)) <<< TAG_BITS)) | unsigned'(64'(req_tag_comb)));
    end

    always @(*) begin  // refill_even_line_comb_func
        logic[31:0] word; word = unsigned'(32'(refill_word_reg));
        refill_even_line_comb = refill_even_line_reg;
        refill_even_line_comb[word*'h10 +:16] = mem_read_data_in & 'hFFFF;
    end

    always @(*) begin  // refill_odd_line_comb_func
        logic[31:0] word; word = unsigned'(32'(refill_word_reg));
        refill_odd_line_comb = refill_odd_line_reg;
        refill_odd_line_comb[word*'h10 +:16] = mem_read_data_in >>> 'h10;
    end

    always @(*) begin  // refill_data_comb_func
        logic[31:0] word;
        logic[31:0] even_half;
        logic[31:0] odd_half;
        word=unsigned'(32'(req_word_comb));
        even_half=unsigned'(32'(refill_even_line_comb[word*'h10 +:16]));
        odd_half=unsigned'(32'(refill_odd_line_comb[word*'h10 +:16]));
        if (req_addr_reg & 'h2) begin
            even_half=unsigned'(32'(refill_even_line_comb[((word + 'h1))*'h10 +:16]));
            refill_data_comb=odd_half | ((even_half <<< 'h10));
        end
        else begin
            refill_data_comb=even_half | ((odd_half <<< 'h10));
        end
    end

    always @(*) begin  // cache_data_comb_func
        logic[63:0] i;
        logic[31:0] word;
        logic[31:0] even_half;
        logic[31:0] odd_half;
        cache_data_comb='h0;
        word=unsigned'(32'(req_word_comb));
        even_half='h0;
        odd_half='h0;
        for (i='h0;i < WAYS;i=i+1) begin
            if (tag_ram__q_out[i][TAG_BITS] && (tag_ram__q_out[i]['h0 +:(TAG_BITS - 'h1) - 'h0 + 1] == req_tag_comb)) begin
                even_half=unsigned'(32'(even_ram__q_out[i][word*'h10 +:16]));
                odd_half=unsigned'(32'(odd_ram__q_out[i][word*'h10 +:16]));
                if (req_addr_reg & 'h2) begin
                    even_half=unsigned'(32'(even_ram__q_out[i][((word + 'h1))*'h10 +:16]));
                    cache_data_comb=odd_half | ((even_half <<< 'h10));
                end
                else begin
                    cache_data_comb=even_half | ((odd_half <<< 'h10));
                end
            end
        end
    end

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        if (flush_in) begin
            req_addr_reg_tmp = addr_in;
            req_read_reg_tmp = read_in;
            req_cacheable_reg_tmp = input_cacheable_comb;
            last_valid_reg_tmp = 0;
            state_reg_tmp = (read_in) ? (ST_LOOKUP) : (ST_IDLE);
        end
        else begin
            if (state_reg == ST_INIT) begin
                req_read_reg_tmp = 0;
                last_valid_reg_tmp = 0;
                if (init_set_reg == (SETS - 'h1)) begin
                    state_reg_tmp = ST_IDLE;
                end
                else begin
                    init_set_reg_tmp = init_set_reg + 'h1;
                end
            end
            else begin
                if (state_reg == ST_IDLE) begin
                    last_valid_reg_tmp = 0;
                    if (read_in) begin
                        req_addr_reg_tmp = addr_in;
                        req_read_reg_tmp = 1;
                        req_cacheable_reg_tmp = input_cacheable_comb;
                        state_reg_tmp = ST_LOOKUP;
                    end
                end
                else begin
                    if ((state_reg == ST_LOOKUP) && req_read_reg) begin
                        if (hit_comb) begin
                            if (stall_in) begin
                                last_addr_reg_tmp = req_addr_reg;
                                last_data_reg_tmp = cache_data_comb;
                                last_valid_reg_tmp = 1;
                                state_reg_tmp = ST_DONE;
                            end
                            else begin
                                if (start_read_comb) begin
                                    req_addr_reg_tmp = addr_in;
                                    req_cacheable_reg_tmp = input_cacheable_comb;
                                    last_valid_reg_tmp = 0;
                                    state_reg_tmp = ST_LOOKUP;
                                end
                                else begin
                                    req_read_reg_tmp = 0;
                                    req_cacheable_reg_tmp = 0;
                                    last_valid_reg_tmp = 0;
                                    state_reg_tmp = ST_IDLE;
                                end
                            end
                        end
                        else begin
                            refill_word_reg_tmp = 'h0;
                            refill_even_line_reg_tmp = 'h0;
                            refill_odd_line_reg_tmp = 'h0;
                            state_reg_tmp = ST_REFILL;
                        end
                    end
                    else begin
                        if ((state_reg == ST_REFILL) && req_read_reg) begin
                            if (req_cacheable_reg) begin
                                refill_even_line_reg_tmp = refill_even_line_comb;
                                refill_odd_line_reg_tmp = refill_odd_line_comb;
                                if (refill_word_reg == (LINE_WORDS - 'h1)) begin
                                    last_addr_reg_tmp = req_addr_reg;
                                    last_data_reg_tmp = refill_data_comb;
                                    last_valid_reg_tmp = 1;
                                    victim_reg_tmp = victim_reg + 'h1;
                                    state_reg_tmp = ST_DONE;
                                end
                                else begin
                                    refill_word_reg_tmp = refill_word_reg + 'h1;
                                end
                            end
                            else begin
                                if (refill_word_reg == 'h0) begin
                                    refill_word_reg_tmp = 'h1;
                                end
                                else begin
                                    last_addr_reg_tmp = req_addr_reg;
                                    last_data_reg_tmp = mem_read_data_in;
                                    last_valid_reg_tmp = 1;
                                    state_reg_tmp = ST_DONE;
                                end
                            end
                        end
                        else begin
                            if ((state_reg == ST_DONE) && !stall_in) begin
                                last_valid_reg_tmp = 0;
                                if (start_read_comb) begin
                                    req_addr_reg_tmp = addr_in;
                                    req_read_reg_tmp = 1;
                                    req_cacheable_reg_tmp = input_cacheable_comb;
                                    state_reg_tmp = ST_LOOKUP;
                                end
                                else begin
                                    req_read_reg_tmp = 0;
                                    req_cacheable_reg_tmp = 0;
                                    state_reg_tmp = ST_IDLE;
                                end
                            end
                        end
                    end
                end
            end
        end
        if (write_in) begin
            last_valid_reg_tmp = 0;
        end
        for (i='h0;i < WAYS;i=i+1) begin
        end
        if (reset) begin
            state_reg_tmp = '0;
            req_addr_reg_tmp = '0;
            req_read_reg_tmp = '0;
            req_cacheable_reg_tmp = '0;
            refill_word_reg_tmp = '0;
            victim_reg_tmp = '0;
            init_set_reg_tmp = '0;
            last_addr_reg_tmp = '0;
            last_data_reg_tmp = '0;
            last_valid_reg_tmp = '0;
            refill_even_line_reg_tmp = '0;
            refill_odd_line_reg_tmp = '0;
            state_reg_tmp = ST_INIT;
        end
    end
    endtask

    generate  // _assign
        for (gi='h0;gi < WAYS;gi=gi+1) begin
            assign even_ram__addr_in[gi] = (((state_reg == ST_REFILL) || (((state_reg == ST_LOOKUP) && !issue_read_comb)))) ? (req_set_comb) : (input_set_comb);
            assign even_ram__data_in[gi] = refill_even_line_comb;
            assign even_ram__wr_in[gi] = (((((state_reg == ST_REFILL)) && req_read_reg) && req_cacheable_reg) && (refill_word_reg == (LINE_WORDS - 'h1))) && (victim_reg == gi);
            assign even_ram__rd_in[gi] = issue_read_comb && input_cacheable_comb;
            assign even_ram__id_in[gi]=(ID*'h64) + (gi*'h3);
            assign odd_ram__addr_in[gi] = (((state_reg == ST_REFILL) || (((state_reg == ST_LOOKUP) && !issue_read_comb)))) ? (req_set_comb) : (input_set_comb);
            assign odd_ram__data_in[gi] = refill_odd_line_comb;
            assign odd_ram__wr_in[gi] = (((((state_reg == ST_REFILL)) && req_read_reg) && req_cacheable_reg) && (refill_word_reg == (LINE_WORDS - 'h1))) && (victim_reg == gi);
            assign odd_ram__rd_in[gi] = issue_read_comb && input_cacheable_comb;
            assign odd_ram__id_in[gi]=((ID*'h64) + (gi*'h3)) + 'h1;
            assign tag_ram__addr_in[gi] = ((state_reg == ST_INIT)) ? (init_set_reg) : (((write_in) ? (input_set_comb) : (((((state_reg == ST_REFILL) || (((state_reg == ST_LOOKUP) && !issue_read_comb)))) ? (req_set_comb) : (input_set_comb)))));
            assign tag_ram__data_in[gi] = ((state_reg == ST_REFILL)) ? (refill_tag_comb) : ('h0);
            assign tag_ram__wr_in[gi] = (((state_reg == ST_INIT)) || (((((((state_reg == ST_REFILL)) && req_read_reg) && req_cacheable_reg) && (refill_word_reg == (LINE_WORDS - 'h1))) && (victim_reg == gi)))) || write_in;
            assign tag_ram__rd_in[gi] = issue_read_comb && input_cacheable_comb;
            assign tag_ram__id_in[gi]=((ID*'h64) + (gi*'h3)) + 'h2;
        end
    endgenerate

    always @(*) begin  // read_data_comb_func
        if (((state_reg == ST_LOOKUP) && req_read_reg) && hit_comb) begin
            read_data_comb=cache_data_comb;
        end
        else begin
            if (last_valid_reg) begin
                read_data_comb=last_data_reg;
            end
            else begin
                read_data_comb=mem_read_data_in;
            end
        end
    end

    always @(*) begin  // read_addr_comb_func
        read_addr_comb=(last_valid_reg) ? (unsigned'(32'(last_addr_reg))) : (unsigned'(32'(req_addr_reg)));
    end

    always @(*) begin  // read_valid_comb_func
        read_valid_comb=last_valid_reg || ((((state_reg == ST_LOOKUP) && req_read_reg) && hit_comb));
    end

    always @(*) begin  // busy_comb_func
        if ((state_reg == ST_INIT) || (state_reg == ST_REFILL)) begin
            busy_comb=1;
        end
        else begin
            if (((state_reg == ST_LOOKUP) && req_read_reg) && !hit_comb) begin
                busy_comb=1;
            end
            else begin
                busy_comb=0;
            end
        end
    end

    always @(*) begin  // mem_read_comb_func
        mem_read_comb=(state_reg == ST_REFILL) && req_read_reg;
    end

    always @(*) begin  // mem_addr_comb_func
        if (((state_reg == ST_REFILL) && req_read_reg) && req_cacheable_reg) begin
            mem_addr_comb=((unsigned'(32'(req_addr_reg)) & ~unsigned'(32'(((CACHE_LINE_SIZE - 'h1)))))) + ((unsigned'(32'(refill_word_reg))*'h4));
        end
        else begin
            if ((state_reg == ST_REFILL) && req_read_reg) begin
                mem_addr_comb=req_addr_reg;
            end
            else begin
                mem_addr_comb=addr_in;
            end
        end
    end

    always @(*) begin  // perf_comb_func
        perf_comb.state = state_reg;
        perf_comb.hit=hit_comb;
        perf_comb.lookup_wait=busy_comb && (state_reg == ST_LOOKUP);
        perf_comb.refill_wait=busy_comb && (state_reg == ST_REFILL);
        perf_comb.init_wait=busy_comb && (state_reg == ST_INIT);
        perf_comb.issue_wait=read_in && (state_reg == ST_IDLE);
    end

    always @(posedge clk) begin
        state_reg_tmp = state_reg;
        req_addr_reg_tmp = req_addr_reg;
        req_read_reg_tmp = req_read_reg;
        req_cacheable_reg_tmp = req_cacheable_reg;
        refill_word_reg_tmp = refill_word_reg;
        victim_reg_tmp = victim_reg;
        init_set_reg_tmp = init_set_reg;
        last_addr_reg_tmp = last_addr_reg;
        last_data_reg_tmp = last_data_reg;
        last_valid_reg_tmp = last_valid_reg;
        refill_even_line_reg_tmp = refill_even_line_reg;
        refill_odd_line_reg_tmp = refill_odd_line_reg;

        _work(reset);

        state_reg <= state_reg_tmp;
        req_addr_reg <= req_addr_reg_tmp;
        req_read_reg <= req_read_reg_tmp;
        req_cacheable_reg <= req_cacheable_reg_tmp;
        refill_word_reg <= refill_word_reg_tmp;
        victim_reg <= victim_reg_tmp;
        init_set_reg <= init_set_reg_tmp;
        last_addr_reg <= last_addr_reg_tmp;
        last_data_reg <= last_data_reg_tmp;
        last_valid_reg <= last_valid_reg_tmp;
        refill_even_line_reg <= refill_even_line_reg_tmp;
        refill_odd_line_reg <= refill_odd_line_reg_tmp;
    end

    assign read_data_out = read_data_comb;

    assign read_addr_out = read_addr_comb;

    assign read_valid_out = read_valid_comb;

    assign busy_out = busy_comb;

    assign mem_write_out = write_in;

    assign mem_write_data_out = write_data_in;

    assign mem_write_mask_out = write_mask_in;

    assign mem_read_out = mem_read_comb;

    assign mem_addr_out = mem_addr_comb;

    assign perf_out = perf_comb;


endmodule

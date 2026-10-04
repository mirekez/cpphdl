`default_nettype none

import Predef_pkg::*;
import HftFraming_pkg::*;
import HftWordMath_pkg::*;
import EthernetCrc_pkg::*;


module Hft (
    input wire clk
,   input wire reset
,   input wire[31:0] rx_data_in
,   input wire rx_valid_in
,   input wire rx_sof_in
,   input wire rx_eof_in
,   input wire[7:0] rx_bytes_in
,   output wire rx_ready_out
,   output wire[31:0] tx_data_out
,   output wire tx_valid_out
,   output wire tx_sof_out
,   output wire tx_eof_out
,   output wire[7:0] tx_bytes_out
,   input wire tx_ready_in
,   output wire[31:0] fault_out
,   output wire frame_size_error_out
,   output wire frame_crc_error_out
);
    localparam  ORDER_DEPTH = 'h4;


    // regs and combs
    reg[4-1:0][65-1:0] orders;
    reg[2-1:0] read_reg;
    reg[2-1:0] write_reg;
    reg[3-1:0] count_reg;
    reg[9-1:0] rx_position_reg;
    reg[32-1:0] ingress_word_reg;
    reg[32-1:0] ingress_position_reg;
    reg[32-1:0] ingress_flags_reg;
    reg[1-1:0] ingress_valid_reg;
    reg[7-1:0] tx_offset_reg;
    reg[32-1:0] tx_crc_reg;
    reg[32-1:0] tx_operation_reg;
    reg[32-1:0] tx_index_reg;
    reg[32-1:0] tx_value_reg;
    reg[1-1:0] tx_command_valid_reg;
    reg[2-1:0] tx_phase_reg;
    logic tx_command_ready_comb;
    logic[65-1:0] order_comb;
    logic[31:0] rx_flags_comb;
    logic[31:0] rx_position_comb;
    logic[31:0] tx_crc_comb;
    logic[31:0] tx_data_comb;

    // members
    wire receiver__command_valid_in;
    wire[31:0] receiver__operation_in;
    wire[31:0] receiver__index_in;
    wire[31:0] receiver__value_in;
    wire receiver__command_ready_out;
    wire receiver__response_ready_in;
    wire receiver__response_valid_out;
    wire[63:0] receiver__result_out;
    wire[31:0] receiver__fault_out;
    cpphdl_hls_ClockedPipelineHftWordMethods_P4      receiver (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(receiver__command_valid_in)
,       .operation_in(receiver__operation_in)
,       .index_in(receiver__index_in)
,       .value_in(receiver__value_in)
,       .command_ready_out(receiver__command_ready_out)
,       .response_ready_in(receiver__response_ready_in)
,       .response_valid_out(receiver__response_valid_out)
,       .result_out(receiver__result_out)
,       .fault_out(receiver__fault_out)
    );
    wire[63:0] collector__data_in;
    wire collector__valid_in;
    wire collector__ready_out;
    wire[31:0] collector__sequence_out;
    wire[31:0] collector__bid_out;
    wire[31:0] collector__ask_out;
    wire collector__valid_out;
    wire collector__ready_in;
    wire collector__size_error_out;
    wire collector__crc_error_out;
    HftCollector      collector (
        .clk(clk)
,       .reset(reset)
,       .data_in(collector__data_in)
,       .valid_in(collector__valid_in)
,       .ready_out(collector__ready_out)
,       .sequence_out(collector__sequence_out)
,       .bid_out(collector__bid_out)
,       .ask_out(collector__ask_out)
,       .valid_out(collector__valid_out)
,       .ready_in(collector__ready_in)
,       .size_error_out(collector__size_error_out)
,       .crc_error_out(collector__crc_error_out)
    );
    wire decision__command_valid_in;
    wire[31:0] decision__operation_in;
    wire[31:0] decision__index_in;
    wire[31:0] decision__value_in;
    wire decision__command_ready_out;
    wire decision__response_ready_in;
    wire decision__response_valid_out;
    wire[63:0] decision__result_out;
    wire[31:0] decision__fault_out;
    cpphdl_hls_ClockedPipelineHftDecisionMethods_P4      decision (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(decision__command_valid_in)
,       .operation_in(decision__operation_in)
,       .index_in(decision__index_in)
,       .value_in(decision__value_in)
,       .command_ready_out(decision__command_ready_out)
,       .response_ready_in(decision__response_ready_in)
,       .response_valid_out(decision__response_valid_out)
,       .result_out(decision__result_out)
,       .fault_out(decision__fault_out)
    );
    wire transmitter__command_valid_in;
    wire[31:0] transmitter__operation_in;
    wire[31:0] transmitter__index_in;
    wire[31:0] transmitter__value_in;
    wire transmitter__command_ready_out;
    wire transmitter__response_ready_in;
    wire transmitter__response_valid_out;
    wire[63:0] transmitter__result_out;
    wire[31:0] transmitter__fault_out;
    cpphdl_hls_ClockedPipelineHftTxMethods_P4      transmitter (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(transmitter__command_valid_in)
,       .operation_in(transmitter__operation_in)
,       .index_in(transmitter__index_in)
,       .value_in(transmitter__value_in)
,       .command_ready_out(transmitter__command_ready_out)
,       .response_ready_in(transmitter__response_ready_in)
,       .response_valid_out(transmitter__response_valid_out)
,       .result_out(transmitter__result_out)
,       .fault_out(transmitter__fault_out)
    );

    // tmp variables
    logic[4-1:0][65-1:0] orders_tmp;
    logic[2-1:0] read_reg_tmp;
    logic[2-1:0] write_reg_tmp;
    logic[3-1:0] count_reg_tmp;
    logic[9-1:0] rx_position_reg_tmp;
    logic[32-1:0] ingress_word_reg_tmp;
    logic[32-1:0] ingress_position_reg_tmp;
    logic[32-1:0] ingress_flags_reg_tmp;
    logic[1-1:0] ingress_valid_reg_tmp;
    logic[7-1:0] tx_offset_reg_tmp;
    logic[32-1:0] tx_crc_reg_tmp;
    logic[32-1:0] tx_operation_reg_tmp;
    logic[32-1:0] tx_index_reg_tmp;
    logic[32-1:0] tx_value_reg_tmp;
    logic[1-1:0] tx_command_valid_reg_tmp;
    logic[2-1:0] tx_phase_reg_tmp;


    always_comb begin : tx_command_ready_comb_func  // tx_command_ready_comb_func
        tx_command_ready_comb=!((tx_command_valid_reg) != '0) || transmitter__command_ready_out;
    end

    always_comb begin : order_comb_func  // order_comb_func
        order_comb = orders[64'h0];
        if (read_reg == 64'h1) begin
            order_comb = orders[64'h1];
        end
        if (read_reg == 64'h2) begin
            order_comb = orders[64'h2];
        end
        if (read_reg == 64'h3) begin
            order_comb = orders[64'h3];
        end
    end

    always_comb begin : rx_flags_comb_func  // rx_flags_comb_func
        rx_flags_comb=(unsigned'(32'(rx_sof_in)) | ((unsigned'(32'(rx_eof_in)) <<< 'h1))) | ((unsigned'(32'(rx_bytes_in)) <<< 'h2));
    end

    function logic[31:0] HftWordMath___add (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic[31:0] propagate;
        logic[31:0] carries;
        logic[31:0] shift;
        propagate=a ^ b;
        carries=a & b;
        for (shift=32'h1;shift < 32'h20;shift<<='h1) begin
            carries|=propagate & ((carries <<< shift));
            propagate&=propagate <<< shift;
        end
        return (a ^ b) ^ ((carries <<< 'h1));
    endfunction

    function logic HftWordMath___less (
        input logic[31:0] a
,       input logic[31:0] b
    );
        logic[31:0] propagate;
        logic[31:0] carries;
        logic[31:0] shift;
        propagate=a ^ ~b;
        carries=a & ~b;
        for (shift=32'h1;shift < 32'h20;shift<<='h1) begin
            carries|=propagate & ((carries <<< shift));
            propagate&=propagate <<< shift;
        end
        return ((((carries | propagate)) >>> 32'sh1F)) == 32'h0;
    endfunction

    function logic[31:0] HftFraming___step (
        input logic[31:0] state
,       input logic[31:0] flags
    );
        logic collecting;
        logic[31:0] count;
        logic[31:0] used;
        logic[31:0] result;
        logic[31:0] total;
        count=flags >>> 32'sh2;
        used=((((flags & 32'h1)) != '0)) ? (32'h0) : (state & 32'hFF);
        collecting=((((flags & 32'h1))) != '0) || ((((state & 32'h100))) != '0);
        result=used;
        total=HftWordMath___add(used, count);
        if (((collecting && (count != 32'h0)) && !HftWordMath___less(32'h4, count)) && ((((((flags & 32'h2))) != '0) || (count == 32'h4)))) begin
            if (HftWordMath___less(HftFraming_pkg::MAX_BYTES, total)) begin
                result|=32'h400;
            end
            else begin
                result=total | 32'h200;
                if (((flags & 32'h2) != '0)) begin
                    if (HftWordMath___less(total, HftFraming_pkg::MIN_BYTES)) begin
                        result|=32'h400;
                    end
                    else begin
                        result|=32'h800;
                    end
                end
                else begin
                    result|=32'h100;
                end
            end
        end
        return result;
    endfunction

    always_comb begin : rx_position_comb_func  // rx_position_comb_func
        logic[31:0] offset;
        offset = (rx_sof_in) ? ('h0) : (unsigned'(32'(rx_position_reg)) & 'hFF);
        rx_position_comb=HftFraming___step(unsigned'(32'(rx_position_reg)), rx_flags_comb) | ((offset <<< 'h10));
    end

    function logic[31:0] EthernetCrc___byte (
        input logic[31:0] crc
,       input logic[31:0] value
    );
        logic[31:0] x;
        x = crc ^ value;
        return (((crc >>> 32'sh8)) ^ ((((((((((x & 'h1))) != '0)) ? ('h77073096) : ('h0)) ^ ((((((x & 'h2))) != '0)) ? ('hEE0E612C) : ('h0)))) ^ ((((((((x & 'h4))) != '0)) ? ('h76DC419) : ('h0)) ^ ((((((x & 'h8))) != '0)) ? ('hEDB8832) : ('h0))))))) ^ ((((((((((x & 'h10))) != '0)) ? ('h1DB71064) : ('h0)) ^ ((((((x & 'h20))) != '0)) ? ('h3B6E20C8) : ('h0)))) ^ ((((((((x & 'h40))) != '0)) ? ('h76DC4190) : ('h0)) ^ ((((((x & 'h80))) != '0)) ? ('hEDB88320) : ('h0))))));
    endfunction

    function logic[31:0] EthernetCrc___fullWord (
        input logic[31:0] crc
,       input logic[31:0] value
    );
        logic[31:0] x;
        logic[31:0] g0;
        logic[31:0] g1;
        logic[31:0] g2;
        logic[31:0] g3;
        logic[31:0] g4;
        logic[31:0] g5;
        logic[31:0] g6;
        logic[31:0] g7;
        x=crc ^ value;
        g0=((((((((x & 'h1))) != '0)) ? ('hB8BC6765) : ('h0)) ^ ((((((x & 'h2))) != '0)) ? ('hAA09C88B) : ('h0)))) ^ ((((((((x & 'h4))) != '0)) ? ('h8F629757) : ('h0)) ^ ((((((x & 'h8))) != '0)) ? ('hC5B428EF) : ('h0))));
        g1=((((((((x & 'h10))) != '0)) ? ('h5019579F) : ('h0)) ^ ((((((x & 'h20))) != '0)) ? ('hA032AF3E) : ('h0)))) ^ ((((((((x & 'h40))) != '0)) ? ('h9B14583D) : ('h0)) ^ ((((((x & 'h80))) != '0)) ? ('hED59B63B) : ('h0))));
        g2=((((((((x & 'h100))) != '0)) ? ('h1C26A37) : ('h0)) ^ ((((((x & 'h200))) != '0)) ? ('h384D46E) : ('h0)))) ^ ((((((((x & 'h400))) != '0)) ? ('h709A8DC) : ('h0)) ^ ((((((x & 'h800))) != '0)) ? ('hE1351B8) : ('h0))));
        g3=((((((((x & 'h1000))) != '0)) ? ('h1C26A370) : ('h0)) ^ ((((((x & 'h2000))) != '0)) ? ('h384D46E0) : ('h0)))) ^ ((((((((x & 'h4000))) != '0)) ? ('h709A8DC0) : ('h0)) ^ ((((((x & 'h8000))) != '0)) ? ('hE1351B80) : ('h0))));
        g4=((((((((x & 'h10000))) != '0)) ? ('h191B3141) : ('h0)) ^ ((((((x & 'h20000))) != '0)) ? ('h32366282) : ('h0)))) ^ ((((((((x & 'h40000))) != '0)) ? ('h646CC504) : ('h0)) ^ ((((((x & 'h80000))) != '0)) ? ('hC8D98A08) : ('h0))));
        g5=((((((((x & 'h100000))) != '0)) ? ('h4AC21251) : ('h0)) ^ ((((((x & 'h200000))) != '0)) ? ('h958424A2) : ('h0)))) ^ ((((((((x & 'h400000))) != '0)) ? ('hF0794F05) : ('h0)) ^ ((((((x & 'h800000))) != '0)) ? ('h3B83984B) : ('h0))));
        g6=((((((((x & 'h1000000))) != '0)) ? ('h77073096) : ('h0)) ^ ((((((x & 'h2000000))) != '0)) ? ('hEE0E612C) : ('h0)))) ^ ((((((((x & 'h4000000))) != '0)) ? ('h76DC419) : ('h0)) ^ ((((((x & 'h8000000))) != '0)) ? ('hEDB8832) : ('h0))));
        g7=((((((((x & 'h10000000))) != '0)) ? ('h1DB71064) : ('h0)) ^ ((((((x & 'h20000000))) != '0)) ? ('h3B6E20C8) : ('h0)))) ^ ((((((((x & 'h40000000))) != '0)) ? ('h76DC4190) : ('h0)) ^ ((((((x & 'h80000000))) != '0)) ? ('hEDB88320) : ('h0))));
        return ((((g0 ^ g1)) ^ ((g2 ^ g3)))) ^ ((((g4 ^ g5)) ^ ((g6 ^ g7))));
    endfunction

    function logic[31:0] EthernetCrc___word (
        input logic[31:0] crc
,       input logic[31:0] value
,       input logic[31:0] bytes
    );
        logic[31:0] c1;
        logic[31:0] c2;
        logic[31:0] c3;
        logic[31:0] c4;
        c1=EthernetCrc___byte(crc, value);
        c2=EthernetCrc___byte(c1, value >>> 32'sh8);
        c3=EthernetCrc___byte(c2, value >>> 32'sh10);
        c4=EthernetCrc___fullWord(crc, value);
        if (bytes == 32'h0) begin
            return crc;
        end
        if (bytes == 32'h1) begin
            return c1;
        end
        if (bytes == 32'h2) begin
            return c2;
        end
        if (bytes == 32'h3) begin
            return c3;
        end
        return c4;
    endfunction

    always_comb begin : tx_crc_comb_func  // tx_crc_comb_func
        tx_crc_comb=EthernetCrc___word((tx_sof_out) ? ('hFFFFFFFF) : (unsigned'(32'(tx_crc_reg))), unsigned'(32'(transmitter__result_out)), 32'(((transmitter__result_out >>> 32'sh25)) & 64'h7));
    end

    always_comb begin : tx_data_comb_func  // tx_data_comb_func
        logic[31:0] bytes;
        bytes = 32'(((transmitter__result_out >>> 32'sh25)) & 64'h7);
        if (bytes == 32'h2) begin
            tx_data_comb=((unsigned'(32'(transmitter__result_out)) & 'hFFFF)) | (((~tx_crc_comb) <<< 'h10));
        end
        else begin
            if (bytes == 32'h0) begin
                tx_data_comb=(~unsigned'(32'(tx_crc_reg))) >>> 32'sh10;
            end
            else begin
                tx_data_comb=transmitter__result_out;
            end
        end
    end

    generate  // _assign
        assign receiver__command_valid_in = ((ingress_valid_reg) != '0);
        assign receiver__operation_in = unsigned'(32'(ingress_position_reg));
        assign receiver__index_in = unsigned'(32'(ingress_flags_reg));
        assign receiver__value_in = unsigned'(32'(ingress_word_reg));
        assign receiver__response_ready_in = ((collector__ready_out) != '0);
        assign rx_ready_out = ((!((count_reg[64'h2]) != '0) && ((!((ingress_valid_reg) != '0) || receiver__command_ready_out))) != '0);
        assign collector__data_in = receiver__result_out;
        assign collector__valid_in = ((receiver__response_valid_out) != '0);
        assign collector__ready_in = ((decision__command_ready_out) != '0);
        assign decision__command_valid_in = ((collector__valid_out) != '0);
        assign decision__operation_in = collector__sequence_out;
        assign decision__index_in = collector__bid_out;
        assign decision__value_in = collector__ask_out;
        assign decision__response_ready_in = ((!((count_reg[64'h2]) != '0) || (decision__result_out == 64'h0)) != '0);
        assign transmitter__command_valid_in = ((tx_command_valid_reg) != '0);
        assign transmitter__operation_in = unsigned'(32'(tx_operation_reg));
        assign transmitter__index_in = unsigned'(32'(tx_index_reg));
        assign transmitter__value_in = unsigned'(32'(tx_value_reg));
        assign transmitter__response_ready_in = ((!((tx_phase_reg[64'h1]) != '0) || tx_ready_in) != '0);
        assign tx_data_out = tx_data_comb;
        assign tx_valid_out = ((((tx_phase_reg[64'h1]) != '0) && transmitter__response_valid_out) != '0);
        assign tx_sof_out = ((((transmitter__result_out >>> 32'sh20)) & 64'h1) != '0);
        assign tx_eof_out = ((((transmitter__result_out >>> 32'sh21)) & 64'h1) != '0);
        assign tx_bytes_out = 8'(((transmitter__result_out >>> 32'sh22)) & 64'h7);
        assign fault_out = (receiver__fault_out | decision__fault_out) | transmitter__fault_out;
        assign frame_size_error_out = ((collector__size_error_out) != '0);
        assign frame_crc_error_out = ((collector__crc_error_out) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[31:0] i;
        logic push;
        logic pop;
        push=(decision__response_valid_out && decision__response_ready_in) && (decision__result_out != 64'h0);
        pop=((tx_phase_reg == 64'h0) && (count_reg != 64'h0)) && tx_command_ready_comb;
        if (reset) begin
            read_reg_tmp = '0;
            write_reg_tmp = '0;
            count_reg_tmp = '0;
            rx_position_reg_tmp = '0;
            tx_phase_reg_tmp = '0;
            ingress_word_reg_tmp = '0;
            ingress_position_reg_tmp = '0;
            ingress_flags_reg_tmp = '0;
            ingress_valid_reg_tmp = '0;
            tx_offset_reg_tmp = '0;
            tx_crc_reg_tmp = '0;
            tx_operation_reg_tmp = '0;
            tx_index_reg_tmp = '0;
            tx_value_reg_tmp = '0;
            tx_command_valid_reg_tmp = '0;
            orders_tmp = '0;
        end
        else begin
            if (tx_command_ready_comb) begin
                tx_command_valid_reg_tmp = (((tx_phase_reg == 64'h0) && (count_reg != 64'h0))) || (tx_phase_reg == 64'h2);
                tx_operation_reg_tmp = unsigned'(32'((tx_phase_reg == 64'h0) ? (((((order_comb[64'h40]) != '0)) ? ('h1) : ('h2))) : ('h0)));
                tx_index_reg_tmp = unsigned'(32'((tx_phase_reg == 64'h0) ? (unsigned'(32'(unsigned'(64'(order_comb)) >>> 32'sh20))) : (unsigned'(32'(tx_offset_reg)))));
                tx_value_reg_tmp = unsigned'(64'(order_comb));
            end
            if (!((ingress_valid_reg) != '0) || receiver__command_ready_out) begin
                ingress_valid_reg_tmp = rx_valid_in && !((count_reg[64'h2]) != '0);
            end
            if (rx_valid_in && rx_ready_out) begin
                rx_position_reg_tmp = 32'(rx_position_comb & 'h1FF);
                ingress_word_reg_tmp = rx_data_in;
                ingress_position_reg_tmp = rx_position_comb;
                ingress_flags_reg_tmp = rx_flags_comb;
            end
            if (tx_valid_out && tx_ready_in) begin
                tx_crc_reg_tmp = tx_crc_comb;
            end
            if (push) begin
                for (i=32'h0;i < unsigned'(32'(ORDER_DEPTH));i=i+1) begin
                    if (write_reg == 64'(i)) begin
                        orders_tmp[64'(i)] = {1'(unsigned'(32'(decision__result_out)) < 'h186A0), unsigned'(64'(decision__result_out))};
                    end
                end
                write_reg_tmp = 32'(unsigned'(32'(write_reg)) + 'h1);
            end
            if (pop) begin
                read_reg_tmp = 32'(unsigned'(32'(read_reg)) + 'h1);
                tx_phase_reg_tmp = 64'h1;
                tx_offset_reg_tmp = 64'h0;
            end
            if (signed'(32'(push)) != signed'(32'(pop))) begin
                count_reg_tmp = HftWordMath___add(unsigned'(32'(count_reg)), (push) ? ('h1) : (32'(-'h1)));
            end
            if ((tx_phase_reg == 64'h1) && transmitter__response_valid_out) begin
                tx_phase_reg_tmp = 64'h2;
            end
            if ((tx_phase_reg == 64'h2) && tx_command_ready_comb) begin
                tx_offset_reg_tmp = 32'(unsigned'(32'(tx_offset_reg)) + 'h4);
                if (tx_offset_reg == 64'h6C) begin
                    tx_phase_reg_tmp = 64'h3;
                end
            end
            if (((((tx_phase_reg[64'h1]) != '0) && tx_valid_out) && tx_ready_in) && tx_eof_out) begin
                tx_phase_reg_tmp = 64'h0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        orders_tmp = orders;
        read_reg_tmp = read_reg;
        write_reg_tmp = write_reg;
        count_reg_tmp = count_reg;
        rx_position_reg_tmp = rx_position_reg;
        ingress_word_reg_tmp = ingress_word_reg;
        ingress_position_reg_tmp = ingress_position_reg;
        ingress_flags_reg_tmp = ingress_flags_reg;
        ingress_valid_reg_tmp = ingress_valid_reg;
        tx_offset_reg_tmp = tx_offset_reg;
        tx_crc_reg_tmp = tx_crc_reg;
        tx_operation_reg_tmp = tx_operation_reg;
        tx_index_reg_tmp = tx_index_reg;
        tx_value_reg_tmp = tx_value_reg;
        tx_command_valid_reg_tmp = tx_command_valid_reg;
        tx_phase_reg_tmp = tx_phase_reg;

        _work(reset);

        orders <= orders_tmp;
        read_reg <= read_reg_tmp;
        write_reg <= write_reg_tmp;
        count_reg <= count_reg_tmp;
        rx_position_reg <= rx_position_reg_tmp;
        ingress_word_reg <= ingress_word_reg_tmp;
        ingress_position_reg <= ingress_position_reg_tmp;
        ingress_flags_reg <= ingress_flags_reg_tmp;
        ingress_valid_reg <= ingress_valid_reg_tmp;
        tx_offset_reg <= tx_offset_reg_tmp;
        tx_crc_reg <= tx_crc_reg_tmp;
        tx_operation_reg <= tx_operation_reg_tmp;
        tx_index_reg <= tx_index_reg_tmp;
        tx_value_reg <= tx_value_reg_tmp;
        tx_command_valid_reg <= tx_command_valid_reg_tmp;
        tx_phase_reg <= tx_phase_reg_tmp;
    end


endmodule

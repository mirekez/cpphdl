`default_nettype none

import Predef_pkg::*;
import HftWordParts_pkg::*;
import HftQuote_pkg::*;
import HftCollection_pkg::*;
import HftFrameCheck_pkg::*;
import HftFoldedFrame_pkg::*;
import HftCandidate_pkg::*;
import HftWordMath_pkg::*;
import EthernetCrc_pkg::*;


module HftCollector (
    input wire clk
,   input wire reset
,   input wire[63:0] data_in
,   input wire valid_in
,   output wire ready_out
,   output wire[31:0] sequence_out
,   output wire[31:0] bid_out
,   output wire[31:0] ask_out
,   output wire valid_out
,   input wire ready_in
,   output wire size_error_out
,   output wire crc_error_out
);


    // regs and combs
    reg[5-1:0] valid_reg;
    HftWordParts parts_reg;
    HftCollection collection_reg;
    HftFrameCheck check_reg;
    HftFoldedFrame folded_reg;
    HftCandidate candidate_reg;
    HftCandidate offer_reg;
    reg[32-1:0] sequence_reg;
    HftCollection collection_comb;
    HftFrameCheck check_comb;
    HftCandidate offer_comb;

    // members

    // tmp variables
    logic[5-1:0] valid_reg_tmp;
    HftWordParts parts_reg_tmp;
    HftCollection collection_reg_tmp;
    HftFrameCheck check_reg_tmp;
    HftFoldedFrame folded_reg_tmp;
    HftCandidate candidate_reg_tmp;
    HftCandidate offer_reg_tmp;
    logic[32-1:0] sequence_reg_tmp;


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

    function logic[31:0] fold (input logic[31:0] sum);
        sum=HftWordMath___add(sum & 'hFFFF, sum >>> 32'sh10);
        return HftWordMath___add(sum & 'hFFFF, sum >>> 32'sh10);
    endfunction

    function HftWordParts parts (input logic[63:0] beat);
        HftWordParts result;
        logic[31:0] offset;
        logic[31:0] bytes;
        logic[31:0] first;
        logic[31:0] second;
        result = 0;
        result.word=beat;
        result.metadata=beat >>> 32'sh20;
        offset=((result.metadata >>> 32'shC)) & 'hFF;
        bytes=result.metadata >>> 32'sh1A;
        first=((((result.word & 'hFF)) <<< 'h8)) | ((bytes>=32'h2) ? ((((result.word >>> 32'sh8)) & 'hFF)) : ('h0));
        second=((((result.word >>> 32'sh8)) & 'hFF00)) | (((bytes == 32'h4)) ? ((result.word >>> 32'sh18)) : ('h0));
        result.ip0=((offset>=32'hE && (offset < 32'h22)) && (bytes != 32'h0)) ? (first) : ('h0);
        result.ip1=((offset>=32'hC && (offset < 32'h20)) && bytes>=32'h3) ? (second) : ('h0);
        result.udp0=((offset>=32'h1A && (offset < 32'h4A)) && (bytes != 32'h0)) ? (first) : ('h0);
        result.udp1=((offset>=32'h18 && (offset < 32'h48)) && bytes>=32'h3) ? (second) : ('h0);
        return result;
    endfunction

    function HftQuote applicationWord (
        input HftQuote quote
,       input logic[31:0] offset
,       input logic[31:0] word
    );
        if (offset == 32'h30) begin
            quote._sequence=word >>> 32'sh10;
        end
        else begin
            if (offset == 32'h34) begin
                quote._sequence|=word <<< 'h10;
                quote.symbol=word >>> 32'sh10;
            end
            else begin
                if (offset == 32'h38) begin
                    quote.symbol|=word <<< 'h10;
                    quote.bid=word >>> 32'sh10;
                end
                else begin
                    if (offset == 32'h3C) begin
                        quote.bid|=word <<< 'h10;
                        quote.ask=word >>> 32'sh10;
                    end
                    else begin
                        if (offset == 32'h40) begin
                            quote.ask|=word <<< 'h10;
                            quote.bid_size=word >>> 32'sh10;
                        end
                        else begin
                            if (offset == 32'h44) begin
                                quote.bid_size|=word <<< 'h10;
                                quote.ask_size=word >>> 32'sh10;
                            end
                            else begin
                                if (offset == 32'h48) begin
                                    quote.ask_size|=word <<< 'h10;
                                end
                            end
                        end
                    end
                end
            end
        end
        return quote;
    endfunction

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

    function HftCollection collect (
        input HftCollection state
,       input HftWordParts beat
    );
        logic[31:0] offset;
        offset=((beat.metadata >>> 32'shC)) & 'hFF;
        if (((beat.metadata & (('h1 <<< 'h18))) != '0)) begin
            state.crc='hFFFFFFFF;
            state.ip_sum=32'h0;
            state.udp_sum=32'h39;
            state.ethernet_ok=1;
            state.ipv4_ok=1;
            state.udp_ok=1;
            state.sbe_ok=1;
            state.udp_checksum_present=0;
        end
        if (((beat.metadata & 'h200) != '0)) begin
            state.crc=EthernetCrc___word(state.crc, beat.word, beat.metadata >>> 32'sh1A);
            state.ip_sum=HftWordMath___add(HftWordMath___add(state.ip_sum, beat.ip0), beat.ip1);
            state.udp_sum=HftWordMath___add(HftWordMath___add(state.udp_sum, beat.udp0), beat.udp1);
            state.ethernet_ok=state.ethernet_ok && (((beat.metadata & (('h1 <<< 'h14)))) != '0);
            state.ipv4_ok=state.ipv4_ok && (((beat.metadata & (('h1 <<< 'h15)))) != '0);
            state.udp_ok=state.udp_ok && (((beat.metadata & (('h1 <<< 'h16)))) != '0);
            state.sbe_ok=state.sbe_ok && (((beat.metadata & (('h1 <<< 'h17)))) != '0);
            if (offset == 32'h28) begin
                state.udp_checksum_present=((beat.word & 'hFFFF)) != 32'h0;
            end
            state.quote = applicationWord(state.quote, offset, beat.word);
        end
        return state;
    endfunction

    function HftFoldedFrame finish (input HftFrameCheck frame);
        HftFoldedFrame result;
        result = 0;
        result.quote = frame.collection.quote;
        result.ip_sum=fold(frame.collection.ip_sum);
        result.udp_sum=fold(frame.collection.udp_sum);
        result.udp_checksum_present=frame.collection.udp_checksum_present;
        result.size_error=frame.size_error;
        result.crc_error=(frame.complete && !frame.size_error) && (frame.collection.crc != 'hDEBB20E3);
        result.valid=((((((frame.complete && frame.normal_length) && !frame.size_error) && !result.crc_error) && frame.collection.ethernet_ok) && frame.collection.ipv4_ok) && frame.collection.udp_ok) && frame.collection.sbe_ok;
        return result;
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

    function HftCandidate validate (input HftFoldedFrame frame);
        HftCandidate result;
        result = 0;
        result._sequence=frame.quote._sequence;
        result.bid=(!HftWordMath___less(frame.quote.bid_size, 32'h64)) ? (frame.quote.bid) : ('h0);
        result.ask=(!HftWordMath___less(frame.quote.ask_size, 32'h64)) ? (frame.quote.ask) : ('h0);
        result.size_error=frame.size_error;
        result.crc_error=frame.crc_error;
        result.valid=(((((frame.valid && (frame.ip_sum == 32'hFFFF)) && ((!frame.udp_checksum_present || (frame.udp_sum == 32'hFFFF)))) && (frame.quote.symbol == 32'h1)) && (frame.quote._sequence != 32'h0)) && (frame.quote.bid != 32'h0)) && HftWordMath___less(frame.quote.bid, frame.quote.ask);
        return result;
    endfunction

    always_comb begin : collection_comb_func  // collection_comb_func
        collection_comb = collect(collection_reg, parts_reg);
    end

    always_comb begin : check_comb_func  // check_comb_func
        check_comb = 0;
        check_comb.collection = collection_comb;
        check_comb.complete=(((parts_reg.metadata & 'h200)) != '0) && (((parts_reg.metadata & (('h1 <<< 'h19)))) != '0);
        check_comb.normal_length=((parts_reg.metadata & 'hFF)) == 32'h4E;
        check_comb.size_error=((parts_reg.metadata & 'h400) != '0);
    end

    always_comb begin : offer_comb_func  // offer_comb_func
        offer_comb = candidate_reg;
        offer_comb.valid=candidate_reg.valid && HftWordMath___less(unsigned'(32'(sequence_reg)), candidate_reg._sequence);
    end

    generate  // _assign
        assign valid_out = ((((valid_reg[64'h4]) != '0) && offer_reg.valid) != '0);
        assign ready_out = ((!valid_out || ready_in) != '0);
        assign sequence_out = offer_reg._sequence;
        assign bid_out = offer_reg.bid;
        assign ask_out = offer_reg.ask;
        assign size_error_out = (((((valid_reg[64'h4]) != '0) && ready_out) && offer_reg.size_error) != '0);
        assign crc_error_out = (((((valid_reg[64'h4]) != '0) && ready_out) && offer_reg.crc_error) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        if (reset) begin
            valid_reg_tmp = '0;
            parts_reg_tmp = '0;
            collection_reg_tmp = '0;
            check_reg_tmp = '0;
            folded_reg_tmp = '0;
            candidate_reg_tmp = '0;
            offer_reg_tmp = '0;
            sequence_reg_tmp = '0;
        end
        else begin
            if (ready_out) begin
                valid_reg_tmp = 32'(((unsigned'(32'(valid_reg)) <<< 'h1)) | unsigned'(32'(valid_in)));
                if (valid_in) begin
                    parts_reg_tmp = parts(data_in);
                end
                if (valid_reg[64'h0]) begin
                    collection_reg_tmp = collection_comb;
                    check_reg_tmp = check_comb;
                end
                if (valid_reg[64'h1]) begin
                    folded_reg_tmp = finish(check_reg);
                end
                if (valid_reg[64'h2]) begin
                    candidate_reg_tmp = validate(folded_reg);
                end
                if (valid_reg[64'h3]) begin
                    offer_reg_tmp = offer_comb;
                    if (offer_comb.valid) begin
                        sequence_reg_tmp = candidate_reg._sequence;
                    end
                end
            end
        end
    end
    endtask

    always @(posedge clk) begin
        valid_reg_tmp = valid_reg;
        parts_reg_tmp = parts_reg;
        collection_reg_tmp = collection_reg;
        check_reg_tmp = check_reg;
        folded_reg_tmp = folded_reg;
        candidate_reg_tmp = candidate_reg;
        offer_reg_tmp = offer_reg;
        sequence_reg_tmp = sequence_reg;

        _work(reset);

        valid_reg <= valid_reg_tmp;
        parts_reg <= parts_reg_tmp;
        collection_reg <= collection_reg_tmp;
        check_reg <= check_reg_tmp;
        folded_reg <= folded_reg_tmp;
        candidate_reg <= candidate_reg_tmp;
        offer_reg <= offer_reg_tmp;
        sequence_reg <= sequence_reg_tmp;
    end


endmodule

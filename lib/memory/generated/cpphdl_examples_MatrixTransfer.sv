`default_nettype none

import Predef_pkg::*;
import FpFormatConversion_pkg::*;


module cpphdl_examples_MatrixTransfer (
    input wire clk
,   input wire reset
,   input wire command_valid_in
,   output wire command_ready_out
,   input wire[31:0] source_in
,   input wire[31:0] destination_in
,   input wire[31:0] rows_in
,   input wire[31:0] columns_in
,   input wire[31:0] source_row_stride_in
,   input wire[31:0] source_column_stride_in
,   input wire[31:0] destination_row_stride_in
,   input wire[31:0] destination_column_stride_in
,   input wire half_in
,   output wire response_valid_out
,   input wire response_ready_in
,   output wire[31:0] fault_out
,   output wire[31:0] m_address_out
,   output wire[16-1:0][32-1:0] m_writedata_out
,   output wire[64-1:0] m_byteenable_out
,   output wire m_read_out
,   output wire m_write_out
,   input wire m_waitrequest_in
,   input wire[16-1:0][32-1:0] m_readdata_in
,   input wire m_readdatavalid_in
,   input wire m_readdataerror_in
);

    typedef logic[16-1:0][32-1:0] Beat;

    // regs and combs
    reg[2-1:0] state;
    reg[32-1:0] source;
    reg[32-1:0] destination;
    reg[32-1:0] rows;
    reg[32-1:0] columns;
    reg[32-1:0] sr;
    reg[32-1:0] sc;
    reg[32-1:0] dr;
    reg[32-1:0] dc;
    reg[32-1:0] error;
    reg half;
    reg load_rows;
    reg store_rows;
    reg[32-1:0] tile_row;
    reg[32-1:0] tile_col;
    reg[32-1:0] tile_rows;
    reg[32-1:0] tile_cols;
    reg[32-1:0] cursor_row;
    reg[32-1:0] cursor_col;
    reg[32-1:0] head;
    reg[32-1:0] tail;
    reg[32-1:0] pending;
    reg requests_done;
    reg store_valid;
    reg read_held;
    reg[64-1:0][32-1:0] metadata;
    reg[32-1:0] store_meta;
    reg[32-1:0] store_address;
    reg[64-1:0] store_mask;
    logic[31:0] address_comb;
    logic[31:0] count_comb;
    logic[31:0] meta_comb;
    logic[31:0] response_meta_comb;
    logic banks_comb;
    logic fetch_comb;
    logic return_comb;
    logic[32-1:0] write_enable_comb;
    logic[32-1:0] read_enable_comb;
    logic[32-1:0][32-1:0] write_address_comb;
    logic[32-1:0][32-1:0] read_address_comb;
    logic[32-1:0][32-1:0] write_data_comb;
    logic[32-1:0][32-1:0] read_data_comb;
    logic[16-1:0][32-1:0] data_comb;

    // members
    genvar __i;
    wire bank__read_in[32];
    wire bank__write_in[32];
    wire[31:0] bank__read_address_in[32];
    wire[31:0] bank__write_address_in[32];
    wire[unsigned'(64'(1))-1:0][32-1:0] bank__data_in[32];
    wire[unsigned'(64'(1))-1:0][32-1:0] bank__data_out[32];
    generate
    for (__i=0; __i < 32; __i = __i + 1) begin
        cpphdl_examples_SynchronousRam #(
        1
,       32
        ) bank (
            .clk(clk)
        ,           .reset(reset)
        ,           .read_in(bank__read_in[__i])
        ,           .write_in(bank__write_in[__i])
        ,           .read_address_in(bank__read_address_in[__i])
        ,           .write_address_in(bank__write_address_in[__i])
        ,           .data_in(bank__data_in[__i])
        ,           .data_out(bank__data_out[__i])
        );
    end
    endgenerate

    // tmp variables
    logic[2-1:0] state_tmp;
    logic[32-1:0] source_tmp;
    logic[32-1:0] destination_tmp;
    logic[32-1:0] rows_tmp;
    logic[32-1:0] columns_tmp;
    logic[32-1:0] sr_tmp;
    logic[32-1:0] sc_tmp;
    logic[32-1:0] dr_tmp;
    logic[32-1:0] dc_tmp;
    logic[32-1:0] error_tmp;
    logic half_tmp;
    logic load_rows_tmp;
    logic store_rows_tmp;
    logic[32-1:0] tile_row_tmp;
    logic[32-1:0] tile_col_tmp;
    logic[32-1:0] tile_rows_tmp;
    logic[32-1:0] tile_cols_tmp;
    logic[32-1:0] cursor_row_tmp;
    logic[32-1:0] cursor_col_tmp;
    logic[32-1:0] head_tmp;
    logic[32-1:0] tail_tmp;
    logic[32-1:0] pending_tmp;
    logic requests_done_tmp;
    logic store_valid_tmp;
    logic read_held_tmp;
    logic[64-1:0][32-1:0] metadata_tmp;
    logic[32-1:0] store_meta_tmp;
    logic[32-1:0] store_address_tmp;
    logic[64-1:0] store_mask_tmp;


    function logic[31:0] minimum (
        input logic[31:0] a
,       input logic[31:0] b
    );
        return (a < b) ? (a) : (b);
    endfunction

    always_comb begin : address_comb_func  // address_comb_func
        logic row_fast;
        logic[31:0] row_stride;
        logic[31:0] col_stride;
        logic[31:0] element;
        logic[31:0] available;
        logic[31:0] fast_left;
        logic[31:0] fast_stride;
        row_stride=(state == 64'h1) ? (unsigned'(32'(sr))) : (unsigned'(32'(dr)));
        col_stride=(state == 64'h1) ? (unsigned'(32'(sc))) : (unsigned'(32'(dc)));
        row_fast=(state == 64'h1) ? (((load_rows) != '0)) : (((store_rows) != '0));
        element=((state == 64'h1) && ((half) != '0)) ? ('h2) : ('h4);
        address_comb=((((state == 64'h1)) ? (unsigned'(32'(source))) : (unsigned'(32'(destination)))) + (((unsigned'(32'(tile_row)) + unsigned'(32'(cursor_row))))*row_stride)) + (((unsigned'(32'(tile_col)) + unsigned'(32'(cursor_col))))*col_stride);
        available=((32'h40 - ((address_comb & 32'h3F))))/element;
        fast_left=(row_fast) ? (unsigned'(32'(tile_rows)) - unsigned'(32'(cursor_row))) : (unsigned'(32'(tile_cols)) - unsigned'(32'(cursor_col)));
        fast_stride=(row_fast) ? (row_stride) : (col_stride);
        count_comb=minimum(fast_left, (fast_stride == element) ? (available) : (32'h1));
        meta_comb=((unsigned'(32'(cursor_row)) | ((unsigned'(32'(cursor_col)) <<< 'h5))) | ((count_comb <<< 'hA))) | ((((address_comb & 32'h3F)) <<< 'h10));
    end

    function logic[63:0] FpFormatConversion___convert (
        input logic[63:0] value
,       input logic[31:0] width
,       input logic[31:0] exp_width
,       input logic[31:0] out_width
,       input logic[31:0] out_exp_width
,       input logic preserve_subnormals
,       input logic preserve_nan_payload
    );
        logic[31:0] mant_width;
        logic[31:0] out_mant_width;
        logic[63:0] exp_max;
        logic[63:0] mant_max;
        logic[63:0] out_exp_max;
        logic[63:0] out_mant_max;
        logic signed[31:0] exp_bias;
        logic signed[31:0] out_exp_bias;
        logic[63:0] significand;
        logic[63:0] retained;
        logic[63:0] tail;
        logic[63:0] halfway;
        logic[63:0] leading;
        logic[63:0] output_sign;
        logic[63:0] payload;
        logic[31:0] input_exponent;
        logic[31:0] normalize;
        logic signed[31:0] output_exponent;
        logic signed[31:0] shift;
        mant_width=(width - exp_width) - 32'h1;
        out_mant_width=(out_width - out_exp_width) - 32'h1;
        exp_max=((64'h1 <<< exp_width)) - 64'h1;
        mant_max=((64'h1 <<< mant_width)) - 64'h1;
        out_exp_max=((64'h1 <<< out_exp_width)) - 64'h1;
        out_mant_max=((64'h1 <<< out_mant_width)) - 64'h1;
        exp_bias=(('h1 <<< ((exp_width - 32'h1)))) - 'h1;
        out_exp_bias=(('h1 <<< ((out_exp_width - 32'h1)))) - 'h1;
        output_sign=((((value >>> ((width - 32'h1)))) & 64'h1)) <<< ((out_width - 32'h1));
        input_exponent=((value >>> mant_width)) & exp_max;
        significand=value & mant_max;
        if (64'(input_exponent) == exp_max) begin
            payload=signed'(32'((((significand) != '0)) ? ('h1) : ('h0)));
            if (preserve_nan_payload && ((significand) != '0)) begin
                if (out_mant_width>=mant_width) begin
                    payload=significand <<< ((out_mant_width - mant_width));
                end
                else begin
                    payload=significand >>> ((mant_width - out_mant_width));
                end
                if (!((payload) != '0)) begin
                    payload=64'h1;
                end
            end
            return 64'((output_sign | ((out_exp_max <<< out_mant_width))) | payload);
        end
        if (!((input_exponent) != '0) && ((!((significand) != '0) || !preserve_subnormals))) begin
            return output_sign;
        end
        output_exponent=(signed'(32'((((input_exponent) != '0)) ? (input_exponent) : (32'h1))) - exp_bias) + out_exp_bias;
        if (((input_exponent) != '0)) begin
            significand|=64'h1 <<< mant_width;
        end
        else begin
            leading=significand;
            leading|=leading >>> 32'sh1;
            leading|=leading >>> 32'sh2;
            leading|=leading >>> 32'sh4;
            leading|=leading >>> 32'sh8;
            leading|=leading >>> 32'sh10;
            if (($bits(logic[63:0])/8) > 64'h4) begin
                leading|=leading >>> 32'sh20;
            end
            leading^=leading >>> 32'sh1;
            normalize=mant_width - ((((((((32'((((64'(leading) & 64'hFFFFFFFF00000000)) != 64'h0)) <<< 'h5)) | ((32'((((64'(leading) & 64'hFFFF0000FFFF0000)) != 64'h0)) <<< 'h4))) | ((32'((((64'(leading) & 64'hFF00FF00FF00FF00)) != 64'h0)) <<< 'h3))) | ((32'((((64'(leading) & 64'hF0F0F0F0F0F0F0F0)) != 64'h0)) <<< 'h2))) | ((32'((((64'(leading) & 64'hCCCCCCCCCCCCCCCC)) != 64'h0)) <<< 'h1))) | 32'((((64'(leading) & 64'hAAAAAAAAAAAAAAAA)) != 64'h0))));
            significand<<=normalize;
            output_exponent-=signed'(32'(normalize));
        end
        if (output_exponent<=32'sh0 && !preserve_subnormals) begin
            return output_sign;
        end
        if (output_exponent>=signed'(32'(out_exp_max))) begin
            return 64'(output_sign | ((out_exp_max <<< out_mant_width)));
        end
        shift=signed'(32'(mant_width)) - signed'(32'(out_mant_width));
        if (output_exponent<=32'sh0) begin
            shift+='h1 - output_exponent;
        end
        retained=64'h0;
        if (shift<=32'sh0) begin
            retained=significand <<< unsigned'(32'(-shift));
        end
        else begin
            if (shift < signed'(32'((($bits(logic[63:0])/8)*64'h8)))) begin
                retained=significand >>> unsigned'(32'(shift));
                tail=significand & ((((64'h1 <<< unsigned'(32'(shift)))) - 64'h1));
                halfway=64'h1 <<< 32'((shift - 'h1));
                if ((tail > halfway) || (((tail == halfway) && ((((retained & 64'h1))) != '0)))) begin
                    retained=retained+1;
                end
            end
        end
        if (output_exponent<=32'sh0) begin
            output_exponent=(retained>=(64'h1 <<< out_mant_width)) ? ('h1) : ('h0);
        end
        else begin
            if (retained>=(64'h1 <<< ((out_mant_width + 32'h1)))) begin
                retained>>='h1;
                output_exponent=output_exponent+1;
            end
        end
        if (output_exponent>=signed'(32'(out_exp_max))) begin
            return 64'(output_sign | ((out_exp_max <<< out_mant_width)));
        end
        return 64'((output_sign | ((unsigned'(64'(output_exponent)) <<< out_mant_width))) | ((retained & out_mant_max)));
    endfunction

    always_comb begin : banks_comb_func  // banks_comb_func
        logic[31:0] ignored;
        logic[31:0] meta;
        logic[31:0] r;
        logic[31:0] c;
        logic[31:0] count;
        logic[31:0] offset;
        logic[31:0] index;
        logic[31:0] raw;
        r=32'h0;
        c=32'h0;
        index=32'h0;
        raw=32'h0;
        ignored=address_comb;
        banks_comb=1;
        return_comb=((state == 64'h1) && m_readdatavalid_in) && (((pending != 64'h0) || ((m_read_out && !m_waitrequest_in))));
        fetch_comb=(((state == 64'h2) && !((requests_done) != '0)) && ((!((store_valid) != '0) || !m_waitrequest_in))) && (error == 64'h0);
        response_meta_comb=(pending != 64'h0) ? (unsigned'(32'(metadata[head]))) : (meta_comb);
        write_enable_comb = 'h0;
        read_enable_comb = 'h0;
        write_address_comb = 'h0;
        read_address_comb = 'h0;
        write_data_comb = 'h0;
        meta=response_meta_comb;
        count=((meta >>> 32'shA)) & 32'h3F;
        offset=((meta >>> 32'sh10)) & 32'h3F;
        for (logic[31:0] lane = 32'h0;lane < 32'h20;lane=lane+1) begin
            if (((return_comb && (error == 64'h0)) && !m_readdataerror_in) && (lane < count)) begin
                r=((meta & 32'h1F)) + ((((load_rows) != '0)) ? (lane) : (32'h0));
                c=((((meta >>> 32'sh5)) & 32'h1F)) + ((((load_rows) != '0)) ? (32'h0) : (lane));
                index=((r + c)) & 32'h1F;
                raw=m_readdata_in[64'(32'(((offset + (lane*unsigned'(32'(((((half) != '0)) ? ('h2) : ('h4)))))))/32'h4))];
                if (((half) != '0)) begin
                    raw=((raw >>> (((((((offset/32'h2) + lane)) & 32'h1))*32'h10)))) & 32'hFFFF;
                end
                write_enable_comb[64'(index)] = 1;
                write_address_comb[64'(index)] = r;
                write_data_comb[64'(index)] = unsigned'(32'((((half) != '0)) ? (unsigned'(32'(FpFormatConversion___convert(64'(16'(raw)), 32'h10, 32'h5, 32'h20, 32'h8, 1, 1)))) : (raw)));
            end
        end
        for (logic[31:0] lane = 32'h0;lane < 32'h10;lane=lane+1) begin
            if (fetch_comb && (lane < count_comb)) begin
                r=unsigned'(32'(cursor_row)) + ((((store_rows) != '0)) ? (lane) : (32'h0));
                c=unsigned'(32'(cursor_col)) + ((((store_rows) != '0)) ? (32'h0) : (lane));
                index=((r + c)) & 32'h1F;
                read_enable_comb[64'(index)] = 1;
                read_address_comb[64'(index)] = r;
            end
        end
    end

    always_comb begin : data_comb_func  // data_comb_func
        logic[31:0] r;
        logic[31:0] c;
        logic[31:0] index;
        logic[31:0] meta;
        logic[31:0] count;
        logic[31:0] offset;
        r=32'h0;
        c=32'h0;
        index=32'h0;
        for (logic[31:0] i = 32'h0;i < 32'h20;i=i+1) begin
            read_data_comb[64'(i)] = bank__data_out[i][64'h0];
        end
        data_comb = 'h0;
        meta=store_meta;
        count=((meta >>> 32'shA)) & 32'h3F;
        offset=((meta >>> 32'sh10)) & 32'h3F;
        for (logic[31:0] lane = 32'h0;lane < 32'h10;lane=lane+1) begin
            if (lane < count) begin
                r=((meta & 32'h1F)) + ((((store_rows) != '0)) ? (lane) : (32'h0));
                c=((((meta >>> 32'sh5)) & 32'h1F)) + ((((store_rows) != '0)) ? (32'h0) : (lane));
                index=((r + c)) & 32'h1F;
                data_comb[64'(32'((offset/32'h4) + lane))] = read_data_comb[64'(index)];
            end
        end
    end

    task advance_cursor (
        input logic row_fast
,       input logic[31:0] count
    );
    begin: advance_cursor
        logic[31:0] next;
        next=((row_fast) ? (unsigned'(32'(cursor_row))) : (unsigned'(32'(cursor_col)))) + count;
        if (next == ((row_fast) ? (unsigned'(32'(tile_rows))) : (unsigned'(32'(tile_cols))))) begin
            if (row_fast) begin
                cursor_row_tmp = 32'h0;
                cursor_col_tmp = 32'(unsigned'(32'(cursor_col)) + 32'h1);
            end
            else begin
                cursor_col_tmp = 32'h0;
                cursor_row_tmp = 32'(unsigned'(32'(cursor_row)) + 32'h1);
            end
            if (((row_fast) ? ((unsigned'(32'(cursor_col)) + 32'h1)) : ((unsigned'(32'(cursor_row)) + 32'h1))) == ((row_fast) ? (unsigned'(32'(tile_cols))) : (unsigned'(32'(tile_rows))))) begin
                requests_done_tmp = 1;
            end
        end
        else begin
            if (row_fast) begin
                cursor_row_tmp = next;
            end
            else begin
                cursor_col_tmp = next;
            end
        end
    end
    endtask

    generate  // _assign
        genvar gi;
        assign command_ready_out = ((state == 64'h0) != '0);
        assign response_valid_out = ((state == 64'h3) != '0);
        assign fault_out = unsigned'(32'(error));
        assign m_read_out = (((state == 64'h1) && ((((read_held) != '0) || (((!((requests_done) != '0) && (pending < 64'h40)) && (error == 64'h0)))))) != '0);
        assign m_write_out = ((((state == 64'h2) && ((store_valid) != '0)) && (error == 64'h0)) != '0);
        assign m_address_out = (state == 64'h2) ? (unsigned'(32'(store_address))) : ((address_comb & 'hFFFFFFC0));
        assign m_byteenable_out = store_mask;
        assign m_writedata_out = data_comb;
        for (gi = 32'h0;gi < 32'h20;gi=gi+1) begin
            assign bank__read_in[gi] = ((read_enable_comb[64'(gi)]) != '0);
            assign bank__read_address_in[gi] = unsigned'(32'(read_address_comb[64'(gi)]));
            assign bank__write_in[gi] = ((write_enable_comb[64'(gi)]) != '0);
            assign bank__write_address_in[gi] = unsigned'(32'(write_address_comb[64'(gi)]));
            assign bank__data_in[gi] = write_data_comb[64'(gi)];
        end
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic request;
        logic response;
        logic ignored;
        logic[31:0] outstanding;
        logic[31:0] element;
        logic[63:0] src_end;
        logic[63:0] dst_end;
        logic[63:0] mask;
        if (!reset) begin
            ignored=banks_comb;
        end
        for (logic[31:0] i = 32'h0;i < 32'h20;i=i+1) begin
        end
        request=m_read_out && !m_waitrequest_in;
        response=return_comb;
        if (reset) begin
            state_tmp = '0;
            source_tmp = '0;
            destination_tmp = '0;
            rows_tmp = '0;
            columns_tmp = '0;
            sr_tmp = '0;
            sc_tmp = '0;
            dr_tmp = '0;
            dc_tmp = '0;
            error_tmp = '0;
            half_tmp = '0;
            load_rows_tmp = '0;
            store_rows_tmp = '0;
            tile_row_tmp = '0;
            tile_col_tmp = '0;
            tile_rows_tmp = '0;
            tile_cols_tmp = '0;
            cursor_row_tmp = '0;
            cursor_col_tmp = '0;
            head_tmp = '0;
            tail_tmp = '0;
            pending_tmp = '0;
            requests_done_tmp = '0;
            store_valid_tmp = '0;
            read_held_tmp = '0;
            metadata_tmp = '0;
            store_meta_tmp = '0;
            store_address_tmp = '0;
            store_mask_tmp = '0;
        end
        else begin
            read_held_tmp = m_read_out && m_waitrequest_in;
            if ((state == 64'h0) && command_valid_in) begin
                source_tmp = source_in;
                destination_tmp = destination_in;
                rows_tmp = rows_in;
                columns_tmp = columns_in;
                sr_tmp = source_row_stride_in;
                sc_tmp = source_column_stride_in;
                dr_tmp = destination_row_stride_in;
                dc_tmp = destination_column_stride_in;
                half_tmp = half_in;
                element=(half_in) ? ('h2) : ('h4);
                load_rows_tmp = source_row_stride_in < source_column_stride_in;
                store_rows_tmp = destination_row_stride_in < destination_column_stride_in;
                tile_row_tmp = 32'h0;
                tile_col_tmp = 32'h0;
                tile_rows_tmp = minimum(rows_in, 32'h20);
                tile_cols_tmp = minimum(columns_in, 32'h20);
                cursor_row_tmp = 32'h0;
                cursor_col_tmp = 32'h0;
                head_tmp = 32'h0;
                tail_tmp = 32'h0;
                pending_tmp = 32'h0;
                requests_done_tmp = 0;
                store_valid_tmp = 0;
                error_tmp = 32'h0;
                src_end=((unsigned'(64'(source_in)) + (128'(64'(32'((rows_in - 32'h1))))*128'(unsigned'(64'(source_row_stride_in))))) + (128'(64'(32'((columns_in - 32'h1))))*128'(unsigned'(64'(source_column_stride_in))))) + 64'(element);
                dst_end=((unsigned'(64'(destination_in)) + (128'(64'(32'((rows_in - 32'h1))))*128'(unsigned'(64'(destination_row_stride_in))))) + (128'(64'(32'((columns_in - 32'h1))))*128'(unsigned'(64'(destination_column_stride_in))))) + 64'h4;
                if ((((((((((((!((rows_in) != '0) || !((columns_in) != '0)) || (rows_in > 32'h20000)) || (columns_in > 32'h20000)) || (((((((source_in | source_row_stride_in) | source_column_stride_in)) & ((element - 32'h1))))) != '0)) || (((((((destination_in | destination_row_stride_in) | destination_column_stride_in)) & 32'h3))) != '0)) || !((source_row_stride_in) != '0)) || !((source_column_stride_in) != '0)) || !((destination_row_stride_in) != '0)) || !((destination_column_stride_in) != '0)) || (64'(src_end) > 64'h100000000)) || (64'(dst_end) > 64'h100000000)) || (((unsigned'(64'(source_in)) < dst_end) && (unsigned'(64'(destination_in)) < src_end)))) begin
                    error_tmp = 32'h1;
                    state_tmp = 64'h3;
                end
                else begin
                    state_tmp = 64'h1;
                end
            end
            if (state == 64'h1) begin
                outstanding=(unsigned'(32'(pending)) + unsigned'(32'(((request) ? ('h1) : ('h0))))) - unsigned'(32'(((response) ? ('h1) : ('h0))));
                pending_tmp = outstanding;
                if (request) begin
                    metadata_tmp[tail] = meta_comb;
                    tail_tmp = 32'(((unsigned'(32'(tail)) + 32'h1)) & 32'h3F);
                    advance_cursor(((load_rows) != '0), count_comb);
                end
                if (response) begin
                    head_tmp = 32'(((unsigned'(32'(head)) + 32'h1)) & 32'h3F);
                    if (m_readdataerror_in) begin
                        error_tmp = 32'h2;
                    end
                end
                if ((((((requests_done) != '0) || (error != 64'h0))) && (outstanding == 32'h0)) && !m_read_out) begin
                    state_tmp = signed'(32'((((error != 64'h0) || ((response && m_readdataerror_in)))) ? ('h3) : ('h2)));
                    cursor_row_tmp = 32'h0;
                    cursor_col_tmp = 32'h0;
                    requests_done_tmp = 0;
                end
            end
            if (state == 64'h2) begin
                if (((store_valid) != '0) && !m_waitrequest_in) begin
                    store_valid_tmp = 0;
                end
                if (fetch_comb) begin
                    store_meta_tmp = meta_comb;
                    store_address_tmp = 32'(address_comb & 'hFFFFFFC0);
                    mask=(count_comb == 32'h10) ? (~64'h0) : (((((64'h1 <<< ((count_comb*32'h4)))) - 64'h1)) <<< ((address_comb & 32'h3F)));
                    store_mask_tmp = mask;
                    store_valid_tmp = 1;
                    advance_cursor(((store_rows) != '0), count_comb);
                end
                if (((requests_done) != '0) && ((!((store_valid) != '0) || !m_waitrequest_in))) begin
                    cursor_row_tmp = 32'h0;
                    cursor_col_tmp = 32'h0;
                    requests_done_tmp = 0;
                    if (64'(32'((unsigned'(32'(tile_col)) + 32'h20))) < columns) begin
                        tile_col_tmp = 32'(unsigned'(32'(tile_col)) + 32'h20);
                        tile_cols_tmp = minimum((unsigned'(32'(columns)) - unsigned'(32'(tile_col))) - 32'h20, 32'h20);
                        state_tmp = 64'h1;
                    end
                    else begin
                        if (64'(32'((unsigned'(32'(tile_row)) + 32'h20))) < rows) begin
                            tile_row_tmp = 32'(unsigned'(32'(tile_row)) + 32'h20);
                            tile_rows_tmp = minimum((unsigned'(32'(rows)) - unsigned'(32'(tile_row))) - 32'h20, 32'h20);
                            tile_col_tmp = 32'h0;
                            tile_cols_tmp = minimum(unsigned'(32'(columns)), 32'h20);
                            state_tmp = 64'h1;
                        end
                        else begin
                            state_tmp = 64'h3;
                        end
                    end
                end
            end
            if ((state == 64'h3) && response_ready_in) begin
                state_tmp = 64'h0;
            end
        end
    end
    endtask

    always @(posedge clk) begin
        state_tmp = state;
        source_tmp = source;
        destination_tmp = destination;
        rows_tmp = rows;
        columns_tmp = columns;
        sr_tmp = sr;
        sc_tmp = sc;
        dr_tmp = dr;
        dc_tmp = dc;
        error_tmp = error;
        half_tmp = half;
        load_rows_tmp = load_rows;
        store_rows_tmp = store_rows;
        tile_row_tmp = tile_row;
        tile_col_tmp = tile_col;
        tile_rows_tmp = tile_rows;
        tile_cols_tmp = tile_cols;
        cursor_row_tmp = cursor_row;
        cursor_col_tmp = cursor_col;
        head_tmp = head;
        tail_tmp = tail;
        pending_tmp = pending;
        requests_done_tmp = requests_done;
        store_valid_tmp = store_valid;
        read_held_tmp = read_held;
        metadata_tmp = metadata;
        store_meta_tmp = store_meta;
        store_address_tmp = store_address;
        store_mask_tmp = store_mask;

            _work(reset);

        state <= state_tmp;
        source <= source_tmp;
        destination <= destination_tmp;
        rows <= rows_tmp;
        columns <= columns_tmp;
        sr <= sr_tmp;
        sc <= sc_tmp;
        dr <= dr_tmp;
        dc <= dc_tmp;
        error <= error_tmp;
        half <= half_tmp;
        load_rows <= load_rows_tmp;
        store_rows <= store_rows_tmp;
        tile_row <= tile_row_tmp;
        tile_col <= tile_col_tmp;
        tile_rows <= tile_rows_tmp;
        tile_cols <= tile_cols_tmp;
        cursor_row <= cursor_row_tmp;
        cursor_col <= cursor_col_tmp;
        head <= head_tmp;
        tail <= tail_tmp;
        pending <= pending_tmp;
        requests_done <= requests_done_tmp;
        store_valid <= store_valid_tmp;
        read_held <= read_held_tmp;
        metadata <= metadata_tmp;
        store_meta <= store_meta_tmp;
        store_address <= store_address_tmp;
        store_mask <= store_mask_tmp;
    end


endmodule

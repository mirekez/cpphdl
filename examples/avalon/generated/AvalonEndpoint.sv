`default_nettype none

import Predef_pkg::*;


module AvalonEndpoint #(
    parameter DATA_WIDTH = 'h200
,   parameter AVALON_WIDTH = 'h200
,   parameter AVALON_BITS = $clog2(AVALON_WIDTH/'h8)
 )
 (
    input wire clk
,   input wire reset
,   input wire debugen_in
,   input wire[DATA_BYTES-1:0][8-1:0] data_in
,   input wire valid_in
,   input wire[63:0] addr_in
,   input wire[7:0] nbytes_in
,   output wire wait_out
,   output wire[63:0] avmm_address_out
,   output wire[AV_BYTES-1:0][8-1:0] avmm_writedata_out
,   output wire[AV_BYTES-1:0] avmm_byteenable_out
,   output wire avmm_write_out
,   output wire avmm_read_out
,   input wire avmm_waitrequest_in
,   input wire[AV_BYTES-1:0][8-1:0] avmm_readdata_in
,   input wire avmm_readdatavalid_in
);
    localparam  DATA_BYTES = ((DATA_WIDTH + 'h7))/'h8;
    localparam  AV_BYTES = AVALON_WIDTH/'h8;
    localparam  DATA_BITS = AV_BYTES*'h8;
    localparam  OUTPUT_BITS = (DATA_BITS + 'h40) + AV_BYTES;


    // regs and combs
    reg valid_delayed;
    reg[DATA_BYTES-1:0][8-1:0] data_delayed;
    reg[64-1:0] addr_delayed;
    reg[8-1:0] nbytes_delayed;
    reg hole_delayed;
    reg[DATA_BITS-1:0] buffer1_precalc;
    reg[DATA_BITS-1:0] buffer2_precalc;
    reg buffer1_valid;
    reg[DATA_BITS-1:0] buffer1;
    reg[AV_BYTES-1:0] buffer1_byteenable;
    reg[64-1:0] buffer1_address;
    reg buffer2_valid;
    reg[DATA_BITS-1:0] buffer2;
    reg[AV_BYTES-1:0] buffer2_byteenable;
    reg[64-1:0] buffer2_address;
    reg[8-1:0] buffer2_pos;

    // members
    wire output_buffer__valid_in;
    wire[OUTPUT_BITS-1:0] output_buffer__data_in;
    wire output_buffer__ready_out;
    wire output_buffer__valid_out;
    wire[OUTPUT_BITS-1:0] output_buffer__data_out;
    wire output_buffer__ready_in;
    Buffer #(
        OUTPUT_BITS
,       'h2
    ) output_buffer (
        .clk(clk)
,       .reset(reset)
,       .valid_in(output_buffer__valid_in)
,       .data_in(output_buffer__data_in)
,       .ready_out(output_buffer__ready_out)
,       .valid_out(output_buffer__valid_out)
,       .data_out(output_buffer__data_out)
,       .ready_in(output_buffer__ready_in)
    );

    // tmp variables
    logic valid_delayed_tmp;
    logic[DATA_BYTES-1:0][8-1:0] data_delayed_tmp;
    logic[64-1:0] addr_delayed_tmp;
    logic[8-1:0] nbytes_delayed_tmp;
    logic hole_delayed_tmp;
    logic[DATA_BITS-1:0] buffer1_precalc_tmp;
    logic[DATA_BITS-1:0] buffer2_precalc_tmp;
    logic buffer1_valid_tmp;
    logic[DATA_BITS-1:0] buffer1_tmp;
    logic[AV_BYTES-1:0] buffer1_byteenable_tmp;
    logic[64-1:0] buffer1_address_tmp;
    logic buffer2_valid_tmp;
    logic[DATA_BITS-1:0] buffer2_tmp;
    logic[AV_BYTES-1:0] buffer2_byteenable_tmp;
    logic[64-1:0] buffer2_address_tmp;
    logic[8-1:0] buffer2_pos_tmp;


    generate  // _assign
        assign output_buffer__valid_in = buffer1_valid;
        assign output_buffer__data_in = {buffer1_byteenable, buffer1_address, buffer1};
        assign output_buffer__ready_in = !avmm_waitrequest_in;
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[63:0] i;
        logic[63:0] addr_lo;
        logic[63:0] addr_hi;
        logic[63:0] addr_sub;
        logic[63:0] in_addr_sub;
        logic delayed_glue;
        logic buffer1_has_bytes;
        logic tail_valid;
        logic[63:0] tail_address;
        logic[63:0] tail_pos;
        logic[512-1:0] wider_bus;
        addr_lo='h0;
        addr_hi='h0;
        addr_sub='h0;
        in_addr_sub='h0;
        delayed_glue=0;
        buffer1_has_bytes=0;
        tail_valid=0;
        tail_address='h0;
        tail_pos='h0;
        if (output_buffer__ready_out) begin
            if (!hole_delayed) begin
                valid_delayed_tmp = unsigned'(1'(valid_in));
                data_delayed_tmp = data_in;
                addr_delayed_tmp = unsigned'(64'(addr_in));
                nbytes_delayed_tmp = unsigned'(8'(nbytes_in));
                wider_bus = data_in;
                in_addr_sub=addr_in & ((((64'h1 <<< AVALON_BITS)) - 'h1));
                buffer1_precalc_tmp = wider_bus << (in_addr_sub*'h8);
                if (in_addr_sub != 'h0) begin
                    buffer2_precalc_tmp = wider_bus >> (AVALON_WIDTH - (in_addr_sub*'h8));
                end
                else begin
                    buffer2_precalc_tmp = 'h0;
                end
            end
            else begin
                if (hole_delayed) begin
                    valid_delayed_tmp = valid_delayed;
                    data_delayed_tmp = data_delayed;
                    addr_delayed_tmp = addr_delayed;
                    nbytes_delayed_tmp = nbytes_delayed;
                    buffer1_precalc_tmp = buffer1_precalc;
                    buffer2_precalc_tmp = buffer2_precalc;
                    hole_delayed_tmp = unsigned'(1'(0));
                end
                else begin
                    valid_delayed_tmp = unsigned'(1'(0));
                    hole_delayed_tmp = unsigned'(1'(0));
                end
            end
            addr_lo=addr_delayed & ~((((64'h1 <<< AVALON_BITS)) - 'h1));
            addr_hi=addr_lo + ((64'h1 <<< AVALON_BITS));
            addr_sub=addr_delayed & ((((64'h1 <<< AVALON_BITS)) - 'h1));
            delayed_glue=!hole_delayed && (((((addr_lo == buffer2_address) && (addr_sub == buffer2_pos))) || !buffer2_valid));
            buffer1_tmp = 'h0;
            buffer1_byteenable_tmp = 'h0;
            buffer1_has_bytes=0;
            if (buffer2_valid) begin
                buffer1_tmp = buffer2;
                buffer1_has_bytes=buffer2_pos != 'h0;
                for (i='h0;i < AV_BYTES;i=i+1) begin
                    if (i < buffer2_pos) begin
                        buffer1_byteenable_tmp[i] = buffer2_byteenable[i];
                    end
                end
            end
            buffer1_address_tmp = buffer2_address;
            buffer1_valid_tmp = buffer2_valid;
            buffer2_tmp = 'h0;
            buffer2_valid_tmp = unsigned'(1'(0));
            buffer2_byteenable_tmp = 'h0;
            if (valid_delayed) begin
                if (delayed_glue) begin
                    buffer1_tmp |= buffer1_precalc;
                end
                buffer2_tmp = buffer2_precalc;
            end
            for (i='h0;i < AV_BYTES;i=i+1) begin
                if (valid_delayed && (i < nbytes_delayed)) begin
                    if ((addr_sub + i) < AV_BYTES) begin
                        if (delayed_glue) begin
                            buffer1_byteenable_tmp[addr_sub + i] = 'h1;
                            buffer1_valid_tmp = unsigned'(1'(1));
                            buffer1_has_bytes=1;
                        end
                    end
                    else begin
                        buffer2_byteenable_tmp[(addr_sub + i) - AV_BYTES] = 'h1;
                        buffer2_valid_tmp = unsigned'(1'(1));
                    end
                end
            end
            if (hole_delayed) begin
                buffer2_valid_tmp = unsigned'(1'(0));
            end
            buffer2_address_tmp = buffer2_address;
            buffer2_pos_tmp = buffer2_pos;
            if (valid_delayed) begin
                buffer1_address_tmp = unsigned'(64'(addr_lo));
                buffer2_address_tmp = unsigned'(64'(addr_hi));
                buffer2_pos_tmp = unsigned'(8'((addr_sub + nbytes_delayed) - (('h1 <<< AVALON_BITS))));
            end
            if (buffer2_valid) begin
                buffer1_address_tmp = buffer2_address;
            end
            if (!buffer1_has_bytes) begin
                buffer1_valid_tmp = unsigned'(1'(0));
            end
            tail_valid=(valid_delayed && !hole_delayed) && (((addr_sub + nbytes_delayed) > AV_BYTES));
            tail_address=addr_hi;
            tail_pos='h0;
            if (tail_valid) begin
                tail_pos=(addr_sub + nbytes_delayed) - AV_BYTES;
            end
            if (!hole_delayed) begin
                in_addr_sub=addr_in & ((((64'h1 <<< AVALON_BITS)) - 'h1));
                hole_delayed_tmp = unsigned'(1'((valid_in && tail_valid) && (((((addr_in & ~((((64'h1 <<< AVALON_BITS)) - 'h1)))) != tail_address) || (in_addr_sub != tail_pos)))));
            end
        end
        if (reset) begin
            valid_delayed_tmp = '0;
            data_delayed_tmp = '0;
            addr_delayed_tmp = '0;
            nbytes_delayed_tmp = '0;
            hole_delayed_tmp = '0;
            buffer1_precalc_tmp = '0;
            buffer2_precalc_tmp = '0;
            buffer1_valid_tmp = '0;
            buffer1_tmp = '0;
            buffer1_byteenable_tmp = '0;
            buffer1_address_tmp = '0;
            buffer2_valid_tmp = '0;
            buffer2_tmp = '0;
            buffer2_byteenable_tmp = '0;
            buffer2_address_tmp = '0;
            buffer2_pos_tmp = '0;
        end
    end
    endtask

    always @(posedge clk) begin
        valid_delayed_tmp = valid_delayed;
        data_delayed_tmp = data_delayed;
        addr_delayed_tmp = addr_delayed;
        nbytes_delayed_tmp = nbytes_delayed;
        hole_delayed_tmp = hole_delayed;
        buffer1_precalc_tmp = buffer1_precalc;
        buffer2_precalc_tmp = buffer2_precalc;
        buffer1_valid_tmp = buffer1_valid;
        buffer1_tmp = buffer1;
        buffer1_byteenable_tmp = buffer1_byteenable;
        buffer1_address_tmp = buffer1_address;
        buffer2_valid_tmp = buffer2_valid;
        buffer2_tmp = buffer2;
        buffer2_byteenable_tmp = buffer2_byteenable;
        buffer2_address_tmp = buffer2_address;
        buffer2_pos_tmp = buffer2_pos;

        _work(reset);

        valid_delayed <= valid_delayed_tmp;
        data_delayed <= data_delayed_tmp;
        addr_delayed <= addr_delayed_tmp;
        nbytes_delayed <= nbytes_delayed_tmp;
        hole_delayed <= hole_delayed_tmp;
        buffer1_precalc <= buffer1_precalc_tmp;
        buffer2_precalc <= buffer2_precalc_tmp;
        buffer1_valid <= buffer1_valid_tmp;
        buffer1 <= buffer1_tmp;
        buffer1_byteenable <= buffer1_byteenable_tmp;
        buffer1_address <= buffer1_address_tmp;
        buffer2_valid <= buffer2_valid_tmp;
        buffer2 <= buffer2_tmp;
        buffer2_byteenable <= buffer2_byteenable_tmp;
        buffer2_address <= buffer2_address_tmp;
        buffer2_pos <= buffer2_pos_tmp;
    end

    assign wait_out = !output_buffer__ready_out || hole_delayed;

    assign avmm_address_out = unsigned'(64'((output_buffer__data_out >> DATA_BITS)));

    assign avmm_writedata_out = output_buffer__data_out;

    assign avmm_byteenable_out = (output_buffer__data_out >> (DATA_BITS + 'h40));

    assign avmm_write_out = output_buffer__valid_out;

    assign avmm_read_out = 0;


endmodule

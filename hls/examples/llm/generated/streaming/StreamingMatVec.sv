`default_nettype none

import Predef_pkg::*;


module StreamingMatVec (
    input wire clk
,   input wire reset
,   output wire weights_out__valid_out
,   output wire[unsigned'(64'(32))-1:0] weights_out__addr_out
,   input wire weights_out__ready_in
,   input wire weights_out__valid_in
,   input wire[unsigned'(64'(512))-1:0] weights_out__data_in
,   input wire weights_out__error_in
,   output wire weights_out__ready_out
,   input wire load_in
,   input wire[31:0] load_address_in
,   input wire[63:0] load_data_in
,   input wire command_valid_in
,   output wire command_ready_out
,   input wire[31:0] rows_in
,   input wire[31:0] base_in
,   input wire ready_in
,   output wire valid_out
,   output wire[63:0] data_out
,   output wire[31:0] row_out
,   output wire fault_out
,   output wire loading_out
,   output wire computing_out
,   output wire product_accepted_out
);
    localparam  DEPTH = 'h8;
    localparam  COUNT_BITS = 'h4;


    // regs and combs
    reg[64-1:0] activations[8];
    reg active;
    reg fault;
    reg operand_valid;
    reg sum_valid;
    reg[8-1:0] row_count;
    reg[8-1:0] output_row;
    reg[4-1:0] issue_column;
    reg[4-1:0] receive_column;
    reg[64-1:0] operand_weight;
    reg[64-1:0] operand_activation;
    reg[128-1:0] sum;
    reg[128-1:0] carry;
    reg[128-1:0] final_sum;
    reg[128-1:0] final_carry;

    // members
    wire reader__memory_out__valid_out;
    wire[unsigned'(64'(32))-1:0] reader__memory_out__addr_out;
    wire reader__memory_out__ready_in;
    wire reader__memory_out__valid_in;
    wire[unsigned'(64'(512))-1:0] reader__memory_out__data_in;
    wire reader__memory_out__error_in;
    wire reader__memory_out__ready_out;
    wire reader__command_in;
    wire[31:0] reader__base_in;
    wire[31:0] reader__words_in;
    wire reader__ready_in;
    wire reader__valid_out;
    wire[63:0] reader__data_out;
    wire reader__fault_out;
    wire reader__pending_out;
    WeightReadStream      reader (
        .clk(clk)
,       .reset(reset)
,       .memory_out__valid_out(reader__memory_out__valid_out)
,       .memory_out__addr_out(reader__memory_out__addr_out)
,       .memory_out__ready_in(reader__memory_out__ready_in)
,       .memory_out__valid_in(reader__memory_out__valid_in)
,       .memory_out__data_in(reader__memory_out__data_in)
,       .memory_out__error_in(reader__memory_out__error_in)
,       .memory_out__ready_out(reader__memory_out__ready_out)
,       .command_in(reader__command_in)
,       .base_in(reader__base_in)
,       .words_in(reader__words_in)
,       .ready_in(reader__ready_in)
,       .valid_out(reader__valid_out)
,       .data_out(reader__data_out)
,       .fault_out(reader__fault_out)
,       .pending_out(reader__pending_out)
    );
    wire product__command_valid_in;
    wire[63:0] product__operation_in;
    wire[63:0] product__index_in;
    wire[63:0] product__value_in;
    wire product__command_ready_out;
    wire product__response_ready_in;
    wire product__response_valid_out;
    wire[128-1:0] product__result_out;
    wire[31:0] product__fault_out;
    cpphdl_hls_ClockedPipelineinteger_llm_Product_logic63_0_logic128m1_0_P4      product (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(product__command_valid_in)
,       .operation_in(product__operation_in)
,       .index_in(product__index_in)
,       .value_in(product__value_in)
,       .command_ready_out(product__command_ready_out)
,       .response_ready_in(product__response_ready_in)
,       .response_valid_out(product__response_valid_out)
,       .result_out(product__result_out)
,       .fault_out(product__fault_out)
    );
    wire reduce__command_valid_in;
    wire[128-1:0] reduce__operation_in;
    wire[128-1:0] reduce__index_in;
    wire[128-1:0] reduce__value_in;
    wire reduce__command_ready_out;
    wire reduce__response_ready_in;
    wire reduce__response_valid_out;
    wire[128-1:0] reduce__result_out;
    wire[31:0] reduce__fault_out;
    cpphdl_hls_ClockedPipelineinteger_llm_WideAdd_logic128m1_0_logic128m1_0_P4      reduce (
        .clk(clk)
,       .reset(reset)
,       .command_valid_in(reduce__command_valid_in)
,       .operation_in(reduce__operation_in)
,       .index_in(reduce__index_in)
,       .value_in(reduce__value_in)
,       .command_ready_out(reduce__command_ready_out)
,       .response_ready_in(reduce__response_ready_in)
,       .response_valid_out(reduce__response_valid_out)
,       .result_out(reduce__result_out)
,       .fault_out(reduce__fault_out)
    );

    // tmp variables
    logic active_tmp;
    logic fault_tmp;
    logic operand_valid_tmp;
    logic sum_valid_tmp;
    logic[8-1:0] row_count_tmp;
    logic[8-1:0] output_row_tmp;
    logic[4-1:0] issue_column_tmp;
    logic[4-1:0] receive_column_tmp;
    logic[64-1:0] operand_weight_tmp;
    logic[64-1:0] operand_activation_tmp;
    logic[128-1:0] sum_tmp;
    logic[128-1:0] carry_tmp;
    logic[128-1:0] final_sum_tmp;
    logic[128-1:0] final_carry_tmp;


    function logic[128-1:0] wide (input logic[128-1:0] v);
        return ((128'(unsigned'(64'((v >> 'h40)))) <<< 'h40)) | 128'(unsigned'(64'(v)));
    endfunction

    generate  // _assign
        assign weights_out__valid_out=reader__memory_out__valid_out;
        assign weights_out__addr_out=reader__memory_out__addr_out;
        assign reader__memory_out__ready_in=weights_out__ready_in;
        assign reader__memory_out__valid_in=weights_out__valid_in;
        assign reader__memory_out__data_in=weights_out__data_in;
        assign reader__memory_out__error_in=weights_out__error_in;
        assign weights_out__ready_out=reader__memory_out__ready_out;
        assign reader__command_in = ((((command_valid_in && command_ready_out) && (rows_in > 32'h0)) && rows_in<=32'hFF) != '0);
        assign reader__base_in = base_in;
        assign reader__words_in = rows_in*DEPTH;
        assign reader__ready_in = (((((active) != '0) && !fault_out) && ((!((operand_valid) != '0) || product__command_ready_out))) != '0);
        assign product__command_valid_in = ((((operand_valid) != '0) && !fault_out) != '0);
        assign product__operation_in = unsigned'(64'(operand_weight));
        assign product__index_in = unsigned'(64'(operand_activation));
        assign product__value_in = 64'h0;
        assign product__response_ready_in = ((!fault_out && ((!((sum_valid) != '0) || reduce__command_ready_out))) != '0);
        assign reduce__command_valid_in = ((((sum_valid) != '0) && !fault_out) != '0);
        assign reduce__operation_in = wide(unsigned'(128'(final_sum)));
        assign reduce__index_in = wide(unsigned'(128'(final_carry)));
        assign reduce__value_in = 128'h0;
        assign reduce__response_ready_in = ((ready_in) != '0);
        assign command_ready_out = (((!((active) != '0) && !fault_out) && !load_in) != '0);
        assign valid_out = ((reduce__response_valid_out) != '0);
        assign data_out = unsigned'(64'(reduce__result_out >>> 32'sh30));
        assign row_out = unsigned'(32'(output_row));
        assign fault_out = ((((fault) != '0) || reader__fault_out) != '0);
        assign loading_out = ((reader__pending_out) != '0);
        assign computing_out = ((((((operand_valid) != '0) || product__response_valid_out) || ((sum_valid) != '0)) || reduce__response_valid_out) != '0);
        assign product_accepted_out = ((product__command_valid_in && product__command_ready_out) != '0);
    endgenerate

    task _work (input logic reset);
    begin: _work
        logic[128-1:0] raw;
        logic[128-1:0] term;
        logic[128-1:0] next_sum;
        logic[128-1:0] next_carry;
        if (reset) begin
            active_tmp = '0;
            fault_tmp = '0;
            operand_valid_tmp = '0;
            sum_valid_tmp = '0;
            row_count_tmp = '0;
            output_row_tmp = '0;
            issue_column_tmp = '0;
            receive_column_tmp = '0;
            operand_weight_tmp = '0;
            operand_activation_tmp = '0;
            sum_tmp = '0;
            carry_tmp = '0;
            final_sum_tmp = '0;
            final_carry_tmp = '0;
        end
        else begin
            if (load_in && !((active) != '0)) begin
                if (load_address_in < unsigned'(32'(DEPTH))) begin
                    activations[unsigned'(64'(load_address_in))] <= load_data_in;
                end
                else begin
                    fault_tmp = 1;
                end
            end
            if (command_valid_in && command_ready_out) begin
                if (!((rows_in) != '0) || (rows_in > 32'hFF)) begin
                    fault_tmp = 1;
                end
                else begin
                    active_tmp = 1;
                    row_count_tmp = rows_in;
                    output_row_tmp = 8'h0;
                end
            end
            if (((operand_valid) != '0) && product__command_ready_out) begin
                operand_valid_tmp = 0;
            end
            if (reader__valid_out && reader__ready_in) begin
                operand_weight_tmp = reader__data_out;
                operand_activation_tmp = activations[64'(unsigned'(32'(issue_column)))];
                operand_valid_tmp = 1;
                issue_column_tmp = unsigned'(32'((issue_column == 64'(32'((DEPTH - 32'h1)))) ? ('h0) : (unsigned'(32'(issue_column)) + 'h1)));
            end
            if (((sum_valid) != '0) && reduce__command_ready_out) begin
                sum_valid_tmp = 0;
            end
            if (product__response_valid_out && product__response_ready_in) begin
                raw=product__result_out;
                term = 64'(raw >>> 32'sh40);
                term = (term << 'h40) | 64'(raw);
                next_sum = sum ^ carry ^ term;
                next_carry = ((sum & carry) | (sum & term) | (carry & term)) << 'h1;
                if (receive_column == 64'(32'((DEPTH - 32'h1)))) begin
                    final_sum_tmp = next_sum;
                    final_carry_tmp = next_carry;
                    sum_valid_tmp = 1;
                    sum_tmp = 'h0;
                    carry_tmp = 'h0;
                    receive_column_tmp = 64'h0;
                end
                else begin
                    sum_tmp = next_sum;
                    carry_tmp = next_carry;
                    receive_column_tmp = 32'(unsigned'(32'(receive_column)) + 'h1);
                end
            end
            if (valid_out && ready_in) begin
                output_row_tmp = unsigned'(32'(output_row)) + 'h1;
                if (64'(32'((unsigned'(32'(output_row)) + 'h1))) == row_count) begin
                    active_tmp = 0;
                end
            end
        end
    end
    endtask

    always @(posedge clk) begin
        active_tmp = active;
        fault_tmp = fault;
        operand_valid_tmp = operand_valid;
        sum_valid_tmp = sum_valid;
        row_count_tmp = row_count;
        output_row_tmp = output_row;
        issue_column_tmp = issue_column;
        receive_column_tmp = receive_column;
        operand_weight_tmp = operand_weight;
        operand_activation_tmp = operand_activation;
        sum_tmp = sum;
        carry_tmp = carry;
        final_sum_tmp = final_sum;
        final_carry_tmp = final_carry;

        _work(reset);

        active <= active_tmp;
        fault <= fault_tmp;
        operand_valid <= operand_valid_tmp;
        sum_valid <= sum_valid_tmp;
        row_count <= row_count_tmp;
        output_row <= output_row_tmp;
        issue_column <= issue_column_tmp;
        receive_column <= receive_column_tmp;
        operand_weight <= operand_weight_tmp;
        operand_activation <= operand_activation_tmp;
        sum <= sum_tmp;
        carry <= carry_tmp;
        final_sum <= final_sum_tmp;
        final_carry <= final_carry_tmp;
    end


endmodule

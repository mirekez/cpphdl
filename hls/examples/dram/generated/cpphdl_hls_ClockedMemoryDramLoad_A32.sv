// AST clocked instantiation. No compiler IR or external compiler invocation.
// Shared schedule: 6 blocks, 6 function bodies
// Shared lowering: 6 bodies, 6 function bodies
// Whole same-clock calls: 0
// Source values: 14 direct values, 6 live across clocks
// Memory effects: 0 redundant loads reused within clock regions
// instantiated: DramLoad::command at /home/me/cpphdl/hls/examples/dram/DramStream.cpp:5:14
module cpphdl_hls_ClockedMemoryDramLoad_A32(
    input wire clk, reset,
    input wire command_valid_in,
    input wire [31:0] operation_in, index_in, value_in,
    output wire command_ready_out,
    input wire response_ready_in,
    output wire response_valid_out,
    output wire [63:0] result_out,
    output wire [31:0] fault_out
  , output wire memory_out__valid_out, memory_out__write_out
  , output wire [31:0] memory_out__addr_out
  , output wire [7:0] memory_out__size_out
  , output wire [63:0] memory_out__data_out
  , input wire memory_out__ready_in, memory_out__valid_in
  , input wire [63:0] memory_out__data_in
  , input wire memory_out__error_in
  , output wire memory_out__ready_out
);
  localparam int ADDR_BITS = 32;
  localparam int HEAP_BYTES = 4096;
  localparam int CALL_RESULT_BITS = 224;
  localparam int MEM_BYTES = 16;
  localparam int INDEX_BITS = $clog2(MEM_BYTES);
  localparam int STATE_BITS = $clog2(6);
  typedef logic [MEM_BYTES-1:0][7:0] storage_t;
  localparam int BANK_BYTES = MEM_BYTES / 8;
  localparam int BANK_INDEX_BITS = $clog2(BANK_BYTES);
  typedef logic [BANK_BYTES-1:0][7:0] bank_t;
  logic [5:0] active;
  logic [31:0] phase, phase_reg, fault, fault_reg;
  logic [63:0] result, result_reg;
  logic [ADDR_BITS-1:0] heap_next, heap_next_reg;
  logic pending, pending_reg, booting, booting_reg;
  bank_t storage_lane0, storage_lane0_reg;
  bank_t storage_lane1, storage_lane1_reg;
  bank_t storage_lane2, storage_lane2_reg;
  bank_t storage_lane3, storage_lane3_reg;
  bank_t storage_lane4, storage_lane4_reg;
  bank_t storage_lane5, storage_lane5_reg;
  bank_t storage_lane6, storage_lane6_reg;
  bank_t storage_lane7, storage_lane7_reg;
  logic [31:0] hls_accepted_operation_in;
  wire [31:0] hls_command_operation_in = hls_accepted_operation_in;
  logic [31:0] hls_accepted_index_in;
  wire [31:0] hls_command_index_in = hls_accepted_index_in;
  logic [31:0] hls_accepted_value_in;
  wire [31:0] hls_command_value_in = hls_accepted_value_in;
  logic [31:0] external_address, external_address_reg;
  logic [63:0] external_write_data, external_data_reg, external_read_data;
  logic [7:0] external_size, external_size_reg;
  logic external_read, external_write, external_write_reg, external_busy, external_sent;
  wire external_accept = memory_out__valid_out && memory_out__ready_in;
  wire external_complete = memory_out__ready_out && memory_out__valid_in;
  wire external_bad_address = (external_address_reg[0] && external_size_reg > 1) ||
    (external_address_reg[1] && external_size_reg > 2) || (external_address_reg[2] && external_size_reg > 4);
  assign memory_out__valid_out = external_busy && !external_sent && !external_bad_address && !reset;
  assign memory_out__write_out = external_write_reg;
  assign memory_out__addr_out = external_address_reg;
  assign memory_out__size_out = external_size_reg;
  assign memory_out__data_out = external_data_reg;
  assign memory_out__ready_out = external_busy && (external_sent || external_accept) && !reset;
  logic [63:0] scratch__external_value_17;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__base_0;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__count_3;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__i_14;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__i_4;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__i_7;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__index_8;
  logic [63:0] scratch__hls_DramLoad__command__call_0_depth_1__return_value_16;
  logic [63:0] scratch__hls_DramLoad__command__call_0_depth_1__sum_12;
  logic [63:0] scratch__hls_DramLoad__command__call_0_depth_1__sum_15;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_1;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_10;
  logic [63:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_11;
  logic [63:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_13;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_2;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_5;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__temporary_9;
  logic [31:0] scratch__hls_DramLoad__command__call_0_depth_1__words_6;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__base_2;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__count_4;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__i_8;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__index_3;
  logic [63:0] values__hls_DramLoad__command__call_0_depth_1__return_value_1;
  logic [63:0] values__hls_DramLoad__command__call_0_depth_1__sum_7;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__temporary_10;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__temporary_11;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__temporary_12;
  logic [63:0] values__hls_DramLoad__command__call_0_depth_1__temporary_13;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__temporary_6;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__temporary_9;
  logic [31:0] values__hls_DramLoad__command__call_0_depth_1__words_5;
  logic [7:0] values__hls_object__object_0;
  typedef struct packed {
    logic [31:0] hls_DramLoad__command__call_0_depth_1__count_4;
    logic [31:0] hls_DramLoad__command__call_0_depth_1__i_8;
    logic [31:0] hls_DramLoad__command__call_0_depth_1__index_3;
    logic [63:0] hls_DramLoad__command__call_0_depth_1__sum_7;
    logic [31:0] hls_DramLoad__command__call_0_depth_1__words_5;
    logic [7:0] hls_object__object_0;
    logic unused_bit;
  } clocked_values_t;
  clocked_values_t values_reg;
  function static logic hls_storage_address_valid(input logic [ADDR_BITS-1:0] address, input int unsigned count);
    hls_storage_address_valid = count <= MEM_BYTES && address >= ADDR_BITS'(16) && address <= ADDR_BITS'(MEM_BYTES) - ADDR_BITS'(count);
  endfunction
  //
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [32+1+8-1:0] hls_object__shared_0(
    input logic [32-1:0] phase,
    input logic [32-1:0] fault,
    input logic [1-1:0] booting,
    input logic [8-1:0] p_object_0,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      if (fault == 0) begin p_object_0 = 8'(8'('0)); end
      if (fault == 0) begin booting = 0; phase = 0; end
      hls_object__shared_0 = {phase, booting, p_object_0};
    end
  endfunction
  //
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [32+32+32+32+64+32-1:0] hls_object__shared_1(
    input logic [32-1:0] operation_in,
    input logic [32-1:0] index_in,
    input logic [32-1:0] value_in,
    input logic [32-1:0] phase,
    input logic [32-1:0] fault,
    input logic [32-1:0] next_block,
    input logic [32-1:0] p_base_0,
    input logic [32-1:0] p_index_1,
    input logic [32-1:0] p_count_2,
    input logic [32-1:0] p_base_3,
    input logic [32-1:0] p_temporary_4,
    input logic [32-1:0] p_temporary_5,
    input logic [32-1:0] p_words_6,
    input logic [64-1:0] p_sum_7,
    input logic [32-1:0] p_temporary_8,
    input logic [32-1:0] p_temporary_9,
    input logic [32-1:0] p_i_10,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      p_base_0 = 32'(32'(operation_in));
      if (fault == 0) begin p_index_1 = 32'(32'(index_in)); end
      if (fault == 0) begin p_count_2 = 32'(32'(value_in)); end
      p_base_3 = 32'(32'(p_base_0));
      p_temporary_4 = 32'(32'(p_base_3));
      p_temporary_5 = 32'(32'(p_temporary_4));
      if (fault == 0) begin p_words_6 = 32'(32'(32'(p_temporary_5))); end
      if (fault == 0) begin p_sum_7 = 64'(64'(64'h0)); end
      p_temporary_8 = 32'(32'(32'h0));
      p_temporary_9 = 32'(32'(p_temporary_8));
      if (fault == 0) begin p_i_10 = 32'(32'(p_temporary_9)); end
      if (fault == 0) begin phase = next_block + 32'd1; // loop boundary
 end
      hls_object__shared_1 = {phase, p_index_1, p_count_2, p_words_6, p_sum_7, p_i_10};
    end
  endfunction
  // /home/me/cpphdl/hls/examples/dram/DramStream.cpp:9:9
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [6+32+32-1:0] hls_DramLoad__command__shared_2(
    input logic [32-1:0] fault,
    input logic [6-1:0] active,
    input logic [32-1:0] next_block,
    input logic [32-1:0] else_block,
    input logic [32-1:0] p_count_0,
    input logic [32-1:0] p_count_1,
    input logic [32-1:0] p_temporary_2,
    input logic [32-1:0] p_i_3,
    input logic [32-1:0] p_i_4,
    input logic [32-1:0] p_temporary_5,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      p_count_0 = 32'(32'(p_count_1));
      p_temporary_2 = 32'(32'(p_count_0));
      p_i_3 = 32'(32'(p_i_4));
      p_temporary_5 = 32'(32'(p_temporary_2));
      if (fault == 0) begin if (((p_i_3 < p_temporary_5) != 0)) active[STATE_BITS'(next_block)] = 1; else active[STATE_BITS'(else_block)] = 1; end
      hls_DramLoad__command__shared_2 = {active, p_count_1, p_i_4};
    end
  endfunction
  // /home/me/cpphdl/hls/examples/dram/DramStream.cpp:9:37
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [32+8+1+32+32+32+32-1:0] hls_DramLoad__command__shared_3(
    input logic [32-1:0] external_address,
    input logic [8-1:0] external_size,
    input logic [1-1:0] external_read,
    input logic [32-1:0] phase,
    input logic [32-1:0] fault,
    input logic [32-1:0] next_block,
    input logic [32-1:0] p_words_0,
    input logic [32-1:0] p_words_1,
    input logic [32-1:0] p_temporary_2,
    input logic [32-1:0] p_i_3,
    input logic [32-1:0] p_i_4,
    input logic [32-1:0] p_temporary_5,
    input logic [32-1:0] p_index_6,
    input logic [32-1:0] p_index_7,
    input logic [32-1:0] p_temporary_8,
    input logic [32-1:0] p_temporary_9,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      p_words_0 = 32'(32'(p_words_1));
      p_temporary_2 = 32'(32'(p_words_0));
      p_i_3 = 32'(32'(p_i_4));
      p_temporary_5 = 32'(32'(p_i_3));
      p_index_6 = 32'(32'(p_index_7));
      p_temporary_8 = 32'(32'(p_temporary_5));
      p_temporary_9 = 32'(32'(p_temporary_2));
      if (fault == 0) begin external_address = 32'(32'((p_temporary_9 + 32'(32'((p_index_6 + p_temporary_8))) * 32'd8))); end
      if (fault == 0) begin external_size = 8; end
      if (fault == 0) begin external_read = 1; end
      if (fault == 0) begin phase = next_block + 32'd1; // memory port boundary
 end
      hls_DramLoad__command__shared_3 = {external_address, external_size, external_read, phase, p_words_1, p_i_4, p_index_7};
    end
  endfunction
  // /home/me/cpphdl/hls/examples/dram/DramStream.cpp:9:9
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [32+64+1+64-1:0] hls_DramLoad__command__shared_4(
    input logic [32-1:0] phase,
    input logic [32-1:0] fault,
    input logic [64-1:0] result,
    input logic [1-1:0] pending,
    input logic [64-1:0] p_sum_0,
    input logic [64-1:0] p_sum_1,
    input logic [64-1:0] p_return_value_2,
    input logic [64-1:0] p_return_value_3,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      p_sum_0 = 64'(64'(p_sum_1));
      p_return_value_2 = 64'(64'(p_sum_0));
      p_return_value_3 = 64'(64'(p_return_value_2));
      if (fault == 0) begin result = p_return_value_3; pending = 1; phase = 0; end
      hls_DramLoad__command__shared_4 = {phase, result, pending, p_sum_1};
    end
  endfunction
  // /home/me/cpphdl/hls/examples/dram/DramStream.cpp:9:37
  // Reused by 1 scheduled blocks
  // Clocked continuation
  function static logic [32+64+32-1:0] hls_DramLoad__command__shared_5(
    input logic [64-1:0] external_read_data,
    input logic [32-1:0] phase,
    input logic [32-1:0] fault,
    input logic [32-1:0] next_block,
    input logic [64-1:0] p_external_value_0,
    input logic [64-1:0] p_temporary_1,
    input logic [64-1:0] p_temporary_2,
    input logic [64-1:0] p_sum_3,
    input logic [64-1:0] p_sum_4,
    input logic [64-1:0] p_temporary_5,
    input logic [32-1:0] p_i_6,
    input logic [32-1:0] p_i_7,
    input logic [CALL_RESULT_BITS-1:0] hls_call_result);
    begin
      p_external_value_0 = 64'(external_read_data);
      p_temporary_1 = 64'(p_external_value_0);
      p_temporary_2 = 64'(64'(p_temporary_1));
      p_sum_3 = 64'(64'(p_sum_4));
      p_temporary_5 = 64'(64'(p_temporary_2));
      if (fault == 0) begin p_sum_4 = 64'(64'((p_sum_3 + p_temporary_5))); end
      p_i_6 = 32'(32'(p_i_7));
      if (fault == 0) begin p_i_7 = 32'(32'((p_i_6 + 1))); end
      if (fault == 0) begin phase = next_block + 32'd1; // loop boundary
 end
      hls_DramLoad__command__shared_5 = {phase, p_sum_4, p_i_7};
    end
  endfunction
  assign command_ready_out = !booting_reg && !pending_reg && phase_reg == 0 && fault_reg == 0;
  assign response_valid_out = pending_reg;
  assign result_out = result_reg;
  assign fault_out = fault_reg;
  always_comb begin
    heap_next = heap_next_reg;
    phase = phase_reg; fault = fault_reg; result = result_reg;
    pending = pending_reg; booting = booting_reg; active = '0;
    storage_lane0 = storage_lane0_reg;
    storage_lane1 = storage_lane1_reg;
    storage_lane2 = storage_lane2_reg;
    storage_lane3 = storage_lane3_reg;
    storage_lane4 = storage_lane4_reg;
    storage_lane5 = storage_lane5_reg;
    storage_lane6 = storage_lane6_reg;
    storage_lane7 = storage_lane7_reg;
    external_address = 0; external_write_data = 0; external_size = 0; external_read = 0; external_write = 0;
    scratch__external_value_17 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__base_0 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__count_3 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__i_14 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__i_4 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__i_7 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__index_8 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__return_value_16 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__sum_12 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__sum_15 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_1 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_10 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_11 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_13 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_2 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_5 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__temporary_9 = '0;
    scratch__hls_DramLoad__command__call_0_depth_1__words_6 = '0;
    values__hls_DramLoad__command__call_0_depth_1__base_2 = '0;
    values__hls_DramLoad__command__call_0_depth_1__count_4 = '0;
    values__hls_DramLoad__command__call_0_depth_1__i_8 = '0;
    values__hls_DramLoad__command__call_0_depth_1__index_3 = '0;
    values__hls_DramLoad__command__call_0_depth_1__return_value_1 = '0;
    values__hls_DramLoad__command__call_0_depth_1__sum_7 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_10 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_11 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_12 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_13 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_6 = '0;
    values__hls_DramLoad__command__call_0_depth_1__temporary_9 = '0;
    values__hls_DramLoad__command__call_0_depth_1__words_5 = '0;
    values__hls_object__object_0 = '0;
    values__hls_DramLoad__command__call_0_depth_1__count_4 = values_reg.hls_DramLoad__command__call_0_depth_1__count_4;
    values__hls_DramLoad__command__call_0_depth_1__i_8 = values_reg.hls_DramLoad__command__call_0_depth_1__i_8;
    values__hls_DramLoad__command__call_0_depth_1__index_3 = values_reg.hls_DramLoad__command__call_0_depth_1__index_3;
    values__hls_DramLoad__command__call_0_depth_1__sum_7 = values_reg.hls_DramLoad__command__call_0_depth_1__sum_7;
    values__hls_DramLoad__command__call_0_depth_1__words_5 = values_reg.hls_DramLoad__command__call_0_depth_1__words_5;
    values__hls_object__object_0 = values_reg.hls_object__object_0;
    if (reset) begin
      phase = 1; fault = 0; pending = 0; result = 0; booting = 1; heap_next = 16;
    end else if (fault == 0 && !external_busy) begin
      if (pending && response_ready_in) pending = 0;
      if (command_valid_in && command_ready_out) phase = 2;
      else case (phase_reg)
        1: active[0] = 1;
        2: active[1] = 1;
        3: active[2] = 1;
        6: active[5] = 1;
        0: begin end
        default: fault = 4;
      endcase
    end else if (response_ready_in) pending = 0;
  end
  logic [31:0] hls_step_0__phase;
  logic [0:0] hls_step_0__booting;
  logic [7:0] hls_step_0__values__hls_object__object_0;
  always_comb begin : hls_step_0
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_0__phase = phase;
    hls_step_0__booting = booting;
    hls_step_0__values__hls_object__object_0 = values__hls_object__object_0;
    if (active[0] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_object__shared_0(hls_step_0__phase, fault, hls_step_0__booting, hls_step_0__values__hls_object__object_0, '0)); {hls_step_0__phase, hls_step_0__booting, hls_step_0__values__hls_object__object_0} = $bits({hls_step_0__phase, hls_step_0__booting, hls_step_0__values__hls_object__object_0})'(hls_call_result); end
  end
  logic [31:0] hls_step_1__phase;
  logic [31:0] hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3;
  logic [31:0] hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4;
  logic [31:0] hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5;
  logic [63:0] hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7;
  logic [31:0] hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8;
  always_comb begin : hls_step_1
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_1__phase = hls_step_0__phase;
    hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3 = values__hls_DramLoad__command__call_0_depth_1__index_3;
    hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4 = values__hls_DramLoad__command__call_0_depth_1__count_4;
    hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5 = values__hls_DramLoad__command__call_0_depth_1__words_5;
    hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7 = values__hls_DramLoad__command__call_0_depth_1__sum_7;
    hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8 = values__hls_DramLoad__command__call_0_depth_1__i_8;
    if (active[1] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_object__shared_1(hls_command_operation_in, hls_command_index_in, hls_command_value_in, hls_step_1__phase, fault, 2, values__hls_DramLoad__command__call_0_depth_1__base_2, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3,
        hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4, scratch__hls_DramLoad__command__call_0_depth_1__base_0, values__hls_DramLoad__command__call_0_depth_1__temporary_6, scratch__hls_DramLoad__command__call_0_depth_1__temporary_1, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7, values__hls_DramLoad__command__call_0_depth_1__temporary_9, scratch__hls_DramLoad__command__call_0_depth_1__temporary_2,
        hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8, '0)); {hls_step_1__phase, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8} = $bits({hls_step_1__phase, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7, hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8})'(hls_call_result); end
  end
  logic [5:0] hls_step_2__active;
  logic [31:0] hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4;
  logic [31:0] hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8;
  always_comb begin : hls_step_2
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_2__active = active;
    hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4 = hls_step_1__values__hls_DramLoad__command__call_0_depth_1__count_4;
    hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8 = hls_step_1__values__hls_DramLoad__command__call_0_depth_1__i_8;
    if (active[2] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_DramLoad__command__shared_2(fault, hls_step_2__active, 3, 4, scratch__hls_DramLoad__command__call_0_depth_1__count_3, hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4, values__hls_DramLoad__command__call_0_depth_1__temporary_10, scratch__hls_DramLoad__command__call_0_depth_1__i_4,
        hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8, scratch__hls_DramLoad__command__call_0_depth_1__temporary_5, '0)); {hls_step_2__active, hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4, hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8} = $bits({hls_step_2__active, hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4, hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8})'(hls_call_result); end
  end
  logic [31:0] hls_step_3__external_address;
  logic [7:0] hls_step_3__external_size;
  logic [0:0] hls_step_3__external_read;
  logic [31:0] hls_step_3__phase;
  logic [31:0] hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5;
  logic [31:0] hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8;
  logic [31:0] hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3;
  always_comb begin : hls_step_3
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_3__external_address = external_address;
    hls_step_3__external_size = external_size;
    hls_step_3__external_read = external_read;
    hls_step_3__phase = hls_step_1__phase;
    hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5 = hls_step_1__values__hls_DramLoad__command__call_0_depth_1__words_5;
    hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8 = hls_step_2__values__hls_DramLoad__command__call_0_depth_1__i_8;
    hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3 = hls_step_1__values__hls_DramLoad__command__call_0_depth_1__index_3;
    if (hls_step_2__active[3] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_DramLoad__command__shared_3(hls_step_3__external_address, hls_step_3__external_size, hls_step_3__external_read, hls_step_3__phase, fault, 5, scratch__hls_DramLoad__command__call_0_depth_1__words_6, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5,
        values__hls_DramLoad__command__call_0_depth_1__temporary_11, scratch__hls_DramLoad__command__call_0_depth_1__i_7, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8, values__hls_DramLoad__command__call_0_depth_1__temporary_12, scratch__hls_DramLoad__command__call_0_depth_1__index_8, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3, scratch__hls_DramLoad__command__call_0_depth_1__temporary_9, scratch__hls_DramLoad__command__call_0_depth_1__temporary_10, '0)); {hls_step_3__external_address, hls_step_3__external_size, hls_step_3__external_read, hls_step_3__phase, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3} = $bits({hls_step_3__external_address, hls_step_3__external_size, hls_step_3__external_read, hls_step_3__phase, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8, hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3})'(hls_call_result); end
  end
  logic [31:0] hls_step_4__phase;
  logic [63:0] hls_step_4__result;
  logic [0:0] hls_step_4__pending;
  logic [63:0] hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7;
  always_comb begin : hls_step_4
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_4__phase = hls_step_3__phase;
    hls_step_4__result = result;
    hls_step_4__pending = pending;
    hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7 = hls_step_1__values__hls_DramLoad__command__call_0_depth_1__sum_7;
    if (hls_step_2__active[4] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_DramLoad__command__shared_4(hls_step_4__phase, fault, hls_step_4__result, hls_step_4__pending, scratch__hls_DramLoad__command__call_0_depth_1__sum_15, hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7, values__hls_DramLoad__command__call_0_depth_1__return_value_1, scratch__hls_DramLoad__command__call_0_depth_1__return_value_16, '0)); {hls_step_4__phase, hls_step_4__result, hls_step_4__pending, hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7} = $bits({hls_step_4__phase, hls_step_4__result, hls_step_4__pending, hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7})'(hls_call_result); end
  end
  logic [31:0] hls_step_5__phase;
  logic [63:0] hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7;
  logic [31:0] hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8;
  always_comb begin : hls_step_5
    logic [CALL_RESULT_BITS-1:0] hls_call_result;
    hls_call_result = '0;
    hls_step_5__phase = hls_step_4__phase;
    hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7 = hls_step_4__values__hls_DramLoad__command__call_0_depth_1__sum_7;
    hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8 = hls_step_3__values__hls_DramLoad__command__call_0_depth_1__i_8;
    if (hls_step_2__active[5] && fault == 0) begin hls_call_result = CALL_RESULT_BITS'(hls_DramLoad__command__shared_5(external_read_data, hls_step_5__phase, fault, 2, scratch__external_value_17, scratch__hls_DramLoad__command__call_0_depth_1__temporary_11, values__hls_DramLoad__command__call_0_depth_1__temporary_13, scratch__hls_DramLoad__command__call_0_depth_1__sum_12,
        hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7, scratch__hls_DramLoad__command__call_0_depth_1__temporary_13, scratch__hls_DramLoad__command__call_0_depth_1__i_14, hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8, '0)); {hls_step_5__phase, hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7, hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8} = $bits({hls_step_5__phase, hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7, hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8})'(hls_call_result); end
  end
  wire external_issue = hls_step_3__external_read || external_write;
  wire [31:0] external_commit_fault = fault != 0 ? fault : external_busy && external_bad_address && !reset ? 32'd3 : external_complete && memory_out__error_in ? 32'd5 : 0;
  always_ff @(posedge clk) begin
    if (reset) begin external_busy <= 0; external_sent <= 0; external_address_reg <= 0; external_data_reg <= 0; external_size_reg <= 0; external_write_reg <= 0; external_read_data <= 0; end
    else begin
      if (external_issue && external_commit_fault == 0) begin
        external_busy <= 1; external_sent <= 0; external_address_reg <= hls_step_3__external_address;
        external_data_reg <= external_write_data; external_size_reg <= hls_step_3__external_size; external_write_reg <= external_write;
      end
      if (external_accept) external_sent <= 1;
      if (external_complete) begin external_busy <= 0; external_sent <= 0; external_read_data <= memory_out__data_in; end
    end
    if (!reset && external_busy && external_bad_address) external_busy <= 0;
    if (reset) hls_accepted_operation_in <= 0;
    else if (command_valid_in && command_ready_out) hls_accepted_operation_in <= operation_in;
    if (reset) hls_accepted_index_in <= 0;
    else if (command_valid_in && command_ready_out) hls_accepted_index_in <= index_in;
    if (reset) hls_accepted_value_in <= 0;
    else if (command_valid_in && command_ready_out) hls_accepted_value_in <= value_in;
    values_reg.unused_bit <= 0;
    values_reg.hls_DramLoad__command__call_0_depth_1__count_4 <= reset ? '0 : hls_step_2__values__hls_DramLoad__command__call_0_depth_1__count_4;
    values_reg.hls_DramLoad__command__call_0_depth_1__i_8 <= reset ? '0 : hls_step_5__values__hls_DramLoad__command__call_0_depth_1__i_8;
    values_reg.hls_DramLoad__command__call_0_depth_1__index_3 <= reset ? '0 : hls_step_3__values__hls_DramLoad__command__call_0_depth_1__index_3;
    values_reg.hls_DramLoad__command__call_0_depth_1__sum_7 <= reset ? '0 : hls_step_5__values__hls_DramLoad__command__call_0_depth_1__sum_7;
    values_reg.hls_DramLoad__command__call_0_depth_1__words_5 <= reset ? '0 : hls_step_3__values__hls_DramLoad__command__call_0_depth_1__words_5;
    values_reg.hls_object__object_0 <= reset ? '0 : hls_step_0__values__hls_object__object_0;
    if (!reset) storage_lane0_reg <= storage_lane0;
    if (!reset) storage_lane1_reg <= storage_lane1;
    if (!reset) storage_lane2_reg <= storage_lane2;
    if (!reset) storage_lane3_reg <= storage_lane3;
    if (!reset) storage_lane4_reg <= storage_lane4;
    if (!reset) storage_lane5_reg <= storage_lane5;
    if (!reset) storage_lane6_reg <= storage_lane6;
    if (!reset) storage_lane7_reg <= storage_lane7;
    phase_reg <= (external_commit_fault != 0 && fault_reg == 0) ? 0 : hls_step_5__phase;
    fault_reg <= external_commit_fault;
    result_reg <= hls_step_4__result;
    pending_reg <= (external_commit_fault != 0 && fault_reg == 0) ? 1 : hls_step_4__pending;
    booting_reg <= (external_commit_fault != 0 && fault_reg == 0) ? 0 : hls_step_0__booting;
    heap_next_reg <= heap_next;
  end
endmodule

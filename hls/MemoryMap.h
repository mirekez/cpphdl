#pragma once
#include "Clocked.h"
#include "MemoryMapTypes.h"

namespace cpphdl::hls
{
// Generic streamed execution of an independent two-dimensional affine map.
// Memory remains ordered and single-outstanding; arithmetic runs independently.
// Slots bound all live data and let later gathers overlap earlier computation
// and scatter. No arithmetic latency is encoded in the controller.
template <unsigned Reads = 4, unsigned Writes = 2, unsigned Slots = 16,
          class InputPacket = MapInput<Reads>, class OutputPacket = MapOutput<Writes>>
class MemoryMapControl : public cpphdl::Module
{
    static_assert(Reads >= 1 && Reads <= 8 && Writes >= 1 && Writes <= 8,
                  "map stream counts must be in1..8");
    static_assert(Slots >= 2 && Slots <= 64 && (Slots & (Slots - 1)) == 0,
                  "map slots must be a power of two in2..64");

public:
    using Input = InputPacket;
    using Result = OutputPacket;
    using ReadWords = cpphdl::array<Reads, cpphdl::u32>;
    using WriteWords = cpphdl::array<Writes, cpphdl::u32>;
    ExternalMemoryIf<32> memory_out;
    _PORT(bool) command_valid_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_valid_out;
    _PORT(bool) response_ready_in;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    _PORT(uint32_t) rows_in;
    _PORT(uint32_t) columns_in;
    _PORT(uint32_t) read_count_in;
    _PORT(uint32_t) write_count_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) parameter_in;
    _PORT(ReadWords) read_base_in;
    _PORT(ReadWords) read_row_stride_in;
    _PORT(ReadWords) read_column_stride_in;
    _PORT(ReadWords) read_size_in;
    _PORT(WriteWords) write_base_in;
    _PORT(WriteWords) write_row_stride_in;
    _PORT(WriteWords) write_column_stride_in;
    _PORT(WriteWords) write_size_in;
    _PORT(bool) math_valid_out;
    _PORT(bool) math_ready_in;
    _PORT(Input) math_input_out;
    _PORT(bool) math_valid_in;
    _PORT(bool) math_ready_out;
    _PORT(Result) math_result_in;
    _PORT(uint32_t) math_fault_in;
    _PORT(bool) flush_valid_out;
    _PORT(bool) flush_ready_in;
    // Bits: all memory transactions, gathered words, iteration math,
    // scattered words, final fence. Counts: [(R+W)N,RN,N,WN,1].
    _PORT(uint32_t) profile_active_out;
    _PORT(uint32_t) profile_progress_out;
    _PORT(uint32_t) profile_wait_out;
    _PORT(uint32_t) profile_accept_out;
    _PORT(uint32_t) profile_complete_out;

private:
    cpphdl::reg<cpphdl::u<3>> phase;
    cpphdl::reg<cpphdl::u32> rows, columns, total, read_count, write_count, operation, parameter,
        error;
    cpphdl::reg<ReadWords> read_cursor, read_row_base, read_row_stride, read_column_stride,
        read_size;
    cpphdl::reg<WriteWords> write_cursor, write_row_base, write_row_stride, write_column_stride,
        write_size;
    cpphdl::reg<cpphdl::u32> gather_index, issue_index, return_index, scatter_index, gather_column,
        scatter_column, gather_lane, scatter_lane;
    // 0free,1gathering,2ready,3math offered/in flight,4result available.
    cpphdl::reg<cpphdl::array<Slots, cpphdl::u<3>>> slot_state;
    cpphdl::reg<cpphdl::array<Slots * Reads, cpphdl::u32>> input_values;
    cpphdl::reg<cpphdl::array<Slots * Writes, cpphdl::u32>> output_values;
    cpphdl::reg<cpphdl::u1> math_request, memory_request, memory_pending, memory_write;
    cpphdl::reg<cpphdl::u32> math_slot, math_index, math_inflight, memory_slot, memory_lane,
        memory_address, memory_data;
    cpphdl::reg<cpphdl::u8> memory_size;
    Input input_comb{};
    Input &input_func()
    {
        for (uint32_t lane = 0; lane < Reads; ++lane)
            input_comb.values[lane] =
                lane < read_count ? uint32_t(input_values[uint32_t(math_slot) * Reads + lane]) : 0u;
        input_comb.operation = uint32_t(operation);
        input_comb.parameter = uint32_t(parameter);
        input_comb.index = uint32_t(math_index);
        return input_comb;
    }
    uint64_t end_address(uint32_t base, uint32_t row_stride, uint32_t column_stride, uint32_t size)
    {
        return uint64_t(base) + uint64_t(rows_in() - 1) * row_stride +
               uint64_t(columns_in() - 1) * column_stride + size;
    }
    bool size_valid(uint32_t size)
    {
        return size == 1 || size == 2 || size == 4;
    }
    bool stream_valid(uint32_t base, uint32_t row_stride, uint32_t column_stride, uint32_t size,
                      bool write)
    {
        uint64_t row_span;
        row_span = uint64_t(columns_in() - 1) * column_stride + size;
        return size_valid(size) && ((base | row_stride | column_stride) & (size - 1)) == 0 &&
               end_address(base, row_stride, column_stride, size) <= 0x100000000ull &&
               (!write || ((columns_in() == 1 || column_stride >= size) &&
                           (rows_in() == 1 || uint64_t(row_stride) >= row_span)));
    }
    // Sufficient affine nonalias proof. Besides separated byte spans, accept
    // disjoint intervals within the same row period (e.g. a split vector).
    // Unproved aliasing is rejected rather than assuming pointer separation.
    bool disjoint(uint32_t a, uint32_t ar, uint32_t ac, uint32_t az, uint32_t b, uint32_t br,
                  uint32_t bc, uint32_t bz)
    {
        uint64_t ae, be, aspan, bspan, delta;
        bool periodic;
        ae = end_address(a, ar, ac, az);
        be = end_address(b, br, bc, bz);
        aspan = uint64_t(columns_in() - 1) * ac + az;
        bspan = uint64_t(columns_in() - 1) * bc + bz;
        delta = a <= b ? uint64_t(b) - a : uint64_t(a) - b;
        periodic = ar != 0 && ar == br && aspan <= ar && bspan <= ar &&
                   (a <= b ? (delta >= aspan && delta + bspan <= ar)
                           : (delta >= bspan && delta + aspan <= ar));
        return ae <= b || be <= a || periodic;
    }
    bool command_valid()
    {
        bool valid, same;
        valid = rows_in() != 0 && columns_in() != 0 &&
                uint64_t(rows_in()) * columns_in() <= 0xffffffffull && read_count_in() <= Reads &&
                write_count_in() != 0 && write_count_in() <= Writes;
        for (uint32_t r = 0; r < Reads; ++r)
            if (r < read_count_in())
                valid = valid && stream_valid(read_base_in()[r], read_row_stride_in()[r],
                                              read_column_stride_in()[r], read_size_in()[r], false);
        for (uint32_t w = 0; w < Writes; ++w)
            if (w < write_count_in())
            {
                valid =
                    valid && stream_valid(write_base_in()[w], write_row_stride_in()[w],
                                          write_column_stride_in()[w], write_size_in()[w], true);
                for (uint32_t v = 0; v < Writes; ++v)
                    if (v < w && v < write_count_in())
                        valid = valid && disjoint(write_base_in()[w], write_row_stride_in()[w],
                                                  write_column_stride_in()[w], write_size_in()[w],
                                                  write_base_in()[v], write_row_stride_in()[v],
                                                  write_column_stride_in()[v], write_size_in()[v]);
                for (uint32_t r = 0; r < Reads; ++r)
                    if (r < read_count_in())
                    {
                        same = read_base_in()[r] == write_base_in()[w] &&
                               read_size_in()[r] == write_size_in()[w] &&
                               (rows_in() == 1 ||
                                read_row_stride_in()[r] == write_row_stride_in()[w]) &&
                               (columns_in() == 1 ||
                                read_column_stride_in()[r] == write_column_stride_in()[w]);
                        valid = valid &&
                                (same || disjoint(read_base_in()[r], read_row_stride_in()[r],
                                                  read_column_stride_in()[r], read_size_in()[r],
                                                  write_base_in()[w], write_row_stride_in()[w],
                                                  write_column_stride_in()[w], write_size_in()[w]));
                    }
            }
        return valid;
    }
    bool fault_now()
    {
        return (memory_pending && memory_out.valid_out() && memory_out.error_out()) ||
               (math_inflight != 0 && math_valid_in() && math_fault_in() != 0);
    }
    bool quiet()
    {
        return !memory_request && !memory_pending && !math_request && math_inflight == 0;
    }
    uint32_t accepted_mask()
    {
        return (memory_request && memory_out.ready_out() ? (memory_write ? 9u : 3u) : 0u) |
               (math_request && math_ready_in() ? 4u : 0u) |
               (phase == 3 && flush_ready_in() ? 16u : 0u);
    }
    uint32_t completed_mask()
    {
        return (memory_pending && memory_out.valid_out() ? (memory_write ? 9u : 3u) : 0u) |
               (math_inflight != 0 && math_valid_in() ? 4u : 0u) |
               (phase == 3 && flush_ready_in() ? 16u : 0u);
    }
    uint32_t active_mask()
    {
        uint32_t bits;
        bits = (memory_request || memory_pending) ? (memory_write ? 9u : 3u) : 0u;
        if (math_request || math_inflight != 0)
            bits |= 4;
        for (uint32_t s = 0; s < Slots; ++s)
        {
            if (slot_state[s] == 2)
                bits |= 4;
            if (slot_state[s] == 4)
                bits |= 8;
        }
        if (phase == 3)
            bits |= 16;
        return bits;
    }

public:
    void _assign()
    {
        command_ready_out = _ASSIGN(phase == 0);
        response_valid_out = _ASSIGN(phase == 4);
        result_out = _ASSIGN(uint64_t(0));
        fault_out = _ASSIGN(uint32_t(error));
        math_valid_out = _ASSIGN(bool(math_request));
        math_input_out = _ASSIGN_COMB(input_func());
        math_ready_out = _ASSIGN(math_inflight != 0);
        memory_out.valid_in = _ASSIGN(bool(memory_request));
        memory_out.write_in = _ASSIGN(bool(memory_write));
        memory_out.addr_in = _ASSIGN(uint32_t(memory_address));
        memory_out.size_in = _ASSIGN(uint8_t(memory_size));
        memory_out.data_in = _ASSIGN(uint64_t(uint32_t(memory_data)));
        memory_out.ready_in = _ASSIGN(bool(memory_pending));
        flush_valid_out = _ASSIGN(phase == 3 || phase == 4);
        profile_accept_out = _ASSIGN(accepted_mask());
        profile_complete_out = _ASSIGN(completed_mask());
        profile_active_out = _ASSIGN(active_mask());
        profile_progress_out = _ASSIGN(profile_accept_out() | profile_complete_out());
        profile_wait_out = _ASSIGN(profile_active_out() & ~profile_progress_out());
    }
    void _work(bool reset)
    {
        uint32_t s, lane;
        bool accept, complete, finish_gather, finish_scatter;
        if (reset)
        {
            phase.clr();
            rows.clr();
            columns.clr();
            total.clr();
            read_count.clr();
            write_count.clr();
            operation.clr();
            parameter.clr();
            error.clr();
            read_cursor.clr();
            read_row_base.clr();
            read_row_stride.clr();
            read_column_stride.clr();
            read_size.clr();
            write_cursor.clr();
            write_row_base.clr();
            write_row_stride.clr();
            write_column_stride.clr();
            write_size.clr();
            gather_index.clr();
            issue_index.clr();
            return_index.clr();
            scatter_index.clr();
            gather_column.clr();
            scatter_column.clr();
            gather_lane.clr();
            scatter_lane.clr();
            slot_state.clr();
            input_values.clr();
            output_values.clr();
            math_request.clr();
            math_slot.clr();
            math_index.clr();
            math_inflight.clr();
            memory_request.clr();
            memory_pending.clr();
            memory_write.clr();
            memory_slot.clr();
            memory_lane.clr();
            memory_address.clr();
            memory_data.clr();
            memory_size.clr();
        }
        else
        {
            finish_gather = false;
            finish_scatter = false;
            if (command_valid_in() && command_ready_out())
            {
                rows._next = rows_in();
                columns._next = columns_in();
                total._next = uint32_t(uint64_t(rows_in()) * columns_in());
                read_count._next = read_count_in();
                write_count._next = write_count_in();
                operation._next = operation_in();
                parameter._next = parameter_in();
                for (uint32_t r = 0; r < Reads; ++r)
                {
                    read_cursor._next[r] = read_base_in()[r];
                    read_row_base._next[r] = read_base_in()[r];
                    read_row_stride._next[r] = read_row_stride_in()[r];
                    read_column_stride._next[r] = read_column_stride_in()[r];
                    read_size._next[r] = read_size_in()[r];
                }
                for (uint32_t w = 0; w < Writes; ++w)
                {
                    write_cursor._next[w] = write_base_in()[w];
                    write_row_base._next[w] = write_base_in()[w];
                    write_row_stride._next[w] = write_row_stride_in()[w];
                    write_column_stride._next[w] = write_column_stride_in()[w];
                    write_size._next[w] = write_size_in()[w];
                }
                gather_index._next = 0;
                issue_index._next = 0;
                return_index._next = 0;
                scatter_index._next = 0;
                gather_column._next = 0;
                scatter_column._next = 0;
                gather_lane._next = 0;
                scatter_lane._next = 0;
                slot_state._next = 0;
                error._next = 0;
                phase._next = command_valid() ? 1 : 4;
                if (!command_valid())
                    error._next = 1;
            }
            accept = bool(math_request) && math_ready_in();
            complete = math_inflight != 0 && math_valid_in();
            if (accept != complete)
                math_inflight._next = uint32_t(math_inflight) + (accept ? 1u : 0xffffffffu);
            if (accept)
            {
                math_request._next = false;
                issue_index._next = uint32_t(issue_index) + 1;
            }
            if (complete)
            {
                s = uint32_t(return_index) & (Slots - 1);
                return_index._next = uint32_t(return_index) + 1;
                if (phase == 1 && !fault_now())
                {
                    for (uint32_t w = 0; w < Writes; ++w)
                        output_values._next[s * Writes + w] = math_result_in().values[w];
                    slot_state._next[s] = 4;
                }
            }
            if (phase == 1 && !fault_now())
            {
                s = uint32_t(issue_index) & (Slots - 1);
                if (!math_request && issue_index < total && slot_state[s] == 2)
                {
                    math_request._next = true;
                    math_slot._next = s;
                    math_index._next = uint32_t(issue_index);
                    slot_state._next[s] = 3;
                }
                s = uint32_t(gather_index) & (Slots - 1);
                if (read_count == 0 && gather_index < total && slot_state[s] == 0)
                {
                    slot_state._next[s] = 2;
                    finish_gather = true;
                }
                if (!memory_request && !memory_pending)
                {
                    s = uint32_t(scatter_index) & (Slots - 1);
                    if (scatter_index < total && slot_state[s] == 4)
                    {
                        lane = uint32_t(scatter_lane);
                        memory_request._next = true;
                        memory_write._next = true;
                        memory_slot._next = s;
                        memory_lane._next = lane;
                        memory_address._next = write_cursor[lane];
                        memory_data._next = output_values[s * Writes + lane];
                        memory_size._next = write_size[lane];
                    }
                    else
                    {
                        s = uint32_t(gather_index) & (Slots - 1);
                        if (read_count != 0 && gather_index < total &&
                            (slot_state[s] == 0 || slot_state[s] == 1))
                        {
                            lane = uint32_t(gather_lane);
                            memory_request._next = true;
                            memory_write._next = false;
                            memory_slot._next = s;
                            memory_lane._next = lane;
                            memory_address._next = read_cursor[lane];
                            memory_data._next = 0;
                            memory_size._next = read_size[lane];
                            slot_state._next[s] = 1;
                        }
                    }
                }
                if (scatter_index == total && quiet())
                    phase._next = 3;
            }
            if (memory_request && memory_out.ready_out())
            {
                memory_request._next = false;
                memory_pending._next = true;
            }
            if (memory_pending && memory_out.valid_out())
            {
                memory_pending._next = false;
                s = uint32_t(memory_slot);
                lane = uint32_t(memory_lane);
                if (phase == 1 && !fault_now())
                {
                    if (memory_write)
                    {
                        if (lane + 1 == write_count)
                        {
                            scatter_lane._next = 0;
                            slot_state._next[s] = 0;
                            finish_scatter = true;
                        }
                        else
                            scatter_lane._next = lane + 1;
                    }
                    else
                    {
                        input_values._next[s * Reads + lane] =
                            uint32_t(memory_out.data_out()) &
                            (memory_size == 1 ? 0xffu : (memory_size == 2 ? 0xffffu : 0xffffffffu));
                        if (lane + 1 == read_count)
                        {
                            gather_lane._next = 0;
                            slot_state._next[s] = 2;
                            finish_gather = true;
                        }
                        else
                            gather_lane._next = lane + 1;
                    }
                }
            }
            if (finish_gather)
            {
                gather_index._next = uint32_t(gather_index) + 1;
                gather_column._next =
                    uint32_t(gather_column) + 1 == columns ? 0u : uint32_t(gather_column) + 1;
                for (uint32_t r = 0; r < Reads; ++r)
                {
                    if (uint32_t(gather_column) + 1 == columns)
                    {
                        read_row_base._next[r] =
                            uint32_t(read_row_base[r]) + uint32_t(read_row_stride[r]);
                        read_cursor._next[r] =
                            uint32_t(read_row_base[r]) + uint32_t(read_row_stride[r]);
                    }
                    else
                        read_cursor._next[r] =
                            uint32_t(read_cursor[r]) + uint32_t(read_column_stride[r]);
                }
            }
            if (finish_scatter)
            {
                scatter_index._next = uint32_t(scatter_index) + 1;
                scatter_column._next =
                    uint32_t(scatter_column) + 1 == columns ? 0u : uint32_t(scatter_column) + 1;
                for (uint32_t w = 0; w < Writes; ++w)
                {
                    if (uint32_t(scatter_column) + 1 == columns)
                    {
                        write_row_base._next[w] =
                            uint32_t(write_row_base[w]) + uint32_t(write_row_stride[w]);
                        write_cursor._next[w] =
                            uint32_t(write_row_base[w]) + uint32_t(write_row_stride[w]);
                    }
                    else
                        write_cursor._next[w] =
                            uint32_t(write_cursor[w]) + uint32_t(write_column_stride[w]);
                }
            }
            if (fault_now())
            {
                phase._next = 2;
                if (error == 0)
                    error._next =
                        (memory_pending && memory_out.valid_out() && memory_out.error_out()) ? 2u
                                                                                             : 3u;
            }
            if (phase == 2 && quiet())
                phase._next = 3;
            if (phase == 3 && flush_ready_in())
                phase._next = 4;
            if (phase == 4 && response_ready_in())
                phase._next = 0;
        }
    }
    void _strobe()
    {
        phase.strobe();
        rows.strobe();
        columns.strobe();
        total.strobe();
        read_count.strobe();
        write_count.strobe();
        operation.strobe();
        parameter.strobe();
        error.strobe();
        read_cursor.strobe();
        read_row_base.strobe();
        read_row_stride.strobe();
        read_column_stride.strobe();
        read_size.strobe();
        write_cursor.strobe();
        write_row_base.strobe();
        write_row_stride.strobe();
        write_column_stride.strobe();
        write_size.strobe();
        gather_index.strobe();
        issue_index.strobe();
        return_index.strobe();
        scatter_index.strobe();
        gather_column.strobe();
        scatter_column.strobe();
        gather_lane.strobe();
        scatter_lane.strobe();
        slot_state.strobe();
        input_values.strobe();
        output_values.strobe();
        math_request.strobe();
        math_slot.strobe();
        math_index.strobe();
        math_inflight.strobe();
        memory_request.strobe();
        memory_pending.strobe();
        memory_write.strobe();
        memory_slot.strobe();
        memory_lane.strobe();
        memory_address.strobe();
        memory_data.strobe();
        memory_size.strobe();
    }
};

// The arithmetic policy is compiled through the existing generic streaming
// HLS backend, including its bounded OpenMP value-loop support. Keeping the
// independent controller separate prevents FP retiming from throttling memory.
template <class Methods, unsigned Reads = 4, unsigned Writes = 2, unsigned Slots = 16,
          class InputPacket = MapInput<Reads>, class OutputPacket = MapOutput<Writes>>
class ClockedMap : public cpphdl::Module
{
    static_assert(std::is_empty<Methods>::value,
                  "map policies must be stateless; pass context in the packet");

public:
    using Input = InputPacket;
    using Result = OutputPacket;
    using ReadWords = cpphdl::array<Reads, cpphdl::u32>;
    using WriteWords = cpphdl::array<Writes, cpphdl::u32>;
    MemoryMapControl<Reads, Writes, Slots, Input, Result> control;
    ClockedPipeline<Methods, 2, Input, Result> compute;
    ExternalMemoryIf<32> memory_out;
    _PORT(bool) command_valid_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_valid_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) flush_valid_out;
    _PORT(bool) flush_ready_in;
    _PORT(uint64_t) result_out;
    _PORT(uint32_t) fault_out;
    _PORT(uint32_t) rows_in;
    _PORT(uint32_t) columns_in;
    _PORT(uint32_t) read_count_in;
    _PORT(uint32_t) write_count_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) parameter_in;
    _PORT(uint32_t) profile_active_out;
    _PORT(uint32_t) profile_progress_out;
    _PORT(uint32_t) profile_wait_out;
    _PORT(uint32_t) profile_accept_out;
    _PORT(uint32_t) profile_complete_out;
    _PORT(ReadWords) read_base_in;
    _PORT(ReadWords) read_row_stride_in;
    _PORT(ReadWords) read_column_stride_in;
    _PORT(ReadWords) read_size_in;
    _PORT(WriteWords) write_base_in;
    _PORT(WriteWords) write_row_stride_in;
    _PORT(WriteWords) write_column_stride_in;
    _PORT(WriteWords) write_size_in;

private:
    Input zero_comb{};
    Input &zero_comb_func()
    {
        for (uint32_t r = 0; r < Reads; ++r)
        {
            zero_comb.values[r] = 0;
        }
        zero_comb.operation = 0;
        zero_comb.parameter = 0;
        zero_comb.index = 0;
        return zero_comb;
    }

public:
    void _assign()
    {
        control.command_valid_in = _ASSIGN(command_valid_in());
        command_ready_out = _ASSIGN(control.command_ready_out());
        response_valid_out = _ASSIGN(control.response_valid_out());
        control.response_ready_in = _ASSIGN(response_ready_in());
        flush_valid_out = _ASSIGN(control.flush_valid_out());
        control.flush_ready_in = _ASSIGN(flush_ready_in());
        result_out = _ASSIGN(control.result_out());
        fault_out = _ASSIGN(control.fault_out());
        control.rows_in = _ASSIGN(rows_in());
        control.columns_in = _ASSIGN(columns_in());
        control.read_count_in = _ASSIGN(read_count_in());
        control.write_count_in = _ASSIGN(write_count_in());
        control.operation_in = _ASSIGN(operation_in());
        control.parameter_in = _ASSIGN(parameter_in());
        profile_active_out = _ASSIGN(control.profile_active_out());
        profile_progress_out = _ASSIGN(control.profile_progress_out());
        profile_wait_out = _ASSIGN(control.profile_wait_out());
        profile_accept_out = _ASSIGN(control.profile_accept_out());
        profile_complete_out = _ASSIGN(control.profile_complete_out());
        control.read_base_in = _ASSIGN(read_base_in());
        control.read_row_stride_in = _ASSIGN(read_row_stride_in());
        control.read_column_stride_in = _ASSIGN(read_column_stride_in());
        control.read_size_in = _ASSIGN(read_size_in());
        control.write_base_in = _ASSIGN(write_base_in());
        control.write_row_stride_in = _ASSIGN(write_row_stride_in());
        control.write_column_stride_in = _ASSIGN(write_column_stride_in());
        control.write_size_in = _ASSIGN(write_size_in());
        assignIf(*this, control, memory_out, control.memory_out);
        compute.command_valid_in = _ASSIGN(control.math_valid_out());
        compute.operation_in = _ASSIGN(control.math_input_out());
        compute.index_in = _ASSIGN_COMB(zero_comb_func());
        compute.value_in = _ASSIGN_COMB(zero_comb_func());
        compute.response_ready_in = _ASSIGN(control.math_ready_out());
        control.math_ready_in = _ASSIGN(compute.command_ready_out());
        control.math_valid_in = _ASSIGN(compute.response_valid_out());
        control.math_result_in = _ASSIGN(compute.result_out());
        control.math_fault_in = _ASSIGN(compute.fault_out());
        control._assign();
        compute._assign();
    }
    void _work(bool reset)
    {
        control._work(reset);
        compute._work(reset);
    }
    void _strobe()
    {
        control._strobe();
        compute._strobe();
    }
};
} // namespace cpphdl::hls

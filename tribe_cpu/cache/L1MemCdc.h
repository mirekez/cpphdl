#pragma once

#include "L1MemIf.h"

// Bridges the level-sensitive L1 memory protocol from clk to l2_clock. The
// fast side cannot observe completion until the slow side has sampled and
// completed the captured request.
template<size_t PORT_BITWIDTH>
class L1MemFastToSlowCdc : public Module
{
public:
    L1MemIf<PORT_BITWIDTH> fast_in;
    L1MemIf<PORT_BITWIDTH> slow_out;

private:
    reg<u1> read_fast_reg;
    reg<u1> write_fast_reg;
    reg<u32> addr_fast_reg;
    reg<u32> write_data_fast_reg;
    reg<u8> write_mask_fast_reg;
    reg<u1> cache_disable_fast_reg;
    reg<u1> request_fast_reg;
    reg<u1> request_active_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> response_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> response_fast2_reg;
    reg<u1> response_ack_fast_reg;
    reg<logic<PORT_BITWIDTH>> read_data_fast_reg;

    // (* ASYNC_REG = "TRUE" *)
    reg<u1> request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> request_slow2_reg;
    reg<u1> request_seen_slow_reg;
    reg<u1> request_active_slow_reg;
    reg<u1> read_slow_reg;
    reg<u1> write_slow_reg;
    reg<u32> addr_slow_reg;
    reg<u32> write_data_slow_reg;
    reg<u8> write_mask_slow_reg;
    reg<u1> cache_disable_slow_reg;
    reg<logic<PORT_BITWIDTH>> read_data_slow_reg;
    reg<u1> response_slow_reg;

public:
    void _assign()
    {
        fast_in.read_data_out = _ASSIGN_REG(read_data_fast_reg);
        fast_in.wait_out = _ASSIGN(
            (fast_in.read_in() || fast_in.write_in()) &&
            !(request_active_fast_reg &&
                response_fast2_reg != response_ack_fast_reg &&
                fast_in.read_in() == (bool)read_fast_reg &&
                fast_in.write_in() == (bool)write_fast_reg &&
                fast_in.addr_in() == (uint32_t)addr_fast_reg &&
                (!fast_in.write_in() ||
                    (fast_in.write_data_in() == (uint32_t)write_data_fast_reg &&
                     fast_in.write_mask_in() == (uint8_t)write_mask_fast_reg)) &&
                fast_in.cache_disable_in() == (bool)cache_disable_fast_reg));

        slow_out.read_in = _ASSIGN((bool)(request_active_slow_reg && read_slow_reg));
        slow_out.write_in = _ASSIGN((bool)(request_active_slow_reg && write_slow_reg));
        slow_out.addr_in = _ASSIGN_REG(addr_slow_reg);
        slow_out.write_data_in = _ASSIGN_REG(write_data_slow_reg);
        slow_out.write_mask_in = _ASSIGN_REG(write_mask_slow_reg);
        slow_out.cache_disable_in = _ASSIGN_REG(cache_disable_slow_reg);
    }

    void work_clk_func(bool reset)
    {
        bool request;
        request = fast_in.read_in() || fast_in.write_in();
        response_fast1_reg._next = response_slow_reg;
        response_fast2_reg._next = response_fast1_reg;
        if (response_fast1_reg != response_fast2_reg) {
            // The response payload was stable before the toggle entered stage
            // 1.  Capture it when the toggle advances to stage 2 so no L2-clock
            // register drives CPU logic combinationally.
            read_data_fast_reg._next = read_data_slow_reg;
        }

        if (request_active_fast_reg && response_fast2_reg != response_ack_fast_reg) {
            response_ack_fast_reg._next = response_fast2_reg;
            // The source consumes the response on this fast edge. Do not
            // sample the shared L1/MMU request mux until the following edge,
            // after its new owner and payload have become stable.
            request_active_fast_reg._next = false;
        }
        else if (!request_active_fast_reg && request) {
            read_fast_reg._next = fast_in.read_in();
            write_fast_reg._next = fast_in.write_in();
            addr_fast_reg._next = fast_in.addr_in();
            write_data_fast_reg._next = fast_in.write_data_in();
            write_mask_fast_reg._next = fast_in.write_mask_in();
            cache_disable_fast_reg._next = fast_in.cache_disable_in();
            request_fast_reg._next = !request_fast_reg;
            request_active_fast_reg._next = true;
        }

        if (reset) {
            read_fast_reg.clr();
            write_fast_reg.clr();
            addr_fast_reg.clr();
            write_data_fast_reg.clr();
            write_mask_fast_reg.clr();
            cache_disable_fast_reg.clr();
            request_fast_reg.clr();
            request_active_fast_reg.clr();
            response_fast1_reg.clr();
            response_fast2_reg.clr();
            response_ack_fast_reg.clr();
            read_data_fast_reg.clr();
        }
    }

    void _work(bool reset) { work_clk_func(reset); }
    void _work_clk(bool reset) { work_clk_func(reset); }

    void _strobe_clk()
    {
        read_fast_reg.strobe();
        write_fast_reg.strobe();
        addr_fast_reg.strobe();
        write_data_fast_reg.strobe();
        write_mask_fast_reg.strobe();
        cache_disable_fast_reg.strobe();
        request_fast_reg.strobe();
        request_active_fast_reg.strobe();
        response_fast1_reg.strobe();
        response_fast2_reg.strobe();
        response_ack_fast_reg.strobe();
        read_data_fast_reg.strobe();
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        read_fast_reg.strobe(checkpoint_fd);
        write_fast_reg.strobe(checkpoint_fd);
        addr_fast_reg.strobe(checkpoint_fd);
        write_data_fast_reg.strobe(checkpoint_fd);
        write_mask_fast_reg.strobe(checkpoint_fd);
        cache_disable_fast_reg.strobe(checkpoint_fd);
        request_fast_reg.strobe(checkpoint_fd);
        request_active_fast_reg.strobe(checkpoint_fd);
        response_fast1_reg.strobe(checkpoint_fd);
        response_fast2_reg.strobe(checkpoint_fd);
        response_ack_fast_reg.strobe(checkpoint_fd);
        read_data_fast_reg.strobe(checkpoint_fd);
    }

    void _work_l2_clock(bool reset)
    {
        request_slow1_reg._next = request_fast_reg;
        request_slow2_reg._next = request_slow1_reg;

        if (!request_active_slow_reg && request_slow2_reg != request_seen_slow_reg) {
            request_seen_slow_reg._next = request_slow2_reg;
            // Capture the bundled request only after its toggle has traversed
            // both synchronizer stages.  The fast-side payload remains held
            // until the response returns.
            read_slow_reg._next = read_fast_reg;
            write_slow_reg._next = write_fast_reg;
            addr_slow_reg._next = addr_fast_reg;
            write_data_slow_reg._next = write_data_fast_reg;
            write_mask_slow_reg._next = write_mask_fast_reg;
            cache_disable_slow_reg._next = cache_disable_fast_reg;
            request_active_slow_reg._next = true;
        }
        else if (request_active_slow_reg && !slow_out.wait_out()) {
            read_data_slow_reg._next = slow_out.read_data_out();
            response_slow_reg._next = !response_slow_reg;
            request_active_slow_reg._next = false;
        }

        if (reset) {
            request_slow1_reg.clr();
            request_slow2_reg.clr();
            request_seen_slow_reg.clr();
            request_active_slow_reg.clr();
            read_slow_reg.clr();
            write_slow_reg.clr();
            addr_slow_reg.clr();
            write_data_slow_reg.clr();
            write_mask_slow_reg.clr();
            cache_disable_slow_reg.clr();
            read_data_slow_reg.clr();
            response_slow_reg.clr();
        }
    }

    void _strobe_l2_clock()
    {
        request_slow1_reg.strobe();
        request_slow2_reg.strobe();
        request_seen_slow_reg.strobe();
        request_active_slow_reg.strobe();
        read_slow_reg.strobe();
        write_slow_reg.strobe();
        addr_slow_reg.strobe();
        write_data_slow_reg.strobe();
        write_mask_slow_reg.strobe();
        cache_disable_slow_reg.strobe();
        read_data_slow_reg.strobe();
        response_slow_reg.strobe();
    }

#ifndef SYNTHESIS
    void checkpoint(FILE* checkpoint_fd)
    {
        _strobe(checkpoint_fd);
        request_slow1_reg.strobe(checkpoint_fd);
        request_slow2_reg.strobe(checkpoint_fd);
        request_seen_slow_reg.strobe(checkpoint_fd);
        request_active_slow_reg.strobe(checkpoint_fd);
        read_slow_reg.strobe(checkpoint_fd);
        write_slow_reg.strobe(checkpoint_fd);
        addr_slow_reg.strobe(checkpoint_fd);
        write_data_slow_reg.strobe(checkpoint_fd);
        write_mask_slow_reg.strobe(checkpoint_fd);
        cache_disable_slow_reg.strobe(checkpoint_fd);
        read_data_slow_reg.strobe(checkpoint_fd);
        response_slow_reg.strobe(checkpoint_fd);
    }
#endif
};

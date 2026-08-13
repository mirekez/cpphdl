#pragma once

#include "Axi4.h"

// Bridges an AXI-like master in the l2_clock domain to a responder in the
// primary clk domain. Each independent channel is a one-entry toggle mailbox;
// its payload remains stable until the destination acknowledges the toggle.
template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4SlowToFastCdc : public Module
{
public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> slow_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> fast_out;

private:
    reg<u<ADDR_WIDTH>> aw_addr_slow_reg;
    reg<u<ID_WIDTH>> aw_id_slow_reg;
    reg<logic<DATA_WIDTH>> w_data_slow_reg;
    reg<logic<DATA_WIDTH / 8>> w_strb_slow_reg;
    reg<u1> w_last_slow_reg;
    reg<u<ADDR_WIDTH>> ar_addr_slow_reg;
    reg<u<ID_WIDTH>> ar_id_slow_reg;
    reg<u1> aw_request_slow_reg;
    reg<u1> w_request_slow_reg;
    reg<u1> ar_request_slow_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_ack_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_ack_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_ack_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_ack_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_ack_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_ack_slow2_reg;

    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_request_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_request_slow2_reg;
    reg<u1> b_ack_slow_reg;
    reg<u1> r_ack_slow_reg;

    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_request_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_request_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_request_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_request_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_request_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_request_fast2_reg;
    reg<u1> aw_ack_fast_reg;
    reg<u1> w_ack_fast_reg;
    reg<u1> ar_ack_fast_reg;

    reg<u<ID_WIDTH>> b_id_fast_reg;
    reg<logic<DATA_WIDTH>> r_data_fast_reg;
    reg<u1> r_last_fast_reg;
    reg<u<ID_WIDTH>> r_id_fast_reg;
    reg<u1> b_request_fast_reg;
    reg<u1> r_request_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_ack_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_ack_fast2_reg;

public:
    void _assign()
    {
        slow_in.awready_out = _ASSIGN(aw_request_slow_reg == aw_ack_slow2_reg);
        slow_in.wready_out = _ASSIGN(w_request_slow_reg == w_ack_slow2_reg);
        slow_in.arready_out = _ASSIGN(ar_request_slow_reg == ar_ack_slow2_reg);
        slow_in.bvalid_out = _ASSIGN(b_request_slow2_reg != b_ack_slow_reg);
        slow_in.bid_out = _ASSIGN((u<ID_WIDTH>)b_id_fast_reg);
        slow_in.rvalid_out = _ASSIGN(r_request_slow2_reg != r_ack_slow_reg);
        slow_in.rdata_out = _ASSIGN((logic<DATA_WIDTH>)r_data_fast_reg);
        slow_in.rlast_out = _ASSIGN((bool)r_last_fast_reg);
        slow_in.rid_out = _ASSIGN((u<ID_WIDTH>)r_id_fast_reg);

        fast_out.awvalid_in = _ASSIGN(aw_request_fast2_reg != aw_ack_fast_reg);
        fast_out.awaddr_in = _ASSIGN((u<ADDR_WIDTH>)aw_addr_slow_reg);
        fast_out.awid_in = _ASSIGN((u<ID_WIDTH>)aw_id_slow_reg);
        fast_out.wvalid_in = _ASSIGN(w_request_fast2_reg != w_ack_fast_reg);
        fast_out.wdata_in = _ASSIGN((logic<DATA_WIDTH>)w_data_slow_reg);
        fast_out.wstrb_in = _ASSIGN((logic<DATA_WIDTH / 8>)w_strb_slow_reg);
        fast_out.wlast_in = _ASSIGN((bool)w_last_slow_reg);
        fast_out.bready_in = _ASSIGN(b_request_fast_reg == b_ack_fast2_reg);
        fast_out.arvalid_in = _ASSIGN(ar_request_fast2_reg != ar_ack_fast_reg);
        fast_out.araddr_in = _ASSIGN((u<ADDR_WIDTH>)ar_addr_slow_reg);
        fast_out.arid_in = _ASSIGN((u<ID_WIDTH>)ar_id_slow_reg);
        fast_out.rready_in = _ASSIGN(r_request_fast_reg == r_ack_fast2_reg);
    }

    void work_clk_func(bool reset)
    {
        aw_request_fast1_reg._next = aw_request_slow_reg;
        aw_request_fast2_reg._next = aw_request_fast1_reg;
        w_request_fast1_reg._next = w_request_slow_reg;
        w_request_fast2_reg._next = w_request_fast1_reg;
        ar_request_fast1_reg._next = ar_request_slow_reg;
        ar_request_fast2_reg._next = ar_request_fast1_reg;
        b_ack_fast1_reg._next = b_ack_slow_reg;
        b_ack_fast2_reg._next = b_ack_fast1_reg;
        r_ack_fast1_reg._next = r_ack_slow_reg;
        r_ack_fast2_reg._next = r_ack_fast1_reg;

        if (fast_out.awvalid_in() && fast_out.awready_out()) {
            aw_ack_fast_reg._next = aw_request_fast2_reg;
        }
        if (fast_out.wvalid_in() && fast_out.wready_out()) {
            w_ack_fast_reg._next = w_request_fast2_reg;
        }
        if (fast_out.arvalid_in() && fast_out.arready_out()) {
            ar_ack_fast_reg._next = ar_request_fast2_reg;
        }
        if (fast_out.bvalid_out() && fast_out.bready_in()) {
            b_id_fast_reg._next = fast_out.bid_out();
            b_request_fast_reg._next = !b_request_fast_reg;
        }
        if (fast_out.rvalid_out() && fast_out.rready_in()) {
            r_data_fast_reg._next = fast_out.rdata_out();
            r_last_fast_reg._next = fast_out.rlast_out();
            r_id_fast_reg._next = fast_out.rid_out();
            r_request_fast_reg._next = !r_request_fast_reg;
        }

        if (reset) {
            aw_request_fast1_reg.clr();
            aw_request_fast2_reg.clr();
            w_request_fast1_reg.clr();
            w_request_fast2_reg.clr();
            ar_request_fast1_reg.clr();
            ar_request_fast2_reg.clr();
            aw_ack_fast_reg.clr();
            w_ack_fast_reg.clr();
            ar_ack_fast_reg.clr();
            b_id_fast_reg.clr();
            r_data_fast_reg.clr();
            r_last_fast_reg.clr();
            r_id_fast_reg.clr();
            b_request_fast_reg.clr();
            r_request_fast_reg.clr();
            b_ack_fast1_reg.clr();
            b_ack_fast2_reg.clr();
            r_ack_fast1_reg.clr();
            r_ack_fast2_reg.clr();
        }
    }

    void _work(bool reset)
    {
        work_clk_func(reset);
    }

    void _work_clk(bool reset)
    {
        work_clk_func(reset);
    }

    void _strobe_clk()
    {
        aw_request_fast1_reg.strobe();
        aw_request_fast2_reg.strobe();
        w_request_fast1_reg.strobe();
        w_request_fast2_reg.strobe();
        ar_request_fast1_reg.strobe();
        ar_request_fast2_reg.strobe();
        aw_ack_fast_reg.strobe();
        w_ack_fast_reg.strobe();
        ar_ack_fast_reg.strobe();
        b_id_fast_reg.strobe();
        r_data_fast_reg.strobe();
        r_last_fast_reg.strobe();
        r_id_fast_reg.strobe();
        b_request_fast_reg.strobe();
        r_request_fast_reg.strobe();
        b_ack_fast1_reg.strobe();
        b_ack_fast2_reg.strobe();
        r_ack_fast1_reg.strobe();
        r_ack_fast2_reg.strobe();
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        aw_request_fast1_reg.strobe(checkpoint_fd);
        aw_request_fast2_reg.strobe(checkpoint_fd);
        w_request_fast1_reg.strobe(checkpoint_fd);
        w_request_fast2_reg.strobe(checkpoint_fd);
        ar_request_fast1_reg.strobe(checkpoint_fd);
        ar_request_fast2_reg.strobe(checkpoint_fd);
        aw_ack_fast_reg.strobe(checkpoint_fd);
        w_ack_fast_reg.strobe(checkpoint_fd);
        ar_ack_fast_reg.strobe(checkpoint_fd);
        b_id_fast_reg.strobe(checkpoint_fd);
        r_data_fast_reg.strobe(checkpoint_fd);
        r_last_fast_reg.strobe(checkpoint_fd);
        r_id_fast_reg.strobe(checkpoint_fd);
        b_request_fast_reg.strobe(checkpoint_fd);
        r_request_fast_reg.strobe(checkpoint_fd);
        b_ack_fast1_reg.strobe(checkpoint_fd);
        b_ack_fast2_reg.strobe(checkpoint_fd);
        r_ack_fast1_reg.strobe(checkpoint_fd);
        r_ack_fast2_reg.strobe(checkpoint_fd);
    }

    void _work_l2_clock(bool reset)
    {
        aw_ack_slow1_reg._next = aw_ack_fast_reg;
        aw_ack_slow2_reg._next = aw_ack_slow1_reg;
        w_ack_slow1_reg._next = w_ack_fast_reg;
        w_ack_slow2_reg._next = w_ack_slow1_reg;
        ar_ack_slow1_reg._next = ar_ack_fast_reg;
        ar_ack_slow2_reg._next = ar_ack_slow1_reg;
        b_request_slow1_reg._next = b_request_fast_reg;
        b_request_slow2_reg._next = b_request_slow1_reg;
        r_request_slow1_reg._next = r_request_fast_reg;
        r_request_slow2_reg._next = r_request_slow1_reg;

        if (slow_in.awvalid_in() && slow_in.awready_out()) {
            aw_addr_slow_reg._next = slow_in.awaddr_in();
            aw_id_slow_reg._next = slow_in.awid_in();
            aw_request_slow_reg._next = !aw_request_slow_reg;
        }
        if (slow_in.wvalid_in() && slow_in.wready_out()) {
            w_data_slow_reg._next = slow_in.wdata_in();
            w_strb_slow_reg._next = slow_in.wstrb_in();
            w_last_slow_reg._next = slow_in.wlast_in();
            w_request_slow_reg._next = !w_request_slow_reg;
        }
        if (slow_in.arvalid_in() && slow_in.arready_out()) {
            ar_addr_slow_reg._next = slow_in.araddr_in();
            ar_id_slow_reg._next = slow_in.arid_in();
            ar_request_slow_reg._next = !ar_request_slow_reg;
        }
        if (slow_in.bvalid_out() && slow_in.bready_in()) {
            b_ack_slow_reg._next = b_request_slow2_reg;
        }
        if (slow_in.rvalid_out() && slow_in.rready_in()) {
            r_ack_slow_reg._next = r_request_slow2_reg;
        }

        if (reset) {
            aw_addr_slow_reg.clr();
            aw_id_slow_reg.clr();
            w_data_slow_reg.clr();
            w_strb_slow_reg.clr();
            w_last_slow_reg.clr();
            ar_addr_slow_reg.clr();
            ar_id_slow_reg.clr();
            aw_request_slow_reg.clr();
            w_request_slow_reg.clr();
            ar_request_slow_reg.clr();
            aw_ack_slow1_reg.clr();
            aw_ack_slow2_reg.clr();
            w_ack_slow1_reg.clr();
            w_ack_slow2_reg.clr();
            ar_ack_slow1_reg.clr();
            ar_ack_slow2_reg.clr();
            b_request_slow1_reg.clr();
            b_request_slow2_reg.clr();
            r_request_slow1_reg.clr();
            r_request_slow2_reg.clr();
            b_ack_slow_reg.clr();
            r_ack_slow_reg.clr();
        }
    }

    void _strobe_l2_clock()
    {
        aw_addr_slow_reg.strobe();
        aw_id_slow_reg.strobe();
        w_data_slow_reg.strobe();
        w_strb_slow_reg.strobe();
        w_last_slow_reg.strobe();
        ar_addr_slow_reg.strobe();
        ar_id_slow_reg.strobe();
        aw_request_slow_reg.strobe();
        w_request_slow_reg.strobe();
        ar_request_slow_reg.strobe();
        aw_ack_slow1_reg.strobe();
        aw_ack_slow2_reg.strobe();
        w_ack_slow1_reg.strobe();
        w_ack_slow2_reg.strobe();
        ar_ack_slow1_reg.strobe();
        ar_ack_slow2_reg.strobe();
        b_request_slow1_reg.strobe();
        b_request_slow2_reg.strobe();
        r_request_slow1_reg.strobe();
        r_request_slow2_reg.strobe();
        b_ack_slow_reg.strobe();
        r_ack_slow_reg.strobe();
    }

#ifndef SYNTHESIS
    void checkpoint(FILE* checkpoint_fd)
    {
        _strobe(checkpoint_fd);
        aw_addr_slow_reg.strobe(checkpoint_fd);
        aw_id_slow_reg.strobe(checkpoint_fd);
        w_data_slow_reg.strobe(checkpoint_fd);
        w_strb_slow_reg.strobe(checkpoint_fd);
        w_last_slow_reg.strobe(checkpoint_fd);
        ar_addr_slow_reg.strobe(checkpoint_fd);
        ar_id_slow_reg.strobe(checkpoint_fd);
        aw_request_slow_reg.strobe(checkpoint_fd);
        w_request_slow_reg.strobe(checkpoint_fd);
        ar_request_slow_reg.strobe(checkpoint_fd);
        aw_ack_slow1_reg.strobe(checkpoint_fd);
        aw_ack_slow2_reg.strobe(checkpoint_fd);
        w_ack_slow1_reg.strobe(checkpoint_fd);
        w_ack_slow2_reg.strobe(checkpoint_fd);
        ar_ack_slow1_reg.strobe(checkpoint_fd);
        ar_ack_slow2_reg.strobe(checkpoint_fd);
        b_request_slow1_reg.strobe(checkpoint_fd);
        b_request_slow2_reg.strobe(checkpoint_fd);
        r_request_slow1_reg.strobe(checkpoint_fd);
        r_request_slow2_reg.strobe(checkpoint_fd);
        b_ack_slow_reg.strobe(checkpoint_fd);
        r_ack_slow_reg.strobe(checkpoint_fd);
    }
#endif
};

// Bridges an AXI-like master in the primary clk domain to a responder in the
// l2_clock domain. Requests and responses use independent one-entry toggle
// mailboxes, so neither side depends on pulse width or a fixed clock ratio.
template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4FastToSlowCdc : public Module
{
public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> fast_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> slow_out;

private:
    reg<u<ADDR_WIDTH>> aw_addr_fast_reg;
    reg<u<ID_WIDTH>> aw_id_fast_reg;
    reg<logic<DATA_WIDTH>> w_data_fast_reg;
    reg<logic<DATA_WIDTH / 8>> w_strb_fast_reg;
    reg<u1> w_last_fast_reg;
    reg<u<ADDR_WIDTH>> ar_addr_fast_reg;
    reg<u<ID_WIDTH>> ar_id_fast_reg;
    reg<u1> aw_request_fast_reg;
    reg<u1> w_request_fast_reg;
    reg<u1> ar_request_fast_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_ack_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_ack_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_ack_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_ack_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_request_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_request_fast2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_request_fast1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_request_fast2_reg;
    reg<u1> b_ack_fast_reg;
    reg<u1> r_ack_fast_reg;
    reg<u1> write_outstanding_fast_reg;
    reg<u1> read_outstanding_fast_reg;
    reg<u1> aw_seen_fast_reg;
    reg<u1> ar_seen_fast_reg;

    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> aw_request_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> w_request_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_request_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> ar_request_slow2_reg;
    reg<u1> aw_ack_slow_reg;
    reg<u1> w_ack_slow_reg;
    reg<u1> ar_ack_slow_reg;
    reg<u<ID_WIDTH>> b_id_slow_reg;
    reg<logic<DATA_WIDTH>> r_data_slow_reg;
    reg<u1> r_last_slow_reg;
    reg<u<ID_WIDTH>> r_id_slow_reg;
    reg<u1> b_request_slow_reg;
    reg<u1> r_request_slow_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_ack_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> b_ack_slow2_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_ack_slow1_reg;
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> r_ack_slow2_reg;

public:
    void _assign()
    {
        fast_in.awready_out = _ASSIGN(!write_outstanding_fast_reg &&
            aw_request_fast_reg == aw_ack_fast2_reg &&
            (!aw_seen_fast_reg || !fast_in.awvalid_in() ||
             fast_in.awaddr_in() != aw_addr_fast_reg || fast_in.awid_in() != aw_id_fast_reg));
        fast_in.wready_out = _ASSIGN(w_request_fast_reg == w_ack_fast2_reg);
        fast_in.arready_out = _ASSIGN(!read_outstanding_fast_reg &&
            ar_request_fast_reg == ar_ack_fast2_reg &&
            (!ar_seen_fast_reg || !fast_in.arvalid_in() ||
             fast_in.araddr_in() != ar_addr_fast_reg || fast_in.arid_in() != ar_id_fast_reg));
        fast_in.bvalid_out = _ASSIGN(b_request_fast2_reg != b_ack_fast_reg);
        fast_in.bid_out = _ASSIGN((u<ID_WIDTH>)b_id_slow_reg);
        fast_in.rvalid_out = _ASSIGN(r_request_fast2_reg != r_ack_fast_reg);
        fast_in.rdata_out = _ASSIGN((logic<DATA_WIDTH>)r_data_slow_reg);
        fast_in.rlast_out = _ASSIGN((bool)r_last_slow_reg);
        fast_in.rid_out = _ASSIGN((u<ID_WIDTH>)r_id_slow_reg);

        slow_out.awvalid_in = _ASSIGN(aw_request_slow2_reg != aw_ack_slow_reg);
        slow_out.awaddr_in = _ASSIGN((u<ADDR_WIDTH>)aw_addr_fast_reg);
        slow_out.awid_in = _ASSIGN((u<ID_WIDTH>)aw_id_fast_reg);
        slow_out.wvalid_in = _ASSIGN(w_request_slow2_reg != w_ack_slow_reg);
        slow_out.wdata_in = _ASSIGN((logic<DATA_WIDTH>)w_data_fast_reg);
        slow_out.wstrb_in = _ASSIGN((logic<DATA_WIDTH / 8>)w_strb_fast_reg);
        slow_out.wlast_in = _ASSIGN((bool)w_last_fast_reg);
        slow_out.bready_in = _ASSIGN(b_request_slow_reg == b_ack_slow2_reg);
        slow_out.arvalid_in = _ASSIGN(ar_request_slow2_reg != ar_ack_slow_reg);
        slow_out.araddr_in = _ASSIGN((u<ADDR_WIDTH>)ar_addr_fast_reg);
        slow_out.arid_in = _ASSIGN((u<ID_WIDTH>)ar_id_fast_reg);
        slow_out.rready_in = _ASSIGN(r_request_slow_reg == r_ack_slow2_reg);
    }

    void work_clk_func(bool reset)
    {
        aw_ack_fast1_reg._next = aw_ack_slow_reg;
        aw_ack_fast2_reg._next = aw_ack_fast1_reg;
        w_ack_fast1_reg._next = w_ack_slow_reg;
        w_ack_fast2_reg._next = w_ack_fast1_reg;
        ar_ack_fast1_reg._next = ar_ack_slow_reg;
        ar_ack_fast2_reg._next = ar_ack_fast1_reg;
        b_request_fast1_reg._next = b_request_slow_reg;
        b_request_fast2_reg._next = b_request_fast1_reg;
        r_request_fast1_reg._next = r_request_slow_reg;
        r_request_fast2_reg._next = r_request_fast1_reg;

        if (fast_in.awvalid_in() && fast_in.awready_out()) {
            aw_addr_fast_reg._next = fast_in.awaddr_in();
            aw_id_fast_reg._next = fast_in.awid_in();
            aw_request_fast_reg._next = !aw_request_fast_reg;
            write_outstanding_fast_reg._next = true;
            aw_seen_fast_reg._next = true;
        }
        if (fast_in.wvalid_in() && fast_in.wready_out()) {
            w_data_fast_reg._next = fast_in.wdata_in();
            w_strb_fast_reg._next = fast_in.wstrb_in();
            w_last_fast_reg._next = fast_in.wlast_in();
            w_request_fast_reg._next = !w_request_fast_reg;
        }
        if (fast_in.arvalid_in() && fast_in.arready_out()) {
            ar_addr_fast_reg._next = fast_in.araddr_in();
            ar_id_fast_reg._next = fast_in.arid_in();
            ar_request_fast_reg._next = !ar_request_fast_reg;
            read_outstanding_fast_reg._next = true;
            ar_seen_fast_reg._next = true;
        }
        if (fast_in.bvalid_out() && fast_in.bready_in()) {
            b_ack_fast_reg._next = b_request_fast2_reg;
            write_outstanding_fast_reg._next = false;
        }
        if (fast_in.rvalid_out() && fast_in.rready_in()) {
            r_ack_fast_reg._next = r_request_fast2_reg;
            read_outstanding_fast_reg._next = false;
        }
        if (!fast_in.awvalid_in()) {
            aw_seen_fast_reg._next = false;
        }
        if (!fast_in.arvalid_in()) {
            ar_seen_fast_reg._next = false;
        }

        if (reset) {
            aw_addr_fast_reg.clr();
            aw_id_fast_reg.clr();
            w_data_fast_reg.clr();
            w_strb_fast_reg.clr();
            w_last_fast_reg.clr();
            ar_addr_fast_reg.clr();
            ar_id_fast_reg.clr();
            aw_request_fast_reg.clr();
            w_request_fast_reg.clr();
            ar_request_fast_reg.clr();
            aw_ack_fast1_reg.clr();
            aw_ack_fast2_reg.clr();
            w_ack_fast1_reg.clr();
            w_ack_fast2_reg.clr();
            ar_ack_fast1_reg.clr();
            ar_ack_fast2_reg.clr();
            b_request_fast1_reg.clr();
            b_request_fast2_reg.clr();
            r_request_fast1_reg.clr();
            r_request_fast2_reg.clr();
            b_ack_fast_reg.clr();
            r_ack_fast_reg.clr();
            write_outstanding_fast_reg.clr();
            read_outstanding_fast_reg.clr();
            aw_seen_fast_reg.clr();
            ar_seen_fast_reg.clr();
        }
    }

    void _work(bool reset) { work_clk_func(reset); }
    void _work_clk(bool reset) { work_clk_func(reset); }

    void _strobe_clk()
    {
        aw_addr_fast_reg.strobe();
        aw_id_fast_reg.strobe();
        w_data_fast_reg.strobe();
        w_strb_fast_reg.strobe();
        w_last_fast_reg.strobe();
        ar_addr_fast_reg.strobe();
        ar_id_fast_reg.strobe();
        aw_request_fast_reg.strobe();
        w_request_fast_reg.strobe();
        ar_request_fast_reg.strobe();
        aw_ack_fast1_reg.strobe();
        aw_ack_fast2_reg.strobe();
        w_ack_fast1_reg.strobe();
        w_ack_fast2_reg.strobe();
        ar_ack_fast1_reg.strobe();
        ar_ack_fast2_reg.strobe();
        b_request_fast1_reg.strobe();
        b_request_fast2_reg.strobe();
        r_request_fast1_reg.strobe();
        r_request_fast2_reg.strobe();
        b_ack_fast_reg.strobe();
        r_ack_fast_reg.strobe();
        write_outstanding_fast_reg.strobe();
        read_outstanding_fast_reg.strobe();
        aw_seen_fast_reg.strobe();
        ar_seen_fast_reg.strobe();
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        aw_addr_fast_reg.strobe(checkpoint_fd);
        aw_id_fast_reg.strobe(checkpoint_fd);
        w_data_fast_reg.strobe(checkpoint_fd);
        w_strb_fast_reg.strobe(checkpoint_fd);
        w_last_fast_reg.strobe(checkpoint_fd);
        ar_addr_fast_reg.strobe(checkpoint_fd);
        ar_id_fast_reg.strobe(checkpoint_fd);
        aw_request_fast_reg.strobe(checkpoint_fd);
        w_request_fast_reg.strobe(checkpoint_fd);
        ar_request_fast_reg.strobe(checkpoint_fd);
        aw_ack_fast1_reg.strobe(checkpoint_fd);
        aw_ack_fast2_reg.strobe(checkpoint_fd);
        w_ack_fast1_reg.strobe(checkpoint_fd);
        w_ack_fast2_reg.strobe(checkpoint_fd);
        ar_ack_fast1_reg.strobe(checkpoint_fd);
        ar_ack_fast2_reg.strobe(checkpoint_fd);
        b_request_fast1_reg.strobe(checkpoint_fd);
        b_request_fast2_reg.strobe(checkpoint_fd);
        r_request_fast1_reg.strobe(checkpoint_fd);
        r_request_fast2_reg.strobe(checkpoint_fd);
        b_ack_fast_reg.strobe(checkpoint_fd);
        r_ack_fast_reg.strobe(checkpoint_fd);
        write_outstanding_fast_reg.strobe(checkpoint_fd);
        read_outstanding_fast_reg.strobe(checkpoint_fd);
        aw_seen_fast_reg.strobe(checkpoint_fd);
        ar_seen_fast_reg.strobe(checkpoint_fd);
    }

    void _work_l2_clock(bool reset)
    {
        aw_request_slow1_reg._next = aw_request_fast_reg;
        aw_request_slow2_reg._next = aw_request_slow1_reg;
        w_request_slow1_reg._next = w_request_fast_reg;
        w_request_slow2_reg._next = w_request_slow1_reg;
        ar_request_slow1_reg._next = ar_request_fast_reg;
        ar_request_slow2_reg._next = ar_request_slow1_reg;
        b_ack_slow1_reg._next = b_ack_fast_reg;
        b_ack_slow2_reg._next = b_ack_slow1_reg;
        r_ack_slow1_reg._next = r_ack_fast_reg;
        r_ack_slow2_reg._next = r_ack_slow1_reg;

        if (slow_out.awvalid_in() && slow_out.awready_out()) {
            aw_ack_slow_reg._next = aw_request_slow2_reg;
        }
        if (slow_out.wvalid_in() && slow_out.wready_out()) {
            w_ack_slow_reg._next = w_request_slow2_reg;
        }
        if (slow_out.arvalid_in() && slow_out.arready_out()) {
            ar_ack_slow_reg._next = ar_request_slow2_reg;
        }
        if (slow_out.bvalid_out() && slow_out.bready_in()) {
            b_id_slow_reg._next = slow_out.bid_out();
            b_request_slow_reg._next = !b_request_slow_reg;
        }
        if (slow_out.rvalid_out() && slow_out.rready_in()) {
            r_data_slow_reg._next = slow_out.rdata_out();
            r_last_slow_reg._next = slow_out.rlast_out();
            r_id_slow_reg._next = slow_out.rid_out();
            r_request_slow_reg._next = !r_request_slow_reg;
        }

        if (reset) {
            aw_request_slow1_reg.clr();
            aw_request_slow2_reg.clr();
            w_request_slow1_reg.clr();
            w_request_slow2_reg.clr();
            ar_request_slow1_reg.clr();
            ar_request_slow2_reg.clr();
            aw_ack_slow_reg.clr();
            w_ack_slow_reg.clr();
            ar_ack_slow_reg.clr();
            b_id_slow_reg.clr();
            r_data_slow_reg.clr();
            r_last_slow_reg.clr();
            r_id_slow_reg.clr();
            b_request_slow_reg.clr();
            r_request_slow_reg.clr();
            b_ack_slow1_reg.clr();
            b_ack_slow2_reg.clr();
            r_ack_slow1_reg.clr();
            r_ack_slow2_reg.clr();
        }
    }

    void _strobe_l2_clock()
    {
        aw_request_slow1_reg.strobe();
        aw_request_slow2_reg.strobe();
        w_request_slow1_reg.strobe();
        w_request_slow2_reg.strobe();
        ar_request_slow1_reg.strobe();
        ar_request_slow2_reg.strobe();
        aw_ack_slow_reg.strobe();
        w_ack_slow_reg.strobe();
        ar_ack_slow_reg.strobe();
        b_id_slow_reg.strobe();
        r_data_slow_reg.strobe();
        r_last_slow_reg.strobe();
        r_id_slow_reg.strobe();
        b_request_slow_reg.strobe();
        r_request_slow_reg.strobe();
        b_ack_slow1_reg.strobe();
        b_ack_slow2_reg.strobe();
        r_ack_slow1_reg.strobe();
        r_ack_slow2_reg.strobe();
    }

#ifndef SYNTHESIS
    void checkpoint(FILE* checkpoint_fd)
    {
        _strobe(checkpoint_fd);
        aw_request_slow1_reg.strobe(checkpoint_fd);
        aw_request_slow2_reg.strobe(checkpoint_fd);
        w_request_slow1_reg.strobe(checkpoint_fd);
        w_request_slow2_reg.strobe(checkpoint_fd);
        ar_request_slow1_reg.strobe(checkpoint_fd);
        ar_request_slow2_reg.strobe(checkpoint_fd);
        aw_ack_slow_reg.strobe(checkpoint_fd);
        w_ack_slow_reg.strobe(checkpoint_fd);
        ar_ack_slow_reg.strobe(checkpoint_fd);
        b_id_slow_reg.strobe(checkpoint_fd);
        r_data_slow_reg.strobe(checkpoint_fd);
        r_last_slow_reg.strobe(checkpoint_fd);
        r_id_slow_reg.strobe(checkpoint_fd);
        b_request_slow_reg.strobe(checkpoint_fd);
        r_request_slow_reg.strobe(checkpoint_fd);
        b_ack_slow1_reg.strobe(checkpoint_fd);
        b_ack_slow2_reg.strobe(checkpoint_fd);
        r_ack_slow1_reg.strobe(checkpoint_fd);
        r_ack_slow2_reg.strobe(checkpoint_fd);
    }
#endif
};

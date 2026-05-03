#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "cpphdl.h"
#include <print>
#include <algorithm>

using namespace cpphdl;

#include <cstdint>
#include <type_traits>

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4If : Interface
{
    __PORT(bool)               awvalid_in;
    __PORT(bool)               awready_out;
    __PORT(u<ADDR_WIDTH>)      awaddr_in;
    __PORT(u<ID_WIDTH>)        awid_in;

    __PORT(bool)               wvalid_in;
    __PORT(bool)               wready_out;
    __PORT(logic<DATA_WIDTH>)  wdata_in;
    __PORT(bool)               wlast_in;

    __PORT(bool)               bvalid_out;
    __PORT(bool)               bready_in;
    __PORT(u<ID_WIDTH>)        bid_out;

    __PORT(bool)               arvalid_in;
    __PORT(bool)               arready_out;
    __PORT(u<ADDR_WIDTH>)      araddr_in;
    __PORT(u<ID_WIDTH>)        arid_in;

    __PORT(bool)               rvalid_out;
    __PORT(bool)               rready_in;
    __PORT(logic<DATA_WIDTH>)  rdata_out;
    __PORT(bool)               rlast_out;
    __PORT(u<ID_WIDTH>)        rid_out;
};

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4Mux : public Module
{
public:
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    slaves_in[N];
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    master_out;
private:
    // Round-robin pointers
    reg<u<clog2(N)>> rr_aw, rr_ar;

    // Active transaction tracking
    reg<u<clog2(N)>> aw_sel, ar_sel;
    reg<u1> aw_active, ar_active;

    // AW arbitration
    u<clog2(N)> aw_next_comb;
    auto& aw_next_comb_func()
    {
        u8 i, idx = rr_aw;
        for (i = 0; i < N; i++) {
            u8 candidate = (rr_aw + i) % N;
            if (slaves_in[candidate].awvalid_in()) {
                 idx = candidate;
                 break;
            }
        }
        return aw_next_comb = idx;
    }

    // AR arbitration
    u<clog2(N)> ar_next_comb;
    auto& ar_next_comb_func()
    {
        u8 i, idx = rr_ar;
        for (i = 0; i < N; i++) {
            u8 candidate = (rr_ar + i) % N;
            if (slaves_in[candidate].arvalid_in()) {
                 idx = candidate;
                 break;
            }
        }
        return ar_next_comb = idx;
    }

public:
    void _assign()
    {
        u8 i;

        master_out.awvalid_in = __EXPR( !aw_active && slaves_in[aw_next_comb_func()].awvalid_in() );
        master_out.awaddr_in  = __EXPR( slaves_in[aw_next_comb_func()].awaddr_in() );
        master_out.awid_in    = __EXPR( slaves_in[aw_next_comb_func()].awid_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].awready_out = __EXPR_I( (!aw_active && aw_next_comb_func() == i) ? master_out.awready_out() : 0 );
        }

        // W channel (follows AW)
        master_out.wvalid_in = __EXPR( aw_active ? slaves_in[aw_sel].wvalid_in() : 0 );
        master_out.wdata_in  = __EXPR( slaves_in[aw_sel].wdata_in() );
        master_out.wlast_in  = __EXPR( slaves_in[aw_sel].wlast_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].wready_out = __EXPR_I( (aw_active && aw_sel == i) ? master_out.wready_out() : 0 );
        }

        // B response routing
        master_out.bready_in = __EXPR( slaves_in[aw_sel].bready_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].bvalid_out = __EXPR_I( (aw_sel == i) ? master_out.bvalid_out() : 0 );
            slaves_in[i].bid_out    = master_out.bid_out;
        }

        master_out.arvalid_in = __EXPR( !ar_active && slaves_in[ar_next_comb_func()].arvalid_in() );
        master_out.araddr_in  = __EXPR( slaves_in[ar_next_comb_func()].araddr_in() );
        master_out.arid_in    = __EXPR( slaves_in[ar_next_comb_func()].arid_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].arready_out = __EXPR_I( (!ar_active && ar_next_comb_func() == i) ? master_out.arready_out() : 0 );
        }

        // R response routing
        master_out.rready_in = __EXPR( slaves_in[ar_sel].rready_in() );

        for (i = 0; i < N; i++) {
            slaves_in[i].rvalid_out = __EXPR_I( (ar_sel == i) ? master_out.rvalid_out() : 0 );
            slaves_in[i].rdata_out  = master_out.rdata_out;
            slaves_in[i].rlast_out  = master_out.rlast_out;
            slaves_in[i].rid_out    = master_out.rid_out;
        }
    }

    void _work(bool reset)
    {
        if (!ar_active && master_out.arvalid_in() && master_out.arready_out()) {
            ar_active._next = 1;
            ar_sel._next    = ar_next_comb_func();
            rr_ar._next     = ar_next_comb_func() + 1;
        }
        if (master_out.rvalid_out() && master_out.rready_in() && master_out.rlast_out()) {
            ar_active._next = 0;
        }

        if (!aw_active && master_out.awvalid_in() && master_out.awready_out()) {
            aw_active._next = 1;
            aw_sel._next    = aw_next_comb_func();
            rr_aw._next     = aw_next_comb_func() + 1;
        }
        if (master_out.bvalid_out() && master_out.bready_in()) {
            aw_active._next = 0;
        }

        if (reset) {
            ar_active._next = 0;
            rr_ar._next = 0;

            aw_active._next = 0;
            rr_aw._next = 0;
        }
    }


    void _strobe()
    {
        rr_aw.strobe();
        rr_ar.strobe();
        aw_sel.strobe();
        ar_sel.strobe();
        aw_active.strobe();
        ar_active.strobe();
    }

    bool     debugen_in;
};
/////////////////////////////////////////////////////////////////////////

// C++HDL INLINE TEST ///////////////////////////////////////////////////

template class Axi4Mux<4,32,8,128>;
template class Axi4Mux<8,64,16,512>;

#if !defined(SYNTHESIS) && !defined(NO_MAINFILE)

#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include <cstdlib>
#include "../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class TestAxi4Mux : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL mux;
#else
    Axi4Mux<N,ADDR_WIDTH,ID_WIDTH,DATA_WIDTH> mux;
#endif

    static constexpr size_t TX_PER_SLAVE = 16;

    reg<u1> s_awvalid[N], s_wvalid[N], s_wlast[N], s_bready[N];
    reg<u<ADDR_WIDTH>> s_awaddr[N];
    reg<u<ID_WIDTH>> s_awid[N];
    reg<logic<DATA_WIDTH>> s_wdata[N];

    reg<u1> s_arvalid[N], s_rready[N];
    reg<u<ADDR_WIDTH>> s_araddr[N];
    reg<u<ID_WIDTH>> s_arid[N];

    reg<u1> m_awready, m_wready, m_bvalid;
    reg<u<ID_WIDTH>> m_bid;
    reg<u1> m_arready, m_rvalid, m_rlast;
    reg<u<ID_WIDTH>> m_rid;
    reg<logic<DATA_WIDTH>> m_rdata;

    int write_phase[N];  // 0 idle, 1 AW, 2 W, 3 B
    int read_phase[N];   // 0 idle, 1 AR, 2 R
    size_t write_started[N], write_done[N], read_started[N], read_done[N];
    uint64_t write_addr[N], write_id[N], write_seq[N];
    uint64_t read_addr[N], read_id[N], read_seq[N];
    logic<DATA_WIDTH> write_data[N], read_data_ref[N];

    bool downstream_write_active;
    bool downstream_write_wait_b;
    size_t downstream_write_slave;
    uint64_t downstream_write_id;
    logic<DATA_WIDTH> downstream_write_data;
    int b_delay;

    bool downstream_read_wait_r;
    size_t downstream_read_slave;
    uint64_t downstream_read_id;
    logic<DATA_WIDTH> downstream_read_data;
    int r_delay;

    bool error;
    bool done;

public:

    bool debugen_in;

    TestAxi4Mux(bool debug)
    {
        debugen_in = debug;
    }

    ~TestAxi4Mux()
    {
    }

    logic<DATA_WIDTH> make_data(size_t slave, uint64_t seq, uint64_t addr, bool read) const
    {
        logic<DATA_WIDTH> ret = 0;
        for (size_t bit = 0; bit < DATA_WIDTH; bit += 32) {
            size_t hi = std::min(bit + 31, DATA_WIDTH - 1);
            uint32_t word = 0x51000000u
                ^ (read ? 0x00a50000u : 0x005a0000u)
                ^ ((uint32_t)slave << 20)
                ^ ((uint32_t)seq << 8)
                ^ ((uint32_t)(addr >> 2))
                ^ (uint32_t)(bit / 32);
            ret.bits(hi, bit) = word;
        }
        return ret;
    }

    bool all_done() const
    {
        for (size_t i = 0; i < N; ++i) {
            if (write_done[i] != TX_PER_SLAVE || read_done[i] != TX_PER_SLAVE) {
                return false;
            }
        }
        return !downstream_write_active && !downstream_write_wait_b && !downstream_read_wait_r;
    }

    void _assign()
    {
#ifndef VERILATOR
        mux.__inst_name = __inst_name + "/mux";

        for (size_t i = 0; i < N; ++i) {
            mux.slaves_in[i].awvalid_in = __VAR_I( s_awvalid[i] );
            mux.slaves_in[i].awaddr_in  = __VAR_I( s_awaddr[i] );
            mux.slaves_in[i].awid_in    = __VAR_I( s_awid[i] );
            mux.slaves_in[i].wvalid_in  = __VAR_I( s_wvalid[i] );
            mux.slaves_in[i].wdata_in   = __VAR_I( s_wdata[i] );
            mux.slaves_in[i].wlast_in   = __VAR_I( s_wlast[i] );
            mux.slaves_in[i].bready_in  = __VAR_I( s_bready[i] );

            mux.slaves_in[i].arvalid_in = __VAR_I( s_arvalid[i] );
            mux.slaves_in[i].araddr_in  = __VAR_I( s_araddr[i] );
            mux.slaves_in[i].arid_in    = __VAR_I( s_arid[i] );
            mux.slaves_in[i].rready_in  = __VAR_I( s_rready[i] );
        }

        mux.master_out.awready_out = __VAR( m_awready );
        mux.master_out.wready_out  = __VAR( m_wready );
        mux.master_out.bvalid_out  = __VAR( m_bvalid );
        mux.master_out.bid_out     = __VAR( m_bid );
        mux.master_out.arready_out = __VAR( m_arready );
        mux.master_out.rvalid_out  = __VAR( m_rvalid );
        mux.master_out.rdata_out   = __VAR( m_rdata );
        mux.master_out.rlast_out   = __VAR( m_rlast );
        mux.master_out.rid_out     = __VAR( m_rid );

        mux.debugen_in   = debugen_in;
        mux._assign();
#endif
    }

    void _work(bool reset)
    {
        if (reset) {
            error = false;
            done = false;
            downstream_write_active = false;
            downstream_write_wait_b = false;
            downstream_read_wait_r = false;
            b_delay = 0;
            r_delay = 0;
            m_awready.clr();
            m_wready.clr();
            m_bvalid.clr();
            m_bid.clr();
            m_arready.clr();
            m_rvalid.clr();
            m_rlast.clr();
            m_rid.clr();
            m_rdata.clr();
            for (size_t i = 0; i < N; ++i) {
                s_awvalid[i].clr();
                s_awaddr[i].clr();
                s_awid[i].clr();
                s_wvalid[i].clr();
                s_wdata[i].clr();
                s_wlast[i].clr();
                s_bready[i].clr();
                s_arvalid[i].clr();
                s_araddr[i].clr();
                s_arid[i].clr();
                s_rready[i].clr();
                write_phase[i] = 0;
                read_phase[i] = 0;
                write_started[i] = write_done[i] = 0;
                read_started[i] = read_done[i] = 0;
            }
#ifndef VERILATOR
            mux._work(reset);
#endif
            return;
        }

#ifdef VERILATOR
        // we're using this trick to update comb values of Verilator on it's outputs without strobing registers
        // the problem is that it's difficult to see 0-delayed memory output from Verilator
        // because if we write the same cycle Verilator updates combs in eval() and we see same clock written words
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
//        memcpy(mux.data_in, &out_reg, sizeof(mux.data_in));
        mux.clk = 0;
        mux.reset = 0;
        mux.eval();  // so lets update Verilator's combs without strobing registers
//        memcpy(&read_data, &mux.data_out, sizeof(read_data));
#else
#endif

#ifndef VERILATOR
        for (size_t i = 0; i < N; ++i) {
            s_awvalid[i].set();
            s_awaddr[i].set();
            s_awid[i].set();
            s_wvalid[i].set();
            s_wdata[i].set();
            s_wlast[i].set();
            s_bready[i].set();
            s_arvalid[i].set();
            s_araddr[i].set();
            s_arid[i].set();
            s_rready[i].set();
        }
        m_awready.set();
        m_wready.set();
        m_bvalid.set();
        m_bid.set();
        m_arready.set();
        m_rvalid.set();
        m_rlast.set();
        m_rid.set();
        m_rdata.set();

        size_t aw_slave = N;
        size_t ar_slave = N;

        for (size_t i = 0; i < N; ++i) {
            bool aw_hs = s_awvalid[i] && mux.slaves_in[i].awready_out();
            bool w_hs = s_wvalid[i] && mux.slaves_in[i].wready_out();
            bool b_hs = mux.slaves_in[i].bvalid_out() && s_bready[i];
            bool ar_hs = s_arvalid[i] && mux.slaves_in[i].arready_out();
            bool r_hs = mux.slaves_in[i].rvalid_out() && s_rready[i] && mux.slaves_in[i].rlast_out();

            if (mux.slaves_in[i].bvalid_out() && write_phase[i] != 3) {
                std::print("{:s} ERROR: unexpected B response on slave {}\n", __inst_name, i);
                error = true;
            }
            if (mux.slaves_in[i].rvalid_out() && read_phase[i] != 2) {
                std::print("{:s} ERROR: unexpected R response on slave {}\n", __inst_name, i);
                error = true;
            }

            if (aw_hs) {
                aw_slave = i;
                s_awvalid[i]._next = 0;
                s_wvalid[i]._next = 1;
                s_wlast[i]._next = 1;
                write_phase[i] = 2;
            }
            if (w_hs) {
                s_wvalid[i]._next = 0;
                write_phase[i] = 3;
            }
            if (b_hs) {
                if ((uint64_t)mux.slaves_in[i].bid_out() != write_id[i]) {
                    std::print("{:s} ERROR: slave {} got BID {} instead of {}\n",
                        __inst_name, i, mux.slaves_in[i].bid_out(), write_id[i]);
                    error = true;
                }
                write_phase[i] = 0;
                ++write_done[i];
            }

            if (ar_hs) {
                ar_slave = i;
                s_arvalid[i]._next = 0;
                read_phase[i] = 2;
            }
            if (r_hs) {
                if ((uint64_t)mux.slaves_in[i].rid_out() != read_id[i] || mux.slaves_in[i].rdata_out() != read_data_ref[i]) {
                    std::print("{:s} ERROR: slave {} got R id/data {}/{} instead of {}/{}\n",
                        __inst_name, i, mux.slaves_in[i].rid_out(), mux.slaves_in[i].rdata_out(),
                        read_id[i], read_data_ref[i]);
                    error = true;
                }
                read_phase[i] = 0;
                ++read_done[i];
            }
        }

        if (mux.master_out.awvalid_in() && m_awready) {
            if (aw_slave == N || downstream_write_active || downstream_write_wait_b) {
                std::print("{:s} ERROR: invalid downstream AW handshake aw_slave={} active={} wait_b={} m_awready={} master_awvalid={}\n",
                    __inst_name, aw_slave, downstream_write_active, downstream_write_wait_b,
                    (int)(bool)m_awready, (int)(bool)mux.master_out.awvalid_in());
                error = true;
            }
            else {
                downstream_write_active = true;
                downstream_write_slave = aw_slave;
                downstream_write_id = write_id[aw_slave];
                downstream_write_data = write_data[aw_slave];
                if ((uint64_t)mux.master_out.awid_in() != downstream_write_id
                    || (uint64_t)mux.master_out.awaddr_in() != write_addr[aw_slave]) {
                    std::print("{:s} ERROR: downstream AW id/addr {}/{} instead of {}/{}\n",
                        __inst_name, mux.master_out.awid_in(), mux.master_out.awaddr_in(),
                        downstream_write_id, write_addr[aw_slave]);
                    error = true;
                }
            }
        }
        if (mux.master_out.wvalid_in() && m_wready) {
            if (!downstream_write_active || !mux.master_out.wlast_in() || mux.master_out.wdata_in() != downstream_write_data) {
                std::print("{:s} ERROR: invalid downstream W beat\n", __inst_name);
                error = true;
            }
            downstream_write_active = false;
            downstream_write_wait_b = true;
            b_delay = random() % 4;
        }
        if (m_bvalid && mux.master_out.bready_in()) {
            m_bvalid._next = 0;
            downstream_write_wait_b = false;
        }
        else if (!m_bvalid && downstream_write_wait_b) {
            if (b_delay > 0) {
                --b_delay;
            }
            else {
                m_bvalid._next = 1;
                m_bid._next = downstream_write_id;
            }
        }

        if (mux.master_out.arvalid_in() && m_arready) {
            if (ar_slave == N || downstream_read_wait_r) {
                std::print("{:s} ERROR: invalid downstream AR handshake ar_slave={} wait_r={} m_arready={} master_arvalid={}\n",
                    __inst_name, ar_slave, downstream_read_wait_r,
                    (int)(bool)m_arready, (int)(bool)mux.master_out.arvalid_in());
                error = true;
            }
            else {
                downstream_read_wait_r = true;
                downstream_read_slave = ar_slave;
                downstream_read_id = read_id[ar_slave];
                downstream_read_data = read_data_ref[ar_slave];
                r_delay = random() % 4;
                if ((uint64_t)mux.master_out.arid_in() != downstream_read_id
                    || (uint64_t)mux.master_out.araddr_in() != read_addr[ar_slave]) {
                    std::print("{:s} ERROR: downstream AR id/addr {}/{} instead of {}/{}\n",
                        __inst_name, mux.master_out.arid_in(), mux.master_out.araddr_in(),
                        downstream_read_id, read_addr[ar_slave]);
                    error = true;
                }
            }
        }
        if (m_rvalid && mux.master_out.rready_in()) {
            m_rvalid._next = 0;
            m_rlast._next = 0;
            downstream_read_wait_r = false;
        }
        else if (!m_rvalid && downstream_read_wait_r) {
            if (r_delay > 0) {
                --r_delay;
            }
            else {
                m_rvalid._next = 1;
                m_rlast._next = 1;
                m_rid._next = downstream_read_id;
                m_rdata._next = downstream_read_data;
            }
        }

        for (size_t i = 0; i < N; ++i) {
            if (write_phase[i] == 0 && write_started[i] < TX_PER_SLAVE) {
                uint64_t seq = write_started[i]++;
                write_seq[i] = seq;
                write_addr[i] = (((uint64_t)i << 20) | (seq << 7) | 0x40) & ((ADDR_WIDTH == 64) ? ~0ULL : ((1ULL << ADDR_WIDTH) - 1));
                write_id[i] = ((i << 4) ^ seq) & ((1ULL << ID_WIDTH) - 1);
                write_data[i] = make_data(i, seq, write_addr[i], false);
                s_awaddr[i]._next = write_addr[i];
                s_awid[i]._next = write_id[i];
                s_wdata[i]._next = write_data[i];
                s_awvalid[i]._next = 1;
                write_phase[i] = 1;
            }
            if (read_phase[i] == 0 && read_started[i] < TX_PER_SLAVE) {
                uint64_t seq = read_started[i]++;
                read_seq[i] = seq;
                read_addr[i] = (((uint64_t)i << 21) | (seq << 8) | 0x80) & ((ADDR_WIDTH == 64) ? ~0ULL : ((1ULL << ADDR_WIDTH) - 1));
                read_id[i] = ((i << 5) ^ (seq << 1) ^ 1) & ((1ULL << ID_WIDTH) - 1);
                read_data_ref[i] = make_data(i, seq, read_addr[i], true);
                s_araddr[i]._next = read_addr[i];
                s_arid[i]._next = read_id[i];
                s_arvalid[i]._next = 1;
                read_phase[i] = 1;
            }

            s_bready[i]._next = write_phase[i] == 3;
            s_rready[i]._next = read_phase[i] == 2;
        }

        m_awready._next = (random() % 10) != 0;
        m_wready._next = (random() % 10) != 1;
        m_arready._next = (random() % 10) != 2;

        done = all_done();
        mux._work(reset);
#endif

#ifndef VERILATOR
#else
//        memcpy(mux.data_in, &out_reg, sizeof(mux.data_in));
        mux.debugen_in = debugen_in;

        mux.clk = 1;
        mux.reset = reset;
        mux.eval();  // eval of verilator should be in the end in 0-delay test
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        mux._strobe();
        for (size_t i = 0; i < N; ++i) {
            s_awvalid[i].strobe();
            s_awaddr[i].strobe();
            s_awid[i].strobe();
            s_wvalid[i].strobe();
            s_wdata[i].strobe();
            s_wlast[i].strobe();
            s_bready[i].strobe();
            s_arvalid[i].strobe();
            s_araddr[i].strobe();
            s_arid[i].strobe();
            s_rready[i].strobe();
        }
        m_awready.strobe();
        m_wready.strobe();
        m_bvalid.strobe();
        m_bid.strobe();
        m_arready.strobe();
        m_rvalid.strobe();
        m_rlast.strobe();
        m_rid.strobe();
        m_rdata.strobe();
#endif
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        mux.clk = 0;
        mux.reset = reset;
        mux.eval();  // eval of verilator should be in the end
#endif
    }

    void _strobe_neg()
    {
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestAxi4Mux, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
#else
        std::print("C++HDL TestAxi4Mux, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
#endif
        if (debugen_in) {
            std::print("\n");
        }

        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "mux_test";
        _assign();
        _work(1);
        _work_neg(1);

        int cycles = 100000;
        while (--cycles) {
            _strobe();
            ++sys_clock;
            _work(0);
            _strobe_neg();
            _work_neg(0);

            if (error) {
                break;
            }
            if (done) {
                break;
            }
        }
        if (!done) {
            std::print("\n{:s} ERROR: test did not complete all AXI transactions\n", __inst_name);
            error = true;
        }
        std::print(" {} ({} microseconds)\n", !error?"PASSED":"FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main (int argc, char** argv)
{
    bool debug = false;
    bool noveril = false;
    int only = -1;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[argc-1]);
        }
    }

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        auto start = std::chrono::high_resolution_clock::now();
        ok &= VerilatorCompile(__FILE__, "Axi4Mux", {"Predef_pkg"}, {"../../../../include"}, 4, 32, 8, 128);
        ok &= VerilatorCompile(__FILE__, "Axi4Mux", {"Predef_pkg"}, {"../../../../include"}, 8, 64, 16, 512);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Axi4Mux_4_32_8_128/obj_dir/VAxi4Mux") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 0) || std::system((std::string("Axi4Mux_8_64_16_512/obj_dir/VAxi4Mux") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestAxi4Mux<4,32,8,128>(debug).run())
        && ((only != -1 && only != 1) || TestAxi4Mux<8,64,16,512>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

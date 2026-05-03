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

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH> struct Axi4If;

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4Driver
{
    bool awvalid;
    u<ADDR_WIDTH> awaddr;
    u<ID_WIDTH> awid;

    bool wvalid;
    logic<DATA_WIDTH> wdata;
    bool wlast;

    bool bready;

    bool arvalid;
    u<ADDR_WIDTH> araddr;
    u<ID_WIDTH> arid;

    bool rready;

    Axi4Driver& operator=(const Axi4Driver&) = default;
};

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4Responder
{
    bool awready;
    bool wready;
    bool bvalid;
    u<ID_WIDTH> bid;

    bool arready;
    bool rvalid;
    logic<DATA_WIDTH> rdata;
    bool rlast;
    u<ID_WIDTH> rid;

    Axi4Responder& operator=(const Axi4Responder&) = default;
};

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

    Axi4If& operator=(Axi4Driver<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>& other)
    {
        Axi4Driver<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>* p = &other;
        awvalid_in = [p]() { return &p->awvalid; };
        awaddr_in = [p]() { return &p->awaddr; };
        awid_in = [p]() { return &p->awid; };

        wvalid_in = [p]() { return &p->wvalid; };
        wdata_in = [p]() { return &p->wdata; };
        wlast_in = [p]() { return &p->wlast; };

        bready_in = [p]() { return &p->bready; };

        arvalid_in = [p]() { return &p->arvalid; };
        araddr_in = [p]() { return &p->araddr; };
        arid_in = [p]() { return &p->arid; };

        rready_in = [p]() { return &p->rready; };
        return *this;
    }

    Axi4If& operator=(Axi4Responder<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>& other)
    {
        Axi4Responder<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>* p = &other;
        awready_out = [p]() { return &p->awready; };
        wready_out = [p]() { return &p->wready; };

        bvalid_out = [p]() { return &p->bvalid; };
        bid_out = [p]() { return &p->bid; };

        arready_out = [p]() { return &p->arready; };
        rvalid_out = [p]() { return &p->rvalid; };
        rdata_out = [p]() { return &p->rdata; };
        rlast_out = [p]() { return &p->rlast; };
        rid_out = [p]() { return &p->rid; };
        return *this;
    }
};

// C++HDL MODEL /////////////////////////////////////////////////////////

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4Mux : public Module
{
public:
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    master_out;
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    slaves_in[N];
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
        u8 i, idx;
        bool found;
        idx = rr_aw;
        found = false;
        for (i = 0; i < N; i++) {
            u8 candidate = (rr_aw + i) % N;
            if (!found && slaves_in[candidate].awvalid_in()) {
                 idx = candidate;
                 found = true;
            }
        }
        return aw_next_comb = idx;
    }

    // AR arbitration
    u<clog2(N)> ar_next_comb;
    auto& ar_next_comb_func()
    {
        u8 i, idx;
        bool found;
        idx = rr_ar;
        found = false;
        for (i = 0; i < N; i++) {
            u8 candidate = (rr_ar + i) % N;
            if (!found && slaves_in[candidate].arvalid_in()) {
                 idx = candidate;
                 found = true;
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
        if (master_out.rvalid_out() && slaves_in[ar_sel].rready_in() && master_out.rlast_out()) {
            ar_active._next = 0;
        }

        if (!aw_active && master_out.awvalid_in() && master_out.awready_out()) {
            aw_active._next = 1;
            aw_sel._next    = aw_next_comb_func();
            rr_aw._next     = aw_next_comb_func() + 1;
        }
        if (master_out.bvalid_out() && slaves_in[aw_sel].bready_in()) {
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
#include <cstring>
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
    reg<Axi4Driver<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>> s_slave[N];
    reg<Axi4Responder<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>> m_master;

    static constexpr size_t TX_PER_SLAVE = 16;

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

#ifdef VERILATOR
    void drive_verilator_inputs(bool reset, bool clk)
    {
        mux.clk = clk;
        mux.reset = reset;
        mux.debugen_in = debugen_in;

        mux.master_out___05Fawready_in = m_master.awready;
        mux.master_out___05Fwready_in = m_master.wready;
        mux.master_out___05Fbvalid_in = m_master.bvalid;
        mux.master_out___05Fbid_in = m_master.bid;
        mux.master_out___05Farready_in = m_master.arready;
        mux.master_out___05Frvalid_in = m_master.rvalid;
        mux.master_out___05Frlast_in = m_master.rlast;
        mux.master_out___05Frid_in = m_master.rid;
        logic<DATA_WIDTH> rdata = m_master.rdata;
        std::memcpy(&mux.master_out___05Frdata_in, &rdata, sizeof(rdata));

        for (size_t i = 0; i < N; ++i) {
            mux.slaves_in___05Fawvalid_in[i] = s_slave[i].awvalid;
            mux.slaves_in___05Fawaddr_in[i] = s_slave[i].awaddr;
            mux.slaves_in___05Fawid_in[i] = s_slave[i].awid;
            mux.slaves_in___05Fwvalid_in[i] = s_slave[i].wvalid;
            logic<DATA_WIDTH> wdata = s_slave[i].wdata;
            std::memcpy(&mux.slaves_in___05Fwdata_in[i], &wdata, sizeof(wdata));
            mux.slaves_in___05Fwlast_in[i] = s_slave[i].wlast;
            mux.slaves_in___05Fbready_in[i] = s_slave[i].bready;

            mux.slaves_in___05Farvalid_in[i] = s_slave[i].arvalid;
            mux.slaves_in___05Faraddr_in[i] = s_slave[i].araddr;
            mux.slaves_in___05Farid_in[i] = s_slave[i].arid;
            mux.slaves_in___05Frready_in[i] = s_slave[i].rready;
        }
        mux.eval();
    }
#endif

    bool slave_awready(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Fawready_out[i];
#else
        return mux.slaves_in[i].awready_out();
#endif
    }

    bool slave_wready(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Fwready_out[i];
#else
        return mux.slaves_in[i].wready_out();
#endif
    }

    bool slave_bvalid(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Fbvalid_out[i];
#else
        return mux.slaves_in[i].bvalid_out();
#endif
    }

    uint64_t slave_bid(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Fbid_out[i];
#else
        return mux.slaves_in[i].bid_out();
#endif
    }

    bool slave_arready(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Farready_out[i];
#else
        return mux.slaves_in[i].arready_out();
#endif
    }

    bool slave_rvalid(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Frvalid_out[i];
#else
        return mux.slaves_in[i].rvalid_out();
#endif
    }

    bool slave_rlast(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Frlast_out[i];
#else
        return mux.slaves_in[i].rlast_out();
#endif
    }

    uint64_t slave_rid(size_t i)
    {
#ifdef VERILATOR
        return mux.slaves_in___05Frid_out[i];
#else
        return mux.slaves_in[i].rid_out();
#endif
    }

    logic<DATA_WIDTH> slave_rdata(size_t i)
    {
#ifdef VERILATOR
        logic<DATA_WIDTH> ret;
        std::memcpy(&ret, &mux.slaves_in___05Frdata_out[i], sizeof(ret));
        return ret;
#else
        return mux.slaves_in[i].rdata_out();
#endif
    }

    bool master_awvalid()
    {
#ifdef VERILATOR
        return mux.master_out___05Fawvalid_out;
#else
        return mux.master_out.awvalid_in();
#endif
    }

    uint64_t master_awaddr()
    {
#ifdef VERILATOR
        return mux.master_out___05Fawaddr_out;
#else
        return mux.master_out.awaddr_in();
#endif
    }

    uint64_t master_awid()
    {
#ifdef VERILATOR
        return mux.master_out___05Fawid_out;
#else
        return mux.master_out.awid_in();
#endif
    }

    bool master_wvalid()
    {
#ifdef VERILATOR
        return mux.master_out___05Fwvalid_out;
#else
        return mux.master_out.wvalid_in();
#endif
    }

    bool master_wlast()
    {
#ifdef VERILATOR
        return mux.master_out___05Fwlast_out;
#else
        return mux.master_out.wlast_in();
#endif
    }

    logic<DATA_WIDTH> master_wdata()
    {
#ifdef VERILATOR
        logic<DATA_WIDTH> ret;
        std::memcpy(&ret, &mux.master_out___05Fwdata_out, sizeof(ret));
        return ret;
#else
        return mux.master_out.wdata_in();
#endif
    }

    bool master_bready()
    {
#ifdef VERILATOR
        return mux.master_out___05Fbready_out;
#else
        return mux.master_out.bready_in();
#endif
    }

    bool master_arvalid()
    {
#ifdef VERILATOR
        return mux.master_out___05Farvalid_out;
#else
        return mux.master_out.arvalid_in();
#endif
    }

    uint64_t master_araddr()
    {
#ifdef VERILATOR
        return mux.master_out___05Faraddr_out;
#else
        return mux.master_out.araddr_in();
#endif
    }

    uint64_t master_arid()
    {
#ifdef VERILATOR
        return mux.master_out___05Farid_out;
#else
        return mux.master_out.arid_in();
#endif
    }

    bool master_rready()
    {
#ifdef VERILATOR
        return mux.master_out___05Frready_out;
#else
        return mux.master_out.rready_in();
#endif
    }

    void debug_print_cycle()
    {
        if (!debugen_in) {
            return;
        }
        std::print("{:s}: awv={} awr={} awaddr={} awid={} wv={} wr={} bv={} br={} "
                   "arv={} arr={} araddr={} arid={} rv={} rr={} done={}\n",
            __inst_name,
            (int)master_awvalid(), (int)(bool)m_master.awready, master_awaddr(), master_awid(),
            (int)master_wvalid(), (int)(bool)m_master.wready,
            (int)(bool)m_master.bvalid, (int)master_bready(),
            (int)master_arvalid(), (int)(bool)m_master.arready, master_araddr(), master_arid(),
            (int)(bool)m_master.rvalid, (int)master_rready(), done);
    }

    size_t find_write_request(uint64_t addr, uint64_t id)
    {
        for (size_t i = 0; i < N; ++i) {
            if (write_phase[i] == 1 && write_addr[i] == addr && write_id[i] == id) {
                return i;
            }
        }
        return N;
    }

    size_t find_read_request(uint64_t addr, uint64_t id)
    {
        for (size_t i = 0; i < N; ++i) {
            if (read_phase[i] == 1 && read_addr[i] == addr && read_id[i] == id) {
                return i;
            }
        }
        return N;
    }

    void _assign()
    {
#ifndef VERILATOR
        mux.__inst_name = __inst_name + "/mux";
        for (size_t i = 0; i < N; ++i) {
            mux.slaves_in[i] = s_slave[i];
        }
        mux.master_out = m_master;

        mux.debugen_in = debugen_in;
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
            m_master.clr();
            for (size_t i = 0; i < N; ++i) {
                s_slave[i].clr();
                write_phase[i] = 0;
                read_phase[i] = 0;
                write_started[i] = write_done[i] = 0;
                read_started[i] = read_done[i] = 0;
            }
#ifndef VERILATOR
            mux._work(reset);
#else
            drive_verilator_inputs(reset, 1);
#endif
            return;
        }

#ifdef VERILATOR
        drive_verilator_inputs(0, 0);
#endif

        for (size_t i = 0; i < N; ++i) {
            s_slave[i].set();
        }
        m_master.set();

        size_t aw_slave = N;
        size_t w_slave = N;
        size_t ar_slave = N;
        bool write_completed_this_cycle[N] = {};
        bool read_completed_this_cycle[N] = {};

        for (size_t i = 0; i < N; ++i) {
            bool aw_hs = s_slave[i].awvalid && slave_awready(i);
            bool w_hs = s_slave[i].wvalid && slave_wready(i);
            bool b_hs = slave_bvalid(i) && s_slave[i].bready;
            bool ar_hs = s_slave[i].arvalid && slave_arready(i);
            bool r_hs = slave_rvalid(i) && s_slave[i].rready && slave_rlast(i);

            if (slave_bvalid(i) && write_phase[i] != 3) {
                std::print("{:s} ERROR: unexpected B response on slave {}\n", __inst_name, i);
                error = true;
            }
            if (slave_rvalid(i) && read_phase[i] != 2) {
                std::print("{:s} ERROR: unexpected R response on slave {}\n", __inst_name, i);
                error = true;
            }

            if (aw_hs) {
                aw_slave = i;
            }
            if (w_hs) {
                w_slave = i;
                s_slave[i]._next.wvalid = 0;
                write_phase[i] = 3;
            }
            if (b_hs) {
                if (slave_bid(i) != write_id[i]) {
                    std::print("{:s} ERROR: slave {} got BID {} instead of {}\n",
                        __inst_name, i, slave_bid(i), write_id[i]);
                    error = true;
                }
                write_phase[i] = 0;
                ++write_done[i];
                write_completed_this_cycle[i] = true;
            }

            if (ar_hs) {
                ar_slave = i;
            }
            if (r_hs) {
                if (slave_rid(i) != read_id[i] || slave_rdata(i) != read_data_ref[i]) {
                    std::print("{:s} ERROR: slave {} got R id/data {}/{} instead of {}/{}\n",
                        __inst_name, i, slave_rid(i), slave_rdata(i),
                        read_id[i], read_data_ref[i]);
                    error = true;
                }
                read_phase[i] = 0;
                ++read_done[i];
                read_completed_this_cycle[i] = true;
            }
        }

        if (master_awvalid() && m_master.awready) {
            aw_slave = find_write_request(master_awaddr(), master_awid());
            if (aw_slave == N || downstream_write_active || downstream_write_wait_b) {
                std::print("{:s} ERROR: invalid downstream AW handshake aw_slave={} active={} wait_b={} m_master.awready={} master_awvalid={}\n",
                    __inst_name, aw_slave, downstream_write_active, downstream_write_wait_b,
                    (int)(bool)m_master.awready, (int)master_awvalid());
                error = true;
            }
            else {
                s_slave[aw_slave]._next.awvalid = 0;
                s_slave[aw_slave]._next.wvalid = 1;
                s_slave[aw_slave]._next.wlast = 1;
                write_phase[aw_slave] = 2;
                downstream_write_active = true;
                downstream_write_slave = aw_slave;
                downstream_write_id = write_id[aw_slave];
                downstream_write_data = write_data[aw_slave];
                if (master_awid() != downstream_write_id
                    || master_awaddr() != write_addr[aw_slave]) {
                    std::print("{:s} ERROR: downstream AW id/addr {}/{} instead of {}/{}\n",
                        __inst_name, master_awid(), master_awaddr(),
                        downstream_write_id, write_addr[aw_slave]);
                    error = true;
                }
            }
        }
        if (w_slave != N) {
            if (!downstream_write_active || !master_wlast() || master_wdata() != downstream_write_data) {
                std::print("{:s} ERROR: invalid downstream W beat active={} wlast={} wdata={} expected={} w_slave={} stored_slave={}\n",
                    __inst_name, downstream_write_active, (int)master_wlast(), master_wdata(),
                    downstream_write_data, w_slave, downstream_write_slave);
                error = true;
            }
            downstream_write_active = false;
            downstream_write_wait_b = true;
            b_delay = random() % 4;
        }
        if (m_master.bvalid && master_bready()) {
            m_master._next.bvalid = 0;
            downstream_write_wait_b = false;
        }
        else if (!m_master.bvalid && downstream_write_wait_b) {
            if (b_delay > 0) {
                --b_delay;
            }
            else {
                m_master._next.bvalid = 1;
                m_master._next.bid = downstream_write_id;
            }
        }

        if (master_arvalid() && m_master.arready) {
            ar_slave = find_read_request(master_araddr(), master_arid());
            if (ar_slave == N || downstream_read_wait_r) {
                std::print("{:s} ERROR: invalid downstream AR handshake ar_slave={} wait_r={} m_master.arready={} master_arvalid={}\n",
                    __inst_name, ar_slave, downstream_read_wait_r,
                    (int)(bool)m_master.arready, (int)master_arvalid());
                error = true;
            }
            else {
                s_slave[ar_slave]._next.arvalid = 0;
                read_phase[ar_slave] = 2;
                downstream_read_wait_r = true;
                downstream_read_slave = ar_slave;
                downstream_read_id = read_id[ar_slave];
                downstream_read_data = read_data_ref[ar_slave];
                r_delay = random() % 4;
                if (master_arid() != downstream_read_id
                    || master_araddr() != read_addr[ar_slave]) {
                    std::print("{:s} ERROR: downstream AR id/addr {}/{} instead of {}/{}\n",
                        __inst_name, master_arid(), master_araddr(),
                        downstream_read_id, read_addr[ar_slave]);
                    error = true;
                }
            }
        }
        if (m_master.rvalid && master_rready()) {
            m_master._next.rvalid = 0;
            m_master._next.rlast = 0;
            downstream_read_wait_r = false;
        }
        else if (!m_master.rvalid && downstream_read_wait_r) {
            if (r_delay > 0) {
                --r_delay;
            }
            else {
                m_master._next.rvalid = 1;
                m_master._next.rlast = 1;
                m_master._next.rid = downstream_read_id;
                m_master._next.rdata = downstream_read_data;
            }
        }

        for (size_t i = 0; i < N; ++i) {
            if (!write_completed_this_cycle[i] && write_phase[i] == 0 && write_started[i] < TX_PER_SLAVE) {
                uint64_t seq = write_started[i]++;
                write_seq[i] = seq;
                write_addr[i] = (((uint64_t)i << 20) | (seq << 7) | 0x40) & ((ADDR_WIDTH == 64) ? ~0ULL : ((1ULL << ADDR_WIDTH) - 1));
                write_id[i] = ((i << 4) ^ seq) & ((1ULL << ID_WIDTH) - 1);
                write_data[i] = make_data(i, seq, write_addr[i], false);
                s_slave[i]._next.awaddr = write_addr[i];
                s_slave[i]._next.awid = write_id[i];
                s_slave[i]._next.wdata = write_data[i];
                s_slave[i]._next.awvalid = 1;
                write_phase[i] = 1;
            }
            if (!read_completed_this_cycle[i] && read_phase[i] == 0 && read_started[i] < TX_PER_SLAVE) {
                uint64_t seq = read_started[i]++;
                read_seq[i] = seq;
                read_addr[i] = (((uint64_t)i << 21) | (seq << 8) | 0x80) & ((ADDR_WIDTH == 64) ? ~0ULL : ((1ULL << ADDR_WIDTH) - 1));
                read_id[i] = ((i << 5) ^ (seq << 1) ^ 1) & ((1ULL << ID_WIDTH) - 1);
                read_data_ref[i] = make_data(i, seq, read_addr[i], true);
                s_slave[i]._next.araddr = read_addr[i];
                s_slave[i]._next.arid = read_id[i];
                s_slave[i]._next.arvalid = 1;
                read_phase[i] = 1;
            }

            s_slave[i]._next.bready = 1;
            s_slave[i]._next.rready = read_phase[i] == 2;
        }

        m_master._next.awready = (random() % 10) != 0;
        m_master._next.wready = (random() % 10) != 1;
        m_master._next.arready = (random() % 10) != 2;

        done = all_done();
        debug_print_cycle();
#ifndef VERILATOR
        mux._work(reset);
#else
        drive_verilator_inputs(reset, 1);
#endif
    }

    void _strobe()
    {
#ifndef VERILATOR
        mux._strobe();
#endif
        for (size_t i = 0; i < N; ++i) {
            s_slave[i].strobe();
        }
        m_master.strobe();
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
            for (size_t i = 0; i < N; ++i) {
                std::print("{:s}: slave {} write_started={} write_done={} write_phase={} read_started={} read_done={} read_phase={}\n",
                    __inst_name, i, write_started[i], write_done[i], write_phase[i],
                    read_started[i], read_done[i], read_phase[i]);
            }
            std::print("{:s}: downstream write_active={} write_wait_b={} read_wait_r={} bvalid={} rvalid={}\n",
                __inst_name, downstream_write_active, downstream_write_wait_b, downstream_read_wait_r,
                (int)(bool)m_master.bvalid, (int)(bool)m_master.rvalid);
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
            && ((only != -1 && only != 1) || std::system((std::string("Axi4Mux_8_64_16_512/obj_dir/VAxi4Mux") + (debug?" --debug":"") + " 1").c_str()) == 0)
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

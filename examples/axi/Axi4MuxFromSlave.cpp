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

#include "Axi4.h"

// CppHDL MODEL /////////////////////////////////////////////////////////

template<size_t N, size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
class Axi4MuxFromSlave : public Module
{
public:
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    slave_in;
    Axi4If<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>    masters_out[N];
private:
    // Active transaction tracking
    reg<u<clog2(N)>> aw_sel, ar_sel;
    reg<u1> aw_active, ar_active;

    bool awready_comb;
    auto& awready_comb_func()
    {
        u8 i;
        bool ret;
        ret = false;
        for (i = 0; i < N; i++) {
            if ((slave_in.awaddr_in() % N) == i) {
                ret = masters_out[i].awready_out();
            }
        }
        return awready_comb = ret;
    }

    bool arready_comb;
    auto& arready_comb_func()
    {
        u8 i;
        bool ret;
        ret = false;
        for (i = 0; i < N; i++) {
            if ((slave_in.araddr_in() % N) == i) {
                ret = masters_out[i].arready_out();
            }
        }
        return arready_comb = ret;
    }

public:
    void _assign()
    {
        u8 i;

        for (i = 0; i < N; i++) {
            masters_out[i].awvalid_in = _ASSIGN_I( (!aw_active && (slave_in.awaddr_in() % N) == i) ? slave_in.awvalid_in() : 0 );
            masters_out[i].awaddr_in  = slave_in.awaddr_in;
            masters_out[i].awid_in    = slave_in.awid_in;
        }
        slave_in.awready_out = _ASSIGN( !aw_active ? awready_comb_func() : 0 );

        // W channel (follows AW)
        for (i = 0; i < N; i++) {
            masters_out[i].wvalid_in = _ASSIGN_I( (aw_active && aw_sel == i) ? slave_in.wvalid_in() : 0 );
            masters_out[i].wdata_in  = slave_in.wdata_in;
            masters_out[i].wlast_in  = slave_in.wlast_in;
        }
        slave_in.wready_out = _ASSIGN( aw_active ? masters_out[aw_sel].wready_out() : 0 );

        // B response routing
        for (i = 0; i < N; i++) {
            masters_out[i].bready_in = _ASSIGN_I( (aw_sel == i) ? slave_in.bready_in() : 0 );
        }
        slave_in.bvalid_out = _ASSIGN( aw_active ? masters_out[aw_sel].bvalid_out() : 0 );
        slave_in.bid_out    = _ASSIGN( masters_out[aw_sel].bid_out() );

        for (i = 0; i < N; i++) {
            masters_out[i].arvalid_in = _ASSIGN_I( (!ar_active && (slave_in.araddr_in() % N) == i) ? slave_in.arvalid_in() : 0 );
            masters_out[i].araddr_in  = slave_in.araddr_in;
            masters_out[i].arid_in    = slave_in.arid_in;
        }
        slave_in.arready_out = _ASSIGN( !ar_active ? arready_comb_func() : 0 );

        // R response routing
        for (i = 0; i < N; i++) {
            masters_out[i].rready_in = _ASSIGN_I( (ar_sel == i) ? slave_in.rready_in() : 0 );
        }
        slave_in.rvalid_out = _ASSIGN( ar_active ? masters_out[ar_sel].rvalid_out() : 0 );
        slave_in.rdata_out  = _ASSIGN( masters_out[ar_sel].rdata_out() );
        slave_in.rlast_out  = _ASSIGN( masters_out[ar_sel].rlast_out() );
        slave_in.rid_out    = _ASSIGN( masters_out[ar_sel].rid_out() );
    }

    void _work(bool reset)
    {
        if (!ar_active && slave_in.arvalid_in() && slave_in.arready_out()) {
            ar_active._next = 1;
            ar_sel._next    = slave_in.araddr_in() % N;
        }
        if (slave_in.rvalid_out() && slave_in.rready_in() && slave_in.rlast_out()) {
            ar_active._next = 0;
        }

        if (!aw_active && slave_in.awvalid_in() && slave_in.awready_out()) {
            aw_active._next = 1;
            aw_sel._next    = slave_in.awaddr_in() % N;
        }
        if (slave_in.bvalid_out() && slave_in.bready_in()) {
            aw_active._next = 0;
        }

        if (reset) {
            ar_active._next = 0;

            aw_active._next = 0;
        }
    }


    void _strobe()
    {
        aw_sel.strobe();
        ar_sel.strobe();
        aw_active.strobe();
        ar_active.strobe();
    }

    bool     debugen_in;
};
/////////////////////////////////////////////////////////////////////////

// CppHDL INLINE TEST ///////////////////////////////////////////////////

template class Axi4MuxFromSlave<4,32,8,128>;
template class Axi4MuxFromSlave<8,64,16,512>;

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
class TestAxi4MuxFromSlave : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL mux;
#else
    Axi4MuxFromSlave<N,ADDR_WIDTH,ID_WIDTH,DATA_WIDTH> mux;
#endif
    reg<Axi4Driver<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>> m_master;
    reg<Axi4Responder<ADDR_WIDTH,ID_WIDTH,DATA_WIDTH>> s_slave[N];

    static constexpr size_t TX_PER_SLAVE = 16;

    int write_phase;  // 0 idle, 1 AW, 2 W, 3 B
    int read_phase;   // 0 idle, 1 AR, 2 R
    size_t write_started[N], write_done[N], read_started[N], read_done[N];
    size_t write_rr, read_rr;
    size_t write_target, read_target;
    uint64_t write_addr, write_id, write_seq;
    uint64_t read_addr, read_id, read_seq;
    logic<DATA_WIDTH> write_data, read_data_ref;

    bool slave_write_active[N];
    bool slave_write_wait_b[N];
    uint64_t slave_write_id[N];
    logic<DATA_WIDTH> slave_write_data[N];
    int b_delay[N];

    bool slave_read_wait_r[N];
    uint64_t slave_read_id[N];
    logic<DATA_WIDTH> slave_read_data[N];
    int r_delay[N];

    bool error;
    bool done;

public:

    bool debugen_in;

    TestAxi4MuxFromSlave(bool debug)
    {
        debugen_in = debug;
    }

    ~TestAxi4MuxFromSlave()
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
            if (slave_write_active[i] || slave_write_wait_b[i] || slave_read_wait_r[i]) {
                return false;
            }
        }
        return write_phase == 0 && read_phase == 0;
    }

#ifdef VERILATOR
    void drive_verilator_inputs(bool reset, bool clk)
    {
        mux.clk = clk;
        mux.reset = reset;
        mux.debugen_in = debugen_in;

        mux.slave_in___05Fawvalid_in = m_master.awvalid;
        mux.slave_in___05Fawaddr_in = m_master.awaddr;
        mux.slave_in___05Fawid_in = m_master.awid;
        mux.slave_in___05Fwvalid_in = m_master.wvalid;
        logic<DATA_WIDTH> wdata = m_master.wdata;
        std::memcpy(&mux.slave_in___05Fwdata_in, &wdata, sizeof(wdata));
        mux.slave_in___05Fwlast_in = m_master.wlast;
        mux.slave_in___05Fbready_in = m_master.bready;
        mux.slave_in___05Farvalid_in = m_master.arvalid;
        mux.slave_in___05Faraddr_in = m_master.araddr;
        mux.slave_in___05Farid_in = m_master.arid;
        mux.slave_in___05Frready_in = m_master.rready;

        for (size_t i = 0; i < N; ++i) {
            mux.masters_out___05Fawready_in[i] = s_slave[i].awready;
            mux.masters_out___05Fwready_in[i] = s_slave[i].wready;
            mux.masters_out___05Fbvalid_in[i] = s_slave[i].bvalid;
            mux.masters_out___05Fbid_in[i] = s_slave[i].bid;
            mux.masters_out___05Farready_in[i] = s_slave[i].arready;
            mux.masters_out___05Frvalid_in[i] = s_slave[i].rvalid;
            logic<DATA_WIDTH> rdata = s_slave[i].rdata;
            std::memcpy(&mux.masters_out___05Frdata_in[i], &rdata, sizeof(rdata));
            mux.masters_out___05Frlast_in[i] = s_slave[i].rlast;
            mux.masters_out___05Frid_in[i] = s_slave[i].rid;
        }
        mux.eval();
    }
#endif

    bool driver_awready()
    {
#ifdef VERILATOR
        return mux.slave_in___05Fawready_out;
#else
        return mux.slave_in.awready_out();
#endif
    }

    bool driver_wready()
    {
#ifdef VERILATOR
        return mux.slave_in___05Fwready_out;
#else
        return mux.slave_in.wready_out();
#endif
    }

    bool driver_bvalid()
    {
#ifdef VERILATOR
        return mux.slave_in___05Fbvalid_out;
#else
        return mux.slave_in.bvalid_out();
#endif
    }

    uint64_t driver_bid()
    {
#ifdef VERILATOR
        return mux.slave_in___05Fbid_out;
#else
        return mux.slave_in.bid_out();
#endif
    }

    bool driver_arready()
    {
#ifdef VERILATOR
        return mux.slave_in___05Farready_out;
#else
        return mux.slave_in.arready_out();
#endif
    }

    bool driver_rvalid()
    {
#ifdef VERILATOR
        return mux.slave_in___05Frvalid_out;
#else
        return mux.slave_in.rvalid_out();
#endif
    }

    bool driver_rlast()
    {
#ifdef VERILATOR
        return mux.slave_in___05Frlast_out;
#else
        return mux.slave_in.rlast_out();
#endif
    }

    uint64_t driver_rid()
    {
#ifdef VERILATOR
        return mux.slave_in___05Frid_out;
#else
        return mux.slave_in.rid_out();
#endif
    }

    logic<DATA_WIDTH> driver_rdata()
    {
#ifdef VERILATOR
        logic<DATA_WIDTH> ret;
        std::memcpy(&ret, &mux.slave_in___05Frdata_out, sizeof(ret));
        return ret;
#else
        return mux.slave_in.rdata_out();
#endif
    }

    bool out_awvalid(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fawvalid_out[i];
#else
        return mux.masters_out[i].awvalid_in();
#endif
    }

    uint64_t out_awaddr(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fawaddr_out[i];
#else
        return mux.masters_out[i].awaddr_in();
#endif
    }

    uint64_t out_awid(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fawid_out[i];
#else
        return mux.masters_out[i].awid_in();
#endif
    }

    bool out_wvalid(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fwvalid_out[i];
#else
        return mux.masters_out[i].wvalid_in();
#endif
    }

    bool out_wlast(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fwlast_out[i];
#else
        return mux.masters_out[i].wlast_in();
#endif
    }

    logic<DATA_WIDTH> out_wdata(size_t i)
    {
#ifdef VERILATOR
        logic<DATA_WIDTH> ret;
        std::memcpy(&ret, &mux.masters_out___05Fwdata_out[i], sizeof(ret));
        return ret;
#else
        return mux.masters_out[i].wdata_in();
#endif
    }

    bool out_bready(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Fbready_out[i];
#else
        return mux.masters_out[i].bready_in();
#endif
    }

    bool out_arvalid(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Farvalid_out[i];
#else
        return mux.masters_out[i].arvalid_in();
#endif
    }

    uint64_t out_araddr(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Faraddr_out[i];
#else
        return mux.masters_out[i].araddr_in();
#endif
    }

    uint64_t out_arid(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Farid_out[i];
#else
        return mux.masters_out[i].arid_in();
#endif
    }

    bool out_rready(size_t i)
    {
#ifdef VERILATOR
        return mux.masters_out___05Frready_out[i];
#else
        return mux.masters_out[i].rready_in();
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
            (int)(bool)m_master.awvalid, (int)driver_awready(), (uint64_t)m_master.awaddr, (uint64_t)m_master.awid,
            (int)(bool)m_master.wvalid, (int)driver_wready(),
            (int)driver_bvalid(), (int)(bool)m_master.bready,
            (int)(bool)m_master.arvalid, (int)driver_arready(), (uint64_t)m_master.araddr, (uint64_t)m_master.arid,
            (int)driver_rvalid(), (int)(bool)m_master.rready, done);
    }

    size_t target_from_addr(uint64_t addr) const
    {
        return addr & (N - 1);
    }

    uint64_t addr_for(size_t slave, uint64_t seq, bool read) const
    {
        uint64_t addr = ((seq << 8) | (read ? 0x80 : 0x40) | slave);
        if constexpr (ADDR_WIDTH == 64) {
            return addr;
        }
        else {
            return addr & ((1ULL << ADDR_WIDTH) - 1);
        }
    }

    void _assign()
    {
#ifndef VERILATOR
        mux.__inst_name = __inst_name + "/mux";
        mux.slave_in = m_master;
        for (size_t i = 0; i < N; ++i) {
            mux.masters_out[i] = s_slave[i];
        }

        mux.debugen_in = debugen_in;
        mux._assign();
#endif
    }

    void _work(bool reset)
    {
        if (reset) {
            error = false;
            done = false;
            m_master.clr();
            write_phase = 0;
            read_phase = 0;
            write_rr = 0;
            read_rr = 0;
            write_target = 0;
            read_target = 0;
            for (size_t i = 0; i < N; ++i) {
                s_slave[i].clr();
                write_started[i] = write_done[i] = 0;
                read_started[i] = read_done[i] = 0;
                slave_write_active[i] = false;
                slave_write_wait_b[i] = false;
                slave_read_wait_r[i] = false;
                b_delay[i] = 0;
                r_delay[i] = 0;
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

        bool write_completed = false;
        bool read_completed = false;

        for (size_t i = 0; i < N; ++i) {
            bool aw_hs = out_awvalid(i) && s_slave[i].awready;
            bool w_hs = out_wvalid(i) && s_slave[i].wready;
            bool b_hs = s_slave[i].bvalid && out_bready(i);
            bool ar_hs = out_arvalid(i) && s_slave[i].arready;
            bool r_hs = s_slave[i].rvalid && out_rready(i) && s_slave[i].rlast;

            if (aw_hs) {
                if (target_from_addr(out_awaddr(i)) != i || i != write_target
                    || out_awid(i) != write_id || slave_write_active[i] || slave_write_wait_b[i]) {
                    std::print("{:s} ERROR: invalid routed AW slave={} addr={} id={} target={} expected_id={} active={} wait_b={}\n",
                        __inst_name, i, out_awaddr(i), out_awid(i), write_target, write_id,
                        slave_write_active[i], slave_write_wait_b[i]);
                    error = true;
                }
                slave_write_active[i] = true;
                slave_write_id[i] = out_awid(i);
            }
            if (w_hs) {
                if (!slave_write_active[i] || i != write_target || !out_wlast(i) || out_wdata(i) != write_data) {
                    std::print("{:s} ERROR: invalid routed W slave={} active={} wlast={} wdata={} expected={}\n",
                        __inst_name, i, slave_write_active[i], (int)out_wlast(i), out_wdata(i), write_data);
                    error = true;
                }
                slave_write_data[i] = out_wdata(i);
                slave_write_active[i] = false;
                slave_write_wait_b[i] = true;
                b_delay[i] = random() % 4;
            }
            if (b_hs) {
                s_slave[i]._next.bvalid = 0;
            }
            else if (!s_slave[i].bvalid && slave_write_wait_b[i]) {
                if (b_delay[i] > 0) {
                    --b_delay[i];
                }
                else {
                    s_slave[i]._next.bvalid = 1;
                    s_slave[i]._next.bid = slave_write_id[i];
                    slave_write_wait_b[i] = false;
                }
            }

            if (ar_hs) {
                if (target_from_addr(out_araddr(i)) != i || i != read_target
                    || out_arid(i) != read_id || slave_read_wait_r[i]) {
                    std::print("{:s} ERROR: invalid routed AR slave={} addr={} id={} target={} expected_id={} wait_r={}\n",
                        __inst_name, i, out_araddr(i), out_arid(i), read_target, read_id, slave_read_wait_r[i]);
                    error = true;
                }
                slave_read_id[i] = out_arid(i);
                slave_read_data[i] = read_data_ref;
                slave_read_wait_r[i] = true;
                r_delay[i] = random() % 4;
            }
            if (r_hs) {
                s_slave[i]._next.rvalid = 0;
                s_slave[i]._next.rlast = 0;
            }
            else if (!s_slave[i].rvalid && slave_read_wait_r[i]) {
                if (r_delay[i] > 0) {
                    --r_delay[i];
                }
                else {
                    s_slave[i]._next.rvalid = 1;
                    s_slave[i]._next.rlast = 1;
                    s_slave[i]._next.rid = slave_read_id[i];
                    s_slave[i]._next.rdata = slave_read_data[i];
                    slave_read_wait_r[i] = false;
                }
            }
        }

        if (m_master.awvalid && driver_awready()) {
            m_master._next.awvalid = 0;
            m_master._next.wvalid = 1;
            m_master._next.wlast = 1;
            write_phase = 2;
        }
        if (m_master.wvalid && driver_wready()) {
            m_master._next.wvalid = 0;
            write_phase = 3;
        }
        if (write_phase == 3 && driver_bvalid() && m_master.bready) {
            if (driver_bid() != write_id) {
                std::print("{:s} ERROR: driver got BID {} instead of {}\n",
                    __inst_name, driver_bid(), write_id);
                error = true;
            }
            write_phase = 0;
            ++write_done[write_target];
            write_completed = true;
        }
        if (m_master.arvalid && driver_arready()) {
            m_master._next.arvalid = 0;
            read_phase = 2;
        }
        if (read_phase == 2 && driver_rvalid() && m_master.rready && driver_rlast()) {
            if (driver_rid() != read_id || driver_rdata() != read_data_ref) {
                std::print("{:s} ERROR: driver got R id/data {}/{} instead of {}/{}\n",
                    __inst_name, driver_rid(), driver_rdata(), read_id, read_data_ref);
                error = true;
            }
            read_phase = 0;
            ++read_done[read_target];
            read_completed = true;
        }

        if (!write_completed && write_phase == 0 && !driver_bvalid()) {
            for (size_t tries = 0; tries < N; ++tries) {
                size_t target = (write_rr + tries) % N;
                if (write_started[target] < TX_PER_SLAVE) {
                    write_rr = (target + 1) % N;
                    write_target = target;
                    write_seq = write_started[target]++;
                    write_addr = addr_for(target, write_seq, false);
                    write_id = ((target << 4) ^ write_seq) & ((1ULL << ID_WIDTH) - 1);
                    write_data = make_data(target, write_seq, write_addr, false);
                    m_master._next.awaddr = write_addr;
                    m_master._next.awid = write_id;
                    m_master._next.wdata = write_data;
                    m_master._next.awvalid = 1;
                    write_phase = 1;
                    break;
                }
            }
        }

        if (!read_completed && read_phase == 0) {
            for (size_t tries = 0; tries < N; ++tries) {
                size_t target = (read_rr + tries) % N;
                if (read_started[target] < TX_PER_SLAVE) {
                    read_rr = (target + 1) % N;
                    read_target = target;
                    read_seq = read_started[target]++;
                    read_addr = addr_for(target, read_seq, true);
                    read_id = ((target << 5) ^ (read_seq << 1) ^ 1) & ((1ULL << ID_WIDTH) - 1);
                    read_data_ref = make_data(target, read_seq, read_addr, true);
                    m_master._next.araddr = read_addr;
                    m_master._next.arid = read_id;
                    m_master._next.arvalid = 1;
                    read_phase = 1;
                    break;
                }
            }
        }

        m_master._next.bready = 1;
        m_master._next.rready = read_phase == 2;
        for (size_t i = 0; i < N; ++i) {
            s_slave[i]._next.awready = (random() % 10) != 0;
            s_slave[i]._next.wready = (random() % 10) != 1;
            s_slave[i]._next.arready = (random() % 10) != 2;
        }

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
        std::print("VERILATOR TestAxi4MuxFromSlave, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
#else
        std::print("CppHDL TestAxi4MuxFromSlave, N: {}, ADDR_WIDTH: {}, ID_WIDTH: {}, DATA_WIDTH: {}...", N, ADDR_WIDTH, ID_WIDTH, DATA_WIDTH);
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
                std::print("{:s}: slave {} write_started={} write_done={} read_started={} read_done={} write_wait_b={} read_wait_r={}\n",
                    __inst_name, i, write_started[i], write_done[i],
                    read_started[i], read_done[i], slave_write_wait_b[i], slave_read_wait_r[i]);
            }
            std::print("{:s}: write_phase={} read_phase={} bvalid={} rvalid={}\n",
                __inst_name, write_phase, read_phase, (int)driver_bvalid(), (int)driver_rvalid());
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
        ok &= VerilatorCompile(__FILE__, "Axi4MuxFromSlave", {"Predef_pkg"}, {"../../../../include"}, 4, 32, 8, 128);
        ok &= VerilatorCompile(__FILE__, "Axi4MuxFromSlave", {"Predef_pkg"}, {"../../../../include"}, 8, 64, 16, 512);
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        std::cout << "Executing tests... ===========================================================================\n";
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Axi4MuxFromSlave_4_32_8_128/obj_dir/VAxi4MuxFromSlave") + (debug?" --debug":"") + " 0").c_str()) == 0)
            && ((only != -1 && only != 1) || std::system((std::string("Axi4MuxFromSlave_8_64_16_512/obj_dir/VAxi4MuxFromSlave") + (debug?" --debug":"") + " 1").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestAxi4MuxFromSlave<4,32,8,128>(debug).run())
        && ((only != -1 && only != 1) || TestAxi4MuxFromSlave<8,64,16,512>(debug).run())
    );
}

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS && !NO_MAINFILE

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif

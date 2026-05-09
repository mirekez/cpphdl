#pragma once

#include "cpphdl.h"
#include "Axi4.h"

using namespace cpphdl;

template<size_t N, size_t ADDR_WIDTH = 32, size_t ID_WIDTH = 4, size_t DATA_WIDTH = 256>
class Axi4RegionMux : public Module
{
    static_assert(N >= 1, "Axi4RegionMux needs at least one target");
    static constexpr size_t SEL_BITS = N <= 1 ? 1 : clog2(N);

public:
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> slave_in;
    Axi4If<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH> masters_out[N];

    __PORT(uint32_t) region_base_in[N];
    __PORT(uint32_t) region_size_in[N];

private:
    reg<u<SEL_BITS>> aw_sel_reg;
    reg<u<SEL_BITS>> ar_sel_reg;
    reg<u1> aw_active_reg;
    reg<u1> ar_active_reg;

    // Registered write target, clamped so reset-time/uninitialized values never index past masters_out.
    __LAZY_COMB(aw_sel_safe_comb, uint32_t)
        uint32_t sel;
        sel = (uint32_t)aw_sel_reg;
        if (sel >= N) {
            sel = 0;
        }
        return aw_sel_safe_comb = sel;
    }

    // Registered read target, clamped so reset-time/uninitialized values never index past masters_out.
    __LAZY_COMB(ar_sel_safe_comb, uint32_t)
        uint32_t sel;
        sel = (uint32_t)ar_sel_reg;
        if (sel >= N) {
            sel = 0;
        }
        return ar_sel_safe_comb = sel;
    }

    // Device selected by the write address against configured base/size windows.
    __LAZY_COMB(aw_next_comb, uint32_t)
        size_t i;
        uint32_t addr;
        aw_next_comb = 0;
        addr = slave_in.awaddr_in();
        for (i = 0; i < N; ++i) {
            if (addr >= region_base_in[i]() && addr - region_base_in[i]() < region_size_in[i]()) {
                aw_next_comb = i;
            }
        }
        return aw_next_comb;
    }

    // Device selected by the read address against configured base/size windows.
    __LAZY_COMB(ar_next_comb, uint32_t)
        size_t i;
        uint32_t addr;
        ar_next_comb = 0;
        addr = slave_in.araddr_in();
        for (i = 0; i < N; ++i) {
            if (addr >= region_base_in[i]() && addr - region_base_in[i]() < region_size_in[i]()) {
                ar_next_comb = i;
            }
        }
        return ar_next_comb;
    }

    // Local write address after subtracting the selected device base.
    __LAZY_COMB(aw_local_addr_comb, u<ADDR_WIDTH>)
        uint32_t sel;
        sel = aw_active_reg ? aw_sel_safe_comb_func() : aw_next_comb_func();
        return aw_local_addr_comb = (u<ADDR_WIDTH>)(slave_in.awaddr_in() - region_base_in[sel]());
    }

    // Local read address after subtracting the selected device base.
    __LAZY_COMB(ar_local_addr_comb, u<ADDR_WIDTH>)
        uint32_t sel;
        sel = ar_active_reg ? ar_sel_safe_comb_func() : ar_next_comb_func();
        return ar_local_addr_comb = (u<ADDR_WIDTH>)(slave_in.araddr_in() - region_base_in[sel]());
    }

    // Write-address ready from the currently selected target.
    __LAZY_COMB(awready_comb, bool)
        size_t i;
        awready_comb = false;
        for (i = 0; i < N; ++i) {
            if (aw_next_comb_func() == i) {
                awready_comb = masters_out[i].awready_out();
            }
        }
        return awready_comb;
    }

    // Read-address ready from the currently selected target.
    __LAZY_COMB(arready_comb, bool)
        size_t i;
        arready_comb = false;
        for (i = 0; i < N; ++i) {
            if (ar_next_comb_func() == i) {
                arready_comb = masters_out[i].arready_out();
            }
        }
        return arready_comb;
    }

public:
    void _assign()
    {
        size_t i;
        for (i = 0; i < N; ++i) {
            masters_out[i].awvalid_in = __EXPR_I(!aw_active_reg && aw_next_comb_func() == i && slave_in.awvalid_in());
            masters_out[i].awaddr_in = __VAR(aw_local_addr_comb_func());
            masters_out[i].awid_in = slave_in.awid_in;

            masters_out[i].wvalid_in = __EXPR_I(aw_active_reg && aw_sel_safe_comb_func() == i && slave_in.wvalid_in());
            masters_out[i].wdata_in = slave_in.wdata_in;
            masters_out[i].wlast_in = slave_in.wlast_in;
            masters_out[i].bready_in = __EXPR_I(aw_active_reg && aw_sel_safe_comb_func() == i && slave_in.bready_in());

            masters_out[i].arvalid_in = __EXPR_I(!ar_active_reg && ar_next_comb_func() == i && slave_in.arvalid_in());
            masters_out[i].araddr_in = __VAR(ar_local_addr_comb_func());
            masters_out[i].arid_in = slave_in.arid_in;
            masters_out[i].rready_in = __EXPR_I(ar_active_reg && ar_sel_safe_comb_func() == i && slave_in.rready_in());
        }

        slave_in.awready_out = __EXPR(!aw_active_reg && awready_comb_func());
        slave_in.wready_out = __EXPR(aw_active_reg ? masters_out[aw_sel_safe_comb_func()].wready_out() : false);
        slave_in.bvalid_out = __EXPR(aw_active_reg ? masters_out[aw_sel_safe_comb_func()].bvalid_out() : false);
        slave_in.bid_out = __EXPR(masters_out[aw_sel_safe_comb_func()].bid_out());

        slave_in.arready_out = __EXPR(!ar_active_reg && arready_comb_func());
        slave_in.rvalid_out = __EXPR(ar_active_reg ? masters_out[ar_sel_safe_comb_func()].rvalid_out() : false);
        slave_in.rdata_out = __EXPR(masters_out[ar_sel_safe_comb_func()].rdata_out());
        slave_in.rlast_out = __EXPR(ar_active_reg ? masters_out[ar_sel_safe_comb_func()].rlast_out() : false);
        slave_in.rid_out = __EXPR(masters_out[ar_sel_safe_comb_func()].rid_out());
    }

    void _work(bool reset)
    {
        if (reset) {
            aw_sel_reg.clr();
            ar_sel_reg.clr();
            aw_active_reg.clr();
            ar_active_reg.clr();
            return;
        }
        if (!aw_active_reg && slave_in.awvalid_in() && slave_in.awready_out()) {
            aw_active_reg._next = true;
            aw_sel_reg._next = aw_next_comb_func();
        }
        if (aw_active_reg && slave_in.bvalid_out() && slave_in.bready_in()) {
            aw_active_reg._next = false;
        }
        if (!ar_active_reg && slave_in.arvalid_in() && slave_in.arready_out()) {
            ar_active_reg._next = true;
            ar_sel_reg._next = ar_next_comb_func();
        }
        if (ar_active_reg && slave_in.rvalid_out() && slave_in.rready_in() && slave_in.rlast_out()) {
            ar_active_reg._next = false;
        }
    }

    void _strobe()
    {
        aw_sel_reg.strobe();
        ar_sel_reg.strobe();
        aw_active_reg.strobe();
        ar_active_reg.strobe();
    }
};

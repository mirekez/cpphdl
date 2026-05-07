#pragma once

#include "cpphdl.h"

using namespace cpphdl;

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

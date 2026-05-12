#pragma once

#include "cpphdl.h"

using namespace cpphdl;

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH> struct Axi4If;

template<size_t ADDR_WIDTH, size_t ID_WIDTH>
struct Axi4WriteAddress
{
    bool valid;
    u<ADDR_WIDTH> addr;
    u<ID_WIDTH> id;
};

template<size_t DATA_WIDTH>
struct Axi4WriteData
{
    bool valid;
    logic<DATA_WIDTH> data;
    bool last;
};

struct Axi4WriteResponseReady
{
    bool ready;
};

template<size_t ADDR_WIDTH, size_t ID_WIDTH>
struct Axi4ReadAddress
{
    bool valid;
    u<ADDR_WIDTH> addr;
    u<ID_WIDTH> id;
};

struct Axi4ReadDataReady
{
    bool ready;
};

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4Driver
{
    Axi4WriteAddress<ADDR_WIDTH, ID_WIDTH> aw;
    Axi4WriteData<DATA_WIDTH> w;
    Axi4WriteResponseReady b;
    Axi4ReadAddress<ADDR_WIDTH, ID_WIDTH> ar;
    Axi4ReadDataReady r;
};

struct Axi4WriteAddressReady
{
    bool ready;
};

struct Axi4WriteDataReady
{
    bool ready;
};

template<size_t ID_WIDTH>
struct Axi4WriteResponse
{
    bool valid;
    u<ID_WIDTH> id;
};

struct Axi4ReadAddressReady
{
    bool ready;
};

template<size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4ReadData
{
    bool valid;
    logic<DATA_WIDTH> data;
    bool last;
    u<ID_WIDTH> id;
};

template<size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4Responder
{
    Axi4WriteAddressReady aw;
    Axi4WriteDataReady w;
    Axi4WriteResponse<ID_WIDTH> b;
    Axi4ReadAddressReady ar;
    Axi4ReadData<ID_WIDTH, DATA_WIDTH> r;
};

template<size_t ADDR_WIDTH, size_t ID_WIDTH, size_t DATA_WIDTH>
struct Axi4If : Interface
{
    _PORT(bool)               awvalid_in;
    _PORT(bool)               awready_out;
    _PORT(u<ADDR_WIDTH>)      awaddr_in;
    _PORT(u<ID_WIDTH>)        awid_in;

    _PORT(bool)               wvalid_in;
    _PORT(bool)               wready_out;
    _PORT(logic<DATA_WIDTH>)  wdata_in;
    _PORT(bool)               wlast_in;

    _PORT(bool)               bvalid_out;
    _PORT(bool)               bready_in;
    _PORT(u<ID_WIDTH>)        bid_out;

    _PORT(bool)               arvalid_in;
    _PORT(bool)               arready_out;
    _PORT(u<ADDR_WIDTH>)      araddr_in;
    _PORT(u<ID_WIDTH>)        arid_in;

    _PORT(bool)               rvalid_out;
    _PORT(bool)               rready_in;
    _PORT(logic<DATA_WIDTH>)  rdata_out;
    _PORT(bool)               rlast_out;
    _PORT(u<ID_WIDTH>)        rid_out;

    Axi4If& operator=(Axi4Driver<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>& other)
    {
        Axi4Driver<ADDR_WIDTH, ID_WIDTH, DATA_WIDTH>* p = &other;
        awvalid_in = [p]() { return &p->aw.valid; };
        awaddr_in = [p]() { return &p->aw.addr; };
        awid_in = [p]() { return &p->aw.id; };

        wvalid_in = [p]() { return &p->w.valid; };
        wdata_in = [p]() { return &p->w.data; };
        wlast_in = [p]() { return &p->w.last; };

        bready_in = [p]() { return &p->b.ready; };

        arvalid_in = [p]() { return &p->ar.valid; };
        araddr_in = [p]() { return &p->ar.addr; };
        arid_in = [p]() { return &p->ar.id; };

        rready_in = [p]() { return &p->r.ready; };
        return *this;
    }

    Axi4If& operator=(Axi4Responder<ID_WIDTH, DATA_WIDTH>& other)
    {
        Axi4Responder<ID_WIDTH, DATA_WIDTH>* p = &other;
        awready_out = [p]() { return &p->aw.ready; };
        wready_out = [p]() { return &p->w.ready; };

        bvalid_out = [p]() { return &p->b.valid; };
        bid_out = [p]() { return &p->b.id; };

        arready_out = [p]() { return &p->ar.ready; };
        rvalid_out = [p]() { return &p->r.valid; };
        rdata_out = [p]() { return &p->r.data; };
        rlast_out = [p]() { return &p->r.last; };
        rid_out = [p]() { return &p->r.id; };
        return *this;
    }

};

#define AXI4_DRIVER_FROM(dst, src) \
    (dst).awvalid_in = (src).awvalid_in; \
    (dst).awaddr_in = (src).awaddr_in; \
    (dst).awid_in = (src).awid_in; \
    (dst).wvalid_in = (src).wvalid_in; \
    (dst).wdata_in = (src).wdata_in; \
    (dst).wlast_in = (src).wlast_in; \
    (dst).bready_in = (src).bready_in; \
    (dst).arvalid_in = (src).arvalid_in; \
    (dst).araddr_in = (src).araddr_in; \
    (dst).arid_in = (src).arid_in; \
    (dst).rready_in = (src).rready_in

#define AXI4_RESPONDER_FROM(dst, src) \
    (dst).awready_out = (src).awready_out; \
    (dst).wready_out = (src).wready_out; \
    (dst).bvalid_out = (src).bvalid_out; \
    (dst).bid_out = (src).bid_out; \
    (dst).arready_out = (src).arready_out; \
    (dst).rvalid_out = (src).rvalid_out; \
    (dst).rdata_out = (src).rdata_out; \
    (dst).rlast_out = (src).rlast_out; \
    (dst).rid_out = (src).rid_out

#define AXI4_DRIVER_FROM_VERILATOR(dst, src, index, addr_type, data_func) \
    do { \
        (dst).awvalid_in = _BIND_I((bool)(src).axi_out___05Fawvalid_out[index]); \
        (dst).awaddr_in = _BIND_I((addr_type)(uint32_t)(src).axi_out___05Fawaddr_out[index]); \
        (dst).awid_in = _BIND_I((u<4>)(uint32_t)(src).axi_out___05Fawid_out[index]); \
        (dst).wvalid_in = _BIND_I((bool)(src).axi_out___05Fwvalid_out[index]); \
        (dst).wdata_in = _BIND_I(data_func((src).axi_out___05Fwdata_out[index])); \
        (dst).wlast_in = _BIND_I((bool)(src).axi_out___05Fwlast_out[index]); \
        (dst).bready_in = _BIND_I((bool)(src).axi_out___05Fbready_out[index]); \
        (dst).arvalid_in = _BIND_I((bool)(src).axi_out___05Farvalid_out[index]); \
        (dst).araddr_in = _BIND_I((addr_type)(uint32_t)(src).axi_out___05Faraddr_out[index]); \
        (dst).arid_in = _BIND_I((u<4>)(uint32_t)(src).axi_out___05Farid_out[index]); \
        (dst).rready_in = _BIND_I((bool)(src).axi_out___05Frready_out[index]); \
    } while (false)

#define AXI4_RESPONDER_FROM_VERILATOR(dst, src, index) \
    do { \
        (dst).axi_out___05Fawready_in[index] = (src).awready_out(); \
        (dst).axi_out___05Fwready_in[index] = (src).wready_out(); \
        (dst).axi_out___05Fbvalid_in[index] = (src).bvalid_out(); \
        (dst).axi_out___05Fbid_in[index] = (src).bid_out(); \
        (dst).axi_out___05Farready_in[index] = (src).arready_out(); \
        (dst).axi_out___05Frvalid_in[index] = (src).rvalid_out(); \
        verilator_logic_to_wide((dst).axi_out___05Frdata_in[index], (src).rdata_out()); \
        (dst).axi_out___05Frlast_in[index] = (src).rlast_out(); \
        (dst).axi_out___05Frid_in[index] = (src).rid_out(); \
    } while (false)

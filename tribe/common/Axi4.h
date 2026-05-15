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

#define AXI4_DRIVER_FROM_PORTS(dst) \
    (dst).awvalid_in = awvalid_in; \
    (dst).awaddr_in = awaddr_in; \
    (dst).awid_in = awid_in; \
    (dst).wvalid_in = wvalid_in; \
    (dst).wdata_in = wdata_in; \
    (dst).wlast_in = wlast_in; \
    (dst).bready_in = bready_in; \
    (dst).arvalid_in = arvalid_in; \
    (dst).araddr_in = araddr_in; \
    (dst).arid_in = arid_in; \
    (dst).rready_in = rready_in

#define AXI4_RESPONDER_TO_PORTS(src) \
    awready_out = (src).awready_out; \
    wready_out = (src).wready_out; \
    bvalid_out = (src).bvalid_out; \
    bid_out = (src).bid_out; \
    arready_out = (src).arready_out; \
    rvalid_out = (src).rvalid_out; \
    rdata_out = (src).rdata_out; \
    rlast_out = (src).rlast_out; \
    rid_out = (src).rid_out

#define AXI4_DRIVER_FROM_I(dst, src) \
    (dst).awvalid_in = _ASSIGN_I((src).awvalid_in()); \
    (dst).awaddr_in = _ASSIGN_I((src).awaddr_in()); \
    (dst).awid_in = _ASSIGN_I((src).awid_in()); \
    (dst).wvalid_in = _ASSIGN_I((src).wvalid_in()); \
    (dst).wdata_in = _ASSIGN_I((src).wdata_in()); \
    (dst).wlast_in = _ASSIGN_I((src).wlast_in()); \
    (dst).bready_in = _ASSIGN_I((src).bready_in()); \
    (dst).arvalid_in = _ASSIGN_I((src).arvalid_in()); \
    (dst).araddr_in = _ASSIGN_I((src).araddr_in()); \
    (dst).arid_in = _ASSIGN_I((src).arid_in()); \
    (dst).rready_in = _ASSIGN_I((src).rready_in())

#define AXI4_RESPONDER_FROM_I(dst, src) \
    (dst).awready_out = _ASSIGN_I((src).awready_out()); \
    (dst).wready_out = _ASSIGN_I((src).wready_out()); \
    (dst).bvalid_out = _ASSIGN_I((src).bvalid_out()); \
    (dst).bid_out = _ASSIGN_I((src).bid_out()); \
    (dst).arready_out = _ASSIGN_I((src).arready_out()); \
    (dst).rvalid_out = _ASSIGN_I((src).rvalid_out()); \
    (dst).rdata_out = _ASSIGN_I((src).rdata_out()); \
    (dst).rlast_out = _ASSIGN_I((src).rlast_out()); \
    (dst).rid_out = _ASSIGN_I((src).rid_out())

#define AXI4_DRIVER_FROM_REGS(dst, awvalid, awaddr, awid, wvalid, wdata, wlast, bready, arvalid, araddr, arid, rready) \
    (dst).awvalid_in = _ASSIGN_REG(awvalid); \
    (dst).awaddr_in = _ASSIGN_REG(awaddr); \
    (dst).awid_in = _ASSIGN_REG(awid); \
    (dst).wvalid_in = _ASSIGN_REG(wvalid); \
    (dst).wdata_in = _ASSIGN_REG(wdata); \
    (dst).wlast_in = _ASSIGN_REG(wlast); \
    (dst).bready_in = _ASSIGN_REG(bready); \
    (dst).arvalid_in = _ASSIGN_REG(arvalid); \
    (dst).araddr_in = _ASSIGN_REG(araddr); \
    (dst).arid_in = _ASSIGN_REG(arid); \
    (dst).rready_in = _ASSIGN_REG(rready)

#define AXI4_DRIVER_FROM_REGS_I(dst, awvalid, awaddr, awid, wvalid, wdata, wlast, bready, arvalid, araddr, arid, rready) \
    (dst).awvalid_in = _ASSIGN_REG_I(awvalid); \
    (dst).awaddr_in = _ASSIGN_REG_I(awaddr); \
    (dst).awid_in = _ASSIGN_REG_I(awid); \
    (dst).wvalid_in = _ASSIGN_REG_I(wvalid); \
    (dst).wdata_in = _ASSIGN_REG_I(wdata); \
    (dst).wlast_in = _ASSIGN_REG_I(wlast); \
    (dst).bready_in = _ASSIGN_REG_I(bready); \
    (dst).arvalid_in = _ASSIGN_REG_I(arvalid); \
    (dst).araddr_in = _ASSIGN_REG_I(araddr); \
    (dst).arid_in = _ASSIGN_REG_I(arid); \
    (dst).rready_in = _ASSIGN_REG_I(rready)

#define AXI4_DRIVER_FROM_DRIVER(dst, src) \
    (dst).awvalid_in = _ASSIGN_REG((src).aw.valid); \
    (dst).awaddr_in = _ASSIGN_REG((src).aw.addr); \
    (dst).awid_in = _ASSIGN_REG((src).aw.id); \
    (dst).wvalid_in = _ASSIGN_REG((src).w.valid); \
    (dst).wdata_in = _ASSIGN_REG((src).w.data); \
    (dst).wlast_in = _ASSIGN_REG((src).w.last); \
    (dst).bready_in = _ASSIGN_REG((src).b.ready); \
    (dst).arvalid_in = _ASSIGN_REG((src).ar.valid); \
    (dst).araddr_in = _ASSIGN_REG((src).ar.addr); \
    (dst).arid_in = _ASSIGN_REG((src).ar.id); \
    (dst).rready_in = _ASSIGN_REG((src).r.ready)

#define AXI4_DRIVER_FROM_DRIVER_I(dst, src) \
    (dst).awvalid_in = _ASSIGN_REG_I((src).aw.valid); \
    (dst).awaddr_in = _ASSIGN_REG_I((src).aw.addr); \
    (dst).awid_in = _ASSIGN_REG_I((src).aw.id); \
    (dst).wvalid_in = _ASSIGN_REG_I((src).w.valid); \
    (dst).wdata_in = _ASSIGN_REG_I((src).w.data); \
    (dst).wlast_in = _ASSIGN_REG_I((src).w.last); \
    (dst).bready_in = _ASSIGN_REG_I((src).b.ready); \
    (dst).arvalid_in = _ASSIGN_REG_I((src).ar.valid); \
    (dst).araddr_in = _ASSIGN_REG_I((src).ar.addr); \
    (dst).arid_in = _ASSIGN_REG_I((src).ar.id); \
    (dst).rready_in = _ASSIGN_REG_I((src).r.ready)

#define AXI4_DRIVER_POKE_FROM_REGS(dst, awvalid, awaddr, awid, wvalid, wdata, wlast, bready, arvalid, araddr, arid, rready) \
    (dst).awvalid_in = awvalid; \
    (dst).awaddr_in = awaddr; \
    (dst).awid_in = awid; \
    (dst).wvalid_in = wvalid; \
    (dst).wdata_in = wdata; \
    (dst).wlast_in = wlast; \
    (dst).bready_in = bready; \
    (dst).arvalid_in = arvalid; \
    (dst).araddr_in = araddr; \
    (dst).arid_in = arid; \
    (dst).rready_in = rready

#define AXI4_DRIVER_POKE_VERILATOR_IF_FROM_REGS(dst, if_name, awvalid, awaddr, awid, wvalid, wdata, wlast, bready, arvalid, araddr, arid, rready) \
    (dst).if_name##___05Fawvalid_in = awvalid; \
    (dst).if_name##___05Fawaddr_in = awaddr; \
    (dst).if_name##___05Fawid_in = awid; \
    (dst).if_name##___05Fwvalid_in = wvalid; \
    (dst).if_name##___05Fwdata_in = wdata; \
    (dst).if_name##___05Fwlast_in = wlast; \
    (dst).if_name##___05Fbready_in = bready; \
    (dst).if_name##___05Farvalid_in = arvalid; \
    (dst).if_name##___05Faraddr_in = araddr; \
    (dst).if_name##___05Farid_in = arid; \
    (dst).if_name##___05Frready_in = rready

#define AXI4_DRIVER_POKE_FROM_DRIVER(dst, src) \
    (dst).awvalid_in = (src).aw.valid; \
    (dst).awaddr_in = (uint32_t)(src).aw.addr; \
    (dst).awid_in = (uint32_t)(src).aw.id; \
    (dst).wvalid_in = (src).w.valid; \
    (dst).wdata_in = (uint32_t)(src).w.data; \
    (dst).wlast_in = (src).w.last; \
    (dst).bready_in = (src).b.ready; \
    (dst).arvalid_in = (src).ar.valid; \
    (dst).araddr_in = (uint32_t)(src).ar.addr; \
    (dst).arid_in = (uint32_t)(src).ar.id; \
    (dst).rready_in = (src).r.ready

#define AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER(dst, if_name, src) \
    (dst).if_name##___05Fawvalid_in = (src).aw.valid; \
    (dst).if_name##___05Fawaddr_in = (uint32_t)(src).aw.addr; \
    (dst).if_name##___05Fawid_in = (uint32_t)(src).aw.id; \
    (dst).if_name##___05Fwvalid_in = (src).w.valid; \
    (dst).if_name##___05Fwdata_in = (uint32_t)(src).w.data; \
    (dst).if_name##___05Fwlast_in = (src).w.last; \
    (dst).if_name##___05Fbready_in = (src).b.ready; \
    (dst).if_name##___05Farvalid_in = (src).ar.valid; \
    (dst).if_name##___05Faraddr_in = (uint32_t)(src).ar.addr; \
    (dst).if_name##___05Farid_in = (uint32_t)(src).ar.id; \
    (dst).if_name##___05Frready_in = (src).r.ready

#define AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER_I(dst, if_name, index, src) \
    (dst).if_name##___05Fawvalid_in[index] = (src).aw.valid; \
    (dst).if_name##___05Fawaddr_in[index] = (src).aw.addr; \
    (dst).if_name##___05Fawid_in[index] = (src).aw.id; \
    (dst).if_name##___05Fwvalid_in[index] = (src).w.valid; \
    verilator_logic_to_wide((dst).if_name##___05Fwdata_in[index], (src).w.data); \
    (dst).if_name##___05Fwlast_in[index] = (src).w.last; \
    (dst).if_name##___05Fbready_in[index] = (src).b.ready; \
    (dst).if_name##___05Farvalid_in[index] = (src).ar.valid; \
    (dst).if_name##___05Faraddr_in[index] = (src).ar.addr; \
    (dst).if_name##___05Farid_in[index] = (src).ar.id; \
    (dst).if_name##___05Frready_in[index] = (src).r.ready

#define AXI4_DRIVER_FROM_VERILATOR(dst, src, index, addr_type, data_func) \
    do { \
        (dst).awvalid_in = _ASSIGN_I((bool)(src).axi_out___05Fawvalid_out[index]); \
        (dst).awaddr_in = _ASSIGN_I((addr_type)(uint32_t)(src).axi_out___05Fawaddr_out[index]); \
        (dst).awid_in = _ASSIGN_I((u<4>)(uint32_t)(src).axi_out___05Fawid_out[index]); \
        (dst).wvalid_in = _ASSIGN_I((bool)(src).axi_out___05Fwvalid_out[index]); \
        (dst).wdata_in = _ASSIGN_I(data_func((src).axi_out___05Fwdata_out[index])); \
        (dst).wlast_in = _ASSIGN_I((bool)(src).axi_out___05Fwlast_out[index]); \
        (dst).bready_in = _ASSIGN_I((bool)(src).axi_out___05Fbready_out[index]); \
        (dst).arvalid_in = _ASSIGN_I((bool)(src).axi_out___05Farvalid_out[index]); \
        (dst).araddr_in = _ASSIGN_I((addr_type)(uint32_t)(src).axi_out___05Faraddr_out[index]); \
        (dst).arid_in = _ASSIGN_I((u<4>)(uint32_t)(src).axi_out___05Farid_out[index]); \
        (dst).rready_in = _ASSIGN_I((bool)(src).axi_out___05Frready_out[index]); \
    } while (false)

#define AXI4_RESPONDER_FROM_VERILATOR(dst, src, index) \
    do { \
        const bool axi4_responder_rvalid = (src).rvalid_out(); \
        (dst).axi_out___05Fawready_in[index] = (src).awready_out(); \
        (dst).axi_out___05Fwready_in[index] = (src).wready_out(); \
        (dst).axi_out___05Fbvalid_in[index] = (src).bvalid_out(); \
        (dst).axi_out___05Fbid_in[index] = (src).bid_out(); \
        (dst).axi_out___05Farready_in[index] = (src).arready_out(); \
        (dst).axi_out___05Frvalid_in[index] = axi4_responder_rvalid; \
        verilator_logic_to_wide((dst).axi_out___05Frdata_in[index], axi4_responder_rvalid ? (src).rdata_out() : std::remove_reference_t<decltype((src).rdata_out())>(0)); \
        (dst).axi_out___05Frlast_in[index] = (src).rlast_out(); \
        (dst).axi_out___05Frid_in[index] = (src).rid_out(); \
    } while (false)

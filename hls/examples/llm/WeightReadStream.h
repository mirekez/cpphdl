#pragma once
#include "../../ExternalMemory.h"

#ifndef LLM_READ_WINDOW
#define LLM_READ_WINDOW 8
#endif

// Credit reserves a FIFO slot at request acceptance, before DDR returns data.
// A separate beat register permits continuous unpacking across FIFO entries.
class WeightReadStream : public cpphdl::Module {
public:
    cpphdl::hls::DramReadIf<512> memory_out;
    _PORT(bool) command_in;
    _PORT(uint32_t) base_in;
    _PORT(uint32_t) words_in;
    _PORT(bool) ready_in;
    _PORT(bool) valid_out;
    _PORT(uint64_t) data_out;
    _PORT(bool) fault_out;
    _PORT(bool) pending_out;
private:
    static constexpr unsigned WINDOW = LLM_READ_WINDOW;
    static constexpr unsigned INDEX_BITS = cpphdl::clog2(WINDOW);
    static_assert(WINDOW >= 2 && WINDOW <= 32 && (WINDOW & (WINDOW-1)) == 0);
    cpphdl::memory<cpphdl::logic<512>,1,WINDOW> fifo;
    cpphdl::reg<cpphdl::u<INDEX_BITS>> head, tail;
    cpphdl::reg<cpphdl::u<INDEX_BITS+1>> reserved, buffered, outstanding;
    cpphdl::reg<cpphdl::u16> beats_left, words_left;
    cpphdl::reg<cpphdl::u32> address;
    cpphdl::reg<cpphdl::logic<512>> current;
    cpphdl::reg<cpphdl::u<3>> lane;
    cpphdl::reg<cpphdl::u1> active, current_valid, fault, request_hold;
public:
    void _assign() {
        memory_out.valid_in = _ASSIGN(request_hold || (active && !fault && beats_left != 0 && reserved != WINDOW));
        memory_out.addr_in = _ASSIGN(uint32_t(address));
        memory_out.ready_in = _ASSIGN(active && !fault && outstanding != 0);
        valid_out = _ASSIGN(active && current_valid && !fault);
        data_out = _ASSIGN(uint64_t(current >> (uint32_t(lane)*64u)));
        fault_out = _ASSIGN(bool(fault));
        pending_out = _ASSIGN(outstanding != 0);
    }
    void _work(bool reset) {
        bool request, response, consume, fetch;
        if (reset) {
            head.clr(); tail.clr(); reserved.clr(); buffered.clr(); outstanding.clr();
            beats_left.clr(); words_left.clr(); address.clr(); current.clr(); lane.clr();
            active.clr(); current_valid.clr(); fault.clr(); request_hold.clr();
        } else {
            request = memory_out.valid_in() && memory_out.ready_out();
            response = memory_out.valid_out() && memory_out.ready_in();
            consume = valid_out() && ready_in();
            fetch = active && !fault && buffered != 0 &&
                (!current_valid || (consume && lane == 7 && words_left != 1));
            request_hold._next = memory_out.valid_in() && !memory_out.ready_out();
            if (command_in() && !active && !fault) {
                if ((base_in() & 63u) || !words_in() || words_in() > 65280 ||
                    base_in() > UINT32_MAX - 65280u*8u) fault._next = true;
                else {
                    active._next = true; address._next = base_in();
                    beats_left._next = (words_in()+7u)>>3; words_left._next = words_in();
                }
            }
            if (request) { address._next = uint32_t(address)+64u; beats_left._next = uint32_t(beats_left)-1u; }
            if (response) {
                fifo[uint32_t(tail)] = memory_out.data_out();
                tail._next = uint32_t(tail)+1u;
                if (memory_out.error_out()) fault._next = true;
            }
            if (consume) {
                words_left._next = uint32_t(words_left)-1u; lane._next = uint32_t(lane)+1u;
                if (lane == 7 || words_left == 1) current_valid._next = false;
                if (words_left == 1) active._next = false;
            }
            if (fetch) {
                current._next = fifo[uint32_t(head)]; current_valid._next = true;
                lane._next = 0; head._next = uint32_t(head)+1u;
            }
            if (request != fetch) reserved._next = uint32_t(reserved)+(request ? 1u : uint32_t(-1));
            if (response != fetch) buffered._next = uint32_t(buffered)+(response ? 1u : uint32_t(-1));
            if (request != response) outstanding._next = uint32_t(outstanding)+(request ? 1u : uint32_t(-1));
        }
    }
    void _strobe() {
        fifo.apply(); head.strobe(); tail.strobe(); reserved.strobe(); buffered.strobe(); outstanding.strobe();
        beats_left.strobe(); words_left.strobe(); address.strobe(); current.strobe(); lane.strobe();
        active.strobe(); current_valid.strobe(); fault.strobe(); request_hold.strobe();
    }
};

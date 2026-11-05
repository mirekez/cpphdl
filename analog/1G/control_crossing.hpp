#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Transfer the latest control word coherently between unrelated clocks.
// The source holds its bundled data until the destination acknowledges it.
// Intermediate settings may coalesce; this is not a stream/event FIFO.
// Both domains share asynchronous reset. Reset release takes two local edges.
// Physical implementation must constrain held -> value delay/skew to less
// than one destination period; only the request/acknowledgement use ASYNC_REG.
template<unsigned Width>
class ControlCrossing : public Module {
public:
    _PORT(u<Width>) data_in;
    _PORT(u<Width>) data_out;
    _PORT(bool) valid_out;

private:
    reg<u<Width>> held{}, value{};
    reg<u1> request{}, acknowledge{}, valid{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> acknowledge1{}, acknowledge2{}, request1{}, request2{};
    // (* ASYNC_REG = "TRUE" *)
    reg<u1> source1{}, source2{}, destination1{}, destination2{};

public:
    void _assign() {
        data_out = _ASSIGN_REG(value);
        valid_out = _ASSIGN(valid && destination2);
    }
    void _work_source_clk(bool) {
        source1._next = 1; source2._next = source1;
        acknowledge1._next = acknowledge; acknowledge2._next = acknowledge1;
        if (source2 && request == acknowledge2) {
            held._next = data_in();
            request._next = !request;
        }
    }
    void _work_destination_clk(bool) {
        destination1._next = 1; destination2._next = destination1;
        request1._next = request; request2._next = request1;
        if (destination2 && request2 != acknowledge) {
            value._next = held;
            acknowledge._next = request2;
            valid._next = 1;
        }
    }
    void _reset_pos_source_clk() {
        held._next = 0; request._next = 0;
        acknowledge1._next = 0; acknowledge2._next = 0;
        source1._next = 0; source2._next = 0;
    }
    void _reset_pos_destination_clk() {
        value._next = 0; acknowledge._next = 0; valid._next = 0;
        request1._next = 0; request2._next = 0;
        destination1._next = 0; destination2._next = 0;
    }
    void _strobe_source_clk() {
        held.strobe(); request.strobe(); acknowledge1.strobe(); acknowledge2.strobe();
        source1.strobe(); source2.strobe();
    }
    void _strobe_destination_clk() {
        value.strobe(); acknowledge.strobe(); valid.strobe(); request1.strobe(); request2.strobe();
        destination1.strobe(); destination2.strobe();
    }
    void _work_neg_source_clk(bool) {}
    void _work_neg_destination_clk(bool) {}
    void _strobe_neg_source_clk() {}
    void _strobe_neg_destination_clk() {}
};

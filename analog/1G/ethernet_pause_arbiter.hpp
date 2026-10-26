#pragma once
#include "ethernet_pause_transmit.hpp"
#include "ethernet_pause_timer.hpp"

// GMII transmit arbitration. The upstream FIFO observes hold_data_out only at
// frame boundaries and exposes idle_in after its complete 12-symbol IFG.
class EthernetPauseArbiter : public Module {
public:
    _PORT(u<10>) data_in;
    _PORT(bool) idle_in;
    _PORT(bool) request_in;
    _PORT(bool) enable_in;
    _PORT(bool) command_in;
    _PORT(u<16>) quanta_in;
    _PORT(u<48>) address_in;
    _PORT(u<10>) gmii_out = _ASSIGN(state == 2 ? transmitter.gmii_out() : data_in());
    _PORT(bool) hold_data_out = _ASSIGN(state != 0 || pending_func() || timer.hold_out());
    _PORT(bool) sent_out = _ASSIGN(transmitter.done_out());
private:
    EthernetPauseTransmit transmitter;
    EthernetPauseTimer timer;
    reg<u<2>> state{};
    reg<u1> advertised{};
    reg<u<14>> refresh{};
    _LAZY_COMB(pending, bool)
        return pending = enable_in() && (request_in() != bool(advertised) || (request_in() && refresh == 0));
    }
    _LAZY_COMB(start, bool)
        return start = state == 1 && idle_in() && (data_in() & 256) == 0 && enable_in();
    }
public:
    void _assign() {
        transmitter.start_in = _ASSIGN(start_func());
        // Refresh a 131.072 us PAUSE at least every 65.536 us, plus one
        // maximum data frame. A zero request emits an explicit resume frame.
        transmitter.quanta_in = _ASSIGN(request_in() ? u<16>(256) : u<16>(0));
        transmitter.source_address_in = _ASSIGN(address_in());
        timer.command_in = _ASSIGN(command_in());
        timer.quanta_in = _ASSIGN(quanta_in());
        timer.enable_in = _ASSIGN(enable_in());
        timer.data_active_in = _ASSIGN((data_in() & 256) != 0);
        transmitter._assign(); timer._assign();
    }
    void _work(bool reset) {
        if (refresh != 0) refresh._next = refresh - 1;
        if (state == 0 && pending_func()) state._next = 1;
        if (state == 1) {
            if (!enable_in()) state._next = 0;
            else if (start_func()) {
                state._next = 2;
                advertised._next = request_in();
                refresh._next = 8192;
            }
        }
        if (state == 2 && transmitter.done_out()) state._next = 0;
        transmitter._work(reset); timer._work(reset);
        if (reset) { state._next = 0; advertised._next = 0; refresh._next = 0; }
    }
    void _strobe() {
        state.strobe(); advertised.strobe(); refresh.strobe();
        transmitter._strobe(); timer._strobe();
    }
};

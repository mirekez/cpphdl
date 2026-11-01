#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Validate a full GMII PAUSE frame, including preamble, length and FCS.
// valid_in is a recovered-symbol enable; gaps in the processing clock stream
// do not consume bytes. The command is published only after the frame ends.
class EthernetPauseReceive : public Module {
public:
    _PORT(u<10>) gmii_in;
    _PORT(bool) valid_in;
    _PORT(bool) enable_in;
    _PORT(u<48>) local_address_in;
    _PORT(bool) command_out = _ASSIGN_REG(command);
    _PORT(u<16>) quanta_out = _ASSIGN_REG(quanta);
    // Header recognition is early enough for a 32-symbol ingress delay to
    // suppress this link-local control frame before forwarding its first byte.
    _PORT(bool) control_out = _ASSIGN_REG(control);

private:
    reg<u<14>> count{};
    reg<u<32>> crc{};
    reg<u<48>> destination{};
    reg<u<16>> pending_quanta{}, quanta{};
    reg<u1> active{}, malformed{}, matching{}, control{}, command{};

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned data, position;
        u<32> next_crc;
        bool bad, match;
        data = unsigned(gmii_in()) & 255;
        position = active ? unsigned(count) : 0;
        bad = active ? bool(malformed) : false;
        match = active ? bool(matching) : true;
        next_crc = active ? crc : u<32>(0xffffffffu);
        command._next = 0;
        if (valid_in()) {
            if ((gmii_in() & 256) != 0) {
                if ((gmii_in() & 512) != 0 || position >= 16383)
                    bad = true;
                if (position < 7 && data != 0x55) bad = true;
                if (position == 7 && data != 0xd5) bad = true;
                if (position >= 8) {
                    next_crc = next_crc ^ data;
                    for (unsigned bit = 0; bit < 8; ++bit)
                        next_crc = (next_crc >> 1) ^ ((next_crc & 1) != 0 ? 0xedb88320u : 0u);
                }
                if (position >= 8 && position <= 13)
                    destination._next = (destination << 8) | data;
                if (position == 14 && destination != u<48>(0x0180c2000001ull) && destination != local_address_in())
                    match = false;
                if ((position == 20 && data != 0x88) || (position == 21 && data != 0x08)) match = false;
                // All addressed MAC Control frames remain link-local, even
                // when their opcode is unsupported or their eventual FCS fails.
                if (position == 21) control._next = match && !bad && enable_in();
                if ((position == 22 && data != 0) || (position == 23 && data != 1)) match = false;
                if (position == 24) pending_quanta._next = data << 8;
                if (position == 25) pending_quanta._next = pending_quanta | data;
                if (position == 0) control._next = 0;
                active._next = 1;
                count._next = position < 16383 ? position + 1 : 16383;
                crc._next = next_crc;
                malformed._next = bad;
                matching._next = match;
            } else {
                // A PAUSE MAC Control frame is exactly 64 bytes from DA to FCS.
                if (active && enable_in() && matching && !malformed && count == 72 && crc == 0xdebb20e3u) {
                    command._next = 1;
                    quanta._next = pending_quanta;
                }
                active._next = 0;
                control._next = 0;
                count._next = 0;
            }
        }
        if (reset) {
            count._next = 0; crc._next = 0; destination._next = 0;
            pending_quanta._next = 0; quanta._next = 0;
            active._next = 0; malformed._next = 0; matching._next = 0;
            control._next = 0; command._next = 0;
        }
    }
    void _strobe() {
        count.strobe(); crc.strobe(); destination.strobe();
        pending_quanta.strobe(); quanta.strobe(); active.strobe();
        malformed.strobe(); matching.strobe(); control.strobe(); command.strobe();
    }
};

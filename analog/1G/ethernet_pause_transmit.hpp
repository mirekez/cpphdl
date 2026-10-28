#pragma once
#include <cpphdl.h>

using namespace cpphdl;

// Produce one complete PAUSE frame. The caller grants start only between
// frames, after the preceding IFG. busy_out includes the following 12-byte IFG.
// PAUSE control traffic must remain sendable while ordinary data is paused.
class EthernetPauseTransmit : public Module {
public:
    _PORT(bool) start_in;
    _PORT(u<16>) quanta_in;
    _PORT(u<48>) source_address_in;
    _PORT(u<10>) gmii_out = _ASSIGN_REG(output);
    _PORT(bool) busy_out = _ASSIGN_REG(busy);
    _PORT(bool) done_out = _ASSIGN_REG(done);

private:
    reg<u<7>> position{};
    reg<u<48>> source{};
    reg<u<16>> quanta{};
    reg<u<32>> crc{};
    reg<u<10>> output{};
    reg<u1> busy{}, done{};

public:
    void _assign() {}
    void _work(bool reset) {
        unsigned data, index;
        u<32> next_crc;
        index = unsigned(position);
        data = 0;
        next_crc = crc;
        output._next = 0;
        done._next = 0;
        if (!busy && start_in()) {
            source._next = source_address_in();
            quanta._next = quanta_in();
            position._next = 0;
            crc._next = 0xffffffffu;
            busy._next = 1;
        } else if (busy) {
            if (index < 7) data = 0x55;
            else if (index == 7) data = 0xd5;
            else if (index < 14) data = unsigned(u<48>(0x0180c2000001ull) >> ((13 - index) * 8)) & 255;
            else if (index < 20) data = unsigned(source >> ((19 - index) * 8)) & 255;
            else if (index == 20) data = 0x88;
            else if (index == 21) data = 0x08;
            else if (index == 23) data = 1;
            else if (index == 24) data = unsigned(quanta) >> 8;
            else if (index == 25) data = unsigned(quanta) & 255;
            else if (index >= 68 && index < 72) data = unsigned(~crc >> ((index - 68) * 8)) & 255;
            if (index >= 8 && index < 68) {
                next_crc = crc ^ data;
                for (unsigned bit = 0; bit < 8; ++bit)
                    next_crc = (next_crc >> 1) ^ ((next_crc & 1) != 0 ? 0xedb88320u : 0u);
                crc._next = next_crc;
            }
            if (index < 72) output._next = 256 | data;
            if (index == 83) {
                busy._next = 0;
                done._next = 1;
            } else position._next = position + 1;
        }
        if (reset) {
            position._next = 0; source._next = 0; quanta._next = 0;
            crc._next = 0; output._next = 0; busy._next = 0; done._next = 0;
        }
    }
    void _strobe() {
        position.strobe(); source.strobe(); quanta.strobe(); crc.strobe();
        output.strobe(); busy.strobe(); done.strobe();
    }
};

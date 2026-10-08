#include "cpphdl.h"
#include "../../../hls/examples/net/ReceiveMetadata.h"

class FrameParser : public cpphdl::Module {
public:
    _PORT(uint32_t) flags_in;
    _PORT(uint32_t) rx_word_in;
    _PORT(uint16_t) rx_frame_id_in;
    _PORT(bool) rx_valid_in;
    _PORT(uint32_t) metadata_out = _ASSIGN_REG(metadata);
    _PORT(uint32_t) word_out = _ASSIGN_REG(word);
    _PORT(uint32_t) framing_out = _ASSIGN_REG(framing);
    _PORT(uint16_t) frame_id_out = _ASSIGN_REG(frame_id);
    _PORT(bool) valid_out = _ASSIGN_REG(valid);
    cpphdl::reg<cpphdl::u<9>> state;
    cpphdl::reg<cpphdl::u32> metadata, word, framing, crc;
    cpphdl::reg<cpphdl::u16> frame_id;
    cpphdl::reg<cpphdl::logic<1>> valid;
    void _work(bool reset) {
        uint64_t transition;
        uint32_t next;
        transition = hftReceiveMetadata(uint32_t(state), uint32_t(crc), flags_in(), rx_word_in());
        next = uint32_t(transition >> 32);
        if (rx_valid_in()) state._next = next & 511u;
        if (rx_valid_in()) crc._next = uint32_t(transition);
        metadata._next = rx_valid_in() ? next : 0;
        word._next = rx_word_in();
        framing._next = flags_in();
        frame_id._next = rx_frame_id_in();
        valid._next = rx_valid_in();
        if (reset) {
            state._next = 0; metadata._next = 0; word._next = 0;
            framing._next = 0; crc._next = 0xffffffffu;
            frame_id._next = 0; valid._next = 0;
        }
    }
    void _strobe() {
        state.strobe(); metadata.strobe(); word.strobe(); frame_id.strobe(); valid.strobe();
        framing.strobe(); crc.strobe();
    }
};
FrameParser cpphdl_top;
#ifdef RETIMING_RUN
#include "FeedbackTest.h"
#endif

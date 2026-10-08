#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef RETIMING_GRAPH
#include "model.h"
#define DRIVE(name, value) dut.name[0] = value
#define GET(name) dut.name[0]
#else
#include "VSynthRetiming.h"
#define DRIVE(name, value) dut.name = value
#define GET(name) dut.name
#endif
long _system_clock = 0;
static uint32_t referenceByte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    return crc;
}
static std::vector<uint8_t> packet(unsigned frame) {
    const unsigned sizes[] = {60, 64, 78, 100, 104, 108};
    std::vector<uint8_t> bytes(sizes[frame % 6]);
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < bytes.size() - 4; ++i) {
        bytes[i] = uint8_t(frame * 37 + i * 71); crc = referenceByte(crc, bytes[i]);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = uint8_t(crc >> (8 * i));
    if (frame % 3 == 1) bytes.back() ^= 0x40;
    return bytes;
}
int main() {
#ifdef RETIMING_GRAPH
    cpphdl_native::Model dut;
#else
    VSynthRetiming dut;
#endif
    uint32_t flags = 0, word = 0;
    uint16_t id = 0;
    bool valid = false;
    cpphdl_top.flags_in = _ASSIGN(flags); cpphdl_top.rx_word_in = _ASSIGN(word);
    cpphdl_top.rx_frame_id_in = _ASSIGN(id); cpphdl_top.rx_valid_in = _ASSIGN(valid);
    unsigned frames = 0, beat = 0, length = 0, transactions = 0, errors = 0, crcErrors = 0, stalls = 0;
    uint32_t referenceCrc = 0xffffffffu;
    unsigned acceptedAt = 0, previousMetadata = 0;
    bool collecting = false, pending = false;
    for (unsigned cycle = 0; cycle < 500000 && (frames < 1000 || pending); ++cycle) {
        bool reset = cycle == 0 || cycle == 137 || cycle == 1031;
        unsigned bytes = (frames % 6 == 2 && beat == 19) ? 2 : 4;
        unsigned beats = frames % 6 == 0 ? 15 : frames % 6 == 1 ? 16 :
            frames % 6 == 2 ? 20 : frames % 6 == 3 ? 25 : frames % 6 == 4 ? 26 : 27;
        bool sof = beat == 0, eof = beat + 1 == beats;
        flags = (bytes << 2) | unsigned(sof) | (unsigned(eof) << 1);
        auto data = packet(frames);
        word = 0;
        for (unsigned i = 0; i < bytes; ++i) word |= uint32_t(data[beat * 4 + i]) << (8 * i);
        id = uint16_t(frames); valid = cycle % 7 != 3;
        if (pending) { word = cycle * 0x1379a5u; id = uint16_t(cycle); flags = cycle * 11; }
        // Inputs deliberately change on every busy clock, not just on accepts.
        DRIVE(flags, flags); DRIVE(rx_word, word); DRIVE(rx_frame_id, id); DRIVE(rx_valid, valid);
        DRIVE(work_reset, reset);
#ifndef RETIMING_GRAPH
        dut.clk = 0;
#endif
        dut.eval();
        bool ready = GET(retiming_ready_out), commit = GET(retiming_commit_out);
        if (reset) {
            cpphdl_top._work(true); cpphdl_top._strobe();
            pending = false; collecting = false; length = 0; beat = 0; previousMetadata = 0;
            referenceCrc = 0xffffffffu;
            if (ready || commit) return 2;
        } else if (ready) {
            if (pending) { std::fprintf(stderr, "accepted stale feedback at %u\n", cycle); return 3; }
            acceptedAt = cycle; pending = true; ++transactions;
            unsigned expected = 0;
            if (valid) {
                if (sof) { length = 0; collecting = true; referenceCrc = 0xffffffffu; }
                expected = length;
                if (collecting) {
                    if (length + bytes > 100) { collecting = false; expected |= 1024; }
                    else {
                        for (unsigned i = 0; i < bytes; ++i) referenceCrc = referenceByte(referenceCrc, uint8_t(word >> (8 * i)));
                        length += bytes; expected = length | 512;
                        if (eof) { collecting = false; expected |= length < 64 ? 1024 : 2048; }
                        else expected |= 256;
                        if (eof && length >= 64 && referenceCrc != 0xdebb20e3u) expected |= 4096;
                    }
                }
            }
            cpphdl_top._work(false); cpphdl_top._strobe();
            if (uint32_t(cpphdl_top.metadata_out()) != expected) return 4;
            if (valid) { if (eof) { ++frames; beat = 0; } else ++beat; }
        } else ++stalls;
#ifdef RETIMING_GRAPH
        dut.step();
#else
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#endif
        if (commit) {
            if (!pending || cycle - acceptedAt != RETIMING_LATENCY) return 5;
            if (GET(metadata) != uint32_t(cpphdl_top.metadata_out()) ||
                GET(word) != uint32_t(cpphdl_top.word_out()) ||
                GET(framing) != uint32_t(cpphdl_top.framing_out()) ||
                GET(frame_id) != uint16_t(cpphdl_top.frame_id_out()) ||
                GET(valid) != bool(cpphdl_top.valid_out())) {
                std::fprintf(stderr, "metadata/payload mismatch at %u frame %u\n", cycle, frames); return 6;
            }
            errors += bool(GET(metadata) & 1024); crcErrors += bool(GET(metadata) & 4096); pending = false;
            previousMetadata = GET(metadata);
        } else if (GET(metadata) != previousMetadata) return 7;
        ++_system_clock;
    }
    if (frames != 1000 || !errors || !crcErrors || !stalls || transactions < 20000) return 8;
    std::printf("1000 frames: %u transactions, %u size errors, %u CRC errors, %u busy clocks; metadata and feedback verified\n",
        transactions, errors, crcErrors, stalls);
}

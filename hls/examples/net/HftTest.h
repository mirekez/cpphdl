#pragma once
#include <algorithm>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>
#ifdef VERILATOR
#include "VHft.h"
#endif

long _system_clock = 0;
namespace hft_test {
using Bytes = std::vector<uint8_t>;
inline void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline void be(Bytes& b, unsigned at, uint32_t value, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b.at(at + i) = uint8_t(value >> (8 * (n - 1 - i)));
}
inline void le(Bytes& b, unsigned at, uint32_t value, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b.at(at + i) = uint8_t(value >> (8 * i));
}
inline uint16_t checksum(const Bytes& b) {
    uint32_t sum = 0;
    for (size_t i = 0; i < b.size(); i += 2) {
        sum += unsigned(b[i]) * 256 + (i + 1 < b.size() ? b[i + 1] : 0);
        sum = (sum & 65535) + (sum >> 16);
    }
    return uint16_t(~sum);
}
// Independent bit-serial reference, deliberately unlike the DUT XOR network.
inline uint32_t crcReference(const Bytes& b) {
    uint32_t crc = 0xffffffffu;
    for (uint8_t byte : b) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
inline void appendFcs(Bytes& b) {
    uint32_t fcs = crcReference(b);
    for (unsigned i = 0; i < 4; ++i) b.push_back(uint8_t(fcs >> (8 * i)));
}
inline void wordNetworks() {
    const uint32_t edges[] = {0, 1, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    for (uint32_t a : edges) for (uint32_t b : edges) {
        require(HftWordMath::add(a, b) == uint32_t(a + b), "parallel carry boundary mismatch");
        require(HftWordMath::less(a, b) == (a < b), "parallel comparison boundary mismatch");
    }
    std::mt19937 random{0x63726332};
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t a = random(), b = random();
        require(HftWordMath::add(a, b) == uint32_t(a + b), "parallel carry mismatch");
        require(HftWordMath::less(a, b) == (a < b), "parallel comparison mismatch");
        uint32_t reference = a;
        for (unsigned count = 0; count <= 4; ++count) {
            require(EthernetCrc::word(a, b, count) == reference, "word CRC network mismatch");
            if (count < 4) {
                reference ^= uint8_t(b >> (8 * count));
                for (unsigned bit = 0; bit < 8; ++bit)
                    reference = (reference >> 1) ^ ((reference & 1) ? 0xedb88320u : 0u);
            }
        }
    }
}
inline void ipChecksum(Bytes& b) {
    b[24] = b[25] = 0;
    be(b, 24, checksum(Bytes(b.begin() + 14, b.begin() + 34)), 2);
}
inline Bytes pseudo(const Bytes& b, unsigned protocol, unsigned length) {
    Bytes p(b.begin() + 26, b.begin() + 34);
    p.push_back(0); p.push_back(uint8_t(protocol));
    p.push_back(uint8_t(length >> 8)); p.push_back(uint8_t(length));
    p.insert(p.end(), b.begin() + 34, b.begin() + 34 + length); return p;
}
struct Quote { uint32_t sequence, symbol, bid, ask, bidSize, askSize; };
inline Bytes market(const Quote& q, bool udpChecksum) {
    Bytes b(74, 0);
    b[0] = 1; b[2] = 94; b[3] = 1; b[4] = 2; b[5] = 3; b[6] = 2; b[11] = 9;
    be(b, 12, 0x0800, 2); b[14] = 0x45; be(b, 16, 60, 2); b[22] = 64; b[23] = 17;
    b[26] = 10; b[29] = 9; b[30] = 239; b[31] = 1; b[32] = 2; b[33] = 3;
    be(b, 34, 8000, 2); be(b, 36, 9000, 2); be(b, 38, 40, 2);
    le(b, 42, 24, 2); le(b, 44, 1, 2); le(b, 46, 42, 2);
    const uint32_t fields[] = {q.sequence, q.symbol, q.bid, q.ask, q.bidSize, q.askSize};
    for (unsigned i = 0; i < 6; ++i) le(b, 50 + 4 * i, fields[i], 4);
    ipChecksum(b);
    if (udpChecksum) { uint16_t c = checksum(pseudo(b, 17, 40)); be(b, 40, c ? c : 65535, 2); }
    appendFcs(b); return b;
}
// Independent transaction oracle: no DUT method calls or implementation helpers.
struct Reference {
    uint32_t last = 0, orders = 0, buys = 0, sells = 0;
    Bytes expected(const Quote& q, bool malformed = false) {
        if (malformed || q.symbol != 1 || !q.sequence || q.sequence <= last || !q.bid || q.ask <= q.bid) return {};
        last = q.sequence;
        char side = 0;
        uint32_t price = 0;
        if (q.ask < 100000 && q.askSize >= 100) { side = 'B'; price = q.ask; ++buys; }
        else if (q.bid > 100020 && q.bidSize >= 100) { side = 'S'; price = q.bid; ++sells; }
        if (!side) return {};
        Bytes b(106, 0);
        b[0] = 2; b[5] = 2; b[6] = 2; b[11] = 1; be(b, 12, 0x0800, 2);
        b[14] = 0x45; be(b, 16, 92, 2); be(b, 20, 0x4000, 2); b[22] = 64; b[23] = 6;
        b[26] = 10; b[29] = 1; b[30] = 10; b[33] = 2; ipChecksum(b);
        be(b, 34, 40000, 2); be(b, 36, 9001, 2); be(b, 38, 1000 + 52 * orders++, 4);
        be(b, 42, 1, 4); b[46] = 0x50; b[47] = 0x18; be(b, 48, 4096, 2);
        be(b, 54, 50, 2); b[56] = 'U'; b[57] = 'O';
        char token[15]; std::snprintf(token, sizeof(token), "SIM000%08X", q.sequence);
        std::copy(token, token + 14, b.begin() + 58); b[72] = uint8_t(side); be(b, 73, 100, 4);
        const char stock[] = "TEST    "; std::copy(stock, stock + 8, b.begin() + 77);
        be(b, 85, price, 4); std::fill(b.begin() + 93, b.begin() + 97, ' '); b[97] = 'Y';
        b[98] = 'P'; b[99] = 'N'; b[104] = 'N'; b[105] = 'N';
        be(b, 50, checksum(pseudo(b, 6, 72)), 2);
        appendFcs(b); return b;
    }
};

class Simulation {
#ifdef VERILATOR
    VHft dut;
#else
    Hft dut;
#endif
    uint32_t data = 0;
    uint8_t bytes = 4;
    bool valid = false, sof = false, eof = false, ready = false;
    bool packetOpen = false, held = false;
    bool stopOutput = false;
    bool fullRate = false;
    unsigned receivedWhileHeld = 0, simultaneousTransfers = 0;
    uint64_t heldWord = 0;
    std::mt19937 random{0x762cab};
    unsigned frames = 0, sizeErrors = 0, crcErrors = 0;
    Bytes output;
    void pins() {
#ifdef VERILATOR
        dut.rx_data_in = data; dut.rx_bytes_in = bytes; dut.rx_valid_in = valid;
        dut.rx_sof_in = sof; dut.rx_eof_in = eof; dut.tx_ready_in = ready; dut.eval();
#endif
    }
    bool inputReady() {
#ifdef VERILATOR
        return dut.rx_ready_out;
#else
        return dut.rx_ready_out();
#endif
    }
public:
    uint64_t cycles = 0;
    unsigned tested = 0;
    Simulation() {
#ifndef VERILATOR
        dut.rx_data_in = _ASSIGN(data); dut.rx_bytes_in = _ASSIGN(bytes);
        dut.rx_valid_in = _ASSIGN(valid); dut.rx_sof_in = _ASSIGN(sof); dut.rx_eof_in = _ASSIGN(eof);
        dut.tx_ready_in = _ASSIGN(ready); dut._assign();
#endif
        reset();
    }
    void reset() {
        valid = false; ready = false; stopOutput = false; pins();
#ifdef VERILATOR
        dut.reset = 1; dut.clk = 0; dut.eval(); dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval(); dut.reset = 0; dut.eval();
#else
        dut._work(true); dut._strobe(); ++_system_clock;
#endif
        held = false; packetOpen = false; frames = 0; sizeErrors = 0; crcErrors = 0; output.clear();
        for (unsigned i = 0; !inputReady() && i < 20000; ++i) step();
        require(inputReady(), "constructor/reset timeout");
    }
    bool step(bool stall = false) {
        bool accepted, txValid, txSof, txEof, sizeError, crcError;
        uint32_t txData, fault;
        uint8_t txBytes;
        uint64_t word;
        ready = !stall && !stopOutput && (fullRate || random() % 4 != 0); pins();
        accepted = valid && inputReady();
#ifdef VERILATOR
        txValid = dut.tx_valid_out; txSof = dut.tx_sof_out; txEof = dut.tx_eof_out;
        txData = dut.tx_data_out; txBytes = dut.tx_bytes_out; fault = dut.fault_out;
        sizeError = dut.frame_size_error_out;
        crcError = dut.frame_crc_error_out;
#else
        txValid = dut.tx_valid_out(); txSof = dut.tx_sof_out(); txEof = dut.tx_eof_out();
        txData = dut.tx_data_out(); txBytes = dut.tx_bytes_out(); fault = dut.fault_out();
        sizeError = dut.frame_size_error_out();
        crcError = dut.frame_crc_error_out();
#endif
        require(!fault, "HLS scheduler fault");
        if (fullRate && packetOpen && ready) require(txValid, "bubble inside TX frame");
        if (accepted && txValid && !ready) ++receivedWhileHeld;
        if (accepted && txValid && ready) ++simultaneousTransfers;
        if (sizeError) {
            ++sizeErrors;
        }
        if (crcError) {
            require(!sizeError, "CRC error mixed with size error");
            ++crcErrors;
        }
        word = txData | (uint64_t(txSof) << 32) | (uint64_t(txEof) << 33) | (uint64_t(txBytes) << 34);
        if (held) require(txValid && word == heldWord, "output changed under backpressure");
        held = txValid && !ready; heldWord = word;
        if (txValid && ready) {
            require(txSof == !packetOpen, "incorrect SOF or overlapping frames");
            require(txBytes > 0 && txBytes <= 4 && (txEof || txBytes == 4), "invalid TX byte count");
            packetOpen = true;
            for (unsigned i = 0; i < txBytes; ++i) output.push_back(uint8_t(txData >> (8 * i)));
            if (txEof) { packetOpen = false; ++frames; }
        }
#ifdef VERILATOR
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
#else
        dut._work(false); dut._strobe(); ++_system_clock;
#endif
        ++cycles;
        return accepted;
    }
    void beat(uint32_t word, unsigned n, bool first, bool last) {
        for (unsigned i = fullRate ? 0 : random() % 3; i; --i) { valid = false; step(); }
        data = word; bytes = uint8_t(n); sof = first; eof = last; valid = true;
        unsigned wait = 0;
        while (!step()) require(++wait < 20000, "RX ready timeout");
        valid = false;
        if (!last && !fullRate) {
            // Exercise input bubbles independently of pipeline latency.
            // Earlier orders can fill the FIFO mid-frame; the next beat
            // must wait for ready just as it does at a frame boundary.
            step();
        }
    }
    void frame(const Bytes& input, const Bytes& expected, bool first = true,
               unsigned expectedSizeErrors = 0, unsigned expectedCrcErrors = 0) {
        ++tested; output.clear(); frames = 0; sizeErrors = 0; crcErrors = 0;
        for (unsigned i = 0; i < input.size(); i += 4) {
            unsigned n = std::min(4u, unsigned(input.size()) - i);
            uint32_t word = 0;
            for (unsigned j = 0; j < n; ++j) word |= uint32_t(input[i + j]) << (8 * j);
            beat(word, n, first && i == 0, i + n == input.size());
        }
        for (unsigned i = 0; !inputReady() && i < 20000; ++i) step();
        require(inputReady(), "packet processing timeout");
        for (unsigned i = 0; !expected.empty() && !frames && i < 20000; ++i) step();
        // Allow every region to drain, including the longest possible TX frame.
        // Gate tests override this bound using the emitted region latencies.
#ifndef HFT_TEST_DRAIN_CYCLES
#define HFT_TEST_DRAIN_CYCLES (3 * HFT_PIPELINE_STAGES + 7 + 64)
#endif
        for (unsigned i = 0; i < HFT_TEST_DRAIN_CYCLES; ++i) step();
        if (output != expected) {
            std::fprintf(stderr, "frame %u: expected %zu bytes, received %zu\n", tested, expected.size(), output.size());
            for (unsigned i = 0; i < std::min(output.size(), expected.size()); ++i)
                if (output[i] != expected[i]) { std::fprintf(stderr, "first difference at byte %u: %02x != %02x\n", i, output[i], expected[i]); break; }
            throw std::runtime_error("order frame mismatch");
        }
        require(!packetOpen && frames == (expected.empty() ? 0u : 1u), "missing or extra output packet");
        require(sizeErrors == expectedSizeErrors, "missing, unexpected or repeated frame-size error");
        require(crcErrors == expectedCrcErrors, "missing, unexpected or repeated frame-CRC error");
        if (!output.empty()) {
            require(checksum(Bytes(output.begin() + 14, output.begin() + 34)) == 0, "bad output IP checksum");
            require(checksum(pseudo(output, 6, 72)) == 0, "bad output TCP checksum");
            require(crcReference(output) == 0x2144df1cu, "bad output Ethernet FCS");
        }
    }
    void resetPartial(const Bytes& input) {
        beat(uint32_t(input[0]) | (uint32_t(input[1]) << 8) | (uint32_t(input[2]) << 16) | (uint32_t(input[3]) << 24), 4, true, false);
        reset(); frame(input, {}, false);
    }
    void restartPartial(const Bytes& abandoned, unsigned prefix, const Bytes& input, const Bytes& expected) {
        require(prefix < abandoned.size() && prefix % 4 == 0, "invalid abandoned prefix");
        for (unsigned i = 0; i < prefix; i += 4) {
            uint32_t word = uint32_t(abandoned[i]) | (uint32_t(abandoned[i + 1]) << 8) |
                (uint32_t(abandoned[i + 2]) << 16) | (uint32_t(abandoned[i + 3]) << 24);
            beat(word, 4, i == 0, false);
        }
        // SOF must replace partial quote fields and all sticky validation state.
        frame(input, expected);
    }
    void cancelOrder(const Bytes& input, bool waitForOutput, unsigned delay = 0) {
        for (unsigned i = 0; i < input.size(); i += 4) {
            unsigned n = std::min(4u, unsigned(input.size()) - i);
            uint32_t word = 0;
            for (unsigned j = 0; j < n; ++j) word |= uint32_t(input[i + j]) << (8 * j);
            beat(word, n, i == 0, i + n == input.size());
        }
        if (waitForOutput) {
            for (unsigned i = 0; !held && i < 20000; ++i) step(true);
            require(held, "did not reach stalled order for reset test");
        }
        for (unsigned i = 0; i < delay; ++i) step(true);
        reset();
        for (unsigned i = 0; i < HFT_TEST_DRAIN_CYCLES; ++i) step();
        require(output.empty(), "reset leaked a cancelled order");
    }
    void resetStages() {
        Quote q{1, 1, 99000, 99001, 100, 100};
        Bytes input = market(q, true);
        // Cancel after each possible pipeline phase, then reuse the sequence:
        // neither an old valid bit nor the duplicate filter may survive reset.
        for (unsigned delay = 0; delay < HFT_TEST_DRAIN_CYCLES; ++delay) {
            Reference oracle;
            reset();
            cancelOrder(input, false, delay);
            frame(input, oracle.expected(q));
        }
    }
    void invalidCount(const Bytes& input, uint8_t count) {
        beat(0, count, true, false);
        frame(input, {}, false);
    }
    void oversizedWithoutEof() {
        sizeErrors = 0; crcErrors = 0;
        for (unsigned i = 0; i < 25; ++i) beat(0, 4, i == 0, false);
        for (unsigned i = 0; !inputReady() && i < 20000; ++i) step();
        require(inputReady() && sizeErrors == 0, "maximum size rejected before exceeding limit");
        beat(0, 4, false, false);
        for (unsigned i = 0; sizeErrors == 0 && i < 20000; ++i) step();
        require(inputReady() && sizeErrors == 1, "oversize error must not wait for EOF");
        for (unsigned i = 0; i < 20; ++i) step();
        require(sizeErrors == 1 && crcErrors == 0, "oversize error repeated while discarding");
    }
    void sendFrame(const Bytes& input, unsigned start = 0) {
        ++tested;
        for (unsigned i = start; i < input.size(); i += 4) {
            unsigned n = std::min(4u, unsigned(input.size()) - i);
            uint32_t word = 0;
            for (unsigned j = 0; j < n; ++j) word |= uint32_t(input[i+j]) << (8*j);
            beat(word, n, i == 0, i+n == input.size());
        }
    }
    void lineRate() {
        reset();
        fullRate = true;
        Reference oracle;
        Bytes expected;
        uint64_t begin = cycles;
        for (unsigned sequence = 1; sequence <= 1000; ++sequence) {
            uint32_t bid = sequence % 2 ? 99000u : 100030u;
            Quote quote{sequence, sequence % 7 ? 2u : 1u, bid, bid + 1, 100, 100};
            Bytes response = oracle.expected(quote);
            expected.insert(expected.end(), response.begin(), response.end());
            Bytes packet = market(quote, sequence % 2);
            for (unsigned offset = 0; offset < packet.size(); offset += 4) {
                data = 0; bytes = uint8_t(std::min(4u, unsigned(packet.size()) - offset));
                for (unsigned lane = 0; lane < bytes; ++lane) data |= uint32_t(packet[offset + lane]) << (8 * lane);
                sof = offset == 0; eof = offset + bytes == packet.size(); valid = true;
                require(step(), "RX failed to accept a word on every clock");
            }
            ++tested;
        }
        valid = false;
        require(cycles - begin == 20000, "line-rate input used extra clocks");
        for (unsigned i = 0; frames < oracle.orders && i < 20000; ++i) step();
        for (unsigned i = 0; i < 200; ++i) step();
        require(output == expected && frames == oracle.orders && !packetOpen, "line-rate orders differ from oracle");
        require(sizeErrors == 0 && crcErrors == 0, "line-rate metadata lost alignment");
        fullRate = false;
        std::puts("II=1: 20000 consecutive RX words; no bubbles within TX frames");
    }
    void concurrent() {
        reset();
        receivedWhileHeld = 0; simultaneousTransfers = 0;
        Reference oracle;
        Bytes expected;
        auto order = [&](unsigned sequence) {
            Quote q{sequence, 1, sequence % 2 ? 99000u : 100030u,
                sequence % 2 ? 99001u : 100031u, 100, 100};
            Bytes response = oracle.expected(q);
            expected.insert(expected.end(), response.begin(), response.end());
            return market(q, true);
        };
        stopOutput = true;
        sendFrame(order(1));
        for (unsigned i = 0; !held && i < 20000; ++i) step();
        require(held, "TX did not produce a held word");
        // An RX error is independent of the concurrently stalled TX frame.
        Bytes corrupt = market(Quote{2,1,99000,99001,100,100}, true);
        corrupt.back() ^= 1;
        sendFrame(corrupt);
        sendFrame(Bytes(4,0));
        // One active TX order plus four queued descriptors, no packet buffers.
        for (unsigned i = 2; i <= 5; ++i) sendFrame(order(i));
        for (unsigned i = 0; i < 200; ++i) step();
        require(receivedWhileHeld >= 80, "RX stopped while TX held an output word");
        require(crcErrors == 1 && sizeErrors == 1, "concurrent RX error reporting failed");
        require(output.empty() && !inputReady(), "full descriptor FIFO did not backpressure RX");
        Bytes sixth = order(6);
        data = uint32_t(sixth[0]) | (uint32_t(sixth[1]) << 8) |
            (uint32_t(sixth[2]) << 16) | (uint32_t(sixth[3]) << 24);
        bytes = 4; sof = true; eof = false; valid = true;
        for (unsigned i = 0; i < 100; ++i)
            require(!step(), "full FIFO accepted an extra RX word");
        stopOutput = false;
        unsigned wait = 0;
        while (!step()) require(++wait < 20000, "FIFO failed to resume RX after TX drain");
        valid = false;
        sendFrame(sixth,4);
        // Continuous randomized frames force FIFO wraparound, mixed decisions,
        // and concurrent RX/TX. No per-frame output drain is allowed here.
        std::mt19937 quotes{0x636f6e63};
        for (unsigned i = 7; i <= 262; ++i) {
            uint32_t bid = i % 7 == 0 ? 0x80000000u + (quotes() & 0xffffffu) : 99800u + quotes() % 400;
            Quote q{i, i % 9 ? 1u : 2u, bid, uint32_t(bid + 1 + quotes() % 30),
                uint32_t(quotes() % 300), uint32_t(quotes() % 300)};
            Bytes response = oracle.expected(q);
            expected.insert(expected.end(), response.begin(), response.end());
            sendFrame(market(q, i % 2));
        }
        for (unsigned i = 0; frames < oracle.orders && i < 20000; ++i) step();
        for (unsigned i = 0; i < 100; ++i) step();
        require(frames == oracle.orders && output == expected && !packetOpen,
            "concurrent FIFO lost, duplicated, reordered or corrupted an order");
        require(simultaneousTransfers != 0, "RX and TX never transferred on the same clock");
        require(crcErrors == 1 && sizeErrors == 1, "RX errors repeated during queue drain");
        // Reset must discard both the active TX frame and queued descriptors.
        stopOutput = true;
        for (unsigned i = 1000; i <= 1004; ++i) sendFrame(order(i));
        for (unsigned i = 0; i < 200; ++i) step();
        require(held && !inputReady(), "reset test did not fill the order queue");
        reset();
        for (unsigned i = 0; i < 300; ++i) step();
        require(output.empty(), "reset leaked an active or queued order");
        oracle = Reference{};
        Quote q{1,1,99000,99001,100,100};
        frame(market(q,true), oracle.expected(q));
    }
};
} // namespace hft_test

inline void hftStreamingTest(hft_test::Simulation& sim) {
    using namespace hft_test;
    Reference oracle;
    std::mt19937 random{0x7374726d};
    sim.reset();
    Quote q{1, 1, 99000, 99001, 100, 100};
    // Every checked header byte gets a malformed frame with valid FCS/IP
    // checksum, then a valid frame with the same sequence. This isolates
    // streaming header rejection from CRC rejection and sequence filtering.
    for (unsigned at : {0u, 1u, 2u, 3u, 4u, 5u, 12u, 13u, 14u, 16u, 17u,
                        20u, 21u, 22u, 23u, 30u, 31u, 32u, 33u, 36u, 37u,
                        38u, 39u, 42u, 43u, 44u, 45u, 46u, 47u, 48u, 49u}) {
        Bytes broken = market(q, false);
        broken.resize(74);
        if (at == 22) broken[at] = 0;
        else broken[at] ^= 1;
        ipChecksum(broken);
        appendFcs(broken);
        sim.frame(broken, {});
        sim.frame(market(q, true), oracle.expected(q));
        ++q.sequence;
    }
    for (unsigned prefix : {4u, 44u, 52u, 60u, 72u, 76u}) {
        Bytes abandoned = market(Quote{0xffffffffu, 2, 1, 2, 0xffffffffu, 0xffffffffu}, false);
        abandoned[0] = 2;
        sim.restartPartial(abandoned, prefix, market(q, true), oracle.expected(q));
        ++q.sequence;
    }
    // Header fields that are not fixed by this example still contribute to
    // checksums; they must not accidentally become part of a fixed-word match.
    for (unsigned i = 0; i < 128; ++i) {
        q.sequence = ((i + 1) << 24) | (random() & 0xffffffu);
        q.bid = i % 2 ? uint32_t(0x80000000u + (random() & 0xfffffff)) : 99000;
        q.ask = q.bid + 1;
        q.bidSize = random() | 0x80000000u; q.askSize = random() | 0x80000000u;
        Bytes input = market(q, false);
        input.resize(74);
        for (unsigned at : {6u, 7u, 8u, 9u, 10u, 11u, 15u, 18u, 19u,
                            26u, 27u, 28u, 29u, 34u, 35u}) input[at] = uint8_t(random());
        input[20] = i % 2 ? 0x40 : 0; // DF is permitted; fragmentation is not.
        input[22] = uint8_t(1 + random() % 255);
        ipChecksum(input);
        uint16_t check = checksum(pseudo(input, 17, 40));
        be(input, 40, check ? check : 65535, 2);
        appendFcs(input);
        sim.frame(input, oracle.expected(q));
    }
}

inline int hftTest() {
    using namespace hft_test;
    try {
        wordNetworks();
        Simulation sim;
        Reference oracle;
        std::mt19937 random{0x534245};
        require(crcReference(Bytes{'1','2','3','4','5','6','7','8','9'}) == 0xcbf43926u,
            "CRC reference does not match the standard check vector");
        for (unsigned i = 1; i <= 1000; ++i) {
            Quote q{i, random() % 10 ? 1u : 2u, uint32_t(99800 + random() % 400), 0,
                uint32_t(random() % 300), uint32_t(random() % 300)};
            q.ask = q.bid + 1 + random() % 30;
            if (i % 19 == 0) q.sequence = i - 1;
            if (i % 23 == 0) q.ask = q.bid;
            if (i % 31 == 0) q.bid = 0;
            sim.frame(market(q, i % 2), oracle.expected(q));
        }
        require(oracle.buys > 100 && oracle.sells > 100 && oracle.orders < 800, "insufficient decision coverage");
        for (unsigned defect = 0; defect < 14; ++defect) {
            Quote q{1001 + defect, 1, 99000, 99001, 100, 100};
            Bytes b = market(q, false);
            b.resize(b.size() - 4);
            switch (defect) {
                case 0: b[0] = 2; break;
                case 1: b[12] = 0x86; break;
                case 2: b[14] = 0x46; ipChecksum(b); break;
                case 3: b[17] = 59; ipChecksum(b); break;
                case 4: b[20] = 0x20; ipChecksum(b); break;
                case 5: b[24] ^= 1; break;
                case 6: b[23] = 6; ipChecksum(b); break;
                case 7: b[33] = 4; ipChecksum(b); break;
                case 8: b[37] ^= 1; break;
                case 9: b[39] = 39; break;
                case 10: b[40] = 1; break;
                case 11: b[44] = 2; break;
                case 12: b.resize(70); break;
                case 13: b.resize(100); break;
            }
            appendFcs(b);
            sim.frame(b, {}, true, defect == 13 ? 1u : 0u);
            // Rejection must not consume the quote's sequence number.
            sim.frame(market(q, true), oracle.expected(q));
            sim.frame(market(q, true), {}); // exact duplicate
        }
        Quote q{2000, 1, 99000, 99001, 100, 100};
        sim.resetPartial(market(q, true)); oracle = Reference{};
        sim.frame(market(q, true), oracle.expected(q));
        for (bool waitForOutput : {false, true}) {
            ++q.sequence;
            sim.cancelOrder(market(q, true), waitForOutput); oracle = Reference{};
            sim.frame(market(q, true), oracle.expected(q));
        }
        for (uint8_t count : {0, 1, 5, 12}) {
            ++q.sequence;
            sim.invalidCount(market(q, true), count);
            sim.frame(market(q, true), oracle.expected(q));
        }
        // Ethernet minimum including FCS, and this example's frame-size limit.
        // The oracle deliberately uses literal limits, not DUT constants.
        for (unsigned length : {1u, 2u, 3u, 4u, 63u, 64u, 65u, 66u, 67u, 99u, 100u, 101u, 102u, 103u, 104u, 256u}) {
            Bytes b(length >= 4 ? length - 4 : length, 0);
            if (length >= 4) appendFcs(b);
            sim.frame(b, {}, true, length < 64 || length > 100 ? 1u : 0u);
            ++q.sequence;
            sim.frame(market(q, true), oracle.expected(q));
        }
        // Every bit of the FCS, and each byte lane of the data, must be checked.
        for (unsigned bit = 0; bit < 36; ++bit) {
            ++q.sequence;
            Bytes b = market(q, true);
            unsigned at = bit < 32 ? unsigned(b.size()) - 4 + bit / 8 : 58 + bit - 32;
            b[at] ^= uint8_t(1u << (bit % 8));
            sim.frame(b, {}, true, 0, 1);
            sim.frame(market(q, true), oracle.expected(q));
        }
        sim.oversizedWithoutEof();
        ++q.sequence;
        sim.frame(market(q, true), oracle.expected(q));
        // Exercise arbitrary data bits and all four tail counts independently
        // of the fixed quote format. CRC-valid non-quote frames do not trade.
        for (unsigned i = 0; i < 256; ++i) {
            Bytes b(60 + i % 37);
            for (auto& byte : b) byte = uint8_t(random());
            b[0] = 2; // deliberately not the subscribed multicast address
            appendFcs(b);
            sim.frame(b, {});
            b.back() ^= 1;
            sim.frame(b, {}, true, 0, 1);
        }
        hftStreamingTest(sim);
        sim.concurrent();
        sim.lineRate();
        sim.resetStages();
        std::printf("PASS: 1000 randomized quotes, concurrent RX/TX, descriptor FIFO full/wrap/reset, streaming headers/application records, CRC/checksums, frame-size boundaries and backpressure; %u frames, %llu clocks\n",
            sim.tested, (unsigned long long)sim.cycles);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

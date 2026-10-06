#pragma once
#include "EthernetCrc.h"
#include "WordMath.h"

struct HftQuote {
    uint32_t sequence, symbol, bid, ask, bid_size, ask_size;
};

struct HftWordParts {
    uint32_t word, metadata;
    uint32_t ip0, ip1, udp0, udp1;
};

struct HftCollection {
    HftQuote quote;
    uint32_t crc, ip_sum, udp_sum;
    bool ethernet_ok, ipv4_ok, udp_ok, sbe_ok, udp_checksum_present;
};

struct HftFrameCheck {
    HftCollection collection;
    bool complete, normal_length, size_error;
};

struct HftFoldedFrame {
    HftQuote quote;
    uint32_t ip_sum, udp_sum;
    bool valid, udp_checksum_present, size_error, crc_error;
};

struct HftCandidate {
    uint32_t sequence, symbol, bid, ask;
    bool bid_liquid, ask_liquid;
    bool valid, size_error, crc_error;
};

// Five elastic stages, with one-cycle accumulation and sequence feedback.
class HftCollector : public cpphdl::Module {
public:
    _PORT(uint64_t) data_in;
    _PORT(bool) valid_in;
    _PORT(bool) ready_out;
    _PORT(uint32_t) sequence_out;
    _PORT(uint32_t) symbol_out;
    _PORT(uint64_t) bid_out;
    _PORT(uint64_t) ask_out;
    _PORT(bool) valid_out;
    _PORT(bool) ready_in;
    _PORT(bool) size_error_out;
    _PORT(bool) crc_error_out;
private:
    cpphdl::reg<cpphdl::u<5>> valid_reg;
    cpphdl::reg<HftWordParts> parts_reg;
    cpphdl::reg<HftCollection> collection_reg;
    cpphdl::reg<HftFrameCheck> check_reg;
    cpphdl::reg<HftFoldedFrame> folded_reg;
    cpphdl::reg<HftCandidate> candidate_reg, offer_reg;
    cpphdl::reg<cpphdl::u32> sequence_reg;

    static uint32_t fold(uint32_t sum) {
        sum = HftWordMath::add(sum & 65535u, sum >> 16);
        return HftWordMath::add(sum & 65535u, sum >> 16);
    }
    static HftWordParts parts(uint64_t beat) {
        uint32_t offset, bytes, first, second;
        HftWordParts result{};
        result.word = uint32_t(beat);
        result.metadata = uint32_t(beat >> 32);
        offset = (result.metadata >> 12) & 255u;
        bytes = result.metadata >> 26;
        first = ((result.word & 255u) << 8) | (bytes >= 2 ? (result.word >> 8) & 255u : 0u);
        second = ((result.word >> 8) & 0xff00u) | (bytes == 4 ? result.word >> 24 : 0u);
        result.ip0 = offset >= 14 && offset < 34 && bytes != 0 ? first : 0u;
        result.ip1 = offset >= 12 && offset < 32 && bytes >= 3 ? second : 0u;
        result.udp0 = offset >= 26 && offset < 74 && bytes != 0 ? first : 0u;
        result.udp1 = offset >= 24 && offset < 72 && bytes >= 3 ? second : 0u;
        return result;
    }
    static HftQuote applicationWord(HftQuote quote, uint32_t offset, uint32_t word) {
        if (offset == 48) quote.sequence = word >> 16;
        else if (offset == 52) { quote.sequence |= word << 16; quote.symbol = word >> 16; }
        else if (offset == 56) { quote.symbol |= word << 16; quote.bid = word >> 16; }
        else if (offset == 60) { quote.bid |= word << 16; quote.ask = word >> 16; }
        else if (offset == 64) { quote.ask |= word << 16; quote.bid_size = word >> 16; }
        else if (offset == 68) { quote.bid_size |= word << 16; quote.ask_size = word >> 16; }
        else if (offset == 72) quote.ask_size |= word << 16;
        return quote;
    }
    static HftCollection collect(HftCollection state, HftWordParts beat) {
        uint32_t offset;
        offset = (beat.metadata >> 12) & 255u;
        if (beat.metadata & (1u << 24)) {
            state.crc = 0xffffffffu;
            state.ip_sum = 0; state.udp_sum = 57;
            state.ethernet_ok = true; state.ipv4_ok = true;
            state.udp_ok = true; state.sbe_ok = true;
            state.udp_checksum_present = false;
        }
        if (beat.metadata & 512u) {
            state.crc = EthernetCrc::word(state.crc, beat.word, beat.metadata >> 26);
            // At most 100 frame bytes: raw sums cannot overflow uint32_t.
            // Folding is a separate stage, not part of this recurrence.
            state.ip_sum = HftWordMath::add(HftWordMath::add(state.ip_sum, beat.ip0), beat.ip1);
            state.udp_sum = HftWordMath::add(HftWordMath::add(state.udp_sum, beat.udp0), beat.udp1);
            state.ethernet_ok = state.ethernet_ok && bool(beat.metadata & (1u << 20));
            state.ipv4_ok = state.ipv4_ok && bool(beat.metadata & (1u << 21));
            state.udp_ok = state.udp_ok && bool(beat.metadata & (1u << 22));
            state.sbe_ok = state.sbe_ok && bool(beat.metadata & (1u << 23));
            if (offset == 40) state.udp_checksum_present = (beat.word & 65535u) != 0;
            state.quote = applicationWord(state.quote, offset, beat.word);
        }
        return state;
    }
    static HftFoldedFrame finish(HftFrameCheck frame) {
        HftFoldedFrame result{};
        result.quote = frame.collection.quote;
        result.ip_sum = fold(frame.collection.ip_sum);
        result.udp_sum = fold(frame.collection.udp_sum);
        result.udp_checksum_present = frame.collection.udp_checksum_present;
        result.size_error = frame.size_error;
        result.crc_error = frame.complete && !frame.size_error && frame.collection.crc != 0xdebb20e3u;
        result.valid = frame.complete && frame.normal_length && !frame.size_error && !result.crc_error &&
            frame.collection.ethernet_ok && frame.collection.ipv4_ok && frame.collection.udp_ok && frame.collection.sbe_ok;
        return result;
    }
    static HftCandidate validate(HftFoldedFrame frame) {
        HftCandidate result{};
        result.sequence = frame.quote.sequence;
        result.symbol = frame.quote.symbol;
        result.bid = frame.quote.bid;
        result.ask = frame.quote.ask;
        result.bid_liquid = !HftWordMath::less(frame.quote.bid_size, 100);
        result.ask_liquid = !HftWordMath::less(frame.quote.ask_size, 100);
        result.size_error = frame.size_error;
        result.crc_error = frame.crc_error;
        result.valid = frame.valid && frame.ip_sum == 65535 &&
            (!frame.udp_checksum_present || frame.udp_sum == 65535) &&
            frame.quote.symbol >= 1 && frame.quote.symbol <= 4 && frame.quote.sequence != 0 &&
            frame.quote.bid != 0 && HftWordMath::less(frame.quote.bid, frame.quote.ask);
        return result;
    }
    HftCollection collection_comb;
    const HftCollection& collection_comb_func() {
        collection_comb = collect(collection_reg, parts_reg);
        return collection_comb;
    }
    HftFrameCheck check_comb;
    const HftFrameCheck& check_comb_func() {
        check_comb = HftFrameCheck{};
        check_comb.collection = collection_comb_func();
        check_comb.complete = bool(parts_reg.metadata & 512u) && bool(parts_reg.metadata & (1u << 25));
        check_comb.normal_length = (parts_reg.metadata & 255u) == 78;
        check_comb.size_error = bool(parts_reg.metadata & 1024u);
        return check_comb;
    }
    HftCandidate offer_comb;
    const HftCandidate& offer_comb_func() {
        offer_comb = candidate_reg;
        offer_comb.valid = candidate_reg.valid && HftWordMath::less(uint32_t(sequence_reg), candidate_reg.sequence);
        return offer_comb;
    }
public:
    void _assign() {
        valid_out = _ASSIGN(bool(valid_reg[4]) && offer_reg.valid);
        ready_out = _ASSIGN(!valid_out() || ready_in());
        sequence_out = _ASSIGN(offer_reg.sequence);
        symbol_out = _ASSIGN(offer_reg.symbol);
        bid_out = _ASSIGN(uint64_t(offer_reg.bid) | (uint64_t(offer_reg.bid_liquid) << 32));
        ask_out = _ASSIGN(uint64_t(offer_reg.ask) | (uint64_t(offer_reg.ask_liquid) << 32));
        size_error_out = _ASSIGN(bool(valid_reg[4]) && ready_out() && offer_reg.size_error);
        crc_error_out = _ASSIGN(bool(valid_reg[4]) && ready_out() && offer_reg.crc_error);
    }
    void _work(bool reset) {
        if (reset) {
            valid_reg.clr(); parts_reg.clr(); collection_reg.clr(); check_reg.clr();
            folded_reg.clr(); candidate_reg.clr(); offer_reg.clr(); sequence_reg.clr();
        } else if (ready_out()) {
            valid_reg._next = (uint32_t(valid_reg) << 1) | uint32_t(valid_in());
            if (valid_in()) parts_reg._next = parts(data_in());
            if (valid_reg[0]) {
                collection_reg._next = collection_comb_func();
                check_reg._next = check_comb_func();
            }
            if (valid_reg[1]) folded_reg._next = finish(check_reg);
            if (valid_reg[2]) candidate_reg._next = validate(folded_reg);
            if (valid_reg[3]) {
                offer_reg._next = offer_comb_func();
                if (offer_comb_func().valid) sequence_reg._next = candidate_reg.sequence;
            }
        }
    }
    void _strobe() {
        valid_reg.strobe(); parts_reg.strobe(); collection_reg.strobe(); check_reg.strobe();
        folded_reg.strobe(); candidate_reg.strobe(); offer_reg.strobe(); sequence_reg.strobe();
    }
};

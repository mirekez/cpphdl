#include "../../Clocked.h"
#include "ReceiveMetadata.h"

// Each call parses one independent word; framing metadata travels with its data.
class HftWordMethods {
public:
    bool ethernet(uint32_t offset, uint32_t word) const {
        if (offset == 0) return word == 0x015e0001;
        if (offset == 4) return (word & 65535) == 0x0302;
        if (offset == 12) return (word & 65535) == 0x0008;
        return true;
    }
    bool ipv4(uint32_t offset, uint32_t word) const {
        if (offset == 12) return ((word >> 16) & 255) == 0x45;
        if (offset == 16) return (word & 65535) == 0x3c00;
        if (offset == 20) return (word & 0xffbf) == 0 &&
            ((word >> 16) & 255) != 0 && (word >> 24) == 17;
        if (offset == 28) return (word >> 16) == 0x01ef;
        if (offset == 32) return (word & 65535) == 0x0302;
        return true;
    }
    bool udp(uint32_t offset, uint32_t word) const {
        return offset != 36 || word == 0x28002823; // destination 9000, length 40
    }
    bool sbe(uint32_t offset, uint32_t word) const {
        // Header starts at byte 42; the quote starts at byte 50.
        if (offset == 40) return (word >> 16) == 24;
        if (offset == 44) return word == 0x002a0001;
        if (offset == 48) return (word & 65535) == 0;
        return true;
    }
    uint64_t command(uint32_t position, uint32_t flags, uint32_t word) {
        uint32_t offset = position >> 16;
        uint32_t checks = uint32_t(ethernet(offset, word)) |
            (uint32_t(ipv4(offset, word)) << 1) | (uint32_t(udp(offset, word)) << 2) |
            (uint32_t(sbe(offset, word)) << 3);
        uint32_t metadata = (position & 4095u) | (offset << 12) |
            (checks << 20) | ((flags & 31u) << 24);
        return (uint64_t(metadata) << 32) | word;
    }
};

#include "HftCollector.h"

class HftDecisionMethods {
public:
    uint64_t command(uint32_t sequence, uint32_t bid, uint32_t ask) {
        if (ask != 0 && ask < 100000) return (uint64_t(sequence) << 32) | ask;
        if (bid > 100020) return (uint64_t(sequence) << 32) | bid;
        return 0;
    }
};

// Output frame generation is independent of input parsing. No packet buffer.
class HftTxMethods {
    struct Order {
        uint32_t sequence = 0, price = 0, tcp_sequence = 0;
        uint16_t checksum = 0;
        bool buy = false;
    } order;
    uint32_t tcp_sequence = 1000;
    static uint32_t fold(uint32_t sum) {
        sum = (sum & 65535u) + (sum >> 16);
        return (sum & 65535u) + (sum >> 16);
    }
    static uint32_t hex(uint32_t nibble) {
        nibble &= 15;
        return nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    static uint32_t tokenPair(uint32_t value) {
        return (hex(value >> 4) << 8) | hex(value);
    }
    void ouch(uint32_t sequence, uint32_t price, bool buy) {
        order.sequence = sequence; order.price = price; order.buy = buy;
    }
    void tcpEthernet() {
        uint32_t sum;
        // Checksum fields precede the payload. Sum the fixed template and
        // variable application fields directly, without building/scanning a frame.
        order.tcp_sequence = tcp_sequence;
        sum = 0x0a00 + 1 + 0x0a00 + 2 + 6 + 72; // IPv4 pseudo-header
        sum += 40000 + 9001 + (tcp_sequence >> 16) + (tcp_sequence & 65535) + 1 + 0x5018 + 4096;
        sum += 50 + ('U' * 256 + 'O') + ('S' * 256 + 'I') + ('M' * 256 + '0') + ('0' * 256 + '0');
        sum += tokenPair(order.sequence >> 24) + tokenPair(order.sequence >> 16) +
            tokenPair(order.sequence >> 8) + tokenPair(order.sequence);
        sum += (order.buy ? 'B' : 'S') * 256;
        sum += (100 * 256 + 'T') + ('E' * 256 + 'S') + ('T' * 256 + ' ') + (' ' * 256 + ' ');
        // Price starts on an odd byte, between the stock and time-in-force.
        sum += (' ' * 256) + (order.price >> 24) + ((order.price >> 8) & 65535) + ((order.price & 255) << 8);
        sum += ' ' + (' ' * 256 + ' ') + (' ' * 256 + 'Y') + ('P' * 256 + 'N') + ('N' * 256 + 'N');
        order.checksum = uint16_t(~fold(sum));
        tcp_sequence += 52;
    }
    uint32_t ethernetWord(uint32_t offset) const {
        // First wire byte is in bits 7:0. Fixed MACs, EtherType, IPv4 version.
        if (offset == 0) return 0x00000002;
        if (offset == 4) return 0x00020200;
        if (offset == 8) return 0x01000000;
        if (offset == 12) return 0x00450008;
        return 0;
    }
    uint32_t ipv4Word(uint32_t offset) const {
        // All IPv4 fields are fixed in this mock session.
        uint32_t checksum = (~fold(0x4500 + 92 + 0x4000 + 0x4006 + 0x0a00 + 1 + 0x0a00 + 2)) & 65535;
        if (offset == 16) return 0x00005c00; // length 92, identification 0
        if (offset == 20) return 0x06400040; // DF, TTL 64, TCP
        if (offset == 24) return 0x000a0000 | (checksum >> 8) | ((checksum & 255) << 8);
        if (offset == 28) return 0x000a0100; // source/destination IPv4 addresses
        return 0;
    }
    uint32_t tcpWord(uint32_t offset) const {
        if (offset == 32) return 0x409c0200; // IPv4 address tail, source port 40000
        if (offset == 36) return 0x2923 | ((order.tcp_sequence >> 24) << 16) |
            (((order.tcp_sequence >> 16) & 255) << 24); // port 9001, sequence high
        if (offset == 40) return ((order.tcp_sequence >> 8) & 255) | ((order.tcp_sequence & 255) << 8);
        if (offset == 44) return 0x18500100; // acknowledgment 1, PSH/ACK
        if (offset == 48) return 0x10 | (uint32_t(order.checksum >> 8) << 16) |
            ((uint32_t(order.checksum) & 255) << 24); // window 4096, checksum
        if (offset == 52) return 0x32000000; // urgent pointer, Soup length 50
        return 0;
    }
    uint32_t ouchWord(uint32_t offset) const {
        if (offset == 56) return 'U' | ('O' << 8) | ('S' << 16) | ('I' << 24);
        if (offset == 60) return 'M' | ('0' << 8) | ('0' << 16) | ('0' << 24);
        if (offset == 64) return hex(order.sequence >> 28) | (hex(order.sequence >> 24) << 8) |
            (hex(order.sequence >> 20) << 16) | (hex(order.sequence >> 16) << 24);
        if (offset == 68) return hex(order.sequence >> 12) | (hex(order.sequence >> 8) << 8) |
            (hex(order.sequence >> 4) << 16) | (hex(order.sequence) << 24);
        if (offset == 72) return order.buy ? 'B' : 'S';
        if (offset == 76) return 100 | ('T' << 8) | ('E' << 16) | ('S' << 24);
        if (offset == 80) return 'T' | (' ' << 8) | (' ' << 16) | (' ' << 24);
        if (offset == 84) return ' ' | ((order.price >> 24) << 8) |
            (((order.price >> 16) & 255) << 16) | (((order.price >> 8) & 255) << 24);
        if (offset == 88) return order.price & 255;
        if (offset == 92) return (' ' << 8) | (' ' << 16) | (' ' << 24);
        if (offset == 96) return ' ' | ('Y' << 8) | ('P' << 16) | ('N' << 24);
        if (offset == 104) return 'N' | ('N' << 8);
        return 0;
    }
    uint64_t transmit(uint32_t offset) const {
        uint32_t word = 0;
        uint32_t data_count = offset < 104 ? 4u : (offset == 104 ? 2u : 0u);
        uint32_t count = offset < 108 ? 4u : 2u;
        if (offset < 16) word = ethernetWord(offset);
        else if (offset < 32) word = ipv4Word(offset);
        else if (offset < 56) word = tcpWord(offset);
        else word = ouchWord(offset);
        return word | (uint64_t(offset == 0) << 32) | (uint64_t(offset == 108) << 33) |
            (uint64_t(count) << 34) | (uint64_t(data_count) << 37);
    }
public:
    // Commit one order before issuing its independent, explicitly indexed words.
    uint64_t command(uint32_t operation, uint32_t sequence, uint32_t price) {
        if (operation != 0) {
            ouch(sequence, price, operation == 1);
            tcpEthernet();
            return 0;
        }
        return transmit(sequence);
    }
};

#ifndef HFT_PIPELINE_STAGES
#define HFT_PIPELINE_STAGES 4
#endif

// Elastic word pipelines surround short, explicitly ordered stream recurrences.
class Hft : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<HftWordMethods, HFT_PIPELINE_STAGES> receiver;
    HftCollector collector;
    cpphdl::hls::ClockedPipeline<HftDecisionMethods, HFT_PIPELINE_STAGES> decision;
    cpphdl::hls::ClockedPipeline<HftTxMethods, HFT_PIPELINE_STAGES> transmitter;
    _PORT(uint32_t) rx_data_in;
    _PORT(bool) rx_valid_in;
    _PORT(bool) rx_sof_in;
    _PORT(bool) rx_eof_in;
    _PORT(uint8_t) rx_bytes_in;
    _PORT(bool) rx_ready_out;
    _PORT(uint32_t) tx_data_out;
    _PORT(bool) tx_valid_out;
    _PORT(bool) tx_sof_out;
    _PORT(bool) tx_eof_out;
    _PORT(uint8_t) tx_bytes_out;
    _PORT(bool) tx_ready_in;
    _PORT(uint32_t) fault_out;
    _PORT(bool) frame_size_error_out;
    _PORT(bool) frame_crc_error_out;
private:
    // Four slots: two-bit indices wrap naturally and count_reg[2] means full.
    static constexpr unsigned ORDER_DEPTH = 4;
    // Low 32 bits: price; next 32: market sequence; bit 64: buy.
    cpphdl::reg<cpphdl::array<ORDER_DEPTH, cpphdl::logic<65>>> orders;
    cpphdl::reg<cpphdl::u<2>> read_reg, write_reg;
    cpphdl::reg<cpphdl::u<3>> count_reg;
    cpphdl::reg<cpphdl::u<9>> rx_position_reg;
    cpphdl::reg<cpphdl::u32> ingress_word_reg, ingress_position_reg, ingress_flags_reg;
    cpphdl::reg<cpphdl::u<1>> ingress_valid_reg;
    cpphdl::reg<cpphdl::u<7>> tx_offset_reg;
    cpphdl::reg<cpphdl::u32> tx_crc_reg;
    cpphdl::reg<cpphdl::u32> tx_operation_reg, tx_index_reg, tx_value_reg;
    cpphdl::reg<cpphdl::u<1>> tx_command_valid_reg;
    // Load order, await its commit, issue all words, drain the pipeline.
    cpphdl::reg<cpphdl::u<2>> tx_phase_reg;
    bool tx_command_ready_comb;
    const bool& tx_command_ready_comb_func() {
        tx_command_ready_comb = !bool(tx_command_valid_reg) || transmitter.command_ready_out();
        return tx_command_ready_comb;
    }
    cpphdl::logic<65> order_comb;
    const cpphdl::logic<65>& order_comb_func() {
        // A four-way descriptor mux, not a variable shift of the entire FIFO.
        order_comb = orders[0];
        if (read_reg == 1) order_comb = orders[1];
        if (read_reg == 2) order_comb = orders[2];
        if (read_reg == 3) order_comb = orders[3];
        return order_comb;
    }
    uint32_t rx_flags_comb;
    const uint32_t& rx_flags_comb_func() {
        rx_flags_comb = uint32_t(rx_sof_in()) | (uint32_t(rx_eof_in()) << 1) | (uint32_t(rx_bytes_in()) << 2);
        return rx_flags_comb;
    }
    uint32_t rx_position_comb;
    const uint32_t& rx_position_comb_func() {
        uint32_t offset = rx_sof_in() ? 0u : uint32_t(rx_position_reg) & 255u;
        rx_position_comb = HftFraming::step(uint32_t(rx_position_reg), rx_flags_comb_func()) | (offset << 16);
        return rx_position_comb;
    }
    uint32_t tx_crc_comb;
    const uint32_t& tx_crc_comb_func() {
        tx_crc_comb = EthernetCrc::word(tx_sof_out() ? 0xffffffffu : uint32_t(tx_crc_reg),
            uint32_t(transmitter.result_out()), uint32_t((transmitter.result_out() >> 37) & 7));
        return tx_crc_comb;
    }
    uint32_t tx_data_comb;
    const uint32_t& tx_data_comb_func() {
        uint32_t bytes = uint32_t((transmitter.result_out() >> 37) & 7);
        if (bytes == 2) tx_data_comb = (uint32_t(transmitter.result_out()) & 65535u) | ((~tx_crc_comb_func()) << 16);
        else if (bytes == 0) tx_data_comb = (~uint32_t(tx_crc_reg)) >> 16;
        else tx_data_comb = uint32_t(transmitter.result_out());
        return tx_data_comb;
    }
public:
    void _assign() {
        receiver.command_valid_in = _ASSIGN(bool(ingress_valid_reg));
        receiver.operation_in = _ASSIGN(uint32_t(ingress_position_reg));
        receiver.index_in = _ASSIGN(uint32_t(ingress_flags_reg));
        receiver.value_in = _ASSIGN(uint32_t(ingress_word_reg));
        receiver.response_ready_in = _ASSIGN(collector.ready_out());
        receiver._assign();
        rx_ready_out = _ASSIGN(!bool(count_reg[2]) && (!bool(ingress_valid_reg) || receiver.command_ready_out()));

        collector.data_in = _ASSIGN(receiver.result_out());
        collector.valid_in = _ASSIGN(receiver.response_valid_out());
        collector.ready_in = _ASSIGN(decision.command_ready_out());
        collector._assign();
        decision.command_valid_in = _ASSIGN(collector.valid_out());
        decision.operation_in = _ASSIGN(collector.sequence_out());
        decision.index_in = _ASSIGN(collector.bid_out());
        decision.value_in = _ASSIGN(collector.ask_out());
        decision.response_ready_in = _ASSIGN(!bool(count_reg[2]) || decision.result_out() == 0);
        decision._assign();

        transmitter.command_valid_in = _ASSIGN(bool(tx_command_valid_reg));
        transmitter.operation_in = _ASSIGN(uint32_t(tx_operation_reg));
        transmitter.index_in = _ASSIGN(uint32_t(tx_index_reg));
        transmitter.value_in = _ASSIGN(uint32_t(tx_value_reg));
        transmitter.response_ready_in = _ASSIGN(!bool(tx_phase_reg[1]) || tx_ready_in());
        transmitter._assign();
        tx_data_out = _ASSIGN(tx_data_comb_func());
        tx_valid_out = _ASSIGN(bool(tx_phase_reg[1]) && transmitter.response_valid_out());
        tx_sof_out = _ASSIGN(bool((transmitter.result_out() >> 32) & 1));
        tx_eof_out = _ASSIGN(bool((transmitter.result_out() >> 33) & 1));
        tx_bytes_out = _ASSIGN(uint8_t((transmitter.result_out() >> 34) & 7));
        fault_out = _ASSIGN(receiver.fault_out() | decision.fault_out() | transmitter.fault_out());
        frame_size_error_out = _ASSIGN(collector.size_error_out());
        frame_crc_error_out = _ASSIGN(collector.crc_error_out());
    }
    void _work(bool reset) {
        bool push, pop;
        unsigned i;
        receiver._work(reset);
        collector._work(reset);
        decision._work(reset);
        transmitter._work(reset);
        push = decision.response_valid_out() && decision.response_ready_in() && decision.result_out() != 0;
        pop = tx_phase_reg == 0 && count_reg != 0 && tx_command_ready_comb_func();
        if (reset) {
            read_reg.clr(); write_reg.clr(); count_reg.clr();
            rx_position_reg.clr(); tx_phase_reg.clr();
            ingress_word_reg.clr(); ingress_position_reg.clr(); ingress_flags_reg.clr(); ingress_valid_reg.clr();
            tx_offset_reg.clr(); tx_crc_reg.clr();
            tx_operation_reg.clr(); tx_index_reg.clr(); tx_value_reg.clr(); tx_command_valid_reg.clr();
            orders.clr();
        } else {
            if (tx_command_ready_comb_func()) {
                tx_command_valid_reg._next = (tx_phase_reg == 0 && count_reg != 0) || tx_phase_reg == 2;
                tx_operation_reg._next = tx_phase_reg == 0 ? (bool(order_comb_func()[64]) ? 1u : 2u) : 0u;
                tx_index_reg._next = tx_phase_reg == 0 ? uint32_t(uint64_t(order_comb_func()) >> 32) : uint32_t(tx_offset_reg);
                tx_value_reg._next = uint32_t(uint64_t(order_comb_func()));
            }
            if (!bool(ingress_valid_reg) || receiver.command_ready_out())
                ingress_valid_reg._next = rx_valid_in() && !bool(count_reg[2]);
            if (rx_valid_in() && rx_ready_out()) {
                rx_position_reg._next = rx_position_comb_func() & 511u;
                ingress_word_reg._next = rx_data_in();
                ingress_position_reg._next = rx_position_comb_func();
                ingress_flags_reg._next = rx_flags_comb_func();
            }
            if (tx_valid_out() && tx_ready_in()) tx_crc_reg._next = tx_crc_comb_func();
            if (push) {
                for (i = 0; i < ORDER_DEPTH; ++i) {
                    if (write_reg == i) orders._next[i] = cpphdl::cat(
                        cpphdl::logic<1>(uint32_t(decision.result_out()) < 100000u), cpphdl::logic<64>(decision.result_out()));
                }
                write_reg._next = uint32_t(write_reg) + 1u;
            }
            if (pop) {
                read_reg._next = uint32_t(read_reg) + 1u;
                tx_phase_reg._next = 1;
                tx_offset_reg._next = 0;
            }
            if (push != pop) count_reg._next = HftWordMath::add(uint32_t(count_reg), push ? 1u : uint32_t(-1));
            if (tx_phase_reg == 1 && transmitter.response_valid_out()) tx_phase_reg._next = 2;
            if (tx_phase_reg == 2 && tx_command_ready_comb_func()) {
                tx_offset_reg._next = uint32_t(tx_offset_reg) + 4u;
                if (tx_offset_reg == 108) tx_phase_reg._next = 3;
            }
            if (bool(tx_phase_reg[1]) && tx_valid_out() && tx_ready_in() && tx_eof_out()) tx_phase_reg._next = 0;
        }
    }
    void _strobe() {
        receiver._strobe(); collector._strobe(); decision._strobe(); transmitter._strobe();
        read_reg.strobe(); write_reg.strobe(); count_reg.strobe();
        rx_position_reg.strobe(); tx_phase_reg.strobe();
        ingress_word_reg.strobe(); ingress_position_reg.strobe(); ingress_flags_reg.strobe(); ingress_valid_reg.strobe();
        tx_offset_reg.strobe(); tx_crc_reg.strobe();
        tx_operation_reg.strobe(); tx_index_reg.strobe(); tx_value_reg.strobe(); tx_command_valid_reg.strobe();
        orders.strobe();
    }
};

#ifndef SYNTHESIS
static_assert(sizeof(HftCollection) <= 48, "RX must retain only quote fields and stream state");
static_assert(sizeof(HftTxMethods) <= 48, "TX must retain only an order and stream state");
#include "HftTest.h"
int main() { return hftTest(); }
#endif

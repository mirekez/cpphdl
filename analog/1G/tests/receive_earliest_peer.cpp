#include "../receive_sample_pcs_qualified.hpp"
#include "../pma_control.hpp"
#include "../pcs_transmit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <vector>
long _system_clock = -1;

#ifdef VERILATOR
#include "VReceiveSamplePCSQualified.h"
// Test-only adapter; present inputs and settle before observing a clock edge.
class PeerRX : public Module {
    VReceiveSamplePCSQualified rtl;

public:
    _PORT(u<32>) samples_in;
    _PORT(u<16>) tag_in;
    _PORT(bool) valid_in;
    _PORT(bool) restart_in;
    _PORT(u<25>) initial_phase_in;
    _PORT(u<16>) origin_in;
    _PORT(bool) remote_master_in;
    _PORT(bool) pma_ok_in;
    _PORT(u<10>) receive_out = _ASSIGN(u<10>(rtl.receive_out));
    _PORT(bool) receive_valid_out = _ASSIGN(bool(rtl.receive_valid_out));
    _PORT(bool) quality_out = _ASSIGN(bool(rtl.quality_out));
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(bool(rtl.remote_receiver_ok_out));
    _PORT(bool) locked_out = _ASSIGN(bool(rtl.locked_out));
    _PORT(bool) fault_out = _ASSIGN(bool(rtl.fault_out));
    _PORT(bool) symbol_valid_out = _ASSIGN(bool(rtl.symbol_valid_out));
    _PORT(u<16>) symbol_tag_out = _ASSIGN(u<16>(rtl.symbol_tag_out));
    _PORT(u<24>) symbol_fraction_out = _ASSIGN(u<24>(rtl.symbol_fraction_out));
    _PORT(u<25>) timing_period_out = _ASSIGN(u<25>(rtl.timing_period_out));
    _PORT(u<14>) timing_error_out = _ASSIGN(u<14>(rtl.timing_error_out));
    _PORT(bool) timing_confident_out = _ASSIGN(bool(rtl.timing_confident_out));
    _PORT(bool) timing_valid_out = _ASSIGN(bool(rtl.timing_valid_out));
    void _assign() {}
    void prepare(bool reset = false) {
        rtl.clk = 0;
        rtl.reset = reset;
        rtl.samples_in = unsigned(samples_in());
        rtl.tag_in = unsigned(tag_in());
        rtl.valid_in = unsigned(valid_in());
        rtl.restart_in = unsigned(restart_in());
        rtl.initial_phase_in = unsigned(initial_phase_in());
        rtl.origin_in = unsigned(origin_in());
        rtl.remote_master_in = unsigned(remote_master_in());
        rtl.pma_ok_in = unsigned(pma_ok_in());
        rtl.eval();
    }
    void _work(bool reset) {
        prepare(reset);
        rtl.clk = 1;
        rtl.eval();
    }
    void _strobe() {
        rtl.clk = 0;
        rtl.eval();
    }
};
#else
using PeerRX = ReceiveSamplePCSQualified;
#endif

// Diagnostic two-register status crossing. Both assertion and deassertion take
// two destination edges; PMA consumes the previous second-stage state at an edge.
class StatusCrossing : public Module {
public:
    _PORT(u<3>) status_in;
    _PORT(u<3>) status_out = _ASSIGN_REG(second);

private:
    reg<u<3>> first{}, second{};

public:
    void _assign() {}
    void _work(bool reset) {
        first._next = status_in();
        second._next = first;
        if (reset) {
            first._next = 0;
            second._next = 0;
        }
    }
    void _strobe() {
        first.strobe();
        second.strobe();
    }
};

unsigned payload_bytes = 60;
std::vector<unsigned> frame(unsigned pattern) {
    std::vector<unsigned> result(8, 0x55);
    result[7] = 0xd5;
    unsigned crc = 0xffffffff;
    for (unsigned i = 0; i < payload_bytes; ++i) {
        unsigned byte = (i * 83 + pattern * 61) & 255;
        result.push_back(byte);
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    for (unsigned i = 0; i < 4; ++i)
        result.push_back((~crc >> (8 * i)) & 255);
    return result;
}
double pulse(double x) {
    constexpr double pi = 3.14159265358979323846, beta = .35;
    if (std::abs(x) >= 8)
        return 0;
    if (std::abs(x) < 1e-12)
        return 1;
    if (std::abs(std::abs(x) - 1 / (2 * beta)) < 1e-10)
        return (pi / 4) * std::sin(pi / (2 * beta)) / (pi / (2 * beta));
    return std::sin(pi * x) / (pi * x) * std::cos(pi * beta * x) / (1 - 4 * beta * beta * x * x);
}
struct Channel {
    struct Event {
        double time;
        unsigned symbol;
    };
    std::deque<Event> history;
    double period = 1, delay = .105;
    unsigned sample(double time) const {
        unsigned word = 0;
        for (unsigned lane = 0; lane < 4; ++lane) {
            double value = 0;
            for (auto event : history)
                value += (int((event.symbol >> (3 * lane)) & 7) - 2) * pulse((time - event.time - delay) / period - 8);
            word |= unsigned(std::clamp(std::floor(128 + 32 * value + .5), 0., 255.)) << (8 * lane);
        }
        return word;
    }
    void emit(double time, unsigned symbol) {
        history.push_back({time, symbol});
        if (history.size() > 64)
            history.pop_front();
    }
};
struct Endpoint {
    PeerRX rx;
    PMAControl control;
    PCSTransmitter tx;
    StatusCrossing crossing;
    bool master = false, active = false, started = false, arm_tick = false;
    unsigned sample = 0x80808080, tag = 0, status = 0, data = 0, adc_ticks = 0, sent = 0;
    unsigned first_stage = 0, second_stage = 0, idle_errors = 0;
    double period = 1, next = 0, first_link = -1, first_send = -1;
    std::vector<unsigned> send_frame, received;
    bool frame_finished = false;
    Endpoint(bool role, unsigned pattern) : master(role), send_frame(frame(pattern)) {
        rx.samples_in = _ASSIGN(sample);
        rx.tag_in = _ASSIGN(tag);
        rx.valid_in = _ASSIGN(true);
        rx.restart_in = _ASSIGN(false);
        rx.initial_phase_in = _ASSIGN(u<25>(0));
        rx.origin_in = _ASSIGN(u<16>(0));
        rx.remote_master_in = _ASSIGN(!master);
        rx.pma_ok_in = _ASSIGN(true);
        crossing.status_in = _ASSIGN(status);
        control.enable_in = _ASSIGN(true);
        control.restart_in = _ASSIGN(false);
        control.master_in = _ASSIGN(master);
        control.tick_ms_in = _ASSIGN(false);
        control.scrambler_ok_in = _ASSIGN((crossing.status_out() & 1) != 0);
        control.local_receiver_ok_in = _ASSIGN((crossing.status_out() & 2) != 0);
        control.remote_receiver_ok_in = _ASSIGN((crossing.status_out() & 4) != 0);
        control.transmit_active_in = _ASSIGN(active);
        tx.data_in = _ASSIGN(data);
        tx.enable_in = _ASSIGN(active);
        tx.error_in = _ASSIGN(false);
        tx.master_in = _ASSIGN(master);
        tx.mode_in = _ASSIGN(control.mode_out());
        tx.link_in = _ASSIGN(control.mode_out() == 2);
        tx.local_receiver_ok_in = _ASSIGN((crossing.status_out() & 2) != 0);
        tx.seed_load_in = _ASSIGN(control.state_out() == 0);
        tx.seed_in = _ASSIGN(u<33>(master ? 0x123456789ULL : 0x0abced123ULL));
        rx._assign();
        crossing._assign();
        control._assign();
        tx._assign();
    }
    void reset() {
        rx._work(true);
        crossing._work(true);
        control._work(true);
        tx._work(true);
        rx._strobe();
        crossing._strobe();
        control._strobe();
        tx._strobe();
    }
};

void crossing_guards() {
    StatusCrossing dut;
    unsigned input = 0, first = 0, second = 0;
    dut.status_in = _ASSIGN(input);
    dut._assign();
    for (unsigned cycle = 0; cycle < 300; ++cycle) {
        ++_system_clock;
        input = ((cycle / 7) ^ (cycle / 13)) & 7;
        if (unsigned(dut.status_out()) != second)
            throw std::runtime_error("crossing assertion/deassertion delay mismatch");
        const bool reset = cycle == 0 || cycle == 149;
        dut._work(reset);
        dut._strobe();
        second = reset ? 0 : first;
        first = reset ? 0 : input;
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 6)
            throw std::runtime_error("expected ppm delay_UI trace noise_counts payload_bytes");
        const double ppm = std::stod(argv[1]), delay = std::stod(argv[2]);
        const unsigned noise = std::stoul(argv[4]);
        payload_bytes = std::stoul(argv[5]);
        unsigned rng = 0x6d2b79f5;
        std::ofstream trace(argv[3]);
        std::ofstream tx_trace(std::string(argv[3]) + ".tx.csv");
        trace << std::setprecision(17);
        tx_trace << std::setprecision(17);
        crossing_guards();
        Endpoint a(true, 1), b(false, 2);
        Endpoint* ends[2] = {&a, &b};
        Channel channel[2];
        b.period = 1 + ppm * 1e-6;
        b.next = .17;
        channel[0].period = a.period;
        channel[1].period = b.period;
        channel[0].delay = channel[1].delay = delay;
        ++_system_clock;
        a.reset();
        b.reset();
        for (unsigned event = 0; event < 200000; ++event) {
            const double time = std::min(a.next, b.next);
            bool edges[2] = {std::abs(a.next - time) < 1e-10, std::abs(b.next - time) < 1e-10};
            ++_system_clock;
            for (unsigned side = 0; side < 2; ++side) {
                if (!edges[side])
                    continue;
                auto& e = *ends[side];
                const auto& peer = *ends[1 - side];
                e.sample = channel[1 - side].sample(time);
                if (noise) {
                    unsigned perturbed = 0;
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        rng ^= rng << 13;
                        rng ^= rng >> 17;
                        rng ^= rng << 5;
                        int value = int((e.sample >> (8 * lane)) & 255) + int(rng % (2 * noise + 1)) - int(noise);
                        perturbed |= unsigned(std::max(0, std::min(255, value))) << (8 * lane);
                    }
                    e.sample = perturbed;
                }
                e.tag = e.adc_ticks & 65535;
#ifdef VERILATOR
                e.rx.prepare();
#endif
                const bool locked = e.rx.locked_out(), quality = e.rx.quality_out(),
                           remote = e.rx.remote_receiver_ok_out();
                const bool output_valid = e.rx.receive_valid_out();
                const unsigned output = unsigned(e.rx.receive_out());
                e.status = unsigned(locked) | (unsigned(quality) << 1) | (unsigned(remote) << 2);
                const bool source_edge = (e.adc_ticks & 1) == 0;
                const bool link = e.control.link_out();
                if (source_edge) {
                    if (unsigned(e.crossing.status_out()) != e.second_stage)
                        throw std::runtime_error("live crossing schedule mismatch");
                    if (link && e.first_link < 0) {
                        e.first_link = time;
                        std::cout << "side " << side << " first_link=" << time << '\n';
                    }
                    if (!e.started && link && e.arm_tick) {
                        e.started = true;
                        e.first_send = time;
                        std::cout << "side " << side << " first_send=" << time << '\n';
                    }
                    e.active = e.started && e.sent < e.send_frame.size();
                    e.data = e.active ? e.send_frame[e.sent] : 0;
                    if (!e.started)
                        e.arm_tick = link && e.control.mode_out() == 2;
                    if (e.active)
                        ++e.sent;
                }
                trace << event << ',' << time << ',' << side << ',' << e.adc_ticks << ',' << e.sample << ',' << output
                      << ',' << output_valid << ',' << locked << ',' << quality << ',' << remote << ','
                      << unsigned(e.control.state_out()) << ',' << unsigned(e.control.mode_out()) << ',' << link << ','
                      << e.active << ',' << e.data << ',' << e.status << ',' << unsigned(e.crossing.status_out()) << ','
                      << unsigned(e.rx.symbol_tag_out()) << ',' << unsigned(e.rx.symbol_fraction_out()) << ','
                      << e.rx.symbol_valid_out() << ',' << unsigned(e.rx.timing_period_out()) << ','
                      << unsigned(e.rx.timing_error_out()) << ',' << e.rx.timing_confident_out() << ','
                      << e.rx.timing_valid_out() << '\n';
                if (e.rx.fault_out())
                    throw std::runtime_error("continuous fixed-grid input faulted");
                if (output_valid && locked) {
                    e.idle_errors += (output & 512) && !(output & 256);
                    if (output & 256) {
                        if (!quality || (output & 512)) {
                            std::cerr << "side " << side << " bad_frame time=" << time << " quality=" << quality
                                      << " word=" << output << " byte=" << e.received.size() << '\n';
                            throw std::runtime_error("earliest allowed frame lacks receive quality or has RX_ER");
                        }
                        if (e.received.size() >= peer.send_frame.size() ||
                            (output & 255) != peer.send_frame[e.received.size()])
                            throw std::runtime_error("earliest allowed frame byte differs from independent GMII truth");
                        e.received.push_back(output & 255);
                    } else if (!e.received.empty()) {
                        if (e.received != peer.send_frame)
                            throw std::runtime_error("earliest allowed frame ended short");
                        e.frame_finished = true;
                    }
                }
                e.rx._work(false);
                if (source_edge) {
                    e.crossing._work(false);
                    e.control._work(false);
                    e.tx._work(false);
                }
            }
            for (unsigned side = 0; side < 2; ++side) {
                if (!edges[side])
                    continue;
                auto& e = *ends[side];
                e.rx._strobe();
                if ((e.adc_ticks & 1) == 0) {
                    e.crossing._strobe();
                    e.control._strobe();
                    e.tx._strobe();
                    e.second_stage = e.first_stage;
                    e.first_stage = e.status;
                    const unsigned symbols = unsigned(e.tx.symbols_out());
                    channel[side].emit(time, symbols);
                    tx_trace << time << ',' << side << ',' << (e.adc_ticks / 2) << ',' << symbols << ',' << e.active
                             << ',' << e.data << '\n';
                }
                ++e.adc_ticks;
                e.next += e.period * .5;
            }
            if (a.frame_finished && b.frame_finished) {
                std::cout << "PASS earliest peer frames ppm=" << ppm << " delay_UI=" << delay
                          << " idle_errors=" << a.idle_errors + b.idle_errors << '\n';
                return 0;
            }
        }
        for (unsigned side = 0; side < 2; ++side)
            std::cerr << "side " << side << " sent=" << ends[side]->sent << " received=" << ends[side]->received.size()
                      << " finished=" << ends[side]->frame_finished << '\n';
        throw std::runtime_error("peer startup/frame deadline expired");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

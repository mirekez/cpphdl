#include "../adc_receive_lane.hpp"
#include "../adc_receive_bank.hpp"
#include <iostream>
#include <stdexcept>

long _system_clock = -1;

// The original nested composition is an independent sequential reference for
// removing readiness from the physical output gates. Exercise both sides of
// every edge, including a reset while conversion and normalization are busy.
class BoundaryHarness : public Module {
public:
    ADCReceiveLaneController<13> lane;
    ADCStartupSequencer<> startup;
    ADCReceiveBankController<13> bank;
    bool enabled = false, sampling = false;
    unsigned comparisons = 0, offset = 0, gain = 256;

    void _assign() {
        lane.enable_in = _ASSIGN(enabled);
        lane.sample_enable_in = _ASSIGN(sampling);
        lane.comparisons_in = _ASSIGN(comparisons);
        lane.offset_in = _ASSIGN(offset);
        lane.gain_in = _ASSIGN(gain);
        startup.enable_in = _ASSIGN(enabled);
        bank.enable_in = _ASSIGN(enabled && startup.ready_out());
        bank.precharge_in = _ASSIGN(false);
        bank.sample_enable_in = _ASSIGN(sampling && startup.ready_out());
        bank.comparisons_in = _ASSIGN(comparisons);
        bank.offset_in = _ASSIGN(offset);
        bank.gain_in = _ASSIGN(gain);
        lane._assign();
        startup._assign();
        bank._assign();
    }
    void _work(bool reset) {
        lane._work(reset);
        startup._work(reset);
        bank._work(reset);
    }
    void _strobe() {
        lane._strobe();
        startup._strobe();
        bank._strobe();
    }
};

int main() {
    try {
        BoundaryHarness harness;
        auto& lane = harness.lane;
        auto& startup = harness.startup;
        auto& bank = harness.bank;
        auto& enabled = harness.enabled;
        auto& sampling = harness.sampling;
        auto& comparisons = harness.comparisons;
        auto& offset = harness.offset;
        auto& gain = harness.gain;
        harness._assign();
        unsigned ready_edges = 0, busy_resets = 0, disabled_edges = 0;
        unsigned random = 0x12345678;
        for (unsigned clock = 0; clock < 60000; ++clock) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            // Long enabled intervals reach ready; interruptions strike during
            // precharge, opening, active SAR work and pending normalization.
            const unsigned age = clock % 6000;
            const bool reset = age == 0 || age == 4501 || age == 4502;
            enabled = age != 4100 && age != 5990;
            sampling = (random & 7) != 0;
            comparisons = random & 65535;
            offset = (random >> 16) & 255;
            gain = 200 + ((random >> 24) & 255);
            // Alternate uninterrupted startups with pre-ready enable drops.
            if ((clock / 6000) % 2 == 0 && age == 4100)
                enabled = true;
            ++_system_clock;
            ready_edges += unsigned(startup.ready_out());
            busy_resets += unsigned(reset && startup.ready_out());
            disabled_edges += unsigned(!enabled);
            for (unsigned phase = 0; phase < 2; ++phase) {
                if (phase) {
                    harness._work(reset);
                    harness._strobe();
                    ++_system_clock;
                }
                if (lane.valid_out() != bank.valid_out() || lane.data_out() != bank.data_out() ||
                    lane.slice_out() != bank.slice_out() || lane.fault_out() != bank.fault_out() ||
                    lane.normalized_valid_out() != bank.normalized_valid_out() ||
                    lane.normalized_out() != bank.normalized_out() ||
                    lane.normalized_slice_out() != bank.normalized_slice_out() ||
                    lane.clipped_out() != bank.clipped_out() ||
                    lane.startup_ready_out() != startup.ready_out() ||
                    lane.clear_out() != startup.clear_out() || lane.precharge_out() != startup.precharge_out())
                    throw std::runtime_error("lane reset/enable boundary differs from nested reference");
                for (unsigned i = 0; i < 16; ++i) {
                    const bool acquire = i < 13 && ((enabled && startup.precharge_out()) || bank.acquire_out[i]());
                    if (lane.acquire_out[i]() != acquire || lane.convert_out[i]() != bank.convert_out[i]() ||
                        lane.trial_out[i]() != bank.trial_out[i]())
                        throw std::runtime_error("lane physical control differs at reset/enable boundary");
                }
            }
        }
        if (ready_edges < 1000 || busy_resets < 5 || disabled_edges < 10)
            throw std::runtime_error("boundary coverage incomplete");
        std::cout << "PASS 60,000 lane clocks, both edge phases, " << busy_resets
                  << " ready-state resets and " << disabled_edges << " enable drops\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once
#include "autonomous_receive_filter.hpp"
#include "pair_acquisition.hpp"
#include "pcs_receiver.hpp"
#include "receive_quality.hpp"

// Recovered four-lane observations to Ethernet bytes without far-end training
// targets. Local TX references acquire echo first. Residual receive energy then
// admits idle adaptation; actual pair/scrambler/PCS checks qualify reception.
class AutonomousReceivePCS : public Module {
public:
    _PORT(u<32>) samples_in;
    _PORT(u<12>) local_in;
    _PORT(bool) valid_in;
    _PORT(bool) clipped_in;
    _PORT(bool) enable_in;
    _PORT(bool) restart_in;
    _PORT(bool) signal_ready_in;
    _PORT(bool) transport_fault_in;
    _PORT(bool) remote_master_in;
    _PORT(u<10>) receive_out = _ASSIGN(pcs.receive_out());
    _PORT(bool) receive_valid_out = _ASSIGN(pcs.receive_valid_out() && available_comb_func());
    _PORT(bool) locked_out = _ASSIGN(pcs.locked_out() && available_comb_func());
    _PORT(bool) quality_out = _ASSIGN(quality.ready_out() && available_comb_func());
    _PORT(bool) remote_receiver_ok_out = _ASSIGN(pcs.remote_receiver_ok_out() && available_comb_func());
    _PORT(bool) pairs_locked_out = _ASSIGN(pairs.locked_out() && acquired_comb_func());
    _PORT(u<8>) pair_mapping_out = _ASSIGN(pairs.mapping_out());
    _PORT(u<20>) pair_delays_out = _ASSIGN(pairs.delays_out());
    _PORT(bool) acquiring_out = _ASSIGN(phase != 3 || clear_comb_func());
    _PORT(bool) fault_out = _ASSIGN(transport_fault_in() || filter_fault_comb_func());

private:
    AutonomousReceiveFilter filters[4];
    ReceivePairAcquirer pairs;
    PCSReceiver pcs;
    ReceiveQuality quality;
    reg<u<2>> phase{};
    reg<u<14>> accepted{};
    reg<u<8>> energy_count{};
    reg<u<23>> energy[4]{};

    _LAZY_COMB(filter_fault_comb, bool)
        return filter_fault_comb = filters[0].fault_out() || filters[1].fault_out() ||
                                   filters[2].fault_out() || filters[3].fault_out();
    }
    _LAZY_COMB(clear_comb, bool)
        return clear_comb = !enable_in() || restart_in() || !signal_ready_in() || transport_fault_in();
    }
    _LAZY_COMB(acquired_comb, bool)
        return acquired_comb = phase == 3 && !clear_comb_func() && !filter_fault_comb_func();
    }
    _LAZY_COMB(available_comb, bool)
        return available_comb = acquired_comb_func() && pairs.locked_out();
    }
    _LAZY_COMB(samples_comb, u<32>)
        return samples_comb = unsigned(filters[0].sample_out()) | (unsigned(filters[1].sample_out()) << 8) |
                              (unsigned(filters[2].sample_out()) << 16) | (unsigned(filters[3].sample_out()) << 24);
    }
    _LAZY_COMB(valid_comb, bool)
        return valid_comb = filters[0].valid_out() && filters[1].valid_out() &&
                            filters[2].valid_out() && filters[3].valid_out();
    }

public:
    void _assign() {
        for (unsigned i = 0; i < 4; ++i) {
            filters[i].sample_in = _ASSIGN_I((samples_in() >> (8 * i)) & 255);
            filters[i].local_in = _ASSIGN(local_in());
            filters[i].mode_in = _ASSIGN(phase < 2 ? 1 : phase == 2 ? 2 : 3);
            filters[i].advance_in = _ASSIGN(valid_in() && !clear_comb_func());
            filters[i]._assign();
        }
        pairs.samples_in = _ASSIGN(samples_comb_func());
        pairs.enable_in = _ASSIGN(enable_in());
        pairs.restart_in = _ASSIGN(clear_comb_func());
        pairs.signal_ok_in = _ASSIGN(acquired_comb_func());
        pairs.advance_in = _ASSIGN(valid_comb_func());
        pairs.remote_master_in = _ASSIGN(remote_master_in());
        pcs.samples_in = _ASSIGN(pairs.samples_out());
        pcs.symbol_enable_in = _ASSIGN(pairs.valid_out() && available_comb_func());
        pcs.remote_master_in = _ASSIGN(remote_master_in());
        pcs.pma_ok_in = _ASSIGN(available_comb_func());
        quality.receive_in = _ASSIGN(pcs.receive_out());
        quality.valid_in = _ASSIGN(pcs.receive_valid_out());
        quality.locked_in = _ASSIGN(pcs.locked_out());
        quality.fault_in = _ASSIGN(transport_fault_in() || filter_fault_comb_func());
        quality.clipped_in = _ASSIGN(valid_in() && clipped_in());
        quality.enable_in = _ASSIGN(available_comb_func());
        pairs._assign(); pcs._assign(); quality._assign();
    }
    void _work(bool reset) {
        bool remote_energy;
        int sample;
        unsigned sum;
        remote_energy = true;
        sample = 0;
        sum = 0;
        if ((phase == 0 || phase == 2) && valid_in()) {
            accepted._next = accepted + 1;
            if ((phase == 0 && accepted == 8191) || (phase == 2 && accepted == 16383)) {
                phase._next = phase + 1;
                accepted._next = 0;
            }
        }
        if (phase == 1 && valid_comb_func()) {
            energy_count._next = energy_count + 1;
            for (unsigned lane = 0; lane < 4; ++lane) {
                sample = int(unsigned(filters[lane].sample_out())) - 128;
                sum = unsigned(energy[lane]) + unsigned(sample * sample);
                // All lanes must carry residual signal beyond cancelled echo
                // and ADC noise. Silence alone must not start FFE adaptation.
                remote_energy = remote_energy && sum > 65536;
                energy[lane]._next = energy_count == 255 ? 0 : sum;
            }
            if (energy_count == 255 && remote_energy) {
                phase._next = 2;
                accepted._next = 0;
            }
        }
        for (unsigned lane = 0; lane < 4; ++lane)
            filters[lane]._work(reset || clear_comb_func());
        pairs._work(reset || clear_comb_func());
        pcs._work(reset || !available_comb_func());
        quality._work(reset || clear_comb_func());
        if (reset || clear_comb_func()) {
            phase._next = 0; accepted._next = 0; energy_count._next = 0;
            for (unsigned lane = 0; lane < 4; ++lane)
                energy[lane]._next = 0;
        }
    }
    void _strobe() {
        for (unsigned lane = 0; lane < 4; ++lane) {
            filters[lane]._strobe();
            energy[lane].strobe();
        }
        phase.strobe(); accepted.strobe(); energy_count.strobe();
        pairs._strobe(); pcs._strobe(); quality._strobe();
    }
};

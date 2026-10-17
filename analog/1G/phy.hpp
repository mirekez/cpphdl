#pragma once
#include "channel.hpp"
#include "equalizer.hpp"
#include "hybrid.hpp"
#include "transmitter.hpp"
#include "dfe.hpp"

namespace analog::one_g {
// Electrical front end. ADC, adaptation and clock tracking are explicit sampled-model boundaries.
class receiver : public IC {
public:
    _PORT(electrical) P;
    _PORT(electrical) N;
    _PORT(electrical) LOCAL;
    _PORT(electrical) VCM;
    _PORT(electrical) OUT;
    hybrid cancellation;
    ctle equalizer;

    void _assign() override {
        cancellation.P = P;
        cancellation.N = N;
        cancellation.LOCAL = LOCAL;
        cancellation.VCM = VCM;
        bind(cancellation, Vdd, Vss);
        equalizer.IN = cancellation.OUT;
        equalizer.VCM = VCM;
        equalizer.OUT = OUT;
        equalizer.gain = 3.45; // Also compensate the hybrid's 1 kOhm output loading by the lead divider.
        bind(equalizer, Vdd, Vss);
    }
};

class pair_link : public IC {
public:
    std::array<port<electrical>, 4> A, B;
    _PORT(electrical) CLOCK;
    _PORT(electrical) VREF;
    _PORT(electrical) VCM;
    _PORT(electrical) OUTA;
    _PORT(electrical) OUTB;
    transmitter tx_a, tx_b;
    receiver rx_a, rx_b;
    cable_pair medium;

    void _assign() override {
        for (unsigned i = 0; i < A.size(); ++i) {
            tx_a.CODE[i] = A[i];
            tx_b.CODE[i] = B[i];
        }
        for (auto* tx : {&tx_a, &tx_b}) {
            tx->CLOCK = CLOCK;
            tx->VREF = VREF;
            tx->VCM = VCM;
            bind(*tx, Vdd, Vss);
        }
        medium.AP = tx_a.P;
        medium.AN = tx_a.N;
        medium.BP = tx_b.P;
        medium.BN = tx_b.N;
        medium.VCM = VCM;
        bind(medium, Vdd, Vss);
        rx_a.P = tx_a.P;
        rx_a.N = tx_a.N;
        rx_a.LOCAL = tx_a.REPLICA;
        rx_a.OUT = OUTA;
        rx_b.P = tx_b.P;
        rx_b.N = tx_b.N;
        rx_b.LOCAL = tx_b.REPLICA;
        rx_b.OUT = OUTB;
        for (auto* rx : {&rx_a, &rx_b}) {
            rx->VCM = VCM;
            bind(*rx, Vdd, Vss);
        }
    }
};

class link : public IC {
public:
    std::array<std::array<port<electrical>, 4>, pairs> A, B;
    std::array<port<electrical>, pairs> OUTA, OUTB;
    _PORT(electrical) CLOCK;
    _PORT(electrical) VREF;
    _PORT(electrical) VCM;
    std::array<transmitter, pairs> tx_a, tx_b;
    std::array<receiver, pairs> rx_a, rx_b;
    four_pair_cable medium;

    void _assign() override {
        for (unsigned lane = 0; lane < pairs; ++lane) {
            for (unsigned bit = 0; bit < 4; ++bit) {
                tx_a[lane].CODE[bit] = A[lane][bit];
                tx_b[lane].CODE[bit] = B[lane][bit];
            }
            for (auto* tx : {&tx_a[lane], &tx_b[lane]}) {
                tx->CLOCK = CLOCK;
                tx->VREF = VREF;
                tx->VCM = VCM;
                bind(*tx, Vdd, Vss);
            }
            medium.AP[lane] = tx_a[lane].P;
            medium.AN[lane] = tx_a[lane].N;
            medium.BP[lane] = tx_b[lane].P;
            medium.BN[lane] = tx_b[lane].N;
            rx_a[lane].P = tx_a[lane].P;
            rx_a[lane].N = tx_a[lane].N;
            rx_a[lane].LOCAL = tx_a[lane].REPLICA;
            rx_a[lane].OUT = OUTA[lane];
            rx_b[lane].P = tx_b[lane].P;
            rx_b[lane].N = tx_b[lane].N;
            rx_b[lane].LOCAL = tx_b[lane].REPLICA;
            rx_b[lane].OUT = OUTB[lane];
            for (auto* rx : {&rx_a[lane], &rx_b[lane]}) {
                rx->VCM = VCM;
                bind(*rx, Vdd, Vss);
            }
        }
        medium.VCM = VCM;
        bind(medium, Vdd, Vss);
    }
};
} // namespace analog::one_g

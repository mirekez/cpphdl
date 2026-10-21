# Scope and requirements

1. Express circuit purpose through composable C++ `IC` classes and member ports.
   Every connected device contributes to the coupled electrical equations. Keep
   model fidelity and simulation-only approximations explicit.
2. Target the SFP+ host SFI electrical interface for limiting optical modules,
   using SFF-8418 Rev 1.4. Include module-side DC blocks and nominal 100 Ohm
   differential termination. Model the 10GBASE-R serial rate of 10.3125 Gb/s using NRZ signaling. Provide
   serializer/deserializer, TX emphasis, a channel fixture, CTLE, CDR and DFE, with
   module tests and an integrated transmit-to-receive experiment.
3. Recover clock from received data. The receive CDR must have no transmitter
   clock, ideal recovered-clock waveform or system-clock reference connection.
   A tuned oscillator's nominal range is a design parameter. Arbitrary wide-range
   frequency acquisition is not a requirement of this prototype.
4. Qualify local CDR lock using sustained phase-detector consistency and transition
   activity. An oscillating clock is insufficient. Test loss of transition activity
   and reacquisition. Local CDR lock is distinct from Ethernet PCS block lock.
5. Use three TX taps: precursor, main cursor and postcursor. Generate their history
   in the transmitter; never inject expected receiver decisions as feedback.
   The DFE must use its own capacitor-backed decisions, and a bypass control must
   demonstrate the injected ISI cannot be canceled without that feedback.
6. For every module/system case, independently run native C++ electrical simulation
   and actual generated Verilog-A compiled by OpenVAF and simulated by ngspice.
   Apply the same absolute checker separately to each output dataset. Compare
   only after both checks pass, using documented voltage/timing tolerances.
7. Include independent analytic checks for the passive/active primitive laws,
   CTLE response, loaded channel and three-tap sum. Check recovered serial data,
   all four parallel outputs, and a complete scrambled 66-bit data-block vector.
8. Exercise positive/negative 100 ppm input-rate offsets, bounded input timing and
   voltage disturbances, a transition-free interval, output loading and timestep
   effects. Treat these as finite prototype experiments, not a measured BER floor
   or a complete PMD jitter-tolerance mask.

## Standards boundary

10GBASE-R describes the 64b/66b PCS; the serial PMA and the selected medium's PMD
have additional requirements. IEEE's [sublayer overview](https://www.ieee802.org/3/ak/public/jan03/WP2p0_cls44_1.pdf)
and [PMA function description](https://ieee802.org/3/ap/public/sep05/szczepanek_02_0905.pdf)
explain that separation. These public draft/contribution documents are architectural
references, not substitutes for a selected normative standard edition.

This implementation is an electrical PMA modeling baseline. The 66-bit test vector
checks a data header and self-synchronous `1 + x^39 + x^58` scrambling; the checker
performs independent descrambling. Header bits follow the first-bit-on-the-left
convention in the [public Clause 49 draft, 49.2.4.1–3](https://grouper.ieee.org/groups/802/3/ap/public/sep05/szczepanek_02_0905.pdf):
this bench sends data header 0 then 1, and each payload byte least-significant bit first. It does not implement a complete PCS encoder,
control-block decoder, gearbox, elastic buffer, block-lock state machine, MDIO,
an optical PMD. The selected SFP+ SFI target does not call for KR backplane
startup/training. Fixed CTLE/FFE/DFE settings are configurable receiver/transmitter
design choices. The [SFI contract](sfi.md) records applicable interface
requirements and the compliance work still needed before claiming a conforming PHY.

Recent ISSCC reference-less CDRs establish that the architecture remains active;
they do not impose an Ethernet requirement to reproduce their frequency-acquisition
algorithms. See [ISSCC 2025 paper 7.8](https://www.isscc.org/s/ISSCC2025AdvanceProgram.pdf#page=28)
and [ISSCC 2026 paper 8.5](https://doi.org/10.1109/ISSCC49663.2026.11409236).

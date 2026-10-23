# Requirements and verification boundary

1. Build every electrical component as an `IC` with member ports and line-by-line
   purposes. Other components influence its achieved behavior through shared wires,
   finite impedance and stored charge. No device erases a preceding declaration.
2. Use four bidirectional pairs, five symbol values `{-2, -1, 0, 1, 2}`, and a
   125 MBd symbol rate. Both ends remain electrically connected and transmit
   independent streams simultaneously. Four independent one-way fixtures are
   insufficient evidence for the system test.
3. Retain actual transmit history for the `0.75 + 0.25 z^-1` pulse filter. Its
   seventeen possible intermediate levels must not be mistaken for PAM17 coding.
4. Include finite output impedance, reciprocal channel loading, coupling, imperfect
   hybrid cancellation and receive filtering. Check changed loads and reversed
   direction, not only one nominal waveform.
5. Check each electrical module in C++RTL and Verilog-A/ngspice with independent
   absolute contracts, then compare the passing results using declared tolerances.
6. Exercise a sampled ADC, receive FFE and echo/coupling adaptation on each flow's
   waveform independently. Expected remote symbols are available during the
   declared training interval only. Freeze coefficients before evaluating holdout.
7. Include negative controls: an imbalanced hybrid, disabled DFE and disabled
   adaptation must expose their expected residuals/errors.
8. Put process-specific implementations and tests in `IHPSG13G2/`. Test the IHP
   driver with actual PDK compact models. Generic compact-model and IHP results
   share functional requirements, not identical physics.
9. Encode real GMII frames with the C++RTL transmit PCS, pass them through the
   coupled electrical link in both solvers, and recover them with the unseeded
   receive PCS. Require exact bytes, preamble/SFD, FCS and frame boundaries in
   each flow before comparing recovered streams. Expected symbols may train
   sampled equalization only during the declared training interval.

10. Distinguish the internal weighted-level precision tests from formal
    transmitter compliance. Exercise the required transmitter test waveforms
    through the same physical path, with the proper peak, symmetry, relative
    level, droop, distortion, common-mode, return-loss and timing checks. See
    [transmitter levels and compliance](line-compliance.md).

## Implemented coverage

The original electrical-system constellation mapper covers **normal-data entries only**. Inputs are an
explicit six-bit point and a three-bit subset in printed `Sdn[6:8]` order. Tests
exhaustively check all 512 unique quartets, inverse mapping, parity and the
within-subset squared distance. Original electrical cases use these quartets;
`link_pcs` now uses actual C++RTL transmitters carrying complete Ethernet frames.
`PCSSymbolMapper` includes normal and special code-group tables. Additional
classes implement the Sx/Sy/Sg expansion, alternating scrambling, trellis coding,
unseeded receive acquisition, framing, soft branch metrics and Viterbi decoding.
Native, RTL and IHP-gate contracts and the precise remaining boundary are in
[PCS hardware](pcs.md).

The analog system clock is a bench-supplied 1.2 V signal. Gardner timing recovery
has both a sampled reference and a fixed-point `ReceiveTimingLoop` hardware
controller with noisy closed-loop native/RTL/mapped tests. Its physical sampling
and clock actuator are not yet connected to the electrical transmitter/receiver
network. The receiver exposes continuous
voltage to a sampled eight-bit ADC. Both a floating-point DSP reference and the
actual fixed-point `ReceiveAdaptiveFilter` hardware consume each flow's samples.
The ADC remains an explicit simulation boundary. See [filter hardware](receive_filter.md)
for its function, area and remaining timing/physical implementation work.

`ReceiveClockAligner` implements a separate recovered-GMII/reference clock
crossing with frame buffering, minimum output IFG, Gray-pointer synchronizers,
short-frame completion and overflow/underflow containment. Native, generated
RTL and mapped-gate tests compare exact trajectories. It does not implement
physical clock recovery, reset distribution or routed CDC signoff.

## Work needed for an interoperable PHY

The implemented Gardner detector consumes half-symbol observations. The
physical receive conversion target is therefore 250 MS/s for 125 MBd PAM5,
using an internal receive clock and one external 125 MHz reference. A single
125 MS/s converter stream does not supply those observations. The sixteen-slice
controller and electrical array pass finite 250 MS/s experiments, including
32 consecutive conversions with slice reuse. The
[core-domain receive interface](adc_receive.md) connects physical level-down
gates and a composed calibrated output stream; its larger array experiments
are under qualification.

The following remain unimplemented or unproved: complete standards qualification
of the PCS control/error behavior, process-qualified and continuously connected
link-pulse driver/detection, full startup qualification, physical
implementation/timing of receive adaptation and a closed slave
timing loop. Polarity and scrambler acquisition and full-duplex GMII frame
transfer now have explicit hardware tests. A complete 1000BASE-T PHY still
requires the remaining functions and physical implementations.

Cable pair identification and ±8-symbol relative deskew now have an actual
hardware class and independent native/RTL/IHP tests. The composed aligned PMA
passes native/RTL/IHP frame, startup and retraining checks; its further qualification is
tracked in [pair acquisition](pair_acquisition.md). Input gain normalization,
timing and channel adaptation remain separate requirements.

FLP transmit/receive timing, page arbitration/retry and master/slave role
resolution now have digital hardware and independent native/RTL/IHP unit tests.
The composed two-peer negotiation passes native, RTL and a separate mapped
endurance run; the original complete CTest needs repetition after its timeout
was corrected. [Negotiation](negotiation.md) describes the
physical pulse and PMA-training boundaries. A separate electrical SAR slice now
closes actual controller feedback around both solvers. The interleaved electrical
array passes its one/four/sixteen-slice burst checks in both solvers at 250 MS/s;
it does not yet replace
the full link's simulation ADC boundary.

The generic MOS [FLP interface](flp_frontend.md) now has independent electrical,
refinement and sampled-decoder checks in both solvers. Process sizing,
pad/magnetics loading and full physical negotiation integration remain open.

The lumped RC/coupling fixture is not a measured or worst-case 100 m Cat5 channel.
Magnetics, galvanic isolation, ESD/surge behavior, return-loss masks, transmitter
distortion/jitter templates, BER targets and interoperability are not qualified.
No complete PHY GDSII, DRC, LVS or post-layout timing result is produced here. The generic
gm/switch models omit supply power, noise and intrinsic transistor parasitics;
the earlier IHP driver includes transistor parasitics but retains ideal bias and
gate sources. The newer [biased/trimmed driver](IHPSG13G2/biased_driver.md) replaces
the bias with explicit MOS/resistor circuits and has process-corner PSP tests;
gate stimulus, calibration selection and the linear resistor remain bench/design
boundaries. These limits must remain visible in reports and documentation.

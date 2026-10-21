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

## Implemented coverage

The original electrical-system constellation mapper covers **normal-data entries only**. Inputs are an
explicit six-bit point and a three-bit subset in printed `Sdn[6:8]` order. Tests
exhaustively check all 512 unique quartets, inverse mapping, parity and the
within-subset squared distance. Electrical system stimuli use these quartets;
they are not PCS-generated Ethernet frames. The separate hardware `PCSSymbolMapper`
adds the normal and special code-group tables, supplied per-pair sign inversions,
and explicit invalid-code reporting. `PCSSideStream` adds the 33-bit master/slave
LFSR recurrences. Neither implements the complete PCS; see [PCS hardware](pcs.md).

The analog system clock is a bench-supplied 1.2 V signal. Gardner timing recovery
is a separately tested sampled algorithm; it is not yet connected back into the
electrical transmitter/receiver clock network. The receiver exposes continuous
voltage to a sampled eight-bit ADC and C++ DSP reference model. This is an
explicit simulation boundary, not a hidden ideal ADC/sense primitive.

`ReceiveClockAligner` implements a separate recovered-GMII/reference clock
crossing with frame buffering, minimum output IFG, Gray-pointer synchronizers,
short-frame completion and overflow/underflow containment. Native, generated
RTL and mapped-gate tests compare exact trajectories. It does not implement
physical clock recovery, reset distribution or routed CDC signoff.

## Work needed for an interoperable PHY

The following are not implemented or proved by these tests: complete Clause 40
side-stream scrambling/sign coding, trellis encoder/Viterbi decoder, SSD/ESD and
idle/control handling, GMII transactions, auto-negotiation, master/slave startup,
pair/polarity identification, deskew and a closed slave timing loop. A complete
1000BASE-T PHY needs those functions in addition to this PAM5/PMA development model.

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

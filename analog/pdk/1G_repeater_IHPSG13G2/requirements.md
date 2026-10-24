# Repeater requirements and implementation boundary

1. Target two full-duplex 1000BASE-T ports, four differential pairs per port,
   125 MBd PAM5 and 1 Gb/s per direction. The intended external clock interface
   has one 125 MHz reference. Internal receive timing loops do not constitute
   additional reference oscillators.
2. Maintain C++ as the source of implemented logic, with class member ports.
   Use the root converter, actual IHP cells and actual KLayout verification.
   A floorplan or generated polygon file alone is not implementation evidence.
3. The implemented core accepts PHY-provided, reference-clock-aligned words:
   bits 7:0 data, bit 8 valid, bit 9 error. Both directions transfer every cycle
   with one registered boundary, preserving bytes, valid/error and IFG. Both
   links and enable must be true; reset or a lost link disables forwarding.
4. Inputs must meet the reference-clock setup/hold contract. Raw recovered-clock
   GMII requires explicit clock-domain alignment. C++ types do not infer CDC.
5. Verify native, generated RTL, mapped gates and routed gates. Check complete
   64/1518-byte Ethernet frames including preamble/SFD, FCS and minimum IFG,
   every 10-bit input combination, link loss, enable and reset. Passing GMII
   frames is not copper-PHY interoperability.
6. Produce routed core GDS with physical masters and power grid. Require full
   PDK DRC including density/antenna, transistor LVS and routed RC timing.
   Missing/failing evidence must fail the flow. Retain logs and artifact hashes.
7. A core-only result must not be labeled a complete repeater chip or fabrication
   signoff. The chip target remains unsuccessful until its missing implementation
   is present and validated.
8. First meet functional and electrical requirements, then minimize verified
   die area. Prefer shared resources and smaller devices when the connected
   circuit still passes the same checks; do not trade away operating margin
   merely to fit a template. There is no user-imposed die-area or power ceiling.
   Size the chip from the implemented PHY/PCS, analog blocks,
   routing, power distribution, pad access and verification margins. Existing
   standalone die templates are test fixtures, not a fixed full-chip budget.
   Enlarging a floorplan requires fresh physical verification.

## Required physical blocks and remaining qualification

The `1G/` library contains implemented controllers and electrical circuits.
The table distinguishes their completed checks from the remaining work for
complete, integrated physical PHYs. Recorded component results below belong
to their tested revisions. Current receiver/transmitter candidates and their
separate acceptance boundaries are tracked in
[IHP qualification status](../../1G/IHPSG13G2/qualification-status.md).
In particular, the thirteen-slice cold receiver and the controller that
includes startup do not inherit the earlier receiver/controller proofs.

| Block | Remaining implementation and evidence |
| --- | --- |
| PCS per port | GMII transmit/receive, Sx/Sy/Sg, trellis coding/decoding, framing and unseeded polarity/scrambler acquisition now exist in `1G/`; complete control/error qualification, startup and chip integration remain |
| Startup | Digital FLP transmit/receive, page arbitration/retry and role resolution have native/RTL/IHP unit tests; composed native/RTL and separate mapped endurance checks pass. Aligned PMA startup/PCS, pair identification, polarity and deskew pass native/RTL/IHP frame and retraining checks. The generic electrical FLP interface passes its separate pulse/decoder checks; continuous physical negotiation and chip integration remain |
| Receive AFE | Physical hybrids, programmable gain/equalization, 250 MS/s conversion for half-symbol timing observations, noise/linearity/headroom and PVT characterization. The 3 pF parallel-drive receiver passes 32 consecutive closed-loop conversions in C++, Verilog-A and actual PSP, including controller decisions and normalization; its PSP acquisition error is 9.554 mV. Physical control/clock translation, full receiver PVT, gain-loop integration, layout and complete receive-chain integration remain |
| Receive DSP | Four filter lanes connected to pair acquisition and PCS pass independent native/RTL/IHP mapped frame checks; their [measured baseline](../../1G/receive_dsp.md) occupies 2.550 mm² of cell area per receive path and fails the 4 ns cell-timing requirement by 3.13 ns. It still needs external supervised targets and aligned recovered-symbol/local-TX inputs. Composed SAR control and current normalization separately pass native/RTL/IHP/routed equivalence, 250 MHz setup/hold with 160 fF control-output loads, full DRC and strict transistor LVS in a 500 × 500 µm macro. Receiver timing closure, routing, autonomous training and complete receive integration remain |
| Timing | `1G/receive_alignment.hpp` implements the digital crossing and passes native/RTL/mapped tests; physical receive recovery, reset distribution and routed CDC/MTBF proof remain |
| TX | The complete sixteen-translator weighted main/history bank passes independent generic flows/comparison and nominal IHP PSP checking with 218 valid-finger MOS devices. Its registered unary controller passes native/RTL/IHP/routed equivalence, setup/hold, DRC and LVS as a standalone macro. PVT/mismatch, analog transistor layout, calibrated standard-qualified amplitude/template and chip integration remain |
| Chip interface | The common-ground IHP pad/ESD cells, seal ring, fill and connected decoupling pass full standalone padframe DRC and strict-port transistor LVS. Enlarging the opening requires fresh checks. Core routing, power distribution, package/pinout and magnetics integration remain |
| Verification | Full-chip post-fill extraction, timing, noise/mismatch, power integrity, DRC/LVS, cable compliance and interoperability |

The core flow implements forwarding and its physical path. The chip target
reports the unresolved integration and exits unsuccessfully. It does not
instantiate empty PHY macros or silently replace simulation-only algorithms
with imaginary physical circuitry.

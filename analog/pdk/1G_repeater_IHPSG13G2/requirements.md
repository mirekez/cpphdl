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

## Required physical blocks that are not implemented

The generic `1G/` PAM5 library is a development model, not these complete,
synthesizable or layout-ready PHY blocks:

| Block | Remaining implementation and evidence |
| --- | --- |
| PCS per port | Hardware symbol/control mapper and LFSRs now exist in `1G/pcs.hpp`; Sx/Sy/Sg expansion, trellis encoder/decoder, transmit/receive state machines and link handling remain |
| Startup | Auto-negotiation, master/slave resolution, training, pair/polarity handling and diagnostics |
| Receive AFE | Physical hybrids, gain/equalization, 125 MS/s conversion, noise/linearity/headroom and PVT characterization |
| Receive DSP | Hardware echo/coupling cancellation, FFE/DFE adaptation and trellis decoding with independent implementation tests |
| Timing | `1G/receive_alignment.hpp` implements the digital crossing and passes native/RTL/mapped tests; physical receive recovery, reset distribution and routed CDC/MTBF proof remain |
| TX | MOS-biased and externally trimmed PSP-tested driver schematics now exist; transistor layout, gate buffering, calibrated standard-qualified amplitude/template and pulse shaping remain |
| Chip interface | Real IHP pad/ESD cells now assemble into a ring with abutment checks; transistor LVS is unresolved, and seal ring, core routing, power distribution, package/pinout and magnetics integration remain |
| Verification | Full-chip post-fill extraction, timing, noise/mismatch, power integrity, DRC/LVS, cable compliance and interoperability |

The core flow implements forwarding and its physical path. The chip target
reports the unresolved integration and exits unsuccessfully. It does not
instantiate empty PHY macros or silently replace simulation-only algorithms
with imaginary physical circuitry.

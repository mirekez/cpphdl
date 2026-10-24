# PCS hardware

The PCS classes use the shared C++HDL `Module` and member-port syntax. They
operate on digital values at the PMA boundary; electrical loading belongs to
connected `IC` models. The target is full-duplex 1000BASE-T at 125 MBd.

| Class | Purpose | Interface / latency |
| --- | --- | --- |
| `PCSSideStream` | Master/slave 33-bit LFSR, seed and hold checks | State advances on an enabled symbol |
| `PCSSymbolMapper` | IEEE Tables 40-1/40-2, including special code groups | One registered stage |
| `PCSTransmitBits` | Sx/Sy/Sg expansion, alternating scrambling, convolutional coding, delimiter/control selection and sign reversal | One registered stage |
| `PCSTransmitter` | Composed GMII-to-PAM5 transmit path | Bit encoder followed by mapper |
| `PCSReceiveSynchronizer` | Acquire remote scrambler state and pair polarities from idle; track without a supplied seed | 33 observed bits and at least 64 qualifying symbols |
| `PCSReceiveFramer` | Confirm SSD, recognize data/reset/ESD, propagate errors and request re-acquisition | One registered observation; SSD1 is committed only after SSD2 |
| `PCSBranchMetrics` | Closest constellation point and squared distance for each of eight subsets | One registered stage |
| `PipelinedPCSBranchMetrics` | Same subset costs, with registered coordinate, pair, group and subset reductions | Four registered stages; output at input edge + 3 |
| `PCSTrellisDecoder` | Eight-state soft-decision Viterbi decoding and terminated-stream draining | 32-observation survivor history |
| `PCSReceiver` | Synchronizer, framer, pipelined metrics, decoder and aligned GMII metadata | 39 accepted symbol ticks from ADC observation to GMII |

Sources are `pcs.hpp`, `pcs_transmit.hpp`, `pcs_synchronize.hpp`,
`pcs_framer.hpp`, `pcs_receive.hpp`, `pcs_branch_metrics_pipeline.hpp` and `pcs_receiver.hpp`. Each `.cpp` entry
point includes its hardware classes for conversion by the root `build/cpphdl`.

## Representations

Four PAM5 values are packed in 12 bits: A in bits 2:0, B in 5:3, C in 8:6 and
D in 11:9. Each three-bit value is **physical symbol + 2**, so codes 0..4 mean
-2..+2. The DAC thermometer decoder remains a separate circuit.

The table subset is the printed string **Sd[6], Sd[7], Sd[8]**: Sd[6] is its
most significant bit. This differs from `(Sd >> 6) & 7`. The encoder reverses
these bits explicitly. The mapper's point is Sd[5:0].

`PCSReceiver.samples_in` packs four unsigned eight-bit ADC samples in A/B/C/D
order. Code 128 means zero; a PAM level spans 32 counts. Its output uses the
shared GMII word format: data in bits 7:0, valid in bit 8 and error in bit 9.
The receiver needs **simultaneous deskewed observations on recovered symbol
ticks**. `symbol_enable_in` advances every stage together, including trellis
draining and frame metadata. Between ticks those registers hold. Reset has
priority over the enable; PMA loss also revokes lock while paused.
`receive_valid_out` is a registered pulse paired with `receive_out`, including
idle words. It is distinct from GMII data-valid bit 8. Downstream logic consumes
only these pulses. Existing symbol-clocked callers bind the enable to true.
The receiver does not provide the analog ADC or recover symbol timing itself.

```cpp
PCSTransmitter tx;
tx.data_in = _ASSIGN(gmii_data);
tx.enable_in = _ASSIGN(gmii_valid);
tx.error_in = _ASSIGN(gmii_error);
tx.master_in = _ASSIGN(local_is_master);
tx.link_in = _ASSIGN(link_ok);
tx.local_receiver_ok_in = _ASSIGN(local_receiver_ok);
tx.mode_in = _ASSIGN(mode);             // 0: SEND_Z, 1: SEND_I, 2: SEND_N
// Bind seed_load_in and a nonzero seed_in, then call _assign().

PCSReceiver rx;
rx.samples_in = _ASSIGN(adc_samples);
rx.symbol_enable_in = _ASSIGN(recovered_symbol_valid);
rx.remote_master_in = _ASSIGN(!local_is_master);
rx.pma_ok_in = _ASSIGN(pma_receive_ok);
rx._assign();
```

A 250 MHz implementation must meet 4 ns timing: rate correction can produce
adjacent symbol-valid events. No multicycle exception follows merely from the
average two samples per symbol. The native/RTL pause regression preserves all
59,456 independent fixture observations across 126,996 poisoned pause clocks
and 32 disabled resets. Earlier 8 ns physical results do not qualify this new
enabled implementation. All three functional trajectories also match after
mapping to 42,388 SG13G2 cells. The new 4 ns slow/1.08 V/125 °C cell-only check
fails with −3.38 ns setup slack after fanout/capacitance repair. Its longest
reported path starts at forced-termination control and reaches a trellis cost
register. That failure is retained in
`out/pcs-symbol-enable/yosys038-mapped/cell_timing.json`; it is not waived by
average symbol rate. An isolated experiment moves the forced metric selection
before the existing final metric register, preserving the symbol latency.
No physical qualification is claimed for that experiment yet.

The receive interface has **no seed input**. During idle D0, the magnitude of
pair A identifies Scr[0]. After collecting the 33-bit history, the synchronizer
checks the recurrence, both odd/even hypotheses and consistent polarity on all
four pairs. It continues the acquired polynomial through data. The framer
requests re-acquisition after invalid groups. PMA failure revokes lock.
Pair ordering and deskew are prerequisites; this is polarity acquisition, not
an implementation of cable-pair identification or skew compensation.

The independent ADC/FFE reference in `adaptation.hpp` remains sampled software.
A usable `pma_ok_in` in a chip requires implemented receive-training and timing
logic. Passing a bench's declared training interval is not that circuitry.

## Coding and buffering

Both LFSRs shift toward increasing bit indices. New bit 0 is old bit 32 XOR
old bit 12 for a master, or old bit 19 for a slave. The auxiliary XOR networks
implement the Clause 40 polynomial construction. Sc[3:1] alternates between
current Sy and inverted previous Sy; Sc[0] uses current Sy. Upper scrambling
bits are present during data. The trellis encoder retains three bits, and two
CSReset symbols terminate it at zero. Sign reversal uses logical OR of the
delayed transmit enables.

`PCSTransmitter` replaces the first two preamble octets with SSD1/SSD2 and
appends two reset symbols and two ESD symbols. Re-arming after link loss requires
quiet GMII. Zero seeds and invalid transmit modes fail closed at the DAC output.

The metric block partitions each 64-point subset into six Cartesian groups.
It evaluates the nearest point in each group, then selects the minimum. A
separate test searches every table entry directly, including tie cases and
full-scale ADC samples. Squared distances fit in 18 bits; ADC subtraction uses
signed host arithmetic before taking a bounded magnitude.

The Viterbi decoder retains eight survivor paths of 32 bytes and uses the known
zero ending state at the second reset symbol. Its 21-bit circular metrics avoid
a serial global-minimum subtraction. Every trellis state can reach every other
state in two symbols. With branch costs bounded by `2^18 - 1`, reachable metric
spread is at most twice that bound; candidate spread is at most three times it,
strictly below the half-range `2^20`. Modular comparisons therefore preserve
the ordinary unbounded-cost ordering, including wraparound. Four-candidate
selection uses parallel pairwise comparisons and preserves predecessor order
on ties. Registered selectors separate metric arithmetic from survivor-history
fanout; a decision stage accounts for the additional output clock.
Pending tail decisions have their own validity schedule. Starting another
stream, including a short malformed stream, cannot erase an earlier frame's
tail. The receiver delays tags and scrambling masks to match those decisions.
SSD1 is provisional: SSD2 confirmation inserts both preamble bytes into that
delay before either is observable on GMII.

The mapper conditions remain: 0 data; 1 transmit error; 2/3 extension error /
extension; 4 CSReset; 5/6 SSD; 7 ESD1; 8/9/10 ESD2 variants; 11 ESD extension
error; 12 idle; 13 SEND_Z. Unsupported mapper combinations report invalid and
produce zero amplitude. The full-duplex receive regression does not qualify
half-duplex collision/carrier-extension operation.

## Verification

All digital blocks have native, generated-RTL and IHP-mapped-gate contracts.
Each flow passes its own checker before complete traces are compared exactly.
The receive system uses an independently encoded stream rather than the DUT
transmitter as its only oracle. Separate integration drives the actual DUT
transmitter through both electrical simulators.

- Transmit: 82,099 vectors; every LFSR basis state, both roles, all fourteen
  conditions, seed errors, training, delimiters, deferred errors and re-arming.
  Auxiliary reference bits come from GF(2) polynomial multiplication.
- Metrics: 16,706 observations checked against exhaustive IEEE-table search.
  The four-stage version checks 16,709 clock states, including pipeline drainage.
- Trellis: independent full-path dynamic programming, clean payloads, moderate
  noise coding gain, excessive-noise failure and overlapping terminated tails;
  17,670 states include 4096 high common-offset costs forcing repeated metric wraps.
- Synchronization: 90,243 observations; arbitrary seeds and phases, both roles,
  sixteen polarity combinations, tracking, remote status, PMA loss and stuck data.
- Framing: 3,730 explicit tag/control/sign observations and bad-SSD recovery.
- Receive system: 64 complete 64/1518-byte Ethernet frames, 51,136 GMII octets,
  preamble/SFD, FCS, minimum IFG and twelve explicit error frames. Another 4,244
  observations inject malformed SSD/ESD, seed changes and PMA failures.
- Electrical `link_pcs`: both ends transmit a complete frame simultaneously over
  all four coupled pairs. Each solver's voltages pass the same analog checks,
  ADC/equalization and actual C++RTL receiver independently, then its recovered
  GMII stream is compared with the other solver's.

Use `check-1g`, or select `ctest --test-dir analog/build -L hardware
--output-on-failure -j 1`. A CTest fixture generates each independent stimulus;
removing or truncating it fails its consumer. `generate-1g-rtl` exports the
catalog; `analog.1g.generated_rtl` checks it for drift.

## Remaining PHY integration

These blocks do not complete PHY startup, auto-negotiation, master/slave
resolution, pair identification/deskew, physical receive conversion/adaptation,
a closed slave timing loop, routed reset/CDC or full-chip integration. The
frame tests establish their stated coding and simulation contracts, not cable
interoperability, a BER target or fabrication signoff.

## Primary references

- IEEE 802.3 Clause 40.3.1.3: scrambler and transmit encoding equations;
  Clause 40.3.1.4 and Figures 40-8 through 40-10: transmit/receive behavior.
  The 2005 encoding excerpts are also reproduced in Appendix 1 of the authors'
  [UC Berkeley report EECS-2009-68](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2009/EECS-2009-68.pdf).
  The report is background evidence, not a claim of current-standard qualification.
- IEEE [Table 40-1](https://www.ieee802.org/3/publication/ab/table40_1.txt) and
  [Table 40-2](https://www.ieee802.org/3/publication/ab/table40_2.txt): independently
  retained numeric fixtures in `tests/pcs_data.csv` and `tests/pcs_controls.csv`.
- IEEE [maintenance request 1030](https://www.ieee802.org/3/maint/requests/maint_1030.pdf):
  delayed-enable / SSD subset corrections relevant to encoding.

Encoding sources checked 2026-10-18. Full standards text is not redistributed.

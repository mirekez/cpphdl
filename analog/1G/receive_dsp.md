# Four-lane receive DSP and PCS connection

[`ReceiveDSP`](receive_dsp.hpp) connects four synthesizable `ReceiveFilter`
instances. Each lane equalizes one received pair and cancels interference from
all four local transmit streams. [`ReceiveDSPPCS`](receive_dsp_pcs.hpp) connects
that bank to cable-pair identification/deskew, the receive PCS and clean-idle
quality qualification. These are digital `Module` classes with member ports.
The electrical input network, amplifiers and ADC remain analog `IC` classes.

## Sample contract

All ports belong to the same clock domain. A `valid_in` tick accepts one quartet
of **recovered-symbol observations**, rather than one half-symbol ADC sample.
The implementation accepts adjacent ticks, and drains accepted transactions
between ticks. Input bubbles do not shift the symbol histories.

| Port | Meaning |
| --- | --- |
| `samples_in` | Four physical-pair offset-binary samples; pair 0 in bits 7:0, zero at 128, one PAM level at 32 counts |
| `local_in` | Four actual local transmit symbols before pulse shaping, each encoded as level + 2 in three bits |
| `desired_in` | Four training targets in physical-pair order and polarity, also level + 2; ignored outside training |
| `tag_in` | Opaque 16-bit observation identifier; the bank preserves it through the pipeline |
| `clipped_in` | Clipping reported by the upstream sample path |
| `train_in` | Training phase level; coefficient updates occur only on accepted observations |
| `restart_in` | Discard pending observations and restore initial filter coefficients |

`local_in` must describe the local transmit history corresponding to the
received observation. Crossing transmit symbols between unrelated clocks or
selecting a current symbol after interpolator latency is not that alignment.
The bank does not synthesize this reference alignment from its tag.

An accepted observation at rising edge *n* reaches the output registers after
edge *n + 6*. A consumer samples it at edge *n + 7*. Training updates commit
at edge *n + 10*. `training_busy_out` stays asserted until those updates drain.
`samples_out`, `tag_out` and `clipped_out` are meaningful with `valid_out`.
Output codes at either rail conservatively assert clipping.

Illegal accepted local codes, or illegal accepted targets during training,
flush all four lanes and latch a fault. Reset or restart clears it. Invalid
codes on unaccepted inputs are harmless. The bank also checks that all four
lane-valid flags agree. The arithmetic and initial coefficients are documented
in [the receive filter description](receive_filter.md).

## Acquisition and availability

`ReceiveDSPPCS` has separate inputs for observation validity and stable
receiver availability:

- `enable_in` enables the receiver.
- `signal_ready_in` states that upstream gain, sampling and timing are usable.
- `transport_fault_in` reports failed sample/reference transport.
- `remote_master_in` supplies the role resolved during negotiation.

Pair acquisition waits until `train_in` is low and the last coefficient update
has committed. Keep `train_in` high throughout training, including bubbles.
The pair acquirer then identifies the actual physical-to-logical mapping and
relative delays from the observed three-level training stream. It retains
polarity in its samples; the PCS synchronizer resolves that polarity.
The independent training stream must continue long enough for acquisition.

A missing input tick preserves pair identification. Loss of availability,
restart or fault revokes pair lock, PCS lock, output validity, remote readiness
and quality. `quality_out` requires the PCS pipeline to drain and then observe
64 clean accepted idles; elapsed wall-clock time cannot establish it.

The combinational status checks use C++HDL `_LAZY_COMB` definitions. They export
as combinational signals, including child-reset wiring, so the installed Yosys
frontend does not have to resolve nested functions with implicit child-port
references.

## Maintained checks

Build the two native harnesses, then use CTest:

```sh
cmake --build build --target analog_1g_receive_dsp analog_1g_receive_dsp_pcs -j 2
ctest --test-dir build -R 'analog[.]1g[.]receive_dsp(_pcs)?[.]' --output-on-failure -j 2
```

The bank oracle independently schedules scalar arithmetic transactions. It
checks 39,539 clocks and 38,092 four-lane observations, including adaptation,
holdout, bubbles, tag wrap, bad codes and pending-work reset. C++RTL and generated
RTL each check the expected outputs before their canonical traces are compared.

The composed test generates its input before executing either DUT. A separate
Clause 40 transmitter oracle provides the cable symbols and Ethernet byte
truth. Four cases cover both roles, straight and reordered pairs, polarity
inversion, 0–8-symbol skew, local echo/NEXT, noise and irregular symbol ticks.
Each case checks two complete frames, 1,598 bytes including preambles, plus
correct pair identity, quality qualification and availability withdrawal.
Targets are deliberately invalid during holdout. Exact native/RTL trace
comparison includes every clock, not just the decoded frames.

When the local IHP tools are available, CTest also registers mapped-gate checks.
They require fresh native/RTL receipts, regenerate and hash-check the fixture,
map the hierarchy to actual IHP cells, and apply the same independent assertions
to the gate model before comparing its trace. Passing runners remove their
large object directories and retain compressed traces and hash receipts.

The composed receiver preserves its module hierarchy during synthesis to bound
tool memory. Its gate build uses [hierarchical Verilation](https://verilator.org/guide/latest/verilating.html#hierarchical-verilation)
with `ReceiveFilter` and `PCSReceiver` as compiled blocks. The four filter
instances share host model code. Physical area counts all four instances,
recursively down to real IHP library cells. Unknown cells, blackboxes and
recursive module definitions fail that inventory check.

`receive_hierarchy_mapping.py --resume-mapping <receipt>` can reuse a completed
mapping after an interrupted simulation build. It checks the netlist hash and
all circuit, fixture and tool inputs, reconstructs the cell inventory from the
actual netlist, then repeats the complete gate simulation. A changed runner can
use its byte-for-byte archived predecessor as mapping provenance. An interrupted
build remains a failed receipt until the new simulation passes its assertions
and exact trace comparison.

The six/ten-cycle bank measured on 2026-10-24 passes the independent native,
RTL and IHP mapped-gate checks with exact traces. It maps to 112,634 cells and
1,708,782.8688 µm² of Liberty cell area. Slow-corner cell timing, after fanout
and capacitance repair, fails the 4 ns requirement with −2.13 ns setup slack.
This is a functional baseline, not a 250 MHz implementation. The maintained
timing check retains the 4 ns limit and reports failure.

The composed `ReceiveDSPPCS` baseline also passes native, RTL and IHP mapped
checks: all four cases, eight frames and 6,392 bytes, with identical canonical
traces. Hierarchical mapping contains 170,766 physical cells and
2,549,678.7204 µm² of Liberty cell area per receive path. Its slow-corner cell
timing fails at 4 ns with −3.13 ns setup slack. The worst reported path enters
the PCS trellis decoder, so fixing the filter alone cannot close the composed
receiver. These areas exclude routing, the analog front end and padframe.

## Integration boundary

This connection still takes supervised training targets and normalized recovered
samples as inputs. It does not establish autonomous startup, arbitrary cable
reach/BER, aligned local transmit references, a physical clock-recovery loop,
or a complete two-port PHY. The export's 250 MHz clock is a requested constraint;
it is not a timing result. Gate equivalence, routed setup/hold, extraction,
DRC and LVS are separate checks. A complete-chip GDS requires those checks on
the complete connected circuit.

# Prepared trellis metrics

`PCSReceiver` uses `PCSTrellisMetricRegister` and
`PreparedPCSTrellisDecoder` to move branch subtraction out of the recurrent
path-cost comparison. The generic `PCSTrellisDecoder` retains its original
ports and computes comparisons directly from eight independent metrics.
Both decoder classes use the same parameterized `Module` implementation for
path costs, tie order, survivor history and traceback.

For candidate costs `a + b` and `c + d`, the generic recurrence computes the
21-bit modular difference `a + b - c - d`. Prepared mode registers `b - d`
alongside the branch metrics, then computes `a + delta - c`. This needs one
carry-save reduction before the prefix adder instead of two. It changes no
arithmetic precision or decoder latency. Physical timing must still establish
whether the shorter logic improves the placed circuit.

Only pairs of distinct subsets with equal parity occur in the comparisons.
The register module produces all 24 directed differences, including both
orientations. Registering both avoids a negation in the recurrence. Differences
use 21-bit modular representation; metrics remain 18 bits. This adds at most
504 logical register bits before synthesis simplification.

The producer accepts the **effective** metrics after the receiver's control-symbol
mux. The metric values, differences, points and control flags cross the same
existing symbol-register boundary. Disabled symbol ticks hold their state;
reset clears it even during a disabled tick. The receiver's 42 accepted-symbol
latency is unchanged. Forced control symbols may repeat, so reducing precision
under an assumption of only two forced symbols would be invalid.

Connect a prepared decoder's metric and difference inputs to the same
`PCSTrellisMetricRegister`. A difference must describe the same symbol as its
metrics. The standalone decoder test computes these differences independently;
the register test and complete receiver tests cover their production alignment.

Validation is layered:

- `analog.1g.trellis_metrics.native` checks all 24 registered differences,
  forced metrics, poisoned pauses and reset.
- `analog.1g.trellis_prepared.native` and `.rtl` compare decisions against an
  independent unbounded-cost dynamic program, including extremes, ties,
  wraparound, end-of-frame draining and disabled ticks. With the IHP tools
  configured, `.rtl` also repeats the oracle on mapped standard cells.
- `analog.1g.trellis_prepared.selector_cone` requires all 24 differences to
  reach the mapped selector logic and rejects raw metric dependencies there.
- Receiver native/RTL/mapped tests check frame, fault and symbol-enable traces
  before a fresh physical implementation can be admitted.

The prior physical receiver checkpoint remains available separately. A passing
functional test does not qualify a new layout or complete the repeater ASIC.

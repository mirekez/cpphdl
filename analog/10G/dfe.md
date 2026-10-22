# One-tap decision-feedback equalizer

[dfe.hpp](dfe.hpp) sums the received differential voltage and a correction from its
own previous sampled Q. A finite-bandwidth limiting amplifier drives a buffered
master/slave sampling IC. All state is declared capacitor charge; no software
callback computes the receiver decisions or feeds them into Verilog-A.

```text
SUM - VCM = V(P, N) - tap * (Q_previous - VCM) / 0.6 V
```

The 1 kOhm summing return and two VCCS contributions produce this settled relation.
At the sampling edge the master closes before the slave changes Q. Feedback then
changes the summer while the captured decision remains held. Output clamps have
finite resistance, and storage/loading can still affect sampled values.

The normal receiver uses a fixed 25 mV tap. In the isolated `dfe` test the artificial
channel is `0.08 * current_symbol + 0.09 * previous_symbol` volts, and a 90 mV tap
must recover every checked bit. `dfe_bypass` sets the tap to zero and must exhibit
errors, demonstrating that the reference sequence is not secretly driving the
receiver. Both cases have independent native and Verilog-A checks, then comparison.
This is fixed-coefficient cancellation, not an adaptation/training algorithm.
Module: [TenGDFE.va](generated/TenGDFE.va).

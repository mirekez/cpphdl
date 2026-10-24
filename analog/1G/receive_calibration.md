# Gain and offset from measured training samples

[`ReceiveGainCalibration`](receive_calibration.hpp) is a digital `Module` with
member ports. It estimates coefficients for `ReceiveSampleNormalizer` from
achieved ADC codes and **known training labels**, rather than assuming the
amplifier, source follower and converter retain an ideal gain and offset.
It does not infer the remote symbols from payload data or declare link lock.

| Port | Meaning |
| --- | --- |
| `enable_in` | Disabling flushes collection/division and restores neutral defaults |
| `start_in` | Start or restart collection; clear readiness and previous faults |
| `sample_in`, `level_in`, `valid_in` | Achieved eight-bit ADC code and valid training label 0..4 for PAM −2..+2 |
| `finish_in` | End collection on a cycle with `valid_in` low |
| `offset_out`, `gain_out`, `ready_out` | Atomic published zero-code offset and unsigned Q8.8 normalization gain |
| `fault_out` | Sticky invalid-training or unrepresentable-gain indication; cleared by start/reset/disable |
| `busy_out`, `collecting_out` | Collection or division is active; collection accepts observations |

The training source must deliver the same nonzero number N of observations
for every label, with strictly increasing accumulated means. Samples may arrive
in any label order and with gaps. The limit is 65,535 observations per label.
Unknown labels, population overflow, missing/unbalanced/nonmonotonic training
and simultaneous finish/sample are rejected. Acquisition must align the known
label to the corresponding raw ADC result before asserting `valid_in`.

For bin sums S₀..S₄, the coefficients are

\[
offset = round(S_2/N),\qquad
gain = round\left(32768N/(S_4-S_0)\right).
\]

The four endpoint spacings correspond to 128 normalized counts, and gain has
eight fractional bits. Nonnegative exact half-counts round upward. The middle
bin establishes the zero code; endpoint span establishes scale. This is a
linear gain/offset estimate, not correction for front-end nonlinearity,
per-slice mismatch, cable equalization or unknown training polarity.

Two sequential restoring divisions publish the result exactly 64 clocks after
finish. Each clock performs one shift/compare/subtract step, avoiding a large
combinational divider. Publication occurs only after both results are complete;
an out-of-range Q8.8 gain faults without asserting ready. Previously published
data remain while readiness is low; consumers must respect `ready_out`.
Reset/disable sets offset 128 and gain 256 and discards pending work.

The independent unit oracle uses long-double means, endpoint span and `lround`.
Native execution passes 413,612 clocks spanning different gains, offsets and
populations, invalid bubbles, all invalid labels, flat/missing training,
unrepresentable gain, full accumulator/count range, population overflow,
reset during both divisions and restart. The first
measured receive-front-end codes `{97, 107, 118, 128, 137}` produce offset 118
and gain 819. Those are a test vector, not hardcoded circuit coefficients.
RTL/mapped qualification and connection to continuous receiver training are
in progress.

The standalone implementation targets a 125 MHz training domain. Integrating
it with the 250 MS/s ADC requires selecting and aligning the intended training
observations and respecting the clock-domain boundary explicitly. A passing
125 MHz cell estimate would not qualify the divider for 250 MHz operation.
This is digital hardware; the root converter exports
`generated/ReceiveGainCalibration.sv`, rather than an analog Verilog-A primitive.

```sh
cmake --build analog/build --target analog_1g_receive_calibration
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]receive_calibration[.]'
```

The complete [receive front end](receive_frontend.md) still needs to apply
these measured coefficients at its normalizer interface and qualify the
resulting constellation before PCS integration.

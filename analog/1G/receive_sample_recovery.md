# Fixed-sample receive timing recovery

`ReceiveSampleRecovery` consumes four simultaneous 8-bit lanes with contiguous,
wrapping 16-bit sample tags at 250 MS/s. It produces one four-lane symbol word
and a valid pulse per recovered symbol. An eight-sample history supplies
four-tap Catmull–Rom early, middle and late estimates. The middle coefficients
are shared across all four lanes; lane zero supplies the Gardner timing error.
The existing `ReceiveTimingLoop` controls period and phase. The wrapper reduces
its proportional correction by four to accommodate the interpolation pipeline;
the integral period correction is unchanged.

The test computes all input samples before running the DUT. Independently seeded
PAM5 streams pass through a raised-cosine pulse with roll-off 0.35, sampled on a
fixed grid and quantized to bytes. No DUT phase or output controls source sample
times. Twelve cases cover source offsets −1500, 0 and +1500 ppm and common delays
0, 0.105, 0.35 and 0.9 symbol intervals; 0.105 UI corresponds to 0.84 ns.
Independent source coordinates check phase, symbol slips and all four lane
symbols after acquisition. This is a functional timing-recovery experiment,
not channel equalization or a physical clock qualification.

The NCO uses Q24 sample coordinates. Interpolation retains eight fraction bits,
so the coefficient grid is 15.625 ps at 250 MS/s; it is not exact-time
interpolation. The regression compares arithmetic against floating-point cubic
interpolation of the same fixed samples with a two-count fixture limit.
Catmull–Rom can overshoot: outputs saturate to bytes and expose a clipping flag.
The two-count fixture limit is not a universal arbitrary-waveform error bound.

A result appears four clocks after its history request. With continuously
accepted samples, its output clock is the fixed transport delay plus its
unwrapped center sample index plus eight. Output tags retain that center and
its Q24 fraction. The period is bounded to approximately ±2000 ppm, and the
phase actuator to the existing timing-loop interval limits; center advances are
one, two or three sample positions. Missing, duplicate or out-of-order tags
cause a sticky fault and suppress output until explicit restart or reset.
Transport bubbles hold history. Restart clears pending interpolation and timing
state; the caller supplies the new tag origin. The regression covers tag wrap,
all three tag faults, restart and reset with pending output.

Run `1G/tests/receive_sample_recovery.py --native <bench> --converter <cpphdl>
--verilator <verilator> --cxx <compiler> --work <results>`. The runner checks all
504,533 native and RTL clock states, including input, output, tags, valid,
fault, reset and timing-loop signals; it retains compressed traces and source,
tool and artifact hashes. Failed revisions remain under
`out/receive-sample-recovery/`.

A valid recovered-symbol pulse must drive the explicit PCS symbol clock enable.
This recovery stage cannot replace physical TRACK-to-SAR setup/hold, comparator
return timing, slice skew or jitter qualification. The physical shared TRACK
unit's original fixed-aperture failure remains valid evidence; absorbing common
sample delay requires a separately declared delivered-clock interface and an
end-to-end test.

The maintained v4 receipt is
`out/receive-sample-recovery/v4/qualification/checks.json`: all 504,533 native and
RTL clock states match exactly. The twelve source cases have zero symbol errors
and slips, maximum phase error 0.0442814 UI and maximum interpolation discrepancy
0.883707 ADC counts. The earlier v3 native pass/RTL declaration failure remains
retained; flattening the local coefficient array enabled conversion without
changing the coefficient equations.

`python3 1G/tests/receive_sample_recovery_bounds.py` independently checks endpoint
bounds for the eight-sample history (tap bases 0–4) and center advances (1–3),
and enumerates all 256 coefficient fractions. The maximum basis quantization
error for arbitrary byte inputs is 0.132872 counts. Including phase truncation
and output rounding gives a conservative 2.127012-count arbitrary-input bound;
the tighter two-count limit above is checked against the declared waveform.

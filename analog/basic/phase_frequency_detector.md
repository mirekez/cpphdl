# MOS phase/frequency detector

[`cmos_phase_frequency_detector`](phase_frequency_detector.hpp) is an electrical
IC with REF, FEEDBACK, CLEAR, UP, UP_N, DOWN and DOWN_N member ports and inherited
Vdd/Vss. Two physical rising-edge flip-flops each capture Vdd. The first input
edge asserts its output; the second asserts the other output and initiates a
common reset. A NAND network combines this reset with external CLEAR. The
circuit contains 134 MOS channels.

The default 1 kΩ/100 fF reset path gives both storage paths time to clear. This
is a finite electrical pulse, whose duration changes with loading and device
parameters. Neither edge timing nor UP/DOWN state is supplied by host code.
Missing feedback leaves UP asserted after a reference edge. The detector
alone does not assert a lock signal.

The unit test checks coincident edges, either input leading, frequency ratios
3:1 and 1:2, missing feedback, asynchronous clear, and two loaded cases. Its
ninth case releases reset during an existing feedback pulse. Subsequent edges
then associate across a cycle: a feedback clock nominally leading by 1 ns
produces an UP pulse of about 7 ns at an 8 ns period. The checker explicitly
requires this behavior rather than assuming nearest-edge association after
arbitrary reset release.

Both native and Verilog-A/ngspice flows independently pass all nine cases,
including rail, clear, complement, signed average and activity checks. The
comparison rechecks each waveform and its input hashes before comparing.
The selected generic transistor equations are not an IHP sizing or PVT claim.

```sh
cmake --build analog/build --target analog_phase_frequency_detector
ctest --test-dir analog/build -R 'analog.basic.phase_frequency_detector\.' --output-on-failure
```

The export is [`generated/cmos_phase_frequency_detector.va`](generated/cmos_phase_frequency_detector.va).

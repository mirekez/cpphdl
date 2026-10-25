# Generated example models

The folders correspond to every [C++ example](../). Eleven analog examples have
complete simulation bundles generated from their actual native bench definitions.
The mixed receiver has an explicitly bounded analog partition. The digital pipeline
and diagnostic suite have explanations of why full Verilog-A export is unavailable.
No placeholder analog circuit stands in for their original behavior.

| Folder | Contents |
|---|---|
| [agc](agc/README.md) | Nominal, weak-input and refined-step AGC benches |
| [class_variants](class_variants/README.md) | Channel inverter and identified fast simulation approximation in one bench |
| [cmos_inverter](cmos_inverter/README.md) | `Inverter.va` reusable class and driven, loaded inverter bench |
| [diagnostics](diagnostics/README.md) | Deliberately invalid circuits and framework diagnostic limits |
| [digital_pipeline](digital_pipeline/README.md) | Pure digital state/arithmetic export limit |
| [ldo](ldo/README.md) | Nominal, dropout and refined-step regulator benches |
| [mixed_signal](mixed_signal/README.md) | `MixedReceiverAnalog.va`; native digital controller stays external |
| [mutual_influence](mutual_influence/README.md) | Competing driver and shared supply; native purposes intentionally fail |
| [opamp_feedback](opamp_feedback/README.md) | Finite transistor amplifier with external feedback and load |
| [pll](pll/README.md) | Nominal, loaded and refined-step phase-locked-loop benches |
| [rc_filter](rc_filter/README.md) | `LowPass.va` reusable active-port class and RC step bench |
| [regulator](regulator/README.md) | Reference-controlled transistor regulator and supply step |
| [transistor_pair](transistor_pair/README.md) | `NamedDevices.va` reusable class and named-device bench |
| [two_transistor_expression](two_transistor_expression/README.md) | `WireOutput.va`, `ActiveOutput.va` and a bench comparing their output storage |

Each complete bundle contains `design.va`, `bench.cir`, `columns.csv` and
`conditions.json`. Sources and integration settings are bench inputs; waveform
results and compiled binaries are ignored. The Verilog-A file preserves all
physical contributions; the SPICE deck supplies ideal stimuli, declared initial
charges and named observations. Purpose/assumption checks remain native C++ checks;
exporting a circuit does not assert those checks passed. The `refined/` bundles
preserve the examples' own refined settings, which differ from the smaller-step
[dedicated cross-simulator regressions](../../VerilogA/tests/README.md).

Regenerate everything from the package root:

```bash
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCPPHDL_BUILD_ANALOG_TESTS=ON
cmake --build analog/build --target generate-veriloga -j 4
ctest --test-dir analog/build -R 'analog.veriloga.generated' --output-on-failure
```

To export one ordinary analog example manually, keep the output directory first,
scenario second, and export flag last:

```bash
./analog/build/analog_ldo analog/out/ldo-va dropout --verilog-a
```

This elaborates the original C++ bench without integrating it. Full exports reject
managed bit operations or native digital callbacks instead of silently dropping
them. The mixed receiver's separate export mode explicitly produces only its
analog partition and exposes its external digital control. See each folder's
README and the [backend documentation](../../VerilogA/README.md) before simulation.

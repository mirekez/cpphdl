# Analog gain acquisition

`ReceiveAnalogGainControl : Module` chooses one of five ordered gain settings
for the [programmable gain stage](IHPSG13G2/programmable_receive_gain.md).
`select_n_out` is a five-bit active-low mask: 30, 29, 27, 23, 15 select gains
−0.8, −1.25, −2, −3.5, −8. The bits are core logic. The parent must translate
them to physical analog gate voltages, account for the analog inversion and
perform digital offset/gain calibration after acquisition.

After reset, restart, disable or a gain change, the controller waits 4096 clock
edges before accepting measurements. At 250 MHz this is 16.384 µs. It then
captures extrema over 1024 accepted ADC samples. The following clock edge
publishes their range and rounded midpoint; the next edge publishes the gain
and status decision. Both pipeline stages overlap collection of the next
window. A gain change discards that partial window before the settling interval.
`advance_in` marks an accepted sample; `training_in` permits acquisition.
Pauses preserve the partial measurement window. Settling counts clock edges,
including edges without accepted samples. The last accepted sample contributes
to both range and midpoint.

The parent supplies an inclusive amplitude band in ADC counts. A range below
`minimum_span_in` increases gain; a range above `maximum_span_in` decreases
it. `changed_out` pulses for one clock edge when the selection changes.
`ready_out` records an accepted window inside the band. At the gain limits,
`weak_out` or `overload_out` identifies a range the available gain cannot fix.
Subsequent measurement windows can recover from those conditions. Leaving
training freezes selection and measurement status. Configuration must remain
stable until restart; the parent must discard data while acquisition is pending.

The upper/lower ratio must be at least 16/7, the largest adjacent gain ratio.
For example 16..38 counts is valid; 16..36 is rejected because an input can
otherwise alternate indefinitely between adjacent gains. Zero lower bounds,
reversed bounds and `adc_fault_in` latch a fault and select the lowest gain.
Reset, restart or disable clear the fault. These band checks cannot guarantee
convergence with noise, missing PAM levels, clipping or an unsettled analog path.

The independent window-based reference supplies 246,993 clocks covering exact
settling/window boundaries, overlapping measurement/collection, pauses with a pending decision, both inclusive thresholds, the final sample, input
pauses, training freeze, all gain steps, saturation, recovery, invalid bands,
latched ADC faults and quantized closed-loop amplitude cases. The C++ bench
checks every output on every clock. RTL and mapped flows use the same vectors
and separately check the actual native and hardware traces. The synthetic ADC
closure is a controller regression; it is separate from transistor simulation.

```sh
cmake --build analog/build --target analog_1g_receive_gain_control
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]receive_gain_control[.]' -j 1
```

All 246,993 clocks pass independently in native C++RTL, converted RTL and IHP
mapped-gate simulation; the native and hardware traces are also exactly equal.
The mapped controller contains 772 IHP cells with 10,137.355 µm² of Liberty
cell area. Fanout/capacitance repair gives 940 cells and 11,403.806 µm², with +0.29 ns
cell-only setup slack at 4 ns in the slow IHP corner. The repaired netlist
passes the same independent vectors and exact native trace comparison.
Those figures exclude placement, routing and physical wire delays.
Physical timing and integration with level translators, the receiver and its
calibration sequence remain in progress. No complete PHY qualification is implied by this module test.

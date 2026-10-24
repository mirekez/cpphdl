# Analog gain acquisition and digital calibration

`ReceiveAnalogAcquisition : Module` connects the existing
[gain controller](receive_gain_control.md) to the
[range calibrator](receive_range_calibration.md). It selects a usable analog
gain from accepted raw ADC samples, freezes that selection, then calculates
digital offset and Q8.8 scale. It reuses their estimator and divider.

Drive `sample_in` from the raw ADC code and `advance_in` from raw-valid.
`training_in` permits measurement. Advances outside training do not count.
The controller waits 4096 clocks after reset or a gain change, then measures
1024 accepted samples. Its pipeline publishes range and gain decisions on
subsequent edges. `minimum_span_in` / `maximum_span_in` retain the gain-spacing
validity checks described by the gain controller.

The first acceptable range starts calibration on the next eligible training
edge. Selection then stays frozen even if training continues or samples change.
The captured centre, span and `target_span_in` produce offset and gain sixteen
clocks later. Target changes during division do not change that request. The
target describes the actual training alphabet's span; five labeled PAM levels
are not assumed.

Connect `offset_out` and `gain_out` to the ADC receive normalizer's inputs.
Gate downstream normalized data with `ready_out`; raw samples must remain
available for acquisition. `busy_out` denotes division, `settled_out` denotes
completion of analog settling, and `weak_out` / `overload_out` distinguish
exhausted gain range from an internal fault.

Reset, disable or `restart_in` cancels acquisition and restores lowest analog
gain, offset 128 and unity digital gain. ADC faults latch in the gain controller.
An invalid calibration request reaches that latch on the following edge.
Either child's fault suppresses ready. Restart after a link/training change;
a completed acquisition does not change gain during data reception.

The independent test combines the existing window reference with rounded
rational calibration. Native C++RTL, generated RTL and IHP mapped gates each
check 400,993 clocks: every selected gain, two target spans, weak/overload limits,
frozen selection, changed division inputs, handoff cancellation, training pauses
and fault recovery. Samples come from a synthetic gain fixture. The repaired
slow-corner cell implementation also passes all vectors, with 0.61 ns setup
slack at a 4 ns period. Its 1,595 cells total 20,364.75 µm² of Liberty area.
These estimates exclude routed wires, hold, whitespace and pads. Transistor-loop
acquisition, physical control translation, routing and chip integration remain
separate requirements.

```sh
cmake --build analog/build --target analog_1g_receive_acquisition
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]receive_acquisition[.]' -j 1
```

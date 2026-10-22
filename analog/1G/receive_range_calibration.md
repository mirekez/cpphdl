# Receiver range calibration

`ReceiveRangeCalibration : Module` converts a measured training range into
the offset and Q8.8 gain expected by the ADC receive normalizer. It does not
assume five labeled levels: the caller supplies the normalized `target_span_in`
represented by its particular training sequence.

With `enable_in` true, `start_in` captures `centre_in`, nonzero `span_in` and
nonzero `target_span_in`. Sixteen subsequent clock edges perform unsigned
restoring division. The result is
`gain_out = round(256 * target_span_in / span_in)` and
`offset_out = centre_in`. Half cases round upward. `busy_out` covers the
calculation; `ready_out` asserts when both outputs commit together. Input
changes after the start do not change the captured calculation.

A new start replaces a pending request, including on its final division edge.
The preceding valid offset and gain remain visible during a valid calculation.
A zero span or target sets `fault_out`, cancels the request, clears ready and
selects offset 128 / unity gain 256. A valid new start clears that fault.
Reset or disabled operation clears busy, ready and fault and selects the same
safe outputs. The parent must gate data until ready, after analog settling and
coarse gain selection; this module does not establish those physical conditions.

The independent integer reference checks 56,026 clocks in native C++RTL,
generated RTL, IHP mapped gates and timing-repaired gates. Cases include all
255 nonzero spans, four target spans, three centres, input changes while busy,
restart/reset phases and invalid requests. The repaired slow-corner cell
estimate meets a 4 ns period with 1.71 ns setup slack. This estimate includes
fanout/capacitance repair and functional rechecking, but has no routed wire,
hold or full-chip qualification.

```sh
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]1g[.]receive_range_calibration[.]' -j 1
```

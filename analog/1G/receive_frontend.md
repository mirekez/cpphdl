# Coupled receive amplifier, buffer and ADC bank

[`PAM5ReceiveFrontend<Count>`](receive_frontend.hpp) composes three existing IC
classes: the resistor-loaded differential amplifier, an NMOS source follower,
and the core-domain SAR ADC array. `Count` selects the number of interleaved
slices. The default is sixteen. The external ports are differential `PLUS` and
`MINUS`, inherited IO supplies, `Vlogic`, evaluation/clear, per-slice acquisition
and trial gates, and restored `CORE_DECISION` outputs.

The amplifier's positive drain drives the follower gate. Its 500 Ω drain loads
leave headroom for the follower's gate/source drop. The follower drives the
common ADC input, so acquisition from any slice changes that input, the other
held slices through off-state coupling, the amplifier through gate charge,
and the shared supply. There is no ideal isolation buffer between these stages.
The negative amplifier output remains physically present with its resistor and
charge; single-ended conversion uses the positive output.

The shared buffer uses β = 0.1 A/V² and a 250 Ω return. Both are doubled in
conductance relative to the standalone follower, retaining its ideal DC
transfer while lowering output impedance. Gate/source, gate/drain and output
capacitance also double to 40/10/40 fF. This increases current and amplifier
loading. The standalone 200 µm PSP result does not qualify this stronger buffer.

The default initial charges describe amplifier inputs at 1.65 V, drains at
3.3 V and the shared ADC input/held capacitors at 1.65 V. They agree across
the connected capacitor network. The supply and bias still settle dynamically.
This is a precharged experiment; cold-start sequencing needs separate verification.

`tests/receive_frontend.cpp` exercises acquisition twice per slice for one,
four and sixteen slices. The larger runs cover five differential input levels
and successive 4 ns acquisition slots. The evaluation clock stays low: this
test establishes the coupled acquisition path, while the separate electrical
ADC/core-controller fixture establishes closed SAR decision scheduling. The
combined path has not yet been qualified for decoded conversion.

The independent checker solves the amplifier's two degenerated MOS branches
and shared tail-resistor KCL, then the follower's loaded DC equation. Each
solver must meet 20 mV acquisition, buffer delivery and held-voltage drift
limits, finite IO/core supply bounds, input delivery and output headroom.
An added 40 pF input load must expose acquisition failure. A refined time step
must agree within 20 mV. Only after both absolute checks pass may the two flows
be compared, with 20 mV waveform and 10 mV held-sample tolerances.

The first, weaker buffer passed all six native acquisition cases, including
sixteen-slice reuse and overload. Closing the converter loop then exposed
22.3 mV held error with four slices, exceeding the unchanged 20 mV limit.
The stronger buffer and both acquisition/conversion flows are being qualified.
These checks use generic MOS laws, explicit
charge and externally supplied control waveforms. They do not establish PSP
equivalence, receiver calibration, transistor control buffers or a routed ADC.

```sh
cmake --build analog/build --target analog_1g_receive_frontend
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]receive_frontend[.]'
```

The catalog generator emits `generated/OneGPAM5ReceiveFrontend.va` from the
same hierarchy. The [ADC/core composition](adc_receive.md) and
[standalone follower](../basic/nmos_source_follower.md) document adjacent
verification boundaries.

`tests/receive_conversion.cpp` extends the same circuit with online SAR feedback
and `ADCReceiveController`. The native callback and the paused-ngspice host each
read their own achieved core-domain comparator voltages. The test reuses the
qualified ADC host protocol and controller capture helpers; it does not replay
the other solver's control decisions. The independent checker derives input
transfer from amplifier/follower DC KCL, requires raw codes within two counts,
held error below 20 mV, exact slice/cadence and normalization arithmetic, and
resolved core input levels. Cross-solver limits are one raw count and two
normalized counts after separate absolute passes. Conversion qualification is
in progress for one, four and repeated sixteen-slice operation.

This experiment retains the host's fixed offset 128 and gain 424 to check the
composed normalizer arithmetic. The amplifier/follower's offset and gain differ
from the earlier direct ADC source. Receiver calibration must account for that
transfer before these normalized values feed the PCS constellation interface.

The current 2 pF hierarchy passes the one/four/repeated-sixteen-slice closed
conversion checks in both generic solvers and their comparisons. Its IHP
experiment exposed additional injection/feedthrough errors. A
[derived 4 pF process variant](IHPSG13G2/precision_receive_frontend.md) is being
qualified separately; generic circuit agreement does not establish PSP accuracy.

```sh
cmake --build analog/build --target analog_1g_receive_conversion
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]receive_conversion_'
```

Closed conversion also exercises continuous input changes and overlapping track
windows. Its checker separates frontend settling at the closing aperture,
sampler error relative to the achieved input at that aperture, and subsequent
hold drift. Each is bounded independently at 20 mV. End-to-end raw conversion
must still remain within two counts of the DC target, and normalization must
match the exact integer contract. The combined DC-to-held difference is
reported separately: it includes frontend delay and is not a hold-drift metric.
Fault-injection guards reject independent 30 mV frontend, sampler and hold
faults and a three-count raw conversion fault.

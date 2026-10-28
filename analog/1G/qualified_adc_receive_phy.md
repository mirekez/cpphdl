# Clock-qualified receive path

`QualifiedADCReceivePHY` connects `PHYResetControl` to the actual
`AutonomousADCReceivePHY` hierarchy. It accepts the external reference,
delivered ADC and processing clocks, power-good, and both clock qualification
levels. The oscillator and acquisition controllers stay outside this reset
boundary so they can acquire while the data path is held reset.

Loss of power-good, enable or either clock qualification, an external reset,
or a restart request asserts the reset synchronizers asynchronously. Each
delivered domain releases after two of its own edges. A second pair of
synchronizers exchanges those release acknowledgements. Sampling remains
disabled while the processing clock is stopped, even if the ADC clock has
already resumed. The processing domain likewise waits for the ADC domain.
Neither clock is gated by this logic.

Separate `clear_adc_in` and `clear_pcs_in` ports carry these local resets through
the capture/recovery logic and the two sides of `ReceiveDSPCrossing`. A single
shared child reset cannot express two independently synchronized releases.
The register pipelines reset on destination edges; asynchronous output masks
immediately suppress sampling, conversion and receive validity. FIFO pointers
clear before traffic restarts; FIFO storage does not require a mass reset.

The 4096-clock SAR precharge and four-clock release sequence runs again after
reset. Release of digital reset does not bypass analog startup or imply that
the receiver has acquired the cable. Offset/gain calibration inputs and the
physical clock qualification signals still need their respective circuits.

The connected regression exercises stopped-clock power loss, unequal restart
progress and a restart through the independent sample/channel fixtures. Native
C++ and generated RTL each check the expected Ethernet frames and compare
complete event traces. Native asynchronous events are dispatched through the
`sim_assert_*_reset` helpers, separately from clock-edge work and strobes;
generated RTL uses the reset synchronizers' asynchronous clock blocks.

```sh
cmake --build build --target analog_1g_qualified_adc_receive_phy -j 1
ctest --test-dir build -R '^analog\.1g\.qualified_adc_receive_phy\.rtl$' --output-on-failure
```

The maintained CTest passes in both flows: four channel cases, cold startup
and reacquisition after active-receiver power loss, 16 frames and 12,784 bytes.
All 1,876,382 output events match exactly. The passing receipt is
`build/1G/tests/hardware/qualified_adc_receive_phy/checks.json`. This functional integration does not establish reset
pulse width, recovery/removal timing, synchronizer MTBF, physical reset
distribution, extracted ADC behavior or full-chip GDS qualification.

# Autonomous receiver from comparator decisions

`AutonomousADCReceivePHY` connects four `ADCReceiveCapture` lanes to
`AutonomousReceivePHY`. Each lane contains thirteen interleaved SAR controllers.
The ADC clock runs at 250 MHz; the independently qualified processing clock
runs at 132–144 MHz. The parent must reset both domains before admitting samples.
`clear_adc_in` and `clear_pcs_in` supply independently synchronized local reset
release. The [qualified wrapper](qualified_adc_receive_phy.md) connects these
to power/clock qualification and a mutual release handshake.

The input boundary is 52 comparator decisions, packed into four 16-bit ports;
the three unused positions in each lane are ignored. Outputs expose each
slice's trial code, tracking control and conversion control. These outputs must
connect to the corresponding physical DAC, sampling switch and comparator
evaluation circuit. They are not analog-to-digital primitives.

At each actual sampling aperture, the capture pipeline stores the timestamp
and local transmitter symbols. It carries them through SAR conversion and
normalization with the converted sample. The timing recovery circuit then
selects observations and aligned local references, crosses into the processing
clock domain, and performs autonomous echo/idle acquisition, pair alignment,
PCS decoding and receive-quality qualification. There are no far-end training
targets or externally supplied decisions on this interface.

`offset_in` and `gain_in` are explicit calibration coefficients. Supplying them
does not perform electrical calibration. `clock_ready_in`,
`processing_ready_in` and `signal_ready_in` must come from the corresponding
physical qualification/control circuits. Their withdrawal suppresses valid
data and receive qualification. SAR startup still performs its real 4096-clock
precharge and four-clock release sequence.

Run the connected regression with:

```sh
cmake --build build --target analog_1g_autonomous_adc_receive_phy
ctest --test-dir build -R '^analog\.1g\.autonomous_adc_receive_phy\.rtl$' --output-on-failure
```

The bench samples an independent channel waveform only when a real controller
opens its sampling switch. It compares each held input against the controller's
actual trial code. Both C++ and converted RTL must independently recover the
expected Ethernet frames, then match complete output traces. Cases exercise
both remote roles, pair permutations, polarity, skew, interference, four
initial sampling phases, timestamp wrap and independent processing clocks.
The timestamp continues during conversion drain: stopping new launches does
not cancel apertures already in flight.

This regression checks the connected digital receive path. Comparator/DAC
settling, transistor noise, physical clock generation, automatic calibration,
two-port startup and extracted chip behavior require their electrical and
physical integration checks.

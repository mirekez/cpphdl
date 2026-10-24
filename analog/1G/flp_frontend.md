# Electrical link-pulse interface

[`flp_pulse_driver`](flp_frontend.hpp) contains ten MOS channels, explicit gate
and output storage, and two 20 Ω output resistors. Two inverter stages drive
the positive output. Transmission gates disconnect both line terminals when
`ENABLE` falls. `COMMAND` and `ENABLE` are electrical I/O-domain inputs; core
logic needs a real level interface. The connected line and the detector load
the driver, including while it switches or disconnects.

[`flp_pulse_detector`](flp_frontend.hpp) contains two transistor differential
amplifiers with PMOS mirror loads. Finite resistor networks offset their
inputs to reject small differential voltages. One amplifier detects each
polarity; a MOS NOR/inverter restores the combined pulse to I/O logic levels.
The nominal unloaded bias difference is 0.55 V at 3.3 V, but reference loading,
input common mode and transistor behavior determine the achieved threshold.
There is no ideal comparator or imposed internal output voltage.

Legacy link pulses occupy roughly 100 ns. The TI example specifies 114 ns FLP
pulses, 125 µs clock spacing and a data position near the halfway point; see the
[TI DP83822 timing tables](https://www.ti.com/lit/ds/symlink/dp83822i.pdf), section 6.18.
The electrical experiment uses 96/104/120 ns pulses, a 16 ns glitch and a
mid-pulse disable. Its compressed pulse schedule tests the interface, rather
than a complete negotiation exchange. Pulse amplitude limits below are this
prototype's contract, not a complete Ethernet transmitter mask.

Both native C++RTL and Verilog-A/ngspice independently check:

- 80/100/120 Ω differential loads and reversed detector polarity.
- Driver amplitudes from 1.8 to 2.5 V, finite supply loading and less than 20 mV
  differential output after disable.
- Rejection at 0.25 V differential input and detection at 0.8/1.2/2.2 V in
  either polarity, with settled outputs below 0.3 V or above 3.0 V.
- Exact pulse count/order, bounded edge latency, quiet-line behavior and a
  finer integration step for the disconnected line's stored-charge transient.

Thirteen electrical cases pass both flows. The measured loaded amplitudes are
1.983–2.288 V. The checked solver differences are at most 2.2 mV plateau RMS
and 48.4 ps at pulse edges. Declared comparison limits are 20 mV RMS and 500 ps.
Default integration steps are 62.5 ps, with a 31.25 ps refinement case. Each
flow also requires less than 5 mV RMS and 250 ps change under refinement.

`flp_frontend_receiver.cpp` samples each solver's actual detector waveform at
eight phases of the 125 MHz reference and drives the real `FLPReceiver` class.
Measured pulse templates are separated by the production clock/data intervals;
short-glitch templates are injected between them. Four distinct pages exercise
zeros, ones and mixed bit patterns. The subthreshold cases must produce no page.
This checks the electrical-to-digital pulse boundary. It does not simulate the
analog circuit continuously for a full millisecond-scale burst or complete
auto-negotiation. Each flow passes 352 decoded pages plus 64 subthreshold
page attempts that must remain silent: 104,016,640 reference clocks per flow.
Its result is recorded separately in each electrical case. The three CTest
stages, including both electrical/refinement and decoder checks, pass in 127 s.

```sh
cmake --build analog/build --target analog_1g_flp_frontend analog_1g_flp_frontend_receiver
ctest --test-dir analog/build -R 'analog.1g.flp_frontend.' --output-on-failure
```

Generated electrical models are `generated/OneGFLPPulseDriver.va` and
`generated/OneGFLPPulseDetector.va`. The flow hashes its source, binaries and
waveforms, reapplies each absolute contract before comparison, and retains
detector templates and decoder logs.

These circuits use generic channel equations. Process sizing, body connections,
pad/clamp and magnetics loading, PVT/noise, return loss, the pulse mask and the
shared normal-data/FLP output path remain integration work. In the selected IHP
pad, connect the high-current transmitter to `pad`; `padres` supplies the
separate protected receive path behind its series resistor. See the
[physical interface](../pdk/1G_repeater_IHPSG13G2/chip.md).

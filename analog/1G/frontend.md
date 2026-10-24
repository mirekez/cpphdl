# Receive front-end circuits

[`PAM5ReceiveFrontend`](receive_frontend.md) now connects the MOS amplifier,
shared source follower and core-domain ADC bank in one electrical hierarchy.
Its acquisition/loading qualification is in progress; complete conversion and
four-pair receive integration remain separate work.

The [1.2-to-3.3 V gate interface](level_interface.md) now has native/Verilog-A
and actual-PSP PVT tests. It remains separate from the ADC slice's gate schedule;
the tested 8 ns control spacing does not establish 250 MHz clock operation.

[`passive_hybrid`](passive_hybrid.hpp) adds two resistor paths at each output:
one from the local line and one from the opposite local-transmit replica. The
crossed replica subtracts the local differential component electrically. The
`balance` parameter chooses its conductance relative to the line arm. Both
sources see the network's load, and an external differential load changes its
gain. Output capacitors retain charge relative to `VCM`; they do not impose a
voltage. There is no controlled voltage source inside this circuit.

With equal source resistance `Rs`, line-arm resistance `R`, replica-arm
resistance `R/balance`, and differential output load `RL`, let

```
g_line = 1 / (R + Rs)
g_local = 1 / (R / balance + Rs)
Vout_diff = (g_line * Vline_diff - g_local * Vlocal_diff)
            / (g_line + g_local + 2 / RL)
```

Omit `2/RL` for an unloaded output. This equation includes the finite source
impedances used by the tests. Their absolute checker derives this passive
network solution independently from the simulator. The network attenuates the
wanted signal too; it does not provide free cancellation and gain.

[`nmos_resistor_amplifier`](../basic/nmos_resistor_amplifier.hpp) provides
differential gain from two MOS devices, drain-load resistors, source degeneration
and a shared tail resistor. The input ports drive gates. Gate/source and
gate/drain capacitors also return current into the preceding stage, so input
loading and feedback are part of the coupled solve. Output and tail storage
determine transients; external loading reduces the achieved differential gain.

The initial input/supply parameters describe the connected test circuit's
initial charges, not internal voltage sources. Defaults describe gates at
1.65 V, output drains initially at 3.3 V and source/tail nodes initially at zero.
The three gate/source/drain capacitor paths form consistent dependent voltage
constraints. The native solver selects an independent startup constraint basis,
checks every dependent charge, and retains **all** capacitor integration
equations during finite time steps. Contradictory initial charges and unsupported
instantaneous source/charge conflicts produce diagnostics. The mesh regression
checks all declaration orders, both Jacobians and step refinement against an
independent matrix-exponential solution.

`mos_amplifier` and `mos_amplifier_loaded` test the amplifier at 125 MBd with
and without a 2 kOhm differential load. The absolute checker solves the tail
current equation by scalar bisection and derives drain voltages and common mode.
`passive_hybrid` and `passive_hybrid_loaded` exercise the bridge under the same
two output loads. Each native/Verilog-A flow must pass its independent absolute
checker before waveform comparison. Generated basic/1G Verilog-A files preserve
the same transistor, resistor and capacitor topology.

The generic MOS amplifier uses the framework's square-law channel law. A
separate process experiment in `IHPSG13G2/tests/amplifier.py` uses actual HV PSP
devices at 54 process/temperature/supply/load combinations. DC transfer, AC gain
and integrated input noise pass their independent limits: 31.02–54.19 uV RMS
from 1 kHz to 100 MHz and at most 0.261 dB loss at 125 MHz with corrected,
valid-finger device geometry. Those measurements do
not make the generic law identical to PSP or add layout/mismatch qualification.

The original composed
`receiver` remains the earlier gm/CTLE prototype. Replacing it requires compatible
common-mode bias, gain control, equalization, conversion and physical timing
recovery. Passing these isolated circuit tests does not silently replace those
remaining blocks or establish a layout-ready PHY.

## Sampling and reference conversion

[`mos_track_hold`](../basic/mos_track_hold.hpp) closes a bidirectional NMOS/PMOS
transmission gate while CLOCK is high. A CMOS inverter supplies the complementary
gate. When CLOCK falls, the 2 pF capacitor holds the achieved voltage, subject to
leakage, external loading and explicit clock/input coupling. Half-width dummy
switches with both conduction terminals tied to OUT oppose channel charge
injection in process models. The generic channel law has no intrinsic channel
charge; its dummy devices retain the same topology but cannot establish the
process model's injection accuracy. The PSP experiment checks actual acquisition,
turnoff injection, hold feedthrough and finite-source loading separately.

The compensated PSP circuit passes all 54 TT/SS/FF, −40/27/125 °C,
2.97/3.3/3.63 V and unloaded/200 kOhm cases. Its switch widths are 50 µm NMOS
and 100 µm PMOS, with half-width dummy devices, all at L = 0.45 µm. Worst
acquisition error is 6.00 mV, hold/feedthrough error 2.01 mV and turnoff injection
21.16 mV after folding the total widths into valid IHP fingers. The large-switch circuit without compensation failed injection; the
smaller-switch version failed slow-corner settling. These observations motivated
the added devices. This sweep has no mismatch, noise or extracted-layout proof.

[`mos_r2r_dac`](../basic/mos_r2r_dac.hpp) contains sixteen MOS channels and an
eight-bit R/2R ladder. BIT_N is complemented binary input, least significant bit
first. For ideal settled rail drivers, `Vout = Vss + code * (Vdd - Vss) / 256`
and source resistance is R = 10 kOhm. Loading with RL to Vss multiplies that
voltage difference by `RL / (R + RL)`. Real MOS drive resistance, resistor
currents and stored charge remain in the solve. Both flows check all 256 codes,
monotonicity and differential nonlinearity, with and without 20 kOhm loading,
before checking their waveforms against each other. Native UI/384 and SPICE
maximum UI/768 resolve switching more closely than the default link step.

[`SARADCFrontend`](adc.hpp) connects the sampler and ladder to the physical
regenerative comparator. Its controller and host-clock boundary are described
in [ADC operation](adc.md). This integration is distinct from the older ideal
sample conversion in the full link bench.

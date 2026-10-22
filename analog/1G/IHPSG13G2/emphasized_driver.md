# Physical main/history PAM5 output stage

[`basic_emphasized_pam5_driver`](emphasized_driver.hpp) inherits the physical
MOS-biased driver and adds a second four-cell steering bank. The two banks
share the output pins, pullups, finite supply and MOS bias reference. Their
currents and displacement charge influence each other through those nodes.

[`PAM5DriverControl`](../pam5_driver_control.hpp) retains the preceding symbol
in a register. Codes 0 through 4 represent +2 through −2. Four complementary
unary controls drive each bank. The main bank supplies three quarters of the
current and the history bank supplies one quarter, giving

```
Vdiff[n] = 0.125 V * (3 * symbol[n] + symbol[n-1])
```

at the nominal 100 Ohm differential load. This is a partial-response PAM5
transmitter: its seventeen intermediate output levels come from symbol history.
The coding alphabet remains five symbols.

Eight physical MOS tail sinks draw nominally 40 mA together. Each main sink
targets 7.5 mA and each history sink 2.5 mA. Two 50 Ohm pullups and the external
100 Ohm load produce approximately ±1 V for sustained outer symbols. A
zero-to-outer transition targets ±0.75 V before the history bank follows it.
The standard Test Mode 1 pattern and masks require separate checking; these
numbers alone do not establish transmitter compliance.

The reference uses a 1525 Ohm physical feed resistor. Its selected generic tail
strength is 43.9908637 mA/V² at a 0.35 V threshold. This preserves nominal
current after correcting the reference resistor; it is not an extracted PSP fit.

The class includes steering gate/source and gate/drain capacitances, tail
junction storage, tail-gate charge and 600 pF of shared bias storage. That large
storage value is an explicit circuit/area tradeoff, not free simulation memory.
The compact equations use selected framework MOS parameters. They do not
import PSP geometry, process variation, mismatch or layout parasitics.

Reset, disable or an invalid code turn every steering gate off. Invalid enabled
codes latch a fault until reset. Disable resets the retained history to zero
symbol, preventing a stale symbol from reappearing on re-enable. Disabled
steering leaves the physical pullups and reference connected; their loading
remains visible to other components.

The new regression checks actual gate voltages, every weighted level, disabled
output, supply current and bias disturbance in native C++RTL and independently
compiled Verilog-A/ngspice. It includes 80/100/120 Ohm loads, an open load,
time-step refinement and a deliberately reduced bias capacitor. The open load
must break the nominal swing purpose, and removing bias storage must expose
shared-bias disturbance. Both flows must pass their absolute contracts before
their plateau voltages are compared within 10 mV. All six cases pass
independently in native C++RTL and Verilog-A, and their comparison passes.
The common checker is rerun on each archived waveform before comparison.

```sh
cmake --build analog/build --target check-1g-transmit-driver
ctest --test-dir analog/build -R '^analog[.]1g[.]pam5_driver[.]' --output-on-failure
```

This block is not yet connected to the complete link, physical negotiation,
magnetics or padframe. Its controls are generated from the real controller in
the electrical unit fixture, with finite gate-drive resistance. Full PHY
clock distribution and IHP qualification remain separate requirements.

`tests/emphasized_driver.py` adds a nominal actual-PSP experiment. The C++ bench
exports every elaborated resistor, capacitor and MOS terminal. Explicit sizing
binds the reference and sixteen steering devices to 100 µm/.45 µm HV NMOS,
four main sinks to 450 µm/.45 µm and four history sinks to 150 µm/.45 µm. All
bodies connect to ground. Total widths are folded into 10 µm fingers: 10 for
reference/steering devices, 45 for each main sink and 15 for each history sink.
This follows the pinned PSP width range; a provisional single 450 µm finger
exhibited long gate-resistance settling and failed the output contract. The
fingered exploratory circuit with the corrected reference resistor reduced its
worst plateau error to 11.6 mV at coarse diagnostic resolution. Fine-step
qualification with 400 pF subsequently passed at 11.57 mV. The open-load generic
circuit missed its 20 mV bound in both solvers by about 0.018 mV. Increasing
the physical bias capacitor to 600 pF reduced that diagnostic error to 16.85 mV;
the full 600 pF generic qualification now passes in both solvers. The nominal actual
PSP experiment passes the unchanged absolute checker: maximum plateau error
11.34 mV and shared-bias ripple 7.72 mV. The process deck retains the finite gate resistors
and explicit storage and adds the PSP intrinsic charges. It refuses a changed
device inventory or an unbound MOS role. Its independent absolute qualification
requires additional PVT and layout checking; there is no inferred equivalence
between PSP and the generic laws.

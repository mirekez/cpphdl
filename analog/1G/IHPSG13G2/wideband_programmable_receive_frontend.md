# Receiver with higher gain-stage bandwidth

`wideband_programmable_receive_frontend<Count>` inherits the buffered
programmable receiver's ports, devices and sampling-storage declaration. Its
`_assign()` selects 0.25 pF compensation in the gain stage before elaborating
the parent circuit. The lower-gain compensation branches scale with that value.
The selected total compensation is therefore 1.25, 1.0, 0.75, 0.5 or 0.25 pF,
from the lowest to highest gain. All those capacitors contribute charge to the
connected circuit; changing the class changes the physical equations.

```cpp
wideband_programmable_receive_frontend<16> receiver;
receiver.PLUS = positive;
receiver.MINUS = negative;
receiver.Vdd = analog_supply;
receiver.Vss = ground;
receiver.Vlogic = core_supply;
// Connect gain selection, sampling controls and comparator feedback too.
receiver.elaborate();
```

The preceding 1 pF compensation variant passed nominal checks but failed the
SS/125 °C high-gain single-slice fixture: gain −6.119 and 21.042 mV linearity
residual. A separate PSP sizing experiment reducing the amplifier's compensation
to 0.25 pF passed the same fixture with gain −7.356, 4.090 mV residual and
8.533 mV acquisition error. Its source, executed deck and complete waveform
were retained. That experiment motivated this class; it does not qualify the
complete new class, whose selected compensation branches also scale.

Both classes use the [same single-slice fixture](tests/programmable_receiver_fixture.hpp).
`analog.1g.ihp_wideband_receive.{cpp,veriloga,compare}` exercises all five gain
settings with independent electrical checks followed by a 10 mV generic
waveform comparison. `.process_tt`, `.process_ss_hot` and `.process_ff_cold`
repeat those checks with IHP PSP devices at TT/27 °C, SS/125 °C and FF/−40 °C,
respectively. These fixtures use nominal supply voltages, finite source/control
resistance and finite supply impedance. `.bindings` checks actual topology,
geometry and storage and rejects altered circuits.

The process fixtures initialize intrinsic PSP charge through a bias solution
with the original nodal and capacitor-voltage constraints. A continuous
transient then warms the circuit for 16 µs before the original 20 ps maximum
measurement grid. Quiet warmup uses a declared 1 ns maximum step. Refinement,
full-bank acquisition/conversion, physical control translation, supply extremes,
noise/mismatch, loop margin and extracted layout remain required. Neither
single-slice success nor lower capacitor area establishes a working PHY.

## Current evidence

All five gains pass independently in native C++RTL and Verilog-A. Their separate
waveform comparison peaks at 2.600, 2.674, 2.780, 2.994 and 3.559 mV, below the
10 mV bound. All five also pass the IHP single-slice checks at TT/27 °C,
SS/125 °C and FF/−40 °C with nominal supply voltages. At SS/125 °C, the
highest gain is −7.35637, maximum linearity residual 4.090 mV and acquisition
error 8.533 mV. The C++ class's complete compensation branches are included.

Closed single-slice ADC conversion passes independently in C++RTL, Verilog-A
and nominal PSP, with identical generic ADC codes. Separate PSP high-gain
conversions pass at SS/125 °C and FF/−40 °C. The sixteen-slice Verilog-A test
initially completed 32 conversions at the lowest gain, but that result requires
repetition: its unbounded 144-channel DAC bridge can overflow ngspice's parser
token array. A simulator run that survives this overflow is insufficient
evidence. The bounded bridge's separate 1/4/16-slice controller test passes.
Full-bank conversion, 64-acquisition and numerical-refinement tests remain
under qualification. Fixed-PWL acquisition does not use the affected bridge.

The closed generic comparison checks ADC codes within one count. Its Verilog-A
host bridge adds approximately 3 ps clock latency and 100 ps control transitions;
native control values are discrete. This is a comparison of independently
checked conversion results. The separate fixed-PWL acquisition fixtures compare
voltage waveforms with identical electrical stimulus and a 10 mV limit.
Physical control translators remain required in the IC.

The real [MIM tests](capacitor-layout.md) check every compensation size and
measure its footprint. Reduced capacitor area and single-slice results do not
establish full-bank behavior or full-chip qualification.

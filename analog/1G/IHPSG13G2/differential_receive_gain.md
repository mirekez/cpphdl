# Differential gain and common mode

`differential_receive_gain` is an IC assembled from two resistor strings,
NMOS tap selectors with CMOS control inverters, a cascode OTA, and an NMOS output follower. Wires connect
all devices into one coupled circuit. A gain control selects corresponding
taps on both strings; selector gates draw charge but carry no string current.

For tap code k, the ideal closed-loop relation is:

```
OUT = VREF + k / (9 - k) * (IN_PLUS - IN_MINUS)
```

This relation expresses the intended behavior. Simulation solves the actual
MOS, resistor and storage equations, including feedback, loading and supply
impedance. Code 4 targets gain 0.8; code 8 targets gain 8. The polarity is
positive because the negative input connects to the inverting string.

The two strings each contain nine 100-ohm sections. Their input loading is
real: the testbench's two 50-ohm sources lose voltage. Checks therefore use
achieved input difference, and separately constrain output headroom. VREF
is a port with finite testbench source resistance and storage; it is not an
ideal reference primitive. [`receive_reference`](receive_reference.md) builds a candidate physical
supply-ratio source for integration and is being checked separately.

The signal selectors use NMOS channels because the PMOS-only version failed
at 1.1 V input common mode: body effect made its selected channel too weak.
Increasing compensation did not fix the resulting bias and gain errors. Five
small CMOS inverters derive active-high gates from the existing `SELECT_N`
interface. The switched compensation capacitors retain their PMOS selectors
because those switches operate near the much higher amplifier drive voltage.

The cascode OTA has no extra DC output shunt. Its MOS branches, compensation,
output follower and closed feedback define its operating point. The earlier
100-kilohm shunt version failed the gain-8 offset limit; a 1-megohm trial also
failed the process check. Both failures remain in diagnostic results. Removing
that shunt avoids its physical resistor area and its systematic loading error.

The unit experiment starts from zero supply/input conditions, warms up for
16 microseconds, then applies 32 PAM-5 plateaus at 8 ns spacing. It tests gain
codes 4/8 at input common-mode voltages 1.1/1.4/1.65 V. Every settled solver
state must meet gain, 15 mV linearity and offset limits, reference bounds and
output/drive/supply headroom. These are unit limits, not an Ethernet compliance
claim. The preceding PMOS-selector OTA version passed both generic flows and a
nominal IHP gain-8/1.4 V case, but failed the lower-common-mode process case.
The NMOS-selector candidate uses a 500-ohm output-follower return instead of
250 ohms. Lower DC current improves the high-output headroom while reducing
power; no transistor width increase is needed. It passes both generic flows,
their 5 mV waveform comparison (maximum difference 3.757 mV), and the full
nominal PSP matrix. Maximum nominal fitted offset is 13.844 mV.

The slow/hot sweep passes at common modes 1.1 and 1.4 V, but its gain-0.8 case
at 1.65 V fails linearity: 15.859 mV against 15 mV. That stress failure remains
recorded. The complete fast/cold unit sweep passes. The coupled 50-MOS preamplifier
passes nominal, slow/hot and fast/cold process tests at a 2.3 V line common
mode. Its achieved PGA input mean is 1.238..1.274 V across those conditions.
The actual connected input range must be established before changing the
circuit or narrowing its declared operating range.

Run the registered `analog.1g.ihp_differential_gain.*` CTests after building
`analog_1g_ihp_differential_receive_gain`. PSP binding checks the actual C++
inventory and contribution equations before replacing channels with reviewed
IHP HV dimensions and legal finger geometries. Process model equations are
intentionally distinct from the generic C++RTL/Verilog-A comparison. Resistors
and capacitors are still nominal; mismatch, noise, physical passives, extracted
layout and the coupled MDI/ADC system are separate acceptance steps.


The receiver's maintained operating tests use source common modes 1.1 and
1.4 V. This boundary follows the measured connected circuit: the complete
MDI/reference/preamplifier sweeps at line common modes 2.1/2.5 V and both gain
settings pass nominal, slow/hot and fast/cold conditions; their PGA input means
stay within 1.182..1.321 V. Achieved individual terminals stay within
1.139..1.350 V, supporting the reference unit's separate 0.9..1.5 V load sweep.
Those tests use nominal supply and passives and the midrail-reference fixture;
they do not establish all intermediate process/supply/mismatch conditions or
qualify the assembled ADC-range-reference/eight-lane system.

The 1.65 V slow/hot point remains an explicitly unsupported characterization.
`analog.1g.ihp_differential_gain.high_common_stress` runs that actual process
case, rechecks its retained waveform and verifies that linearity alone fails
the unchanged 15 mV bound. Its passing test result verifies rejection, not a
passing analog operating point. The 1.65 V source cases remain available in
`differential_receive_gain.py --common 1.65` for characterization. No voltage
error limit has been increased to hide that failure.

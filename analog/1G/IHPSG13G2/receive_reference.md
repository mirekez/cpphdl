# Shared receive reference

`receive_reference` is an electrical IC assembled from a supply divider,
bias resistors/transistors, a cascode OTA and a source follower in feedback.
Its intended output is Vdd/2. It is a supply-ratio reference; it does not
claim temperature-independent absolute voltage. Its output is a port, so
connected gain stages change its current, voltage and transient response.

`cascode_voltage_buffer` exposes its bias ports so larger designs can share
bias circuitry. The reference IC supplies those biases with physical divider
and diode-connected MOS networks. Its output follower has a real 250-ohm
return to Vss and a finite transistor channel to Vdd. Storage is explicit:
10 pF at the divider tap and 30 pF at the buffered output, in addition to
amplifier/bias/device storage. These values require physical realization.

The unit bench loads the output with 112.5 ohms to a varying voltage. This
represents eight parallel 900-ohm positive gain strings. Load voltage steps
between 1.1 and 1.9 V, in 0.1 ns, while a shared 0.25-ohm supply impedance
couples reference current back into its own rail. This is a worst coincident
static string load; actual selector and input-device charge must still be
included in the receiver integration test.

The checks require the reference to return within 15 mV of achieved Vdd/2
by 6 ns after each transition, stay within 100 mV during transitions, and
keep its divider tap within 2 mV. Both generic solvers are checked separately,
then directly compared within 5 mV. A 5 ps measurement step and complete
solver states are retained. PSP additionally uses reviewed IHP HV dimensions
and legal fingers, with the same purpose limits.

The first 2 pF/2 pF implementation passed the generic checks but failed PSP:
151.293 mV transient excursion and 3.903 mV divider kickback. Its settled error
was 2.322 mV. That failure remains recorded. A 20 pF output version still failed slow/hot
transients. The selected candidate uses 30 pF output storage and a W = 1600 um
HV NMOS follower, represented by an explicit reviewed process choice. The
larger follower improves settling enough to use the additional storage.

The candidate passes both generic solvers and their 5 mV waveform comparison.
It also passes TT/27 C, SS/125 C and FF/−40 C PSP tests independently:

| Process condition | Maximum settled error | Maximum transient error |
| --- | --- | --- |
| TT / 27 C | 11.237 mV | 83.607 mV |
| SS / 125 C | 10.769 mV | 98.792 mV |
| FF / −40 C | 6.436 mV | 74.594 mV |

The 15 mV settled and 100 mV transient limits are unchanged. Maximum divider
error is 1.432 mV. The slow/hot transient has only 1.208 mV margin to its unit
bound; nominal passives and unextracted geometry do not establish manufacturing
margin. The candidate is not qualified for the assembled receiver or chip.

Build `analog_1g_ihp_receive_reference`, then run the registered
`analog.1g.ihp_receive_reference.*` CTests. Each receipt records source/model/
tool hashes and independent electrical checks. The binding regression also
rejects changed transistor geometry, supply-body connections, initial charge,
missing contributions and unknown component profiles.


## ADC-range specialization

`adc_range_reference : receive_reference` changes the declared divider arms
to 1275 and 1000 ohms. Its target is 1000/2275 of achieved Vdd, approximately
1.45055 V at 3.3 V, centering the receiver's 0.85..2.05 V ADC input window.
It inherits the physical bias, amplifier, follower and storage circuits;
`regulate()` or another ideal reference primitive is not introduced.
`generated/OneGIHPADCRangeReference.va` is its exported equation model.

The testbench explicitly selects `--load-profile wide` (the original
1.1..1.9 V sweep) or `--load-profile receiver` (0.9..1.5 V). These profiles
change only external load excitation. The actual C++ class and load profile
are recorded and checked before simulation; comparison requires matching
profiles. All voltage limits and all eight-channel loading remain identical.
The contract regression rejects excessive error, incorrect divider targets,
wrong load labels, incomplete apertures, rail/headroom faults and inadequate
stimulus.

The ADC-range class fails the wide PSP characterization at 16.252 mV settled
error versus 15 mV. That result is retained. Under the receiver load profile,
it passes independent C++RTL and Verilog-A checks and their waveform comparison
(maximum node difference 3.257 mV). Process results are:

| Condition | Maximum settled error |
| --- | --- |
| TT / 27 C | 8.445 mV |
| SS / 125 C | 8.289 mV |
| FF / −40 C | 7.648 mV |

Transient error remains below 100 mV and divider error below 2 mV. Receipts are
under `tests/results/adc_range_reference_receiver*` in the build tree. The
connected MDI/PGA input-range tests separately check whether receiver terminal
voltages meet the declared 0.9..1.5 V load assumptions. Coincident selector
charge, eight-lane coupling, resistor corners, mismatch, noise and extracted
layout still require integration checks.

Build `analog_1g_ihp_adc_range_reference` and use the registered
`analog.1g.ihp_adc_range_reference.*` CTests. The original midrail class and its
wide characterization remain separate tests.

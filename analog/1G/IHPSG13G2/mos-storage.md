# MOS bias storage and measured area

Bias decoupling tolerates voltage-dependent storage; the ADC hold and DAC
accuracy contracts are different. The process experiments explicitly select
MOS gate charge for the transmitter bias nodes while retaining the C++ IC's
nominal 400 pF / 50 pF approximation. They are process realizations of that
storage intention, with distinct equations, rather than identical-equation
Verilog-A exports. Actual charge, gate leakage and finite device response enter
the coupled PSP simulation. The generic C++RTL and generated Verilog-A circuits
remain independently checked against their own nominal equations.

## Current-reference storage

Forty dedicated `sg13_moscap_n` cells, each 30 x 30 micrometres, replace only
the identified 400 pF capacitor. Their substrate terminal connects to ground.
The pinned `moscap_tt` model is characterized at −40, 27 and 125 C. Across
0.85–1.10 V and 100 kHz–1 GHz, minimum storage is 449.80, 443.66 and 436.38 pF
respectively. Leakage is also measured through 1.30 V, reaching at most
42.30 microamps at 125 C. The full transient must stay between −0.1 and 1.3 V;
a nominal storage estimate alone cannot establish this.

The nominal complete transmitter passes its unchanged all-state electrical
checks with this realization: maximum plateau error 9.698 mV against 20 mV.
Its observed bias spans 0.000019–0.966602 V, including cold startup. The cascode
bias still uses its nominal 50 pF capacitor in this first experiment.

The actual forty-cell layout occupies 57,565.0514 square micrometres. The
400 pF MIM bank occupies 324,808.8064 square micrometres. This is an 82.28%
standalone footprint reduction. It excludes chip routing, density balancing,
extracted coupling and any new integration margins. The isolated MOSCAP bank
exceeds the global active-density maximum; its surrounding chip area must be
checked rather than presenting local geometry as full-chip DRC.

The MIM reference also triggers `MIM.gR`: its 266,158.592 square micrometres
of plate area exceed the pinned deck's recommended whole-chip maximum of
174,800. That retained failure motivates changing implementation technology;
no DRC rule is disabled or reclassified as a passing chip result.

## Cascode-bias storage

The second trial replaces 50 pF by 220 parallel HV NMOS units. Each unit has
W = 100 um, L = 0.5 um and ten fingers; drain, source and body are grounded.
TT/SS/FF AC characterization at all three temperatures retains at least
52.331 pF over the originally tested 1.6–2.0 V and 100 kHz–1 GHz. Larger individual units lost useful
high-frequency capacitance and were rejected. The selected repeated small
units preserve the frequency response; their multiplicity is explicit.

The physical bank uses the routed four-terminal MOS macro and connects D/S/B
on Metal3. Gates use Metal2 and route below each unit's ground terminals;
wide bank straps join the rows. All units share the actual substrate through
explicit bulk ports in the verification schematic, retaining each finite tap.
Independent hidden substrate nodes would describe a different circuit.

Its footprint is 120,138.456 square micrometres and its local DRC and strict
transistor LVS pass with unmodified PDK decks. Cut gate straps and removed
return vias fail LVS. The combined two-bank footprint is 177,703.5074 square
micrometres, versus 371,565.8592 for the two MIM reference banks: 52.17% less.
Fresh complete transmitter tests with both MOS storage realizations pass at
TT/27 C, SS/125 C and FF/−40 C. Maximum all-state plateau errors are 9.410,
13.190 and 11.993 mV, respectively; bias ripple is 6.589, 6.233 and 7.000 mV.
The corresponding shared-line static interaction tests also pass. These tests
retain the original 20 mV error and 15 mV ripple limits.

Startup overshoots the nominal cascode bias: observed maxima are 2.280, 2.270
and 2.291 V. An initial 2.2 V characterization guard correctly failed. The
actual charge/leakage study was extended through 2.4 V at all three corners and
temperatures before fresh transmitter runs were accepted. The earlier failed
receipt is retained. Only exactly zero initial voltage is accepted for the two
replaced storage stamps; nonzero or missing storage is rejected by binder tests.
This footprint comparison is not accepted die area or extracted performance.

## MOSCAP LVS correction

The pinned dedicated-MOSCAP connection deck links its derived bulk port to
diffusion but omits its link to the Metal1 routing conductor. The unmodified
unit LVS failure is reproduced and retained. `moscap_lvs.py` creates a separate,
audited copy that adds only the two geometric bulk-to-Metal1 connections for
N/P MOSCAPs. It does not modify installed PDK files, device equivalence,
port requirements, tolerances or DRC. Every copied rule and the exact delta
are hashed in the result. Upstream changes require review; the adapter rejects
an already-corrected deck.

The forty-cell bank passes strict LVS with this adapter. A disconnected gate
strap and removed substrate vias are independently rejected. This is explicitly
adapter-based LVS, not a claim that the unmodified upstream LVS passed. HV
MOS gate-storage LVS uses the unmodified deck. Both layout experiments retain
the isolated global-density failures and do not qualify a complete chip.

## Run

Build the cascoded transmitter fixture and run the relevant CTests:

```bash
ctest --test-dir analog/build -R 'analog.1g.ihp_moscap_bias_storage' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_mos_gate_storage' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_moscap_transmit' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_mos_storage_transmit' --output-on-failure
```

Each process run starts from the real C++ export and reviewed transistor binder.
Only the two identified cold-start capacitors may be replaced. Their ideal
stamps are removed to avoid counting their charge twice. Raw trajectories,
geometry, source/model/tool hashes and independent bounds remain in results.
The combined trial also records every cascode-bias solver state and checks its
range. MOSCAP has a TT/statistical deck; selecting a MOS transistor SS/FF corner
does not create an unavailable MOSCAP corner. Mismatch, extracted gate/strap
resistance, noise and full-chip power/voltage sequencing remain to be qualified.

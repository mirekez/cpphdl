# Transmitter bias layout

`transmit_bias_layout.py` implements `trimmed_mirror_bias` and the cascode
reference divider from `cascoded_emphasized_pam5_driver`. Its ten ports are
VDD, VSS, BIAS, CASCODE and six active-low trim controls. A 100 µm/0.45 µm HV
NMOS is diode connected at BIAS. The 1.8 kΩ supply feed is supplemented by six
HV PMOS/resistor branches with nominal resistances 256 kΩ divided by powers
of two. The 3 kΩ/3.6 kΩ divider generates the cascode reference. The surrounding
transmitter must provide translated trim voltages and the separately tested
400 pF/50 pF storage banks.

The layout uses actual guarded PDK MOS and rppd cells. Four parallel cuts at
routing transitions and 2 µm signal buses reduce bias-current voltage drop.
HV and LV NMOS bodies share the row's physical substrate, with each guard
contact retained in the schematic. The footprint is 221,281.85 µm²; this first
connected implementation prioritizes correct operation over packing the long
high-value resistor bodies.

The first narrow-route candidate failed monotonicity at carry transitions and
exceeded the 5% comparison budget. Wider routes and parallel vias fix those
failures without changing the acceptance limits. Maintained tests now pass:

- Local DRC, strict LVS with 32 normalized devices and all ten ports.
- LVS rejection after physically removing a trim switch.
- Conserved RC extraction with 70 MOS fingers and nine physical resistors.
- Explicit resistor-body charge partition and all 64 codes at TT, 27 °C,
  3.3 V. Physical and schematic circuits independently satisfy bias/cascode
  ranges, positive supply current and monotonic trim before comparison.
- Six malformed-result guards, including missing codes, reversed current,
  nonmonotonic response and excessive physical-versus-schematic error.

The physical BIAS range is 0.9403–1.0063 V. CASCODE is 1.7943–1.7947 V.
Maximum comparison differences are 2.963% BIAS, 0.315% CASCODE and 3.299%
supply current, below the 5% local limits. These are component DC results;
full transmitter loading/startup, storage integration, trim calibration, PVT,
mismatch, EM and filled-chip verification remain required. Isolated minimum
metal-density violations remain recorded for chip fill.

From the cpphdl repository root:

```bash
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog.1g.ihp_transmit.bias(_guards)?$' --output-on-failure
```

Results are regenerated under the build directory, rather than committed.

## Reference-access candidate

`transmit_bias_reference_access.py` adds parallel upper-metal rails, direct
source/drain routes and via arrays across the reference transistor fingers.
It preserves the devices, original connections and external Metal5 ports.
The default macro remains unchanged until complete transmitter qualification.

The registered candidate CTest passes local DRC/LVS, deliberate missing-switch
rejection, conserved extraction and the same 64-code DC checks. At trim 27,
3.3 V and TT/27 °C, its physical-versus-schematic bias difference is 4.862 mV,
compared with 23.433 mV for the original layout. Across all codes the maximum
bias difference is 6.515 mV. These local results do not establish compliance
with the complete transmitter's 20 mV output-level limit or full-chip DRC.
Minimum-density violations still require fill and requalification.

```bash
ctest --test-dir analog/build/regeneration-audit \
  -R '^analog.1g.ihp_transmit.bias_reference_access(_guards)?$' --output-on-failure
```

The candidate receipt and GDS regenerate under
`build/regeneration-audit/1G/IHPSG13G2/tests/results/transmit_bias_reference_access/`.

Five registered routing guards verify device/port and existing-conductor
preservation, and rejection before mutation for unsupported finger geometry,
relocated ports, missing escapes and repeated application.

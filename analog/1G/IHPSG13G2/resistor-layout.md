# Physical poly resistance

[`resistor_layout.append_rppd`](resistor_layout.py) sizes a straight IHP `rppd`
PCell from requested resistance and explicit width. The pinned PDK calculation
includes body sheet resistance, linewidth correction and metal-to-poly contact
resistance. Dimensions round upward and nominal resistance must stay within
1% of the request. Width starts at 2 µm; large-current resistors need a wider
choice. The [IHP process specifications](https://ihp-open-pdk-docs.readthedocs.io/en/latest/process_specs/02_process_control_params.html#rppd-specs)
describe the material, temperature dependence and current-density limits.

The cell exposes Metal2 `PLUS`, `MINUS` and `SUB` ports. Via1 contacts reach
both actual Metal1 end contacts and the retained substrate guard ring. Its
independent LVS schematic includes the poly resistor and the ring's extracted
tap resistance. The process simulation instance uses the three-terminal
`rppd` subcircuit and the PDK's `r3_cmc` model.

| Request | Width | Body length | PCell nominal resistance | Bounding footprint |
| --- | --- | --- | --- | --- |
| 100 Ω | 20 µm | 7.43 µm | 100.0610 Ω | 311.1408 µm² |
| 2.5 kΩ | 2 µm | 19.02 µm | 2,500.2044 Ω | 157.4640 µm² |
| 100 kΩ | 2 µm | 772 µm | 100,094.8205 Ω | 5,036.7744 µm² |

All three pass strict three-port resistor/tap LVS and complete local DRC,
including pin and antenna checks. Removing the physical vias fails LVS while
the schematic and terminal labels remain intact. The isolated layouts retain
global minimum-density failures; the report keeps `full_chip_drc` false.
The PDK model's four nonzero DC points, from −100 to +100 mV at 27 °C and
typical resistor parameters, agree with their nominal geometry within
0.000022%. This low-field check does not establish PVT, self-heating, mismatch,
high-current reliability or the connected receiver's behavior.

The pinned PCell's Tcl callbacks format lengths of at least 100 µm as
millimetres with three decimal places. The helper chooses an upward whole-µm
length in that range, then checks actual generated contact spacing. It rejects
an unrealizable dimension instead of describing one geometry in the schematic
and producing another. The earlier 771.27 µm request became 771 µm and failed
strict LVS; that failed run is retained. The 772 µm design passes.

Run `analog.1g.ihp_resistor.layout` with CTest. Its latest report is
`analog/build/1G/IHPSG13G2/tests/results/resistor_layout/checks.json`, and each
invocation retains its own layout, schematic, model deck and rule reports.

The receiver's present MOS process tests still contain nominal ideal resistors.
This macro is a physical building block; it has not yet replaced those branches.
In particular, a 1 GΩ phenomenological leakage branch cannot become a practical
poly resistor through this helper. Matching structures, parasitic loading,
process variation and physical current paths require explicit circuit work.

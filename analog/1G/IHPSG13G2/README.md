# IHP SG13G2 implementations

Keep process-specific cells, their tests and their generated counterparts here.
Generic PHY topology remains in `1G/`; downloaded foundry data remains in
[`analog/pdk/IHPSG13G2/`](../../pdk/README.md).

`current_steering_driver.hpp` declares a four-cell unary differential output
stage as an `IC`. Each cell adds two NMOS devices sharing a tail. Four ideal
supply-referenced tail biases draw 5 mA each. Two 50 Ohm pullups and an external
100 Ohm differential load give 0.25 V per PAM level, with a nominal 2.8 V output
common mode at a 3.3 V supply. Gate controls are complementary 0/3.3 V signals.
Each output has an explicit 0.5 pF capacitor.

The C++ and generated Verilog-A counterpart use the framework's **generic
square-law channel** with beta 20 mA/V² and threshold 0.7 V. These are prototype
parameters, not imported IHP process parameters. `generated/OneGMOSLineDriver.va`
is the exported same-equation counterpart for solver regression.

`tests/driver.py` creates the same steering topology using the actual IHP
`sg13_hv_nmos` four-terminal subcircuit, with W = 100 um and L = 0.45 um, body
connected to ground. It loads `cornerMOShv.lib` in the prepared PDK and runs
125 MBd waveforms in ngspice using the compiled PSP models. TT, SS and FF pass
the same five-level and common-mode absolute checker as the two prototype flows.
The PDK tests also check tail headroom, supply current and settling 1 ns after
the control ramp. They are enabled when the prepared model binaries are present.

This establishes a tested process-specific topology, not a foundry-characterized
Ethernet output cell. The tail bias generators and gate drivers remain ideal;
there is no transistor bias reference, 1.2-to-3.3 V level shifter, ESD, magnetics,
layout, mismatch/noise analysis or extracted parasitic model. Its 2.8 V common
mode also needs an explicit interface design before replacing the generic
system driver's 1.65 V common mode. It is not silently substituted into the link.

```bash
ctest --test-dir ~/cpphdl/analog/build -R '^analog\.1g\.(mos_driver|ihp_driver)\.' --output-on-failure
```

PDK setup: `../../pdk/.prepare_IHPSG13G2.sh`. To select another prepared location,
configure CMake with `-DCPPHDL_IHP_PDK_ROOT=/absolute/path/to/IHP-Open-PDK`.
## MOS bias and trim implementation

[biased_driver.hpp](biased_driver.hpp) adds a resistor/diode-connected MOS bias,
physical MOS tail sinks and a PMOS-switched six-bit resistor bank. Read
[the circuit and validation contract](biased_driver.md) for sizes, trim codes,
native/Verilog-A comparison and PSP process-corner results. The older
`current_steering_driver` remains available as an explicit ideal-bias reference.

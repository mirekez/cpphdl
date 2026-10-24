# Continuous C++RTL and PSP co-simulation

[`xspice_live.py`](xspice_live.py) runs the native C++RTL ADC controller inside
one continuous ngspice transient using its `d_cosim` interface. The adapter is
[`tests/sim_adc_controller.cpp`](tests/sim_adc_controller.cpp), compiled as a
small shared library against the installed ngspice ABI headers. It includes
the digital controller and C++HDL runtime headers; the analog framework's core
does not acquire a digital simulation dependency.

An `adc_bridge` converts each achieved core-domain decision voltage into a
three-state logic value. On a rising host clock the adapter samples only the
currently converting slices. An unresolved input fails the run. It then calls
the real controller's work/strobe sequence once and publishes its trial and
acquisition outputs. `dac_bridge` generates their 100 ps unit-control ramps;
the existing finite 50 Ω control drivers draw current from the solved rails.
These bridges and the host clock are explicit simulation adapters. They do
not stand for implemented clock distribution or voltage translators.

Clock input and co-simulator delays are explicit: each is 1 ps, with a 2 ps
clock rise. Observed controller events occur 3 ps after their nominal edge.
The checker rejects phase offsets above 5 ps, missing/extra clocks, changed
slice masks, unresolved data or feedback bits inconsistent with the saved
analog trajectory. Decisions must remain resolved with the same value over
a 50 ps window around the sampling event. The independent conversion checker
then checks acquisition, hold, raw accuracy, cadence and normalization.

`tests/xspice_controller.py` drives distinct decision patterns through all
1/4/16-slice interfaces, including sixteen-slice reuse. Every resulting control
row must exactly match a separately stepped native controller. It independently
checks all analog control bits using weighted output observations. The bridge
checker has separate wrong-bit, missing-clock, late-clock and unresolved-voltage
fault tests.

```sh
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_conversion[.]xspice_'
```

`tests/receive_conversion_process.py --backend xspice` selects continuous
co-simulation. `--backend prefix` retains the slower causal-prefix audit with
its source-checked checkpoints and resume. Both use their own solved PSP
circuit and the same independent conversion limits. Prefix replay checks its
recorded electrical voltages against the final trajectory. The continuous
backend instead checks the actual bridge event bits against that trajectory.
It does not invent recorded analog voltage reads after the run.

The continuous deck selects one solver thread. This avoids multiplying model
workers when several independent regressions run concurrently. Relative library
paths avoid ngspice lowercasing the uppercase package directories on model
cards with several quoted parameters. Sources, interface headers, code-model
libraries, model files, tools and generated artifacts are hashed in the report.

The [official ngspice manual](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf)
describes `d_cosim`, its `co_info` ABI and the analog/digital bridge models.
The installed `share/ngspice/scripts/src/ngspice/cosim.h` is the ABI used here.

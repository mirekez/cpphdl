# Native IHP SG13G2 devices

`mos.hpp` supplies electrical IC classes for the native class-based solver.
`mos` has member ports `D`, `G`, `S`, `B`; its inherited `process`, `parameters`
and `temperature_K` select the actual IHP PSP model. Connections are shared
unknowns: each device contributes current, charge and derivatives to one
coupled solve. A device does not independently advance its terminal voltages.

```cpp
#include <basic/IHPSG13G2/mos.hpp>

class transistor_stage : public analog::IC {
public:
    _PORT(analog::electrical) A;
    _PORT(analog::electrical) Y;
    analog::ihp::mos pull_down;

    void _assign() override {
        pull_down.D = Y;
        pull_down.G = A;
        pull_down.S = Vss;
        pull_down.B = Vss;
        pull_down.elaborate();
    }
};
```

Before elaboration/binding, configure `pull_down.process` with an
`analog::osdi::model` and its instance parameters. The complete executable
example, including supplies, load, model setup and observations, is
[process_bench.cpp](../../tests/IHPSG13G2/process_bench.cpp).

`inverter.hpp` composes two `mos` objects into a CMOS inverter with member
ports `A`, `Y` and inherited `Vdd`, `Vss`. The PMOS enables the output from
Vdd; the NMOS discharges that same output to Vss. Both devices contribute
their PSP current and terminal charge to the shared solve. This is a circuit
class, not a Boolean or ideal-voltage-source inverter primitive.

[inverter_bench.cpp](../../tests/IHPSG13G2/inverter_bench.cpp) derives a
`loaded_inverter` class that adds an output capacitor and overrides `_assign`.
The test frontend reads the installed `sg13g2_inv_1` CDL, checks its topology,
and configures both transistor geometries from it. No process or corner is
silently selected by the reusable inverter class. The ngspice reference uses
the same CDL connections and geometry with the original PDK wrappers; CDL
MOS instance prefixes change from `M` to `X` for those subcircuit wrappers.

`parameters.py` resolves the installed PDK's LV/HV NMOS/PMOS PSP cards for
TT, SS and FF corners without running ngspice. It preserves the single-finger,
non-RF schematic wrapper's area/perimeter defaults, verifies wrapper defaults,
rejects unknown expressions and records input hashes. It is deliberately not a
complete SPICE hierarchy frontend. Multichannel/multifinger layouts, mismatch,
RF/NQS and arbitrary extracted subcircuits need additional lowering.

`varactor` exposes the three terminals of a MOSVAR **core**, not the complete
IHP varactor wrapper (which also includes well, junction and coupling elements).
No full-varactor qualification is claimed by the PSP tests.

Model parameters are fixed after binding. Inherited virtual `evaluate`,
`begin_step`, `accept` and `reject` implement device behavior and transactions;
`behavior::traits<ihp::mos>` inherits compact-model capabilities. Derived ICs can
compose and override behavior. The simulator owns time advancement and solves
`F(x,t) + dQ(x,t)/dt = 0` for all connected objects.

See [process tests](../../tests/IHPSG13G2/README.md) for installation, building,
comparison commands and supported scope.

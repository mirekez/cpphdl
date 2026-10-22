# Achieved electrical clocks in co-simulation

An analog clock has finite transitions. The ordinary `sim_sense` adapter
returns unknown between its low/high thresholds. That is useful for checking
an unresolved comparator decision, but a two-state C++RTL module cannot use
an unknown clock level.

`sim_schmitt_sense(node, reference, low, high)` retains its preceding known
level inside the band. It starts unknown until a threshold establishes a
level. This is a simulation adapter with zero input loading; a physical input
buffer and its loads must still be declared as circuits. Hybrid iterations
use the state on entry to the integration step, and rejected steps roll that
state back before retry. The module lifecycle advances only after an accepted
electrical solution supplies a rising edge.

[`live.py`](live.py) provides `NgspiceVoltageClock` for the other solver. Start
ngspice and pause at a known initial clock rail, then construct it with the
observed voltage vector and the same thresholds. Each `advance(end)` resumes
until the opposite threshold or the end guard. The object owns the session's
breakpoints and reports rising/falling edges separately. An initially in-band
voltage is an error, and a completed clock cannot resume beyond its guard.

Ngspice stops at an accepted integration point satisfying an inequality; this
is not exact root localization. Its maximum step and the native observation
step bound clock quantization. Refine both and check the resulting controller
behavior and edge times. The implementation uses `ge`/`le` breakpoint aliases
and retains transient state between `resume` calls. See the official
[ngspice manual](https://ngspice.sourceforge.io/docs.html), “Stop: Set a
breakpoint” and “Resume: Continue a simulation after a stop.”

`analog.schmitt_sense` checks known/in-band excursions, both clock polarities,
native state advancement, ambiguous startup and rejection after a trial
hysteresis change. It passes at two step sizes. `analog.veriloga.live_clock`
checks actual RC-loaded ngspice transitions, in-band noise, ambiguous startup,
termination and refinement. Its maximum measured refinement departure is
5.62 ps for 10 ps versus 5 ps integration limits.

The reference PLL regression additionally drives the same C++RTL edge counter
from each solver's own achieved clock. That integrated regression must pass
again after this adapter is connected; earlier analog-only PLL results do
not establish the new controller boundary.

# Clocked transistor comparator

[`basic/strongarm_comparator.hpp`](../basic/strongarm_comparator.hpp) connects
eleven MOS devices. A differential input pair shares a clocked tail transistor.
Two cross-coupled NMOS and two cross-coupled PMOS devices regenerate the small
input imbalance. Four clock-controlled PMOS devices precharge the internal and
output nodes. The topology follows the circuit discussed in
[Razavi, *The Design of a Comparator*, Figure 1](https://www.seas.ucla.edu/brweb/papers/Journals/BR_SSCM_4_2020.pdf).
The selected dimensions, loads, clock and tests here are our SG13G2 experiment;
the paper's process, dimensions and performance are not transferred to it.

Clock low resets both outputs high. Clock high evaluates: `PLUS > MINUS` makes
`OUTP` high and `OUTN` low. A following register must capture the result late in
the evaluation phase, before reset begins. There is no validity/lock signal and
no guarantee of a decision at exactly equal inputs. A single comparator is not
an eight-bit ADC: threshold generation, sampling, encoding, offset/noise control,
capture timing and the interface to the receive DSP still require implementation.

The generic circuit uses the framework's square-law MOS equations and explicit
storage and gate-coupling capacitors. There is no ideal comparator operation,
controlled ideal output source or `sim_` primitive in the IC. An external load
adds to the same node's charge and changes its response. Input charge also flows
back into whatever drives the gate ports. The generic fixture's input sources
are ideal boundaries; the process fixture adds 50 ohms on each input.

The `strongarm` and `strongarm_loaded` fixtures check both decision signs at
1, 2.5, 5, 10, 40 and 80 mV input differences and 125 MHz. The second adds
150 fF to each 50 fF output. Each native and Verilog-A run must independently
pass decision, complementary rail and four-node precharge checks before its
trajectory can be compared. Refinement halves each solver's timestep and
checks convergence. Regeneration needs a much smaller timestep than the symbol
period: the base native step is UI/3072 and SPICE maximum is UI/6144. An initial
UI/128 experiment incorrectly settled near the unstable midpoint in the native
backward-Euler solver. Its failed result is not accepted as a physical decision.

`IHPSG13G2/tests/comparator.py` uses actual four-terminal HV PSP models with
grounded NMOS bodies and supply-connected PMOS wells. Input/tail NMOS dimensions
are 50/0.45 µm, feedback NMOS 20/0.45 µm, feedback PMOS 40/0.45 µm and reset
PMOS 10/0.45 µm. It exercises TT/SS/FF, −40/27/125 °C, 2.97/3.3/3.63 V and
both output loads, with 50-ohm input sources. It independently checks decisions,
reset, rails and supply energy. Intrinsic PSP charge is retained in addition to
the explicitly declared capacitors. The process and generic models therefore
have different equations and are not presented as an equation-equivalence test.

All fourteen native/Verilog-A absolute, comparison and refinement CTests pass
(169.37 s for the selected run). Both loads have zero wrong decisions; the
largest coarse/fine waveform RMS difference is 5.12 mV. The generated standalone
Verilog-A module also compiles with OpenVAF. The IHP PSP PVT CTest passes all
54 cases with valid IHP fingers (1262.60 s): worst late-decision rail error
1.28 µV, worst precharge error 1.85 mV and maximum measured supply power
1.651 mW. This power excludes
the external clock and input-source drivers. Reports retain model/source hashes
and compressed complete waveforms; these are deterministic selected-case results.

Neither deterministic experiment qualifies mismatch, transient noise, ADC
resolution, aperture jitter, kickback at a complete AFE or a transistor layout.

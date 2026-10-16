# Context handoff for Codex: intention-first electronic circuit description language

## User's original objective
Design a compact, composable language/model that maps transistor-level analog/digital building blocks to arithmetic/semantic primitives, allowing fast description of individual transistors, interconnected networks, CMOS gates, flip-flops, LPM/arithmetic blocks, memories, operational amplifiers, PLLs, feedback loops, power rails, regulators, and control loops. The system should represent analog behavior when needed but avoid per-IC modeling of general parasitics or environmental factors where shared models suffice.

The user initially imagined each transistor as an input -> output voltage function with VCC, then recognized that not all components or ICs can be represented by voltage-only or feed-forward functions. The goal is to preserve intuitive descriptions while supporting bidirectional electrical connections, load-dependent voltage, feedback, stored state, timing, and multi-domain effects.

## User's conceptual breakthrough
Three kinds of expressions:
1. **Purpose/intention expressions:** What the designer wants, readable like programming.
2. **Consequence expressions:** Mathematical equations describing physical cause and response; ideally generated/hidden and selectable by fidelity.
3. **Constraint expressions:** Requirements, operating bounds, and restrictions on realizations/solutions.

User prefers combining (1) and (3): **“do this, subject to these constraints”**, while automatically lowering to (2). They want a universal intention-description level applicable to all circuit classes. Need an explicit, principled way to derive lower-level models/implementations from intentions and constraints, and optionally infer higher-level intent/contracts from physical models (recognition is generally ambiguous).

## Recommended architecture
Core separation:

**Intent + constraints + context + observation -> candidate realization + behavioral model -> predicted consequences + diagnostics.**

- **Intent:** Desired external behavior, e.g. `invert(A) -> Y`, `RESULT = A + B`, `maintain(VOUT, 1.2V)`, `lock(CLK_OUT, CLK_IN, ratio=8)`.
- **Constraints:** Hard physical/functional requirements and design limits (supply, timing, load, power, technology, area, stability, operating range).
- **Context/assumptions:** Environment, loads, supply, temperature, valid input range, topology already provided, initial conditions.
- **Observation:** Which views/properties matter: logic, voltage, current, edge events, phase, frequency, average/instantaneous power, stability.
- **Realization:** Concrete circuit topology or abstract behavioral implementation satisfying the contract; may be synthesized, selected from library, or supplied explicitly.
- **Consequence model:** Algebraic equations, differential equations, discrete-event state transitions, and hybrid systems, derived from realization and chosen fidelity.
- **Diagnostics:** Unsatisfiable, underconstrained, out-of-contract, ambiguous, or unsupported modeling requests; never silently invent physical precision.

**Crucial:** An intention is a specification, not necessarily an executable physical law. `maintain(VOUT,1.2V)` cannot uniquely predict behavior without a realization, source/load, control law, and constraints. A compiler should either choose/propose a realization, return a family of admissible solutions, or report insufficient information.

## Proposed language primitives
Statement families:
- `do` / declarative assignment: desired behavior.
- `require` / `subject to`: hard requirements and constraints.
- `assume`: conditions under which guarantees hold.
- `connect`: physical/electrical topology; connections need not be directed.
- `realize` / `using`: choose implementation or technology.
- `observe`: requested outputs/views.
- `when`: discrete event trigger.
- `analyze`: simulation mode and fidelity.

Seven useful *semantic* intention verbs (not necessarily runtime opcodes):
- `transform` (invert, amplify, ADC/DAC)
- `combine` (logic, arithmetic, mixing)
- `route` (switch, mux, buses)
- `store` (flip-flop, SRAM, capacitors, inductors)
- `compare` (voltage/phase comparator)
- `regulate` (LDO, PLL, AGC)
- `generate` (oscillator, clock, PWM)

Underlying model should distinguish **functional contracts** from **physical realization** and support multiple implementations of the same contract (Boolean, behavioral analog, transistor network, standard cell). Never conflate a requested bound with a measured property.

## Mathematical substrate (hidden under language)
For physical circuits, use terminal relations plus state, not just `Vout=f(Vin,VCC)`:

```
0 = F(z, x, u, t)          # algebraic constraints / KCL / device equations
x_dot = G(z, x, u, t)     # charge/flux/continuous dynamics
x_next = H(x, u, event)   # discrete state transitions
observables = O(z, x, u, t)
```

`z` includes node voltages and auxiliary currents; `x` includes charges, filter states, stored bits, oscillator phase; `u` external controls. Electrical node connectivity imposes Kirchhoff current balance. Some circuits require simultaneous nonlinear solving and differential-algebraic integration; feedback cannot always be evaluated left-to-right.

Small physical primitive library:
- resistor/conductance: `I = G(Va - Vb)`
- nonlinear controlled terminal current: `I = f(terminal voltages, parameters)` (MOSFETs, mirrors)
- ideal/controlled voltage source: algebraic voltage constraint plus unknown source current
- capacitor/charge storage: `I = dQ/dt`
- inductor/flux storage: `V = dPhi/dt`
- event/state update, threshold, delay, clipping, integration
- optional multi-domain thermal/mechanical/optical/magnetic relations

NMOS first approximation: `Vov=Vgs-Vt`, `beta=mu*Cox*W/L`; cutoff `Id=0` for `Vov<=0`; triode `Id=beta*(Vov*Vds - Vds^2/2)` for `0<Vds<Vov`; saturation `Id=beta*Vov^2/2` for `Vds>=Vov`. PMOS uses source-referenced polarities. This is a basic long-channel model, not adequate for all advanced CMOS or weak inversion. Node voltage is solved from surrounding currents and loads.

Behavioral gate example: `Vtarget=f(inputs, rails)` plus `Cload*dVout/dt=Gdrive*(Vtarget-Vout)-Iload`. For CMOS inverter more physical: `Cload*dVout/dt=Ip(Vin,Vout)-In(Vin,Vout)-Iload`. Boolean endpoint expressions (with normalized `a,b`): NOT `1-a`, NAND `1-ab`, NOR `(1-a)(1-b)`, AND `ab`, OR `a+b-ab`, XOR `a+b-2ab`. These are **not** true analog transfer characteristics. Smooth sigmoid approximations can be used where appropriate.

Op-amp behavioral model: `tau*dVa/dt=A0*(Vplus-Vminus)-Va`, `Vtarget=clip(Va,VOL,VOH)`, `Io=(Vtarget-Vout)/Ro`; optionally slew-limit `dVa/dt`. For dominant-pole model, `GBW ~ A0/(2*pi*tau)`. Needs feedback/load solving, not hardcoded closed-loop gains.

Storage: DFF `Q_next=D` on active clock edge, otherwise hold; SRAM cross-coupled inverter state voltages and access currents; DRAM `Q=C*V`, `dQ/dt=Iaccess-Ileak` (leakage is essential for refresh behavior). Large ROM/RAM can be abstract arrays with timing and power models.

Feedback: linear closed-loop `Y/U=F/(1+F*beta)` under applicable LTI assumptions; general feedback uses nonlinear DAE and states. PLL minimal model: phase error `wrap(phi_ref-phi_vco/N)`, loop filter `tau_f*dVf/dt+Vf=Kd*sin(error)`, VCO `dphi_vco/dt=omega0+Kv*Vf`; clock output may be waveform or event view. Real charge-pump PLL needs event/pulse dynamics. Oscillator phase model alone doesn't establish startup or sustained oscillation.

## Important semantic refinements
1. **Contracts/assume-guarantee:** A block guarantees behavior only when supply, input range, loading, timing, etc. meet assumptions.
2. **Domain and views:** An electrical node may expose voltage, logic, edge, phase, and frequency views. Conversions require explicit assumptions; phase/logic aren't interchangeable with analog voltage.
3. **Fidelity per observable/subsystem:** Functional Boolean, behavioral analog, transistor DC/transient, event-driven, phase noise, average power. Use mixed-fidelity simulation and refine only where observations need it.
4. **Unspecified behavior states:** `unknown` (actual value exists but not determined), `unconstrained` (many allowed values), `invalid` (assumptions violated). Also consider unsatisfiable requirements.
5. **Power rails are actual network nodes:** Rail droop, current limits, regulator control, startup/brownout, and supply dependence may be relevant; do not treat VDD as a magical global constant unless explicitly idealized.
6. **Shared parasitic/environmental models:** Allow global/default technology and interconnect models, but retain locally important nonidealities (e.g., DRAM leakage, oscillator noise, PLL jitter, stability, transistor capacitance when switching speed matters).
7. **Equivalence is relative to observations and operating envelope:** Two implementations can match logically yet differ in transient, power, or out-of-range behavior.
8. **No general unique inverse:** Deriving intent from equations/topology requires inference/recognition and may yield multiple possible intentions; deriving physics from intent is synthesis/model selection, not a deterministic mathematical transformation without more context.

## Illustrative DSL sketches

```text
component Inverter {
  ports: A, Y, VDD, VSS: electrical
  do: invert(A) -> Y
  assume: VDD within [1.1V, 1.3V]
  require: delay(Y) <= 50ps; output_drive >= 2mA
  realize: technology=CMOS
  observe: voltage(Y), current(VDD), logic(Y)
}
```

```text
component Adder32 {
  input A, B: bits[32]
  output RESULT: bits[32]
  do: RESULT = A + B
  require: delay(RESULT) <= 2ns; area <= 1000um2
  realize: technology=CMOS
}
```

```text
component Register {
  do: store(D) -> Q
  when: rising_edge(CLK)
  require: retention=while_powered; setup_time <= 100ps
}
```

```text
component PLL {
  do: synchronize_phase(OUT, REF); multiply_frequency(REF, factor=8) -> OUT
  require: lock_time <= 10us; jitter_rms <= 2ps
}
```

```text
component Regulator {
  do: maintain(VOUT, target=1.2V)
  assume: VIN within [2V,5V]; load_current within [0mA,100mA]
  require: ripple(VOUT) <= 5mV; stable_for(load_range)
}
```

```text
resistor R1 between A and B { resistance=10kOhm }
mosfet M1 { gate=G; drain=D; source=S; type=NMOS }
```

Compact syntax possibility:

```text
OUT = invert(IN) subject to CMOS, VDD=1.2V, delay<50ps
VOUT = maintain(1.2V) subject to VIN=3.3V, load<=100mA
CLK_OUT = lock(CLK_IN * 8) subject to jitter<2ps
DATA_OUT = remember(DATA_IN, on=rising(CLK)) subject to retention=powered
```

Caution: distinguish `=` as functional definition, physical equality, goal/target, and measurement; do not give it contradictory semantics. Could use explicit `do`, `target`, `connect`, `require` internally even if surface syntax is concise.

## Compiler/solver pipeline proposal
1. Parse DSL into typed semantic IR (intent, ports/domains, assumptions, constraints, observations).
2. Check units/dimensions, typing, supply/ground reference, operating envelopes, and contract compatibility.
3. Resolve or synthesize realizations (behavioral templates, known cell libraries, transistor-level topology). Detect missing information and unsatisfiable constraints.
4. Choose models/fidelity based on observations and user-specified precision.
5. Lower to electrical/multi-domain equations and discrete events; generate KCL/DAE/state transitions automatically.
6. Solve DC operating points, transient behavior, events, feedback, and relevant performance constraints.
7. Return results with validity ranges, uncertainty, diagnostics, and provenance (which assumptions/models were used).
8. Optionally refine local models or realizations if constraints fail.

## Open design questions for Codex work
- Define a small rigorous grammar and typed IR for intent vs constraint vs assumption vs topology vs observation.
- Decide which statements are specifications versus actual connected component behaviors; define executable semantics of `maintain`, `store`, `route`, etc.
- Define power and ground as ports and reference frames, plus signal domains and event conversion.
- Define satisfiability and synthesis search: when is a design underspecified, infeasible, or simply missing a chosen implementation?
- Define observable-relative equivalence and fidelity/error contracts.
- Prototype a minimal engine: resistor, ideal source, capacitor, NMOS/PMOS, inverter, DFF, op-amp behavioral model, feedback regulator. Show automatic lowering and mixed algebraic/dynamic/event solving.
- Keep surface syntax readable for programmers while ensuring bidirectional physics and feedback are correctly handled.

## User's latest request
Compact all conversation context and questions/answers into a transferable file for Codex. This file is that handoff. The previous assistant response was truncated while discussing compact `=` syntax; no additional resolved design decision was made after that.

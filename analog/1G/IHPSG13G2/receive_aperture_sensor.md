# Open-loop sampling-edge sensor candidate

`receive_aperture_sensor.hpp` is an unqualified prerequisite for a delivered-edge
clock loop. It neither generates a clock nor demonstrates receiver aperture
closure. Its inherited Vdd is the core supply; SAMPLE_N accepts the external
high-voltage replica complement through a finite resistor divider. REF and
CLEAR are core signals. All timing and reset behavior comes from electrical
components.

A 7 kΩ/4 kΩ divider attenuates SAMPLE_N. The reference arm has a 2.545 kΩ series
resistor, matching the divider's nominal Thevenin resistance before source
impedance. Both inputs retain 5 fF storage and two core CMOS buffers. Their
50 Ω bench sources are real circuit elements; the feedback source supplies
about 0.3 mA while high. The final buffers drive physical flip-flops, which
in turn drive the actual existing phase/frequency detector. No lumped load
stands in for those logic gates.

The feedback flip-flop divides nominal 250 MHz sampling edges by two. The
reference flip-flop captures one at 125 MHz and clears through a physical
falling-edge pulse generator. The 1 kΩ/0.5 pF delay gives that clear a finite
width, allowing release and master-latch preparation before the next rising
edge. Global CLEAR overrides the reference pulse and resets feedback and PFD.
The reference pulse generator additionally loads its arm; any resulting
mismatch is measured, not assumed to cancel.

The exported candidate contains 278 MOS channels and 134 explicit capacitors,
including both flip-flops and the real 134-MOS PFD. The initial process binding
uses LV N10/P20 µm logic and N50/P100 µm clear devices, all at L=0.13 µm with
legal fingers. These are candidate dimensions, not a process qualification or
GDS area claim. The mapper locks the reviewed transistor/resistor export and
all capacitor/observation wiring; ten negative binding mutations are rejected.

The isolated bench uses discharged storage, a finite core supply, 400 ps
reference edges and finite source resistance. Cases are nominal, ±300 ps
feedback phase, 100 ps feedback edges, wrong frequency ratio, missing feedback,
clear/restart, and independent low/high core and high/low input supplies.
The first native nominal diagnostic runs under
`out/receive-aperture-sensor/v1/native_nominal`; pending cases are not results.
No large corner sweep is started before basic topology evidence is reviewed.

The checker compares every output edge to its declared source-command
half-rail time. It reports both absolute delivery delays and their mismatch;
it never aligns trajectories after simulation. The initial 500 ps arm delivery
and 50 ps mismatch bounds are diagnostic allocations only. In particular,
50 ps has not been shown to fit the balanced gain-8 receiver's remaining
acquisition margin. Reset, ratio, PFD polarity, missing-feedback behavior and
observed input/core voltage limits are separate checks. A separate source
checker reconstructs each imposed voltage through its finite source resistor
and checks the exact declared PWL schedule within 500 µV.

Even a passing sensor only measures the receiver threshold crossing of
CLOCK_N. It does not establish when both transmission-gate channels cease
tracking. A later physical loop must keep the original reference-relative
20 mV held-voltage limits, real TRACK and CLOCK_N skew, all thirteen actual
clock loads, replica mismatch, signal slope/level and charge injection.
Existing PLL generic proofs do not qualify an IHP loop. A proposed 500 MHz
oscillator and divide-by-two for equal half cycles remains a separate tuning,
startup and process evidence requirement.

# Data-driven PLL clock recovery

[pll_cdr.hpp](pll_cdr.hpp) defines three constituent ICs and the composed CDR:

- `hogge_detector`: a rising-edge sample, a falling-edge delayed sample, and two
  explicit `sim_xor` networks produce variable-width ERROR and half-cycle REFERENCE pulses.
- `vco_10g`: three current-starved MOS inverter stages oscillate near twice the line
  rate; a stored divide-by-two produces equal clock half cycles. Each ring stage
  declares 1.02 fF of storage. Control changes the actual discharge current.
- `pll_cdr`: a differential limiter, that detector, a VCCS charge-pump model, a
  200 fF/5 kOhm lead-lag filter, an additional 1 kOhm/100 fF ripple filter and the VCO.
  Explicit diode paths bound its control voltage near 0.5–0.95 V.
- [lock_monitor](lock_monitor.hpp): filtered pulse imbalance and transition activity
  qualify lock. It is separate from the oscillator and has its own good/bad tests.

No input supplies a reference clock. P/N are the received electrical waveform;
VCM is a bias voltage. ERROR pulses depend on where real data transitions fall
relative to the sampled data. REFERENCE pulses are generated from the recovered
clock's own half cycles. Their imbalance charges the loop filter and shifts the
VCO phase. The [Hogge architecture reference](https://people.engr.tamu.edu/spalermo/ecen620/lecture12_ee620_cdrs.pdf)
explains the phase-detector structure and its nonidealities.

The nominal oscillator range is configured by its devices. Initial ring charge
and a 0.7 V filter state are explicit experiment/model conditions. This PLL starts
near the known 10GBASE-R rate; it has no arbitrary-rate frequency-search controller.
Its low-power/power and jitter properties have not been validated in a silicon PDK.

Lock qualification averages ERROR minus REFERENCE over 8 UI, rectifies it, then
filters its magnitude over 4 UI. A second 4 UI filter measures reference-pulse
activity. Comparators use VCM/11 (about 54.5 mV) as a threshold: small sustained
imbalance and sufficient activity enable a final 4 UI qualification filter. LOCK
is an electrical confidence level, interpreted high above 1 V in the tests. It is
not IEEE PCS block lock, a universal harmonic-lock detector or proof of a BER floor.

Without transitions, the VCO keeps running from stored control state while LOCK
falls as activity decays. `cdr_gap` tests a 66-UI constant run and subsequent
reacquisition within an explicit 80 UI budget after transitions resume. During
the transition-free interval, holdover phase is unobservable and is not treated as
qualified sampling phase. Other cases test initial phase offset, both signs of 100 ppm rate
offset, bounded timing/voltage disturbance, steady frequency tracking and constant
data. The same checker evaluates each solver independently. Clock comparison
uses matching recovered edges on the known symbol grid, rather than treating a
small edge displacement as a full-rail DC error. Modules are in [generated/](generated/).

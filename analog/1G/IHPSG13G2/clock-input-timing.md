# Physical clock input contract

The routed receiver SDC specifies a 100 ps transition at `clk`. Passing clock
pulse count, width and propagation delay does not establish this transition
limit. The IHP libraries measure slew between 20% and 80% of supply.

`processing_clock_slew.py` reads that SDC and the actual Liberty thresholds,
then measures every admitted edge in the retained extracted-gate PVT traces.
It verifies the original trace/deck hashes and the reference's source hashes.
A repeated experiment changes only the explicitly requested output load; the
extracted circuit, controller output cells, supply, temperature and stimulus
remain bound to the reference. All original pulse checks still apply.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/processing_clock_slew.py \
    --reference build/clock-integration/clock-gate-placed-controller-pex/checks.json \
    --constraints build/clock-integration/trellis-compressor-revision/global-unbuffer-repair-200/candidate.sdc \
    --output build/clock-slew-recheck
```

The retained 200 fF experiment fails the 100 ps input constraint: TT rise is
101.784 ps; SS rise/fall are 141.066/106.913 ps; FF rise/fall are
80.155/65.429 ps. Its original pulse-only pass remains valid within that scope.
It cannot establish the receiver's input slew at a 200 fF load.

The explicit `--load-ff 100` experiment also fails: SS rise is 112.491 ps
(fall 81.963 ps). TT rise/fall are 79.403/59.744 ps and FF are 61.961/47.154 ps.
All three cases pass the original pulse checks. These completed measurements
are under `build/clock-integration/clock-gate-input-slew-100/`; a 100 fF load
budget therefore does not fix the interface.

`processing_clock_buffer.py` screens an actual `sg13g2_buf_16` after the
conserved extracted gate. It preserves all four transistor terminals, total
widths and finger counts from the bound PDK CDL and uses the same PSP models,
controller output cells and complete pulse stimulus. The proposed connection
has an explicit 30 ohm / 20 fF pi wire budget and a 100 fF final load. Both
the 100 ps slew and existing 400 ps delay limit still apply. Under
`build/clock-integration/clock-gate-buffer16-100/`, the SS simulation fails:
output rise/fall are 53.552/49.277 ps, but rising-edge delay is 427.627 ps.
The stage stops before the remaining corners and the buffer is not adopted.

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/processing_clock_buffer.py \
    --reference build/clock-integration/clock-gate-placed-controller-pex/checks.json \
    --constraints build/clock-integration/trellis-compressor-revision/global-unbuffer-repair-200/candidate.sdc \
    --output build/clock-buffer-candidate --load-ff 100 --conditions mos_ss mos_tt mos_ff
```

`receiver_clock_load.py` reads a completed receiver route's bound SPEF and
matching slow/fast Liberty models. It accounts separately for ground wire,
coupling and maximum characterized input pin capacitance, including antenna
diodes. Its opposite-equal-swing estimate doubles coupling capacitance; this
is a load bound, not a transient noise analysis. Streaming retains the name
map and clock net without materializing the entire receiver SPEF.

```sh
python3 1G/IHPSG13G2/receiver_clock_load.py \
    --route build/clock-integration/trellis-compressor-revision/early-route-200 \
    --budget-ff 55 --output build/receiver-clock-load.json
ctest --test-dir build/behavior --output-on-failure \
    -R 'ihp_processing_clock_gate.slew_guards|ihp_receiver.clock_load_guards|ihp_adc_lane.interface_load_guards'
```

The original extracted gate passes all three corners at `--load-ff 55`, with
the complete pulse stimulus and unchanged slew/delay limits. Results are under
`build/clock-integration/clock-gate-input-slew-55/`:

| Corner | Maximum rise (ps) | Maximum fall (ps) |
| --- | ---: | ---: |
| TT | 68.940 | 50.394 |
| SS | 98.893 | 70.426 |
| FF | 53.807 | 38.886 |

SS maximum edge delay is 300.074 ps. Its rise has only 1.107 ps margin to the
input constraint, so the physical connection must stay within the tested load
and pass a connected waveform check; this screen does not justify arbitrary
inter-macro wiring or a full-clock claim.

The older `receiver-margin-route-v5` root measured 33.887 fF. The newer
`early-route-200-jemalloc` root measures 47.200 fF, leaving **7.800 fF** for
external wiring within the passing 55 fF load budget. Its separate measurement
is saved as `trellis-compressor-revision/clock-root-budget-55.json`. This early
route still fails receiver setup and routing checks; a replacement receiver
must be measured again. Routed inter-macro RC, driver/receiver interaction,
oscillator waveform, reset delivery, post-fill coupling and complete-chip timing
remain required.

For the powered-branch receiver revision, the measurement can wait for the
active early-route probe and report both input loads:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/receiver_clock_load.py \
    --route build/clock-integration/receiver-onehot-revision/early-route-powered-branches \
    --wait-for-probe-seconds 18000 --budget-ff 55 --reset-budget-ff 80 \
    --output build/clock-integration/receiver-onehot-revision/powered-branches-input-loads.json
```

The clock default is now 55 fF, matching the passing extracted gate screen.
The optional reset budget uses the reset controller's tested output load;
both budgets include receiver wire, coupling and input-pin capacitance before
external inter-macro wiring. Large SPEF files are hashed in bounded chunks.
Waiting holds no EDA memory slot, checks producer liveness, and rejects changed
measurement code. The final route must be measured again: the early probe
does not establish complete routing, connected waveform integrity or timing.

The current receiver netlist uses synchronous reset: its 7,803 flip-flop
asynchronous RESET_B pins are tied inactive, and its reset data path has a
0.4 ns input delay and 0.1 ns input transition constraint. Do not interpret
the tied pins as evidence that reset is missing. Controller/domain release,
physical reset loading and connected timing still require integration checks.

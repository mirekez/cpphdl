# Timing model

125 MBd gives an 8 ns nominal symbol period. The electrical benches provide a
1.2 V boundary clock. It captures the previous transmit symbol before the new
command changes at 0.15 UI; the command ramp ends at 0.19 UI. Receiver scoring
samples at 0.8 UI. These phases are declared, not fitted to each output.

`gardner_timing` is a sampled second-order timing-loop reference. The caller
interpolates actual receive samples at the predicted symbol time and half a
period before and after it. The error is `(early - late) * middle`, followed by
bounded proportional phase correction and integral period correction. Expected
symbols do not enter the loop. Unit tests start with phase error, check source
periods corresponding to both signs of 100 ppm, and exercise no-data frequency
holdover. Activity is recorded; it is not advertised as a PHY lock signal.

The sampled loop has not been connected to the full electrical system's clock
network. Thus the link tests exercise supplied timing, not autonomous slave PHY
clock recovery or master/slave negotiation. That integration must include the
receiver filter delay, training protocol, loss-of-signal criteria and lock/loss
qualification rather than reusing an NRZ threshold as a PAM5 lock proof.

The native backend uses backward Euler and analytic Jacobians. The default
regular step is UI/128; SPICE uses second-order Gear with maximum UI/256.
PWL breakpoints are retained and failed Newton solves may subdivide to UI/65536.
Refined link cases halve both regular/maximal steps. Same-equation trajectory
agreement and refinement apply to the selected prototype laws, not to silicon
timing, metastability or a jitter-compliance mask.

These experiment decks set `spice_capacitance_threshold_F = 1e-18`: capacitors
are stamped once by the ngspice host with their original terminals, values and
initial voltages. All other device laws remain in the compiled Verilog-A model.
Flattening all charge derivatives into one OSDI instance caused ngspice 47 to
abort with "timestep too small" on channel, DFE and four-pair cases. Moving the
unchanged capacitor equations to native SPICE stamps resolves those failures;
the checker rejects aborted/truncated traces even when ngspice exits with zero.
The reusable files in `generated/` retain every Verilog-A `ddt` capacitor and
are compile-checked. The transient evidence is specifically for the exported
Verilog-A/SPICE partition, not a successful run of every unpartitioned file.

# Buffered PAM5 receiver with a faster SAR reference

[`receive_frontend<Count>`](receive_frontend.hpp) is an `IC` derived from the
common [PAM5 receiver](../receive_frontend.md). It retains 2 pF sampling storage
and changes the SAR reference ladder from 10 kΩ to 1 kΩ. The sampler and DAC
both see the achieved input and gate charge of the following amplifier.
Reducing ladder impedance gives that charge a faster path before comparator
evaluation, at the cost of greater reference current. Each comparator drives a
[`buffered_nand_latch`](../../basic/buffered_nand_latch.md): two small inverter
stages per command isolate the regenerative decision from state-dependent
NAND gate charge. Both stages draw current from the shared supply.

```cpp
ihp_sg13g2::receive_frontend<16> receiver;
receiver.PLUS = hybrid.OUTP;
receiver.MINUS = hybrid.OUTN;
receiver.Vdd = analog_supply;
receiver.Vlogic = core_supply;
receiver.Vss = ground;
// Connect evaluation, clear and each slice's trial and track ports.
receiver.elaborate();
```

Its [precision subclass](precision_receive_frontend.md) changes storage to
4 pF through the same interface. The initial 2 pF choice reduces capacitor area
and shared-buffer loading. Selection is based on acquisition, hold and closed
conversion tests, including connected loads and supplies.

The generic C++ and generated `OneGIHPReceiveFrontend.va` use the framework MOS
law and explicit charge. [`receive_frontend_process.py`](receive_frontend_process.py)
binds their recognized device laws to four-terminal IHP PSP devices. The input
pair has 50 µm devices and the shared follower 400 µm, at 0.45 µm channel length.
Sampling switches use 100 µm NMOS / 200 µm PMOS; current dummy widths are
50/100 µm. This is a sizing candidate under connected-circuit qualification.
These total widths are folded into valid fingers. Smaller switches reduce
injection and physical gate load; their longer charging time must still pass
the connected-circuit tests. The binder contains the complete DAC, latch,
comparator and level-down sizing table. Body and intrinsic charge effects
remain in PSP; declared resistors and capacitors retain their values.

The acquisition fixture checks one, four and sixteen slices, loading,
overload rejection and timestep refinement. Process tests first measure their
own five-level DC transfer, then check acquisition and hold against that
transfer. Nominal rails are 3.3/1.2 V with finite source resistance and
decoupling. PSP internal charge is bias-initialized while honoring the declared
external initial voltages. These experiments include precharge, rather than
cold-start qualification. The extended sweep varies five process corners and
three temperatures; it does not sweep supply voltage.

The closed-conversion fixture adds the actual C++RTL SAR controller. Generic
C++ and Verilog-A use their own solved core-domain decisions and must pass
the same absolute checker separately. Frontend settling at the aperture,
sampler acquisition relative to that achieved input, and subsequent hold drift
have separate 20 mV budgets. Raw conversion must remain within two counts of
the DC target, and normalized codes must match exact integer arithmetic.
Cross-solver comparison allows one raw or two normalized counts.

[`conversion_process.py`](conversion_process.py) provides the corresponding
PSP binding. Its 50 Ω, 100 ps host control adapters return current to the real
rails and are explicitly testbench circuitry. PSP pause/resume failed at source
breakpoints and after control updates. This backend instead recomputes each
causal transient prefix from the specified initial charges and its own complete
control history. It reads the required electrical decisions from that prefix,
then runs the final complete trajectory. This is slower than continuous
co-simulation and does not substitute another solver's decisions.

```sh
cmake --build analog/build --target analog_1g_ihp_receive_process \
  analog_1g_ihp_receive_process_conversion
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_process[.]'
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_process_conversion_'
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_frontend[.](nominal|process_temperature)$'
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_receive_conversion_.*[.]psp$'
```

The previous 60 µm dummy/1 kΩ/buffered-latch revision passed nominal PSP acquisition
for one, four and sixteen slices. Maximum acquisition errors are 12.707,
18.080 and 18.001 mV; maximum hold drift is 2.503 mV. Its first closed PSP
conversion passed with code 126 against target 125. Eight causal feedback
reads agree with the final trajectory within 0.416 nV. Generic closed
conversion passes for one, four and sixteen slices, including 32 consecutive
conversions with slice reuse in the sixteen-slice case. Each solver passes
the absolute checks and their comparison passes.
That revision's extended PSP sweep fails sixteen-slice acquisition at TT and 125 °C:
24.519 mV exceeds the retained 20 mV limit, although amplifier/buffer delivery
and hold pass. Its closed four-slice PSP test also fails, with up to four counts
of error, 25.473 mV frontend aperture error and 24.669 mV sampler acquisition
error. The final trajectory reproduces its causal feedback reads, so replay
consistency does not imply a passing physical circuit.

Reducing the dummy NMOS to 40 µm improves the hot sixteen-slice diagnostic to
12.697 mV, but fails nominal one-slice acquisition at 24.126 mV. The current
50 µm choice passes that nominal one-slice check at 8.723 mV, nominal four-slice
at 7.457 mV, and the hot sixteen-slice diagnostic at 14.150 mV. Full nominal,
process-corner and closed-conversion qualification of this choice remains open.
The 2 pF storage is retained during these sizing comparisons.
The previous 2 kΩ receiver passed nominal PSP acquisition for all three slice
counts but failed closed conversion by three counts. Isolation and reference
settling diagnostics then exposed the NAND input load changing the comparator
decision. Small inverter buffers with a 1 kΩ reference restored its first
MSB decision within the original two-nanosecond window. The complete
closed-conversion loop must still qualify. A previous
large-switch/10 kΩ revision failed hot acquisition and closed conversion;
those failures are retained. Voltage corners, mismatch/noise, continuous
physical clock and control translation, transistor layout and extracted
whole-PHY operation remain required. A generated model or standalone digital
GDS does not establish those properties.

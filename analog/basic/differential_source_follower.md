# Differential common-mode follower

`differential_source_follower : IC` uses two
[`nmos_source_follower`](nmos_source_follower.md) instances. `PLUS` follows to
`OUTP`, and `MINUS` follows to `OUTN`. Both draw bias current from `Vdd` and
return it through their source resistors to `Vss`. A connected load changes
the achieved output and the common supply; there is no ideal voltage offset.

The purpose is to lower input common mode before a differential amplifier.
The source voltage follows the gate with a voltage loss set by the MOS law,
bias resistance, body effect in a process model, and load. Differential gain
is consequently below unity and can vary with common mode. This is an input
interface circuit, rather than a rail-to-rail amplifier.

Default generic devices use beta 0.05 A/V², threshold 0.7 V, a 500 Ω source
return, and explicit 20/5/20 fF gate-source/gate-drain/output charge per side.
The initial gate, source and supply voltages are 2.8, 1.65 and 3.3 V; these
specify precharge and must agree with all connected capacitor initial states.
They do not establish a cold-start guarantee.

The standalone `analog.basic.differential_source_follower` CTest passes seven
cases in both solvers: three supply voltages, capacitive and resistive loading,
deliberate overload and timestep refinement. Maximum solver difference is
1.838 mV, and the largest refinement difference is 0.911 mV. The overload must
fail delivery; settled DC, polarity and gain guarantees apply to rated loads.

The [MDI receive test](../1G/IHPSG13G2/tests/mdi_receive.py) checks the pair
while it drives the actual amplifier input charge and shared receive supply.
It independently solves each source's DC KCL before calculating the expected
amplifier and sampled output. The one/four-slice and refinement experiment
passes, with maximum refinement difference 6.849 mV. The C++ and Verilog-A flows each require the
same delivery, headroom and acquisition limits before comparison. Process
binding and physical layout qualification of this composition remain separate.

# Recovered symbols between ADC and PCS clocks

`ReceiveSymbolCrossing` transfers each recovered four-lane sample bundle from
the ADC clock domain to a separate PCS clock domain. It preserves the 32 sample
bits, 16-bit sample tag and clipping flag together in an eight-entry FIFO.
`valid_in` marks a bundle on an `adc_clk` edge; `valid_out` marks a delivered
bundle on a `pcs_clk` edge. Idle destination cycles do not advance the PCS
symbol pipeline. Connect `valid_out` to `PCSReceiver.symbol_enable_in`.

This provides a possible clock boundary after `ReceiveSampleRecovery`. It is
not yet connected into a physically qualified receiver. The current PCS does
not meet a 4 ns clock at the slow IHP corner. An isolated retiming candidate
passes preliminary cell timing at 7 ns, with 0.61 ns setup slack; routing and
the clock generator remain unqualified. That separate feasibility result
does not change the failed 4 ns requirement.

The unit fixture uses a 4 ns ADC clock and a 7 ns PCS clock. Its highest input
rate is 0.522 bundles per ADC edge (130.5 million bundles/s), covering the
resampler's acquisition actuator limit as well as nominal 125 MBd traffic.
These rates describe the tested interface envelope, not permission to sample
the analog input more slowly. A clock plan derived from one reference must
also implement the PHY master/slave transmit timing requirements.

Four-bit Gray write/read pointers cross through two registers each. A
separate two-register path carries a sticky source fault. The payload memory
is written only by `adc_clk` and read only after the destination sees a
published pointer. Output data and valid are registered in `pcs_clk`.

Overflow or `fault_in` stops source writes and latches `source_fault_out`.
After synchronization, `fault_out` latches and suppresses further output.
Already published words can reach the destination before the fault does;
the downstream link supervisor must invalidate the affected receive frame.
There is no backpressure port: a stopped or underspeed destination eventually
causes an explicit overflow instead of overwriting unread words.

Both clock domains must observe reset before traffic resumes. Reset release
and clock availability are responsibilities of the enclosing clock/reset
controller. Resetting one side of a live FIFO is outside this interface's
contract. Empty cycles never manufacture an output symbol.

`tests/receive_symbol_crossing.cpp` checks 70 phase/rate combinations, tag wrap,
sample and clipping-bit integrity, drain, stopped-clock overflow, external
faults, a fault during an active transfer, and joint reset. An independent
queue checks each delivered bundle. The Python runner separately requires
the native and generated RTL checks to pass, then compares all 236,950 event
states exactly, including 70,047 delivered bundles. It also checks that the
export preserves both clock processes and all six synchronizer attributes.

Functional simulation does not model metastability. Physical implementation
must preserve the synchronizers, constrain Gray-bus skew and launch-to-first-
stage delay, and check the payload memory and reset paths. Neither a blanket
false path nor the functional unit test establishes routed CDC signoff.

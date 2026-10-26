# Receive-to-transmit frame crossing

`ReceiveTransmitCrossing` transfers GMII frames from a receive processing clock
to another PHY's transmit symbol clock. `receive_in[7:0]` carries data, bit 8
marks a frame byte and bit 9 carries its error flag. `valid_in` admits one
recovered symbol; processing-clock bubbles admit nothing.

`FrameCrossingFIFO<AddressBits>` supplies the shared implementation. The
standalone `ReceiveTransmitCrossing` has 64 entries; the repeater uses a
separate `SynchronousFrameFIFO` with 4096 entries and a registered SRAM read. The buffer stores frame bytes and an explicit end marker. Two-stage
Gray-pointer synchronizers keep the source and destination independent. The
transmitter normally waits for eight buffered words before starting. A small
waiting counter also releases short frames. Once a frame starts, the buffer
delivers consecutive bytes and preserves their error flags. An end marker
starts at least twelve idle symbol periods before the next frame.

An empty buffer during a frame is an underrun, not an end marker. It produces
one explicit `TX_EN | TX_ER` symbol and a sticky destination fault. Overflow
or `fault_in` becomes a sticky source fault and crosses to the destination;
an active frame is explicitly aborted. Both clock domains must observe reset
before restarting. Neither side may reset its pointer independently.
`clear_rx_in` and `clear_tx_in` provide domain-local clear while a paired reset
coordinator exchanges release acknowledgements. Tie both low in standalone
tests that supply a joint reset directly. The two-port
[forwarding composition](repeater_forwarding.md) connects these clears and
the independent physical clocks.

`hold_tx_in` inhibits only new frames. `idle_out` reports the end of the
required transmit gap. `used_out` exposes conservative source-domain occupancy
using the synchronized read pointer. Tie hold low when testing the standalone
FIFO without a flow-control client.

A finite buffer cannot provide indefinitely lossless forwarding from a faster
source into a slower destination. This module detects exhausted capacity; a
system must supply a rate policy and reset/recovery mechanism. The
[PAUSE composition](flow_control.md) supplies the selected flow-control policy.
Physical implementation must also constrain Gray-bus delay/skew and protect
the synchronizer stages. Functional RTL equivalence does not verify metastability.

```sh
cmake --build analog/build --target analog_1g_receive_transmit_crossing
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]receive_transmit_crossing[.]rtl$'
```

The regression independently checks frame bytes, boundaries and interframe
gaps across 36 clock-rate/phase combinations, including runt and jumbo stress
frames. It also forces overflow, underrun, an upstream failure and joint reset.
Native and RTL event trajectories must match exactly.

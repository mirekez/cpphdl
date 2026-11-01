# Forwarding between the two physical PHYs

`RepeaterForwarding` connects A's recovered GMII stream to B's transmit domain
and B's stream to A's. Receive processing can run at 132–136 MHz with valid
strobes; each transmitter consumes consecutive frame bytes at its own symbol
clock. The sole board reference is used by the physical clock generators.
The four inputs here are those generators' delivered clocks, not four board
references.

| Clock | State advanced |
| --- | --- |
| `rx_clk` | Port A receive side |
| `tx_clk` | Port B transmit side |
| `b_rx_clk` | Port B receive side |
| `a_tx_clk` | Port A transmit side |

Each `QualifiedFrameCrossing` contains the 4096-word synchronous-read frame FIFO and a
`FrameCrossingReset`. Losing either endpoint's readiness, disabling forwarding,
or asserting reset immediately holds that direction's output quiet. Reset
release needs two local edges and two edges sampling the peer's release.
The FIFO's domain-local clear inputs keep its pointers reset throughout that
exchange. A clock that stops during reset therefore cannot leave stale pointer
state available to its running peer. The other forwarding direction has its
own coordinator and FIFO.

`valid_a_in` and `valid_b_in` mark recovered symbol observations, including
idle observations. The first idle after a frame creates its end marker.
Frame bytes, in-frame errors, boundaries and at least twelve transmit idle
symbols between frames are preserved. An upstream fault or exhausted FIFO
aborts an active frame with `TX_ER` and leaves a sticky directional fault.
Re-enabling after a joint reset clears that fault and discards the old frame.
The PHY/link controller must treat readiness withdrawal as link loss and reset
its transmitter; quiet GMII alone is not a wire-level frame-abort protocol.

The native and generated-RTL fixture checks simultaneous bidirectional traffic
with four independent clock phases/rates, source-valid bubbles, runt and jumbo
stress frames, every readiness input withdrawn with all clocks stopped, either
restart order, isolated directional faults and fresh traffic after reset.
Every observed event is compared exactly. This is a digital integration test;
physical Gray-bus timing, reset recovery/removal, synchronizer metastability,
transmit-clock recovery and complete PHY electrical verification remain separate.

`hold_a_in` and `hold_b_in` prevent starting a new data frame while allowing
an active frame to complete. `pause_a_out` and `pause_b_out` request upstream
flow control at occupancy 256 and clear it at 64. `idle_a_out` and `idle_b_out`
indicate completed frame/IFG boundaries for transmit arbitration. The
[flow-controlled composition](flow_control.md) implements PAUSE generation,
reception, local filtering and command clock crossings around these ports.
The pressure regression checks 512 intact frames with clock drift, output
blocking and delayed peer response; it requires actual pause/resume activity.

```sh
cmake --build build --target analog_1g_repeater_forwarding
ctest --test-dir build -R '^analog\.1g\.repeater_forwarding\.rtl$' --output-on-failure
```

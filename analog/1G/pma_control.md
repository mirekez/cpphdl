# PHY startup and connected symbol path

[`PMAControl`](pma_control.hpp) controls the existing PCS transmit modes at
125 MHz. It captures the negotiated role when enabled. A master may begin
training immediately; a slave first waits for receive-scrambler acquisition.
Neither an assigned role nor an elapsed timer grants permission to forward data.

| State | PCS mode | Exit condition |
| --- | --- | --- |
| Disabled | Zero symbols | Enable captures the role and starts acquisition |
| Silent | Zero symbols | Master role or acquired remote scrambler |
| Training | Training idles | Minimum dwell plus qualified local reception |
| Idle | Idles carrying receive readiness | Both receivers ready after dwell, or local failure |
| Normal | Data or idles | Peer failure selects idle; local failure returns to acquisition after the active frame ends |
| Timeout | Zero symbols | Restart, disable or reset |

Minimum dwell is 125 symbol clocks, or 1 µs. The acquisition limit counts
750 master or 350 slave millisecond enables. It continues across silent and
training states, stops after successful training, and restarts on a new attempt.
The millisecond source must derive from the reference, as `ReferenceMillisecond`
does in the negotiation composition. Unit tests inject enables with gaps to
check counting and expiration; they do not accelerate the production divider.
The state relationships and timer values are cross-checked against the
[IEEE presentation reproducing Figure 40-15 and the inherited timer values](https://www.ieee802.org/3/an/public/nov04/seki_1_1104.pdf).
This is not a claim that all current Clause 40 control requirements are qualified.

`link_out` withdraws immediately when either receiver becomes unqualified.
Transmit mode retains an active frame until the controller can enter its next
state. The upstream MAC/forwarder must honor link withdrawal for new traffic.
The native controller test checks 6,593 observations: both roles, slave silence,
dwell boundaries, local/remote readiness, frame completion, both acquisition
timeouts, role-input changes, restart and disable. The same trajectory passes
generated RTL and IHP mapped logic exactly: 202 mapped cells. The slow-corner
cell-only setup estimate is +4.95 ns at an 8 ns period.

[`PMAEndpoint`](pma_endpoint.hpp) connects the controller, `PCSTransmitter` and
`PCSReceiver`. Its receiver reconstructs the peer's readiness from the C-pair
idle bit after polarity/mask normalization. The framer holds this status through
data and clears it on lost lock, PMA loss or repeated framing faults. The
composed receiver exposes that result through `remote_receiver_ok_out`.

The two-end native test supplies each endpoint with quantized samples of its
peer's actual transmit symbols. It applies different pair-polarity masks, starts
without receive seeds, verifies slave silence, and requires both peers to reach
normal mode through wire-carried readiness. Six complete 64/1518-byte Ethernet
frames pass exact preamble, byte, FCS and boundary checks: 3340 GMII octets in
2608 symbol clocks. Removing receiver quality withdraws both links; restoration
requires successful retraining before another frame exchange. Both ends stop on
disable. The same independent frame checks and complete trajectories pass
generated RTL and IHP mapped logic exactly: 51,225 mapped cells. The slow-corner
cell-only setup estimate is +1.03 ns at an 8 ns period. These estimates include
the declared interface delays and loads; they do not qualify routed timing.

The native calls that clear child transmit/receive state on disable or restart
must also drive those children's generated reset connections. The root
converter now preserves these reset expressions. A separate scalar/indexed
counter regression checks external clears over 2000 clocks against independent
native and RTL expectations; the endpoint test checks the resulting frame path.

[`AlignedPMAEndpoint`](pma_aligned.hpp) adds actual cable pair identification
and deskew before this endpoint; its separate [acquisition contract and tests](pair_acquisition.md)
describe the supported wiring and delay range.

This is the symbol-domain integration test. `samples_in` must be sampled and
deskewed by the physical receive path. `receiver_quality_in` must come from its
qualified timing, adaptation and signal-quality checks. These are explicit
interfaces, not substitutes for implementing the ADC array, clock loop, cable
pair alignment or analog link-pulse circuitry. Negotiation and recovered-clock
domain integration remain separate work.

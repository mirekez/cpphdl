# Ethernet flow control for the two-port repeater

The user-selected congestion policy is IEEE 802.3 MAC Control PAUSE. There is
no congestion-driven whole-frame drop path. `FlowControlledRepeater` connects
two `EthernetPausePort` instances to `RepeaterForwarding`.

Each port has a stable, configured individual MAC address. Auto-negotiation
advertises symmetric PAUSE in its base page and exposes `pause_out` only after
the peer's matching capability and PMA link are established. The forwarding
composition requires both negotiated capability inputs before admitting data.
A peer without PAUSE support therefore cannot silently enter the lossless mode.

`NegotiatedRepeater` makes this connection explicit: two `Autonegotiation`
instances supply the capability flags directly to `FlowControlledRepeater`.
Its public interface has FLP pulse detection/drive, PMA training/link status,
role configuration, stable MAC addresses and the delivered datapath clocks.
It has no externally supplied PAUSE-enable override. Loss of either PMA link,
either restart request, or global disable clears forwarding even when datapath
clocks have stopped. Reference-clock negotiation continues independently.
The physical PHY must still provide FLP electrical conversion and recovered
slave transmit clocks; this digital composition does not provide those circuits.

| Class | Responsibility |
| --- | --- |
| `EthernetPauseReceive` | Recognize addressed MAC Control; accept PAUSE only after preamble, opcode, exact length, RX_ER and FCS checks |
| `EthernetPauseFilter` | Delay ingress by 32 recovered symbols and consume addressed MAC Control frames locally |
| `EthernetPauseCrossing` | Transfer a validated command and its 16-bit duration between RX and TX clocks using a held payload and acknowledgement |
| `EthernetPauseTimer` | Hold new data frames for the requested duration; finish any active frame first |
| `EthernetPauseTransmit` | Emit preamble, destination/source addresses, control fields, padding, FCS and twelve idle symbols |
| `EthernetPauseArbiter` | Insert control traffic between complete data frames, refresh PAUSE and emit zero-duration resume |
| `EthernetPausePort` | Combine these functions with coordinated clock-domain reset |
| `FlowControlledRepeater` | Connect both ports, frame buffers and their independent delivered clocks |
| `NegotiatedRepeater` | Connect real FLP negotiation and trained-link status to both forwarding ports |

At 1 Gb/s a quantum is 512 ns, or 64 GMII clocks. A local congestion request
sends 256 quanta and refreshes after 8192 TX clocks while congestion persists.
A cleared request sends zero quanta. Received commands replace the timer;
control traffic remains sendable while ordinary data transmission is paused.
The command crossing retains the latest pending update while awaiting an
acknowledgement, including a zero-duration resume.

Each direction uses `SynchronousFrameFIFO` with 4096 eleven-bit entries: frame bytes with their error flag,
and one end marker per frame. Occupancy of 256 requests upstream PAUSE;
occupancy of 64 clears the request. The remaining capacity accommodates traffic
already in flight. Four 1024-word SRAM banks each feed a local output register,
so bank selection and frame-control logic do not share the SRAM's read-delay
budget. A four-word prefetch queue reserves room for both in-flight read stages
before issuing another read. It sustains one GMII byte per TX clock and retains
every pending word if PAUSE stops consumption. Transmission hold applies only
at frame boundaries.
Unexpected capacity exhaustion or an upstream PHY fault remains an explicit
sticky fault with an active-frame TX_ER abort, requiring coordinated recovery.

The product's intended maximum MAC frame is 1522 bytes including FCS, plus the
eight-byte preamble/SFD on GMII. Isolated jumbo traffic in the FIFO regression
is a stress case, not a lossless jumbo-frame guarantee. The physical integration
must prove the remaining buffer capacity covers local frame completion, PAUSE
serialization, cable/PHY latency, peer reaction and completion of its current
frame. The standalone buffer test includes 1800 symbol times of response delay;
the bilateral MAC test includes 750 ns of response delay after observing FCS.
Neither substitutes for the eventual MDI-to-MDI timing measurement.

The [IEEE 802.3 Annex 31B text](https://www.my-wire.de/schnittstellenbeschreibung/ethernet.pdf)
and [UNH-IOL Annex 31B test suite](https://www.iol.unh.edu/sites/default/files/testsuites/ethernet/CL31_FC/flow_control_testsuite_v1.7.pdf)
provide the protocol references. At 1000 Mb/s, commencement of PAUSE must prevent
starting a new data frame within two quanta of valid PAUSE reception at the MDI;
an already-started frame completes. PAUSE cannot guarantee lossless operation
when a peer ignores it or a physical link fails.

## Regression

```sh
cmake --build build --target analog_1g_flow_controlled_repeater \
  analog_1g_repeater_forwarding analog_1g_receive_transmit_crossing \
  analog_1g_synchronous_frame_fifo \
  analog_1g_ethernet_pause_0 analog_1g_ethernet_pause_1 \
  analog_1g_ethernet_pause_2 analog_1g_ethernet_pause_3
ctest --test-dir build --output-on-failure \
  -R '^analog[.]1g[.](ethernet_pause|flow_controlled_repeater|repeater_forwarding|receive_transmit_crossing|synchronous_frame_fifo)[.]rtl$'
```

The protocol tests independently check valid and invalid commands, multicast
and local-unicast addressing, byte order, zero resume, maximum timer duration,
FCS, frame lengths, source-address capture, IFG, valid bubbles and adjacent-frame
filtering. The integrated test runs independent RX/TX clocks with drift and
simultaneous bidirectional congestion. Both peers initially pause data, yet
locally generated control frames must still escape. All 240 data frames must
arrive in order with their original FCS, alongside six PAUSE and six resume
frames. Each flow passes these checks separately before cycle digests are
compared. The synchronous SRAM FIFO regression preloads data while TX is stopped, then
checks 606 frames through bank/pointer wraps, boundary holds, and real fault
cases. It prevents declaring an underrun before the first SRAM read completes.
The original FIFO/reset regressions retain full event traces.

These digital checks pass, including replay after mapping to eight IHP
dual-port SRAM macros and IHP standard cells. The SRAM mapping rules and
physical-stage scripts live in `IHPSG13G2/`. The latest integrated regression
also checks two unsupported-peer/reset-isolation cases (630084 cycles total).

Physical placement/routing, CDC constraints, complete PHY
latency, chip clocking and full-chip physical verification remain required for
a complete ASIC GDSII; this document does not claim those are complete.

The separate `analog.1g.negotiated_repeater.rtl` regression runs the new top
through the actual 1300 ms negotiation interval at 125 MHz, PMA handoff,
bidirectional data, received PAUSE and stopped-clock link loss. It compares
the independently checked C++ and RTL observation digests. The converter's
primary-domain timing budget is conservatively 136 MHz so its legacy
negotiation methods remain on `ref_clk` while the receive processing domains
also have a 136 MHz budget. This is an overconstraint for conversion, not a
change to the 125 MHz physical reference or negotiation timebase.

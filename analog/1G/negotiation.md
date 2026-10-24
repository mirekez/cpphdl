# Link-pulse and role hardware

[`FLPTransmitter`](flp_transmit.hpp) captures a sixteen-bit page at the start of
each legacy FLP burst. On the 125 MHz reference, clock pulses are exactly
125 µs apart, data pulses are 62.496 µs after the preceding clock, and bursts
start 16 ms apart. A high output lasts thirteen clocks, or 104 ns. An absent
data pulse encodes zero; a present pulse encodes one, least significant bit
first. Page changes during a burst take effect at the following burst.
Disable, restart and reset abort the current pulse and re-arm the first clock.
The output is a digital control for a physical pulse driver, not an MDI source.
Nominal wire timing is described by the
[UNH-IOL Clause 28/40 introduction](https://www.iol.unh.edu/sites/default/files/knowledgebase/ethernet/Copper_ANEG_JEFF_LAPAK.pdf).

[`FLPReceiver`](flp_receive.hpp) consumes a physical detector's Boolean output.
Two marked input synchronizers precede edge and width qualification. Pulses
shorter than five clocks are ignored; more than 32 clocks causes a fault.
Data and clock apertures are 47–78 µs and 111–139 µs after the previous clock;
160 µs without a clock abandons a partial burst. Seventeen qualified clocks
commit sixteen data bits. Duplicate data, invalid spacing, oversized pulses and
incomplete bursts cannot produce a qualified page. No link-lock flag is inferred
from receiving a single burst.

The transmitter's independent wire-interval oracle checks 12,001,007 clocks,
six complete pages, changing inputs, reset, disable and restart. The receiver's
oracle supplies its own pulse intervals, jitter, pulse widths and malformed
bursts: 3,360,003 clocks, eight accepted pages and four rejected bursts. Native,
generated RTL and mapped IHP gates pass their own contracts and match exact
event traces. Separate 8 ns slow-corner cell checks pass. These are digital
checks, not analog pulse shape, cable or routed CDC qualification.

[`MasterSlaveResolver`](master_slave.hpp) consumes acknowledged UP1/UP2 fields.
Manual role requests take precedence. Conflicting equal manual roles report
configuration fault. With automatic roles, a multiport device takes precedence
over a single-port device; matching port types use the eleven-bit seed ordering.
Equal automatic seeds report collision and leave the role unresolved. Reset
clears the result; disabling advance holds it. Ability and reserved UP1 bits do
not alter role resolution. Tests use a separately encoded four-category role
table and every local seed with equality, neighboring and extreme remote seeds,
including stalls and resets. Native/RTL/IHP-gate and cell checks pass. The role
table is documented in the
[IEEE 1000BASE-T Auto-Negotiation presentation](https://www.ieee802.org/3/ab/public/feb98/an1.pdf).

[`Autonegotiator`](negotiation.hpp) implements base-page acknowledgement, message
code 8, UP1 capability, UP2 seed and trailing null pages when required by the
peer. Three consistent abilities and acknowledgements qualify each exchange;
seven complete acknowledged bursts precede advancement. `transmit_burst_in`
reports the completed final pulse, so a page change or PMA handoff cannot truncate
the last burst. Ability-associated toggles and the matched next-page indication
stay frozen through acknowledgement. Local configuration and seed are captured
at startup. Quiet restart, exchange timeout and PMA qualification timers use
the millisecond enable. Seed collisions retry; seven consecutive collisions or
conflicting manual roles report configuration fault.

Its independent 23,211-state unit contract covers ordered exchange, premature
and inconsistent acknowledgements, toggle/next-page races, retries, timeout,
restart and link loss. The current transmitter and page controller pass six
native/RTL/IHP-gate/cell CTests in 44.38 s. The maintenance cases follow the
[IEEE toggle request](https://www.ieee802.org/3/maint/requests/maint_1039.pdf)
and [next-page request](https://www.ieee802.org/3/maint/requests/maint_1040.pdf).

[`Autonegotiation`](autonegotiation.hpp) connects this controller to the actual
FLP transmitter, receiver, role resolver and `ReferenceMillisecond` divider.
The divider derives each millisecond from 125,000 reference edges. Two connected
peers pass a 264,750,017-cycle native and generated-RTL test. An independent wire
monitor checks the actual 1300 ms startup interval, pulse widths, page order,
acknowledgement completion and opposite roles before asserting PMA readiness.
The retained mapped-gate executable now also completes all 264,750,017 cycles
and matches both traces exactly (1911.90 s). The original runner timed out at
1200 s; the longer endurance run records its binary/netlist/input hashes and
does not turn that failed CTest into a pass. The complete composed-runner CTest
with a suitable timeout remains to be repeated.

Physical pulse drive/detection, entropy generation and PMA training/startup are
explicit integration boundaries. A resolved role chooses clock ownership; it
does not establish receive lock or justify forwarding data. `pma_link_in` must
come from a trained physical receiver before negotiation can report link.

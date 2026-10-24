# Recovered receive clock to reference clock

[ReceiveClockAligner](receive_alignment.hpp) is a synthesizable C++RTL class with
`receive_in`, `receive_enable_in`, `aligned_out`, `overflow_out` and `fault_out`
member ports. Run one instance per PHY port. Its `rx_clk` can be an internally
recovered symbol clock, with the enable held true, or a fixed 250 MHz ADC clock,
with `PCSReceiver.receive_valid_out` marking accepted words. `ref_clk` comes
from the chip's one external reference. The class implements the digital clock
crossing; symbol timing recovery precedes this block.

The 64-entry memory stores GMII words (data bits 7:0, valid bit 8, error bit 9)
and one end token per frame. Idle clock cycles consume no entries. Write and
read pointers each advance by at most one per respective clock edge. Seven-bit
Gray pointers cross through two registers marked `ASYNC_REG`; the read domain
never directly samples the binary write pointer. A separate Gray frame counter
allows a completed short/error frame to drain below the eight-word start threshold.
An invalid input clock does not advance the previous-data-valid state, write
a word, or manufacture an end token. Pointer synchronizers continue running.
Reset remains effective when the input enable is false.

The output starts with eight available words, or a complete short frame. It
then delivers one word on each reference-clock edge through the end token.
At least twelve idle cycles separate output frames. Data and error bits are
preserved in order; the input's exact idle count and latency are not preserved.
These semantics are suitable for a full-duplex forwarding boundary, not a
claim of IEEE half-duplex repeater timing compliance.

Overflow latches in the receive domain and crosses as a synchronized bit.
Underflow inside a frame emits one valid/error word (`0x300`) and latches a
fault. Both conditions stop output until reset. A link supervisor must quiesce
traffic and reset **both** domains together before restarting. Resets in this
class are synchronous to their respective domain; the chip reset controller
must provide safe release and keep the reset asserted until both clocks run.
Resetting only one side of an active FIFO violates its contract.

[tests/alignment.cpp](tests/alignment.cpp) checks forty alternating 72/1526-byte
wire frames at each of nine initial phase/rate combinations: 0, +/-200 ppm and
three phases, both with a symbol clock and with fixed-rate enabled sampling.
It verifies 720 frames, 575,280 bytes, FCS residue, minimum IFG, byte/error order,
short frames, overflow, stopped receive clock, failure containment and joint
reset. Every native and generated-RTL output is compared exactly; mapped IHP
simulation uses the same independent frame checker and trace comparison.

A finite FIFO cannot fix an indefinitely faster incoming stream while also
preserving all data and minimum IFG. Tests cover bounded frames and bounded
frequency error; overflow remains a visible failure. With both local PHY ports
operating as masters, remote slave timing should follow the common reference,
but that requires the not-yet-implemented startup and physical timing loops.

For physical implementation, constrain each Gray launch-to-first-synchronizer
path to less than one source period, preserve and place the synchronizer stages,
and check recovery/removal and payload-memory timing. A blanket false path is
not sufficient. Metastability and routing skew are not modeled by either
functional simulator; these blocks do not yet have routed CDC/MTBF signoff.

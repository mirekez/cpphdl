# Cable pair acquisition and aligned PMA

[`ReceivePairAcquirer`](pair_acquisition.hpp) identifies the received A/B/C/D
streams and corrects their relative sample delay before the receive PCS runs.
Its inputs are four simultaneous, offset-binary samples: zero is 128 and one
PAM level is 32 counts. Gain, common mode and sampling timing must already
provide this interface. This class does not infer those analog conditions.

During PAM3 training, the A stream carries the identifying scrambler recurrence.
The acquirer retains separate magnitude histories for all four physical inputs,
requires 128 consistent observations after recurrence acquisition, and searches
the remaining streams for both alternating scrambler phases and 17 delay taps.
It accepts only a unique one-to-one mapping. A fixed C-stream complement for
peer receiver readiness is allowed. The implementation does not receive the
remote seed, a wiring map or a known training sequence from its caller.

An eight-symbol lookback on the A stream permits relative delays from −8 to +8
symbols, or ±64 ns at 125 MBd. Seventeen stored sample quartets supply the
selected taps. The output preserves physical polarity; the following PCS
performs its own polarity acquisition. `mapping_out` identifies physical lanes
for logical A/B/C/D, and `delays_out` records four five-bit tap indices. See the
member-port comments for packing and state details.

`advance_in` consumes one simultaneous quartet. A gap stalls the acquisition
history and deskew buffers. Signal-quality loss, restart, disable or reset
withdraws lock and invalidates output. The remote master/slave role is captured
at startup; changing an external role input during reception cannot silently
change the acquired recurrence.

The independent fixture generator uses the transmit reference and the checked
IEEE code-group table to generate eight streams: both roles, both readiness
values and two seeds. The native test covers all 24 permutations, all 16 polarity
masks, delay combinations, ±3-count noise, gaps, false-lock cases and loss/restart.
It also checks 12,288 aligned data quartets containing all five PAM levels.
Its 56,385-clock trajectory passes generated RTL and IHP mapped gates exactly.
The implementation maps to 7,802 cells, with +4.22 ns slow-corner cell-only setup
slack at an 8 ns period. This is functional and pre-placement evidence.

[`AlignedPMAEndpoint`](pma_aligned.hpp) composes this acquirer with the existing
PMA control, transmitter and complete receive PCS. The two-ended test applies
different permutations, polarities and physical delays to each endpoint's actual
transmit stream. Native execution passes slave-silent startup, six complete
64/1518-byte frames, 3,340 received GMII octets with independent FCS checks,
receive-failure propagation, retraining and disable in 7,883 symbol clocks.
The same independent checks and complete output trajectory pass generated RTL
and IHP mapped gates exactly. The composition maps to 57,032 cells, with
+0.70 ns slow-corner cell-only setup slack at an 8 ns period. This still does
not establish a physical copper implementation or routed timing.

Run the standalone and composed regressions from the repository root:

```sh
cmake --build analog/build --target analog_1g_pair_acquisition analog_1g_pma_aligned
ctest --test-dir analog/build -R 'analog.1g.(pair_acquisition|pma_aligned).' --output-on-failure
```

The electrical AFE, ADC normalization, adaptive-filter training, closed clock
loop and analog FLP interface remain required around these digital classes.
These tests establish acquisition at the declared sample interface; they do not
establish channel masks, BER, process-corner performance or routed signoff.

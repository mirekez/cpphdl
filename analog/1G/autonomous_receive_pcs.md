# Autonomous receive adaptation and PCS

`AutonomousReceivePCS` accepts four recovered symbol observations, packed as
four eight-bit ADC codes, and the four corresponding local PAM5 transmit
codes. It has no expected-symbol or training-target input. Timing recovery
and alignment of those local references belong before this interface.

Each lane uses `AutonomousReceiveFilter`. Its shared arithmetic is the same
six-cycle sample pipeline and ten-cycle coefficient-update pipeline used by
the supervised `PipelinedReceiveFilter` contract. Adaptation mode follows each
accepted observation through the pipeline, including clock-enable bubbles.

Acquisition proceeds as follows:

1. Adapt only the twenty local-reference coefficients for 8,192 accepted
   symbols. The four receive FFE coefficients stay fixed, preventing blind
   energy minimization from erasing the wanted signal.
2. Continue echo adaptation until every lane has more than 65,536 squared ADC
   counts over a 256-observation window. A silent far-end peer does not start
   receive-FFE adaptation merely because a timer expires.
3. Adapt using sliced PAM3 idle decisions for 16,384 accepted symbols.
4. Continue with PAM5 decisions and admit observations to pair identification,
   deskew, scrambler acquisition, trellis decoding and receive-quality checks.

The energy threshold and acquisition lengths are implementation choices, not
an IEEE compliance claim. Link quality comes from received idle/PCS evidence.
Disable, restart, loss of signal readiness or transport failure immediately
withdraws availability and resets acquisition on the next processing edge.

The filter regression checks eight independent synthetic interference/noise
sequences, frozen-coefficient holdouts, and a disabled-adaptation negative
control. The connected PCS regression exercises both transmitter roles, pair
permutation, polarity reversal, skew, local interference, a silent peer,
clock-enable gaps and complete independently encoded Ethernet frames. Native
and converted RTL runs check their outputs separately, then compare complete
event traces.

```sh
cmake --build analog/build --target analog_1g_autonomous_receive_filter analog_1g_autonomous_receive_pcs
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]autonomous_receive_(filter|pcs)[.]rtl$'
```

These tests start with recovered symbol observations. Electrical ADC operation,
the receive timing loop during initial echo acquisition, PMA startup handshakes,
the full cable operating envelope and physical integration require their own
connected verification. This component does not establish a completed PHY ASIC.

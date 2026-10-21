# Continuous-time linear equalizer

[ctle.hpp](ctle.hpp) contains symmetric lead networks and two finite-bandwidth gm
stages. On each lane, 200 Ohm is bypassed by 400 fF; 50 Ohm returns the lead node
to VCM. Each gain stage has voltage gain four, 50 Ohm output impedance and 5 fF
output storage. Input resistors and bypass charge load the upstream channel.

For ideal differential excitation and an unloaded output, the circuit yields:

```text
H(s) = 4 * 50 * (1 + s * 200 * 400 fF)
       / (250 + s * 200 * 50 * 400 fF)
       / (1 + s * 50 * 5 fF)
```

DC gain is 0.8. The lead zero is about 1.99 GHz and its pole about 9.95 GHz; the
output storage adds a higher pole. This is a consequence of declared components,
not a primitive CTLE transfer-function opcode. Parameters remain configurable.

`ctle_low` and `ctle_nyquist` compare independently measured complex sine gain at
1 GHz and 5.15625 GHz against this analytic expression. Both flows must satisfy
the same 2.5% complex-gain error bound before their measured gains/phases are
compared. Integrated receiver/link tests exercise its actual loaded waveform.
Module: [TenGCTLE.va](generated/TenGCTLE.va).

# Transmitter line pullup

`transmit_resistor_layout.py` realizes the output stage's 50 Ω pullup as
eight parallel 400 Ω contacted-poly sections. Each section is 12.5 µm wide;
the PDK's quantized lengths give 50.0243 Ω before interconnect extraction.
Thirty cuts at each via transition connect each end to center-fed TopMetal1
collectors. The substrate guard remains an explicit third port.

The maintained test checks strict transistor-level LVS, all local geometry
rules and a deliberately missing section. Isolated minimum-density violations
remain recorded; they require chip integration and fill. Maximum-density
violations are not waived. Corrected-sidewall Magic extraction preserves eight
resistors and unique TopMetal1 terminals. Its DC sweep applies 3.3 V at the
supply and 1.3–3.3 V at the output, checking resistance within 5% of 50 Ω.

Parallel devices cannot be identified by shared net names or enumeration order.
`resistor_array_extraction.py` matches their immutable Magic rectangles to the
distributed extraction's actual terminal nodes. It retains each body's external
coupling at a finite-resistance midpoint and removes only duplicated intrinsic
substrate charge. Tests reject missing identities, changed geometry, incorrect
bulk connections, logical shorts and insufficient charge. They also reverse
record order and use different SPICE instance indices.

Run:

```sh
ctest --test-dir analog/build -R 'analog.1g.ihp_(extraction.resistor_array_connections|transmit.pullup)$' --output-on-failure
```

This is nominal local geometry/DC evidence. Final fill, process corners,
mismatch, field/contact calibration, self-heating, electromigration and the
connected transmitter remain separate qualification requirements.

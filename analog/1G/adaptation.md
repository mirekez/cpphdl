# Sampled receiver and training boundary

`sampled_adc` quantizes a normalized receive voltage into eight bits over
`[-4, +4]` PAM levels and counts clips. It receives each solver's achieved
waveform, not a generated expected symbol. This is a sampled behavioral model;
it does not claim sample-switch noise, aperture jitter or an ADC layout.

`adaptive_receiver` contains a four-tap receive FFE and five taps from each of
the four local transmit streams: 24 coefficients in total. The receive FFE is
seeded with the first four terms of the known pulse-filter inverse
`[4/3, -4/9, 4/27, -4/81]`. Local reference voltages come from each transmitter's
actual `REPLICA` observation. They include its history and filtering.

For feature vector `x` and coefficient vector `w`, the normalized LMS update is

```
y = dot(w, x)
w += mu * (desired - y) * x / (0.001 + dot(x, x))
```

The reference features have negative signs, so positive coefficients subtract
local interference. Five reference taps cover the FFE's temporal spreading of
the local echo/coupling terms. A two-tap canceller would leave older terms even
when the unfiltered interference itself had only two taps.

The nominal, noisy, pair and negative-control benches have 256 symbols. The first
160 are declared training. The stronger interference/changed-load case has 512
symbols with 416 training symbols; 160 symbols did not converge sufficiently in
that case. Training length is declared in `contract.json` before simulation.
No desired symbol is passed to `sample()` thereafter, and the test checks that weights
remain bit-identical throughout holdout. Eight guard symbols flush filter history;
88 symbols per receiver are checked independently. A four-pair link therefore
checks 704 simultaneous-direction holdout decisions per nominal/noisy flow,
and 704 in the changed-load case (512 - 416 - 8 = 88 per receiver). The checker uses
known answers after recovery only to score decisions, never to change them.

Each analog solver feeds the same separately executed C++ DSP reference. That
tests both analog paths and the sampled interface, but is not independent DSP
implementation equivalence. Unit tests additionally use a separate analytical
ISI/echo/coupling fixture with a longer held-out interval. A disabled-adaptation
system is a negative control and is expected to show errors.

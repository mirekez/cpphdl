# Connected PAM5 current cell

`transmit_cell_layout.py` connects two steering NMOS banks, a cascode bank
and a current-setting bank. These are the main/history cell topologies used
inside `cascoded_emphasized_pam5_driver`. External ports are P, N, GP, GN,
BIAS, CASCODE and VSS. TAIL and LOWER are internal conductors.

| Bank | Main width | History width | Length |
| --- | ---: | ---: | ---: |
| Each steering device | 400 µm | 200 µm | 0.45 µm |
| Cascode | 1,500 µm | 500 µm | 0.45 µm |
| Current-setting device | 450 µm | 150 µm | 0.45 µm |

The guarded banks retain 5 µm fingers. Their source/drain collectors feed
TopMetal2 through distributed TopVia2 arrays. Local landing pads extend
outwards so their second via row does not short the opposite terminal.
Current routes use 16 µm metal; gate and bulk access first leaves each bank
on Metal2. The external line and ground ports feed their actual current
branches directly. The common return still connects every guard ring.

The initial two-cut connections passed geometry/LVS but lost as much as 16.0%
of the intended current. Wider routing and parallel cuts reduced that to
6.7%; a second row and closer return reduced it to 5.41%. Those failed
experiments motivated the direct branch connections. None relaxes the test's
5% current-error limit.

The direct connections pass both main/history DC matrices with maximum errors
of 4.00% and 3.08%, respectively. Both local geometry/LVS checks and deliberate
cut-input rejections also pass. This is local nominal evidence, not full-driver
or chip qualification.

The maintained test checks both widths, strict LVS, complete local geometry
rules and a physically cut steering input that LVS must reject. Isolated
minimum-density failures remain visible. A separate Magic extraction view adds
only internal labels: KLayout treats top-level text as ports, while Magic can
retain non-port node names. The test compares every physical polygon before
and after annotation, verifies exactly seven extracted external ports, and
checks capacitor connectivity and conserved distributed RC.

DC comparisons independently assemble the four PSP devices with equal-width
5 µm fingers and ideal wiring. Both steering directions are checked at 1.3,
2.3 and 3.3 V line bias, using half, nominal and four-thirds branch current.
Nominal main/history currents are 7.5/2.5 mA. These comparisons isolate layout
loss at TT/27 °C with fixed 3.3 V steering and 1.8 V cascode bias.

```sh
ctest --test-dir analog/build -R 'analog.1g.ihp_transmit.current_cell$' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_transmit.(switching_guards|current_cell_switching)$' --output-on-failure
```

The switching test passes six extracted cases at the same three line voltages.
It applies complementary 100 ps steering edges over five 8 ns symbols and
checks the last two nanoseconds of each symbol. Each flow must independently
deliver the intended differential current and total current; agreement alone
cannot pass two incorrect waveforms. Maximum extracted/reference difference
is 3.61% main and 2.90% history, below 5%. Five checker tests reject wrong
amplitude, stuck steering, common excess current and incomplete/nonfinite data.
The 42 ns experiments use ideal gate/bias sources and begin from an operating
point; they do not establish translator drive, bias startup or full-line behavior.

This cell does not include the reference/trim generator, level shifters, bias
storage, output pullups or the additional explicit storage of the complete
C++ transmitter experiment. Connected transient behavior, full-driver
integration, PVT, mismatch, fill, thermal/EM and full-chip qualification remain
required. Each run retains its actual pass/fail receipt and source/tool hashes.

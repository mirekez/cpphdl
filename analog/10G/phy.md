# Electrical PHY composition

[phy.hpp](phy.hpp) defines `transmitter`, `receiver` and `link` as IC classes.
The transmitter binds serializer, stored tap history and TX emphasis. The receiver
binds CTLE, PLL CDR, decision feedback and a sliding four-bit deserializer. `link`
connects them through the explicit [channel](channel.hpp) and
[SFI input](sfi_input.hpp), including module-side blocking capacitors. Receiver timing comes
only from the recovered clock; TX clock-tree ports terminate at the transmitter.

The channel is a symmetric two-section RC fixture. Each section contributes
15 Ohm and 200 fF per lane, followed by a 100 Ohm differential termination. The
isolated ideal-source DC ratio is `100 / (100 + 4 * 15) = 0.625`. TX impedance
and CTLE loading further reduce the integrated voltage. This fixture is not a
measured SFP+ board/connector S-parameter model or an optical channel. In the
integrated link its shunt termination is set to 1 GOhm; `sfi_input` provides the
single nominal 100 Ohm load. `link_loaded` changes that input termination to 80 Ohm to exercise environmental influence on the whole network.

`receiver` tests use independent differential data sources. `link` and `link_loaded`
begin with four parallel electrical inputs and finish at recovered serial decisions
and all four parallel outputs. Each flow checks every measured bit and independently
descrambles a complete 66-bit data block. The initial symbol placement and transmitter
history contribute 1.5 UI of main-cursor latency; the checker accounts for that
known architecture, not a fitted best-match delay. Deserializer outputs add their
explicit pipeline history. A complete PCS block-lock/word-aligner is outside this IC.

See [tests](tests/README.md) for exact flow/check/comparison commands and
[requirements](requirements.md) for the standards boundary. Reusable modules for
the channel and all three composed classes are in [generated/](generated/).

`sfi_input` uses two 100 nF blocking capacitors, each with 0.1 Ohm ESR, and two 50 Ohm resistors to VCM.
These are circuit purposes, so stored capacitor voltage, common mode and downstream
CTLE loading all participate in the coupled solve. The capacitors belong to the
external module/fixture boundary, not to an assumed host transmitter requirement.
For an ideal differential source the pole is 31.77 kHz; with 50 Ohm source resistance
per lane it is 15.90 kHz. `sfi` checks 10G symbol transmission and bias; `sfi_decay`
checks the independent 5.01 us high-pass decay and DC blocking over 96 us.
See [SFP+ SFI scope](sfi.md) for the exact reference and compliance boundary.

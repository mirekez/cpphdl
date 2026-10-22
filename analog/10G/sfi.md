# SFP+ SFI target and coverage

Target: a 10GBASE-R host electrical interface for a limiting SFP+ optical module.
The optical PMD is external. The primary high-speed reference is
[SFF-8418 Rev 1.4, July 30, 2015](https://members.snia.org/document/dl/25879),
listed as published by [SNIA](https://www.snia.org/node/15434).
SFF-8431 provides the earlier combined interface specification.

Section 3.4 places DC blocks in the module and specifies nominal 100 Ohm
differential source/load impedances. Tables 5–6 specify host TX limits at B,
including 34 ps maximum 20–80% transition time, 15 mV RMS AC common mode,
and eye coordinates X1 = 0.12 UI, X2 = 0.33 UI, Y1 = 95 mV, Y2 = 350 mV.
Tables 7–8 and Figure 10 specify limiting-module host receiver stresses, including
separate sensitivity/overload and sinusoidal-jitter tests. Appendix D defines
calibration, patterns and statistical measurements; finite error-free vectors
cannot establish the required BER.

## Implemented modeling scope

`tx_emphasis` has 50 Ohm source resistance per lane. `sfi_input` declares the
module-side capacitors and a nominal 100 Ohm receiver load, split to the receiving
common-mode node. The split bias is a design choice. `channel` adds explicit
lumped RC loss; it is not a characterized host board or compliance board.

`link` is an electrical loopback experiment. Its path is transmitter → RC fixture →
DC blocks/termination → CTLE → CDR/DFE → deserializer. It does not model the two
optical conversions of a real duplex SFP+ link. The receiver has no TX clock input.
FFE, CTLE and DFE coefficients are configurable implementation choices. KR
backplane training is outside the selected interface.

## Compliance work still required

The regression suite establishes selected circuit laws, coupling behavior, loaded
transmission and finite-vector recovery in two solvers. It is not an SFI conformity
suite. Remaining work includes compliance-board/connector transmission-line models,
return-loss and mode-conversion masks, calibrated stressed-eye sensitivity and
overload, the full sinusoidal-jitter sweep, asynchronous aggressors, noise and
statistical BER evaluation across operating conditions. The existing RC fixture and
idealized gm/switch models cannot establish those claims. No ISSCC power, area or
fabricated-silicon performance comparison is claimed.

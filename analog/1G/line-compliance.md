# Transmitter levels and compliance

The current bank's engineering target is
`Vdiff = -0.5 V * (0.75 * symbol + 0.25 * previous_symbol)` at its declared
100 Ω load. The minus sign follows the current-steering pin convention.
A sustained ±2 symbol therefore gives a ±1 V magnitude; an isolated ±2 symbol
following zero gives a 0.75 V main pulse and a 0.25 V history contribution.
Scaling the whole transmitter to 0.75 V at its sustained level would change this
pulse target incorrectly.

[Tektronix's physical-layer test description](https://www.tek.com/en/documents/application-note/physical-layer-compliance-testing-1000base-t-ethernet)
places the Test Mode 1 A/B peaks between 670 and 820 mV and describes separate
symmetry, relative-level, droop and waveform-template checks. The existing
20 mV weighted-plateau checker is an internal precision test. It is not that
compliance suite, and passing either check must not be substituted for the other.

[UNH-IOL's test-mode guidance](https://www.iol.unh.edu/testing/ethernet/ge/test-modes-help)
identifies the four transmitter test modes and the shared 125 MHz transmit
clock. Formal testing must exercise the implemented MDI path, including the
selected pads, magnetics and loads. The present process fixture does not include
that complete path. Test-mode access, complete waveform masks, distortion,
common-mode, return-loss and clock tests remain required before compliance can
be claimed.

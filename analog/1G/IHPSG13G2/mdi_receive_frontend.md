# Receiver input common-mode interface

`mdi_receive_frontend<Count> : IC` composes a
[`differential_source_follower`](../../basic/differential_source_follower.md)
with the existing [receive frontend](receive_frontend.md). Its exposed ports
remain `PLUS`, `MINUS`, `Vlogic`, `EVALUATE`, `CLEAR`, `TRACK`, `BIT_N` and
`CORE_DECISION`, with inherited `Vdd` and `Vss`.

The transmit driver operates near a 2.8 V common mode. A passive hybrid does
not automatically translate that DC level to the 1.65 V used by the isolated
receiver fixture. The two input followers provide a physical current path and
lower common mode before the differential amplifier. Their input charge loads
the hybrid, their outputs load each other through the amplifier, and their
supply currents affect the ADC through the shared rail.

The dedicated fixture drives a 2.8 V common mode through finite 50 Ω sources
and supplies the followers, amplifier and SAR slices from one resistively
loaded, decoupled rail. It observes both shifted inputs as well as amplifier,
buffer and held sample voltages. One- and four-slice tests and a halved timestep
check the independently calculated DC transfer, input delivery, sample delivery
and hold. Both simulation flows must pass before comparison.

```sh
cmake --build analog/build --target analog_1g_ihp_mdi_receive
ctest --test-dir analog/build --output-on-failure \
  -R '^analog[.]1g[.]ihp_mdi_receive$'
```

This fixture is a precharged generic-MOS acquisition experiment. It does not
include package/magnetics, pad-cell loading, closed SAR conversion or continuous
physical clock/control translation. It does not establish 1000BASE-T MDI
compliance, a passing PSP corner sweep, or transistor layout qualification.
This composition retains the base receiver's 1 kΩ preamplifier loads. It does
not select the newer [wideband receiver](wideband_receive_frontend.md), and
does not inherit that circuit's acquisition or closed-conversion results.

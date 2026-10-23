# A derived receive IC with more hold charge

[`precision_receive_frontend<Count>`](precision_receive_frontend.hpp) inherits
[`receive_frontend<Count>`](receive_frontend.md), including electrical ports,
supplies, amplifier, shared buffer, ADC slices and core-domain decisions.
It changes each hold capacitor from 2 pF to 4 pF before elaboration. Both
variants use the process-specific 1 kΩ reference ladder.

```cpp
ihp_sg13g2::receive_frontend<16> compact;
ihp_sg13g2::precision_receive_frontend<16> precision;
```

Use either type through the same ports. More stored charge reduces switch
injection and off-state feedthrough, while increasing acquisition current,
settling time and capacitor area. The shared buffer and supplies resolve the
extra load together with the other slices.

The earlier 4 pF / 10 kΩ circuit with 200/400 µm sampling switches passed generic
acquisition and nominal PSP acquisition for one, four and sixteen slices. It
failed TT at 125 °C: 20.553 mV acquisition error exceeded the 20 mV requirement.
Its first PSP conversion returned code 128 instead of the DC target 125;
frontend settling also exceeded its aperture budget. These retained results
identify the earlier design and are not qualifications for the current type.

Halving the sampling and dummy widths reduced the hot-corner acquisition
diagnostic error to 5.719 mV, with 1.143 mV hold drift, on 4 pF. The same smaller
switches achieved 14.448 mV acquisition error and 1.787 mV hold drift on 2 pF.
The smaller circuit therefore enters qualification first. The 4 pF type remains
available when its extra hold charge is justified by the connected loads.

The shared fixtures select the IC type at compile time and exercise it through
its ports. Generic C++ and generated Verilog-A must pass the same absolute
contracts separately before comparison. The independent PSP binder preserves
body domains and declared R/C values, rejects unknown laws, and uses explicit
process device sizes. See the [process receiver](receive_frontend.md) for its
qualification commands and physical implementation boundary.

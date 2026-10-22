# MOS storage and clock division

[`cmos_storage.hpp`](cmos_storage.hpp) defines three electrical `IC` classes
with inherited Vdd/Vss and member ports. Storage comes from cross-coupled MOS
gates and declared capacitance. No host variable supplies Q or counts clocks.

| Class | Ports | Behavior and implementation |
| --- | --- | --- |
| `cmos_d_latch` | D, ENABLE, CLEAR, Q, Q_N | Transparent while ENABLE is high; retains state otherwise; 30 MOS channels |
| `cmos_dff` | D, CLOCK, CLEAR, Q, Q_N | Low-transparent master followed by a high-transparent slave; 62 channels |
| `cmos_divide2` | CLOCK, CLEAR, Q, Q_N | A DFF with Q_N wired back to D; 62 channels |

CLEAR disables the latch input path and forces Q low/Q_N high through physical
devices. It has finite propagation time and supply current. The divider toggles
at each rising edge after clear; its output frequency is half the input only
when the connected transistor circuit can settle correctly.

The member `channel` and explicit node/input storage parameters belong to the
circuit model. Their defaults use a 0.35 V threshold and 10 mA/V² channel
strength. The clear devices are five times stronger. Changing parameters or
substituting a derived class changes the circuit that must be qualified; class
inheritance does not confer timing or process guarantees.

The unit experiment applies a 250 MHz input, data changes during both clock
phases, power-up clear and a second asynchronous clear during operation. It
checks unloaded outputs and an additional 100 fF/10 kΩ output load. Every case
runs from a finite 1.2 V supply network. An independent behavioral event oracle
checks transparency, rising-edge capture, retention, clear, complementary
outputs, complete edge order/count and a 500 ps settling bound.

All six cases pass native and Verilog-A/ngspice checks separately before
comparison. The largest checked plateau difference is 0.223 mV RMS; the
largest edge difference is 17.5 ps. Declared comparison limits are 20 mV RMS
and 100 ps. The three registered CTests pass in 169 s:

```sh
cmake --build analog/build --target analog_cmos_storage
ctest --test-dir analog/build -R 'analog.basic.cmos_storage.' --output-on-failure
```

The generated models are `generated/cmos_d_latch.va`, `generated/cmos_dff.va`
and `generated/cmos_divide2.va`. They retain the same physical connections and
charges. These results qualify the selected generic equations and fixtures;
process sizing/body charge, PVT, setup/hold and metastability characterization
remain separate from this functional timing experiment.

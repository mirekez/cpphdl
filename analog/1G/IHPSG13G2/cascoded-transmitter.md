# Cascoded PAM5 transmitter

`cascoded_core_emphasized_pam5_driver : IC` accepts the same sixteen core
steering ports as the earlier transmitter, plus six active-low trim ports.
Twenty-two physical level shifters drive the output-stage gates. Every port
remains a member declaration; nested classes contribute devices to the same
coupled electrical system.

The output stage has four main and four history cells. Each cell declares two
steering NMOS devices, one cascode and one current-setting NMOS. The cascode
reduces the influence of the shared line on the current-setting drain. It adds
finite output resistance and charge; it does not impose an ideal current.

| Device | Main W | History W | L |
| --- | ---: | ---: | ---: |
| Each steering NMOS | 400 um | 200 um | 0.45 um |
| Cascode NMOS | 1500 um | 500 um | 0.45 um |
| Current-setting NMOS | 450 um | 150 um | 0.45 um |

Total widths are divided into legal fingers by `mos_geometry.py`. A 3 kOhm /
3.6 kOhm divider biases the cascodes near 1.8 V at nominal supply. The current
reference uses the existing physical resistor trim. Characterized TT/27 C,
SS/125 C and FF/−40 C codes are 44, 54 and 34 respectively; the hardware does
not automatically identify those codes. Bias filtering is 400 pF, cascode-bias
filtering 50 pF, and each line pullup 50 ohms. Explicit gate, drain and internal
storage accompanies the generic channel law. PSP additionally includes intrinsic
charge, leakage and body effects; the two model families need not agree numerically.

The strict binder recognizes 304 MOS devices, 39 resistors and 615 explicit
capacitors in the complete test experiment, including its supplies and load.
It verifies cascode connectivity, actual geometry and both bias-storage values.
Changing topology or silently replacing a law fails binding.

## Tests

Build `analog_1g_cascoded_core_transmit_driver`, then run:

```bash
ctest --test-dir analog/build -R 'analog.1g.cascoded_core_transmit_driver' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_cascoded_core_transmit' --output-on-failure
ctest --test-dir analog/build -R 'analog.1g.ihp_transmit_interaction' --output-on-failure
```

The first group checks independent generic C++RTL and generated Verilog-A,
80/100/120-ohm loads, timestep refinement and their comparison. The second
uses actual IHP PSP devices and separately checks the same 20 mV plateau,
15 mV ripple and existing gate/current limits. The third tests two connected
output stages with a finite shared supply, and includes the old uncascoded
stage as a required failing electrical control.

For each DC point, zero-volt probes measure each driver's P/N terminal currents.
The 50-ohm pullups give that driver's 100-ohm-loaded Norton contribution as
`25 * (I_P - I_N) + Vdiff / 2`. Both contributions must sum to the measured
line voltage, and each must match its own requested main/history value within
20 mV. Checking only their sum would miss opposing nonlinear errors. There is
no additional 100-ohm receiver termination in parallel with the two drivers.

DC gate controls are ideal static voltages that track achieved Vdd. The complete
transient experiment independently exercises the actual translators and native
PAM5 control trace. Neither experiment establishes resistor corners, mismatch,
noise, voltage-extreme operation, extracted parasitics or full Ethernet compliance.
See [current qualification](qualification-status.md) for accepted results and failures.

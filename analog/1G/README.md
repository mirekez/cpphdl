# 1000BASE-T PAM5 PHY development model

This is the `1G/` counterpart of the [10G prototype](../10G/README.md): reusable
electrical IC classes, generated Verilog-A, module tests and a coupled system
test. It develops the PAM5/PMA signal path for **four pairs at 125 MBd**, with
simultaneous transmission in both directions. It is not a complete interoperable
IEEE 802.3 Clause 40 PHY. [Requirements and coverage](requirements.md) identify
the implemented boundary precisely.

Generic modules are flat in this directory; their exports share `generated/`.
Process-specific modules, tests and exports live in
[IHPSG13G2/](IHPSG13G2/README.md). Main system regression is in [tests/](tests/README.md).
The [recorded validation](tests/validation.md) reports measured results and limitations.

```mermaid
flowchart LR
    A[Four PAM5 transmitters A] <--> C[Shared four-pair RC and coupling network]
    B[Four PAM5 transmitters B] <--> C
    C --> RA[Four hybrids and CTLEs A]
    C --> RB[Four hybrids and CTLEs B]
    A -->|Achieved local replicas| RA
    B -->|Achieved local replicas| RB
    RA --> DA[Sampled ADC and adaptive FFE / echo cancellation A]
    RB --> DB[Sampled ADC and adaptive FFE / echo cancellation B]
    DA --> PA[C++RTL receive PCS A]
    DB --> PB[C++RTL receive PCS B]
```

The shared network and receive front ends run in each electrical simulator.
The sampled receiver consumes each simulation's resulting voltages separately.
The `link_pcs` case drives both transmitters from actual C++RTL GMII encoders
and recovers complete FCS-checked frames from each solver's voltages.

| Source | Purpose |
| --- | --- |
| `parameters.hpp` | 125 MHz symbol clock, 8 ns UI, four pairs and voltage conventions |
| `mapping.hpp` | All 512 normal-data 4D constellation points from IEEE Tables 40-1/40-2; inverse mapping |
| `pam5.hpp` | Thermometer-code DAC and explicitly approximate four-threshold slicer |
| `symbol_memory.hpp` | Two-phase charge storage for the previous analog symbol |
| `transmitter.hpp` | Partial-response pulse filter, finite-impedance differential driver and composed transmitter |
| `channel.hpp` | Reciprocal loaded pair and four-pair network with coupling |
| `hybrid.hpp` | Local transmit-replica subtraction, with configurable balance error |
| `passive_hybrid.hpp` | Four-resistor physical subtraction bridge with attenuation, input loading and output charge; [circuit explanation](frontend.md) |
| `equalizer.hpp` | Passive lead network and finite-bandwidth gain stage |
| `dfe.hpp` | One postcursor tap using the receiver's own held PAM5 decisions |
| `phy.hpp` | Receive front end, one bidirectional pair, and four simultaneous bidirectional pairs |
| `adaptation.hpp` | Sampled ADC, joint receive FFE and adaptive local echo/coupling cancellation |
| `receive_filter.hpp` | Fixed-point hardware FFE and adaptive echo/NEXT cancellation; [interface and arithmetic](receive_filter.md) |
| `receive_filter_pipeline.hpp` | Pipelined implementation with delayed adaptation and explicit latency |
| `receive_filter_datapath.hpp` | PHY-facing pipelined filter interface; coefficient test probes remain internal |
| `adc_sar_control.hpp`, `adc.hpp` | SAR controller and electrical sample/hold, ladder, preamplifier and comparator; [clock schedule and feedback test](adc.md) |
| `adc_core.hpp`, `adc_receive.hpp` | [Electrical core-domain decision interfaces and composed calibrated receive stream](adc_receive.md) |
| `receive_frontend.hpp` | [MOS differential amplifier, shared source follower and core-domain ADC bank](receive_frontend.md); coupled acquisition qualification in progress |
| `receive_normalizer.hpp` | [Programmable ADC gain/offset normalization](receive_normalizer.md) into the PCS sample convention |
| `receive_calibration.hpp` | [Measured training gain and offset](receive_calibration.md), with balanced-label checks and sequential hardware division |
| `receive_gain_control.hpp` | [Coarse analog gain acquisition](receive_gain_control.md), with settling, bounded sample windows and training-only changes |
| `receive_range_calibration.hpp` | [Measured-range normalization](receive_range_calibration.md), using a sixteen-clock divider without requiring five labeled training levels |
| `receive_acquisition.hpp` | [Connected gain-acquisition and range-calibration handoff](receive_acquisition.md); freezes gain before releasing the calibrated stream |
| `IHPSG13G2/programmable_receive_frontend.hpp` | [Fixed-termination buffered receiver with one programmable gain stage](IHPSG13G2/programmable_receive_frontend.md); all five single-slice generic flows and their waveform comparisons pass; sixteen-slice loading is under qualification |
| `pam5_driver_control.hpp` | [Registered main/history unary controls](pam5_driver_control.md) and their physical implementation; [weighted output stage](IHPSG13G2/emphasized_driver.md) |
| `pma_test_pattern.hpp` | [Registered PMA transmitter test symbols](pma_test_pattern.md), with independent native/RTL sequence checks |
| `pma_transmit_lane.hpp` | [Shared normal/test steering path](pma_transmit_lane.md), with explicit PCS polarity and history reset at stream boundaries |
| `IHPSG13G2/core_transmit_driver.hpp` | [Sixteen loaded voltage translators and the weighted output stage](IHPSG13G2/core_transmit_driver.md); generic independent flows/comparison and nominal PSP pass |
| `timing_recovery.hpp` | Sampled Gardner timing-loop reference |
| `receive_timing.hpp` | Fixed-point C++RTL timing controller; noisy closed-loop native/RTL/IHP gate tests; [interface and boundary](timing.md) |
| `receive_alignment.hpp` | Hardware GMII crossing from a recovered clock to the reference domain, with frame buffering and fault containment |
| `pair_acquisition.hpp`, `pma_aligned.hpp` | [Cable pair identification/deskew and aligned PMA/PCS](pair_acquisition.md) |
| `flp_frontend.hpp` | [MOS link-pulse driver and either-polarity detector](flp_frontend.md) |
| `pcs.hpp` | Hardware normal/control symbol mapper and master/slave LFSRs |
| `flp_transmit.hpp`, `flp_receive.hpp`, `master_slave.hpp`, `negotiation.hpp`, `autonegotiation.hpp` | Digital link-pulse timing, role resolution, page exchange/retry and composed negotiation; [interfaces and validation boundaries](negotiation.md) |
| `receive_clock_oscillator.hpp` | Two physical R/2R DACs and MOS ring actuator; separate tuning/load/refinement and solver comparisons |
| `reference_clock.hpp` | [Physical 125-to-250 MHz reference PLL](reference_clock.md), with MOS divider/PFD, finite charge paths and passive filter |
| `reference_clock_domain.hpp` | [Connected PLL and 3.3 V clock delivery](reference_clock_domain.md), including translator loading and achieved-edge controller checks |
| `clock_level_shifter.hpp` | Derived cascode clock interface with explicit output-charge correction; [circuit and pending qualification](../basic/cascoded_level_shifter.md) |
| `pcs_transmit.hpp` | GMII scrambler, convolutional encoder, delimiters and composed PAM5 transmitter |
| `pcs_synchronize.hpp` | Unseeded receive scrambler and polarity acquisition |
| `pcs_framer.hpp` | Receive framing, control tags, error containment and re-acquisition requests |
| `pcs_receive.hpp` | Soft branch metrics, eight-state Viterbi decoding and terminated tail buffering |
| `pcs_receiver.hpp` | Composed ADC-sample-to-GMII receiver with aligned masks and metadata |
| `pma_control.hpp`, `pma_endpoint.hpp` | Startup, receiver-readiness exchange, mode control and connected TX/RX PCS; [interfaces and current tests](pma_control.md) |

All electrical modules derive from `IC`, declare member ports and define purposes
line by line in `_assign()`. They share actual nodes: changing another device's
load changes the solution. DSP reference classes operate on samples and are
identified separately; they are not hidden electrical primitives.

```cpp
#include <1G/phy.hpp>
using namespace analog;
using namespace analog::one_g;

simulation sim;
electrical vdd, vss, vcm, vref, clock;
sim.reference(vss);
vdd = sim_voltage_source(vss, voltage(3.3));
vcm = sim_voltage_source(vss, voltage(1.65));
vref = sim_voltage_source(vss, voltage(.6));
// The bench supplies a 1.2 V boundary clock and thermometer controls.
analog::one_g::link cable_link;
cable_link.Vdd = vdd;
cable_link.Vss = vss;
cable_link.VCM = vcm;
cable_link.VREF = vref;
cable_link.CLOCK = clock;
// Connect A[pair][bit] and B[pair][bit] before elaborate().
// OUTA[pair] receives B; OUTB[pair] receives A, while both transmitters remain active.
```

Read [PAM5 and transmit shaping](pam5.md), [physical topology](phy.md),
[sampling and adaptation](adaptation.md), and [timing boundaries](timing.md)
before modifying an IC or interpreting a test result.

## Build and test

Use the existing [OpenVAF/ngspice toolchain](../VerilogA/toolchain.md):

```bash
cd ~/cpphdl
cmake -S analog -B analog/build -DCMAKE_BUILD_TYPE=Release \
    -DCPPHDL_ANALOG_VERILOGA_TESTS=ON \
    -DOPENVAF_EXECUTABLE="$PWD/analog/out/veriloga-toolchain/openvaf-compatible/openvaf" \
    -DNGSPICE_EXECUTABLE="$PWD/analog/out/veriloga-toolchain/install/bin/ngspice"
cmake --build analog/build --target generate-1g-veriloga -j 2
cmake --build analog/build --target generate-1g-rtl
cmake --build analog/build --target check-1g -j 2
```

Each electrical case runs in native C++RTL and independently compiled
Verilog-A/ngspice. Both outputs pass the same absolute checker separately before
waveform and trained-coefficient comparison. The mixed system runs the same C++
sampled-DSP implementation separately on each solver's actual waveform.
This tests the analog-backend boundary; it does not claim a Verilog-A hardware
implementation of that sampled DSP. Pure DSP algorithms have separate unit tests.
The transient deck places unchanged capacitor laws in native SPICE stamps;
the reusable Verilog-A exports retain them. See [numerical conditions](timing.md).

The IHP-specific driver adds PSP-based transistor tests when the prepared PDK is
available. Its physical compact model differs from the generic square-law model,
so it passes the common output-level contract separately, without an invalid
same-equation solver-equivalence claim.

The [PCS hardware](pcs.md) and [receive clock alignment](receive_alignment.md)
also run in native C++, generated SystemVerilog and mapped IHP gates. Each
implementation passes the same absolute checks, followed by exact output-trace
comparison. Run them with `ctest --test-dir analog/build -L hardware
--output-on-failure -j 1` after building the targets through `check-1g`.
IHP gate simulation is enabled when the prepared
repeater toolchain and standard-cell library exist.

The new [MOS-biased drivers](IHPSG13G2/biased_driver.md) replace the ideal tail
bias with a resistor/diode MOS reference and optional six-bit PMOS-switched
resistor bank. Native and Verilog-A equations are compared; separate actual-PSP
tests cover nominal fixed bias and externally trimmed TT/SS/FF process corners.
These schematic blocks do not yet supply a layout-ready complete PHY macro.

## Primary references

- [IEEE Clause 40 machine-readable extracts](https://www.ieee802.org/3/publication/index.html),
  specifically [Table 40-1](https://www.ieee802.org/3/publication/ab/table40_1.txt)
  and [Table 40-2](https://www.ieee802.org/3/publication/ab/table40_2.txt): normal-data mapping facts in `mapping.hpp`.
- [IEEE 802.3ab development tutorial](https://www.ieee802.org/3/tutorial/march98/mick_170398.pdf):
  architecture and partial-response transmit filter. This is historical design
  material, not a substitute for the current normative standard.
- [TI Gigabit Ethernet design reference](https://www.ti.com/lit/ug/tidu832b/tidu832b.pdf):
  four-pair, 125 MBd PAM5 operation, isolation magnetics and differential termination.

Source material checked 2026-10-17. No waveform in this prototype is asserted to
meet a normative transmit template or the complete 100 m cable specification.

[Analog gain acquisition](receive_gain_control.md) provides a five-setting training controller and independent native/RTL/mapped regression flows.

# Generated 1G models

The `.va` files are exports of the electrical ICs listed in [the module guide](../README.md).
Process-specific electrical exports are in [`../IHPSG13G2/generated/`](../IHPSG13G2/generated/).
The `.sv` files describe the separate digital hardware blocks:

| Model | C++ source | Clocks |
| --- | --- | --- |
| `ReceiveClockAligner.sv` | `receive_alignment.hpp` | `ref_clk`, `rx_clk` |
| `PMATestPattern.sv` | `pma_test_pattern.hpp` | `clk` |
| `PMATransmitLane.sv` | `pma_transmit_lane.hpp` | `clk` |
| `PCSSymbolMapper.sv` | `pcs.hpp` | `clk` |
| `PCSSideStream.sv` | `pcs.hpp` | `clk` |
| `PCSTransmitBits.sv`, `PCSTransmitter.sv` | `pcs_transmit.hpp` | `clk` |
| `PCSReceiveSynchronizer.sv` | `pcs_synchronize.hpp` | `clk` |
| `PCSReceiveFramer.sv` | `pcs_framer.hpp` | `clk` |
| `PCSBranchMetrics.sv`, `PCSTrellisDecoder.sv` | `pcs_receive.hpp` | `clk` |
| `PipelinedPCSBranchMetrics.sv` | `pcs_branch_metrics_pipeline.hpp` | `clk` |
| `PCSReceiver.sv` | `pcs_receiver.hpp` | `clk` |
| `ReceiveAdaptiveFilter.sv` | `receive_filter.hpp` | `clk` |
| `PipelinedReceiveFilter.sv` | `receive_filter_pipeline.hpp` | `clk` |
| `ReceiveFilter.sv` | `receive_filter_datapath.hpp` | `clk`; child `PipelinedReceiveFilter` |
| `ReceiveTimingLoop.sv` | `receive_timing.hpp` | `clk` |
| `ADCSARController.sv` | `adc_sar_control.hpp` | `clk`, 250 MHz target |
| `InterleavedSARController.sv` | `adc_interleave.hpp` | `clk`, sixteen slices; 250 MHz receive target |
| `ReceiveSampleNormalizer.sv` | `receive_normalizer.hpp` | `clk`, eight normalization stages at 250 MHz |
| `ReceiveGainCalibration.sv` | `receive_calibration.hpp` | `clk`, balanced PAM5 training, 125 MHz implementation target |
| `ADCReceiveController.sv` | `adc_receive.hpp` | `clk`, SAR control plus calibrated samples at 250 MHz |
| `FLPTransmitter.sv`, `FLPReceiver.sv` | `flp_transmit.hpp`, `flp_receive.hpp` | `clk`, 125 MHz timing contract |
| `MasterSlaveResolver.sv` | `master_slave.hpp` | `clk` |
| `Autonegotiator.sv` | `negotiation.hpp` | `clk`; millisecond enable input |
| `ReferenceMillisecond.sv`, `Autonegotiation.sv` | `autonegotiation.hpp` | `clk`, 125 MHz reference |
| `PMAControl.sv`, `PMAEndpoint.sv` | `pma_control.hpp`, `pma_endpoint.hpp` | `clk`, 125 MHz symbol domain |
| `ReceivePairAcquirer.sv`, `AlignedPMAEndpoint.sv` | `pair_acquisition.hpp`, `pma_aligned.hpp` | `clk`, 125 MHz symbol domain |

`OneGSARADCFrontend.va` is the electrical sampler/ladder/comparator slice. It
requires controller gate voltages at its external ports; it contains no digital
sequencer or clock generator. See [ADC integration](../adc.md).

`OneGSARADCFeedbackFrontend.va` adds the electrical decision-hold latch.
`OneGHighRateSARADCFeedbackFrontend.va` widens the sampling devices and their
explicit coupling capacitances. `OneGSARADCArray.va` instantiates sixteen such
frontends on shared input and supply nets. They still require connected control
and reference circuits; exporting them does not qualify interleaved throughput.

`OneGSARADCCoreArray.va` adds sixteen transistor level-down circuits, one common
`Vlogic` rail and achieved `CORE_DECISION` ports. The 3.3 V latches and these
interfaces remain electrically coupled. `ADCReceiveController.sv` consumes
resolved core decisions; it depends on `ADCSARController`,
`InterleavedSARController` and `ReceiveSampleNormalizer`. See
[the connected receive interface](../adc_receive.md) for timing and test scope.

`OneGPAM5ReceiveFrontend.va` connects the amplifier's positive drain through a
shared NMOS follower to sixteen ADC slices and their core-domain interfaces.
Each slice's acquisition and trial gates remain external electrical ports.
See [coupled front-end acquisition](../receive_frontend.md); generation is not
evidence of completed SAR/PCS or physical-layout integration.

`OneGFLPPulseDriver.va` and `OneGFLPPulseDetector.va` export the MOS link-pulse
interface. See the [pulse contract and independent-flow tests](../flp_frontend.md).

`OneGReceiveClockOscillator.va` exports the coarse/fine DAC and MOS ring circuit.
Its gate controls are external electrical ports. See [receive timing](../timing.md)
for the distinction between the controller, actuator and connected timing loop.

`OneGReferenceClockMultiplier.va` exports the MOS PFD/divider, finite charge
paths, passive filter and ring for the local 125-to-250 MHz reference path.
See [reference clock](../reference_clock.md) for its load and startup contract.

Compile `Predef_pkg.sv` before digital modules. Compile child modules before their
parents; `PCSReceiver` uses the synchronizer, framer, metrics and trellis modules;
`PCSTransmitter` uses the bit encoder and mapper. These are PHY building blocks;
startup, physical receive circuits and full-chip qualification remain open.
See [PCS](../pcs.md), [clock alignment](../receive_alignment.md),
[fixed-point receive filtering](../receive_filter.md), and
[verification results](../tests/hardware-validation.md) for interface and scope.

Regenerate from the repository root:

```bash
cmake --build analog/build --target generate-1g-veriloga
cmake --build analog/build --target generate-1g-rtl
```

The electrical catalog regression regenerates and compares all `.va` files.
`analog.1g.generated_rtl` checks all SystemVerilog exports. Digital regressions
independently regenerate RTL, execute the actual C++ tests
against it, and compare native, RTL and available IHP mapped-gate trajectories.

`IHPSG13G2/generated/OneGIHPPreciseWidebandReceiveFrontend.va` is the 4 pF
wideband receiver candidate. Its four-slice IHP PSP test fails settling and one
conversion accuracy check; export freshness does not qualify the circuit. See
[the retained measurements](../IHPSG13G2/precise_wideband_receive_frontend.md).

`IHPSG13G2/generated/OneGIHPParallelReceiveFrontend.va` adds a physical second
amplifier/follower path with 2 pF sampling storage. One/four/sixteen-slice generic
conversion/comparison and sustained sixteen-slice PSP conversion pass;
physical-layout qualification remains open. See [its circuit and tests](../IHPSG13G2/parallel_receive_frontend.md).

`IHPSG13G2/generated/OneGIHPBalancedReceiveFrontend.va` is the maintained
3 pF, parallel-drive receiver. Its one/four/sixteen-slice generic conversion and
comparison checks and sustained sixteen-slice PSP conversion pass. Complete
physical qualification remains in progress. See [its circuit and tests](../IHPSG13G2/balanced_receive_frontend.md).

`IHPSG13G2/generated/OneGIHPProgrammableReceiveGain.va` exposes five active-low physical select ports. All five nominal C++RTL, Verilog-A and PSP circuit tests pass; see [the measured gains and scope](../IHPSG13G2/programmable_receive_gain.md).

`IHPSG13G2/generated/OneGIHPProgrammableReceiveFrontend.va` exports the
sixteen-slice programmable receiver with 1.5 pF sampling storage and 1 pF
base gain-stage compensation. Its slow/hot high-gain single-slice check fails;
see [the receiver qualification](../IHPSG13G2/programmable_receive_frontend.md).

`IHPSG13G2/generated/OneGIHPWidebandProgrammableReceiveFrontend.va` exports
its inherited variant with 0.25 pF compensation. Connected generic and process
regressions are under qualification. See [the class and tests](../IHPSG13G2/wideband_programmable_receive_frontend.md).

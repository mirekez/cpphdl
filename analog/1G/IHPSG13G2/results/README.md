# Current 1G physical layouts

One combined GDSII contains the current available physical blocks, including failed and unfinished candidates.
**This is a development assembly, not an electrically integrated or fabrication-qualified repeater.**
The merge adds no signal, clock or power connections between blocks. Separate clock alternatives and standalone constituent blocks are included for review.

- [Combined GDSII](repeater_development.gds.gz): open `RepeaterDevelopmentAssembly` in KLayout.
- [Layer overview](repeater_layers.png): actual drawing-layer masks, regenerated from that GDSII.
- [Provenance and recorded checks](development_provenance.json): source hashes, status, placements and namespace mapping.

![Current layout layers](repeater_layers.png)

## Current contents

The receiver export uses the latest completed pin-repair candidate, even though its timing screen fails. Global-route guides are not converted into metal: only geometry actually present in the OpenROAD checkpoint is exported. The receiver therefore contains placement/CTS/power geometry and still needs detailed signal routing.
The PCS includes soft branch metrics and the trellis decoder. The receive equalizer/echo-canceller DSP, complete ADC/AFE and complete transmitter do not yet have integrated layouts in this package; their separate available physical constituents are listed below.
The old forwarding core and padframe have no newer physical revisions, so their latest available October 24 geometry is retained. The padframe opening is too small for this collection. The combined bounding box is a review arrangement, not a proposed die size.

| ID / cell in combined GDS | Current module view | Recorded state |
| --- | --- | --- |
| 01 / `B00_PCSReceiver` | [Receiver PCS / current placed checkpoint](receiver_pcs.png) | Placement + CTS; signal detailed routing pending. Timing margin screen fails. |
| 02 / `B01_FlowControlledRepeater` | [Flow control / SRAM assembly candidate](flow_control.png) | Routed block with SRAM well repair; candidate, DRC/LVS qualification incomplete. |
| 03 / `B02_ReceiveCommonReference` | [Receive common reference / amplifier repair](receive_reference_current.png) | Recorded local DRC + LVS pass; unfilled, electrical qualification incomplete. |
| 04 / `B03_ReceiveSampler` | [Receive sample-and-hold](receive_sampler.png) | Clock-shield separation candidate; recorded geometry test pass, electrical work open. |
| 05 / `B04_TransmitBias` | [Transmit bias / reference access](transmit_bias.png) | Recorded geometry receipt; electrical qualification and full-chip integration open. |
| 06 / `B05_TransmitTranslationBias` | [Transmit translation bias / replica](transmit_replica.png) | MOS storage candidate; physical block, full electrical acceptance incomplete. |
| 07 / `B06_HVStorageAdapter` | [High-voltage storage adapter](storage_adapter.png) | Recorded geometry test; standalone storage-access block. |
| 08 / `B07_TransmitTranslator` | [Transmit level translator](transmit_translator.png) | PSP layout candidate; recorded local checks, system qualification incomplete. |
| 09 / `B08_TransmitCell` | [Transmit main current cell](transmit_main.png) | Current-cell geometry; standalone device array, system qualification incomplete. |
| 10 / `B09_TransmitCell` | [Transmit history current cell](transmit_history.png) | Current-cell geometry; standalone device array, system qualification incomplete. |
| 11 / `B10_TransmitPullup` | [Transmit pullup](transmit_pullup.png) | Standalone pullup geometry; full transmitter integration incomplete. |
| 12 / `B11_TransmitMainChargeStorage` | [Transmit main charge storage](transmit_main_storage.png) | Standalone intended storage, not extracted parasitic capacitance. |
| 13 / `B12_TransmitHistoryChargeStorage` | [Transmit history charge storage](transmit_history_storage.png) | Standalone intended storage, not extracted parasitic capacitance. |
| 14 / `B13_ReferenceClockGenerator` | [Reference clock generator / hierarchical layout](reference_clock.png) | Recorded hierarchy/LVS checks; connected-system electrical qualification open. |
| 15 / `B14_RealizedReferenceClock` | [Reference clock / realized-loop candidate](realized_clock.png) | Unfilled realized-loop layout; recorded local checks, electrical acceptance open. |
| 16 / `B15_ProcessingClockOscillator` | [Processing clock oscillator](processing_oscillator.png) | Narrow-layout candidate; standalone oscillator, integrated clock tree unfinished. |
| 17 / `B16_ProcessingClockGate` | [Processing clock gate](processing_gate.png) | Recorded placement proof; clock distribution integration unfinished. |
| 18 / `B17_ProcessingClockControl` | [Processing clock controller / drift revision](processing_control_current.png) | Recorded setup/hold +0.0182/+0.0612 ns; DRC/LVS pass for this block only. |
| 19 / `B18_PHYResetControl` | [PHY reset controller](phy_reset.png) | Standalone physical reset controller; system CDC/reset qualification open. |
| 20 / `B19_RepeaterCore` | [Forwarding core / retained implementation](forwarding_core.png) | Historical physical block retained; not connected to the other blocks. |
| 21 / `B20_RepeaterPadframe` | [Padframe / retained implementation](padframe_current.png) | Historical physical block retained; not connected to the other blocks. |

## Per-module details

### Receiver PCS / current placed checkpoint

Current square-lookup revision, including branch metrics and trellis decoder. Nominal global-route estimate setup/hold +0.412071288/+0.123619340 ns; 1.75x capacitance estimate -0.661499321/+0.140293896 ns. GDS includes placed cells, physical clock/power wires present in the ODB; global-route guides are not signal metal. No receiver equalizer/echo-canceller DSP is included in this PCS block. Latest completed equivalent-input pin repair; enlarged-capacitance clock-pin limits also fail. This failed candidate is published as-is, without replacing the repair driver's retained checkpoint.

![Receiver PCS / current placed checkpoint](receiver_pcs.png)

### Flow control / SRAM assembly candidate

Latest row-well repair candidate. Preceding filled assembly had failing LVS. Current modified assembly has no completed full DRC/LVS qualification; no pass is inherited from the earlier revision.

![Flow control / SRAM assembly candidate](flow_control.png)

### Receive common reference / amplifier repair

Latest amplifier-stack candidate. Recorded local geometry and LVS pass; density/fill and electrical acceptance remain separate.

![Receive common reference / amplifier repair](receive_reference_current.png)

### Receive sample-and-hold

Latest separated-clock-shield sampler candidate. Geometry receipt passes; not a complete ADC or receive AFE.

![Receive sample-and-hold](receive_sampler.png)

### Transmit bias / reference access

Latest maintained finger-access bias geometry. Saved checks are included as evidence, not rerun at publication.

![Transmit bias / reference access](transmit_bias.png)

### Transmit translation bias / replica

Latest replica MOS-storage physical candidate, separate from translator and line driver.

![Transmit translation bias / replica](transmit_replica.png)

### High-voltage storage adapter

Maintained connected adapter geometry; disconnected fault variants excluded.

![High-voltage storage adapter](storage_adapter.png)

### Transmit level translator

Latest maintained PSP translator geometry; standalone, not a connected complete transmitter.

![Transmit level translator](transmit_translator.png)

### Transmit main current cell

Maintained current-cell geometry with intentional device connectivity; no complete MDI driver assembly.

![Transmit main current cell](transmit_main.png)

### Transmit history current cell

Maintained current-cell geometry with intentional device connectivity; no complete MDI driver assembly.

![Transmit history current cell](transmit_history.png)

### Transmit pullup

Maintained pullup layout. System behavior is not established by this physical snapshot.

![Transmit pullup](transmit_pullup.png)

### Transmit main charge storage

Latest transmitter storage candidate; a constituent block, not a complete transmitter.

![Transmit main charge storage](transmit_main_storage.png)

### Transmit history charge storage

Latest transmitter storage candidate; a constituent block, not a complete transmitter.

![Transmit history charge storage](transmit_history_storage.png)

### Reference clock generator / hierarchical layout

Latest completed hierarchical placement-v2 generator. Distinct from the realized-loop alternative; both retained for review.

![Reference clock generator / hierarchical layout](reference_clock.png)

### Reference clock / realized-loop candidate

Latest realized clock filter candidate. An alternative clock macro; not connected in series with the reference-generator alternative.

![Reference clock / realized-loop candidate](realized_clock.png)

### Processing clock oscillator

Latest completed narrow processing oscillator layout. Published independently for inspection; not a signoff claim.

![Processing clock oscillator](processing_oscillator.png)

### Processing clock gate

Latest completed gate placement-proof layout; system clock distribution and reset crossing remain open.

![Processing clock gate](processing_gate.png)

### Processing clock controller / drift revision

Latest clock-drift controller revision. Recorded standalone nominal extracted timing +0.018183399/+0.061225358 ns, full-deck DRC zero violations, LVS 48 circuits/40 ports/20540 devices. Packaging does not rerun these checks.

![Processing clock controller / drift revision](processing_control_current.png)

### PHY reset controller

Latest available filled reset-control layout; not the entire PHY or its clock tree.

![PHY reset controller](phy_reset.png)

### Forwarding core / retained implementation

Most recent available standalone geometry is the October 24 revision. Padframe opening does not fit the current receiver hardware; review arrangement is not a chip floorplan.

![Forwarding core / retained implementation](forwarding_core.png)

### Padframe / retained implementation

Most recent available standalone geometry is the October 24 revision. Padframe opening does not fit the current receiver hardware; review arrangement is not a chip floorplan.

![Padframe / retained implementation](padframe_current.png)

## What publication checks

The publisher checks source hashes, rejects missing or empty tops, namespaces cells, preserves each block's hierarchical geometry through merge and compressed-GDS round trip, and checks that block bounding boxes do not overlap. Saved verification statuses are retained, including failures; packaging does not rerun electrical simulation, timing, DRC or LVS. A historical passing block receipt does not qualify the assembled layout.

Only the combined GDS, current module/overview PNGs, compact provenance and license are published. Build checkpoints, generated HDL and raw logs remain outside Git.

## Regeneration

From `analog/`, with the original checkpoints listed in the provenance available:

```sh
PYTHONPATH=out/pdf-renderer pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/package_development_layout.py --selection 1G/IHPSG13G2/results/development_provenance.json
ctest --test-dir build/behavior -R '^analog.pdk.1g_repeater.package_development_layout$' --output-on-failure
```

The selection records the source checkpoint and producer receipt for every block. This command republishes existing checkpoints; it does not recreate the PnR runs. The retained forwarding-core/padframe inputs and their historical provenance can be recovered from commit `ea6cf21`, under `analog/1G/IHPSG13G2/results/`, into `build/layout-publication/retained-inputs/`.

The earlier standalone GDS files and images remain available in Git history. Their current available geometry is included here.

## Attribution

Embedded IHP PDK cell geometry is distributed under [Apache-2.0](IHP-LICENSE.txt). Custom circuit layout, assembly and publication tooling are cpphdl project work.

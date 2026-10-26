# Physical Ethernet PAUSE datapath

`FlowControlledRepeater` connects two independent receive domains to two transmit
domains. Each direction stores 4096 eleven-bit entries in four real
`RM_IHPSG13_2P_1024x16_c2_bm_bist` macros. There are eight SRAMs in total. The
eleventh bit marks the end of a frame; unused physical data bits are tied off.

The synchronous SRAM read first enters one register per bank, then bank selection
feeds a four-entry prefetch queue. Space is reserved for both outstanding read
stages. This keeps the SRAM-to-register path local and permits backpressure
without discarding an outstanding read. PAUSE stops the start of a frame; it
does not insert idle cycles into a frame already being transmitted.

A separate register holds conservative read credits. It decodes only the
second synchronized write-pointer stage and deducts the read issued on that
edge. Newly observed writes become available one edge later. This removes the
Gray decoder and pointer subtraction from the queue-consumption decision while
preserving the rule that no read may consume an unobserved write.
Read issue uses the reserved occupancy before the current edge's consumption.
A full queue waits one edge before reusing a freed slot; three reserved words
then sustain one word per cycle through the two-stage read pipeline. This
removes frame/PAUSE decisions from the read-credit feedback path. The pressure
fixture now records 5225 paused symbols and 2193433 matching native/RTL events;
its byte, frame, stopped-clock reset and fault/recovery checks are unchanged.

`flow_control_synthesis.py` maps the SRAM-qualified RTL to IHP cells and repeats
the independent frame/PAUSE checks using the PDK memory model. The physical flow
then proceeds through these scripts:

1. `flow_control_physical.py`: place all eight SRAMs and the logic, repair signal
   placement before clock-tree synthesis, and build obstruction-aware clock
   trees. The timing corners include both SRAM and standard-cell libraries.
2. `flow_control_route.py`: global and detailed routing, wire extraction,
   slow/fast setup and hold analysis, and exact replay of the routed netlist.
   The nominal-RC route passed extracted setup (+0.188799202 ns) and hold
   (+0.011086354 ns), but failed slew/capacitance/fanout limits. The current
   repair candidate uses three times the nominal wire capacitance during optimization,
   permits setup buffer removal, limits clock wires to 200 um and repairs
   antenna-diode loads. `flow_control_clock_loads.tcl` splits overloaded clock
   branches into two local non-inverting branches. It preserves buffer leaves
   inverter leaves and directly attached register clock inputs; data and reset pins are not
   eligible register loads. The flow reroutes and repeats hold and electrical
   repair after each split. It restores nominal RC before the hold check.
   Finishing repair targets 0.26 ns slew and 60 fF capacitance to leave margin
   for the extracted loads. Acceptance still uses 0.30 ns, 80 fF and fanout 8.
   The preceding two-times candidate passed extracted setup and hold but
   failed 36 slew and 180 capacitance entries despite passing global estimates.
   A global
   timing/electrical failure stops the flow before detailed routing. Independent
   OpenRCX extraction and routed replay remain required; these repair settings
   are not yet physically qualified. The latest extracted route has positive
   setup/hold slack and only two capacitance failures. An extracted-load repair
   pass now splits overloaded clock branches and point-to-point signal wires
   with non-inverting buffers. It repeats placement, hold repair, antenna
   repair, detailed routing and extraction. Up to two passes are allowed;
   unresolved violations fail the original final checks. Each pre-repair
   database, SPEF, netlist and timing/electrical report is retained with hashes.
   The earlier 2.5-times capacitance trial
   with restricted setup repair is retained as a failed attempt.
3. `flow_control_crossings.py`: separately check the four thirteen-bit FIFO Gray
   buses, two sixteen-bit PAUSE payloads and sixteen control/acknowledgement
   crossings using the extracted parasitics. It deliberately does not load the
   asynchronous clock-group cuts from the synchronous timing analysis.
4. `flow_control_verify.py`: stream the actual SRAM and standard-cell GDS,
   add fill, and run the full IHP DRC and transistor LVS decks.

The crossing audit resolves each semantic state bit through the exact synthesized
netlist. This is necessary because streaming the netlist into OpenROAD discards
some redundant register-net aliases. Missing registers, a changed clock domain,
an absent timing path or an incomplete corner/bit matrix fail the audit.

The crossing budget is 2 ns of data delay and 500 ps of source-clock
insertion spread within each corner. Their conservative sum is below both the
7.353 ns receive and 8 ns transmit source periods. A shared slow/fast latency
change is not counted as inter-bit skew. For each PAUSE payload, the audit also
checks clock insertion relative to its request register. The payload remains
held until acknowledgement; capture follows the two request synchronizer stages.
The 2.5 ns conservative arrival window fits within even one 8 ns destination
period. These checks establish data-delay budgets, not synchronizer MTBF,
asynchronous-reset recovery/removal or safe configuration changes.

From `analog/`, after obtaining a passing route receipt:

```sh
pdk/IHPSG13G2/.cpphdl/venv/bin/python 1G/IHPSG13G2/flow_control_crossings.py \
    --routed build/clock-integration/flow-control-route/checks.json \
    --cells build/1G/tests/hardware/flow_control_cells/checks.json \
    --pdk pdk/IHPSG13G2 \
    --openroad out/repeater-toolchain/bin/openroad \
    --yosys out/repeater-toolchain/bin/yosys \
    --work build/clock-integration/flow-control-crossings
```

`analog.1g.ihp_flow_control.crossing_guards` exercises rejection of incomplete
paths, wrong domains, excessive delay/skew and invalid state bindings. Set the
CMake cache paths `CPPHDL_1G_FLOW_CONTROL_ROUTED_PROOF` and
`CPPHDL_1G_FLOW_CONTROL_CELL_PROOF` to register the actual extracted audit as
`analog.1g.ihp_flow_control.crossings`.

`analog.1g.ihp_flow_control.clock_loads` exercises the clock-branch repair with
actual IHP cells in OpenROAD. It checks both buffer-only branches and a branch
with a directly attached register clock and an inverter leaf, preserves data/reset connections,
and verifies every moved load's non-inverting source. It also checks that an
empty violation report leaves the topology alone. It also verifies that
`flow_control_signal_loads.tcl` divides an overloaded point-to-point wire,
places the buffer between its endpoints and preserves every unrelated pin.
This connectivity regression
does not replace timing or electrical analysis of the repaired route.

Each runner writes a fresh directory and a `checks.json` receipt with input
hashes. A placement-only analysis is not an extracted crossing result. This
macro's qualification also does not cover MDI-to-PAUSE reaction latency,
post-fill coupling, full-chip power integrity or the complete copper PHY.

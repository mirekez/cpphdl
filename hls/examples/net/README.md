# Ethernet / SBE / OUCH pipeline example

## Pipeline structure

`hft.cpp` uses three `cpphdl::hls::ClockedPipeline` instances, not
`ClockedDelayer`:

- `HftWordMethods` checks Ethernet, IPv4, UDP and SBE headers for each
  independent 32-bit word. Frame position and framing flags travel with the word.
- `HftDecisionMethods` selects a buy or sell price from a validated quote.
- `HftTxMethods` calculates an order's TCP checksum and generates its output
  words from explicit offsets. It does not advance a hidden cursor per call.

`HFT_PIPELINE_STAGES` selects the initial depth (default four).
The RTL regression also runs with eight stages. Each pipeline can accept one
command per clock when its output is not stalled.

```text
RX -> ingress register -> word/header pipeline -> collector -> decision pipeline
                                                                  |
                                                         four-order FIFO
                                                                  |
TX <- CRC/FCS insertion <- word formatter pipeline <- command register
```

The collector retains only six application fields, running checksums and
validation/sequence state. No complete packet is stored. Short per-word
recurrences (frame position, CRC and Internet checksum accumulation) remain
explicit in the RTL wrapper: simply placing them inside a floating-feedback
pipeline would let consecutive words consume stale state.

`HftCollector.h` separates the former combinational collector into five
registered stages: prepare checksum contributions, accumulate and capture a
frame snapshot, fold checksums, validate the quote, and filter its sequence.
Each stage carries the matching data, valid bit and error flags. Backpressure
freezes these stages together. Only accumulation and sequence filtering feed
their own state back; both complete in one clock.

The ingress register separates framing calculations from HLS header parsing.
The TX command register separates FIFO selection from HLS word formatting.
Both can accept a replacement word on the same edge that consumes the old one.

The TX object first accepts one order and commits it. The wrapper then issues
explicit offsets 0, 4, ..., 108 on consecutive clocks. It waits for that frame's
EOF before loading another order. This avoids overlapping order-state updates
with reads for the previous frame. There are no internal bubbles within a TX
frame when the consumer remains ready, but a load/drain gap remains between
frames. This is not a claim of minimum Ethernet interpacket spacing.

## 315 MHz Timing Estimate

The target is **3.174603175 ns**, corresponding to 315 MHz and 10.08 Gbit/s
of raw 32-bit interface capacity.

The native model, ordinary Verilator models at four and eight stages, and the
baseline and retimed mapped-gate models pass the packet oracle. The test also supplies
20,000 consecutive RX words without input stalls and checks bubble-free output
within frames. Most quotes in that throughput test deliberately generate no
order: a 110-byte response cannot be emitted for every 78-byte request
indefinitely at the same word rate.

Full-design `fit_pipeline_retiming` now reports **3.15 ns** against the
3.174603175 ns target. The former 8.32 ns collector/validation path was
split manually outside the HLS regions. Word-wide CRC and parallel-carry
arithmetic keep the per-word feedback paths within one clock; no timing
limit was relaxed.

For the default four-stage HLS configuration, retiming produces:

| HLS region | Retimed latency, clocks | Initiation interval |
| --- | ---: | ---: |
| RX header checks | 12 | 1 |
| Trading decision | 6 | 1 |
| TX word formatting | 30 | 1 |

Retiming inserts 30,155 register bits. These are pipeline registers, not packet
RAM. The seven manually added stages (ingress, five collector stages and TX
command) are outside these region latencies. Packet latency also includes word
arrival, queueing and the order-load handshake; it is not simply this table's
sum. Neither this estimate nor Verilator establishes physical timing closure.

```sh
build/cpphdl --synth --top Hft --module Hft \
  --retiming fit_pipeline_retiming --clock-period-ns 3.174603175 \
  --output /tmp/hft-synthesis hls/examples/net/hft.cpp
```

No Yosys or `--hls` flag is needed. The wrapper types select the schedulers
automatically; `--synth` additionally exports their graphs for mapping and retiming.

## Stream and packet contract

Both directions use rising-edge clocks, 32-bit data, valid/ready, SOF/EOF,
and byte count. A transfer occurs on valid AND ready. Data and framing must
remain stable during backpressure. Lane zero carries the first wire byte.
Non-final words contain four bytes; final words contain one to four bytes.
Invalid counts discard the partial frame; SOF starts a new frame.
Reset cancels all queued and in-flight work.

The stream includes Ethernet FCS, but not preamble/SFD. A connected MAC must
preserve RX FCS and must not append another TX FCS. RX backpressure eventually
propagates from the four-entry order FIFO through the decision and word
pipelines. A physical Ethernet receiver needs separate buffering and an
overflow policy because it cannot pause incoming frames.

Receive formats are deliberately restricted:

- Ethernet II multicast destination `01:00:5e:01:02:03`, IPv4.
- IPv4 destination `239.1.2.3`, 20-byte header, no options or fragmentation,
  nonzero TTL and valid header checksum.
- UDP destination 9000, length 40. Nonzero UDP checksums are verified;
  zero checksums are accepted for IPv4.
- One quote using [market.xml](market.xml): eight-byte little-endian SBE
  header (block length 24, template 1, schema 42, version 0), followed by
  sequence, instrument, bid, ask, bid quantity and ask quantity as uint32 values.

Instrument 1 represents TEST; prices are in units of 0.0001. For a fresh
sequence with `0 < bid < ask`, buy 100 shares if ask is below 100000 and
ask quantity is at least 100; otherwise sell 100 if bid exceeds 100020 and
bid quantity is at least 100. Other quotes produce no order. Valid fresh
quotes advance the sequence filter even without an order.

Responses contain an OUCH 4.2 Enter Order in a SoupBinTCP unsequenced-data
message and an Ethernet/IPv4/TCP envelope. The token is SIM000 plus eight
hexadecimal sequence digits. MACs are `02:00:00:00:00:01` to
`02:00:00:00:00:02`; IPs are `10.0.0.1` to `10.0.0.2`; ports are
40000 to 9001. TCP sequence starts at 1000 and advances by 52 per order.
This is an already-established mock session, not a production trading stack:
there is no ARP, TCP establishment/retransmission, exchange login, order
acknowledgment processing or risk management.

## CRC and errors

`EthernetCrc.h` processes full 32-bit words with a balanced parallel XOR
network; partial words use byte transforms. There is no clock-per-byte loop
or full-frame scan. TX appends FCS
including the mixed data/FCS word and partial final word.

`frame_size_error_out` pulses for frames outside 64..100 bytes.
`frame_crc_error_out` pulses for in-range frames with incorrect FCS.
Size errors take precedence. Invalid frames neither update trading sequence
state nor generate orders. Header/application fields are checked independently
of FCS and Internet checksums.

`ReceiveMetadata.h` remains a standalone feedback-retiming demonstration
used by `synth_feedback_frames`. Its multicycle ready/commit protocol is not
the HFT stream protocol.

## Regressions

```sh
cmake -S . -B build -DCPPHDL_BUILD_HLS_TESTS=ON
cmake --build build --target cpphdl hls_hft -j2
ctest --test-dir build -R '^(hls_hft_|synth_hls_hft$)' --output-on-failure
```

`HftTest.h` compares complete frames with an independent byte-oriented oracle.
It covers 1,000 random quotes, invalid headers, every FCS bit, corrupted data
lanes, all final-word lengths, sequence filtering, oversized/truncated frames,
concurrent RX/TX, FIFO saturation/wraparound, backpressure and resets with
active/queued work, including cancellation at each pipeline phase followed by
reuse of the same sequence number. CRC expectations use a separate bit-serial reference and
the standard 123456789 check vector. A further 1,000-frame run verifies
20,000 uninterrupted RX words and TX intra-frame throughput.
Direct helper checks also compare parallel carry/compare results against C++
arithmetic and exercise the CRC network with 4,096 random states/words at
each byte count from zero to four.

Generated SV and logs are under `build/hls/examples/net/rtl4/` and
`rtl8/`. Pipeline modules are named
`cpphdl_hls_ClockedPipelineHftWordMethods_P4.sv`,
`cpphdl_hls_ClockedPipelineHftDecisionMethods_P4.sv` and
`cpphdl_hls_ClockedPipelineHftTxMethods_P4.sv` for the default depth.

[generated/](generated/) contains the default four-stage SystemVerilog snapshot,
including `Hft.sv`, `HftCollector.sv`, all three HLS pipelines and their packages.
This is the ordinary HLS RTL before synthesis retiming; the synthesis regression
writes the 315 MHz retimed netlist to
`build/synth/tests/hls_hft/retimed/gates.v`.

With synthesis tests enabled, `synth_hls_hft` runs native, baseline gates and
315 MHz retiming. It checks three streaming regions with II=1 and uses the
same physical-clock testbench for gates. There is no adapter that waits many
physical clocks per input word. The retimed gate model must pass the same
packet, backpressure, reset and throughput checks as the baseline model.

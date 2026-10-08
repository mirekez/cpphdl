# Streaming Matrix-Vector Pipeline

`StreamingMatVec.cpp` computes the same full-precision Q16.48 matrix-vector
product as `TiledMatVec.cpp`, but removes two serialized dependencies: waiting
for each individual DDR read, and waiting for a row's reduction before issuing
the next row's products. It retains one multiplier lane, not eight replicated
HLS loaders or multipliers.

## Architecture

```text
512-bit ordered DDR responses
             |
   8 reserved FIFO slots <--- sequential reads (up to 8 outstanding)
             |
   beat register: eight 64-bit weights
             |                         activation memory
             +-------------------------------+
                                             |
                                  registered operand pair
                                             |
                                  ClockedPipeline<Product>
                                             |
                                  128-bit carry-save sum
                                             |
                            row-end sum/carry holding registers
                                             |
                                  ClockedPipeline<WideAdd>
                                             |
                              Q48 result + row + valid/ready
```

The C++ math methods in `IntegerMath.h` are unchanged. Each weight is multiplied
by its activation; products accumulate at 128-bit width, modulo 2^128, with one
rounding shift after the final addition. Separate issue, receive and output
counters track columns and rows. They advance on handshakes, not after a fixed
number of clocks, so retiming either arithmetic pipeline preserves row identity.
At the final product of a row, its sum and carry move into holding registers,
and the accumulator starts the next row on the following clock. The final adder
can accept the previous row at the same time. Depth one is supported and tested:
every product then finishes a row.

`WeightReadStream.h` is explicit C++ RTL. A request reserves a FIFO slot before
the controller returns data, preventing responses from overflowing storage when
the consumer stalls. Responses arrive in acceptance order and stay stable until
accepted. A separate beat register permits FIFO reads and unpacking to overlap;
row boundaries need not coincide with beat boundaries. There is no full-row
load barrier and no full-matrix cache.

This frontend intentionally does **not** use `ClockedMemory`: that wrapper still
allows one outstanding access. The source pointer-based example is retained in
`TiledMatVec.cpp`; automatic HLS conversion of its pointer loop into a wide,
multiple-outstanding prefetch engine is not implemented. Here the frontend is
RTL and both arithmetic pipelines are generated from C++ HLS methods. The native
test exercises the actual queued memory protocol for this frontend too.

## Contract and Capacity

- Activation and command/result ports follow `TiledMatVec`. Load activations
  while idle, then submit 1..255 rows. Commands do not overlap matrices.
- `LLM_TILE_DEPTH` selects 1..256 elements per row. `LLM_READ_WINDOW` selects a
  power-of-two queue capacity from 2 to 32, default 8.
- `weights_out` is `DramReadIf<512>`: 32-bit byte addresses, 64-byte aligned
  reads, ordered completions, no IDs, write channel or DDR PHY.
- Weights are contiguous, row-major 64-bit values. Pad the allocation through
  the last 64-byte beat. Unused final lanes are not multiplied.
- The base must be 64-byte aligned and at most `UINT32_MAX - 65280*8`.
  This conservative bound guarantees space for every supported matrix size.
- Eight slots use 512 bytes of FIFO storage, plus a 64-byte current-beat
  register. Activations use `8*LLM_TILE_DEPTH` bytes. Physical RAM inference
  depends on the target implementation; the generic gate mapper uses flops.
- Backpressure propagates through reduction, accumulation and products to the
  weight reader. Outstanding requests already own buffer space. No timeout
  guesses when a DDR response is ready.
- Errors latch `fault_out`, stop new work and require joint controller/DUT
  reset. A stalled request remains asserted until accepted even after an error.
  Rows already admitted to the final adder can still drain; software must treat
  the matrix as failed. Reset discards all remaining requests and results.

## Performance Investigation

Both implementations use depth eight and 255 rows: 2,040 products. The controller
accepts requests whenever capacity permits, returns ordered data after the stated
fixed latency, and the result consumer stays ready. Counts are from command
acceptance through the last result; activation upload is excluded. These are
**Verilator measurements of retimed gates**, not native wall time:

| DDR latency (clocks) | Tiled baseline | Streaming, 8 slots | Speedup |
|---|---:|---:|---:|
| 1 | 12,262 | 2,056 | 5.96x |
| 20 | 51,022 | 2,075 | 24.59x |
| 60 | 132,622 | 2,115 | 62.71x |
| 120 | 255,022 | 3,973 | 64.19x |

The baseline transfers 2,040 separate 64-bit words; streaming transfers 255
512-bit beats. Width alone does not explain the gain: ordinary RTL with only
two prefetch slots takes 7,954 clocks at 60-clock latency; eight slots take
2,112 clocks on the same 512-bit interface. Retiming adds three fill/drain clocks
but does not add gaps between products or rows.

At latencies 1, 20 and 60, all 2,040 product admissions are consecutive and row
results are exactly eight clocks apart. These are regression assertions, along
with a bounded fill/drain cost, not just printed statistics. At latency 120,
the finite window cannot keep the pipeline fed: the product span grows to 3,838
clocks. "Streaming" does not remove physical memory latency or output stalls.

At a 5 ns target, the whole-design generic timing estimate is 4.90 ns, with seven
product stages and four reduction stages. Retiming inserts 4,478 register bits.
The mapped design has 12,243 flip-flops and 69,290 combinational primitives,
versus 8,921 and 62,708 for the tiled baseline: approximately 37% more flip-flops
and 10.5% more combinational primitives, not a 63x hardware replication.
Counts are generic gates, **not** FPGA LUTs, and timing is not placement/routing
sign-off. At an assumed 200 MHz, the 60-clock-latency case takes 10.575 us instead
of 663.11 us. One lane consumes at most 1.6 GB/s of weights at that clock; it does
not saturate a 512-bit DDR interface. More compute lanes remain future work.

## Run and Verify

```sh
cmake --build build --target hls_llm_StreamingMatVec hls_llm_TiledMatVec
ctest --test-dir build -R '^hls_llm_(StreamingMatVec|TiledMatVec)' --output-on-failure

build/cpphdl --synth --top StreamingMatVec --module StreamingMatVec \
  --retiming fit_pipeline_retiming --clock-period-ns 5 \
  --output build/llm-streaming hls/examples/llm/StreamingMatVec.cpp
```

The suites check random signed matrices, extreme operands, output stalls, random
1..61-clock DDR delays, exact requests/products/results, no-reset reuse, reset
with reads pending, invalid commands and memory errors during backpressure.
Depths 1, 8 and 13 and prefetch windows 2 and 8 are covered; retimed gate tests
cover depths 1 and 8. Logs and gate netlists are under
`build/hls/examples/llm/StreamingMatVec_{rtl,gates}/`; timing and cell counts are
in `rtl/manifest.json`. Native/RTL tests at other depths do not establish their
retimed timing. Build the additional `hls_llm_StreamingMatVec_depth1`,
`hls_llm_StreamingMatVec_depth13` and `hls_llm_StreamingMatVec_window2` targets
before running their native CTest variants.
The ordinary RTL snapshot is in [`generated/streaming/`](generated/streaming/).

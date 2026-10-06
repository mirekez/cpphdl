# Integer Transformer: Reference and DDR Arithmetic

This directory starts the hardware port of the integer Qwen implementation in
`~/intllm/report.md`. **It is not yet a complete hardware transformer.** The
current RTL accepts matrix tiles or scalar operands, not an input token.
Transformer control, attention and normalization are still host C++.
Matrix products, their full-width accumulation, and final rounding now execute
in hardware. Nonlinear scalar functions have explicit external blackbox contracts.

## Implemented

- `Transformer.h`: ordinary C++ with separate methods for matrix-vector
  multiplication, RMSNorm, RoPE, causal grouped-query attention and the complete
  forward pass. It keeps a KV cache, uses SwiGLU, ties the embedding/output
  weights, and returns a greedy next-token ID and logits. No clock calls are
  embedded in this algorithm.
- `IntegerMath.h`: the original Q16.48 arithmetic, including full signed
  64-by-64-bit products. Dot products round only after 128-bit accumulation.
  This is not an INT8 approximation of the supplied model.
- `WeightProduct.cpp`: a `ClockedPipeline` product engine with a one-beat DDR
  read cache. The algorithm is simply `Wide(Q(lhs)) * Q(rhs)`. The pipeline
  accepts a command every clock on a cache hit when its output is not stalled.
- `MatrixMath.cpp`: DDR-backed `W[rows,depth] * X[depth,columns]`, with runtime
  dimensions bounded by configurable capacities (default 8). Activations are
  loaded into a local `memory<>` tile. A carry-save
  accumulator consumes full 128-bit products; a separate `ClockedPipeline`
  adder reduces the sum before the single Q48 rounding step. The controller
  waits for actual valid/ready responses, so added pipeline latency cannot
  cause it to use an unfinished sum. The controller is explicit RTL; automatic
  scheduling of the C++ matrix loops over DDR is not implemented.
- `ScalarMath.cpp`: pipelined add and Q48 multiply, plus calls to the external
  division, exponential, inverse-square-root and SiLU modules below.
- `TiledMatVec.cpp`: a pointer-based `ClockedMemory` weight loader, two local
  weight buffers and independent `ClockedPipeline` arithmetic. Loading the next
  row overlaps computation on the current row. See below.
- `StreamingMatVec.cpp`: the preferred throughput example. Eight outstanding
  512-bit reads feed a continuous product stream; completed row sums enter a
  separate reduction pipeline without waiting for the previous row's answer.
  See [Streaming Architecture and Performance](Streaming.md) for measured
  comparisons, the protocol, timing, hardware costs and limitations.
- `../../ExternalMemory.h`: pointer binding and a request/completion interface
  for delayed external reads and writes, plus the older wide-beat read interface
  used by `WeightProduct` and `MatrixMath`. Neither implements AXI or a DDR PHY.

`SmallModel` uses deterministic, untrained weights and small dimensions for
regressions. `QwenModel` records the real 896/4864/24-layer geometry, but has
**not** been synthesized or run through this hardware prototype.

## Overlapped Loader and Compute

`TiledMatVec` separates unpredictable DDR service time from arithmetic pipeline
latency. It computes `W[rows, LLM_TILE_DEPTH] * X[LLM_TILE_DEPTH]`, producing one
Q16.48 result per row. The default depth is eight; define `LLM_TILE_DEPTH` at
conversion time to choose 1..256. Runtime row count is 1..255.

```text
external controller <-> ClockedMemory<WeightLoader> (delayed pointer load)
                                   |
                         weight tile 0 / tile 1
                                   |
activation memory --------> registered operands
                                   |
                         ClockedPipeline<Product>
                                   |
                         128-bit carry-save sum
                                   |
                         ClockedPipeline<WideAdd>
                                   |
                       Q48 result + row + valid/ready
```

The loader algorithm remains ordinary C++:

```cpp
uint64_t command(uint32_t base, uint32_t index, uint32_t unused) {
    auto weights = cpphdl::hls::external_memory<const uint64_t>(base);
    return weights[index];
}
```

`ClockedMemory` selects the delayed scheduler with an external memory port. HLS lowers the
pointer access into a request, waits for the memory response, then returns the
weight. The top does not implement that memory transaction by hand. It connects
the loader's interface through `assignIf` and places each returned word in the
currently filling tile. There is one outstanding load, with 32-bit byte addresses
and 64-bit data; this version is not a burst or wide-beat DDR engine.

The two weight tiles and activation tile use `memory<>`; registered operand
reads separate local storage from the multiplier. Their physical implementation
as block RAM or distributed memory depends on the target mapper and capacities.
With depth eight, weights occupy 128 bytes and activations occupy 64 bytes.
No whole weight matrix is cached. Tile ownership is explicit:

1. The loader writes only a free tile and marks it full after the last response.
2. Compute reads only a full tile, capturing weight/activation pairs in registers.
3. Capturing the last pair releases that tile, even while products remain in flight.
4. The loader can reuse the released tile; it cannot overwrite the other full tile.

Rows are consumed in order. `row_out` belongs to the current reduction and stays
with its result until accepted. Products accumulate at full 128-bit precision;
the final adder is followed by one Q48 shift. Retiming can increase product and
adder latency without changing the controller, which counts valid responses
rather than assuming a fixed number of clocks.

To run it, load `LLM_TILE_DEPTH` activation words using `load_in`,
`load_address_in` and `load_data_in` while idle. Then present `rows_in`, `base_in`
and `command_valid_in`, holding the command until `command_ready_out`.
Consume `data_out` and `row_out` when `valid_out && ready_in`. Loads during an
active command are ignored. Output backpressure can fill both tiles; the loader
then waits instead of overwriting them. `loading_out` and `computing_out` expose
concurrent work for the regression. Memory errors latch `fault_out` and require
reset of both the design and controller. All activation locations must have been
loaded before a command, including after reset if their contents are not known.

Native tests bind a host weight buffer with `bind_external_memory` and check
functional results. They do not emulate DDR latency. RTL and gate tests drive
the actual memory interface with randomized request stalls and response delays,
and compare against `integer_llm::mmul`. They also exercise long output stalls,
tile reuse, extreme signed operands, controller errors and reset cancellation.

```sh
cmake --build build --target cpphdl hls_llm_TiledMatVec
ctest --test-dir build -R '^hls_llm_TiledMatVec_' --output-on-failure
```

The gate test uses `fit_pipeline_retiming` with a 5 ns generic timing target.
The product and reduction are separate pipelined regions; the loader remains a
latency-tolerant scheduled FSM. With the default eight-word tile, the measured
generic estimate is 4.97 ns, seven product stages and four reduction stages;
retiming adds 4,478 register bits. These are estimated cell delays, not a
placed-and-routed timing guarantee. Generated outputs are in
`build/hls/examples/llm/TiledMatVec_{rtl,gates}/rtl/`. The gate-level file is
`TiledMatVec_gates/rtl/gates.v`; timing details are in the adjacent `timing.json`.

**Throughput limit:** double buffering overlaps work but cannot supply weights
faster than this single-outstanding loader fetches them. Each row still requires
`LLM_TILE_DEPTH` products, pipeline draining/reduction, and an accepted output.
This is a tested building block for a transformer, not a full token accelerator
or a demonstration of saturated DDR bandwidth. Larger burst loads, wider buses
and more product lanes remain subsequent work.

## Memory Contract

`WeightProduct` and `MatrixMath` use one read channel with a configurable 64, 128, 256 or 512-bit
beat and a 32-bit byte address. A request transfers when request-valid and
request-ready are both asserted. The responder returns one aligned beat,
little-endian, and holds response-valid/data/error until response-ready.
Only one read is outstanding. Errors latch `fault_out`; unaligned 64-bit
weight addresses fault without issuing a read. Reset must reset/cancel both
the DUT and its responder; this untagged interface cannot distinguish a late
pre-reset response from a new one.

For `MatrixMath`, load `depth*columns` activation words while idle, then present
`rows_in`, `columns_in`, `depth_in`, and the weights' byte address in `base_in`.
Hold `command_valid_in` until `command_ready_out`. Results carry `row_out` and
`column_out` in row-major order and remain stable until `ready_in`. Activation
loads during computation are ignored; weights must remain unchanged until reset
invalidates the read cache. Invalid dimensions and misaligned/out-of-range bases
latch a fault. Reset is required to clear it. Set `LLM_MAX_DEPTH`, `LLM_MAX_ROWS`
and `LLM_MAX_COLUMNS` at conversion time to choose capacities; for example,
`-DLLM_MAX_DEPTH=896 -DLLM_MAX_COLUMNS=1` represents an entire 896-element dot
product without intermediate rounding. Activation storage scales with
`LLM_MAX_DEPTH * LLM_MAX_COLUMNS`; DDR weights remain external. All byte addresses
are 32-bit and the maximum weight tile must fit that address space. Full Qwen
capacities have not been synthesized or performance-validated. Do not add already
rounded depth tiles and assume they equal one full-precision dot product.

The testbench fills memory before simulation. The RTL never loads a model
file and has no weight-upload logic. A 512-bit beat holds eight Q16.48 weights;
adjacent products reuse the beat. This single-lane prototype cannot consume
eight weights per clock and does not saturate a 512-bit channel.

The full supplied model occupies 3,952,262,144 bytes. A 32-bit byte address
space can contain its weights when placed near address zero. Mapping a real
DDR system requires checking the base address and all additional allocations.

## Tests and Generated Verilog

From the repository root, using the existing HLS-enabled CMake build:

```sh
cmake --build build --target cpphdl hls_llm_product hls_llm_product64 hls_llm_MatrixMath hls_llm_ScalarMath
ctest --test-dir build -R '^hls_llm_' --output-on-failure
```

Tests cover native execution, ordinary RTL at 64/512-bit memory widths, and
retimed gate-level RTL at 512 bits. They check random signed products, full
128-bit results, row accumulation semantics, extreme signed operands,
random DDR delays, output stalls, cached II=1 operation, reset cancellation,
unaligned accesses and memory errors. They also compare every logit and the
selected token for two four-token small-model sequences with matrix products
offloaded to the DUT. The other transformer operations remain on the host.

Additional native, RTL and retimed gate tests check 33 complete rectangular
matrices, including 8x8x8 tiles and extreme signed operands, with random DDR
and output stalls. Their matrix loop and accumulator are in the DUT, not in
the host scoreboard. A native configuration with depth capacity 32 also checks
a 2x17 by 17x3 multiplication. Scalar tests check 1,200 results across six operations.

Generated files and logs are under:

```text
build/hls/examples/llm/rtl64/rtl/
build/hls/examples/llm/rtl512/rtl/
build/hls/examples/llm/gates512/rtl/gates.v
build/hls/examples/llm/gates512/rtl/manifest.json
build/hls/examples/llm/gates512/rtl/timing.json
build/hls/examples/llm/MatrixMath_gates/rtl/gates.v
build/hls/examples/llm/ScalarMath_gates/rtl/gates.v
build/hls/examples/llm/ScalarMath_gates/rtl/blackboxes.v
```

The gate test invokes synthesis with:

```sh
build/cpphdl --synth --top WeightProduct --module WeightProduct \
  --retiming fit_pipeline_retiming --clock-period-ns 5 \
  --output build/llm-synthesis --cxx clang++ \
  hls/examples/llm/WeightProduct.cpp -- -Iinclude -DLLM_DDR_BITS=512
```

Use a new output directory. The current generic estimate expands the four
logical HLS stages to seven physical stages, with II=1, 4.9 ns estimated worst
delay, and 4,974 added register bits. The whole mapped prototype contains
6,874 flip-flops and 55,947 two-input logic/mux primitives. Those counts are
not FPGA LUT counts, and 5 ns is not a placed-and-routed timing guarantee.
Memory misses increase end-to-end latency independently of retiming.

The matrix test uses the same synthesis command with `--top MatrixMath`,
`--module MatrixMath` and `MatrixMath.cpp`. Its two HLS regions retime to seven
product stages and four reduction stages; the generic estimate is 4.9 ns.
Only the arithmetic pipelines have II=1. A matrix result needs `depth` products,
DDR service, reduction latency and any output stalls.

## Opaque Math Functions

`IntegerMath.h` marks division, negative exponential, inverse square root and
SiLU with annotations such as:

```cpp
[[clang::annotate("CPPHDL_BLACKBOX=llm_q48_inverse_sqrt:0")]]
inline Q inverse_sqrt(Q x) { /* native C++ implementation */ }
```

The converter does not synthesize that body. It emits a kept external module
with packed `args` and `result` ports. Each argument occupies 64 bits, the first
argument in the low bits. Signed arguments are sign-extended. `:0` declares
zero **combinational delay in nanoseconds**, and these interfaces have zero
clock latency. It is a requested timing abstraction, not a claim that real
division or exponentiation takes no time. A registered implementation cannot
replace such a box without adding a latency/handshake adapter.

`blackboxes.v` contains empty declarations for implementation tools. Actual
hardware implementations are still required. Do not include the empty
declarations alongside implementations, or use them as simulation models.
`MathModels.sv` supplies test-only DPI models using the original integer math;
Verilator checks their connections and aligned arithmetic results through
retiming. Those DPI functions are **not synthesized**. Reports list the boxes
under `external_blackboxes` and `external_implementations_required`; gate counts
and timing exclude their unknown internal costs. Add, multiply, carry-save
accumulation and matrix control are not blackboxed.

For an independent arithmetic comparison against the original checkout:

```sh
cmake -S . -B build -DCPPHDL_INTLLM_SOURCE="$HOME/intllm"
cmake --build build --target hls_llm_original_math
ctest --test-dir build -R '^hls_llm_original_math$' --output-on-failure
```

This runs 50,000 comparisons against the original fixed-point math helpers,
plus inverse-square-root boundary cases. The original checkout is optional
and is not required for the other tests.

## Remaining Hardware Work

The requested token-to-token accelerator needs these scheduler features; they
cannot be supplied by adding pipeline registers to the product alone:

1. **External memory bandwidth and integration.** Delayed pointer loads/stores
   now work through `ClockedMemory`, including dependent operations across calls.
   Add controller adapters, bursts, automatic pointer-loop prefetch, channel
   selection and hardware allocation bounds. The initial scalar 64-bit channel
   cannot feed eight product lanes at DDR line rate. The existing shared-memory
   arena remains local fixed-latency RAM, separate from external pointers.
   `StreamingMatVec` now demonstrates multiple outstanding 512-bit reads using
   an explicit RTL prefetch frontend, not automatic pointer-loop scheduling.
2. **Loop scheduling around pipelined operations.** Matrix/vector loops must
   issue work, track completions and preserve dependencies. Normalization,
   attention and the next token must wait for their inputs. Floating feedback
   must not silently use an earlier token's accumulator or KV state. The
   current `ClockedPipeline` scheduler rejects these loops and memory effects.
3. **Nonlinear implementations.** Supply hardware for the four external math
   modules with honest timing contracts. Small-tile 128-bit row reduction is
   implemented; larger matrix and attention reductions still need integration
   into the complete transformer scheduler.
4. **Local memory and parallel lanes.** Store activations and KV data in
   banked RAM. An initial practical target is eight product lanes per 512-bit
   weight beat. `TiledMatVec` now demonstrates double-buffered rows and a reused
   activation vector with one product lane. `StreamingMatVec` removes the row
   barriers and adds a multi-outstanding prefetch queue; it still uses one
   product lane, not eight.
   Port count must follow measured compute and DDR bandwidth, not merely
   expose more ports. The full 2048-token KV cache is approximately 96 MiB,
   so it cannot generally be assumed to fit in FPGA block RAM.
5. **Token protocol and full-model test.** Add token-valid/ready, sequence
   reset, context-limit and error reporting, then compare logits and generated
   tokens against the original runner. Repack weights from its manifest:
   this reference's `Layout` is not the original file's tensor ordering.
   RoPE coefficients are precomputed test data, not a hardware tokenizer or
   trigonometric generator.

The passing arithmetic tests do not establish a complete token-to-token accelerator.
See [NOTICE.md](NOTICE.md) for arithmetic provenance and licensing.

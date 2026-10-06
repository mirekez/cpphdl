# Streaming DDR Reads and Pipelined Calculation

`DramStream.cpp` combines variable-latency external memory with a retimable
calculation pipeline. The memory is already populated by the controller or
testbench. This example implements the controller-facing channel, not a DDR PHY
or an AXI controller.

## C++ Algorithm

The loader is an ordinary C++ loop over an external pointer:

```cpp
auto words = cpphdl::hls::external_memory<const uint64_t>(base);
uint64_t sum = 0;
for (uint32_t i = 0; i < count; ++i) sum += words[index + i];
return sum;
```

`ClockedMemory<DramLoad>` translates each pointer read into a request and waits
for its completion before continuing the loop. No fixed DDR latency is assumed.
The running sum belongs to this sequential loader. The separate
`DramCalculate::command()` multiplies the low half of that sum by a scale,
adds the high half, and applies a shift/XOR/add transformation. It is wrapped in
`ClockedPipeline`, allowing calculations from different blocks to overlap.

Set `count_in=1` for a stream of individual reads and calculated answers.
Larger counts demonstrate dependent accumulation inside the pointer loop.

## Hardware Composition

```text
commands --> four ClockedMemory loaders --> ordered completion --> ClockedPipeline --> answers
                       |                                             calculation
                read arbiter + owner FIFO
                       |
                 DDR controller
             requests -->  <-- completions
```

Each loader permits one outstanding access. Four loader instances therefore
allow up to four outstanding memory reads, without weakening the sequential
semantics of a single C++ invocation. The arbiter holds a blocked request stable
and records its loader number when the request is accepted. DDR completions must
arrive in request order; the FIFO routes each completion to its original loader.
Blocks may finish loading in a different order. The top retires them in command
order, preserving their tags and scales.

This is explicit RTL composition around two small HLS methods, not automatic
parallelization of a pointer loop. It is not a guarantee of one DDR read per
clock indefinitely. Four outstanding slots hide some latency; sustained
throughput still depends on memory latency, loop scheduling and backpressure.
Consecutive request transfers are supported. The arithmetic pipeline has II=1
when supplied with operands and when its consumer is ready.

## Ports and Reset

- Command: `valid_in/ready_out`, `index_in`, `count_in`, `scale_in`, `tag_in`.
- Answer: `valid_out/ready_in`, `data_out`, `tag_out`, `error_out`.
- `memory_out`: `ExternalMemoryIf<>`, 32-bit byte addresses and 64-bit data.
  Every request in this example is an aligned 8-byte read.
- `fault_out`: sticky loader/calculation fault; new commands stop on a fault.
  A failed block produces an ordered answer with `error_out=5` and zero data.

The base byte address is `0x1000`. The test maps 1,024 words there. Commands in
this example use `1 <= count <= 4` and `index + count <= 1024`; callers must
respect these bounds. Arithmetic sums wrap modulo 2^64. Handshake payloads must
remain stable while valid is asserted and ready is low.

The controller returns exactly one completion for each accepted read, with
arbitrary latency. There are no transaction IDs, bursts, writes or out-of-order
DDR completions here. Reset the DUT and controller together: reset cancels all
outstanding commands and memory responses. Reset is also required after a
memory error.

## HLS and Retiming

Ordinary conversion needs no HLS command-line switch:

```sh
build/cpphdl --generated-dir=build/dram-rtl hls/examples/dram/DramStream.cpp
```

For retimed gate-level Verilog:

```sh
build/cpphdl --synth --top DramStream --module DramStream \
  --retiming fit_pipeline_retiming --clock-period-ns 5 \
  --retime-module cpphdl_synth_top.calculate \
  --output build/dram-gates hls/examples/dram/DramStream.cpp
```

Only the calculation pipeline is retimed. Loader FSMs, arbitration and the DDR
handshake retain their behavior. The completion handshake naturally waits when
the pipeline cannot accept data; retiming carries tag and error metadata with
each result. The 5 ns target uses CppHDL's cell-delay estimates, not placed-and-
routed DDR or FPGA timing sign-off.

The current synthesis report expands the arithmetic pipeline from 3 to 6
stages, with II=1, 1,834 inserted register bits and a 4.90 ns estimated path
(32.37 ns before retiming). The complete design still has a 9.46 ns worst-path
estimate in unretimed logic. Therefore the 5 ns arithmetic result is **not** a
200 MHz guarantee for the entire design. Memory latency and queueing also remain
part of command-to-answer latency.

## Tests

```sh
cmake --build build --target hls_dram_stream
ctest --test-dir build -R '^hls_dram_stream_' --output-on-failure
```

CTest covers native C++, generated RTL under Verilator, and synthesized/retimed
gates under Verilator. `Run.py` retains conversion, build and simulation logs in
`build/hls/examples/dram/{rtl,gates}/`.
An ordinary HLS RTL snapshot is also available in [`generated/`](generated/).
The retimed gate netlist is generated as
`build/hls/examples/dram/gates/generated/gates.v` during testing.

The native wrapper executes pointer accesses against a mapped host buffer as a
transaction reference. It does not simulate DDR latency. RTL and gate tests use
a queued controller model with randomized 1-37-clock response delays and
request stalls. They check 1,000 randomized blocks, a consecutive-request test,
four reads in flight, overlapping memory and compute, exact read counts,
ordered full-width results and tags, stable stalled requests/results, error
delivery, reset during activity, and recovery. The gate test also checks that
retiming inserts stages, meets the selected timing target and preserves II=1
for the arithmetic region.

# Continuous transient windows and trace storage

`time_grid.windowed_transient()` keeps one continuous ngspice transient. It
allows a larger maximum integration step during quiet warmup, then schedules
breakpoints throughout the original measurement window. It preserves every
device, source, initial condition, observed node and measurement limit. It
does not save and reload circuit state between windows.

The helper adds an isolated zero-volt pulse source. Its four equally spaced
phases schedule the fine breakpoints; it carries no current or stored energy
and connects to no circuit node. `check_observed_grid()` checks the actual
saved times for complete coverage, monotonic ordering and the original maximum
gap. Equal-time SPICE states remain available to electrical checks.

A larger warmup step can change numerical error even when it preserves the
equations. Callers must pass the electrical contract independently in each run
and compare different warmup steps before accepting a result. The helper's
regression checks slow and fast RC responses against analytic solutions and
compares uniform and windowed integrations. This does not establish convergence
for an arbitrary receiver circuit.

The sixteen-slice IHP receiver uses separate 1 ns and 0.5 ns warmup experiments,
each followed by the unchanged 20 ps maximum measurement grid. Their comparison
requires both electrical checks to pass first, fresh source and artifact hashes,
matching process conditions, and at most 1 mV difference in either interpolation
direction. A failed acquisition cannot become a pass through convergence alone.

`trace_storage.archive_trace()` stores a complete trace as gzip. It hashes the
original bytes, writes the archive, verifies the decompressed byte stream,
checks that the original did not change, then removes the original. Metadata
records both hashes and byte counts. Collisions and failed reconstruction keep
the original. This saves disk space without removing samples, repeated times
or decimal precision. Readers must explicitly support the returned `.gz` path;
older readers that expect the original pathname need the trace restored first.

```sh
ctest --test-dir analog/build --output-on-failure \
    -R '^analog[.]veriloga[.](time_grid|trace_storage)$'
```

`trace_storage.capture_trace()` streams the producer's complete output through
a FIFO into gzip without allocating a temporary raw trace. The producer must
exit before the capture context closes. The helper verifies reconstructed bytes
and records both hashes and byte counts; it preserves repeated timestamps and
all decimal precision. The closed programmable-conversion fixtures use this
path for native C++ traces and ngspice `wrdata`. Failed producers release the
reader and retain any captured bytes for diagnosis.

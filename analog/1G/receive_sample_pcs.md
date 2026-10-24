# Fixed samples to Ethernet frames

`ReceiveSamplePCS` composes `ReceiveSampleRecovery` and `PCSReceiver` in one
250 MHz clock domain. The recovered-symbol valid pulse drives the PCS's explicit
`symbol_enable_in`. PCS output words have their own `receive_valid_out` pulse;
consumers must honor that pulse, including for idle words. The wrapper exposes
recovered sample tags and fractions, PCS lock and transport faults. A transport
fault inhibits PCS symbol acceptance and revokes PMA availability. Explicit
restart resets both blocks; no generated clock or host interpolation feeds the
receiver.

`tests/receive_sample_pcs_reference.py` uses the independent Clause 40 algebra
and IEEE constellation tables from `transmit_reference.py`. For each remote
role it generates 20,000 idle training symbols followed by two real Ethernet
frames with seven preamble bytes, SFD, payload and Ethernet CRC-32/FCS. The frame
sizes including preamble are 72 and 1526 bytes. Expected bytes are supplied only
to the checker, never to a DUT input.

The C++ fixture first computes an entire fixed 250 MS/s ADC sequence from those
symbols using a raised-cosine pulse, roll-off 0.35, and byte quantization. It
then runs the receiver without consulting DUT phase to generate inputs.
Eighteen cases cover both remote roles, source rates −1500/0/+1500 ppm and
common delays 0/0.105/0.9 UI. Tags start at 65520 to exercise wrap. PMA availability
is asserted after 34,000 input clocks, allowing timing acquisition before PCS
acquisition; this is a declared training interval, not an automatic timing-lock
detector. Every received frame must match independent frame truth byte for byte,
with no receive-error flag, no extra/missing frame and no sample-history fault.
The complete clock trace includes inputs, PCS words/valid/lock, recovered
symbol valid/tag/fraction and reset. Native and generated RTL traces must match
exactly, including clocks without a symbol.

Run `1G/tests/receive_sample_pcs.py --native <bench> --converter <cpphdl>
--verilator <verilator> --cxx <compiler> --work <results>`. The runner retains
source/tool/fixture hashes, per-case frame metrics and verified compressed
native/RTL traces. No physical synthesis frequency is implied by functional
RTL equivalence. Channel equalization, residual echo, noise, per-slice clock
skew and actual sample-clock-to-SAR timing remain separate qualification gates.
A common delivered-sample delay can be addressed by timing recovery in this
controlled waveform model; the earlier physical TRACK fixed-aperture failure
is not reclassified as a pass.

The v2 maintained receipt at
`out/receive-sample-pcs/v2/qualification/checks.json` passes all 796,260 native/RTL
clock states, eighteen cases, 36 frames and 28,764 bytes. The checker observes
settled pre-edge state in both engines; the retained v1 failure exposed a
post-strobe C++ cached-child-port observation mismatch only in diagnostic
symbol-valid. No full-clock field was removed from comparison.

The independent retained-trace checker `tests/receive_sample_pcs_latency.py`
uses source coordinates and frame truth to verify each output against the
latest previously accepted symbol minus 39. The v2 audit checks 92,116 output
words, detects no source slips after PMA assertion and measures maximum phase
error 0.044122 UI. Its evidence and hashes are in
`out/receive-sample-pcs/v2/latency-audit.json`.

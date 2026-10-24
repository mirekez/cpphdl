# Auditing repeated ngspice time records

The strict trace reader still rejects duplicate/backward times, nonfinite data
and incomplete trajectories. A refined 1G transmitter run exposed an ngspice
47 encoding case: adjacent records at exactly the same binary64 time differed
by at most 1.33e-15 V. Neither represented elapsed physical time.

`trace_codec.normalize_roundoff_duplicates()` is an explicit adapter used by
that voltage-trajectory fixture before the strict reader. It keeps the first
record at an instant and permits removal only when every later value differs
from that retained state by at most `16 * epsilon * max(1, abs(a), abs(b))`.
The complete translated transmitter bank explicitly selects `epsilon_units=64`:
its refined run contained twelve equal-time records with differences up to
48 epsilon units (about 12 femtovolts). This voltage-trajectory adapter accepts
only integer bounds from 1 through 64; all other callers retain the default 16.
Each audit records its chosen bound, and reconstruction verifies that same bound.
Backward time, nonfinite data and an actual electrical change remain errors.
The bound applies to the retained state, preventing small changes from
accumulating through a sequence of omitted records.

The adapter saves every omitted original line and its insertion position in
`spice.dat.roundoff.json`. It verifies reconstruction of the original SHA256
before replacing the raw trace. The driver comparison repeats reconstruction
from the compressed canonical waveform and hashes the audit itself. Both the
canonical observations and original raw byte stream remain reproducible.
No electrical tolerance, timestep or waveform interpolation is changed.

This adapter is not used to infer clock events or comparator decisions. Those
interfaces retain their own accepted-point/event checks. Unit guards require
changed-state duplicates, backward time, nonfinite records, accumulated drift
and altered archives to fail. They also check the explicit 64-unit policy,
reject a 65-unit state change and reject invalid policy arguments.

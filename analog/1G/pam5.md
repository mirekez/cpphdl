# PAM5 levels, storage and pulse shaping

Thermometer count `k = 0..4` represents symbol `s = k - 2`. Controls are 0/1.2 V;
`VREF = 0.6 V`. The default DAC declares four controlled currents into the same
loaded output, plus output resistance and charge storage:

```
OUT - VCM = step * sum(CODE[i] - VREF) / 1.2
          = step * s                  (unloaded DC)
```

`step = 0.5 V` is an open-circuit differential command. Loading the DAC output
with 1 kOhm halves its level spacing because the declared output resistance is
also 1 kOhm. The loaded test checks this interaction explicitly.

`sim_symbol_memory` uses two nonoverlapping tracking phases with real capacitor
states. It captures the old command near the symbol boundary; the bench changes
the new command at 0.15 UI. The filter then produces
`VCM + 0.75 * (MAIN - VCM) + 0.25 * (HISTORY - VCM)`. Code uses two VCCS purposes
to add both contributions physically.

Five input levels have 17 distinct filter sums. `transmitter` composes DAC,
history, filter and driver. Its `REPLICA` exposes the achieved filtered command,
not a recomputed ideal waveform. The driver presents 50 Ohm per leg; a 100 Ohm
differential load halves its open-circuit differential voltage.

`sim_pam5_slicer` has four thresholds at `(-1.5, -0.5, 0.5, 1.5) * step` relative
to `VCM`, with 2 mV transition half-width, finite drive and capacitor storage.
It is an approximate comparator bank, not a transistor ADC. `dfe` samples its
own decisions into four flip-flops, reconverts them to voltage and subtracts one
fixed postcursor tap. The bypass test supplies the same ISI and demonstrates
errors when correction is disabled.

# PCS hardware blocks

[pcs.hpp](pcs.hpp) contains two C++RTL `Module` classes. They are hardware
building blocks for Clause 40, **not a complete GMII-to-MDI PCS**. They do not
substitute a private framing protocol for Ethernet.

`PCSSymbolMapper` registers four PAM5 symbols in one symbol-clock cycle:

```cpp
PCSSymbolMapper mapper;
// Bind class member ports, then call _assign(). The hardware clock is 125 MHz.
mapper.point_in = _ASSIGN(point);         // Sd[5:0], unsigned 0..63
mapper.subset_in = _ASSIGN(subset);       // Printed order Sd[6], Sd[7], Sd[8]
mapper.condition_in = _ASSIGN(condition);
mapper.negate_in = _ASSIGN(negate);       // Bit i requests sign inversion on pair i
```

`subset` is the number represented by the **printed** three-bit string, so
Sd[6] is its most significant bit. It is not `(Sd >> 6) & 7` in a conventional
least-significant-bit-first integer representation.

`symbols_out` packs four three-bit unsigned DAC codes, with pair A in bits 2:0,
B in 5:3, C in 8:6 and D in 11:9. Code = physical symbol + 2; hence 0..4
represent -2..+2. This format avoids unsigned/sign-extension ambiguity at the
DAC boundary. The terminal thermometer decoder and DAC remain separate devices.

| condition | Code group |
| --- | --- |
| 0 | Normal data: all 512 point/subset combinations |
| 1, 2 | xmt_err, CSExtend_Err |
| 3, 4 | CSExtend, CSReset |
| 5, 6 | SSD1, SSD2 |
| 7 | ESD1 |
| 8, 9, 10 | ESD2_Ext_0, ESD2_Ext_1, ESD2_Ext_2 |
| 11 | ESD_Ext_Err |
| 12 | Idle/Carrier Extension: subset 0, point 0..15 |
| 13 | SEND_Z: four zero-amplitude symbols |
| 14, 15 | Invalid |

Delimiter entries exist only in subset 0. Unsupported combinations produce
zero amplitude and `invalid_out = true`. Reset produces zero amplitude without
an invalid indication. The caller supplies the condition and per-pair sign
inversions; this mapper does not infer the transmit state machine, sign bits,
GMII pipeline delay or convolutional state.

`PCSSideStream` advances one 33-bit LFSR step per enabled symbol. Bits shift toward
higher indices. New bit 0 is old bit 32 XOR old bit 12 for a master, or old bit
19 for a slave. These are the master `1 + x^13 + x^33` and slave
`1 + x^20 + x^33` recurrences. `load_in` takes priority over advance; reset takes
priority over both and seeds state 1. Loading zero reports invalid and stops
advancement until a nonzero seed or reset. The output is the LFSR state, **not**
a fabricated eight-bit scrambling mask. The Clause 40 Sx/Sy/Sg expansion,
odd/even encoding, trellis encoder/Viterbi decoder, receive synchronization and
PCS transmit/receive state machines remain to be implemented.

[tests/pcs.cpp](tests/pcs.cpp) applies 8,816 data/control/sign combinations and
invalid/reset cases. [tests/scrambler.cpp](tests/scrambler.cpp) uses an independent
bit-array recurrence for 20,000 master/slave steps, holds, seed errors and reset.
Each runs natively and against converted RTL, passes its absolute checker, then
compares every output trace exactly. When the local IHP toolchain is available,
the same tests also run against actual mapped IHP gates and compare their traces.
Digital logic is converted to SystemVerilog, not behavioral Verilog-A.

References:

- IEEE [Table 40-1](https://www.ieee802.org/3/publication/ab/table40_1.txt) and
  [Table 40-2](https://www.ieee802.org/3/publication/ab/table40_2.txt): numerical
  normal/control code groups; downloaded and checked 2026-10-17. Independent
  numeric fixtures are in `tests/pcs_data.csv` and `tests/pcs_controls.csv`.
- IEEE [side-stream scrambling presentation, slide 8](https://grouper.ieee.org/groups/802/3/cg/public/Sept2017/Graber_3cg_15a_0917.pdf):
  the Clause 40 master/slave generator polynomials reused by that proposal.
  This presentation does not specify the rest of a Clause 40 PCS.

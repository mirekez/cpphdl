# Physical topology and loading

For one pair, two active transmitters connect to the opposite ends of the **same**
two wires. Each endpoint's 100 Ohm differential source impedance also terminates
the incoming signal. Two 15 Ohm conductor resistors and four 5 pF shunt capacitors
form the reciprocal short-channel fixture. There are no directed ideal channel
outputs that would hide receiver loading.

For nominal per-leg driver impedance `R = 50 Ohm` and conductor resistance
`r = 15 Ohm`, the settled differential voltage at endpoint A is

```
LINE_A = (R + r) / (2*R + r) * E_A + R / (2*R + r) * E_B
       = 65/115 * E_A + 50/115 * E_B
```

`E_A` and `E_B` are the achieved open-circuit transmit commands. A hybrid
subtracts `balance * E_local`. Perfect nominal balance is `65/115`; the full
system deliberately uses `0.35` to leave substantial echo for adaptation.
Changing the remote source impedance to 35 Ohm changes both coefficients and
the local/remote separation. The changed-load test must still recover holdout
symbols after training.

Four-pair coupling uses 5 kOhm resistors between adjacent positive conductors
and between adjacent negative conductors at both ends. This creates an actual
coupled passive network and a bounded interference fixture; it is not a
frequency-dependent NEXT/FEXT cable specification. The checker independently
solves an eight-node DC network to check loaded cable and hybrid outputs.

The hybrid has a 1 kOhm output impedance. The equalizer's lead divider loads it
with 2 kOhm at DC. `receiver` therefore chooses gain 3.45: the combined hybrid
loading, divider and gain give `2/3 * 1/2 * 3.45 = 1.15`. This compensates the
nominal remote cable attenuation. Removing that loading factor would give an
incorrect gain even though each block's isolated intention looked reasonable.

The CTLE transfer with an ideal input source is

```
H(s) = gain * Rlower * (1 + s*Rupper*Clead)
       / (Rupper + Rlower + s*Rupper*Rlower*Clead)
       / (1 + s*Rout*Cout)
```

Standalone sine tests check complex gain at 7.8125 and 62.5 MHz. Full-system
receive gain includes hybrid loading, so it is not obtained by multiplying
isolated unloaded gains blindly.

# Main/history gate control

[`PAM5DriverControl`](pam5_driver_control.hpp) is a digital `Module`. Each
accepted symbol supplies the main steering bank; a register supplies the prior
symbol to the history bank. Both banks act on the same physical output pins in
the [weighted MOS driver](IHPSG13G2/emphasized_driver.md).

| Port | Meaning |
| --- | --- |
| `symbol_in` | Three-bit code: 0 = +2, 1 = +1, 2 = zero, 3 = −1, 4 = −2 |
| `enable_in` | Enable symbol transfer and steering |
| `main_p_out`, `main_n_out` | Four complementary unary gate controls for the new symbol |
| `history_p_out`, `history_n_out` | Four complementary unary controls for the preceding symbol |
| `valid_out` | This clock produced active steering controls |
| `fault_out` | An enabled invalid code latched a fault |

Outputs update at each rising clock edge. A P-side gate sinks its unit current
from P; an N-side gate sinks it from N. For code k, P enables k cells and N
enables the other four. Each valid bank therefore preserves total tail current.

Reset clears every gate and fault and sets history to zero symbol. Disable
clears every gate and resets history while preserving a latched fault. Codes
5–7 while enabled latch fault and disable steering until reset. The first valid
symbol after reset/disable uses zero history. Electrical settling remains a
property of the attached driver and its finite sources; `valid_out` does not
measure voltage or prove that a disturbed output has achieved its purpose.

Native C++RTL, generated RTL and actual IHP gate models pass 638 identical
contract cycles: all 125 symbol triples, all seventeen weighted combinations,
disable/re-enable, invalid codes, sticky fault and reset. Slow-cell timing has
+6.50 ns setup slack at 8 ns before routed-wire extraction.

The standalone physical implementation also passes exact post-route output
comparison, +6.05 ns setup and +0.08 ns hold with extracted nominal wire RC,
zero full-PDK DRC violations across 868 categories and transistor LVS with
20 matched circuits and all 26 ports. Its report is
`IHPSG13G2/results/pam5_driver_runs/PAM5DriverControl-73vypovp/physical_module.json`.
That report pins its source snapshot and PDK inputs. This is a 200 µm square
digital controller macro; analog steering, clock interfaces and chip assembly
require their separate verification.

```sh
cmake --build analog/build --target check-1g-sample-and-driver-control
cmake --build analog/build --target one-g-pam5_driver-gds
```

# Programmable transmitter

`programmable_core_emphasized_pam5_driver` preserves the main/history PAM-5
driver and its shared translation bias. Its six `TRIM_N` member ports select
parallel resistor branches through physical PMOS devices. A high core voltage
disables a branch; a low core voltage enables it. Six additional level shifters
deliver those controls from 1.2 V to the 3.3 V devices.

The complete exported circuit has 296 MOS devices, including the original
sixteen steering-control translators. These are connected devices with finite
supplies and charge storage. Trim does not replace the bias voltage with an
ideal source.

The purpose is to correct amplitude across process and temperature without
enlarging every steering device. A preliminary static process-model study
selected codes 24 at TT/27 C, 38 at SS/125 C, and 11 at FF/-40 C. Those values
are characterization inputs. An automatic amplitude-calibration controller is
still required; preset codes do not qualify a complete PHY.

## Tests

`transmit_control_fixture.cpp` runs the real `PAM5DriverControl` module. Its CSV
must pass an independent code/history/reset/disable/fault oracle before an
analog fixture can use it. The same stimulus drives both analog solvers.

`programmable_core_transmit_driver.cpp` exercises 80, 100 and 120 ohm loads and
a refined nominal timestep. Generic C++RTL and Verilog-A must pass their
absolute electrical contracts separately before a 10 mV comparison. Nominal
versus refined output must agree within 5 mV. Process tests bind every MOS
device to the declared IHP geometry and check the loaded full circuit at each
declared corner; a static sweep alone is insufficient.

`VerilogA/trace_states.py` retains all states at equal printed times. Aperture
checks use their minimum/maximum envelope, including both neighboring groups
when interpolating. A second state that violates a gate, current or output
limit fails validation. Correlated pin differences are evaluated before taking
an envelope. Neither timestamps nor electrical tolerances are adjusted.

`recheck_transmit_states.py` can validate retained Verilog-A simulations whose
only failure was the old refined-trace reader. It requires unchanged source
and tool hashes, regenerates every experiment byte for byte, rebuilds and
compares each OSDI object, and rechecks all electrical states. It writes a new
receipt and preserves the original failure. This is waveform validation,
not a new simulation or a process/layout qualification.

## Remaining qualification

The programmable driver is under test. Full-circuit process-corner transient
results, resistor process variation, automatic calibration, extracted layout,
package/magnetics effects and the assembled repeater remain outstanding.
No die-area claim follows from its transistor count.

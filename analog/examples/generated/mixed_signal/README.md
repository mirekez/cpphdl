# mixed_signal

[Original C++ model and explanation](../../mixed_signal/README.md).

MixedReceiverAnalog.va contains the actual elaborated RC input, finite-resistance output driver, output capacitance and load. It is an analog partition, not the complete receiver. The host must supply Vdd = 1.2 V relative to Vss, ground Vss, and drive VIN as in the original bench (0 V, then 1.2 V at 750 ns). The cpphdl_bit<ID> port is SECOND, driven at 0/1 V relative to host ground. The native two-register pipeline, clock and threshold sensing remain external. IN and Y are accessible electrical nodes. Never leave the bit control floating.

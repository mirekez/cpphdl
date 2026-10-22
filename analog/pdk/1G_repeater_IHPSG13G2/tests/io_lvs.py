#!/usr/bin/env python3
"""Check that CDL dialect conversion preserves the complete device graph."""
from collections import Counter
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from io_lvs import BULK, elaborate


SOURCE = """* Test all primitive classes, continuation, unused cells and deep fanout.
.SUBCKT leaf a b
M0 a b sub! sub! sg13_hv_nmos w=1u
+ l=600n m=3
XR0 a sub! ptap1 A=141.253p P=47.54u
XR1 b a / ntap1 A=10p P=12u
D0 sub! b dantenna a=35p p=58u
.ENDS leaf
.SUBCKT unused a b
R0 a b 100
.ENDS unused
.SUBCKT resistor a b
R0 a b 100 $SUB=sub! $[rppd] w=1u l=2u
.ENDS resistor
.SUBCKT middle p q
X0 p q / leaf
X1 q p leaf
X2 p q resistor
.ENDS middle
.SUBCKT top pad rail
X0 pad rail middle
X1 rail pad leaf
X2 pad rail unused
.ENDS top
"""


def device_graph(text, top, adapted=False):
    """Independent small SPICE hierarchy walker; parameters remain exact tokens."""
    import re
    lines = re.sub(r"\n\s*\+\s*", " ", text).splitlines()
    circuits = {}
    current = None
    for line in lines:
        fields = line.split()
        if not fields or fields[0].startswith("*"):
            continue
        if fields[0].upper() == ".SUBCKT":
            current = fields[1]
            circuits[current] = (fields[2:], [])
        elif fields[0].upper() == ".ENDS":
            current = None
        else:
            circuits[current][1].append([token for token in fields if token != "/"])
    graph = []

    def visit(name, actual, path):
        ports, body = circuits[name]
        assert len(ports) == len(actual)
        nets = dict(zip(ports, actual))

        def net(token):
            return "substrate" if token == "sub!" else nets.get(token, path + "/" + token)

        for fields in body:
            identifier = fields[0]
            if identifier.startswith("X") and fields[-1] in circuits:
                visit(fields[-1], [net(x) for x in fields[1:-1]], path + "/" + identifier)
            else:
                count = 4 if identifier.startswith("M") else 2
                # X taps and their adapted R form have identical physical identity.
                kind = "tap" if fields[count + 1] in ("ptap1", "ntap1") else identifier[0]
                nodes = [net(x) for x in fields[1:count + 1]]
                parameters = []
                for field in fields[count + 1:]:
                    if field.startswith("$SUB="):
                        nodes.append(net(field.split("=", 1)[1]))
                    else:
                        parameters.append(field)
                graph.append((kind, tuple(nodes), tuple(parameters)))
    visit(top, ["pad", "rail"] + (["substrate"] if adapted else []), "top")
    return Counter(graph)


class CDLDialect(unittest.TestCase):
    def test_transitive_substrate_and_device_parameters(self):
        result, threaded, audit = elaborate(SOURCE)
        self.assertEqual(threaded, {"leaf", "resistor", "middle", "top"})
        self.assertEqual(audit["converted_tap_instances"], 2)
        self.assertFalse(audit["parameters_changed"])
        self.assertEqual(device_graph(SOURCE, "top"), device_graph(result, "top", True))
        self.assertIn(".SUBCKT unused a b\n", result)
        self.assertIn("RXR0 a " + BULK + " ptap1 A=141.253p P=47.54u", result)
        self.assertNotIn("sub!", result)
        # Reversing a terminal or changing geometry must invalidate the contract.
        for corrupted in (result.replace("RXR0 a ", "RXR0 b "),
                          result.replace("A=141.253p", "A=142p")):
            self.assertNotEqual(device_graph(SOURCE, "top"), device_graph(corrupted, "top", True))

    def test_reject_ambiguous_or_malformed_input(self):
        for source in ("+ orphan", ".SUBCKT a p\n", ".ENDS", SOURCE + SOURCE,
                       ".SUBCKT a p\n.ENDS b", ".SUBCKT a " + BULK + "\n.ENDS",
                       ".SUBCKT a p\nX0 p missing\n.ENDS"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                elaborate(source)


if __name__ == "__main__":
    unittest.main()

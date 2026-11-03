"""Resolve repeated SRAM structures, then independently audit their endpoints.

The input is a native KLayout LVS database from the full extraction, with
matched leaf circuits. SPICE round trips can lose device parameters and are
not accepted as a substitute for that extraction. Hints guide correspondence;
only a complete comparison plus the separate graph audit constitutes a pass.
"""
from collections import Counter, defaultdict

import klayout.db as db

from sram_lvs_audit import audit


def net_name(net):
    return None if net is None else net.expanded_name()


class Correspondence:
    def __init__(self, result, top_name):
        self.result = result
        self.layout, self.reference = result.netlist(), result.reference
        self.left = {c.name.upper(): c for c in self.layout.each_circuit()}
        self.right = {c.name.upper(): c for c in self.reference.each_circuit()}
        if set(self.left) != set(self.right) or top_name.upper() not in self.left:
            raise ValueError('circuit inventories or requested top differ')
        self.top_name = top_name.upper()
        self.top_left, self.top_right = self.left[self.top_name], self.right[self.top_name]

    def circuit_pairs(self):
        pairs = list(self.result.xref().each_circuit_pair())
        mapped = {}
        for pair in pairs:
            if pair.first() is None or pair.second() is None:
                raise ValueError('missing circuit correspondence')
            name = pair.first().name.upper()
            if name in mapped or pair.second().name.upper() != name:
                raise ValueError('ambiguous circuit correspondence')
            if name != self.top_name and pair.status() != db.NetlistCrossReference.Match:
                raise ValueError('leaf circuits must match before hierarchy refinement')
            mapped[name] = pair
        if set(mapped) != set(self.left):
            raise ValueError('incomplete circuit correspondence')
        return mapped

    def expand_column_logic(self):
        """Align boundaries already expanded in part by physical extraction."""
        def count(circuit, memo):
            if circuit.name not in memo:
                memo[circuit.name] = len(list(circuit.each_device())) + sum(
                    count(s.circuit_ref(), memo) for s in circuit.each_subcircuit())
            return memo[circuit.name]

        evidence = []
        for circuit, expected in [
            (self.top_left, {'RSC_IHPSG13_INVX2': 80, 'RSC_IHPSG13_NAND2X2': 8}),
            (self.top_right, {'RSC_IHPSG13_INVX2': 464, 'RSC_IHPSG13_NAND2X2': 264}),
        ]:
            children = [s for s in circuit.each_subcircuit() if s.circuit_ref().name.upper() in expected]
            actual = Counter(s.circuit_ref().name.upper() for s in children)
            if actual != expected:
                raise ValueError('unexpected column logic boundary inventory')
            before = count(circuit, {})
            for child in children:
                circuit.flatten_subcircuit(child)
            after = count(circuit, {})
            if before != after:
                raise ValueError('column boundary expansion lost devices')
            evidence.append(dict(instances=dict(actual), instantiated_devices=after))
        return evidence

    def refine(self):
        """Joint color refinement gives structural candidates, not a proof."""
        pairs = self.circuit_pairs()
        xref = self.result.xref()
        labels, adjacent, nets, side_nodes = [], [], [], []

        def add(label):
            node = len(labels)
            labels.append(label)
            adjacent.append([])
            return node

        def edge(a, b, label):
            adjacent[a].append((label, b))
            adjacent[b].append((label, a))

        for side, circuit in enumerate([self.top_left, self.top_right]):
            net_nodes, ports = {}, defaultdict(list)
            side_nodes.append([])
            for pin in circuit.each_pin():
                ports[net_name(circuit.net_for_pin(pin))].append(pin.name().upper())
            for net in circuit.each_net():
                name = net_name(net)
                node = add(('net', tuple(sorted(ports[name]))))
                net_nodes[name] = node
                side_nodes[-1].append(node)
                nets.append((side, name, node))

            def connect(node, net, label):
                target = net_nodes[net_name(net)] if net is not None else add(('unconnected', label))
                edge(node, target, label)

            for device in list(circuit.each_device()):
                model = device.device_class().name.lower()
                dimensions = tuple((p, round(device.parameter(p), 10)) for p in ('L', 'W'))
                node = add(('device', model, dimensions))
                for terminal in device.device_class().terminal_definitions():
                    name = terminal.name
                    label = name
                    if model in ('sg13_lv_nmos', 'sg13_lv_pmos') and name in ('S', 'D'):
                        label = 'SD'
                    if model.startswith('res_metal') and name in ('A', 'B'):
                        label = 'AB'
                    connect(node, device.net_for_terminal(name), label)
            for child in list(circuit.each_subcircuit()):
                target = child.circuit_ref().name.upper()
                node = add(('subcircuit', target))
                pair = pairs[target]
                for pins in xref.each_pin_pair(pair):
                    a, b = pins.first(), pins.second()
                    if a is None or b is None:
                        raise ValueError('incomplete child port correspondence')
                    pin = a if side == 0 else b
                    connect(node, child.net_for_pin(pin.id()), 'P:' + a.name().upper())

        def encode(values):
            table = {}
            return [table.setdefault(value, len(table)) for value in values]

        colors = encode(labels)
        for iteration in range(len(labels) + 1):
            updated = encode((colors[i], tuple(sorted((label, colors[j]) for label, j in neighbours)))
                             for i, neighbours in enumerate(adjacent))
            counts = [Counter(updated[i] for i in nodes) for nodes in side_nodes]
            if counts[0] != counts[1]:
                raise ValueError('unequal structural net groups')
            stable = len(set(updated)) == len(set(colors))
            colors = updated
            if stable:
                break
        else:
            raise ValueError('structural refinement did not converge')
        groups = [defaultdict(list), defaultdict(list)]
        for side, name, node in nets:
            groups[side][colors[node]].append(name)
        hints = [(names[0], groups[1][color][0]) for color, names in groups[0].items() if len(names) == 1]
        return hints, dict(iterations=iteration + 1, unique_nets=len(hints),
                           ambiguous_groups=sum(len(names) > 1 for names in groups[0].values()))

    def compare(self, hints):
        comparer = db.NetlistComparer()
        comparer.max_depth = 2
        comparer.max_branch_complexity = 20
        right_classes = {c.name.lower(): c for c in self.reference.each_device_class()}
        used_classes = {d.device_class().name.lower() for c in self.left.values()
                        for d in c.each_device()}
        for device_class in self.layout.each_device_class():
            target = right_classes.get(device_class.name.lower())
            if target is None:
                if device_class.name.lower() in used_classes:
                    raise ValueError('missing instantiated device class')
                continue
            comparer.same_device_classes(device_class, target)
        for name, left in self.left.items():
            right = self.right[name]
            comparer.same_circuits(left, right)
            for pin in left.each_pin():
                other = right.pin_by_name(pin.name())
                if other is not None:
                    comparer.same_nets(left.net_for_pin(pin), right.net_for_pin(other), True)
            for net in left.each_net():
                if net.name.startswith('__CPPHDL_FILLCAP_'):
                    other = right.net_by_name(net.name)
                    if other is None:
                        raise ValueError('missing prepared filler correspondence')
                    comparer.same_nets(net, other, True)
        for a, b in hints:
            left, right = self.top_left.net_by_name(a), self.top_right.net_by_name(b)
            if left is None or right is None:
                raise ValueError('mapping hint refers to a missing net')
            comparer.same_nets(left, right, True)
        # Re-comparing a loaded report otherwise appends duplicate circuit pairs.
        self.result.xref().clear()
        return self.result.compare(comparer)

    def residual(self, maximum_steps=5000):
        """Resolve remaining child symmetries with bounded bijective search."""
        pairs = self.circuit_pairs()
        top = pairs[self.top_name]
        xref = self.result.xref()
        for method in (xref.each_device_pair, xref.each_pin_pair):
            if any(p.status() != db.NetlistCrossReference.Match for p in method(top)):
                raise ValueError('residual search requires matched top devices and ports')
        left = {s.id(): s for s in self.top_left.each_subcircuit()}
        right = {s.id(): s for s in self.top_right.each_subcircuit()}
        mapping, inverse = {}, {}

        def bind(m, inv, a, b):
            if a is None or b is None:
                return a == b
            if (a in m and m[a] != b) or (b in inv and inv[b] != a):
                return False
            m[a], inv[b] = b, a
            return True

        for pair in xref.each_net_pair(top):
            if pair.status() == db.NetlistCrossReference.Match:
                if not bind(mapping, inverse, net_name(pair.first()), net_name(pair.second())):
                    raise ValueError('conflicting partial net correspondence')
        initial = dict(mapping)

        def bindings(a, b):
            pair = pairs[a.circuit_ref().name.upper()]
            if pair.second().name.upper() != b.circuit_ref().name.upper():
                return None
            return [(net_name(a.net_for_pin(p.first().id())), net_name(b.net_for_pin(p.second().id())))
                    for p in xref.each_pin_pair(pair)]

        remaining_left, remaining_right = [], []
        for pair in xref.each_subcircuit_pair(top):
            a, b = pair.first(), pair.second()
            if pair.status() == db.NetlistCrossReference.Match and a is not None and b is not None:
                for i, j in bindings(left[a.id()], right[b.id()]):
                    if not bind(mapping, inverse, i, j):
                        raise ValueError('matched child contradicts the net correspondence')
            else:
                if a is not None:
                    remaining_left.append(a.id())
                if b is not None:
                    remaining_right.append(b.id())
        if len(remaining_left) != len(remaining_right):
            raise ValueError('unequal residual child counts')
        steps = 0

        def solve(ls, rs):
            nonlocal steps
            steps += 1
            if steps > maximum_steps:
                raise ValueError('residual search budget exceeded')
            if not ls:
                return True
            best = None
            for i in ls:
                choices = []
                for j in rs:
                    endpoints = bindings(left[i], right[j])
                    if endpoints is None:
                        continue
                    local, reverse = {}, {}
                    if all((a is None and b is None) or
                           (a is not None and b is not None and
                            (a not in mapping or mapping[a] == b) and
                            (b not in inverse or inverse[b] == a) and bind(local, reverse, a, b))
                           for a, b in endpoints):
                        choices.append((j, endpoints))
                if not choices:
                    return False
                if best is None or len(choices) < len(best[1]):
                    best = (i, choices)
            i, choices = best
            for j, endpoints in choices:
                added = {(a, b) for a, b in endpoints if a is not None and a not in mapping}
                for a, b in endpoints:
                    if not bind(mapping, inverse, a, b):
                        raise ValueError('inconsistent search candidate')
                if solve([v for v in ls if v != i], [v for v in rs if v != j]):
                    return True
                for a, b in added:
                    del mapping[a]
                    del inverse[b]
            return False

        if not solve(remaining_left, remaining_right):
            raise ValueError('no complete residual correspondence')
        if (len(mapping) != len(list(self.top_left.each_net())) or
                len(mapping) != len(list(self.top_right.each_net()))):
            raise ValueError('residual search did not map every net')
        hints = [(a, b) for a, b in mapping.items() if a not in initial]
        return hints, dict(instances=len(remaining_left), search_steps=steps, new_net_hints=len(hints))

    def run(self, expected_ports, metal_sheet_resistance):
        hints, refinement = self.refine()
        residual = None
        if not self.compare(hints):
            extra, residual = self.residual()
            if not self.compare(hints + extra):
                raise ValueError('complete guided comparison failed')
        result = audit(self.result, self.top_name, expected_ports,
                       metal_sheet_resistance=metal_sheet_resistance)
        return dict(status='graph_correspondence_pass', refinement=refinement,
                    residual=residual, independent_audit=result,
                    full_sram_qualified=False, full_chip_qualified=False)

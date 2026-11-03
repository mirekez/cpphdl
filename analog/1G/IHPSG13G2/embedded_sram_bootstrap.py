"""Match leaf interfaces before resolving the large embedded SRAM graph.

The preliminary comparison excludes only the SRAM and its ancestors. It is
used to obtain child-pin correspondence for structural refinement, never as
qualification. The subsequent comparison and independent audits include the
entire original layout/reference graphs without exclusions or black boxes.
"""
import klayout.db as db

from embedded_sram_lvs import EmbeddedSramCorrespondence


class BootstrappedSramCorrespondence(EmbeddedSramCorrespondence):
    def __init__(self, result, sram_name, top_name):
        super().__init__(result, sram_name, top_name)
        self.leaves_only = False

    def seed_leaves(self):
        self.leaves_only = False
        comparer = db.NetlistComparer()
        comparer.max_depth = 2
        comparer.max_branch_complexity = 20
        reference_classes = {c.name.lower(): c for c in self.reference.each_device_class()}
        for device_class in self.layout.each_device_class():
            target = reference_classes.get(device_class.name.lower())
            if target is not None:
                comparer.same_device_classes(device_class, target)
        for name, circuit in self.left.items():
            reference = self.right[name]
            if name in self.ancestors:
                comparer.same_circuits(circuit, None)
                comparer.same_circuits(None, reference)
                continue
            comparer.same_circuits(circuit, reference)
            for pin in circuit.each_pin():
                other = reference.pin_by_name(pin.name())
                if other is not None:
                    comparer.same_nets(circuit.net_for_pin(pin), reference.net_for_pin(other), True)
            for net in circuit.each_net():
                if net.name.startswith('__CPPHDL_FILLCAP_'):
                    other = reference.net_by_name(net.name)
                    if other is None:
                        raise ValueError('missing prepared leaf filler correspondence')
                    comparer.same_nets(net, other, True)
        self.result.xref().clear()
        if not self.result.compare(comparer):
            raise ValueError('preliminary leaf comparison failed')
        self.leaves_only = True
        pairs = self.circuit_pairs()
        return dict(matched_leaf_circuits=len(pairs), deferred_circuits=sorted(self.ancestors),
                    qualified=False)

    def circuit_pairs(self):
        if not self.leaves_only:
            return super().circuit_pairs()
        matched = {}
        for pair in self.result.xref().each_circuit_pair():
            if pair.first() is None or pair.second() is None:
                raise ValueError('missing preliminary leaf correspondence')
            name = pair.first().name.upper()
            if (name in matched or name in self.ancestors or pair.second().name.upper() != name
                    or pair.status() != db.NetlistCrossReference.Match):
                raise ValueError('invalid preliminary leaf correspondence')
            matched[name] = pair
        if set(matched) != set(self.left) - self.ancestors:
            raise ValueError('incomplete preliminary leaf correspondence')
        return matched

    def compare(self, hints):
        # The final comparer is newly constructed by the base implementation.
        # None of the preliminary exclusions can survive into qualification.
        self.leaves_only = False
        return super().compare(hints)

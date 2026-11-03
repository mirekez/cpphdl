"""Refine an SRAM inside an extracted circuit, then audit the entire circuit."""
import klayout.db as db

from sram_lvs_audit import audit
from sram_lvs_compare import Correspondence


class EmbeddedSramCorrespondence(Correspondence):
    def __init__(self, result, sram_name, top_name):
        super().__init__(result, sram_name)
        self.assembly_name = top_name.upper()
        if self.assembly_name not in self.left or self.assembly_name == self.top_name:
            raise ValueError('missing enclosing assembly')
        self.ancestors = {self.top_name}
        while True:
            parents = {name for name, circuit in self.left.items()
                       if any(child.circuit_ref().name.upper() in self.ancestors
                              for child in circuit.each_subcircuit())}
            expanded = self.ancestors | parents
            if expanded == self.ancestors:
                break
            self.ancestors = expanded
        if self.assembly_name not in self.ancestors:
            raise ValueError('assembly does not instantiate the SRAM')

    def circuit_pairs(self):
        # A parent can remain unmatched until its SRAM child is resolved.
        # Every other leaf must already match. Final comparison and audit still
        # include all parents, devices, instance connections and ports.
        mapped = {}
        for pair in self.result.xref().each_circuit_pair():
            if pair.first() is None or pair.second() is None:
                raise ValueError('missing embedded circuit correspondence')
            name = pair.first().name.upper()
            if name in mapped or pair.second().name.upper() != name:
                raise ValueError('ambiguous embedded circuit correspondence')
            if name not in self.ancestors and pair.status() != db.NetlistCrossReference.Match:
                raise ValueError('embedded leaf circuit does not match')
            mapped[name] = pair
        if set(mapped) != set(self.left):
            raise ValueError('incomplete embedded circuit correspondence')
        return mapped

    def run_assembly(self, sram_ports, assembly_ports, metal_sheet_resistance):
        hints, refinement = self.refine()
        residual = None
        if not self.compare(hints):
            extra, residual = self.residual()
            if not self.compare(hints + extra):
                raise ValueError('complete embedded comparison failed')
        sram_audit = audit(self.result, self.top_name, sram_ports,
                           metal_sheet_resistance=metal_sheet_resistance, allow_antenna_diodes=True)
        result = dict(refinement=refinement, residual=residual, independent_audit=sram_audit,
                      full_sram_qualified=False, full_chip_qualified=False)
        result['assembly_audit'] = audit(self.result, self.assembly_name, assembly_ports,
                                         metal_sheet_resistance=metal_sheet_resistance,
                                         allow_antenna_diodes=True)
        result['assembly_audit']['scope'] = 'complete SRAM/control assembly graph correspondence'
        result['status'] = 'assembly_graph_correspondence_pass'
        return result

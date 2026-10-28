"""Bind C++ IC ownership to the physical instances of the exported PLL."""
from collections import defaultdict
import re


def physical_groups(model, hierarchy):
    """Capacitors merged on one branch stay local only with a unique owner.

    Device identities come from the same Verilog-A export as the circuit. A
    transistor's position in a source file is not a hierarchy specification.
    """
    owners = {}
    for name, indices in hierarchy.items():
        if not isinstance(name, str) or not name or not isinstance(indices, list) or not indices:
            raise ValueError('empty or invalid reference-clock hierarchy group')
        for index in indices:
            if type(index) is not int or index < 0 or index in owners:
                raise ValueError('duplicate or invalid hierarchy device identity')
            owners[index] = name
    if not owners:
        raise ValueError('missing reference-clock hierarchy')
    names = ['Vss', 'Vdd', 'Vlogic', 'REF', 'CLEAR', 'OUT', 'OUT_N']
    names += sorted(set(re.findall(r'\bcpphdl_n\d+\b', model)))
    nodes = {name: 'n' + str(index) for index, name in enumerate(names)}
    result = {name: [] for name in hierarchy}
    capacitors = defaultdict(set)
    seen, physical_index = set(), 0
    for match in re.finditer(r'^\s*// device (\d+),[^\n]*\n\s*(I\([^\n]+)', model, re.M):
        identity, statement = int(match[1]), match[2]
        if identity in seen:
            raise ValueError('duplicate exported device identity')
        seen.add(identity)
        owner = owners.get(identity)
        if '* ddt(' in statement:
            branch = re.match(r'I\((\w+), (\w+)\)', statement)
            if not branch or any(n not in nodes for n in branch.groups()):
                raise ValueError('unknown storage branch in hierarchy')
            capacitors[tuple(nodes[n] for n in branch.groups())].add(owner)
        else:
            if 'cpphdl_channel(' in statement:
                kind = 'MOS'
            elif re.search(r'<\+ V\([^)]*\) / ', statement):
                kind = 'Resistance'
            else:
                raise ValueError('unsupported physical hierarchy contribution')
            if owner is not None:
                result[owner].append('Reference' + kind + str(physical_index))
            physical_index += 1
    if set(owners) - seen:
        raise ValueError('hierarchy refers to an absent exported device')
    for index, (_, branch_owners) in enumerate(sorted(capacitors.items())):
        if len(branch_owners) == 1:
            owner = next(iter(branch_owners))
            if owner is not None:
                result[owner].append('ReferenceStorage' + str(index))
    if any(not group for group in result.values()):
        raise ValueError('hierarchy group has no physical instances')
    return result

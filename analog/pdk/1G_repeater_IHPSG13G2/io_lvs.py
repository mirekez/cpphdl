"""Elaborate IHP's Cadence I/O CDL into the KLayout LVS reader dialect.

Cadence's sub! is one chip-internal substrate net. Thread it as an explicit
subcircuit terminal, keeping it internal at the padframe top. The PDK KLayout
reader represents ptap1/ntap1 as resistor-class devices, not X subcircuits.
Device dimensions/parameters and all other connections are preserved verbatim.
"""
import hashlib

BULK = "CPPHDL_SUBSTRATE"
TAPS = {"PTAP1", "NTAP1"}


def substrate_token(field):
    return field.upper() in ("SUB!", "$SUB=SUB!")


def records(text):
    output = []
    for line in text.splitlines():
        if line.lstrip().startswith("+"):
            if not output or output[-1].lstrip().startswith("*"):
                raise ValueError("orphan CDL continuation")
            output[-1] += " " + line.lstrip()[1:].strip()
        else:
            output.append(line)
    return output


def instance(line, names):
    fields = line.split()
    if not fields or not fields[0].upper().startswith("X"):
        return None
    fields = [field for field in fields if field != "/"]
    for index in range(len(fields) - 1, 0, -1):
        model = fields[index].lstrip("/").upper()
        if model in names or model in TAPS:
            fields[index] = fields[index].lstrip("/")
            return fields, index, model
    raise ValueError("unknown I/O CDL subcircuit: " + line)


def elaborate(text):
    lines = records(text)
    names, bodies = {}, {}
    current = None
    for line in lines:
        fields = line.split()
        if not fields or fields[0].startswith("*"):
            continue
        if fields[0].upper() == ".SUBCKT":
            if current is not None or len(fields) < 2 or fields[1].upper() in names:
                raise ValueError("malformed/duplicate I/O subcircuit")
            if any(field.upper() == BULK for field in fields[2:]):
                raise ValueError("I/O CDL collides with the explicit substrate terminal")
            current = fields[1].upper()
            names[current] = fields[1]
            bodies[current] = []
        elif fields[0].upper() == ".ENDS":
            if current is None:
                raise ValueError("unmatched I/O .ENDS")
            if len(fields) > 1 and fields[1].upper() != current:
                raise ValueError("mismatched I/O .ENDS name")
            current = None
        elif current is not None:
            if any(field.upper() == BULK for field in fields):
                raise ValueError("I/O CDL collides with the explicit substrate terminal")
            bodies[current].append(line)
    if current is not None or not names:
        raise ValueError("incomplete I/O CDL")
    needs_bulk = {name for name, body in bodies.items()
                  if any(any(substrate_token(field) for field in line.split()) for line in body)}
    changed = True
    while changed:
        changed = False
        for name, body in bodies.items():
            if name in needs_bulk:
                continue
            if any((parsed := instance(line, names)) is not None and parsed[2] in needs_bulk for line in body):
                needs_bulk.add(name)
                changed = True
    converted = 0
    output = ["* KLayout dialect adapter: explicit chip-internal substrate; unchanged device parameters."]
    current = None
    for line in lines:
        fields = line.split()
        if not fields or fields[0].startswith("*"):
            output.append(line)
        elif fields[0].upper() == ".SUBCKT":
            current = fields[1].upper()
            output.append(line + (" " + BULK if current in needs_bulk else ""))
        elif fields[0].upper() == ".ENDS":
            current = None
            output.append(line)
        else:
            parsed = instance(line, names)
            if parsed is not None:
                fields, index, model = parsed
                if model in TAPS:
                    fields[0] = "R" + fields[0]
                    converted += 1
                elif model in needs_bulk:
                    fields.insert(index, BULK)
            fields = [("$SUB=" if field.startswith("$") else "") + BULK
                      if substrate_token(field) else field for field in fields]
            output.append(" ".join(fields))
    result = "\n".join(output) + "\n"
    return result, {names[name] for name in needs_bulk}, dict(
        converted_tap_instances=converted, threaded_subcircuits=sorted(names[name] for name in needs_bulk),
        parameters_changed=False, source_sha256=hashlib.sha256(text.encode()).hexdigest(),
        result_sha256=hashlib.sha256(result.encode()).hexdigest())

"""Instantiate the pinned IHP sealring PCell without changing PDK geometry."""
from pathlib import Path
import sys
import klayout.db as db


def append_sealring(layout, top, pdk, die_um):
    python = Path(pdk) / "ihp-sg13g2/libs.tech/klayout/python"
    # Keep pypreprocessor's namespace-package parent on the search path. Adding
    # its repository root instead changes the package layout expected by IHP.
    for path in (python, python / "pycell4klayout-api/source/python"):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    import sg13g2_pycell_lib  # Registers SG13_dev with KLayout's PCell registry.
    library = db.Library.library_by_name("SG13_dev", "sg13g2")
    if library is None:
        raise ValueError("prepared IHP device PCell library is unavailable")
    declaration = library.layout().pcell_declaration("sealring")
    if declaration is None:
        raise ValueError("pinned IHP sealring PCell is unavailable")
    defaults = {parameter.name: parameter.default for parameter in declaration.get_parameters()}
    edge = float(str(defaults["edgeBox"]).removesuffix("u"))
    length = die_um - 2 * edge
    if length < max(float(str(defaults[name]).removesuffix("u")) for name in ("Lmin", "Wmin")):
        raise ValueError("die is too small for the actual PDK sealring")
    cell_index = layout.add_pcell_variant(library, declaration.id(),
        dict(l=f"{length:g}u", w=f"{length:g}u", addLabel="nil", addSlit="nil"))
    cell = layout.cell(cell_index)
    if cell is None or cell.is_empty():
        raise ValueError("IHP sealring generation produced no geometry")
    top.insert(db.CellInstArray(cell_index, db.Trans()))
    info = next((item for item in layout.layer_infos() if item.layer == 39 and item.datatype == 0), None)
    # EdgeSeal.drawing is layer 39/0 in the pinned SG13G2 technology.
    if info is None:
        raise ValueError("generated sealring omitted EdgeSeal.drawing")
    ring = db.Region(cell.begin_shapes_rec(layout.layer(info))).merged()
    holes = ring.holes()
    if ring.is_empty() or holes.count() != 1:
        raise ValueError("sealring is not one closed ring")
    die = db.Box(0, 0, round(die_um / layout.dbu), round(die_um / layout.dbu))
    if not (ring - db.Region(die)).is_empty():
        raise ValueError("sealring exceeds the declared die boundary")
    return dict(source="pinned SG13_dev/sealring PCell", die_um=die_um, edge_box_um=edge,
                parameters=dict(l=f"{length:g}u", w=f"{length:g}u", addLabel="nil", addSlit="nil"),
                ring_holes=holes.count(), pcell=cell.name)

"""Pack legal metal fill into holes left by the pinned PDK pattern grid."""
import pya


def add_metal_remainder(layout, top, number, name):
    extent = top.bbox()
    chip_area = pya.Region(extent).area()
    if chip_area <= 0:
        raise ValueError('cannot fill an empty module')

    def units(value):
        return round(value / layout.dbu)

    def layer(number, datatype):
        return pya.Region(top.begin_shapes_rec(layout.layer(number, datatype)))

    def density():
        metal = (layer(number, 0) | layer(number, 22)) - layer(number, 24)
        return 100 * metal.merged().area() / chip_area

    initial = density()
    if initial >= 36:
        return dict(initial_percent=initial, final_percent=initial, added_area_um2=0)
    tile = layout.create_cell(name + '_Remainder')
    square = pya.Box(0, 0, units(1), units(1))
    tile.shapes(layout.layer(number, 22)).insert(square)
    before = layer(number, 22).area()
    # One global lattice per pass guarantees spacing between new squares.
    # Recompute keepouts after every pass, including its newly inserted fill.
    for x, y in ((.710, .710), (.710, 0), (0, .710)):
        blocked = ((layer(number, 0) | layer(number, 22)).sized(units(.425)) |
                   layer(26, 0).sized(units(1)) | layer(number, 23) |
                   layer(number, 24) | layer(160, 0))
        available = pya.Region(extent) - blocked
        top.fill_region(available, tile.cell_index(), square,
                        pya.Vector(units(1.425), 0), pya.Vector(0, units(1.425)),
                        pya.Point(units(x), units(y)))
        if density() >= 36:
            break
    added = layer(number, 22).area() - before
    if not added:
        layout.delete_cell(tile.cell_index())
    result = dict(initial_percent=initial, final_percent=density(),
                  added_area_um2=added * layout.dbu ** 2)
    if result['final_percent'] < 35:
        raise ValueError(name + ' remains below 35% after legal remainder fill: ' + str(result))
    return result


def add_metal2_remainder(layout, top):
    return add_metal_remainder(layout, top, 10, 'Metal2')


def add_metal3_remainder(layout, top):
    return add_metal_remainder(layout, top, 30, 'Metal3')


def add_metal4_remainder(layout, top):
    return add_metal_remainder(layout, top, 50, 'Metal4')

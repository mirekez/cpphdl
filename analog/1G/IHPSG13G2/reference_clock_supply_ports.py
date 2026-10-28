"""Expose the PLL's real upper-metal supply conductors as integration pins."""
import klayout.db as db


def promote_supply_ports(layout, top, info):
    if len(info.get('upper_ground_straps', [])) != 2:
        raise ValueError('supply pins require the verified upper ground straps')
    changes = []
    for net, target in (('VSS', 134), ('VLOGIC', 126), ('VDD', 126)):
        port = info['terminals'][net]
        if port['layer'] != 67:
            raise ValueError('unexpected original supply pin layer')
        point = db.Point(round(port['x_um'] / layout.dbu), round(port['y_um'] / layout.dbu))
        labels = [shape for shape in top.shapes(layout.layer(67, 25)).each()
                  if shape.is_text() and shape.text.string == net]
        pins = [shape for shape in top.shapes(layout.layer(67, 2)).each()
                if (shape.is_box() or (shape.is_polygon() and shape.polygon.is_box()))
                and shape.bbox().contains(point)]
        if len(labels) != 1 or len(pins) != 1:
            raise ValueError('ambiguous original supply pin: ' + net)
        landing = pins[0].bbox()
        conductor = db.Region(top.begin_shapes_rec(layout.layer(target, 0)))
        if not (db.Region(landing) - conductor).is_empty():
            raise ValueError('upper supply pin lacks a physical conductor: ' + net)
        label = labels[0].text
        top.shapes(layout.layer(target, 2)).insert(landing)
        top.shapes(layout.layer(target, 25)).insert(label)
        labels[0].delete()
        pins[0].delete()
        port['layer'] = target
        changes.append(dict(net=net, from_layer=67, to_layer=target,
                            x_um=port['x_um'], y_um=port['y_um']))
    # Metal and every internal connection remain present. Chip routing must
    # connect these upper pins; lower-metal pin access is no longer assumed.
    info['promoted_supply_ports'] = changes

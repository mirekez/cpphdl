"""Candidate replica bias using physical HV MOS charge storage.

The exported 50 pF intention is a qualification target, not a claim that MOS
charge is linear. This factory leaves the literal MIM realization unchanged.
"""
import re

import klayout.db as db

from mos_gate_storage_adapter import append_hv_storage_adapter
from mos_layout import append_mos
from reference_clock_row import create_row
from resistor_layout import append_rppd
from transmit_translation_bias_layout import PORTS, circuit


def create_mos_translation_bias(pdk, source):
    devices, storage = circuit(source)

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            if device['kind'] == 'R':
                cell, info = append_rppd(layout, pdk, 'ReplicaResistance' + str(index),
                    device['value'], width_um=2)
                binding = dict(PLUS=device['a'], MINUS=device['b'], SUB='VSS')
                kind = 'resistor'
            else:
                cell, info = append_mos(layout, pdk, 'ReplicaMOS', device['width_um'],
                    device['length_um'], 20, 'n', 'hv')
                binding = dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body'])
                kind = 'mos'
            result.append((cell, info, binding, kind))
        cell, info = append_hv_storage_adapter(layout, pdk, 'ReplicaMOSStorage')
        # All NMOS and resistor guards see the same physical substrate. Carry
        # that substrate through the storage hierarchy without idealizing taps.
        text = info['schematic']
        for old, new in (
                ('.subckt ReplicaMOSStorage PLUS MINUS\n',
                 '.subckt ReplicaMOSStorage PLUS MINUS BULK\n'),
                ('Xstorage PLUS MINUS ReplicaMOSStorageBank\n',
                 'Xstorage PLUS MINUS BULK ReplicaMOSStorageBank\n'),
                ('.subckt ReplicaMOSStorageBank G SUB\n',
                 '.subckt ReplicaMOSStorageBank G SUB BULK\n')):
            if text.count(old) != 1:
                raise ValueError('HV storage substrate interface changed')
            text = text.replace(old, new)
        info['schematic'] = re.sub(r'\bbulk\b', 'BULK', text)
        info['requested_capacitance_F'] = storage
        result.append((cell, info, dict(PLUS='BIAS', MINUS='VSS'), 'capacitor'))
        return result

    layout, top, info = create_row('TransmitTranslationBias', PORTS, parts,
        priority_nets=('BIAS', 'DIVIDED'), via_columns=2, signal_width_um=2)
    info['schematic'], count = re.subn(
        r'^(X\d+ BIAS VSS) ReplicaMOSStorage$', r'\1 PSUB ReplicaMOSStorage',
        info['schematic'], flags=re.M)
    if count != 1:
        raise ValueError('ambiguous storage instance substrate binding')
    _add_plate_routes(layout, top, info)
    info.update(storage_realization='hv_gate_candidate', required_storage_F=storage,
        status='candidate_unqualified',
        scope='export-bound replica with nonlinear HV storage; requires local geometry, '
              'charge, cold startup, loaded-bank and complete-transmitter qualification')
    return layout, top, info


def _add_plate_routes(layout, top, info):
    storage, = [part for part in info['components'] if part['name'] == 'ReplicaMOSStorage']
    dx, dy = storage['origin_um']
    pins = storage['geometry']['terminals']
    plus = (pins['PLUS']['x_um'] + dx, pins['PLUS']['y_um'] + dy)
    minus = (pins['MINUS']['x_um'] + dx, pins['MINUS']['y_um'] + dy)

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for net, layer, (x, y) in (('BIAS', 126, plus), ('VSS', 134, minus)):
        port = info['terminals'][net]
        px, py = port['x_um'], port['y_um']
        # Route outside the storage bank: bias on its left, ground on its
        # right. Distinct upper layers permit the two plate networks to cross.
        box(layer, min(px, x) - 4, py - 4, max(px, x) + 4, py + 4)
        box(layer, x - 4, min(py, y) - 4, x + 4, max(py, y) + 4)
        # Only a narrow Metal5 landing is needed at the old row port. A wide
        # square here would bridge the adjacent row's signal rail.
        box(67, px - 2.5, py - .74, px + 2.5, py + .74)
        box(126, px - 3, py - 3, px + 3, py + 3)
        for i in range(5):
            cx = px + (i - 2) * .84
            box(125, cx - .21, py - .21, cx + .21, py + .21)
        if layer == 134:
            box(134, px - 3, py - 3, px + 3, py + 3)
            for ox in (-2, 0, 2):
                for oy in (-2, 0, 2):
                    box(133, px + ox - .45, py + oy - .45,
                        px + ox + .45, py + oy + .45)
        point = db.Point(round(px / layout.dbu), round(py / layout.dbu))
        shapes = [s for s in top.shapes(layout.layer(67, 2)).each()
                  if (s.is_box() or s.is_polygon()) and s.bbox().contains(point)]
        labels = [s for s in top.shapes(layout.layer(67, 25)).each()
                  if s.is_text() and s.text.string == net]
        if len(shapes) != 1 or len(labels) != 1:
            raise ValueError('ambiguous replica plate port: ' + net)
        top.shapes(layout.layer(layer, 2)).insert(shapes[0].bbox())
        top.shapes(layout.layer(layer, 25)).insert(labels[0].text)
        shapes[0].delete()
        labels[0].delete()
        port['layer'] = layer
    info.update(width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        storage_plate_routes=dict(width_um=8, bias_plate_um=plus,
            ground_plate_um=minus, shared_substrate='PSUB'))

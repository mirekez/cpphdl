#!/usr/bin/env python3
"""Route real controller/clock macro pins; retain physical geometry and failures.

This creates a connected subsystem, not a full repeater. LEF abstracts come
from the actual GDS metal. Only OpenROAD's detailed routes become new GDS metal.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

import klayout.db as db

from package_development_layout import ROOT, digest, hierarchy_digest
from route_tech import prepare
from tool_process import configure_cleanup, execute

HERE = Path(__file__).resolve().parent
TOP = 'ProcessingClockSystem'
LAYERS = {8: 'Metal1', 10: 'Metal2', 30: 'Metal3', 50: 'Metal4',
          67: 'Metal5', 126: 'TopMetal1', 134: 'TopMetal2'}
CONTROL = ROOT / 'build/clock-integration/clock-drift-revision/physical/ProcessingClockControl-6j7d36rr'
CLOCK = ROOT / 'build/clock-integration/compact-delivery-clock-priority'


def physical_name(name):
    return re.sub(r'\[(\d+)\]', r'__\1', name)


def connections():
    # The raw oscillator clocks measurement even while ADMIT stops OUT.
    # These bindings follow processing_clock_delivery.cpp's controller bridge.
    return [('RAW', 'pcs_clk'), ('ADMIT', 'ready_pcs_out'), ('CLEAR', 'clear_out'),
            ('VDD', 'VDD'), ('VSS', 'VSS')] + [
        (f'{kind.upper()}_N{bit}', f'{kind}_n_out[{bit}]')
        for kind in ('coarse', 'fine') for bit in range(8)]


def abstract(path, top_name, name, work):
    layout = db.Layout()
    layout.read(str(path))
    cell = layout.cell(top_name)
    if cell is None or cell.is_empty() or abs(layout.dbu - .001) > 1e-12:
        raise ValueError('missing 1 nm macro geometry')
    geometry = hierarchy_digest(cell)
    box = cell.bbox()
    shift = db.Trans(-box.left, -box.bottom)
    metal = {number: db.Region(cell.begin_shapes_rec(layout.layer(number, 0))).merged()
             for number in LAYERS}
    pins = {}
    access = {}
    selected = {number: db.Region() for number in LAYERS}
    for number in LAYERS:
        for shape in cell.shapes(layout.layer(number, 25)).each():
            if not shape.is_text():
                continue
            pin = shape.text.string
            if pin in pins:
                raise ValueError('ambiguous terminal: ' + pin)
            point = shape.text.trans.disp
            region = metal[number].interacting(db.Region(
                db.Box(point.x - 1, point.y - 1, point.x + 1, point.y + 1)))
            if region.is_empty():
                raise ValueError('terminal has no physical metal: ' + pin)
            selected[number] += region
            pins[pin] = (number, region)
            access[pin] = dict(layer=LAYERS[number],
                               point_dbu=[point.x - box.left, point.y - box.bottom])
    if not pins:
        raise ValueError('macro has no labelled physical interface')

    def shapes(region):
        lines = []
        for polygon in region.transformed(shift).each():
            # Rectangular decomposition keeps exact Manhattan metal rather
            # than obstructing a whole polygon bounding box or inventing pins.
            for piece in polygon.decompose_trapezoids():
                if not piece.is_box():
                    raise ValueError('nonrectangular macro routing metal')
                b = piece.bbox()
                lines.append('      RECT ' + ' '.join(f'{v * layout.dbu:.3f}' for v in
                    (b.left, b.bottom, b.right, b.top)) + ' ;')
        return lines

    lines = [f'MACRO {name}', '  CLASS BLOCK ;', '  ORIGIN 0 0 ;',
             f'  SIZE {box.width() * layout.dbu:.3f} BY {box.height() * layout.dbu:.3f} ;',
             '  SYMMETRY X Y ;']
    for pin, (number, region) in sorted(pins.items()):
        direction = 'INOUT' if pin in ('VDD', 'VSS') else ('OUTPUT' if
            pin.endswith('_out') or '_out[' in pin or pin in ('RAW', 'OUT') else 'INPUT')
        # Supplies are explicitly routed below; classification as SIGNAL lets
        # the macro router connect them rather than silently skipping PG nets.
        lines += [f'  PIN {physical_name(pin)}', f'    DIRECTION {direction} ;', '    USE SIGNAL ;',
                  '    PORT', f'      LAYER {LAYERS[number]} ;', *shapes(region),
                  '    END', f'  END {physical_name(pin)}']
    lines.append('  OBS')
    for number in LAYERS:
        region = metal[number] - selected[number]
        if number in (10, 30, 50, 67):
            # IHP Mn.e requires 0.24 um beside >0.39 um metal over a
            # parallel run >1 um. The router admitted 0.22 um beside
            # decomposed macro obstructions. Reserve 40 nm additionally
            # in the abstract; the source GDS remains unchanged.
            region = region.sized(40) - selected[number]
        if not region.is_empty():
            lines += [f'    LAYER {LAYERS[number]} ;', *shapes(region)]
    lines += ['  END', f'END {name}']
    # Normalize geometry once so LEF coordinates and GDS instance origins agree.
    target = db.Layout()
    target.dbu = layout.dbu
    top = target.create_cell(name)
    copied = target.create_cell(name + '_devices')
    copied.copy_tree(cell)
    top.insert(db.CellInstArray(copied.cell_index(), shift))
    if hierarchy_digest(copied) != geometry:
        raise ValueError('macro import changed geometry')
    target.write(str(work / (name + '.gds')))
    return '\n'.join(lines), sorted(pins), dict(original_geometry_sha256=geometry,
        width_um=box.width() * layout.dbu, height_um=box.height() * layout.dbu,
        port_layers={pin: LAYERS[data[0]] for pin, data in pins.items()}, access=access)


def identifier(name):
    return physical_name(name)


def placements(clock, control):
    # Face the controller's south-edge pins toward the analog south-edge
    # clock terminals. Align the RAW receiver before routing the slower trim.
    x = 100000 + clock['access']['RAW']['point_dbu'][0] - (
        round(control['width_um'] * 1000) - control['access']['pcs_clk']['point_dbu'][0])
    return dict(delivery=dict(x=100000, y=500000, orientation='R0'),
                control=dict(x=x, y=100000, orientation='R180'))


def placed_terminal(macro, placement, pin):
    x, y = macro['access'][pin]['point_dbu']
    if placement['orientation'] == 'R180':
        x, y = round(macro['width_um'] * 1000) - x, round(macro['height_um'] * 1000) - y
    return x + placement['x'], y + placement['y']


def verilog(clock_pins, control_pins):
    bindings = dict(connections())
    external_control = {'enable_in', 'fault_out', 'ready_ref_out', 'ref_clk', 'reset'} | {
        f'measured_edges_out[{bit}]' for bit in range(14)}
    if (set(clock_pins) != set(bindings) | {'OUT'} or
            set(control_pins) != set(bindings.values()) | external_control):
        raise ValueError('physical interfaces differ from the clock/control contract')
    control_nets = {pin: pin for pin in control_pins}
    for net, pin in connections():
        control_nets[pin] = net
    external = sorted((set(control_pins) - set(bindings.values())) | {'VDD', 'VSS', 'OUT'})
    all_nets = set(external) | set(bindings)
    lines = []
    for name, pins in [(TOP, external)]:
        lines.append('module ' + name + '(' + ', '.join(identifier(p) for p in pins) + ');')
        for pin in pins:
            direction = 'inout' if pin in ('VDD', 'VSS') else ('output' if
                pin.endswith('_out') or '_out[' in pin or pin in ('RAW', 'OUT') else 'input')
            lines.append(direction + ' ' + identifier(pin) + ';')
        if name == TOP:
            lines += ['wire ' + identifier(n) + ';' for n in sorted(all_nets - set(external))]
            for master, instance, ports, nets in [('ClockDeliveryMacro', 'delivery', clock_pins, {p:p for p in clock_pins}),
                                                 ('ClockControlMacro', 'control', control_pins, control_nets)]:
                lines.append(master + ' ' + instance + '(' + ', '.join(
                    '.' + identifier(p) + '(' + identifier(nets[p]) + ')' for p in ports) + ');')
        lines.append('endmodule')
    return '\n'.join(lines) + '\n', external


def stream(pdk, work):
    technology = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    tech = db.Technology()
    tech.load(str(technology / 'sg13g2.lyt'))
    options = tech.load_layout_options
    config = options.lefdef_config
    config.lef_files = [str(work / 'route_tech.lef'), str(work / 'macros.lef')]
    config.map_file = str(technology / 'sg13g2.map')
    config.produce_cell_outlines = False
    config.produce_obstructions = False
    config.produce_blockages = False
    options.lefdef_config = config
    layout = db.Layout()
    layout.read(str(work / 'system.def'), options)
    top = layout.cell(TOP)
    for name in ('ClockDeliveryMacro', 'ClockControlMacro'):
        cell = layout.cell(name)
        if cell is None:
            raise ValueError('routed system lost a macro')
        cell.clear()
        layout.read(str(work / (name + '.gds')))
    # DEF's routing canvas is not a physical padframe or macro obstruction.
    # Bound this reusable subsystem by its actual devices and routed metal.
    boundary = layout.find_layer(189, 4)
    if boundary is not None:
        top.shapes(boundary).clear()
        top.shapes(boundary).insert(top.bbox())
    result = db.Layout()
    result.dbu = layout.dbu
    final = result.create_cell(TOP)
    final.copy_tree(top)
    if any(cell.is_empty() for cell in result.each_cell()):
        raise ValueError('stream contains an empty abstract')
    result.write(str(work / 'system.gds'))
    box = final.dbbox()
    return [box.left, box.bottom, box.right, box.top]


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if (work / 'checks.json').exists():
        raise ValueError('use a fresh work directory; previous evidence is retained')
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    report = dict(status='running', top=TOP, full_chip_qualified=False,
                  electrical_qualified=False, lvs_qualified=False, drc_qualified=False,
                  connections=[dict(clock=a, control=b) for a,b in connections()])
    def save():
        (work / 'checks.json').write_text(json.dumps(report,indent=2)+'\n')
    try:
        inputs = [Path(__file__), args.clock / 'compact.gds', args.control / 'repeater_core_unfilled.gds',
                  args.clock / 'compact.cir', args.control / 'core.cdl',
                  ROOT / '1G/IHPSG13G2/processing_clock_delivery.hpp',
                  ROOT / '1G/IHPSG13G2/tests/processing_clock_delivery.cpp',
                  ROOT / '1G/processing_clock_control.hpp',
                  HERE / 'route_tech.py', HERE / 'package_development_layout.py', HERE / 'tool_process.py',
                  tools / 'bin/openroad', tools / 'root/usr/bin/openroad',
                  pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef',
                  pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef',
                  pdk / 'ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyt',
                  pdk / 'ihp-sg13g2/libs.tech/klayout/tech/sg13g2.map']
        report['source_sha256'] = {str(p.resolve()):digest(p) for p in inputs}
        prepare(pdk,work)
        clock, cpins, cg = abstract(args.clock / 'compact.gds', 'ProcessingClockDelivery', 'ClockDeliveryMacro', work)
        control, dpins, dg = abstract(args.control / 'repeater_core_unfilled.gds', 'ProcessingClockControl', 'ClockControlMacro', work)
        report['macros'] = dict(delivery=cg, control=dg)
        position = placements(cg, dg)
        report['placements'] = position
        report['terminal_manhattan_um'] = {}
        for analog_pin, control_pin in connections():
            a = placed_terminal(cg, position['delivery'], analog_pin)
            b = placed_terminal(dg, position['control'], control_pin)
            report['terminal_manhattan_um'][analog_pin] = sum(abs(x - y) for x, y in zip(a, b)) / 1000
        (work / 'macros.lef').write_text('VERSION 5.8 ;\nBUSBITCHARS "[]" ;\nDIVIDERCHAR "/" ;\n'+clock+'\n'+control+'\nEND LIBRARY\n')
        netlist, ports = verilog(cpins,dpins)
        report['ports'] = ports
        (work / 'system.v').write_text(netlist)
        # Port-only Liberty prevents hierarchy flattening from deleting empty
        # Verilog macro stubs. It supplies no timing arcs or timing pass claim.
        liberty = ['library (physical_interfaces) {', 'delay_model : table_lookup;',
                   'time_unit : "1ns";', 'voltage_unit : "1V";',
                   'current_unit : "1mA";', 'capacitive_load_unit (1, pf);',
                   'nom_voltage : 1.2;', 'nom_temperature : 25;', 'nom_process : 1;']
        for name, pins in [('ClockDeliveryMacro', cpins), ('ClockControlMacro', dpins)]:
            liberty.append('cell (' + name + ') { area : 1;')
            for pin in pins:
                direction = 'inout' if pin in ('VDD', 'VSS') else ('output' if
                    pin.endswith('_out') or '_out[' in pin or pin in ('RAW', 'OUT') else 'input')
                liberty.append('pin ("' + physical_name(pin) + '") { direction : ' + direction + '; }')
            liberty.append('}')
        liberty.append('}')
        (work / 'interfaces.lib').write_text('\n'.join(liberty) + '\n')
        # Remaining interface ports stay on their real macro terminal metal.
        # Routing them to an artificial perimeter adds unnecessary wires and
        # prevents this subsystem from replacing the neighboring die blocks.
        fixed_pins = []
        for pin in ports:
            macro, place = (cg, position['delivery']) if pin in ('VDD', 'VSS', 'OUT') else (dg, position['control'])
            terminal = macro['access'][pin]
            x, y = placed_terminal(macro, place, pin)
            fixed_pins.append(' '.join(map(str, (physical_name(pin), terminal['layer'], x, y))))
        (work / 'pins.tcl').write_text('''foreach {name layer x y} {
''' + '\n'.join(fixed_pins) + '''
} {
    set pin [odb::dbBPin_create [$block findBTerm $name]]
    $pin setPlacementStatus PLACED
    odb::dbBox_create $pin [[ord::get_db_tech] findLayer $layer] [expr {$x - 100}] [expr {$y - 100}] [expr {$x + 100}] [expr {$y + 100}]
}
''')
        placement_lines = [' '.join(map(str, (name, p['orientation'], p['x'], p['y'])))
                           for name, p in position.items()]
        (work / 'placement.tcl').write_text('''foreach {name orientation x y} {
''' + '\n'.join(placement_lines) + '''
} {
    set inst [$block findInst $name]
    $inst setOrient $orientation
    $inst setLocation $x $y
    $inst setPlacementStatus FIRM
    set box [$inst getBBox]
    if {[$box xMin] != $x || [$box yMin] != $y} { error "macro placement origin differs" }
}
''')
        script = '''set_thread_count 1
read_lef $::env(WORK)/route_tech.lef
read_lef $::env(PDK)/ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef
read_lef $::env(WORK)/macros.lef
read_liberty $::env(WORK)/interfaces.lib
read_verilog $::env(WORK)/system.v
link_design ProcessingClockSystem
initialize_floorplan -die_area {0 0 850 1100} -core_area {10 10 840 1090} -site CoreSite
set block [ord::get_db_block]
source $::env(WORK)/placement.tcl
make_tracks
source $::env(WORK)/pins.tcl
set_routing_layers -signal Metal2-TopMetal2 -clock Metal3-TopMetal2
set_global_routing_layer_adjustment Metal2-TopMetal2 0.2
global_route -guide_file $::env(WORK)/system.guide
write_db $::env(WORK)/global.odb
detailed_route -disable_via_gen -output_drc $::env(WORK)/route_drc.rpt -output_maze $::env(WORK)/route_maze.log
write_db $::env(WORK)/system.odb
write_def $::env(WORK)/system.def
exit
'''
        (work / 'route.tcl').write_text(script)
        save()
        with (work / 'route.log').open('w') as log:
            result = execute([str(tools / 'bin/openroad'), '-no_init', '-exit', str(work / 'route.tcl')],
                env=dict(os.environ, WORK=str(work), PDK=str(pdk)), stdout=log, stderr=-2, timeout=600,
                memory_exclusive=True)
        report['route_exit'] = result.returncode
        if result.returncode:
            raise ValueError('macro routing failed; inspect route.log')
        if (work / 'route_drc.rpt').read_text().strip():
            raise ValueError('detailed router left geometry violations')
        report['router_drc_violations'] = 0
        report['bounds_um'] = stream(pdk, work)
        if any(digest(Path(p))!=h for p,h in report['source_sha256'].items()):
            raise ValueError('integration inputs changed')
        report.update(status='routed_candidate', scope='controller, oscillator and admission gate; chip integration and electrical qualification remain open')
    except BaseException as error:
        report.update(status='fail',error=str(error))
        raise
    finally:
        report['artifact_sha256']={p.name:digest(p) for p in work.iterdir() if p.is_file() and p.name!='checks.json'}
        save()
    return report


if __name__ == '__main__':
    configure_cleanup()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--clock',type=Path,default=CLOCK)
    parser.add_argument('--control',type=Path,default=CONTROL)
    parser.add_argument('--pdk',type=Path,default=ROOT/'pdk/IHPSG13G2')
    parser.add_argument('--tools',type=Path,default=ROOT/'out/repeater-toolchain')
    print(json.dumps(run(parser.parse_args()),indent=2))

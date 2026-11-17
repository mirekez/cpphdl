#!/usr/bin/env python3
"""Placed-device image map, checked against actual Analog++ element names.

The historical D-number is retained as the physical PCell identity because
changing it would invalidate GDS/LVS/extraction receipts. The model names are
declared in primitives.h and checked against the C++ export before being drawn.
"""

import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
GENERIC = HERE.parent


def source_hashes():
    return {str(path.relative_to(GENERIC)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (GENERIC / "adc_sar_slice.h", GENERIC / "primitives.h")}


def names_for(placement):
    components = placement["components"]
    by_id = {int(part["name"][1:]): part for part in components}
    if len(by_id) != len(components) or len(by_id) != 137:
        raise ValueError("expected 137 distinct D-numbered physical components")
    counters = defaultdict(int)
    aliases = {}

    def add(number, path, prefix, role, intent):
        kind = {"M": "mos", "R": "resistor", "C": "capacitor"}[prefix]
        part = by_id.get(number)
        if part is None or part["kind"] != kind:
            raise ValueError(f"D{number} no longer matches the {path}/{role} topology")
        index = counters[path, prefix]
        counters[path, prefix] += 1
        # The map's top field supplies adc_sar_slice once for every relative path.
        alias = f"{path}/{prefix}{index}_{role}"
        if number in aliases:
            raise ValueError(f"duplicate semantic assignment for D{number}")
        aliases[number] = dict(physical_name=f"D{number}", hierarchical_name=alias,
                               kind=kind, intent=intent)

    # sample_hold::_assign(): transmission gate, storage, clock complement,
    # charge-cancelling dummies and three physical series coupling capacitors.
    add(0,"sampler","M","track_n","NMOS input-to-hold sampling switch")
    add(1,"sampler","M","track_p","PMOS input-to-hold sampling switch")
    add(3,"sampler","C","capture","Main sampled-voltage storage capacitor")
    add(4,"sampler","M","clock_inverter_n","Pulls the complemented sampling clock low")
    add(5,"sampler","M","clock_inverter_p","Pulls the complemented sampling clock high")
    add(6,"sampler","M","dummy_n","Cancels NMOS sampling-switch charge injection")
    add(7,"sampler","M","dummy_p","Cancels PMOS sampling-switch charge injection")
    add(9,"sampler","C","clock_load","Physical load on the complemented sampling clock")
    for index,number in enumerate((11,13,15)):
        add(number,"sampler","C",f"off_coupling_series{index}",
            "One series MIM plate modeling input-to-hold off-state coupling")

    # r2r_dac::_assign(): each bit has a rail driver and 2R leg. Bits 1..7
    # additionally have an R inter-tap link. Tap and driver loads come last.
    for bit in range(8):
        path=f"ladder/bit{bit}"
        first=16 if bit==0 else 19+4*(bit-1)
        add(first,path,"M","driver_n","Pulls this complemented-bit DAC driver low")
        add(first+1,path,"M","driver_p","Pulls this complemented-bit DAC driver high")
        add(first+2,path,"R","weight_2r","2R resistor coupling this bit driver to its ladder tap")
        if bit:
            add(first+3,path,"R","tap_link_r","R resistor linking this tap to the preceding ladder tap")
    add(47,"ladder","R","termination_2r","2R ground termination at the least-significant ladder tap")
    for bit in range(8):
        path=f"ladder/bit{bit}"
        add(48+2*bit,path,"C","tap_load","Physical ladder-tap storage and settling load")
        add(49+2*bit,path,"C","driver_load","Physical bit-driver output load")

    # preamplifier::_assign(): input pair and its finite output, source and
    # tail loads. The held branch is PLUS, the DAC branch is MINUS.
    for number,prefix,role,intent in (
        (64,"M","held_input","Input transistor sensing the sampled voltage"),
        (65,"M","dac_input","Input transistor sensing the DAC trial voltage"),
        (66,"R","negative_output_load","Load from the held-side output to the IO supply"),
        (67,"R","positive_output_load","Load from the DAC-side output to the IO supply"),
        (68,"R","held_degeneration","Source degeneration on the held-input branch"),
        (69,"R","dac_degeneration","Source degeneration on the DAC-input branch"),
        (70,"R","tail_return","Shared preamplifier tail return to ground"),
        (71,"C","negative_output_load","Storage on the held-side preamplifier output"),
        (72,"C","positive_output_load","Storage on the DAC-side preamplifier output"),
        (73,"C","tail_load","Storage on the shared preamplifier tail"),
        (74,"C","held_gate_source","Held-input gate-to-source coupling"),
        (75,"C","dac_gate_source","DAC-input gate-to-source coupling"),
        (76,"C","held_gate_drain","Held-input gate-to-drain kickback coupling"),
        (77,"C","dac_gate_drain","DAC-input gate-to-drain kickback coupling")):
        add(number,"amplifier",prefix,role,intent)

    # comparator::_assign(): differential input pair, regenerative cross
    # coupling, reset/precharge network and explicit dynamic-node storage.
    for number,prefix,role,intent in (
        (78,"M","plus_input","Comparator transistor sensing preamplifier PLUS"),
        (79,"M","minus_input","Comparator transistor sensing preamplifier MINUS"),
        (80,"M","evaluate_tail","Enables comparator tail current during evaluation"),
        (81,"M","outn_regen_n","NMOS regeneration from OUTP into OUTN"),
        (82,"M","outp_regen_n","NMOS regeneration from OUTN into OUTP"),
        (83,"M","outp_regen_p","PMOS regeneration from OUTN into OUTP"),
        (84,"M","outn_regen_p","PMOS regeneration from OUTP into OUTN"),
        (85,"M","left_precharge","Precharges the PLUS-side internal dynamic node"),
        (86,"C","left_internal","Storage on the PLUS-side internal dynamic node"),
        (87,"M","right_precharge","Precharges the MINUS-side internal dynamic node"),
        (88,"C","right_internal","Storage on the MINUS-side internal dynamic node"),
        (89,"M","outp_precharge","Precharges comparator OUTP"),
        (90,"M","outn_precharge","Precharges comparator OUTN"),
        (91,"C","tail_internal","Storage on the comparator tail node"),
        (92,"C","outp_load","Load on comparator OUTP"),
        (93,"C","outn_load","Load on comparator OUTN"),
        (94,"C","plus_gate_source","PLUS input gate-to-tail coupling"),
        (95,"C","minus_gate_source","MINUS input gate-to-tail coupling"),
        (96,"C","plus_gate_drain","PLUS input kickback to the left dynamic node"),
        (97,"C","minus_gate_drain","MINUS input kickback to the right dynamic node")):
        add(number,"comparison",prefix,role,intent)

    # decision_latch::_assign(): the two cross-coupled NAND halves precede
    # their comparator-isolation buffers, storage and active CLEAR devices.
    for branch,first in (("q",98),("qn",102)):
        path=f"decision/{branch}"
        add(first,path,"M","input_pullup","NAND pullup controlled by this branch input")
        add(first+1,path,"M","feedback_pullup","NAND pullup controlled by opposite retained output")
        add(first+2,path,"M","input_pulldown","NAND upper pulldown controlled by this branch input")
        add(first+3,path,"M","feedback_pulldown","NAND lower pulldown controlled by opposite retained output")
    for branch,first in (("set",106),("reset",116)):
        path=f"decision/{branch}"
        for offset,role,intent in (
            (0,"first_buffer_n","First isolation inverter NMOS driven by comparator output"),
            (1,"first_buffer_p","First isolation inverter PMOS driven by comparator output"),
            (2,"second_buffer_n","Second isolation inverter NMOS driving the NAND input"),
            (3,"second_buffer_p","Second isolation inverter PMOS driving the NAND input")):
            add(first+offset,path,"M",role,intent)
        for offset,role,intent in (
            (4,"input_load","Comparator-output load of this latch branch"),
            (5,"miller","Miller coupling across this branch's first isolation inverter"),
            (6,"first_buffer_load","Storage on the first isolation inverter output"),
            (7,"second_buffer_load","Storage on the second isolation inverter output"),
            (8,"retained_output","Charge storage on this branch's retained output"),
            (9,"stack_node","Charge storage in this branch's NAND stack")):
            add(first+offset,path,"C",role,intent)
    add(126,"decision/clear","M","inverter_n","NMOS generating the complement of CLEAR")
    add(127,"decision/clear","M","inverter_p","PMOS generating the complement of CLEAR")
    add(128,"decision/clear","C","inverter_load","Load on the complemented CLEAR node")
    add(129,"decision/clear","M","force_q_low","Forces retained Q low while CLEAR is asserted")
    add(130,"decision/clear","M","force_qn_high","Forces retained Q_N high while CLEAR is asserted")

    # core_interface::_assign(): ratioed HV-to-core sensing stage and its
    # core-domain output inverter, with explicit input/output coupling.
    for number,prefix,role,intent in (
        (131,"M","core_pullup","Always-on PMOS pullup of the ratioed interface node"),
        (132,"M","hv_input","HV NMOS sensing the retained latch output"),
        (133,"M","core_output_n","Core-domain output inverter NMOS"),
        (134,"M","core_output_p","Core-domain output inverter PMOS"),
        (135,"C","hv_input_ground","HV input capacitance to ground"),
        (136,"C","hv_input_core","HV input capacitance to the core rail"),
        (137,"C","input_feedback","Feedback coupling from HV input to ratioed node"),
        (138,"C","internal_ground","Storage on the ratioed node to ground"),
        (139,"C","internal_core","Storage on the ratioed node to the core rail"),
        (140,"C","output_feedback","Feedback coupling from ratioed node to core output"),
        (141,"C","decision_output","Load on the core-domain DECISION output")):
        add(number,"interface",prefix,role,intent)

    if set(aliases) != set(by_id):
        raise ValueError(f"semantic aliases do not cover {sorted(set(by_id)-set(aliases))}")
    full = [aliases[number] for number in sorted(aliases)]
    if len({item["hierarchical_name"] for item in full}) != 137:
        raise ValueError("hierarchical element names are not unique")
    return full


def check_model_export(elements, circuit_path):
    circuit=json.loads(circuit_path.read_text())
    actual={part["name"]: part.get("hierarchical_name") for part in circuit["devices"]}
    expected={part["physical_name"]: part["hierarchical_name"] for part in elements}
    if actual!=expected:
        differences=[name for name in sorted(set(actual)|set(expected)) if actual.get(name)!=expected.get(name)]
        raise ValueError(f"model names differ from placement aliases: {differences}")


def write_mapping(placement_path, output_path, circuit_path=None):
    placement=json.loads(placement_path.read_text())
    elements=names_for(placement)
    if circuit_path is not None:
        check_model_export(elements,circuit_path)
    result=dict(schema=1,top="adc_sar_slice",scope="names stored on Analog++ model elements; GDS/DEF D identities remain unchanged",
                model_source_sha256=source_hashes(),elements=elements)
    output_path.write_text(json.dumps(result,indent=2)+"\n")
    return result


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--placement",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--circuit",type=Path,help="verify against names exported by the C++ model")
    args=parser.parse_args()
    result=write_mapping(args.placement,args.output,args.circuit)
    print(f"Named {len(result['elements'])} physical elements")

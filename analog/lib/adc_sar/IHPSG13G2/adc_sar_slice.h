#pragma once
#include "../adc_sar_slice.h"

namespace analog::adc_sar::ihpsg13g2 {
// One analog slice, with an external SAR sequencer. Actual PDK model cards and
// layout passives are bound by this directory's process/layout adapter. This
// alias preserves the explicit device dimensions; it does not relabel generic
// square-law equations as foundry PSP equations.
inline constexpr device_parameters devices = [] {
    auto p=ihp130_profile2;
    // The extracted slow/hot decision path resolved the comparator at
    // 39.79 ns but delivered a core high only at 41.25 ns, after the
    // 40.975 ns deadline. Drive the real routed buffer/latch capacitance.
    auto strengthen=[](mos_parameters& m,double factor){m.width*=factor;m.beta*=factor;};
    // The 6 pF store exposed 22.28 mV slow/hot acquisition error at 2.05 V.
    // Scale the sampling pair, charge-cancelling dummies and complement
    // driver together, preserving their relative charge/drive balance.
    for(auto* m:{&p.sample,&p.dummy,&p.complement}){
        strengthen(m->n,1.5);strengthen(m->p,1.5);
    }
    strengthen(p.buffer_input.n,4);strengthen(p.buffer_input.p,4);
    strengthen(p.buffer_output.n,2);strengthen(p.buffer_output.p,2);
    strengthen(p.interface_input,2);
    // SG13G2 process specification Rev. 1.2, sections 2.4/2.5: the 3.3 V
    // VGS condition at 27 C requires LN >= .6 um and LP >= .5 um. The
    // minimum .45 um model geometry is not a 3.3 V reliability qualification.
    // Retain W/L to the 10 nm PCell grid, keeping each finger <= 10 um.
    auto rated=[](mos_parameters& m,double length){
        const double ratio=m.width/m.length,target=m.width*length/m.length;
        unsigned fingers=static_cast<unsigned>(target/10e-6);
        if(fingers*10e-6<target-1e-15)++fingers;
        if(fingers<m.fingers)fingers=m.fingers;
        const double finger=static_cast<unsigned>(target/fingers/10e-9+.5)*10e-9;
        m.width=finger*fingers;m.length=length;m.fingers=fingers;
        m.beta*=m.width/m.length/ratio;
    };
    auto pair=[&](pair_parameters& m){rated(m.n,.6e-6);rated(m.p,.5e-6);};
    pair(p.sample);pair(p.dummy);pair(p.complement);pair(p.dac);
    pair(p.feedback);pair(p.buffer_input);pair(p.buffer_output);pair(p.nand);pair(p.clear);
    rated(p.preamp,.6e-6);rated(p.comparator_input,.6e-6);rated(p.tail,.6e-6);
    rated(p.interface_input,.6e-6);rated(p.precharge,.5e-6);
    // The always-on core PMOS gate is tied to the full VSS rail. Its original
    // 2 um x 0.13 um gate failed the measured cumulative-metal antenna ratio.
    // Scale L by six and W by twelve: the larger area meets antenna rules,
    // while twice the drive matches the strengthened HV input transistor.
    // PSP tests qualify the actual capacitance, delay and static current.
    p.interface_pullup.width=24e-6;
    p.interface_pullup.length=.78e-6;
    p.interface_pullup.fingers=3;
    p.interface_pullup.beta*=2;
    return p;
}();
inline constexpr passive_parameters passives = [] {
    auto p=ihp130_passives;
    // PSP switching charge exceeded the hold limit at 1 pF. At 4 pF,
    // extracted routing still gave 22--23 mV after the ground-return fix.
    // Store more charge; qualify the 8 ns acquisition separately.
    p.hold=6e-12;
    return p;
}();
using adc_sar_slice = analog::adc_sar::adc_sar_slice<8,devices,passives>;
}

"""Connected trim-current and cascode references for the cascoded transmitter.

The topology follows trimmed_mirror_bias and the cascode divider in
cascoded_emphasized_pam5_driver. Storage and translated trim inputs belong to
the surrounding transmitter and are deliberately external to this macro.
"""
from mos_layout import append_mos
from reference_clock_row import create_row
from resistor_layout import append_rppd


PORTS = ['VDD', 'VSS', 'BIAS', 'CASCODE', *['TRIM_N' + str(bit) for bit in range(6)]]


def create_transmit_bias(pdk):
    def parts(layout):
        result = []

        def resistor(name, value, a, b, width):
            cell, info = append_rppd(layout, pdk, name, value, width_um=width)
            result.append((cell, info, dict(PLUS=a, MINUS=b, SUB='VSS'), 'resistor'))

        def channel(name, polarity, d, g, s, body):
            cell, info = append_mos(layout, pdk, name, 100, .45, 10, polarity, 'hv')
            result.append((cell, info, dict(D=d, G=g, S=s, B=body), 'mos'))

        resistor('BiasFeed', 1800, 'VDD', 'BIAS', 4)
        channel('BiasReference', 'n', 'BIAS', 'BIAS', 'VSS', 'VSS')
        for bit in range(6):
            branch = 'BRANCH' + str(bit)
            channel('BiasSwitch' + str(bit), 'p', branch, 'TRIM_N' + str(bit), 'VDD', 'VDD')
            resistor('BiasTrim' + str(bit), 256000 / (1 << bit), branch, 'BIAS', .5)
        resistor('CascodeUpper', 3000, 'VDD', 'CASCODE', 2)
        resistor('CascodeLower', 3600, 'CASCODE', 'VSS', 2)
        return result

    return create_row('TransmitBias', PORTS, parts, priority_nets=('BIAS', 'CASCODE'),
                      via_columns=4, signal_width_um=2)

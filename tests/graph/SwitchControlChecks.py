from FixtureChecks import FixtureChecks

if __name__ == '__main__':
    test = FixtureChecks('SwitchControl')
    if test.args.flow == 'cpp':
        test.native()
    elif test.args.flow == 'verilator':
        test.rtl()
    elif test.args.flow == 'graph':
        test.graph()
    else:
        test.name = 'SwitchEffects'
        test.native(['-Wl,--wrap=random'])
        test.graph(link_flags=['-Wl,--wrap=random'])

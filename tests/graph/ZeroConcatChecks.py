from FixtureChecks import FixtureChecks

if __name__ == '__main__':
    test = FixtureChecks('ZeroConcat')
    # Zero-width C++ values belong to the native graph contract. The optional
    # RTL flow below tests legal SV zero repetitions inside a concatenation.
    test.native()
    test.graph()
    if test.args.hdlcpp:
        source = test.fixture / 'ZeroRepeat.sv'
        runner = test.fixture / 'ZeroRepeatRun.cc'
        generated = test.work / 'zero-repeat.cc'
        test.run([test.args.hdlcpp, '--native-graph', '--top', 'ZeroRepeat',
                  '--output', generated, source], 'zero-repeat-frontend')
        output = test.work / 'zero-repeat-graph'
        test.run([test.args.cpphdl, '--native-graph', '--output', output,
                  '--cxx', test.args.cxx, '--runner', runner, generated, '--',
                  '-fsanitize=address,undefined'], 'zero-repeat-build')
        print(test.run([output / 'run'], 'zero-repeat-run'), end='')
    if test.args.verilator:
        test.run([test.args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                  '--top-module', 'ZeroRepeat', '--Mdir', test.work / 'repeat-obj',
                  '-CFLAGS', '-std=c++17 -DTEST_RTL', test.fixture / 'ZeroRepeat.sv',
                  test.fixture / 'ZeroRepeatRun.cc'], 'zero-repeat-verilator')
        print(test.run([test.work / 'repeat-obj/VZeroRepeat'], 'zero-repeat-rtl-run'), end='')

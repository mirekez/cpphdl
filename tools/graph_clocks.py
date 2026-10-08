"""Clock CLI shared by native-graph and gate synthesis drivers."""
import re


def add_clock_arguments(parser):
    parser.add_argument('--clock', nargs=2, action='append', default=[], metavar=('NAME', 'HZ'),
                        help='independent clock input; repeat for unrelated clocks')
    parser.add_argument('--primary_clock', nargs=2, metavar=('NAME', 'HZ'))
    parser.add_argument('--secondary_clock', nargs=2, action='append', default=[], metavar=('NAME', 'HZ'))


def clock_arguments(parser, args):
    if args.clock and (args.primary_clock or args.secondary_clock):
        parser.error('use --clock or primary/secondary clocks, not both')
    if args.secondary_clock and not args.primary_clock:
        parser.error('--secondary_clock requires --primary_clock')
    clocks = args.clock or ([args.primary_clock] if args.primary_clock else []) + args.secondary_clock
    names = set()
    result = []
    for name, frequency in clocks:
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) or name in names:
            parser.error('invalid or duplicate clock name: ' + name)
        if not re.fullmatch(r'[0-9]+', frequency) or not 0 < int(frequency) < 2**64:
            parser.error('clock frequency must be a positive 64-bit integer')
        names.add(name)
        result.extend(['--clock', name, frequency])
    if args.primary_clock and any(int(hz) > int(args.primary_clock[1]) for _, hz in args.secondary_clock):
        parser.error('primary clock must have the highest frequency')
    return result

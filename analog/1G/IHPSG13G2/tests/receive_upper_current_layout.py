#!/usr/bin/env python3
"""Regenerate and check the receive-reference candidate with separate sense and distributed current routes."""
import argparse
from pathlib import Path
import runpy
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import digest
from receive_common_current_routes import add_receive_current_routes
from receive_common_drain_contacts import add_receive_drain_contacts
from receive_common_sense_routes import add_receive_sense_routes
from receive_common_upper_current_routes import add_receive_upper_current_routes

# Reuse the complete established geometry checker: exact LVS, foundry local
# DRC, missing-sink rejection and conserved extraction all remain required.
checker = runpy.run_path(str(SOURCE.with_name('receive_common_layout.py')))
shared = checker['run'].__globals__
original_factory = shared['create_receive_common_reference']
original_dependencies = shared['source_dependencies']


def create(*args, **kwargs):
    layout, top, info = original_factory(*args, **kwargs)
    add_receive_current_routes(layout, top, info)
    add_receive_drain_contacts(layout, top, info)
    add_receive_sense_routes(layout, top, info)
    add_receive_upper_current_routes(layout, top, info)
    return layout, top, info


def dependencies(*args, **kwargs):
    hashes = original_dependencies(*args, **kwargs)
    paths = [SOURCE] + [SOURCE.parents[1] / name for name in (
        'receive_common_current_routes.py', 'receive_common_drain_contacts.py',
        'receive_common_sense_routes.py', 'receive_common_upper_current_routes.py')]
    hashes.update({str(path): digest(path) for path in paths})
    return hashes


if __name__ == '__main__':
    checker['configure_cleanup']()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'exporter', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    shared['create_receive_common_reference'] = create
    shared['source_dependencies'] = dependencies
    checker['run'](parser.parse_args())

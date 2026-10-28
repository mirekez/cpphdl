#!/usr/bin/env python3
"""Regenerate the receiver with direct upper-metal storage access."""
import argparse
from pathlib import Path

import receive_upper_current_layout as upper
from extract_analog import digest
from receive_common_storage_routes import add_receive_storage_routes

SOURCE = Path(__file__).resolve()


def create(*args, **kwargs):
    layout, top, info = upper.create(*args, **kwargs)
    add_receive_storage_routes(layout, top, info)
    return layout, top, info


def dependencies(*args, **kwargs):
    hashes = upper.dependencies(*args, **kwargs)
    for path in (SOURCE, SOURCE.parents[1] / 'receive_common_storage_routes.py'):
        hashes[str(path)] = digest(path)
    return hashes


if __name__ == '__main__':
    upper.checker['configure_cleanup']()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'exporter', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    upper.shared.update(create_receive_common_reference=create, source_dependencies=dependencies)
    upper.checker['run'](parser.parse_args())

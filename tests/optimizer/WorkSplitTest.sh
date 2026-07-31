#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

# Make one indivisible if-body larger than the optimizer's work chunk limit.
# The following else must remain attached when generated work is split across
# translation units, and both branches must retain their original behavior.
{
    printf '%s\n' \
        '#pragma once' \
        '#include "cpphdl.h"' \
        'class WorkSplitRoot : public cpphdl::Module {' \
        'public:' \
        '  _PORT(cpphdl::logic<1>) select;' \
        '  cpphdl::logic<32> value = 0;' \
        '  void _work(bool) {' \
        '    if (select()) {'
    for ((index = 0; index < 9000; ++index)); do
        printf '      value = cpphdl::logic<32>((uint64_t)(value) + 1);\n'
    done
    printf '%s\n' \
        '    }' \
        '    else {' \
        '      value = 17;' \
        '    }' \
        '  }' \
        '};'
} > "$build_dir/WorkSplitRoot.h"

printf '%s\n' \
    '#include "WorkSplitRoot.h"' \
    'WorkSplitRoot root;' \
    'int main() { return 0; }' \
    > "$build_dir/WorkSplitSeed.cc"

printf '%s\n' \
    '#include "WorkSplitRoot.h"' \
    '#include "WorkSplitRoot_optimized_combs.h"' \
    '#include <cstdint>' \
    'long _system_clock = 0;' \
    'int main() {' \
    '  WorkSplitRoot root;' \
    '  cpphdl::logic<1> select = 1;' \
    '  root.select = _ASSIGN(select);' \
    '  root._assign();' \
    '  calc_all(root, false);' \
    '  if ((uint64_t)root.value != 9000) return 1;' \
    '  select = 0;' \
    '  ++_system_clock;' \
    '  calc_all(root, false);' \
    '  return (uint64_t)root.value == 17 ? 0 : 2;' \
    '}' \
    > "$build_dir/WorkSplitRun.cc"

"$cpphdl" --optimize-combs WorkSplitRoot \
    --generated-dir="$build_dir" \
    "$build_dir/WorkSplitSeed.cc" -- \
    -w -I"$build_dir" -I"$include_dir"

objects=()
for source in "$build_dir"/WorkSplitRoot_optimized_combs*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$include_dir" \
        -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$include_dir" \
    "$build_dir/WorkSplitRun.cc" "${objects[@]}" -lstdc++exp \
    -o "$build_dir/run"
"$build_dir/run"

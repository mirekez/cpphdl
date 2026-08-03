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

# A concrete template root is wrapped in an immediately invoked generic lambda
# by the optimizer. Its safe top-level work statements must still be partitioned.
{
    printf '%s\n' \
        '#pragma once' \
        '#include "cpphdl.h"' \
        'template <uint64_t Enabled>' \
        'class TemplateWorkSplitRootT : public cpphdl::Module {' \
        'public:' \
        '  _PORT(cpphdl::logic<1>) input;' \
        '  cpphdl::logic<32> value = 0;' \
        '  void _work(bool) {' \
        '    if constexpr (Enabled) value = input();'
    for ((index = 0; index < 4000; ++index)); do
        printf '    value = cpphdl::logic<32>((uint64_t)(value) + 1);\n'
    done
    printf '%s\n' \
        '  }' \
        '};' \
        'using TemplateWorkSplitRoot = TemplateWorkSplitRootT<1>;'
} > "$build_dir/TemplateWorkSplitRoot.h"

printf '%s\n' \
    '#include "TemplateWorkSplitRoot.h"' \
    'TemplateWorkSplitRoot root;' \
    'int main() { return 0; }' \
    > "$build_dir/TemplateWorkSplitSeed.cc"

"$cpphdl" --optimize-combs TemplateWorkSplitRoot \
    --generated-dir="$build_dir" \
    "$build_dir/TemplateWorkSplitSeed.cc" -- \
    -w -I"$build_dir" -I"$include_dir"

# A generic-lambda wrapper keeps template-dependent constexpr branches valid,
# but ordinary port reads inside it must still use optimized graph state.
# Otherwise calc_all falls back to function_ref/std::function in hot work code.
if rg -q 'value = n[0-9]+\.input\(\);' \
    "$build_dir"/TemplateWorkSplitRoot_optimized_combs_work_*.cpp; then
    printf 'templated work retained a runtime port getter\n' >&2
    exit 1
fi

test "$(find "$build_dir" -maxdepth 1 \
    -name 'TemplateWorkSplitRoot_optimized_combs_work_*.cpp' | wc -l)" -gt 1

printf '%s\n' \
    '#include "TemplateWorkSplitRoot.h"' \
    '#include "TemplateWorkSplitRoot_optimized_combs.h"' \
    'long _system_clock = 0;' \
    'int main() {' \
    '  TemplateWorkSplitRoot root;' \
    '  root.input = _ASSIGN(cpphdl::logic<1>(1));' \
    '  root._assign();' \
    '  calc_all(root, false);' \
    '  return (uint64_t)root.value == 4001 ? 0 : 1;' \
    '}' \
    > "$build_dir/TemplateWorkSplitRun.cc"

template_objects=()
for source in "$build_dir"/TemplateWorkSplitRoot_optimized_combs*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$include_dir" \
        -c "$source" -o "$object"
    template_objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$include_dir" \
    "$build_dir/TemplateWorkSplitRun.cc" "${template_objects[@]}" \
    -lstdc++exp -o "$build_dir/template_run"
"$build_dir/template_run"

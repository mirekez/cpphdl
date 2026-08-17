#!/usr/bin/env bash
set -euo pipefail

cpphdl="$1"
include_dir="$2"
source_dir="$3"
stdcxxexp_library="${4:-}"
link_options=()
[[ -n "$stdcxxexp_library" ]] && link_options+=("$stdcxxexp_library")
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

"$cpphdl" --optimize-combs ConstexprBindingRoot \
    --generated-dir="$build_dir" \
    "$source_dir/ConstexprBindingSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Guarded module-valued ports must be graph calls in generated evaluators.
# A surviving source_comb_func() call would bypass calc_all's dependency graph.
# Inspect only emitted implementation files; the seed header remains source text.
if grep -R --include='*.cpp' -q '\.source_comb_func()' "$build_dir"; then
    echo "guarded binding retained a legacy comb call" >&2
    exit 1
fi

objects=()
for source in "$build_dir"/*.cpp; do
    object="${source%.cpp}.o"
    g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
        -I"$include_dir" -c "$source" -o "$object"
    objects+=("$object")
done

g++ -std=c++23 -O0 -g0 -w -I"$build_dir" -I"$source_dir" \
    -I"$include_dir" "$source_dir/ConstexprBindingRun.cc" \
    "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
"$build_dir/run"

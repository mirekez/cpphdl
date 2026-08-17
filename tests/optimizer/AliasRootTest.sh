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

"$cpphdl" --optimize-combs-l1 AliasRoot \
    --optimize-combs-collect="$build_dir/leaf.collection" \
    "$source_dir/AliasRootCollect.cc" -- \
    -w -I"$source_dir" -I"$include_dir"
"$cpphdl" --optimize-combs-l1 AliasRoot \
    --optimize-combs-collect="$build_dir/root.collection" \
    "$source_dir/AliasRootSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"
CPPHDL_OPTIMIZE_COMBS_DYNAMIC_BYTES_PER_CHUNK=1 \
"$cpphdl" --optimize-combs-l1 AliasRoot \
    --optimize-combs-load="$build_dir/leaf.collection" \
    --optimize-combs-load="$build_dir/root.collection" \
    --generated-dir="$build_dir" \
    "$source_dir/AliasRootSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"

# Repartitioning must delete chunks from the previous layout. Otherwise build
# wildcards compile stale evaluator definitions that no longer belong to the
# current calc_all graph.
test -f "$build_dir/AliasRoot_optimized_combs_dynamic_1.cpp"
"$cpphdl" --optimize-combs-l1 AliasRoot \
    --optimize-combs-load="$build_dir/leaf.collection" \
    --optimize-combs-load="$build_dir/root.collection" \
    --generated-dir="$build_dir" \
    "$source_dir/AliasRootSeed.cc" -- \
    -w -I"$source_dir" -I"$include_dir"
test ! -e "$build_dir/AliasRoot_optimized_combs_dynamic_1.cpp"

grep -q 'void calc_all(AliasRoot& obj' \
    "$build_dir/AliasRoot_optimized_combs.cpp"

# Specialized graph objects may be compiled by a different compiler than the
# recursively instantiated model. Keep structural NTTP types out of that ABI.
if ! rg -q 'extern "C" void cpphdl_optimized_bind_ports_abi\(void\* obj\)' \
    "$build_dir/AliasRoot_optimized_combs.h"; then
    printf 'optimized port binding has no compiler-neutral ABI entry point\n' >&2
    exit 1
fi

# Externally read root outputs must point at the value materialized by calc_all.
# Leaving their original initializer in place sends every host observation back
# through the legacy function_ref/std::function graph after optimized work.
if ! rg -q 'obj\.output = _ASSIGN_REG\(' \
    "$build_dir/AliasRoot_optimized_combs.cpp"; then
    printf 'optimized root output was not rebound to addressable storage\n' >&2
    exit 1
fi

# function_ref caches the address returned by its binding. Optimized ports must
# do the same when the binding is an exact lvalue; copying every aggregate port
# into optimizer state makes wide struct pipelines substantially more costly.
if ! rg -q 'p[0-9]+_pointer = std::addressof\(' \
    "$build_dir"/AliasRoot_optimized_combs*.cpp; then
    printf 'optimized port cache does not preserve addressable bindings\n' >&2
    exit 1
fi

# Address-returning bindings need only the cached pointer. Reserving a second
# aggregate value for each such port bloats the specialized state and evicts
# hot guard timestamps without providing any function_ref semantics.
if ! rg -q 'p[0-9]+_pointer\{\};' \
    "$build_dir/AliasRoot_optimized_combs_internal.h"; then
    printf 'addressable optimized port still requires owned value storage\n' >&2
    exit 1
fi

# A template child can be described in more than one bounded collection. Its
# initialized output port must resolve to the merged comb body, never survive as
# an unqualified source-method call in the generated global evaluator.
if rg -q '(^|[^.[:alnum:]_])output_comb_func\(\)' \
    "$build_dir"/AliasRoot_optimized_combs*.cpp; then
    printf 'template child comb call survived collection merge\n' >&2
    exit 1
fi

# Ordinary comb methods remain repeatable and valid recursion is cut by a port
# or _LAZY_COMB timestamp. Optimizer-owned transient guards add state and calls
# that do not exist in CppHDL's execution model.
if rg -q 'bool evaluating|s\.evaluating' \
    "$build_dir"/AliasRoot_optimized_combs*; then
    printf 'ordinary dynamic comb gained optimizer-owned recursion state\n' >&2
    exit 1
fi

# The common already-cached path must test state before making an out-of-line
# evaluator call. Otherwise every graph edge pays a cross-TU call merely to
# return, which made the flattened implementation slower than function_ref.
if ! rg -q '\? void\(\) : AliasRoot_optimized_comb_eval_' \
    "$build_dir"/AliasRoot_optimized_combs*; then
    printf 'dynamic evaluator use-site fast path was not generated\n' >&2
    exit 1
fi

# Every generated evaluator call is guarded at its use site. Repeating that
# test inside the private evaluator adds a branch to every real graph entry and
# cannot catch a caller that the optimizer itself does not emit.
if rg -q 'if \(s\.evaluat(ed|ing)[0-9]+.*return;' \
    "$build_dir"/AliasRoot_optimized_combs_dynamic_*.cpp; then
    printf 'dynamic evaluator repeated its use-site state guard\n' >&2
    exit 1
fi

# Evaluators cross generated source partitions but are not public API. Without
# hidden visibility GCC must preserve ELF interposition and cannot inline the
# high-affinity same-TU call graph assembled by the optimizer.
if ! rg -q '\[\[gnu::visibility\("hidden"\)\]\] void AliasRoot_optimized_comb_eval_' \
    "$build_dir/AliasRoot_optimized_combs_internal.h"; then
    printf 'optimized evaluator visibility permits interposition\n' >&2
    exit 1
fi

# A dynamic value used at exactly one evaluated site is cheaper and clearer as
# an inline guarded body. Its state remains in the global graph, but no dead
# out-of-line evaluator definition should survive after reachability pruning.
if ! rg -q '\? void\(\) : \(\[&\]\(\) \{' \
    "$build_dir"/AliasRoot_optimized_combs_*.cpp; then
    printf 'single-use dynamic evaluator was not fused at its demand site\n' >&2
    exit 1
fi
if rg -q '^.*void AliasRoot_optimized_comb_eval_2\(' \
    "$build_dir"/AliasRoot_optimized_combs_dynamic_*.cpp; then
    printf 'fused dynamic evaluator retained an unreachable definition\n' >&2
    exit 1
fi

# Exact forwarding ports inside a dynamic cone have identical clock-cache
# behavior and should collapse to one value. A surviving forwarding instance
# alias means the optimizer retained an unnecessary out-of-line evaluator.
if rg -q 'auto& n[0-9]+ = n0\.forwarding' \
    "$build_dir"/AliasRoot_optimized_combs_dynamic_*.cpp; then
    printf 'dynamic cached forwarding alias was not collapsed\n' >&2
    exit 1
fi

# A nested child field can share a name with parent storage. The semantic
# mutation collector must keep child.collision distinct from this->collision,
# leaving the parent collision comb in the ordinary eager schedule.
if rg -q 'collision_comb\s*=' \
    "$build_dir"/AliasRoot_optimized_combs_dynamic_*.cpp; then
    printf 'nested child mutation poisoned parent comb scheduling\n' >&2
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
    -I"$include_dir" "$source_dir/AliasRootRun.cc" \
    "${objects[@]}" "${link_options[@]}" -o "$build_dir/run"
"$build_dir/run"

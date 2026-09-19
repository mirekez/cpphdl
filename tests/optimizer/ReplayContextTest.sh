#!/usr/bin/env bash
set -euo pipefail
cpphdl="$1"
include_dir="$2"
source_dir="$3"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
link_options=()
[[ -n "${4:-}" ]] && link_options+=("$4")
cat > "$build_dir/ReplayEnvelope.h" <<'EOF'
#include "DynamicCombRepeatRoot.h"
class ReplayEnvelope : public cpphdl::Module {
public:
    DynamicCombRepeatRoot subject;
    void _assign() { subject._assign(); }
    void _work(bool reset) { subject._work(reset); }
};
EOF
printf '#include "ReplayEnvelope.h"\n' > "$build_dir/ReplayEnvelope.cc"
for mode in --optimize-combs --optimize-combs-l1; do
    mkdir -p "$build_dir/original" "$build_dir/replay"
    "$cpphdl" "$mode" DynamicCombRepeatRoot --replay-export="$build_dir/context" \
        --generated-dir="$build_dir/original" "$source_dir/DynamicCombRepeatSeed.cc" \
        -- -std=c++23 -w -I"$source_dir" -I"$include_dir"
    "$cpphdl" "$mode" ReplayEnvelope --replay-export="$build_dir/nested-context" \
        --generated-dir="$build_dir/envelope" "$build_dir/ReplayEnvelope.cc" \
        -- -std=c++23 -w -I"$source_dir" -I"$include_dir"
    "$cpphdl" "$mode" DynamicCombRepeatRoot --replay-context="$build_dir/nested-context" \
        --replay-source=subject \
        --generated-dir="$build_dir/replay" "$source_dir/DynamicCombRepeatSeed.cc" \
        -- -std=c++23 -w -I"$source_dir" -I"$include_dir"
    g++ -std=c++23 -O2 -w -I"$source_dir" -I"$include_dir" -I"$build_dir/replay" \
        "$source_dir/DynamicCombRepeatRun.cc" "$build_dir/replay"/*.cpp \
        "${link_options[@]}" -o "$build_dir/run"
    "$build_dir/run"
    if "$cpphdl" "$mode" DynamicCombRepeatRoot --replay-context="$build_dir/context" \
        --replay-source=missing --generated-dir="$build_dir/bad" \
        "$source_dir/DynamicCombRepeatSeed.cc" -- -std=c++23 -w -I"$source_dir" -I"$include_dir"; then
        echo 'unmatched context accepted' >&2
        exit 1
    fi
done
sed 's/"DynamicCombRepeatRoot"/"wrong"/g' "$build_dir/context" > "$build_dir/bad-context"
if "$cpphdl" --optimize-combs-l1 DynamicCombRepeatRoot --replay-context="$build_dir/bad-context" \
    --generated-dir="$build_dir/bad" "$source_dir/DynamicCombRepeatSeed.cc" \
    -- -std=c++23 -w -I"$source_dir" -I"$include_dir" > "$build_dir/bad.log" 2>&1; then
    echo 'wrong-type context accepted' >&2
    exit 1
fi
grep -q 'module type mismatch' "$build_dir/bad.log"
for corruption in duplicate mode truncated; do
    cp "$build_dir/context" "$build_dir/bad-context"
    case "$corruption" in
        duplicate) tail -n 1 "$build_dir/context" >> "$build_dir/bad-context" ;;
        mode) sed -i '1s/1 1/1 0/' "$build_dir/bad-context" ;;
        truncated) printf 'C 1' >> "$build_dir/bad-context" ;;
    esac
    if "$cpphdl" --optimize-combs-l1 DynamicCombRepeatRoot --replay-context="$build_dir/bad-context" \
        --generated-dir="$build_dir/bad" "$source_dir/DynamicCombRepeatSeed.cc" \
        -- -std=c++23 -w -I"$source_dir" -I"$include_dir"; then
        echo "invalid $corruption context accepted" >&2
        exit 1
    fi
done

#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/build.sh"
initialize
[[ -f "$ROOT/prepared" ]] || die 'prepare OpenC910 first'
OUT="$ROOT/ordinary"
mkdir -p "$OUT/input" "$OUT/generated" "$OUT/logs" "$OUT/meta"
export HDLCPP_DEFINES=VERILATOR,NO_DUMP
export HDLCPP_INCLUDE_DIRS="$SOURCE/C910_RTL_FACTORY/gen_rtl/cpu/rtl:$SOURCE/C910_RTL_FACTORY/gen_rtl/mmu/rtl:$SOURCE/smart_run/logical/tb"
# The original testbench has no external GPIO driver. Preserve the internally
# driven pad value and readback, with undriven bits zero in the two-state model.
export HDLCPP_OUTPUT_ONLY_INOUTS=soc.b_pad_gpio_porta,apb.b_pad_gpio_porta,gpio.b_pad_gpio_porta
context=$({
    printf '%s\n' "$HDLCPP_DEFINES" "$HDLCPP_INCLUDE_DIRS" "$HDLCPP_OUTPUT_ONLY_INOUTS"
    sha256sum "$HDLCPP" "$SOURCE/C910_RTL_FACTORY/gen_rtl/cpu/rtl/cpu_cfig.h" "$SOURCE/C910_RTL_FACTORY/gen_rtl/mmu/rtl/sysmap.h"
} | sha256sum)

# Preserve the original library precedence: explicit RTL files win over -y libraries.
{
    sed "s|\${CODE_BASE_PATH}|$CODE_BASE_PATH|g" "$CODE_BASE_PATH/gen_rtl/filelists/C910_asic_rtl.fl" | awk '/\.v$/ {print}'
    for directory in ahb apb axi common gpio mem uart pmu; do
        find "$SOURCE/smart_run/logical/$directory" -maxdepth 1 -name '*.v' -type f | sort
    done
} | gawk '{name=$0; sub(/^.*\//,"",name); if(!seen[name]++) print}' > "$OUT/sources.list"

: > "$OUT/modules.tsv"
index=0
while IFS= read -r source; do
    name=$(basename "${source%.v}")
    input="$OUT/input/$name.v"
    printf '`include "cpu_cfig.h"\n`include "sysmap.h"\n`include "%s"\n' "$source" > "$input"
    mkdir -p "$OUT/meta/$name"
    signature=$({ printf '%s\n' "$context"; sha256sum "$source" "$input"; } | sha256sum)
    if [[ ! -f "$OUT/meta/$name/signature" || $(cat "$OUT/meta/$name/signature") != "$signature" ]]; then
        for category in ports params traits dependencies; do : > "$OUT/meta/$name/$category"; done
        env HDLCPP_METADATA_ONLY=1 \
        HDLCPP_WRITE_PORT_TYPES="$OUT/meta/$name/ports" \
        HDLCPP_WRITE_MODULE_PARAMS="$OUT/meta/$name/params" \
        HDLCPP_WRITE_MODULE_TRAITS="$OUT/meta/$name/traits" \
        HDLCPP_WRITE_MODULE_DEPENDENCIES="$OUT/meta/$name/dependencies" \
            "$HDLCPP" "$input" > "$OUT/logs/$name.metadata.log" 2>&1
        printf '%s\n' "$signature" > "$OUT/meta/$name/signature"
    fi
    gawk -F '\t' -v source="$name" '{module=$1; sub(/\..*/,"",module); if(!seen[module]++) print module "\t" source}' "$OUT/meta/$name/ports" >> "$OUT/modules.tsv"
    ((index+=1))
    if ((index%50==0)); then echo "metadata: $index files"; fi
done < "$OUT/sources.list"

for category in ports params traits dependencies; do cat "$OUT/meta/"*/"$category" > "$OUT/$category.tsv"; done
# Dependency order comes from parsed module instances, including multi-module files.
gawk -F '\t' -v level_file="$OUT/levels.tsv" '
    function visit(file, child) {
        if(visited[file]++) return;
        print file " " file;
        level[file]=0;
        for(child in children[file]) {
            if(child ~ /^missing:/) {print child > "/dev/stderr"; bad=1; continue}
            if(child!=file) {
                print child " " file; visit(child);
                if(level[file]<=level[child]) level[file]=level[child]+1;
            }
        }
        print level[file] "\t" file > level_file;
    }
    ARGIND==1 {source[$1]=$2; next}
    {parent=source[$1]; child=($2 in source) ? source[$2] : "missing:" $1 "->" $2;
     children[parent][child]=1}
    END {if(!("soc" in source)) exit 1; visit(source["soc"]); exit bad}
    ' "$OUT/modules.tsv" "$OUT/dependencies.tsv" | tsort > "$OUT/order.list"
export HDLCPP_PORT_TYPES="$OUT/ports.tsv" HDLCPP_MODULE_PARAMS="$OUT/params.tsv" HDLCPP_MODULE_TRAITS="$OUT/traits.tsv"
cd "$OUT"
emit_one() {
    local name=$1 signature
    signature=$({ printf '%s\n' "$context"; cat "$OUT/meta/$name/signature"; sha256sum "$OUT/ports.tsv" "$OUT/params.tsv" "$OUT/traits.tsv"; } | sha256sum)
    if [[ ! -f "$OUT/meta/$name/emitted" || $(head -1 "$OUT/meta/$name/emitted") != "$signature" ]] ||
       ! tail -n +2 "$OUT/meta/$name/emitted" | sha256sum --check --status; then
        : > "$OUT/meta/$name/finaltraits"
        HDLCPP_APPEND_FINAL_MODULE_TRAITS="$OUT/meta/$name/finaltraits" \
            "$HDLCPP" "$OUT/input/$name.v" > "$OUT/logs/$name.emit.log" 2>&1
        { printf '%s\n' "$signature"; sha256sum "$OUT/generated/$name.h" "$OUT/meta/$name/finaltraits"; } > "$OUT/meta/$name/emitted"
    fi
    echo "converted $name"
}
export -f emit_one
export OUT context
printf '#pragma once\n' > all_generated.h
sort -k1,1n -k2,2 "$OUT/levels.tsv" -o "$OUT/levels.tsv"
last_level=$(tail -1 "$OUT/levels.tsv" | cut -f1)
for ((level=0;level<=last_level;level++)); do
    mapfile -t batch < <(gawk -F '\t' -v level="$level" '$1==level {print $2}' "$OUT/levels.tsv")
    # Independent modules read the same immutable metadata. Publish all final
    # traits only after the level finishes; every parent sees completed children.
    # Refill a worker as soon as it finishes instead of idling behind the slower
    # module in each pair. xargs propagates failures before traits are published.
    if ((${#batch[@]})); then
        printf '%s\n' "${batch[@]}" | xargs -r -P2 -n1 bash -euc 'emit_one "$1"' bash
    fi
    for name in "${batch[@]}"; do
        cat "$OUT/meta/$name/finaltraits" >> "$OUT/traits.tsv"
        printf '#include "generated/%s.h"\n' "$name" >> all_generated.h
    done
done
echo "OpenC910 ordinary conversion complete: $(wc -l < "$OUT/order.list") reachable source files"

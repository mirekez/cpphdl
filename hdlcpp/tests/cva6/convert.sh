#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$SCRIPT_DIR/build.sh"
initialize
require_source
OUT="$ROOT/ordinary"
mkdir -p "$OUT/logs" "$OUT/work" "$OUT/generated"
gawk -v root="$OUT" '
    /^@@ / {
        if (target != "") close(target)
        target = root "/" substr($0, 4)
        parent = target; sub(/\/[^/]+$/, "", parent)
        if (system("mkdir -p -- \"" parent "\"") != 0) exit 1
        printf "" > target
        next
    }
    { gsub(/@OUT@/, root); print > target }
' "$SCRIPT_DIR/conversion.in"
cp "$SCRIPT_DIR"/run_cpphdl_testharness* "$OUT/"
source "$OUT/environment.sh"
export HDLCPP_DEFINES=VERILATOR
HDLCPP_INCLUDE_DIRS=
while IFS= read -r directory; do
    HDLCPP_INCLUDE_DIRS+="${HDLCPP_INCLUDE_DIRS:+:}$SOURCE/$directory"
done < "$OUT/includes.list"
export HDLCPP_INCLUDE_DIRS
export HDLCPP_MODULE_TRAITS="$OUT/cva6_merged_module_traits.tsv"
export HDLCPP_PACKAGE_CALLABLES="$OUT/cva6_auto_package_callables.tsv"
export HDLCPP_TYPE_WIDTHS="$OUT/cva6_merged_type_widths.tsv"
categories=(ports params widths traits callables dependencies)
variables=(PORT_TYPES MODULE_PARAMS TYPE_WIDTHS MODULE_TRAITS PACKAGE_CALLABLES MODULE_DEPENDENCIES)
names=(port_types module_params type_widths module_traits package_callables module_dependencies)
for name in "${names[@]}"; do : > "$OUT/cva6_auto_$name.tsv"; done
: > "$HDLCPP_MODULE_TRAITS"
: > "$HDLCPP_TYPE_WIDTHS"
: > "$OUT/work/metadata.map"
index=0
while IFS= read -r source; do
    directory="$OUT/work/metadata/$index"
    mkdir -p "$directory"
    arguments=()
    for category in "${!categories[@]}"; do
        : > "$directory/${categories[category]}.tsv"
        arguments+=("HDLCPP_WRITE_${variables[category]}=$directory/${categories[category]}.tsv")
    done
    (cd "$directory"; env HDLCPP_METADATA_ONLY=1 "${arguments[@]}" "$HDLCPP" "$SOURCE/$source") > "$OUT/logs/metadata-$index.log" 2>&1
    gawk -F '\t' 'NF { key=$1; if (FILENAME ~ /\/(ports|widths|callables)\.tsv$/) sub(/\..*/, "", key); print key }' "$directory"/*.tsv | sort -u > "$directory/modules"
    printf '%s\t%s\n' "$source" "$index" >> "$OUT/work/metadata.map"
    ((index+=1))
done < "$OUT/metadata.list"
while IFS=$'\t' read -r source index; do
    for category in "${!categories[@]}"; do
        cat "$OUT/work/metadata/$index/${categories[category]}.tsv" >> "$OUT/cva6_auto_${names[category]}.tsv"
    done
done < "$OUT/work/metadata.map"
cat "$OUT/cva6_auto_type_widths.tsv" "$OUT/cva6_type_widths.tsv" > "$HDLCPP_TYPE_WIDTHS"
cp "$OUT/cva6_auto_module_traits.tsv" "$HDLCPP_MODULE_TRAITS"
declare -A metadata
while IFS=$'\t' read -r source index; do metadata["$source"]=$index; done < "$OUT/work/metadata.map"
for pass in {1..12}; do
    LC_ALL=C sort -u "$HDLCPP_MODULE_TRAITS" > "$OUT/work/traits.before"
    while IFS= read -r source; do
        index=${metadata[$source]}
        directory="$OUT/work/emit/$index"
        mkdir -p "$directory"
        gawk -F '\t' 'ARGIND==1 { own[$0]=1; next } { key=$1; sub(/\..*/, "", key); if (!(key in own)) print }' "$OUT/work/metadata/$index/modules" "$OUT/cva6_auto_port_types.tsv" > "$directory/ports.tsv"
        cat "$OUT/cva6_port_types.tsv" >> "$directory/ports.tsv"
        cat "$OUT/cva6_module_params.tsv" > "$directory/params.tsv"
        gawk -F '\t' 'ARGIND==1 { own[$0]=1; next } !($1 in own)' "$OUT/work/metadata/$index/modules" "$OUT/cva6_auto_module_params.tsv" >> "$directory/params.tsv"
        : > "$directory/traits.tsv"
        (cd "$directory"; env HDLCPP_PORT_TYPES="$directory/ports.tsv" HDLCPP_MODULE_PARAMS="$directory/params.tsv" HDLCPP_APPEND_FINAL_MODULE_TRAITS="$directory/traits.tsv" "$HDLCPP" "$SOURCE/$source") > "$OUT/logs/${source//\//__}.log" 2>&1
        cat "$HDLCPP_MODULE_TRAITS" "$directory/traits.tsv" | gawk 'NF && !seen[$0]++' > "$OUT/work/traits.next"
        mv "$OUT/work/traits.next" "$HDLCPP_MODULE_TRAITS"
        destination="$OUT/generated/${source%.*}.h"
        mkdir -p "${destination%/*}"
        cp "$directory/generated/$(basename "${source%.*}").h" "$destination"
    done < "$OUT/sources.list"
    LC_ALL=C sort -u "$HDLCPP_MODULE_TRAITS" > "$OUT/work/traits.after"
    echo "CVA6 emission pass $pass complete"
    if cmp -s "$OUT/work/traits.before" "$OUT/work/traits.after"; then break; fi
    [[ "$pass" != 12 ]] || die 'CVA6 module traits did not converge'
done
cp -a "$OUT/adapters/." "$OUT/generated/"
find "$OUT/generated" -name '*.h' -exec sed -i -e 's/\<sync\>/cpphdl_sv_sync/g' -e 's/cpphdl_sv_sync\.h"/sync.h"/g' {} +
gawk '
    BEGIN { RS="\0"; ORS="" }
    function repair(text, method, leaf, parent, start, finish, body, token, position, bracket, depth, cursor, replacement) {
        start=index(text, "& " method "_func()")
        if (!start) { print "missing TLB method " method > "/dev/stderr"; exit 1 }
        body=substr(text,start)
        match(substr(body,2), /\n    (std::remove_cvref_t<decltype\(|_LAZY_COMB\()/)
        finish=RSTART ? RSTART : length(body)
        body=substr(body,1,finish)
        token="content_q_" leaf "_comb_func()"
        while ((position=index(body,token))) {
            bracket=position+length(token)
            while (substr(body,bracket,1)!="[" && bracket<=length(body)) bracket++
            depth=0
            for (cursor=bracket; cursor<=length(body); cursor++) {
                if (substr(body,cursor,1)=="[") depth++
                if (substr(body,cursor,1)=="]" && --depth==0) break
            }
            if (cursor>length(body)) exit 1
            replacement="(content_q_" parent "_comb_func()" substr(body,bracket,cursor-bracket+1) ")." leaf
            body=substr(body,1,position-1) replacement substr(body,cursor+1)
        }
        return substr(text,1,start-1) body substr(text,start+finish)
    }
    {
        text=$0
        text=repair(text,"lu_content_o_ppn_comb","ppn","pte")
        text=repair(text,"lu_content_o_u_comb","u","pte")
        text=repair(text,"lu_g_content_o_ppn_comb","ppn","gpte")
        text=repair(text,"lu_g_content_o_u_comb","u","gpte")
        while (match(text,/    _LAZY_COMB\(content_q_(u|ppn)_comb,/)) {
            start=RSTART; tail=substr(text,start+1)
            finish=index(tail,"\n    _LAZY_COMB(")
            if (!finish) exit 1
            text=substr(text,1,start-1) substr(tail,finish)
        }
        if (text ~ /content_q_(u|ppn)_comb_func\(\)/) exit 1
        print text
    }
' "$OUT/generated/core/cva6_mmu/cva6_tlb.h" > "$OUT/work/tlb.h"
mv "$OUT/work/tlb.h" "$OUT/generated/core/cva6_mmu/cva6_tlb.h"
rm -r -- "$OUT/work" "$OUT/adapters"
echo 'CVA6 conversion complete: cv32a6_imac_sv32, 284 sources'

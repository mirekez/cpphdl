#!/usr/bin/env bash
set -euo pipefail
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
CXX=$(command -v "$1")
FIXTURE="$REPO/hdlcpp/tests/cva6"
TEST_ROOT=$(mktemp -d /tmp/cva6-shell-test.XXXXXX)
trap 'rm -r -- "$TEST_ROOT"' EXIT
export CVA6_WORK_ROOT="$TEST_ROOT/unused"
for script in "$FIXTURE"/.*.sh "$FIXTURE"/build.sh "$FIXTURE"/convert.sh; do bash -n "$script"; done
for script in "$FIXTURE"/.build*.sh "$FIXTURE"/.run*.sh; do bash "$script" --help >/dev/null; done
bash "$FIXTURE/.compare.sh" --help >/dev/null
[[ ! -e "$CVA6_WORK_ROOT" ]]
source "$FIXTURE/build.sh"
(
    before=$(compilation_inputs | sha256sum)
    run_model() { echo 'changed validation'; }
    [[ $(compilation_inputs | sha256sum) == "$before" ]]
    cpp_model() { echo 'changed compilation'; }
    [[ $(compilation_inputs | sha256sum) != "$before" ]]
)
if buffer_output bash -c 'echo stdout; echo stderr >&2; exit 7' > "$TEST_ROOT/buffer.log"; then
    exit 1
else
    [[ $? == 7 ]]
fi
[[ $(cat "$TEST_ROOT/buffer.log") == $'stdout\nstderr' ]]
if buffer_output printf test > /dev/full 2>/dev/null; then exit 1; fi
cat > "$TEST_ROOT/run.log" <<'EOF'
CVA6_MMUL size=32 repeats=1 macs=32768 checksum=0xd687fdc5
PASSED
CVA6_BENCH reset_cycles=10 work_cycles=396768 total_cycles=396778 work_seconds=1.25
matrix.riscv *** SUCCESS *** (tohost = 0) after 396778 cycles
EOF
[[ $(check_run verilator "$TEST_ROOT/run.log") == 1.25 ]]
if check_run cpphdl-native-graph "$TEST_ROOT/run.log" >/dev/null; then exit 1; fi
echo 'CVA6_GRAPH_EDGES total_cycles=396778' >> "$TEST_ROOT/run.log"
check_run cpphdl-native-graph "$TEST_ROOT/run.log" >/dev/null
sed 's/396778/396779/g' "$TEST_ROOT/run.log" > "$TEST_ROOT/bad.log"
if check_run cpphdl-native-graph "$TEST_ROOT/bad.log" >/dev/null; then exit 1; fi
gawk 'BEGIN { for (record=1;record<=297074;record++) print "3 0x80000000 (0x00000013) x 0 0x00000000" }' > "$TEST_ROOT/reference.log"
gawk 'BEGIN { for (record=1;record<=297074;record++) printf "CPPCOMMIT cycle=%d ordinal=%d lane=0 order=%d pc=0x80000000 next=0x80000004 insn=0x00000013 trap=0 cause=0x0 rd=x0 data=0x1234\n",record,record,record }' > "$TEST_ROOT/candidate.log"
normalize_commits verilator "$TEST_ROOT/reference.log" > "$TEST_ROOT/reference"
normalize_commits verilator-4threads "$TEST_ROOT/reference.log" > "$TEST_ROOT/reference4"
cmp "$TEST_ROOT/reference" "$TEST_ROOT/reference4"
normalize_commits cpphdl "$TEST_ROOT/candidate.log" > "$TEST_ROOT/candidate"
cmp "$TEST_ROOT/reference" "$TEST_ROOT/candidate"
sed '1s/trap=0/trap=1/' "$TEST_ROOT/candidate.log" > "$TEST_ROOT/bad.log"
if normalize_commits cpphdl "$TEST_ROOT/bad.log" >/dev/null; then exit 1; fi
sed '2d' "$TEST_ROOT/candidate.log" > "$TEST_ROOT/bad.log"
if normalize_commits cpphdl "$TEST_ROOT/bad.log" >/dev/null; then exit 1; fi
sed '1s/ rd=x0 data=0x1234$/ CPPCOMMIT truncated/' "$TEST_ROOT/candidate.log" > "$TEST_ROOT/bad.log"
if normalize_commits cpphdl "$TEST_ROOT/bad.log" >/dev/null; then exit 1; fi
WORK="$TEST_ROOT/compile"; CACHE="$TEST_ROOT/cache"
mkdir -p "$WORK" "$CACHE"
declare -A opaque=()
collections=()
echo 'cached collection' > "$CACHE/root.bin"
sha256sum "$CACHE/root.bin" > "$CACHE/root.bin.sha256"
collect_group root cpphdl_opt_t0
grep -q 'static_assert(sizeof(cpphdl_opt_t0)' "$WORK/root.cpp"
[[ ${collections[0]} == "--optimize-combs-load=$CACHE/root.bin" ]]
rm -- "$WORK/root.cpp"
collect_group root cpphdl_opt_t0
[[ -s "$WORK/root.cpp" ]]
flags=(-std=c++23 -g0)
objects=()
echo 'int answer() { return 42; }' > "$WORK/answer.inc"
echo 'int answer(); int main() { return answer() != 42; }' > "$WORK/main.inc"
compile_group -O2 unit-0 "$WORK/answer.inc" "$WORK/main.inc"
first=${objects[0]}
compile_group -O2 unit-0 "$WORK/answer.inc" "$WORK/main.inc"
[[ ${objects[1]} == "$first" ]]
printf '%s\n' "$first" > "$WORK/objects.list"
"$CXX" -std=c++17 -O2 "$FIXTURE/link_objects.cpp" -o "$WORK/link-objects"
"$WORK/link-objects" "$WORK/objects.list" "$CXX" -o "$WORK/run"
"$WORK/run"
echo 'not an object' > "$first"
compile_group -O2 unit-0 "$WORK/answer.inc" "$WORK/main.inc"
gzip -t "$first"
REAL_CXX=$CXX
export REAL_CXX
cat > "$WORK/bounded-cxx" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    if [[ "$argument" == *.cpp ]] && [[ $(grep -c '^#line ' "$argument") -gt 1 ]]; then
        echo 'LLVM ERROR: out of memory' >&2
        exit 1
    fi
done
exec "$REAL_CXX" "$@"
EOF
chmod +x "$WORK/bounded-cxx"
CXX="$WORK/bounded-cxx"
objects=()
compile_group -O2 bounded "$WORK/answer.inc" "$WORK/main.inc"
((${#objects[@]}==2))
printf '%s\n' "${objects[@]}" > "$WORK/split.list"
"$WORK/link-objects" "$WORK/split.list" "$REAL_CXX" -o "$WORK/split-run"
"$WORK/split-run"
source "$FIXTURE/.compare.sh"
ROOT="$TEST_ROOT/report"
mkdir -p "$ROOT/stamps"
echo signature > "$ROOT/stamps/verilator"
build_measurement verilator
[[ "$BUILD_SECONDS" == unmeasured && "$BUILD_STATE" == cached ]]
printf 'signature\t123.456\tfresh\n' > "$ROOT/stamps/verilator.time"
build_measurement verilator
[[ "$BUILD_SECONDS" == 123.456 && "$BUILD_STATE" == fresh ]]
echo changed > "$ROOT/stamps/verilator"
build_measurement verilator
[[ "$BUILD_SECONDS" == unmeasured ]]
echo 'CVA6 Bash helpers PASS'

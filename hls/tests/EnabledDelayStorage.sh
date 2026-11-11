#!/usr/bin/env bash
# Prove payload storage survives generic synthesis as RAM, without mapping it
# to flops. This is an inference regression, not a Quartus fit/timing claim.
set -euo pipefail
yosys=$1
root=$(realpath "$2")
mkdir -p "$3"
work=$(realpath "$3")
for depth in 8 46 84; do
    cat > "$work/delay-$depth.ys" <<YOSYS
read_verilog -sv "$root/hls/math/rtl/EnabledDelay.sv"
chparam -set WIDTH 544 -set DEPTH $depth CpphdlEnabledDelay
hierarchy -top CpphdlEnabledDelay
proc
flatten
opt
memory_dff
memory_collect
opt_clean
select -assert-count 1 t:\$mem_v2
write_json "$work/delay-$depth.json"
stat
YOSYS
    "$yosys" -Q -s "$work/delay-$depth.ys" > "$work/delay-$depth.log" 2>&1
    jq -e --argjson depth "$depth" '
      def uint: explode | reduce .[] as $c (0; . * 2 + $c - 48);
      .modules.CpphdlEnabledDelay.cells | [.[] | select(.type=="$mem_v2")] |
      length==1 and (.[0].parameters |
        (.WIDTH|uint)==544 and (.SIZE|uint)==$depth and
        (.RD_PORTS|uint)==1 and (.WR_PORTS|uint)==1 and
        (.RD_CLK_ENABLE|uint)==1)
    ' "$work/delay-$depth.json" > /dev/null
    echo "PASS enabled delay: depth=$depth width=544, one synchronous 1R/1W RAM"
done

# Keep the real arithmetic opaque: count only storage/control in the exact
# generated 4x4 wrapper used by the 1024-lane plane.
"${4:-python3}" "$root/hls/math/generate_mac_replacement.py" 4 4 |
    awk '/^module (CpphdlMacEnabledDelay|Fp32MacArray_4_4)/ {emit=1} emit {print} /^endmodule/ {emit=0}' \
    > "$work/mac-storage.sv"
cat > "$work/mac-blackbox.sv" <<'RTL'
(* blackbox *) module Fp32Mac(input clk, reset, en_in,
    input [31:0] a_in,b_in,c_in, output [31:0] result_out); endmodule
RTL
cat > "$work/mac.ys" <<YOSYS
read_verilog -sv "$work/mac-blackbox.sv" "$work/mac-storage.sv"
chparam -set LATENCY 48 Fp32MacArray_4_4
hierarchy -top Fp32MacArray_4_4
proc
flatten
opt
memory_dff
memory_collect
opt_clean
select -assert-count 1 t:\$mem_v2
select -assert-count 16 t:Fp32Mac
write_json "$work/mac.json"
stat
YOSYS
"$yosys" -Q -s "$work/mac.ys" > "$work/mac.log" 2>&1
jq -e '
    .modules.Fp32MacArray_4_4.cells |
    [.[] | select(.type|test("^\\$(s?dff|adff)")) | .connections.Q | length] |
    add < 2048
' "$work/mac.json" > /dev/null
echo 'PASS MAC storage: 16 arithmetic lanes, one payload RAM, fewer than 2048 external register bits'

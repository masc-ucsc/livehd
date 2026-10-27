#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Vendor RTL bundles (NVDLA's generated vmod tree) are written for DC / Yosys /
# VCS and lean on two conventions slang does not apply by itself. Both are
# configurable; translate_off is enabled by default:
#   compile.slang.translate_off=true
#       drops the text between `// synopsys translate_off` and `translate_on`
#       (and the `synthesis` spelling, with or without a space after `//`);
#       simulation-only code there (plusargs, $timeformat, assertion monitors
#       instantiating modules that only exist for simulation) never reaches the
#       design. An unclosed region is still an error.
#   compile.slang.ignore_uninstantiated_modules=true
#       skips semantic elaboration of modules outside the --top hierarchy (a
#       module whose generated body is empty and references undeclared signals
#       is dead in this configuration). Errors inside the hierarchy remain
#       errors, and syntax errors are reported everywhere.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

cat > "$W/pragma.v" <<'V'
module pragma_top(input clk, input a, output reg y);
always @(posedge clk) y <= a;
// synopsys translate_off
reg dbg; initial dbg = $test$plusargs("dbg_on") != 0;
sim_only_monitor mon (.clk(clk), .x(y));
// synopsys translate_on
//synthesis translate_off
reg dbg2; initial begin dbg2 = 1'b0; $timeformat(-9, 2, "ns", 15); end
//synthesis translate_on
// pragma translate_off
string trace_entry; always @(posedge clk) $sformat(trace_entry, "%d", y);
// pragma translate_on
endmodule
V

# Explicit opt-out: the simulation-only region is real code and fails.
if "$LHD" compile "$W/pragma.v" --top pragma_top --workdir "$W/strict" --emit-dir verilog:"$W/strict_net" -q \
    --set compile.slang.translate_off=false --emit diagnostics:"$W/strict.jsonl" > /dev/null 2>&1; then
  fail "translate_off region was dropped despite the explicit opt-out"
fi
"$LHD" compile "$W/pragma.v" --top pragma_top --workdir "$W/default" --emit-dir verilog:"$W/default_net" -q \
  || fail "default translate_off did not drop the regions"
[ -f "$W/default_net/pragma_top.v" ] || fail "no netlist with default translate_off"
! grep -q 'sim_only_monitor\|dbg\|trace_entry' "$W/default_net/pragma_top.v" || fail "dropped region leaked into default netlist"
"$LHD" compile "$W/pragma.v" --top pragma_top --workdir "$W/off" --emit-dir verilog:"$W/off_net" -q \
  --set compile.slang.translate_off=true || fail "translate_off=true did not drop the regions"
[ -f "$W/off_net/pragma_top.v" ] || fail "no netlist with translate_off=true"
! grep -q 'sim_only_monitor\|dbg' "$W/off_net/pragma_top.v" || fail "dropped region leaked into the netlist"
# A raw slang format wins over the label (slang rejects a repeated optional flag, a repeated list is fine).
"$LHD" compile "$W/pragma.v" --top pragma_top --workdir "$W/raw" --emit-dir verilog:"$W/raw_net" -q \
  --set compile.slang.translate_off=true -- --translate-off-format synopsys,translate_off,translate_on \
  --translate-off-format synthesis,translate_off,translate_on \
  --translate-off-format pragma,translate_off,translate_on || fail "raw --translate-off-format with the label"
# An unclosed region is an error, never a silently truncated design.
cat > "$W/unclosed.v" <<'V'
module unclosed_top(input a, output y);
assign y = a;
// synopsys translate_off
assign y = ~a;
endmodule
V
if "$LHD" compile "$W/unclosed.v" --top unclosed_top --workdir "$W/unclosed" -q \
    --set compile.slang.translate_off=true > /dev/null 2>&1; then
  fail "an unclosed translate_off region was accepted"
fi

cat > "$W/bundle.v" <<'V'
module live_top(input a, output y);
assign y = a;
endmodule
module dead_unfinished(output y);
assign y = missing_wire0;
endmodule
V
if "$LHD" compile "$W/bundle.v" --top live_top --workdir "$W/dead_strict" -q > /dev/null 2>&1; then
  fail "an undeclared identifier in a dead module was accepted without the opt-in"
fi
"$LHD" compile "$W/bundle.v" --top live_top --workdir "$W/dead_ok" --emit-dir verilog:"$W/dead_net" -q \
  --set compile.slang.ignore_uninstantiated_modules=true || fail "ignore_uninstantiated_modules=true did not skip the dead module"
[ -f "$W/dead_net/live_top.v" ] && [ ! -f "$W/dead_net/dead_unfinished.v" ] || fail "dead module was emitted"
# The raw flag and the label together must not trip slang's repeated-flag error.
"$LHD" compile "$W/bundle.v" --top live_top --workdir "$W/dead_both" -q \
  --set compile.slang.ignore_uninstantiated_modules=true -- --ignore-uninstantiated-modules || fail "label plus raw flag"
# The module is only skipped while it is outside the hierarchy.
if "$LHD" compile "$W/bundle.v" --top dead_unfinished --workdir "$W/dead_top" -q \
    --set compile.slang.ignore_uninstantiated_modules=true > /dev/null 2>&1; then
  fail "an error inside the selected hierarchy was accepted"
fi
echo 'PASS: translate_off defaults on, supports opt-out, and vendor policies stay scoped'

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
write_source() {
  cat > "$W/source.v" <<SRC
(* $1 *) module branch(input a, b, output y);
  (* $1 *) wire v = a & b;
  assign y = v;
endmodule
module top(input a, b, output y);
  (* $1 *) branch u(.a(a), .b(b), .y(y));
endmodule
SRC
}
# Compare source-policy output, not merely successful compilation: all three
# attachment sites must import the same policy as the explicit route property.
for spec in 'dont_touch=1:true' 'DONT_TOUCH="TRUE":true' 'dont_touch="yes":true' 'dont_touch:true' 'dont_touch="FALSE":false' 'DONT_TOUCH=0:false'; do
  attr="${spec%:*}"
  expected="${spec##*:}"
  write_source "$attr"
  "$LHD" compile "$W/source.v" --top top --workdir "$W/actual-work" --set lhd.incremental=false --emit-dir pyrope:"$W/actual" -q
  write_source "\\synth.preroute_exclude = $([ "$expected" = true ] && echo 1 || echo 0)"
  "$LHD" compile "$W/source.v" --top top --workdir "$W/expected-work" --set lhd.incremental=false --emit-dir pyrope:"$W/expected" -q
  diff -r "$W/actual" "$W/expected"
done
for attr in 'dont_touch="maybe"' 'dont_touch=1, \synth.preroute_exclude = 0'; do
  write_source "$attr"
  if "$LHD" compile "$W/source.v" --top top --workdir "$W/bad" --set lhd.incremental=false --emit diagnostics:"$W/bad.jsonl" -q; then
    echo "FAIL: invalid or conflicting routing protection accepted: $attr"; exit 1
  fi
  grep -Eq 'synth-value|synth-duplicate' "$W/bad.jsonl"
done

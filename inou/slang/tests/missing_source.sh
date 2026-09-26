#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A -F filelist naming a missing source must FAIL the compile. slang reports it
# on stderr only; it once continued as an empty library that exited 0.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
printf 'module a(input x, output y); assign y = x; endmodule\n' > "$W/a.sv"
printf '%s\n%s\n' "$W/a.sv" "$W/does_not_exist.sv" > "$W/bad.f"
printf '%s\n' "$W/a.sv" > "$W/good.f"
if "$LHD" compile verilog --top a --emit-dir lg:"$W/lg_bad" --workdir "$W/wb" -q \
    --result-json "$W/bad.json" -- -F "$W/bad.f" > "$W/bad.log" 2>&1; then
  echo "FAIL: a missing -F source was accepted"; cat "$W/bad.log" "$W/bad.json"; exit 1
fi
grep -q 'slang front end failed' "$W/bad.json" || { echo "FAIL: no front-end error"; cat "$W/bad.json"; exit 1; }
"$LHD" compile verilog --top a --emit-dir lg:"$W/lg_good" --workdir "$W/wg" -q -- -F "$W/good.f"
echo 'PASS: a missing -F source fails the slang front end; a complete filelist compiles'

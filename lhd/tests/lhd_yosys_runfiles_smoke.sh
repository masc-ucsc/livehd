#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail

# Copy the executable so its normal adjacent runfiles cannot mask this bug.
W="${TEST_TMPDIR:-$(mktemp -d /tmp/lhd_yosys_runfiles.XXXXXX)}"
mkdir -p "$W/bin"
cp -L lhd/lhd "$W/bin/lhd"
cat > "$W/input.v" <<'EOF'
module runfiles_example(input a, b, output y);
  assign y = a ^ b;
endmodule
EOF

for workspace in _main livehd livehd+; do
  root="$W/runfiles-$workspace"
  mkdir -p "$root/$workspace/inou/yosys"
  cp inou/yosys/inou_yosys_read.ys "$root/$workspace/inou/yosys/"
  RUNFILES_DIR="$root" TEST_SRCDIR= "$W/bin/lhd" compile "$W/input.v" \
    --reader yosys-verilog --top runfiles_example --workdir "$W/work-$workspace" \
    --emit verilog:"$W/out-$workspace.v"
  test -s "$W/out-$workspace.v"
done

RUNFILES_DIR= TEST_SRCDIR="$W/runfiles-livehd+" "$W/bin/lhd" compile "$W/input.v" \
  --reader yosys-verilog --top runfiles_example --workdir "$W/work-test-srcdir" \
  --emit verilog:"$W/out-test-srcdir.v"
test -s "$W/out-test-srcdir.v"

# An explicit bad script must not silently fall back to bundled defaults.
if RUNFILES_DIR="$W/runfiles-_main" "$W/bin/lhd" compile "$W/input.v" \
    --reader yosys-verilog --top runfiles_example --workdir "$W/work-bad" \
    --set compile.yosys.script="$W/absent.ys" > "$W/bad.log" 2>&1; then
  echo "FAIL: explicit nonexistent script was ignored" >&2
  exit 1
fi
grep -q 'absent.ys' "$W/bad.log"
echo "PASS: relocated Yosys reader resolves runfiles and honors explicit scripts"

#!/bin/bash
set -euo pipefail

# Bazel's local-test PATH omits Homebrew on macOS; the external leg needs it.
export PATH="/opt/homebrew/bin:${PATH}"

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

SRC=${1:?missing Pyrope source}
WD="${TEST_TMPDIR:-tmp_prp_array_oob_runtime}/array_oob_runtime"
mkdir -p "$WD/v" "$WD/w"

"$LHD" compile "$SRC" \
  --emit-dir verilog:"$WD/v/" --workdir "$WD/w" -q >/dev/null

VFILES=("$WD"/v/*.v)
VFILE=${VFILES[0]}

# The index is masked (`lanes[idx & 3]`), so the address is proven in range at
# compile time: there is no run-time bounds assertion to look for. Native
# simulation checks every u3 index, in range or not, wraps through the mask.
cat >"$WD/vectors.json" <<'JSON'
[{"inputs":{"idx":0},"outputs":{"o":17}},{"inputs":{"idx":1},"outputs":{"o":34}},{"inputs":{"idx":2},"outputs":{"o":51}},{"inputs":{"idx":3},"outputs":{"o":68}},
 {"inputs":{"idx":4},"outputs":{"o":17}},{"inputs":{"idx":5},"outputs":{"o":34}},{"inputs":{"idx":6},"outputs":{"o":51}},{"inputs":{"idx":7},"outputs":{"o":68}}]
JSON
LHD="$LHD" python3 tools/native_sim.py "$VFILE" array_oob_runtime "$WD/vectors.json" "$WD/native"

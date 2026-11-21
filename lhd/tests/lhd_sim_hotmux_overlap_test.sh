#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Run-time `unique if` overlap simulates (first active arm), and a simulator runtime that dies by a signal is
# reported by name, not as "code -1".
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_hotmux_overlap_$$}"
mkdir -p "$W"
F=lhd/tests/lhd_sim_hotmux_overlap.prp
"$LHD" sim "$F" --workdir "$W/sim" --result-json "$W/sim.json" --diag-fmt pretty

# a runtime that traps: lhd must say which signal killed it
cat > "$W/trap_rt.sh" <<'EOF'
#!/bin/sh
kill -ILL $$
EOF
chmod +x "$W/trap_rt.sh"
if LHD_SIMRT="$W/trap_rt.sh" "$LHD" sim "$F" --workdir "$W/sim" --run-only --result-json "$W/trap.json" --diag-fmt pretty; then
  echo "FAIL: a trapping runtime passed" >&2
  exit 1
fi
python3 - "$W/trap.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
text = json.dumps(doc)
assert doc['status'] == 'fail', doc
assert 'killed by signal SIGILL' in text, text
PY
echo "PASS: overlapping unique-if controls simulate; a trapping runtime is reported by signal name"

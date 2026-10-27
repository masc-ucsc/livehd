#!/bin/bash
set -euo pipefail
LHD=./lhd/lhd
[[ -x "$LHD" ]] || LHD=./bazel-bin/lhd/lhd
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/null-port-memory.XXXXXX")
trap 'rm -rf "$W"' EXIT
"$LHD" compile inou/slang/tests/sv/null_port_memory.v --top null_port_memory \
  --emit-dir lg:"$W/lg" --workdir "$W/compile" > "$W/compile.log" 2>&1 \
  || { cat "$W/compile.log"; exit 1; }
"$LHD" tool cat lg:"$W/lg" --top null_port_bank --diag-fmt jsonl > "$W/graph.jsonl"
python3 - "$W/graph.jsonl" <<'PY'
import json, sys
nodes = [json.loads(line) for line in open(sys.argv[1])]
memories = [n for n in nodes if n.get('t') == 'node' and n.get('kind') == 'memory']
assert len(memories) == 1 and memories[0]['name'] == 'mem', memories
PY

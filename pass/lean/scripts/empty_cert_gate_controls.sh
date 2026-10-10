#!/usr/bin/env bash
# Controls for the empty-certificate branch of op_census.py.
#
# The branch used to refuse on the NODE COUNT while its message asked about
# emit_cert, so a design that genuinely folds to constants was reported FAILED.
# These call THE GATE ITSELF on mutated copies of a real artifact: an empty
# certificate must pass only when it is PRESENT and COVERS every ordinal read.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
GATE="$ROOT/pass/lean/scripts/op_census.py"
SRC="${1:-$ROOT/generated/master_port/replay/jobs/CORE-ET/legacy/minion_dcache_texsend/latest/lean/minion_dcache_texsend_Lgraph.lean}"
WORK="$ROOT/generated/master_port/empty_cert_controls"

if [ ! -f "$SRC" ]; then
  echo "REFUSE: no empty-certificate artifact at $SRC" >&2
  echo "        (pass one as \$1; this suite will not fabricate a certificate)" >&2
  exit 2
fi
rm -rf "$WORK"; mkdir -p "$WORK"
fails=0

# want=0 -> the gate must accept; want=1 -> the gate must refuse.
run_case() { # name want file
  local name="$1" want="$2" f="$3" got
  python3 -I "$GATE" "$f" > "$WORK/$name.log" 2>&1; got=$?
  if [ "$got" -eq "$want" ]; then
    echo "ok: $name -> exit $got ($(grep -m1 -E '^(PASS|FAIL|NO |STALE)' "$WORK/$name.log" | cut -c1-64))"
  else
    echo "MISS: $name -> exit $got, wanted $want"; sed -n '1,12p' "$WORK/$name.log"; fails=$((fails+1))
  fi
}

mut() { # name  python-expression-over-s
  local name="$1"; shift
  python3 -I -c "
import sys
s = open(sys.argv[1]).read()
$1
open(sys.argv[2],'w').write(s)
" "$SRC" "$WORK/$name.lean"
  echo "$WORK/$name.lean"
}

run_case unmodified 0 "$SRC"

# emit_cert=false: legacy_model.cpp emits the marker and no graphCert at all.
f=$(mut emit_cert_false "
import re
s = re.sub(r'^def \w+_graphCert[^\n]*\n[^\n]*\n', '-- Certificate emission disabled by lean.emit_cert=false.\n', s, count=1, flags=re.M)")
run_case emit_cert_false 1 "$f"

# The graphCert definition removed without the marker -- still absent, still refused.
f=$(mut no_graph_cert "
import re
s = re.sub(r'^def \w+_graphCert[^\n]*\n[^\n]*\n', '', s, count=1, flags=re.M)")
run_case no_graph_cert 1 "$f"

# An output reads an ordinal that no source defines.
f=$(mut dangling_ordinal "
s = s.replace('rho 1000000002', 'rho 1000000099', 1)")
run_case dangling_ordinal 1 "$f"

# Outputs declared, but the certificate environment is never read: whatever the
# outputs are, they are not coming from this certificate.
f=$(mut outputs_not_from_cert "
import re
s = re.sub(r'\brho (\d+)', r'ZERO \1', s)")
run_case outputs_not_from_cert 1 "$f"

# The pre-certificate memory stub keeps its own refusal.
f=$(mut memory_stub "
s = s.replace('def minion_dcache_texsend_nodeCerts', 'def minion_dcache_texsend_memory_count : Nat := 3\n\ndef minion_dcache_texsend_nodeCerts', 1)")
run_case memory_stub 1 "$f"

# REGRESSION: a design WITH nodes must still take the per-node census path, not
# this branch.  Without this, deleting the whole census would score 6/6 here.
NODED=""
for c in "$ROOT"/generated/master_port/replay/jobs/CORE-ET/legacy/*/latest/lean/*_Lgraph.lean; do
  [ -f "$c" ] || continue
  n=$(grep -c 'LGraphOp\.' "$c")
  if [ "$n" -gt 0 ]; then NODED="$c"; break; fi
done
if [ -n "$NODED" ]; then
  run_case noded_design 0 "$NODED"
  if grep -q 'empty certificate' "$WORK/noded_design.log"; then
    echo "MISS: noded_design took the empty-certificate branch"; fails=$((fails+1))
  elif ! grep -q 'histogram' "$WORK/noded_design.log"; then
    echo "MISS: noded_design did not run the per-node census"; fails=$((fails+1))
  else
    echo "ok: noded_design -> per-node census, not the empty branch"
  fi
else
  echo "SKIP: noded_design -- no emitted artifact with nodes was found to check against"
fi

if [ "$fails" -eq 0 ]; then
  echo "PASS: an empty certificate is accepted only when present and covering"
else
  echo "FAIL: $fails control(s) missed"
fi
exit $((fails > 0))

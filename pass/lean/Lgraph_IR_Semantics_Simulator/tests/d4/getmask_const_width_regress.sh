#!/usr/bin/env bash
# Regression for the Op_GetMask constant-mask width defect.
#
# THE DEFECT.  `Get_mask(a, -1)` is LiveHD's zext idiom, so pass.lean
# deliberately materialises the mask at `max(src_w, out_w)` -- for -1 that is
# all-ones over the source.  But that widening was applied to CONCRETE positive
# patterns too, and `max(src_w, out_w)` can be SMALLER than the value needs.
# `cva6_hpdcache_subsystem` shipped
#
#     SourceDesc.const 2 8     -> Op_GetMask origin 516164   (mk_bv 2 8  = 0)
#     SourceDesc.const 6 124   -> Op_GetMask origin 516384   (mk_bv 6 124 = 60)
#
# so two masks selected the wrong source bits, and `cert_dep_id` had labelled
# them with the caller's `expected_w` without ever consulting the strict fit
# check in `pin_width`.
#
# THE FIX.  The mask pin's own `pin_width` joins the max, so widening can never
# truncate, and the mask constant is subject to the fit invariant rather than
# bypassing it.
#
# This checks the invariant NAMED AT THE OPERAND, which is stronger than the
# generic "every positive constant fits its width" gate, and it plants the
# pre-fix shape to prove the check and both gates can still reject it.
#
#   getmask_const_width_regress.sh [emitted_Lgraph.lean]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
ART="${1:-$ROOT/temp/hpd_fixed/blocks/cva6_hpdcache_subsystem/lean/cva6_hpdcache_subsystem_gate_Lgraph.lean}"
GATE_VC="$ROOT/pass/lean/Exporter/DesignCert/scripts/vc_gates.py"
GATE_DIFF="$ROOT/pass/lean/Exporter/Frontend/scripts/cert_lgraph_diff.py"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

if [[ ! -s "$ART" ]]; then
  echo "SKIP: no emitted artifact at $ART (pass one as \$1)"; exit 0
fi
mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/gmreg.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# check_masks <lean> -> prints one line per violation, exit 1 if any
check_masks() {
  python3 - "$1" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r'sources\s*:=\s*#\[(.*?)\]\s*,?\s*\n\s*nodes', txt, re.S)
if not m:
    print("VIOLATION: cannot locate the sources array"); sys.exit(1)
raw = [r.strip() for r in re.split(r',\s*(?=SourceDesc\.)', m.group(1).strip()) if r.strip()]
def const_of(i):
    if i >= len(raw): return None
    mm = re.match(r'SourceDesc\.const\s+(\d+)\s+\(\((-?)Int\.ofNat\s+(\d+)\)\)', raw[i])
    if not mm: return None
    return int(mm.group(1)), (mm.group(2) == '-'), int(mm.group(3))
viol = 0; seen = 0
for op, w, deps, origin in re.findall(
        r'\{ op := (\S+?),? width := (\d+), deps := #\[([^\]]*)\], origin := (\d+) \}', txt):
    if op != 'LGraphOp.Op_GetMask':
        continue
    dl = [d.strip() for d in deps.split(',') if d.strip()]
    if len(dl) != 2:
        continue
    seen += 1
    c = const_of(int(dl[1]))
    if c is None:
        continue                      # non-constant mask: nothing to check here
    cw, neg, val = c
    if neg:
        continue                      # -1 is all-ones at any width, by design
    if val >= (1 << cw):
        viol += 1
        print(f"VIOLATION: Op_GetMask origin {origin} width {w}: "
              f"mask const declared width {cw} cannot hold {val} "
              f"(needs {val.bit_length()} bits)")
print(f"checked {seen} Op_GetMask nodes, {viol} violation(s)")
sys.exit(1 if viol else 0)
PY
}

# --- 1. the fixed artifact satisfies the operand-level invariant -----------
out="$(check_masks "$ART")"; rc=$?
if [[ "$rc" -eq 0 ]]; then ok "GetMask mask widths: $(tail -1 <<<"$out")"
else bad "the artifact still violates the GetMask mask-width invariant:"; sed 's/^/        /' <<<"$out"; fi

# --- 2. and both existing gates accept it ---------------------------------
top="$(basename "${ART%_Lgraph.lean}")"
python3 "$GATE_VC" "$ART" "$top" --quiet >"$TMP/g1" 2>&1;  r1=$?
python3 "$GATE_DIFF" "$ART" --quiet      >"$TMP/g2" 2>&1;  r2=$?
if [[ "$r1" -eq 0 && "$r2" -eq 0 ]]; then
  ok "vc_gates.py and cert_lgraph_diff.py both accept the artifact"
else
  bad "gates reject the artifact (vc_gates rc=$r1, cert_lgraph_diff rc=$r2): $(grep -h '^FAIL:' "$TMP/g1" "$TMP/g2" | head -2 | tr '\n' ';')"
fi

# --- 3. PLANT THE PRE-FIX SHAPE: it must be rejected ----------------------
# Narrow one GetMask mask constant back below its value, exactly as the defect
# did, and require BOTH the operand check and the generic gate to catch it.
PLANT="$TMP/planted_Lgraph.lean"
python3 - "$ART" "$PLANT" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
m = re.search(r'sources\s*:=\s*#\[(.*?)\]\s*,?\s*\n\s*nodes', txt, re.S)
raw = [r.strip() for r in re.split(r',\s*(?=SourceDesc\.)', m.group(1).strip()) if r.strip()]
idx = {}
for i, r in enumerate(raw):
    mm = re.match(r'SourceDesc\.const\s+(\d+)\s+\(\((-?)Int\.ofNat\s+(\d+)\)\)', r)
    if mm and mm.group(2) != '-':
        idx[i] = (int(mm.group(1)), int(mm.group(3)))
target = None
for op, w, deps, origin in re.findall(
        r'\{ op := (\S+?),? width := (\d+), deps := #\[([^\]]*)\], origin := (\d+) \}', txt):
    if op != 'LGraphOp.Op_GetMask':
        continue
    dl = [d.strip() for d in deps.split(',') if d.strip()]
    if len(dl) != 2:
        continue
    i = int(dl[1])
    if i in idx:
        cw, val = idx[i]
        if val > 0 and val.bit_length() >= 2:
            target = (i, cw, val); break
if target is None:
    sys.stderr.write("no plantable GetMask constant mask found\n"); sys.exit(2)
i, cw, val = target
narrow = max(1, val.bit_length() - 1)      # strictly too narrow to hold it
# EDIT BY INDEX, not by string: the same `SourceDesc.const w v` text occurs many
# times, so replacing the first occurrence would narrow some OTHER source and
# leave the one this GetMask points at untouched -- the plant would then test
# nothing at the operand level.
before, body, after = txt[:m.start(1)], m.group(1), txt[m.end(1):]
parts = re.split(r'(,\s*(?=SourceDesc\.))', body.strip())
elems = parts[0::2]
seps  = parts[1::2]
assert elems[i].strip() == raw[i], "source array split does not line up with the index"
elems[i] = elems[i].replace(f"SourceDesc.const {cw} ", f"SourceDesc.const {narrow} ", 1)
rebuilt = ''.join(a + b for a, b in zip(elems, seps + ['']))
open(sys.argv[2], 'w').write(before + rebuilt + after)
print(f"planted: source[{i}] width {cw} -> {narrow} holding {val}")
PY
prc=$?
if [[ "$prc" -ne 0 ]]; then
  bad "could not plant the pre-fix shape, so the rejection half proves nothing"
else
  out="$(check_masks "$PLANT")"; rc=$?
  if [[ "$rc" -eq 0 ]]; then
    bad "the operand-level check ACCEPTED a mask constant narrower than its value"
  else
    ok "planted pre-fix shape rejected by the operand check: $(grep -m1 '^VIOLATION' <<<"$out" | cut -c1-74)..."
  fi
  python3 "$GATE_DIFF" "$PLANT" --quiet >"$TMP/g3" 2>&1; r3=$?
  if [[ "$r3" -eq 0 ]]; then
    bad "cert_lgraph_diff.py ACCEPTED the planted pre-fix shape -- its fit invariant has been weakened"
  else
    ok "planted pre-fix shape also rejected by cert_lgraph_diff.py: $(grep -m1 '^FAIL:' "$TMP/g3" | cut -c1-62)"
  fi
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

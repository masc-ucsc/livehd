#!/usr/bin/env bash
# cert_admit_selftest.sh: every simulation driver and proof tool admits a DCERT4 certificate and
# refuses, at load and before any step, every other format version and every certificate that
# admission refuses, with a message that names the file and the reason (case C4 of the format
# test plan).
#
# Certificates (written by tests/dcert_version_check.lean from tests/fixtures/ram12.dcert, a
# 12-word RAM exported by lhd; no lhd is needed here), each with ram12's _io.json beside it:
#   ok4      the DCERT4 base
#   ok3      the base as DCERT3 (no memory depth)
#   v2       the base with the DCERT2 magic
#   v1min    DCERT1, every section empty
#   v1flop   DCERT1 with one synchronous flop
#   fpfix    the repo's DCERT1 fixture (specialization/tests/fixtures/fp_reset_polarity_control.dcert)
#   depth0   a memory of depth 0
#   noclk    no clock domains
#   badout   an output reading a slot past the design
#   junk     not a certificate
# Drivers, run interpreted against formal/lean/.olean-dev:
#   cert_trace <c> 3 <stim>                 cert_run <c> max:3
#   lhd_ckpt init <c> <out>                 ckpt_reach check <c> <ok4's power-on ckpt> <stim>
#   sym_verify <c> ok_rd k:1                sym_refmap refmap <impl <c>, spec a memory-free design>
#   sym_probe <c> sim:ir                    dcert2_memroute <c> auto auto 3
#
# Then dcert_upgrade (pass/lean/tools/sim/dcert_upgrade.lean), DCERT3 to DCERT4 (case C7), and the
# design hash across the conversion (case C6):
#   dcert_upgrade ok3 up             exit 0; up is ok4full byte for byte (every memory at depth
#                                    2^aw, the DCERT3 meaning); the two files differ only in the
#                                    magic and one ` <depth>` per memory record; dcert_delta.py reads
#                                    up; a second run onto the existing up is refused (exit 2)
#   dcert_upgrade ok3space|ok3trunc|v2|v1flop|ok4|junk   refused (exit 1), nothing written
#   lhd_ckpt init ok4full a.ckpt; lhd_ckpt check up a.ckpt    OK: the same design hash
#   lhd_ckpt check ok3 a.ckpt        WANT_OK3_CKPT (today: OK, DCERT3 is read)
#   lhd_ckpt check ok4 a.ckpt        REFUSED, design hash (ok4's memory has depth 12)
#
# Expect: on ok4 every driver exits 0 and prints its normal result. Every input WANT names with a
# reason is refused: the driver exits nonzero, prints `<file>: ` and the reason, and prints no
# result of a run. An input WANT marks `admitted` is not refused at load (today's policy; the
# run's own result is not checked).
#
# Prints `PASS cert_admit_selftest: ...` and exits 0, or `FAIL ...` lines and exits 1.
#
# Environment: OUT (default generated/cert_admit_selftest), DRIVERS (default all eight).
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
T="$ROOT/pass/lean/tools"
OUT="${OUT:-$ROOT/generated/cert_admit_selftest}"
DRIVERS="${DRIVERS:-cert_trace cert_run lhd_ckpt ckpt_reach sym_verify sym_refmap sym_probe dcert2_memroute}"
[[ -d "$L/.olean-dev" ]] || { echo "FAIL: no $L/.olean-dev (run formal/lean/.build-proj.sh)"; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT/w"
FX="$OUT/fx"

# what each certificate must get from every driver: `ok` (admitted, the run exits 0), `admitted`
# (not refused at load), or an extended regex the refusal must match
declare -A WANT=(
  [ok4]=ok
  [ok3]=admitted
  [v2]='parse: bad magic'
  [v1min]='parse: bad magic'
  [v1flop]='parse: bad magic'
  [fpfix]='parse: bad magic'
  [depth0]='refused: memory 0 declares depth 0'
  [noclk]='refused: no clock domains'
  [badout]='refused: output 0 reads slot'
  [junk]='parse: bad magic'
)
CERTS="ok4 ok3 v2 v1min v1flop fpfix depth0 noclk badout junk"

lean_run() { # <file.lean> args...
  local f=$1; shift
  (cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$f" "$@")
}

# ---- certificates
lean_run "$SCRIPT_DIR/dcert_version_check.lean" "$SCRIPT_DIR/fixtures/ram12.dcert" "write:$FX" \
  > "$OUT/fixtures.log" 2>&1 || { cat "$OUT/fixtures.log"; echo "FAIL: the certificates were not written"; exit 1; }
cp "$ROOT/pass/lean/specialization/tests/fixtures/fp_reset_polarity_control.dcert" "$FX/fpfix.dcert"
for c in $CERTS; do
  [[ -s "$FX/$c.dcert" ]] || { echo "FAIL: no certificate $c"; exit 1; }
  cp "$SCRIPT_DIR/fixtures/ram12_io.json" "$FX/${c}_io.json"
done
# write enable, data 5, write address 4 at cycle 1, read address 4 from cycle 2
STIM="$FX/stim.txt"
printf 'stim v1\nin 4 1 1\nin 3 8 5\nat 1 in 5 2 1\nat 2 in 1 2 1\n' > "$STIM"
# the refinement spec: memory-free, no state, one output that allows the one channel
printf 'DCERT4\n1\n1 1 1\n0\n1\n0 1\n0\n0\n1\n3 clk\n' > "$FX/spec.dcert"
cat > "$FX/spec_io.json" <<'EOF'
{"schema_version": 1, "kind": "lean_design_io", "top": "spec", "runtime_input_arity": 0,
 "inputs": [], "outputs": [{"name": "allow", "ordinal": 0, "width": 1, "used_by_model": true}],
 "clocks": [{"name": "clk", "ordinal": 0, "implicit": false}], "flops": [], "memories": []}
EOF
# ckpt_reach's target: ok4's power-on state
lean_run "$T/sim/lhd_ckpt.lean" init "$FX/ok4.dcert" "$FX/ok4.ckpt" > "$OUT/ok4_ckpt.log" 2>&1 \
  || { cat "$OUT/ok4_ckpt.log"; echo "FAIL: lhd_ckpt init ok4"; exit 1; }

drive() { # <driver> <certificate>
  local d=$1 c=$2 w="$OUT/w/$1_$(basename "$2" .dcert)"
  mkdir -p "$w"
  case $d in
    cert_trace) lean_run "$L/Futamura_Projection_Scripts/cert_trace.lean" "$c" 3 "$STIM" ;;
    cert_run) lean_run "$T/sim/cert_run.lean" "$c" max:3 ;;
    lhd_ckpt) lean_run "$T/sim/lhd_ckpt.lean" init "$c" "$w/init.ckpt" ;;
    ckpt_reach) lean_run "$T/proof/ckpt_reach.lean" check "$c" "$FX/ok4.ckpt" "$STIM" ;;
    sym_verify) lean_run "$T/proof/sym_verify.lean" "$c" ok_rd k:1 "work:$w" ;;
    sym_refmap)
      printf 'impl %s\nspec %s\nchan c allow:allow\n' "$c" "$FX/spec.dcert" > "$w/r.refmap"
      lean_run "$T/proof/sym_refmap.lean" refmap "$w/r.refmap" "work:$w" ;;
    sym_probe) lean_run "$T/proof/sym_probe.lean" "$c" sim:ir ;;
    dcert2_memroute) lean_run "$L/Futamura_Projection_Scripts/dcert2_memroute.lean" "$c" auto auto 3 ;;
    *) echo "unknown driver $d"; return 2 ;;
  esac
}

# a refusal at load: `<file>: ` then the admission's reason
refused_at_load() { grep -qE "$(basename "$1"): (parse: |refused: )" "$2"; }

fails=0; nok=0; nref=0; nadm=0
for d in $DRIVERS; do
  for c in $CERTS; do
    f="$FX/$c.dcert"; log="$OUT/${d}_$c.log"
    drive "$d" "$f" > "$log" 2>&1; rc=$?
    want=${WANT[$c]}
    case $want in
      ok)
        if [[ $rc -eq 0 ]] && ! refused_at_load "$f" "$log"; then nok=$((nok + 1))
        else echo "FAIL $d $c: want a run that exits 0, got exit $rc ($(tail -2 "$log" | tr '\n' ' ' | cut -c1-160))"; fails=$((fails + 1)); fi ;;
      admitted)
        if ! refused_at_load "$f" "$log"; then nadm=$((nadm + 1))
        else echo "FAIL $d $c: want it admitted, got $(grep -m1 -E 'parse: |refused: ' "$log" | cut -c1-160)"; fails=$((fails + 1)); fi ;;
      *)
        if [[ $rc -ne 0 ]] && refused_at_load "$f" "$log" && grep -qE "$want" "$log" \
           && ! grep -qE '^STEP |RESULT: (INVARIANT|REFINEMENT|REACHABLE|OK)|^PROBE .* OK' "$log"; then
          nref=$((nref + 1))
        else
          echo "FAIL $d $c: want a refusal at load matching '$want', got exit $rc ($(grep -m1 -E 'parse: |refused: |RESULT' "$log" | cut -c1-160))"
          fails=$((fails + 1))
        fi ;;
    esac
  done
done

# ---- dcert_upgrade (C7) and the design hash across it (C6)
WANT_OK3_CKPT=ok   # today: lhd_ckpt reads DCERT3
up_run() { lean_run "$T/sim/dcert_upgrade.lean" "$@"; }
U="$OUT/upgrade"; mkdir -p "$U"
up_run "$FX/ok3.dcert" "$U/up.dcert" > "$U/ok3.log" 2>&1; rc=$?
if [[ $rc -eq 0 ]] && grep -q 'same record: yes' "$U/ok3.log" && grep -q '^RESULT: CONVERTED' "$U/ok3.log" \
   && cmp -s "$U/up.dcert" "$FX/ok4full.dcert"; then
  echo "ok: dcert_upgrade ok3 -> ok4full, byte for byte"
else echo "FAIL dcert_upgrade ok3: exit $rc ($(tail -2 "$U/ok3.log" | tr '\n' ' '))"; fails=$((fails + 1)); fi
# the bytes differ in the magic and by one ` <depth>` at the end of each memory record, nothing else
if python3 - "$FX/ok3.dcert" "$U/up.dcert" <<'EOF'
import sys
a, b = (open(p, 'rb').read().split(b'\n') for p in sys.argv[1:3])
assert len(a) == len(b) and a[0] == b'DCERT3' and b[0] == b'DCERT4', 'magic or line count'
d = [(x, y) for x, y in zip(a[1:], b[1:]) if x != y]
assert d and all(y.startswith(x + b' ') and y[len(x) + 1:].isdigit() for x, y in d), d[:3]
EOF
then echo "ok: the conversion changes only the magic and appends each memory's depth"
else echo "FAIL dcert_upgrade: the bytes differ in more than the magic and the depths"; fails=$((fails + 1)); fi
if python3 "$ROOT/pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/dcert_delta.py" "$U/up.dcert" "$U/up.dcert" \
     > "$U/delta.log" 2>&1; then echo "ok: dcert_delta.py reads the converted certificate"
else echo "FAIL dcert_delta.py refuses the converted certificate: $(tail -1 "$U/delta.log")"; fails=$((fails + 1)); fi
up_run "$FX/ok3.dcert" "$U/up.dcert" > "$U/again.log" 2>&1; rc=$?
if [[ $rc -eq 2 ]] && cmp -s "$U/up.dcert" "$FX/ok4full.dcert"; then echo "ok: an existing output is refused (exit 2)"
else echo "FAIL dcert_upgrade onto an existing file: exit $rc"; fails=$((fails + 1)); fi
for c in ok3space ok3trunc v2 v1flop ok4 junk; do
  case $c in
    ok3space) why='not canonical DCERT3' ;; ok3trunc) why='parse: malformed' ;;
    ok4) why='already DCERT4' ;; *) why='parse: bad magic: expected DCERT3' ;;
  esac
  up_run "$FX/$c.dcert" "$U/$c.dcert" > "$U/$c.log" 2>&1; rc=$?
  if [[ $rc -eq 1 && ! -e "$U/$c.dcert" ]] && grep -q "$c.dcert: $why" "$U/$c.log"; then
    echo "ok: dcert_upgrade refuses $c ($why), nothing written"
  else echo "FAIL dcert_upgrade $c: want exit 1, '$why', no file; got exit $rc ($(head -1 "$U/$c.log"))"; fails=$((fails + 1)); fi
done
ck() { lean_run "$T/sim/lhd_ckpt.lean" "$@"; }
ck init "$FX/ok4full.dcert" "$U/a.ckpt" > "$U/init.log" 2>&1 || { echo "FAIL lhd_ckpt init ok4full"; fails=$((fails + 1)); }
ck check "$U/up.dcert" "$U/a.ckpt" > "$U/check_up.log" 2>&1; rc=$?
if [[ $rc -eq 0 ]] && grep -q '^OK: cycle 0' "$U/check_up.log"; then echo "ok: a checkpoint of ok4full checks against the converted certificate"
else echo "FAIL lhd_ckpt check up: exit $rc ($(tail -1 "$U/check_up.log"))"; fails=$((fails + 1)); fi
ck check "$FX/ok3.dcert" "$U/a.ckpt" > "$U/check_ok3.log" 2>&1; rc=$?
if [[ $WANT_OK3_CKPT == ok ]]; then
  if [[ $rc -eq 0 ]] && grep -q '^OK: cycle 0' "$U/check_ok3.log"; then echo "ok: the DCERT3 file has the same design hash (today: DCERT3 is read)"
  else echo "FAIL lhd_ckpt check ok3: exit $rc ($(tail -1 "$U/check_ok3.log"))"; fails=$((fails + 1)); fi
elif [[ $rc -ne 0 ]] && grep -qE "$WANT_OK3_CKPT" "$U/check_ok3.log"; then echo "ok: lhd_ckpt refuses the DCERT3 file"
else echo "FAIL lhd_ckpt check ok3: want a refusal '$WANT_OK3_CKPT', got exit $rc"; fails=$((fails + 1)); fi
ck check "$FX/ok4.dcert" "$U/a.ckpt" > "$U/check_ok4.log" 2>&1; rc=$?
if [[ $rc -eq 1 ]] && grep -q 'REFUSED' "$U/check_ok4.log" && grep -q 'design hash' "$U/check_ok4.log"; then
  echo "ok: control: ok4 (depth 12) has another design hash"
else echo "FAIL control lhd_ckpt check ok4: exit $rc ($(tail -1 "$U/check_ok4.log"))"; fails=$((fails + 1)); fi

# control: the run saw both an admitted and a refused certificate on every driver
nd=$(wc -w <<< "$DRIVERS")
nrefc=0; for c in $CERTS; do case ${WANT[$c]} in ok|admitted) ;; *) nrefc=$((nrefc + 1)) ;; esac; done
if [[ $nok -ne $nd || $nref -ne $((nd * nrefc)) ]]; then
  echo "FAIL control: $nok of $nd drivers ran ok4, $nref of $((nd * nrefc)) refusals"
  fails=$((fails + 1))
fi
if [[ $fails -gt 0 ]]; then echo "FAIL cert_admit_selftest: $fails"; exit 1; fi
echo "PASS cert_admit_selftest: $nd drivers, ok4 run by each, $nref refusals at load, $nadm admitted (today's policy); dcert_upgrade and the design hash"

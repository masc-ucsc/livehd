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
  [v2]='bad magic|unrecognised certificate'
  [v1min]=admitted
  [v1flop]=admitted
  [fpfix]='refused: a flopQAsync source names a flop'
  [depth0]='refused: memory 0 declares depth 0'
  [noclk]='refused: no clock domains'
  [badout]='refused: output 0 reads slot'
  [junk]='bad magic|unrecognised certificate'
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
refused_at_load() { grep -qE "$(basename "$1"): (parse: |refused: |unrecognised certificate)" "$2"; }

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
        else echo "FAIL $d $c: want it admitted, got $(grep -m1 -E 'parse: |refused: |unrecognised' "$log" | cut -c1-160)"; fails=$((fails + 1)); fi ;;
      *)
        if [[ $rc -ne 0 ]] && refused_at_load "$f" "$log" && grep -qE "$want" "$log" \
           && ! grep -qE '^STEP |RESULT: (INVARIANT|REFINEMENT|REACHABLE|OK)|^PROBE .* OK' "$log"; then
          nref=$((nref + 1))
        else
          echo "FAIL $d $c: want a refusal at load matching '$want', got exit $rc ($(grep -m1 -E 'parse: |refused: |unrecognised|RESULT' "$log" | cut -c1-160))"
          fails=$((fails + 1))
        fi ;;
    esac
  done
done

# control: the run saw both an admitted and a refused certificate on every driver
nd=$(wc -w <<< "$DRIVERS")
nrefc=0; for c in $CERTS; do case ${WANT[$c]} in ok|admitted) ;; *) nrefc=$((nrefc + 1)) ;; esac; done
if [[ $nok -ne $nd || $nref -ne $((nd * nrefc)) ]]; then
  echo "FAIL control: $nok of $nd drivers ran ok4, $nref of $((nd * nrefc)) refusals"
  fails=$((fails + 1))
fi
if [[ $fails -gt 0 ]]; then echo "FAIL cert_admit_selftest: $fails"; exit 1; fi
echo "PASS cert_admit_selftest: $nd drivers, ok4 run by each, $nref refusals at load, $nadm admitted (today's policy)"

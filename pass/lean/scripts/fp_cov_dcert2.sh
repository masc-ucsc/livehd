#!/usr/bin/env bash
# FUTAMURA / DCERT2 serial coverage procedure: decode -> support -> generate ->
# readiness.  One certificate at a time, each phase bound and recorded
# separately, every producer exit propagated.
#
# THE FOUR PHASES ARE DIFFERENT CLAIMS AND ARE NEVER COLLAPSED:
#
#   decode     dcert2_loadcheck.lean  -- the bytes are admitted by the format's
#              OWN policy (DCERT2 -> readCertChecked).  It evaluates NO support
#              predicate; a decode pass says nothing about supportedness.
#   support    dcert2_support.lean    -- the six SupportedByProjection conjuncts,
#              and when one is false, the CONCRETE sources / operators / memories
#              responsible.
#   generate   gen_probe_v2.lean      -- specialize + Hw.checkResidual.  Produces
#              an IN-MEMORY Program and runs NO residual step; this is a
#              generation-and-check result, not a usable-simulator result.
#   readiness  gen_readiness_v2.lean  -- actually executes the residual through
#              Hw.stepOf at the checked bound.  It RE-SPECIALIZES because nothing
#              was serialized, so it is a NEW attempt with its own cost and its
#              specialize time is NOT a second generation measurement.
#
# REUSE, NOT REPLAY.  A phase whose retained cell already holds `exit 0` is NOT
# re-run: it is reported REUSED with the retained record.  `--only readiness`
# skips straight to the readiness phase for certificates whose earlier phases
# already passed.  Re-running a passed hour-long generation to reach the phase
# after it is replay, and replay is what this flag exists to prevent.  Use
# --force to deliberately redo a phase; it then lands in a NEW attempt root.
#
# INTERPRETER STACK.  `lean --run` aborts these designs with "deep recursion was
# detected at 'interpreter'" inside Projection.mixTerms at the default stack.
# `--tstack=262144` alone did NOT help; `ulimit -s unlimited` together with
# `--tstack=1048576` gets past it.  That is a route/configuration requirement,
# recorded as such -- it is NOT an optimization result and NOT a regression, and
# no further stack-limit experiments are run here.
#
# Everything here is INTERPRETED.  Native linking is a separate readiness mode
# and is not implied by any of it: the native route (scripts/build-opt.sh) builds
# Compiler/CertIOV1 and NOT Compiler/CertIO, because the DCERT2 codec reaches
# Compiler/CompileDesign, which does `import Mathlib`.
#
#   fp_cov_dcert2.sh [--only PHASE] [--force] <private-checkout> <out-dir> \
#                    <stepFuel> <workFuel> <cycles> <cert.dcert>...
set -uo pipefail
ONLY=all; FORCE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --only)  ONLY=${2:?--only needs a phase}; shift 2 ;;
    --force) FORCE=1; shift ;;
    --) shift; break ;;
    -*) echo "REFUSING: unknown option $1"; exit 2 ;;
    *) break ;;
  esac
done
case "$ONLY" in all|decode|support|generate|readiness) : ;;
  *) echo "REFUSING: --only must be all|decode|support|generate|readiness, got '$ONLY'"; exit 2 ;;
esac
P=${1:?private checkout root}; OUT=${2:?out dir}
SF=${3:?stepFuel}; WF=${4:?workFuel}; CYC=${5:?cycles}; shift 5

# ---- configuration is validated BEFORE any work -------------------------
for v in SF WF CYC; do
  case "${!v}" in ''|*[!0-9]*) echo "REFUSING: $v='${!v}' is not a natural number"; exit 2 ;; esac
done
[ "$SF"  -gt 0 ] || { echo "REFUSING: stepFuel must be positive"; exit 2; }
[ "$WF"  -gt 0 ] || { echo "REFUSING: workFuel must be positive"; exit 2; }
# Zero cycles executes no residual step, so a READINESS OK would be a pass
# earned by doing nothing.  The Lean driver refuses it too; refusing here as
# well means the sweep never launches the work in the first place.
[ "$CYC" -gt 0 ] || { echo "REFUSING: cycles must be POSITIVE; zero executes no residual step"; exit 2; }
# A sweep over zero certificates is not a successful sweep.
[ "$#" -gt 0 ] || { echo "REFUSING: no certificate given; a sweep with no target is not a pass"; exit 2; }

R=/mada/users/czeng14/projects/livehd-pass-lean-master
LEANBIN=/mada/users/czeng14/.elan/toolchains/leanprover--lean4---v4.31.0/bin/lean
TOOLCHAIN=/mada/users/czeng14/.elan/toolchains/leanprover--lean4---v4.31.0
[ -x "$LEANBIN" ] || { echo "REFUSING: no lean at $LEANBIN"; exit 2; }
PABS=$(cd "$P" && pwd -P) || { echo "REFUSING: cannot resolve $P"; exit 2; }

LP=$(cd "$PABS/formal/lean" && lake env printenv LEAN_PATH 2>/dev/null)
[ -n "$LP" ] || { echo "REFUSING: could not resolve LEAN_PATH from $PABS/formal/lean"; exit 2; }
# EVERY entry must be inside the independently owned private closure, or inside
# the one explicitly allowed immutable toolchain.  Checking only that the
# private root appears SOMEWHERE in the string would pass a LEAN_PATH that also
# carried a live shared build root -- which is exactly the shared-mutable-state
# failure this whole private-root exercise exists to avoid.
IFS=':' read -r -a LPDIRS <<< "$LP"
for d in "${LPDIRS[@]}"; do
  [ -n "$d" ] || continue
  case "$d" in
    "$PABS"/*|"$TOOLCHAIN"/*) : ;;
    *) echo "REFUSING: LEAN_PATH entry outside the private closure and the allowed toolchain: $d"; exit 2 ;;
  esac
done

DRV_DECODE=$R/formal/lean/scripts/dcert2_loadcheck.lean
DRV_SUPPORT=$R/formal/lean/scripts/dcert2_support.lean
DRV_GEN=$R/formal/lean/scripts/gen_probe_v2.lean
DRV_READY=$R/formal/lean/scripts/gen_readiness_v2.lean
for f in "$DRV_DECODE" "$DRV_SUPPORT" "$DRV_GEN" "$DRV_READY"; do
  [ -f "$f" ] || { echo "REFUSING: missing driver $f"; exit 2; }
done

mkdir -p "$OUT"

# phase <cell> <timeout> <driver.lean> <driver-args...>
#   argv is passed as an ARRAY and handed to the inner shell as positional
#   parameters, so a path containing spaces survives intact.  `ulimit` needs a
#   shell, which is why one is used at all; `$*` splicing is not.
phase() {
  local cell=$1 tmo=$2 drv=$3; shift 3
  local -a dargs=("$@")

  if [ "$FORCE" = 0 ] && [ -f "$cell/exit" ] && [ "$(cat "$cell/exit")" = 0 ]; then
    echo "REUSED exit=0 (retained; not re-run)" > "$cell/DISPOSITION.txt"
    return 0
  fi
  mkdir -p "$cell"
  { printf '%s\n' "$LEANBIN" "--tstack=1048576" "--run" "$drv" "${dargs[@]}"
  } > "$cell/argv.txt"
  { echo "driver=$drv sha256=$(sha256sum "$drv" | cut -d' ' -f1)"
    echo "lean=$LEANBIN version=$("$LEANBIN" --version)"
    echo "launched_at=$(date -Iseconds)"
    echo "timeout_s=$tmo ulimit_stack=unlimited tstack=1048576"
    echo "LEAN_PATH=$LP"
  } > "$cell/BINDING.txt"

  LEAN_PATH="$LP" /usr/bin/time \
      -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$cell/time.txt" \
      bash -c 'ulimit -s unlimited; exec nice -n19 ionice -c3 timeout "$@"' bash \
      "$tmo" "$LEANBIN" --tstack=1048576 --run "$drv" "${dargs[@]}" \
      > "$cell/stdout.log" 2> "$cell/stderr.log"
  local rc=$? t tr
  echo "$rc" > "$cell/exit"
  t=$(awk '$1=="exit"{print $2; found=1} END{exit !found}' "$cell/time.txt" 2>/dev/null); tr=$?
  if [ "$tr" != 0 ] || [ -z "$t" ]; then
    echo "MISSING exit=$rc time_exit=- (no parseable producer status)" > "$cell/DISPOSITION.txt"; return 97
  fi
  if [ "$t" != "$rc" ]; then
    echo "DISAGREE exit=$rc producer=$t" > "$cell/DISPOSITION.txt"; return 96
  fi
  case "$rc" in
    0)   echo "OK exit=0" ;;
    124) echo "CAP exit=124 (timeout ${tmo}s; partial and UNDECIDED, not a pass and not a refusal)" ;;
    *)   echo "REFUSED exit=$rc" ;;
  esac > "$cell/DISPOSITION.txt"
  return "$rc"
}

want() { [ "$ONLY" = all ] || [ "$ONLY" = "$1" ]; }

worst=0
note() { [ "$worst" = 0 ] && worst=$1; return 0; }
for cert in "$@"; do
  [ -f "$cert" ] || { echo "REFUSING: no certificate at $cert"; note 2; continue; }
  m=$(basename "$cert" .dcert); d=$OUT/$m
  mkdir -p "$d"
  { echo "cert=$cert sha256=$(sha256sum "$cert" | cut -d' ' -f1) bytes=$(stat -c%s "$cert")"
    echo "private_head=$(git -C "$PABS" rev-parse HEAD)"
    echo "private_root=$PABS"
    echo "host=$(uname -n) kernel=$(uname -r)"
    echo "fuel=$SF/$WF cycles=$CYC only=$ONLY force=$FORCE"
    echo "route=INTERPRETED (lean --run, ulimit -s unlimited, --tstack=1048576); NOT native linking"
  } > "$d/BINDING.txt"

  if want decode; then
    phase "$d/decode" 600 "$DRV_DECODE" "$cert"; e=$?
    echo "$m decode    $(cat "$d/decode/DISPOSITION.txt")"
    [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want support; then
    phase "$d/support" 600 "$DRV_SUPPORT" "$cert"; e=$?
    echo "$m support   $(cat "$d/support/DISPOSITION.txt")"
    [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want generate; then
    phase "$d/generate" 3600 "$DRV_GEN" "$cert" "$SF" "$WF"; e=$?
    echo "$m generate  $(cat "$d/generate/DISPOSITION.txt")"
    # exit 2 = unsupported, 3 = out of fuel: REAL dispositions for this
    # certificate.  They stop this row without stopping the sweep.
    [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want readiness; then
    phase "$d/readiness" 5400 "$DRV_READY" "$cert" "$SF" "$WF" "$CYC"; e=$?
    echo "$m readiness $(cat "$d/readiness/DISPOSITION.txt")"
    [ "$e" = 0 ] || note "$e"
  fi
done
echo "FP DCERT2 SWEEP DISPOSITION exit=$worst (0 = every selected phase reached OK or was REUSED)"
exit "$worst"

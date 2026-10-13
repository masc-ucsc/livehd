#!/usr/bin/env bash
# FUTAMURA / DCERT2 serial coverage procedure: decode -> support -> generate ->
# readiness.  One certificate at a time, each phase recorded as an IMMUTABLE
# ATTEMPT, every producer exit propagated.
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
# ATTEMPTS ARE IMMUTABLE.  Every run writes a fresh `<phase>/attempt-NNNN-<ts>/`
# and nothing ever writes into an existing one -- not a retry, not `--force`, not
# a reuse decision.  An earlier version wrote the reuse verdict straight over the
# retained `DISPOSITION.txt`, destroying the record it claimed to be honouring.
# Reuse decisions now live in `<phase>/decisions/`, and the per-request binding in
# `<module>/requests/`, so a retained attempt's binding, logs, exit, timing and
# disposition are never touched.
#
# REUSE IS EARNED, NOT ASSUMED.  A COUNTEREXAMPLE made this concrete: a retained
# cell with `exit`=0 but GNU time reporting `exit 1` was reused as a pass, because
# the reuse branch read only the `exit` file and skipped the status validation a
# fresh phase performs.  Reuse now requires ALL of:
#     * `exit` present, parseable and 0;
#     * a parseable GNU-time producer status that is also 0 (both records, agreeing);
#     * a `DISPOSITION.txt` that begins with `OK`;
#     * a byte-identical IDENTITY.txt -- same certificate CONTENT, same driver
#       CONTENT, same lean binary and version, same resolved LEAN_PATH, same
#       argv (which carries fuel and cycles) and the same timeout.
# Anything missing, disagreeing, stale or differently configured is REFUSED as
# evidence for this request, and a new attempt is run instead.  Reuse therefore
# never relabels an old result as a fresh source run.
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
# Compiler/CompileDesign, which does `import Mathlib`, and this workspace's
# mathlib package currently ships oleans with no native objects.  That is a
# MISSING PREREQUISITE in this route, not a proof that such a build is impossible.
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
# earned by doing nothing.  The Lean driver refuses it too.
[ "$CYC" -gt 0 ] || { echo "REFUSING: cycles must be POSITIVE; zero executes no residual step"; exit 2; }
# A sweep over zero certificates is not a successful sweep.
[ "$#" -gt 0 ] || { echo "REFUSING: no certificate given; a sweep with no target is not a pass"; exit 2; }

R=${FP_REPO_ROOT:-/WORKROOT/projects/livehd-pass-lean-master}
LEANBIN=${FP_LEAN_BIN:-/WORKROOT/.elan/toolchains/leanprover--lean4---v4.31.0/bin/lean}
TOOLCHAIN=${FP_TOOLCHAIN_ROOT:-/WORKROOT/.elan/toolchains/leanprover--lean4---v4.31.0}
[ -x "$LEANBIN" ] || { echo "REFUSING: no lean at $LEANBIN"; exit 2; }
PABS=$(readlink -e "$P") || { echo "REFUSING: cannot resolve $P"; exit 2; }
TCABS=$(readlink -e "$TOOLCHAIN") || { echo "REFUSING: cannot resolve toolchain $TOOLCHAIN"; exit 2; }

LP=$(cd "$PABS/formal/lean" && lake env printenv LEAN_PATH 2>/dev/null)
[ -n "$LP" ] || { echo "REFUSING: could not resolve LEAN_PATH from $PABS/formal/lean"; exit 2; }
# EVERY entry is RESOLVED and then checked for containment.  A lexical check is
# not a containment check: a symlink under the private root that points at a
# shared mutable build tree passes any string test and still imports foreign
# bytes.  Empty entries are refused too -- an empty element means "the current
# directory", which is implicit, unrecorded state.
IFS=':' read -r -a LPDIRS <<< "$LP"
LPNORM=""        # the NORMALIZED, RESOLVED entries, which is what goes in the identity
LPPRIV=()        # the private-root entries whose .olean content is digested
for d in "${LPDIRS[@]}"; do
  if [ -z "$d" ]; then
    echo "REFUSING: LEAN_PATH contains an empty entry, which means the implicit current directory"; exit 2; fi
  case "$d" in /*) : ;; *) echo "REFUSING: LEAN_PATH entry is not absolute: $d"; exit 2 ;; esac
  # `readlink -m` canonicalises WITHOUT requiring existence, and still resolves
  # symlinks in the components that do exist -- so a symlinked entry pointing at
  # a shared mutable build tree is still caught, while an entry `lake env` emits
  # for a package with nothing built (it does: the Cli package) is not a refusal.
  rd=$(readlink -m "$d")
  case "$rd" in
    "$PABS"/*|"$TCABS"/*) : ;;
    *) echo "REFUSING: LEAN_PATH entry resolves outside the private closure and the allowed toolchain:"
       echo "          $d -> $rd"; exit 2 ;;
  esac
  ex=no; [ -d "$rd" ] && ex=yes
  LPNORM="$LPNORM$rd:$ex"$'\n'
  case "$rd" in "$PABS"/*) [ "$ex" = yes ] && LPPRIV+=("$rd") ;; esac
done
LEANVER=$("$LEANBIN" --version)
# THE EXECUTABLE'S BYTES, not its path and version string.  A counterexample
# showed that replacing the lean binary in place left the identity unchanged.
LEANSHA=$(sha256sum "$LEANBIN" | cut -d' ' -f1)
# THE IMPORTED BYTES, not the LEAN_PATH string.  A second counterexample showed
# that editing a dependency `.olean` at the same LEAN_PATH left the identity
# unchanged.  Digested once per invocation and reused by every phase: measured
# 8,651 oleans / 1,909 MiB in 14.7 s on this workspace, which is dependency
# accounting and belongs to no generation or readiness metric.
LPCLOSURE=$( { for d in "${LPPRIV[@]}"; do find "$d" -name '*.olean' -print0; done; } \
             | sort -z | xargs -0 -r sha256sum | sha256sum | cut -d' ' -f1 )
echo "lean_path closure digest $LPCLOSURE over ${#LPPRIV[@]} private entries"

DRV_DECODE=$R/formal/lean/scripts/dcert2_loadcheck.lean
DRV_SUPPORT=$R/formal/lean/scripts/dcert2_support.lean
DRV_GEN=$R/formal/lean/scripts/gen_probe_v2.lean
DRV_READY=$R/formal/lean/scripts/gen_readiness_v2.lean
for f in "$DRV_DECODE" "$DRV_SUPPORT" "$DRV_GEN" "$DRV_READY"; do
  [ -f "$f" ] || { echo "REFUSING: missing driver $f"; exit 2; }
done

mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M%S)-$$

# identity_of <out-file> <timeout> <driver> <cert> <args...>
#   The identity a reuse must match EXACTLY.  Content hashes, not paths alone:
#   a driver edited in place must not be mistaken for the one that ran.
identity_of() {
  local f=$1 tmo=$2 drv=$3 cert=$4; shift 4
  local -a dargs=("$@")
  local drvn certn
  drvn=$(readlink -m "$drv"); certn=$(readlink -m "$cert")
  { echo "cert=$certn"
    echo "cert_sha256=$(sha256sum "$cert" | cut -d' ' -f1)"
    echo "cert_bytes=$(stat -c%s "$cert")"
    echo "driver=$drvn"
    echo "driver_sha256=$(sha256sum "$drv" | cut -d' ' -f1)"
    echo "lean_bin=$(readlink -m "$LEANBIN")"
    echo "lean_bin_sha256=$LEANSHA"
    echo "lean_version=$LEANVER"
    echo "lean_path_closure_sha256=$LPCLOSURE"
    printf '%s' "$LPNORM" | sed 's/^/lean_path_entry=/'
    # The driver's DIRECT resolved imports, by content.  `lean --deps` prints the
    # `.olean` each import actually resolved to on this LEAN_PATH, so this binds
    # the bytes that will be loaded rather than the search path that finds them.
    if [ -n "${FP_SKIP_DEPS:-}" ]; then
      echo "import_direct=<skipped: FP_SKIP_DEPS>"
    else
      LEAN_PATH="$LP" "$LEANBIN" --deps "$drv" 2>/dev/null | sort -u | while read -r o; do
        [ -n "$o" ] || continue
        echo "import_direct=$(readlink -m "$o") $(sha256sum "$o" 2>/dev/null | cut -d' ' -f1)"
      done
    fi
    echo "timeout_s=$tmo"
    echo "tstack=1048576 ulimit_stack=unlimited"
    # EXACT argv, one element per line.  A single `$*` string cannot distinguish
    # two arguments from one argument containing a space.
    local i=0 a
    for a in "$LEANBIN" "--tstack=1048576" "--run" "$drv" "${dargs[@]}"; do
      printf 'argv[%d]=%s\n' "$i" "$a"; i=$((i+1))
    done
  } > "$f"
}

# reusable <attempt-dir> <wanted-identity-file>  -- 0 iff it may be reused
#   Reads only; writes nothing into the attempt.
reusable() {
  local a=$1 want=$2 e t tr
  [ -d "$a" ] || return 1
  [ -f "$a/exit" ] || { echo "    no exit record"; return 1; }
  e=$(cat "$a/exit"); case "$e" in ''|*[!0-9]*) echo "    unparseable exit '$e'"; return 1 ;; esac
  [ "$e" = 0 ] || { echo "    retained exit=$e is not success"; return 1; }
  t=$(awk '$1=="exit"{print $2; f=1} END{exit !f}' "$a/time.txt" 2>/dev/null); tr=$?
  if [ "$tr" != 0 ] || [ -z "$t" ]; then echo "    no parseable producer status"; return 1; fi
  [ "$t" = "$e" ] || { echo "    wrapper exit=$e disagrees with producer exit=$t"; return 1; }
  [ -f "$a/DISPOSITION.txt" ] || { echo "    no disposition"; return 1; }
  case "$(head -1 "$a/DISPOSITION.txt")" in OK*) : ;; *) echo "    disposition is not OK"; return 1 ;; esac
  [ -f "$a/IDENTITY.txt" ] || { echo "    no identity record"; return 1; }
  cmp -s "$a/IDENTITY.txt" "$want" || { echo "    identity differs from this request"; return 1; }
  return 0
}

# phase <phase-dir> <timeout> <driver> <driver-args...>
phase() {
  local pd=$1 tmo=$2 drv=$3; shift 3
  local -a dargs=("$@")
  local cert=${dargs[0]}
  mkdir -p "$pd/decisions"
  local want=$pd/decisions/want-$TS.identity
  identity_of "$want" "$tmo" "$drv" "$cert" "${dargs[@]}"

  local dec=$pd/decisions/DECISION-$TS.txt
  if [ "$FORCE" = 0 ]; then
    local a why
    for a in "$pd"/attempt-*; do
      [ -d "$a" ] || continue
      why=$(reusable "$a" "$want") && {
        { echo "decision=REUSE"; echo "attempt=$a"; echo "at=$(date -Iseconds)"
          echo "basis=exit 0, producer status 0, disposition OK, identity byte-identical"
        } > "$dec"
        echo "REUSED exit=0 from $(basename "$a") (retained attempt untouched)"
        return 0
      }
    done
    { echo "decision=RUN"; echo "at=$(date -Iseconds)"
      echo "reason=no retained attempt satisfied exit/producer-status/disposition/identity"
    } > "$dec"
  else
    { echo "decision=RUN"; echo "at=$(date -Iseconds)"; echo "reason=--force"; } > "$dec"
  fi

  # A NEW, IMMUTABLE attempt directory.  Never an existing one -- not on a
  # retry, not under --force.
  local n=1 a
  while :; do
    a=$(printf '%s/attempt-%04d-%s' "$pd" "$n" "$TS")
    mkdir "$a" 2>/dev/null && break
    n=$((n+1)); [ "$n" -gt 9999 ] && { echo "REFUSING: cannot allocate an attempt dir in $pd"; return 2; }
  done
  echo "attempt=$a" >> "$dec"
  mv "$want" "$a/IDENTITY.txt"
  printf '%s\n' "$LEANBIN" "--tstack=1048576" "--run" "$drv" "${dargs[@]}" > "$a/argv.txt"
  { echo "launched_at=$(date -Iseconds)"
    echo "host=$(uname -n) kernel=$(uname -r)"
    echo "cwd=$PWD"
    echo "private_root=$PABS"
    echo "private_head=$(git -C "$PABS" rev-parse HEAD 2>/dev/null)"
    echo "timeout_s=$tmo"
    echo "LEAN_PATH=$LP"
    echo "lean_path_closure_sha256=$LPCLOSURE"
  } > "$a/BINDING.txt"

  LEAN_PATH="$LP" /usr/bin/time \
      -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$a/time.txt" \
      bash -c 'ulimit -s unlimited; exec nice -n19 ionice -c3 timeout "$@"' bash \
      "$tmo" "$LEANBIN" --tstack=1048576 --run "$drv" "${dargs[@]}" \
      > "$a/stdout.log" 2> "$a/stderr.log"
  local rc=$? t tr
  echo "$rc" > "$a/exit"
  t=$(awk '$1=="exit"{print $2; f=1} END{exit !f}' "$a/time.txt" 2>/dev/null); tr=$?
  if [ "$tr" != 0 ] || [ -z "$t" ]; then
    echo "MISSING exit=$rc time_exit=- (no parseable producer status)" > "$a/DISPOSITION.txt"
    printf '%s\t%s\t%s\n' "$(basename "$a")" "MISSING" "$rc" >> "$pd/index.tsv"; return 97
  fi
  if [ "$t" != "$rc" ]; then
    echo "DISAGREE exit=$rc producer=$t" > "$a/DISPOSITION.txt"
    printf '%s\t%s\t%s\n' "$(basename "$a")" "DISAGREE" "$rc" >> "$pd/index.tsv"; return 96
  fi
  case "$rc" in
    0)   echo "OK exit=0" ;;
    124) echo "CAP exit=124 (timeout ${tmo}s; partial and UNDECIDED, not a pass and not a refusal)" ;;
    *)   echo "REFUSED exit=$rc" ;;
  esac > "$a/DISPOSITION.txt"
  printf '%s\t%s\t%s\n' "$(basename "$a")" "$(head -1 "$a/DISPOSITION.txt" | cut -d' ' -f1)" "$rc" >> "$pd/index.tsv"
  return "$rc"
}

want() { [ "$ONLY" = all ] || [ "$ONLY" = "$1" ]; }

worst=0
note() { [ "$worst" = 0 ] && worst=$1; return 0; }
for cert in "$@"; do
  [ -f "$cert" ] || { echo "REFUSING: no certificate at $cert"; note 2; continue; }
  m=$(basename "$cert" .dcert); d=$OUT/$m
  mkdir -p "$d/requests"
  # Per-REQUEST, never overwritten: an earlier version rewrote one module-level
  # BINDING before deciding reuse, so the record of the run that produced the
  # retained attempt was replaced by the request that reused it.
  { echo "request_at=$(date -Iseconds)"
    echo "cert=$cert sha256=$(sha256sum "$cert" | cut -d' ' -f1) bytes=$(stat -c%s "$cert")"
    echo "private_root=$PABS"
    echo "private_head=$(git -C "$PABS" rev-parse HEAD 2>/dev/null)"
    echo "host=$(uname -n) kernel=$(uname -r)"
    echo "fuel=$SF/$WF cycles=$CYC only=$ONLY force=$FORCE"
    echo "lean_bin=$LEANBIN version=$LEANVER sha256=$LEANSHA"
    echo "lean_path_closure_sha256=$LPCLOSURE"
    echo "route=INTERPRETED (lean --run, ulimit -s unlimited, --tstack=1048576); NOT native linking"
  } > "$d/requests/REQUEST-$TS.txt"

  if want decode; then
    phase "$d/decode" 600 "$DRV_DECODE" "$cert"; e=$?
    echo "$m decode    exit=$e"; [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want support; then
    phase "$d/support" 600 "$DRV_SUPPORT" "$cert"; e=$?
    echo "$m support   exit=$e"; [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want generate; then
    phase "$d/generate" 3600 "$DRV_GEN" "$cert" "$SF" "$WF"; e=$?
    echo "$m generate  exit=$e"
    # exit 2 = unsupported, 3 = out of fuel: REAL dispositions for this
    # certificate.  They stop this row without stopping the sweep.
    [ "$e" = 0 ] || { note "$e"; continue; }
  fi
  if want readiness; then
    phase "$d/readiness" 5400 "$DRV_READY" "$cert" "$SF" "$WF" "$CYC"; e=$?
    echo "$m readiness exit=$e"; [ "$e" = 0 ] || note "$e"
  fi
done
echo "FP DCERT2 SWEEP DISPOSITION exit=$worst (0 = every selected phase reached OK or was REUSED)"
exit "$worst"

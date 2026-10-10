#!/usr/bin/env bash
# Direction 4 RUNTIME-certificate sweep.
#
# The old route (`run_vc_sweep.sh`, `run_cva6_vc_sweep.sh`) collected
# `_Lgraph.lean` files and handed them to `run_lean_queue.sh`, which ELABORATES
# each one -- the per-design proof path Direction 4 exists to replace, and
# 91-98% of end-to-end wall time on the large modules.  This script never
# elaborates anything.  Per block it runs
#
#     RTL --lhd--> <mod>.dcert --CertIO.loadCert--> runChecked
#
# at the ORDINARY stack limit, and emits one machine-readable row.
#
# Everything is resolved relative to THIS worktree.  `run_cva6_vc_sweep.sh`
# hardcodes R=/soe/czeng14/projects/livehd-new, so it could silently use another
# checkout's binary, sources, wrappers and outputs; nothing here may.  Every
# required input AND TOOL is checked before the first block, so a missing
# dependency is one FATAL line rather than 122 rows of downstream noise paid for
# after the whole corpus's front-end work.
#
# Usage:
#   scripts/run_d4_runtime_sweep.sh <coreet|cva6> <module-list-file> <outdir> [jobs]
#
# Env (all optional, all validated when set):
#   LAKE          lake executable        (default $HOME/.elan/bin/lake, else PATH)
#   COREET_ROOT   CORE-ET RTL corpus     (default /soe/czeng14/projects/core-et)
#   CVA6_FILELIST flat filelist for CVA6 (no default -- required for cva6)
#   CVA6_WRAP_DIR <mod>_gate.sv wrappers (no default -- required for cva6)
#   EMIT_TIMEOUT  per-block emission timeout, seconds (default 1800)
#   RUN_TIMEOUT   per-block runtime timeout, seconds  (default 900)
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

# The limit-hit verdict lives in its own sourceable file so it can be tested
# with synthetic inputs; the shape that matters is not producible on demand.
# shellcheck source=d4_scope_verdict.sh
. "$SCRIPT_DIR/d4_scope_verdict.sh" || die "cannot source $SCRIPT_DIR/d4_scope_verdict.sh"

# scope_cleanup <unit-without-suffix>; prints "clean" or what is left behind.
scope_cleanup() {
  local u="$1.scope"
  systemctl --user stop "$u" >/dev/null 2>&1
  systemctl --user reset-failed "$u" >/dev/null 2>&1
  local st; st="$(systemctl --user show "$u" -p LoadState --value 2>/dev/null)"
  local tc; tc="$(systemctl --user show "$u" -p TasksCurrent --value 2>/dev/null)"
  if [[ "$st" == "not-found" || -z "$st" ]]; then echo clean
  else echo "LEFTOVER LoadState=$st TasksCurrent=${tc:-?}"; fi
}

CORPUS="${1:?usage: run_d4_runtime_sweep.sh <coreet|cva6> <module-list> <outdir> [jobs]}"
LIST="${2:?missing module-list file}"
OUTDIR="${3:?missing output directory}"
JOBS="${4:-1}"

case "$CORPUS" in
  coreet|cva6) ;;
  # `selftest` exists so the driver's OWN decision ordering can be tested
  # end to end.  It replaces ONLY the emission child with a named helper;
  # the row contract, the memory ceiling, the kernel verdict, the
  # limit-hit-before-certificate ordering and the Lean runtime step are the
  # SAME code the real corpora use.  Rows carry `corpus=selftest`, so no
  # selftest row can ever be mistaken for CORE-ET or CVA6 evidence.
  selftest) ;;
  *) die "corpus must be 'coreet', 'cva6' or 'selftest', got '$CORPUS'" ;;
esac
[[ "$JOBS" =~ ^[0-9]+$ && "$JOBS" -ge 1 ]] || die "jobs must be a positive integer, got '$JOBS'"

# ---------------------------------------------------------------------------
# THE ROW CONTRACT.  Eighteen columns, in this order, emitted through ONE helper
# so no branch can drift.  A 17-field NO_WRAPPER row that pushed the diagnostic
# into `wall_s` and still reconciled clean is exactly what this replaces.
# ---------------------------------------------------------------------------
ROW_FIELDS=18
HEADER=$'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb'

row() {
  [[ $# -eq $ROW_FIELDS ]] || die "internal: row() given $# fields, need $ROW_FIELDS"
  local IFS=$'\t'
  printf '%s\n' "$*"
}

# ---------------------------------------------------------------------------
# Fail closed on every input AND every tool, before a single block runs.
# ---------------------------------------------------------------------------
# AN EXPLICIT `LHD` IS HONOURED EXACTLY, under the same policy this script
# already applies to `LAKE`: a named executable is never replaced by a
# fallback. `bazel-bin/lhd/lhd` is whatever the last `bazel build` left there
# -- on this tree a fastbuild -- and the accepted corpus rows were produced by
# the OPTIMIZED build, so a run that silently used the other one would not be
# comparable to them. The binary and its mode are recorded either way.
if [[ -n "${LHD:-}" ]]; then
  [[ -x "$LHD" ]] || die "LHD=$LHD is not an executable; an explicit setting is never replaced by a fallback"
else
  LHD="$ROOT/bazel-bin/lhd/lhd"
  [[ -x "$LHD" ]] || die "no lhd binary at $LHD (build //lhd:lhd in THIS worktree), or set LHD=<path>"
fi

# `lake` is not reliably on PATH -- a bare call exits 127, which would become one
# NO_ROW per block AFTER paying for emission.  Resolve it once, up front.
#
# AN EXPLICIT `LAKE` IS HONOURED EXACTLY.  Falling back to PATH when the operator
# named a specific executable is worse than failing: the run then silently
# measures a DIFFERENT toolchain than the one asked for, and still writes
# `reconciled=YES`.  Only an UNSET `LAKE` may search.
if [[ -n "${LAKE:-}" ]]; then
  [[ -x "$LAKE" ]] || die "LAKE=$LAKE is not an executable; an explicit setting is never replaced by a fallback"
else
  LAKE="$HOME/.elan/bin/lake"
  [[ -x "$LAKE" ]] || LAKE="$(command -v lake 2>/dev/null || true)"
  [[ -n "$LAKE" && -x "$LAKE" ]] || die "no usable lake executable; set LAKE=<path> (tried \$HOME/.elan/bin/lake and PATH)"
fi

# The VERSION LINE, not the first line of output.  A wrapper that prints a
# warning first would otherwise put `warning: ...` in provenance as the version
# -- a provenance field that lies is worse than one that is absent.
# Measured WHERE THE ROWS ARE PRODUCED (`$ROOT/formal/lean`).  elan selects the
# toolchain from the cwd, so a version taken at the invocation directory can name
# a toolchain that produced no row at all: this worktree's root resolves to Lean
# 4.34.1 while `formal/lean/lean-toolchain` pins 4.31.0, and every row comes from
# the latter.
LAKE_VERSION_RAW="$(cd "$ROOT/formal/lean" && "$LAKE" --version 2>&1)" || die "$LAKE --version failed"
LAKE_VERSION="$(grep -m1 '^Lake version ' <<<"$LAKE_VERSION_RAW" || true)"
[[ -n "$LAKE_VERSION" ]] || die "$LAKE --version printed no line starting 'Lake version '; got: $(head -3 <<<"$LAKE_VERSION_RAW" | tr '\n' '|')"
# Anything printed before the version line is kept, separately, never as the version.
LAKE_VERSION_PRELUDE="$(grep -v '^Lake version ' <<<"$LAKE_VERSION_RAW" | head -3 | tr '\n' '|')"

[[ -r "$LIST" ]] || die "module list $LIST is unreadable"
[[ -s "$LIST" ]] || die "module list $LIST is empty"
[[ -d "$ROOT/formal/lean/.lake/build" ]] || die "no local Lean build at $ROOT/formal/lean/.lake/build"
ROW_DRIVER="$ROOT/formal/lean/probes/d4_sweep_row.lean"
[[ -r "$ROW_DRIVER" ]] || die "missing the row driver at $ROW_DRIVER"

# THE PREFLIGHT'S DISPOSITION, PARSED -- NOT SEARCHED FOR.
#
# The row driver's eleven fields are
#   1 sources  2 nodes  3 flops  4 memories  5 parse_status  6 parse_ms
#   7 compiles_ok  8 runtime_status  9 run_ms  10 digest  11 diagnostic
#
# A missing file must come back as READ_ERROR / NOT_RUN with NO compile verdict
# and NO digest. The previous check was `grep -q READ_ERROR` over the whole
# row, which a perfectly successful `PARSED / true / RAN` row satisfies as soon
# as the two words appear anywhere in its diagnostic PROSE -- so a preflight
# that proved nothing passed, and the toolchain was never actually exercised on
# the failure path it exists to test.
preflight_fields_ok() { # <row>
  awk -F'\t' 'NF==11 && $5=="READ_ERROR" && $7=="-" && $8=="NOT_RUN" && $10=="-" { ok = 1 }
               END { exit ok ? 0 : 1 }' <<<"$1"
}

# PREFLIGHT: prove the toolchain actually runs the driver.  A tool that only
# fails at row time has already cost the whole corpus's front-end work.
#
# Its EXIT STATUS is required too. The status was being discarded entirely
# here -- the substitution's value was checked and nothing else -- so a child
# that printed a correct row and then failed blessed a toolchain that cannot
# run. The logs are kept rather than sent to /dev/null, because a preflight
# that refuses has to say why.
mkdir -p "$ROOT/temp" || die "cannot create $ROOT/temp"
PRE_DIR="$(mktemp -d "$ROOT/temp/d4preflight.XXXXXX")" \
  || die "cannot create a preflight scratch dir under $ROOT/temp"
( cd "$ROOT/formal/lean" && "$LAKE" env lean --run "$ROW_DRIVER" /nonexistent.dcert \
    > "$PRE_DIR/preflight.out" 2> "$PRE_DIR/preflight.err" )
PRE_RC=$?
PRE="$(tail -1 "$PRE_DIR/preflight.out" 2>/dev/null)"
[[ "$PRE_RC" -eq 0 ]] || die "row driver preflight exited $PRE_RC (see $PRE_DIR/preflight.err)"
[[ -n "$PRE" ]] || die "$LAKE env lean cannot run the row driver (preflight produced no row)"
[[ "$(awk -F'\t' '{print NF}' <<<"$PRE")" == "11" ]] || die "row driver preflight returned the wrong shape"
preflight_fields_ok "$PRE" \
  || die "row driver preflight did not report READ_ERROR/NOT_RUN with no compile verdict and no digest for a missing file (got: $PRE)"

mapfile -t MODULES < <(grep -v '^[[:space:]]*$' "$LIST" | grep -v '^[[:space:]]*#' | sed 's/[[:space:]]*$//')
N_DECLARED="${#MODULES[@]}"
N_UNIQUE="$(printf '%s\n' "${MODULES[@]}" | sort -u | wc -l)"
[[ "$N_DECLARED" -eq "$N_UNIQUE" ]] || die "module list has duplicates ($N_DECLARED entries, $N_UNIQUE unique)"
[[ "$N_DECLARED" -gt 0 ]] || die "module list has no entries"

if [[ "$CORPUS" == "coreet" ]]; then
  # AN EXPLICIT COREET_ROOT IS HONOURED EXACTLY.  Falling back when the operator
  # named a checkout is how a run silently measures a different corpus.
  if [[ -n "${COREET_ROOT:-}" ]]; then
    [[ -d "$COREET_ROOT" ]] || die "COREET_ROOT=$COREET_ROOT is not a directory; an explicit setting is never replaced by a fallback"
  else
    COREET_ROOT="/soe/czeng14/projects/core-et"
    [[ -d "$COREET_ROOT" ]] || die "CORE-ET corpus not found at $COREET_ROOT"
  fi
  # ONE PREFLIGHT BINDS THE EXTERNAL CORPUS.  It resolves the checkout once,
  # requires its full revision, requires the filelist flow's OWN input paths to
  # be free of tracked changes and untracked files, and verifies the four
  # derived slang-clean sources against their sources byte for byte.  Its output
  # is the corpus provenance for this run.
  #
  # Without the derived sources slang rejects `vpu_defs_pkg.sv` and
  # `intpipe_mul_div_ctl.sv`, 26 blocks that DO emit are reported as emission
  # failures, and the denominator is wrong: measured 63 NO_CERT without them,
  # 37 with, against a historical 37.
  COREET_PREFLIGHT="$SCRIPT_DIR/coreet_preflight.sh"
  [[ -x "$COREET_PREFLIGHT" ]] || die "missing $COREET_PREFLIGHT"
  # `--publish`: the four derived sources are FRESHLY DERIVED into the exact
  # directory the filelist consumes, then verified byte-for-byte against a
  # second, independent derivation.  Verifying the embedded source-hash header
  # alone was not enough -- a derived BODY can be edited with that header intact.
  COREET_PROV="$("$COREET_PREFLIGHT" "$COREET_ROOT" \
                   "$ROOT/generated/core-et/patched_src" \
                   "$SCRIPT_DIR/coreet_patch_srcs.py" --publish)" \
    || die "CORE-ET preflight failed; refusing to measure an unpinned corpus"
  # The canonical tech, passed EXPLICITLY rather than inherited.  The preflight
  # has already refused any other value.
  COREET_TECH="$(grep -m1 '^coreet_tech=' <<<"$COREET_PROV" | cut -d= -f2)"
elif [[ "$CORPUS" == "selftest" ]]; then
  [[ -n "${SELFTEST_EMIT_CMD:-}" ]] || die "selftest needs SELFTEST_EMIT_CMD=<helper>"
  [[ -x "${SELFTEST_EMIT_CMD}" ]] || die "SELFTEST_EMIT_CMD $SELFTEST_EMIT_CMD is not executable"
else
  [[ -n "${CVA6_FILELIST:-}" ]] || die "cva6 needs CVA6_FILELIST (no default: the old sweep read it from another checkout)"
  [[ -s "${CVA6_FILELIST}" ]] || die "CVA6_FILELIST $CVA6_FILELIST is missing or empty"
  [[ -n "${CVA6_WRAP_DIR:-}" ]] || die "cva6 needs CVA6_WRAP_DIR (no default: the old sweep read it from another checkout)"
  [[ -d "${CVA6_WRAP_DIR}" ]] || die "CVA6_WRAP_DIR $CVA6_WRAP_DIR is not a directory"
  # `run_cva6_module_lean_stress.sh` uses CVA6_ROOT to pick the CONFIG PACKAGE.
  # Left unset it defaults to a different checkout, so the filelist and wrappers
  # would come from the revision named here while the config came from
  # somewhere else -- a silent cross-revision build.  Required, like the other
  # two, and recorded.
  [[ -n "${CVA6_ROOT:-}" ]] || die "cva6 needs CVA6_ROOT (the config package must come from the SAME revision as CVA6_FILELIST)"
  [[ -d "${CVA6_ROOT}" ]] || die "CVA6_ROOT $CVA6_ROOT is not a directory"
  CVA6_TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
  CVA6_CONFIG_PKG="$CVA6_ROOT/core/include/${CVA6_TARGET}_config_pkg.sv"
  [[ -r "$CVA6_CONFIG_PKG" ]] || die "no config package at $CVA6_CONFIG_PKG"

  # -------------------------------------------------------------------------
  # BIND EVERY CVA6 INPUT TO ONE FOUNDATION MANIFEST, BEFORE ANY BLOCK RUNS.
  #
  # Requiring CVA6_ROOT is not enough, and a probe showed exactly why: a FAKE,
  # non-git root holding nothing but a COPY of the config package, combined with
  # the real checkout's filelist and wrappers, ran to completion and wrote
  # `reconciled=YES` with `cva6_root_rev=not-a-git-checkout`.  Rows from a mixed
  # root are not rows about any source state.
  #
  # So the root, its revision, its cleanliness, the config package, the
  # filelist, the wrapper corpus, the wrapper generator and the module list must
  # ALL match ONE manifest written by `build_cva6_foundation.sh`.
  # -------------------------------------------------------------------------
  CVA6_FOUNDATION="${CVA6_FOUNDATION:-$ROOT/generated/cva6_foundation/provenance.txt}"
  [[ -r "$CVA6_FOUNDATION" ]] || die "cva6 needs a foundation manifest at $CVA6_FOUNDATION (run scripts/build_cva6_foundation.sh), or set CVA6_FOUNDATION=<path>"
  mfield() { grep -m1 "^$1=" "$CVA6_FOUNDATION" | cut -d= -f2-; }
  want() {  # want <label> <expected> <actual>
    [[ "$2" == "$3" ]] || die "foundation mismatch: $1
       manifest: ${2:-<absent>}
       actual:   ${3:-<absent>}
       The foundation and this run do not describe the same source state.
       Rebuild with scripts/build_cva6_foundation.sh, or point CVA6_* at it."
  }

  FOUND_ROOT_CANON="$(mfield cva6_root_canonical)"
  [[ -n "$FOUND_ROOT_CANON" ]] || die "$CVA6_FOUNDATION has no cva6_root_canonical; it predates the fail-closed binding -- rebuild it"
  want "CVA6_ROOT canonical path" "$FOUND_ROOT_CANON" "$(readlink -f "$CVA6_ROOT")"

  NOW_REV="$(git -C "$CVA6_ROOT" rev-parse HEAD 2>/dev/null || true)"
  [[ -n "$NOW_REV" ]] || die "CVA6_ROOT $CVA6_ROOT is not a git checkout; the source revision must be nameable"
  want "CVA6 revision" "$(mfield cva6_rev)" "$NOW_REV"

  NOW_DIRTY="$(git -C "$CVA6_ROOT" status --porcelain --untracked-files=no 2>/dev/null)"
  [[ -z "$NOW_DIRTY" ]] || die "CVA6_ROOT $CVA6_ROOT has TRACKED modifications; a dirty tree at $NOW_REV is not $NOW_REV"

  want "CVA6 target"              "$(mfield cva6_target)"              "$CVA6_TARGET"
  want "config package sha256"    "$(mfield cva6_config_pkg_sha256)"   "$(sha256sum < "$CVA6_CONFIG_PKG" | cut -d' ' -f1)"
  want "filelist sha256"          "$(mfield filelist_sha256)"          "$(sha256sum < "$CVA6_FILELIST" | cut -d' ' -f1)"
  want "wrapper generator sha256" "$(mfield wrapper_generator_sha256)" "$(sha256sum < "$ROOT/scripts/gen_cva6_wrappers.py" | cut -d' ' -f1)"
  want "wrapper directory"        "$(readlink -f "$(mfield wrapper_dir)")" "$(readlink -f "$CVA6_WRAP_DIR")"

  FOUND_LIST="$(mfield module_list)"
  [[ -r "$FOUND_LIST" ]] || die "the foundation's module list $FOUND_LIST is unreadable"
  want "module list sha256"       "$(mfield module_list_sha256)"       "$(sha256sum < "$FOUND_LIST" | cut -d' ' -f1)"

  # The corpus hash is over ALL the foundation's declared modules, in its order
  # -- not over this run's subset -- so a wrapper swapped anywhere in the corpus
  # is caught even when this run does not touch it.
  mapfile -t FOUND_MODULES < <(grep -v '^[[:space:]]*#' "$FOUND_LIST" | grep -v '^[[:space:]]*$' | sed 's/[[:space:]]*$//')
  want "wrapper corpus sha256"    "$(mfield wrappers_sha256)" "$(for fm in "${FOUND_MODULES[@]}"; do
          fw="$CVA6_WRAP_DIR/${fm}_gate.sv"
          if [[ -r "$fw" ]]; then echo "$fm $(sha256sum < "$fw" | cut -d' ' -f1)"; else echo "$fm -"; fi
        done | sha256sum | cut -d' ' -f1)"

  # Every module this run declares must be one the foundation knows about.
  for m in "${MODULES[@]}"; do
    printf '%s\n' "${FOUND_MODULES[@]}" | grep -qxF -- "$m" \
      || die "module '$m' is not in the foundation's module list $FOUND_LIST"
  done
fi

EMIT_TIMEOUT="${EMIT_TIMEOUT:-1800}"
RUN_TIMEOUT="${RUN_TIMEOUT:-900}"

# ---------------------------------------------------------------------------
# PER-EMISSION MEMORY CEILING.
#
# `cva6_tlb` passed 32 GB RSS during emission on a shared NFS server and had to
# be killed by hand.  An unbounded emission worker is not acceptable for an
# authoritative run, and "the operator watched it" is not a harness property.
#
# ENFORCED WITH A CGROUP, NOT `ulimit -v`.  `ulimit -v` bounds ADDRESS SPACE,
# and it was measured here to be useless for this job: with a 4 GiB ceiling the
# tiny `null_vpu` block FAILED TO EMIT while its peak RSS was 82 MB.  The
# front end reserves far more address space than it ever faults in, so an
# address-space ceiling turns healthy blocks into resource-limit rows and does
# not measure the thing that actually went wrong.  `MemoryMax` bounds real
# memory, which is the quantity `cva6_tlb` blew.
#
# The scope wraps the EMISSION CHILD ONLY.  It deliberately does NOT reach the
# Lean runtime step: that process imports the whole library, legitimately needs
# several GB, and crippling it would turn a memory policy into fake runtime
# failures.
#
# If a ceiling is asked for and the mechanism is not usable here, the run FAILS.
# Silently proceeding unbounded is the outcome this exists to prevent.
# ---------------------------------------------------------------------------
EMIT_RSS_FLOOR_KB=1048576         # 1 GiB: below this every emission dies, so a
                                  # typo like `EMIT_MAX_RSS_KB=512` would turn
                                  # the whole corpus into resource-limit rows.
EMIT_SCOPE=()
if [[ -n "${EMIT_MAX_RSS_KB:-}" ]]; then
  [[ "$EMIT_MAX_RSS_KB" =~ ^[0-9]+$ ]] \
    || die "EMIT_MAX_RSS_KB must be a positive integer of KB, got '$EMIT_MAX_RSS_KB'"
  [[ "$EMIT_MAX_RSS_KB" -ge "$EMIT_RSS_FLOOR_KB" ]] \
    || die "EMIT_MAX_RSS_KB=$EMIT_MAX_RSS_KB is below the ${EMIT_RSS_FLOOR_KB} KB floor; a ceiling that kills every emission is not a measurement"
  command -v systemd-run >/dev/null 2>&1 \
    || die "EMIT_MAX_RSS_KB was requested but systemd-run is not available; refusing to run emission unbounded"
  # Prove the mechanism works HERE, once, rather than discovering per block that
  # it does not -- and prove it ENFORCES, not merely that it starts.
  systemd-run --user --scope --quiet --collect \
      -p "MemoryMax=${EMIT_MAX_RSS_KB}K" -p MemorySwapMax=0 true >/dev/null 2>&1 \
    || die "cannot start a user scope with MemoryMax=${EMIT_MAX_RSS_KB}K (systemd --user may be unavailable); refusing to run emission unbounded"
  # ENFORCEMENT is proved by the KERNEL COUNTERS, not by systemd's `Result`.
  # Measured on this host, five times out of five, a real cgroup OOM kill reports
  # `Result=success  ActiveState=active  Tasks=0  rc=137` with
  # `memory.events: oom=1 oom_kill=1`.  `Result` and `rc` are therefore recorded
  # as DIAGNOSTICS ONLY and never decide anything: a positive kernel counter is
  # the sole thing that can declare a limit hit, here and per block.
  _pu="d4emitprobe-$$"
  # PROJECT-LOCAL scratch, never /tmp: this worktree's rule is that temporary
  # files live under the project, and a shared /tmp is also where two concurrent
  # runs would collide.  Removed on EVERY exit path, including a die().
  # The DIRECTORY is unique, not just the files in it: two concurrent sweeps
  # share $ROOT/temp, and this one removes its scratch wholesale on exit.
  mkdir -p "$ROOT/temp" || die "cannot create $ROOT/temp"
  _pscratch="$(mktemp -d "$ROOT/temp/d4probe.XXXXXX")" || die "cannot create a preflight scratch dir under $ROOT/temp"
  _psnap="$_pscratch/probe"
  trap 'rm -rf "$_pscratch"' EXIT
  _pfinal="${_psnap}.final"
  scope_sample "$_pu" "$_psnap" & _psamp=$!
  systemd-run --user --scope --quiet --unit="$_pu" -p MemoryMax=64M -p MemorySwapMax=0 \
      "$SCRIPT_DIR/d4_emit_in_scope.sh" "$_pfinal" \
      python3 -c "
import time
chunks = []
# Grow in 8 MB steps with a short pause.  A single huge allocation can be
# refused or killed before any snapshot is taken, which would make the preflight
# fail for want of EVIDENCE rather than for want of ENFORCEMENT.  A real
# emission grows over seconds; the probe should too.
for _ in range(400):
    chunks.append(bytearray(8 * 1024 * 1024))
    time.sleep(0.005)
" >/dev/null 2>&1
  _prc=$?
  kill "$_psamp" 2>/dev/null; wait "$_psamp" 2>/dev/null
  _res="$(systemctl --user show "$_pu.scope" -p Result --value 2>/dev/null)"
  read -r _ok _ogk _pk _have _term _src < <(scope_merge "$_psnap" "$_pfinal" "$_pfinal.anchor")
  _clean="$(scope_cleanup "$_pu")"
  rm -rf "$_pscratch"
  trap - EXIT
  [[ "$_have" == "1" ]] \
    || die "cannot capture the probe scope's memory.events; the kernel verdict would be unavailable per block, so the ceiling cannot be trusted"
  _v="$(d4_scope_verdict "$_ok" "$_ogk" "$_prc" "$_res" "$_have" "$_term")"
  # Enforcement is proved by a POSITIVE KERNEL COUNTER and nothing else.
  # Measured here five times out of five, a real kill reports `Result=success`,
  # so demanding `Result=oom-kill` would REFUSE a cap that is in fact enforced;
  # and accepting rc=137 would accept a cap that was never exercised.  LIMIT_HIT
  # requires `oom_kill`/`oom_group_kill` > 0, so this check fails unless the
  # kernel actually counted a kill.
  [[ "$_v" == LIMIT_HIT* ]] \
    || die "the memory ceiling does not ENFORCE: a 512 MB allocation under MemoryMax=64M gave $_v (peak ${_pk} B, snapshots: ${_src:-none})"
  [[ "$_clean" == "clean" ]] \
    || die "the probe scope did not clean up: $_clean"
fi

STACK_KB="$(ulimit -s)"
[[ "$STACK_KB" == "8192" ]] || echo "NOTE: stack limit is ${STACK_KB} KB, not the ordinary 8192" >&2

mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"
# ABSOLUTE from here on: the runtime step runs from formal/lean, so a relative
# output path would silently write beside the Lean package instead.
OUTDIR="$(cd "$OUTDIR" && pwd)"
PARTS="$OUTDIR/rowparts"
mkdir -p "$PARTS"
ROWS="$OUTDIR/rows.tsv"
PROV="$OUTDIR/provenance.txt"

{
  echo "d4_head=$(cd "$ROOT" && git rev-parse HEAD)"
  echo "d4_head_subject=$(cd "$ROOT" && git log -1 --format=%s)"
  echo "d4_tracked_dirty=$(cd "$ROOT" && [ -z "$(git status --porcelain --untracked-files=no)" ] && echo no || echo YES)"
  echo "worktree=$ROOT"
  echo "lhd=$LHD"
  echo "lhd_sha256=$(sha256sum "$LHD" | cut -d' ' -f1)"
  echo "lhd_bytes=$(stat -c%s "$LHD")"
  # The build MODE, as the path records it -- `k8-opt` vs `k8-fastbuild` -- so a
  # row says which binary produced it without needing the digest resolved.
  echo "lhd_build_mode=$(sed -n 's|.*/bazel-out/\([^/]*\)/bin/.*|\1|p' <<<"$(readlink -f "$LHD")" | head -1)"
  echo "lake=$LAKE"
  echo "lake_version=$LAKE_VERSION"
  echo "lake_version_cwd=$ROOT/formal/lean"
  [[ -n "$LAKE_VERSION_PRELUDE" ]] && echo "lake_version_prelude=$LAKE_VERSION_PRELUDE"
  echo "corpus=$CORPUS"
  echo "module_list=$LIST"
  echo "module_list_sha256=$(sha256sum "$LIST" | cut -d' ' -f1)"
  echo "module_count=$N_DECLARED"
  [[ "$CORPUS" == "coreet" ]] && printf '%s\n' "$COREET_PROV"
  if [[ "$CORPUS" == "selftest" ]]; then
    echo "selftest_emit_cmd=$SELFTEST_EMIT_CMD"
    echo "selftest_emit_cmd_sha256=$(sha256sum < "$SELFTEST_EMIT_CMD" | cut -d' ' -f1)"
    echo "NOTE=selftest rows exercise the driver, not any design corpus"
  fi
  if [[ "$CORPUS" == "cva6" ]]; then
    echo "cva6_foundation=$CVA6_FOUNDATION"
    echo "cva6_foundation_sha256=$(sha256sum < "$CVA6_FOUNDATION" | cut -d' ' -f1)"
    echo "cva6_root=$CVA6_ROOT"
    echo "cva6_root_canonical=$(readlink -f "$CVA6_ROOT")"
    echo "cva6_root_rev=$NOW_REV"
    echo "cva6_tracked_clean=yes"
    echo "cva6_target=$CVA6_TARGET"
    echo "cva6_config_pkg=$CVA6_CONFIG_PKG"
    echo "cva6_config_pkg_sha256=$(sha256sum "$CVA6_CONFIG_PKG" | cut -d' ' -f1)"
    echo "cva6_filelist=$CVA6_FILELIST"
    echo "cva6_filelist_sha256=$(sha256sum "$CVA6_FILELIST" | cut -d' ' -f1)"
    echo "cva6_wrap_dir=$CVA6_WRAP_DIR"
    # Hash the wrappers THIS RUN will read, in declared order, so the wrapper set
    # is pinned as one value rather than a directory name.  The name says
    # "declared" because `generated/cva6_foundation/provenance.txt` carries a
    # DIFFERENT hash under a similar name -- that one is over all 78 slots.  Two
    # hashes with the same name and different scopes is a trap.
    # "<module> <digest>" pairs, NEVER `sha256sum <path>`: that folds the
    # directory into the value, so the same corpus under /soe and under /mada --
    # two mount paths for THIS worktree -- would hash differently.
    echo "cva6_wrappers_declared_sha256=$(for m in "${MODULES[@]}"; do
            w="$CVA6_WRAP_DIR/${m}_gate.sv"
            if [[ -r "$w" ]]; then echo "$m $(sha256sum < "$w" | cut -d' ' -f1)"; else echo "$m -"; fi
          done | sha256sum | cut -d' ' -f1)"
  fi
  echo "stack_limit_kb=$STACK_KB"
  echo "emit_timeout_s=$EMIT_TIMEOUT"
  echo "run_timeout_s=$RUN_TIMEOUT"
  # The EXACT ceiling, and what it bounds.  `unset` is recorded explicitly so a
  # run without one can never be mistaken for a run with one.
  echo "emit_max_rss_kb=${EMIT_MAX_RSS_KB:-unset}"
  echo "emit_limit_kind=$([[ -n "${EMIT_MAX_RSS_KB:-}" ]] && echo "cgroup MemoryMax via systemd-run --user --scope, emission child only" || echo none)"
  echo "emit_limit_verdict_source=$([[ -n "${EMIT_MAX_RSS_KB:-}" ]] && echo "cgroup memory.events oom_kill/oom_group_kill ONLY; systemd Result and rc are recorded as diagnostics and never decide a limit hit" || echo none)"
  echo "jobs_requested=$JOBS"
  echo "execution=$([[ "$JOBS" -eq 1 ]] && echo serial || echo "bounded-concurrent-${JOBS}")"
  echo "row_fields=$ROW_FIELDS"
  echo "started=$(date -Is)"
} > "$PROV"

# ---------------------------------------------------------------------------
# One block.  Fresh, isolated output directory; the certificate must be NEWLY
# emitted; the row goes to its OWN file, so concurrent workers never interleave
# in one TSV and the merge below is by DECLARED order, not completion order.
# ---------------------------------------------------------------------------
one() {
  local m="$1"
  local part="$PARTS/$m.row"
  local work="$OUTDIR/blocks/$m"
  rm -rf "$work"; mkdir -p "$work"
  local t_start; t_start=$(date +%s.%N)
  local stamp="$work/.start"; touch "$stamp"
  local wall

  local emit_rc=0
  local etf="$work/emit.time"
  # The ceiling lives in THIS subshell and dies with it; the Lean runtime step
  # below runs in a different one and is untouched.
  # A NAMED, uncollected scope, so the kernel's counters can be read back.
  # NEITHER `Result` NOR THE EXIT STATUS DECIDES ANYTHING: a real kill was
  # measured here reporting `Result=success` with `rc=137`, and `rc` alone only
  # says the process was killed, not that MemoryMax killed it.  Only a positive
  # `oom_kill`/`oom_group_kill` establishes LIMIT_HIT; both of the others are
  # recorded as diagnostics.
  local scope_unit="d4emit-${m}-$$"
  local snap_live="$work/emit.cgroup.live" snap_final="$work/emit.cgroup.final"
  emit_guarded() {
    if [[ -n "${EMIT_MAX_RSS_KB:-}" ]]; then
      systemd-run --user --scope --quiet --unit="$scope_unit" \
        -p "MemoryMax=${EMIT_MAX_RSS_KB}K" -p MemorySwapMax=0 \
        /usr/bin/time -v -o "$etf" "$SCRIPT_DIR/d4_emit_in_scope.sh" "$snap_final" "$@"
    else
      /usr/bin/time -v -o "$etf" "$@"
    fi
  }
  local samp_pid=""
  if [[ -n "${EMIT_MAX_RSS_KB:-}" ]]; then
    scope_sample "$scope_unit" "$snap_live" & samp_pid=$!
  fi
  if [[ "$CORPUS" == "selftest" ]]; then
    emit_guarded timeout "$EMIT_TIMEOUT" "$SELFTEST_EMIT_CMD" "$work" "$m" \
      > "$work/emit.log" 2>&1
    emit_rc=$?
  elif [[ "$CORPUS" == "coreet" ]]; then
    COREET_ROOT="$COREET_ROOT" COREET_TECH="$COREET_TECH" COREET_TOP="$m" LEAN_MODE=verified_compiler \
      RUN_LEAN=false RUN_LEC_GATE=false OUT="$work" \
      emit_guarded timeout "$EMIT_TIMEOUT" bash "$SCRIPT_DIR/run_coreet_module_lean.sh" \
      > "$work/emit.log" 2>&1
    emit_rc=$?
  else
    local wrap="$CVA6_WRAP_DIR/${m}_gate.sv"
    if [[ ! -r "$wrap" ]]; then
      wall=$(echo "$(date +%s.%N) - $t_start" | bc)
      row "$CORPUS" "$m" NO_WRAPPER - - - - - - NOT_EMITTED - - NOT_RUN - - \
          "no wrapper at $wrap" "$wall" - > "$part"
      return
    fi
    CVA6_TOP="${m}_gate" CVA6_FILELIST="$CVA6_FILELIST" CVA6_WRAPPER_FILE="$wrap" \
      CVA6_ROOT="$CVA6_ROOT" CVA6_TARGET="$CVA6_TARGET" \
      LEAN_MODE=verified_compiler RUN_LEAN=false OUT="$work" \
      emit_guarded timeout "$EMIT_TIMEOUT" bash "$SCRIPT_DIR/run_cva6_module_lean_stress.sh" \
      > "$work/emit.log" 2>&1
    emit_rc=$?
  fi
  local emit_rss; emit_rss="$(grep -oP 'Maximum resident set size \(kbytes\): \K[0-9]+' "$etf" 2>/dev/null)"

  # ---------------------------------------------------------------------
  # THE KERNEL'S VERDICT, READ BEFORE CLEANUP AND BEFORE ANYTHING ELSE.
  #
  # `systemctl stop` destroys the cgroup, and with it `memory.events` -- the
  # only authoritative record of whether the ceiling fired.  It is therefore
  # read first, persisted, and only then is the scope torn down.
  # ---------------------------------------------------------------------
  local scope_result="" scope_verdict="OK no ceiling" scope_clean="n/a"
  local c_ok=0 c_ogk=0 c_peak=0 c_have=0 c_term=0 c_src=""
  if [[ -n "${EMIT_MAX_RSS_KB:-}" ]]; then
    [[ -n "$samp_pid" ]] && { kill "$samp_pid" 2>/dev/null; wait "$samp_pid" 2>/dev/null; }
    scope_result="$(systemctl --user show "$scope_unit.scope" -p Result --value 2>/dev/null)"
    scope_result="${scope_result//[[:space:]]/_}"
    read -r c_ok c_ogk c_peak c_have c_term c_src < <(scope_merge "$snap_live" "$snap_final" "$snap_final.anchor")
    scope_verdict="$(d4_scope_verdict "$c_ok" "$c_ogk" "$emit_rc" "$scope_result" "$c_have" "$c_term")"
    # EVIDENCE IS PERSISTED BEFORE THE SCOPE IS TORN DOWN.
    {
      echo "unit=$scope_unit.scope"
      echo "counters_readable=$c_have"
      echo "terminal_snapshot=$c_term"
      echo "snapshot_sources=${c_src:-none}"
      echo "oom_kill=$c_ok"
      echo "oom_group_kill=$c_ogk"
      echo "memory_peak_bytes=$c_peak"
      echo "memory_max_kb=$EMIT_MAX_RSS_KB"
      echo "systemd_result=${scope_result:-unknown}"
      echo "emit_rc=$emit_rc"
      echo "emit_peak_rss_kb=${emit_rss:-unknown}"
      echo "verdict=$scope_verdict"
    } > "$work/emit.cgroup"
    scope_clean="$(scope_cleanup "$scope_unit")"
    echo "cleanup=$scope_clean" >> "$work/emit.cgroup"
  fi

  # A RECORDED LIMIT HIT IS DECIDED BEFORE THE CERTIFICATE IS EVEN LOOKED AT.
  # The front end can write a `.dcert` and be killed a moment later; running
  # that file would publish EMITTED/RAN for a block the ceiling stopped.  The
  # certificate is left on disk as evidence and is NOT used.
  # NO AUTHORITATIVE EVIDENCE IS NOT "NO KILL".  An unreadable or malformed
  # counter set, or a scope that would not clean up, means this block cannot be
  # certified either way -- so it is NOT run, NOT counted as coverage, and NOT
  # reported as an OOM that was never observed.  A certificate on disk is kept
  # as evidence and left unused.
  if [[ -n "${EMIT_MAX_RSS_KB:-}" ]] \
     && { [[ "$scope_verdict" == UNKNOWN* ]] || [[ "$scope_clean" != "clean" ]]; }; then
    local partial_u; partial_u="$(find "$work" -name '*.dcert' -newer "$stamp" -size +0c 2>/dev/null | head -1)"
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$CORPUS" "$m" NO_VERDICT - - - - - - NOT_EMITTED - - NOT_RUN - - \
        "resource-evidence-unavailable: ${scope_verdict#UNKNOWN } | cleanup=$scope_clean, MemoryMax ${EMIT_MAX_RSS_KB} KB${emit_rss:+, peak RSS ${emit_rss} KB}${partial_u:+, certificate written and NOT run}" \
        "$wall" - > "$part"
    return
  fi

  if [[ "$scope_verdict" == LIMIT_HIT* ]]; then
    local partial; partial="$(find "$work" -name '*.dcert' -newer "$stamp" -size +0c 2>/dev/null | head -1)"
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$CORPUS" "$m" NO_CERT - - - - - - NOT_EMITTED - - NOT_RUN - - \
        "resource-limit: ${scope_verdict#LIMIT_HIT } | MemoryMax ${EMIT_MAX_RSS_KB} KB, peak ${c_peak} B${emit_rss:+, peak RSS ${emit_rss} KB}${partial:+, partial certificate written and NOT run}, cleanup=$scope_clean" \
        "$wall" - > "$part"
    return
  fi

  # ---------------------------------------------------------------------
  # A NONZERO EMITTER RESULT IS A FAILURE, EVEN WHEN A CERTIFICATE EXISTS.
  #
  # This ran AFTER certificate discovery, so a `.dcert` on disk overrode the
  # emitter's own verdict.  `cva6_hpdcache_subsystem` returned rc=3 -- its
  # helper exited on a failed static gate -- and was published as ordinary
  # EMITTED/RAN.  The same path would promote a certificate written just before
  # any later emitter failure or timeout.
  #
  # Clean cgroup counters prove only that MemoryMax did not kill the emission;
  # they say nothing about whether the emitter SUCCEEDED.  The kernel verdict is
  # therefore taken first (above), and the emitter's own result here, both
  # BEFORE the certificate is looked for.
  #
  # A written certificate is kept on disk as diagnostic evidence and is never
  # executed and never counted.
  # ---------------------------------------------------------------------
  if [[ "$emit_rc" -ne 0 ]]; then
    local partial_e; partial_e="$(find "$work" -name '*.dcert' -newer "$stamp" -size +0c 2>/dev/null | head -1)"
    local ewhy
    if [[ "$emit_rc" -eq 124 ]]; then
      ewhy="emitter-failed: emission timeout ${EMIT_TIMEOUT}s (rc=124)"
    else
      ewhy="emitter-failed: the emitter returned rc=$emit_rc"
    fi
    local elast; elast="$(grep -m1 -oP '"message":"\K[^"]{0,120}' "$work"/logs/*result.json 2>/dev/null | head -1)"
    [[ -z "$elast" ]] && elast="$(grep -vE '^/bin/(ba)?sh: |Terminated +|Killed +' "$work/emit.log" 2>/dev/null \
                                  | tail -1 | tr '\t\n' '  ' | cut -c1-120)"
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$CORPUS" "$m" EMIT_FAILED - - - - - - NOT_EMITTED - - NOT_RUN - - \
        "$ewhy${partial_e:+ | certificate written and NOT run}${elast:+ | $elast}" \
        "$wall" "${emit_rss:--}" > "$part"
    return
  fi

  # A certificate that is absent, empty, or older than this run is NOT a result.
  local cert; cert="$(find "$work" -name '*.dcert' -newer "$stamp" -size +0c 2>/dev/null | head -1)"
  [[ -n "$cert" ]] && cert="$(cd "$(dirname "$cert")" && pwd)/$(basename "$cert")"
  if [[ -z "$cert" ]]; then
    local why
    why="$(grep -m1 -oP '"message":"\K[^"]{0,120}' "$work"/logs/*result.json 2>/dev/null | head -1)"
    # rc=124 and every other nonzero rc already returned above as EMIT_FAILED;
    # anything here had rc=0 and simply produced nothing.
    # The last line of emit.log can be the SHELL's job-control notice for a
    # killed child, which quotes the harness's own command text.  That is noise
    # about us, not a diagnosis of the block, so it is filtered out.
    [[ -z "$why" ]] && why="$(grep -vE '^/bin/(ba)?sh: |Terminated +|Killed +' "$work/emit.log" 2>/dev/null \
                              | tail -1 | tr '\t\n' '  ' | cut -c1-120)"
    # A CEILING HIT IS ITS OWN OUTCOME.  Reported as a well-formed NO_CERT row
    # with a greppable `resource-limit:` prefix, so it is counted apart from an
    # ordinary front-end failure and can never read as runtime coverage.
    # A ceiling hit has already returned above, on the KERNEL's verdict.  What
    # is left here is an ordinary front-end failure -- except for the degraded
    # case where the counters could not be read at all, which is recorded rather
    # than quietly treated as a clean miss.
    # Unreadable counters already returned above, as NO_VERDICT, whether or not
    # a certificate exists.  Anything reaching here has an authoritative "no
    # kill" and is an ordinary front-end failure.
    wall=$(echo "$(date +%s.%N) - $t_start" | bc)
    row "$CORPUS" "$m" NO_CERT - - - - - - NOT_EMITTED - - NOT_RUN - - \
        "${why:-emission produced no .dcert (rc=$emit_rc)}" "$wall" - > "$part"
    return
  fi

  local sha; sha="$(sha256sum "$cert" | cut -d' ' -f1)"
  local tf="$work/run.time"
  # Ordinary stack: deliberately NOT raised.  slotsFrom is tail-recursive now.
  # THE CHILD'S EXIT STATUS, CAPTURED AND REQUIRED.
  #
  # Two defects, both reproduced against the actual CLI:
  #
  #  1. The captured status was IGNORED. (It was not wrong: this script sets
  #     `-o pipefail`, so `out="$(... | tail -1)"` does carry a failing `lake`
  #     through. My first description said `tail` made the status always zero
  #     -- false with pipefail set, and the status was there to be used.) The
  #     acceptance test simply never consulted it.
  #  2. So a row was admitted on SHAPE alone: a child printing a correct
  #     11-field PARSED/true/RAN row and then exiting 17 was published with its
  #     digest.
  #
  # The acceptance test now requires the status; the pipe is gone anyway, so
  # the guard does not depend on `pipefail` staying set. A nonzero or timed-out
  # child cannot contribute an accepted runtime
  # result however well-formed its row. The logs stay, an 18-column failure row
  # is emitted with NO accepted digest.
  #
  # The sweep's own distinction is preserved: `reconciled=YES` is about row
  # SHAPE and COMPLETENESS, and may still accompany refused or failed modules.
  # Nothing here touches the checker or the resource verdict.
  local out
  ( cd "$ROOT/formal/lean" && timeout "$RUN_TIMEOUT" \
      /usr/bin/time -v -o "$tf" \
      "$LAKE" env lean --run "$ROW_DRIVER" "$cert" \
      > "$work/run.out" 2>"$work/run.err" )
  local run_rc=$?
  out="$(tail -1 "$work/run.out" 2>/dev/null)"
  local peak; peak="$(grep -oP 'Maximum resident set size \(kbytes\): \K[0-9]+' "$tf" 2>/dev/null)"
  wall=$(echo "$(date +%s.%N) - $t_start" | bc)

  # The driver's 11 fields: sources nodes flops memories parse_status parse_ms
  # compiles_ok runtime_status run_ms digest diagnostic.
  local nf; nf="$(awk -F'\t' '{print NF}' <<<"$out" 2>/dev/null)"
  if [[ "$run_rc" -ne 0 || -z "$out" || "$nf" != "11" ]]; then
    local why="runtime produced no usable row (rc=$run_rc, fields=${nf:-0})"
    [[ "$run_rc" -ne 0 && "$nf" == "11" ]] && \
      why="runtime exited $run_rc after printing a complete row; the row is NOT accepted"
    # A timeout is named as one even when a complete row arrived first: the
    # cause is the limit, not an ordinary failure.
    [[ "$run_rc" -eq 124 ]] && why="runtime timeout ${RUN_TIMEOUT}s; any row already printed is NOT accepted"
    row "$CORPUS" "$m" EMITTED "$cert" "$sha" - - - - NO_ROW - - NOT_RUN - - \
        "$why" "$wall" "${peak:--}" > "$part"
    return
  fi
  local f_src f_nod f_fl f_mem f_ps f_pms f_ok f_rs f_rms f_dig f_diag
  IFS=$'\t' read -r f_src f_nod f_fl f_mem f_ps f_pms f_ok f_rs f_rms f_dig f_diag <<<"$out"
  row "$CORPUS" "$m" EMITTED "$cert" "$sha" "$f_src" "$f_nod" "$f_fl" "$f_mem" \
      "$f_ps" "$f_pms" "$f_ok" "$f_rs" "$f_rms" "$f_dig" "$f_diag" "$wall" "${peak:--}" > "$part"
}

rm -f "$PARTS"/*.row
if [[ "$JOBS" -eq 1 ]]; then
  for m in "${MODULES[@]}"; do one "$m"; done
else
  running=0
  for m in "${MODULES[@]}"; do
    one "$m" &
    running=$((running + 1))
    if [[ "$running" -ge "$JOBS" ]]; then
      wait -n
      running=$((running - 1))
    fi
  done
  wait
fi

# ---------------------------------------------------------------------------
# Deterministic merge in DECLARED order, then reconcile.  A missing row, a wrong
# name, or a row of the wrong SHAPE is a failure of the sweep, never a pass.
# ---------------------------------------------------------------------------
printf '%s\n' "$HEADER" > "$ROWS"
missing_parts=()
for m in "${MODULES[@]}"; do
  if [[ -s "$PARTS/$m.row" ]]; then
    cat "$PARTS/$m.row" >> "$ROWS"
  else
    missing_parts+=("$m")
  fi
done

N_ROWS="$(tail -n +2 "$ROWS" | wc -l)"
BAD_SHAPE="$(tail -n +2 "$ROWS" | awk -F'\t' -v want="$ROW_FIELDS" 'NF != want {print NR": "NF" fields"}')"
{
  echo "finished=$(date -Is)"
  echo "rows=$N_ROWS"
  echo "declared=$N_DECLARED"
} >> "$PROV"

fail=0
if [[ "${#missing_parts[@]}" -gt 0 ]]; then
  echo "FATAL: no row produced for: ${missing_parts[*]}" >&2
  fail=1
fi
if [[ -n "$BAD_SHAPE" ]]; then
  echo "FATAL: rows with the wrong field count (want $ROW_FIELDS):" >&2
  echo "$BAD_SHAPE" >&2
  fail=1
fi
if [[ "$N_ROWS" -ne "$N_DECLARED" ]]; then
  echo "FATAL: $N_ROWS rows for $N_DECLARED declared modules" >&2
  fail=1
fi
missing="$(comm -23 <(printf '%s\n' "${MODULES[@]}" | sort) <(tail -n +2 "$ROWS" | cut -f2 | sort))"
extra="$(comm -13 <(printf '%s\n' "${MODULES[@]}" | sort) <(tail -n +2 "$ROWS" | cut -f2 | sort))"
if [[ -n "$missing" || -n "$extra" ]]; then
  echo "FATAL: row/module mismatch; missing=[$missing] extra=[$extra]" >&2
  fail=1
fi
if [[ "$fail" -ne 0 ]]; then
  echo "reconciled=NO" >> "$PROV"
  echo "FATAL: refusing to report coverage" >&2
  exit 3
fi

# `reconciled=YES` means the SHAPE is sound -- every declared module produced
# exactly one well-formed row.  It says NOTHING about whether those rows ran;
# the per-stage classification is what carries that.
echo "reconciled=YES" >> "$PROV"
echo "wrote $ROWS ($N_ROWS rows) and $PROV"

#!/usr/bin/env bash
# Controls for the per-emission MEMORY CEILING in run_d4_runtime_sweep.sh.
#
# Why it exists: `cva6_tlb` reached 32 GB RSS during emission on a shared NFS
# server and had to be killed by hand.  An authoritative run must not depend on
# someone watching it.
#
# Why a cgroup and not `ulimit -v`: measured here, a 4 GiB ADDRESS-SPACE ceiling
# made the tiny `null_vpu` block fail to emit while its peak RSS was 82 MB.  The
# front end reserves far more address space than it faults in, so an
# address-space ceiling manufactures resource-limit rows for healthy blocks.
#
#   1  invalid ceilings (non-numeric, zero, below the floor)  FATAL, no rows
#   2  the ceiling is confined to EMISSION                    block still RANs
#                                                             with a Lean peak
#                                                             far above it
#   3  a real over-limit block                                honest 18-field
#                                                             NO_CERT row with a
#                                                             resource-limit: tag
#   4  no ceiling                                             unchanged, recorded
#                                                             as `unset`
#   5  structural                                             the scope wraps the
#                                                             emission call only
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
SWEEP="$ROOT/scripts/run_d4_runtime_sweep.sh"
FL="$ROOT/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.flistplus.f"
WRAPDIR="$ROOT/generated/cva6_wrappers"
REAL_ROOT="${CVA6_ROOT:-/soe/czeng14/projects/cva6-blockfinding}"

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/ceil_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
printf 'null_vpu\n'  > "$TMP/small.list"
printf 'cva6_tlb\n'  > "$TMP/heavy.list"

# A corpus-independent positive emission.
#
# `null_vpu` was used here and it is DEGENERATE: its certificate has no nodes,
# so the emitter's own static gates reject it ("no nodes emitted") and it has
# always exited 3.  Until the driver started failing closed on a nonzero emitter
# rc, that block was published as EMITTED/RAN -- so it was never a valid
# positive control.  These controls test the DRIVER, so the emission is a named
# helper writing a real certificate, with no design corpus involved.
LAKE="${LAKE:-$HOME/.elan/bin/lake}"
[[ -x "$LAKE" ]] || LAKE="$(command -v lake 2>/dev/null || true)"
TINY="$TMP/tiny.dcert"
if [[ -x "$LAKE" ]]; then
  ( cd "$ROOT/formal/lean" && "$LAKE" env lean --run probes/d4_write_tiny_cert.lean "$TINY" ) >/dev/null 2>&1
fi
cat > "$TMP/emit_ok.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert" && echo "wrote a certificate"
# A real emission takes seconds; this one must stay observable long enough for
# the evidence sampler to take a snapshot.
sleep 1
EOF
chmod +x "$TMP/emit_ok.sh"
run_self() {  # run_self <helper> <module> <outdir> [extra env...]
  printf '%s\n' "$2" > "$TMP/one_$2.list"
  ( cd "$ROOT" && env "SELFTEST_EMIT_CMD=$1" "${@:4}" \
      "$SWEEP" selftest "$TMP/one_$2.list" "$3" ) >"$3.log" 2>&1
  echo $?
}

field() { grep -m1 "^$2=" "$1/provenance.txt" 2>/dev/null | cut -d= -f2-; }
col()   { awk -F'\t' -v c="$2" 'NR==2{print $c}' "$1/rows.tsv" 2>/dev/null; }
nf()    { awk -F'\t' 'NR==2{print NF}' "$1/rows.tsv" 2>/dev/null; }

# --- 1. invalid ceilings ----------------------------------------------------
declare -A BADCEIL=(
  ["non-numeric"]="4g"
  ["zero"]="0"
  ["below the 1 GiB floor"]="524288"
)
for label in "${!BADCEIL[@]}"; do
  out="$TMP/bad_${BADCEIL[$label]}"
  ( cd "$ROOT" && env "EMIT_MAX_RSS_KB=${BADCEIL[$label]}" \
      "$SWEEP" coreet "$TMP/small.list" "$out" ) >"$out.log" 2>&1
  rc=$?
  if [[ "$rc" -eq 0 ]]; then
    bad "an invalid ceiling ($label) exited 0"
  elif ! grep -q 'EMIT_MAX_RSS_KB' "$out.log"; then
    bad "invalid ceiling ($label) failed for the wrong reason: $(head -1 "$out.log")"
  elif [[ -s "$out/rows.tsv" ]]; then
    bad "invalid ceiling ($label) still produced rows"
  else
    ok "invalid ceiling ($label): FATAL before any block, no rows"
  fi
done

# --- 2. the ceiling is confined to the emission child -----------------------
# 2 GiB is ample for null_vpu's emission (~120 MB peak) and FAR below what the
# Lean runtime needs (~6 GB, nearly all of it importing the library).  If the
# ceiling leaked into the runtime step the row would be NO_ROW, not RAN.
out="$TMP/confined"
rc="$(run_self "$TMP/emit_ok.sh" selftest_confined "$out" "EMIT_MAX_RSS_KB=2097152")"
lean_peak="$(col "$out" 18)"
if [[ "$rc" -ne 0 ]]; then
  bad "confinement control failed: $(tail -1 "$out.log")"
elif [[ "$(col "$out" 13)" != "RAN" ]]; then
  bad "with a 2 GiB emission ceiling the block did not run: $(col "$out" 13) / $(col "$out" 16)"
elif [[ -z "$lean_peak" || "$lean_peak" -le 2097152 ]]; then
  bad "the Lean peak ($lean_peak KB) is not above the ceiling, so this control proves nothing"
elif [[ "$(field "$out" emit_max_rss_kb)" != "2097152" ]]; then
  bad "provenance did not record the exact ceiling"
else
  ok "ceiling confined to emission: block RAN with a Lean peak of ${lean_peak} KB under a 2097152 KB emission ceiling"
fi

# --- 3. a real over-limit block becomes an honest row -----------------------
if [[ ! -s "$FL" || ! -d "$WRAPDIR" ]]; then
  bad "no CVA6 foundation; cannot run the over-limit control"
else
  out="$TMP/overlimit"
  ( cd "$ROOT" && env "CVA6_ROOT=$REAL_ROOT" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR" \
      "EMIT_MAX_RSS_KB=2097152" "EMIT_TIMEOUT=900" \
      "$SWEEP" cva6 "$TMP/heavy.list" "$out" ) >"$out.log" 2>&1
  rc=$?
  diag="$(col "$out" 16)"
  if [[ "$rc" -ne 0 ]]; then
    bad "over-limit control: the sweep itself failed: $(tail -1 "$out.log")"
  elif [[ "$(nf "$out")" != "18" ]]; then
    bad "over-limit row has $(nf "$out") fields, not 18"
  elif [[ "$(col "$out" 3)" != "NO_CERT" ]]; then
    bad "over-limit row emit_status is $(col "$out" 3), not NO_CERT"
  elif [[ "$diag" != resource-limit:* ]]; then
    bad "over-limit row is not tagged resource-limit: '$diag'"
  elif [[ "$(col "$out" 13)" != "NOT_RUN" || "$(col "$out" 15)" != "-" ]]; then
    bad "over-limit row claims runtime output: status=$(col "$out" 13) digest=$(col "$out" 15)"
  elif ! grep -q 'reconciled=YES' "$out/provenance.txt"; then
    bad "over-limit row broke the shape contract"
  else
    ok "over-limit block: one 18-field NO_CERT row, $(cut -c1-58 <<<"$diag")..."
  fi
fi

# --- 4. no ceiling: unchanged, and recorded as unset ------------------------
out="$TMP/noceil"
printf '%s\n' selftest_noceil > "$TMP/one_selftest_noceil.list"
( cd "$ROOT" && env -u EMIT_MAX_RSS_KB "SELFTEST_EMIT_CMD=$TMP/emit_ok.sh" \
    "$SWEEP" selftest "$TMP/one_selftest_noceil.list" "$out" ) >"$out.log" 2>&1
rc=$?
if [[ "$rc" -ne 0 ]]; then
  bad "the no-ceiling path regressed: $(tail -1 "$out.log")"
elif [[ "$(col "$out" 13)" != "RAN" ]]; then
  bad "no-ceiling run did not produce a RAN row"
elif [[ "$(field "$out" emit_max_rss_kb)" != "unset" ]]; then
  bad "a run with no ceiling recorded emit_max_rss_kb='$(field "$out" emit_max_rss_kb)'"
elif [[ "$(field "$out" emit_limit_kind)" != "none" ]]; then
  bad "a run with no ceiling recorded a limit kind"
else
  ok "no ceiling: behaviour unchanged, recorded as unset / none"
fi

# --- 5. structural: the scope wraps the EMISSION call only ------------------
# Comment lines explain WHY ulimit -v was rejected, so only CODE is inspected.
if grep -vE '^[[:space:]]*#' "$SWEEP" | grep -q 'ulimit -v'; then
  bad "the driver still uses ulimit -v in code, which bounds address space, not memory"
elif [[ "$(grep -c 'systemd-run --user --scope' "$SWEEP")" -lt 1 ]]; then
  bad "no cgroup scope in the driver"
elif grep -n 'lake. env lean --run' "$SWEEP" | grep -q 'systemd-run'; then
  bad "the Lean runtime invocation is inside a memory scope"
elif ! grep -q 'emit_guarded timeout' "$SWEEP"; then
  bad "the emission call is not routed through emit_guarded"
else
  ok "structural: scope on the emission call only, Lean runtime untouched"
fi


# --- 6. the VERDICT itself, on synthetic inputs ----------------------------
# The shape that matters cannot be produced on demand, so it is injected.
# Measured on this host five times out of five, a real cgroup OOM kill reports
# rc=137 with Result=success and memory.events oom_kill=1 -- while an earlier
# cva6_tlb run reported rc=143 with Result=oom-kill.  A harness that believes
# `Result` both misses real kills and refuses enforced caps.  ONLY A POSITIVE
# KERNEL COUNTER ESTABLISHES A LIMIT HIT; `Result` and `rc` are diagnostics.
# shellcheck source=../../../../scripts/d4_scope_verdict.sh
. "$ROOT/scripts/d4_scope_verdict.sh"
vfails=0
vcase() {  # vcase <want> <ok> <ogk> <rc> <result> <have> <term> [why]
  local want="$1"; shift
  local why="${7:-}"
  local got; got="$(d4_scope_verdict "$1" "$2" "$3" "$4" "$5" "$6")"
  if [[ "${got%% *}" != "$want" ]]; then
    bad "verdict($1,$2,$3,$4,have=$5,term=$6) = ${got%% *}, want $want ${why:+-- $why}"
    vfails=$((vfails + 1))
  fi
}
# ONLY A POSITIVE KERNEL COUNTER MAKES A LIMIT HIT.
vcase LIMIT_HIT 1 0 137 success  1 1 "counters positive, Result=success -- the measured real shape"
vcase LIMIT_HIT 1 0 137 success  1 0 "positive counters are monotone; no terminal reading needed"
vcase LIMIT_HIT 0 1 143 success  1 1 "oom_group_kill counts too"
# rc AND Result MUST NOT CREATE A VERDICT ON THEIR OWN.
vcase UNKNOWN   0 0 137 success  0 0 "SIGKILL without a counter is not an OOM"
vcase UNKNOWN   0 0 0   oom-kill 0 0 "Result alone is not an OOM"
vcase UNKNOWN   0 0 143 oom-kill 1 1 "Result disagrees with terminal zero counters"
# MISSING, MALFORMED AND ENTRY-ONLY EVIDENCE.
vcase UNKNOWN   0 0 0   success  0 0 "unreadable counters must not read as OK"
vcase UNKNOWN   x 0 0   success  1 1 "a malformed counter must not normalise to zero"
vcase UNKNOWN   0 bogus 0 success 1 1 "likewise for oom_group_kill"
vcase UNKNOWN   "" "" 0 success  1 1 "empty counters are not zero"
vcase UNKNOWN   0 0 0   success  1 0 "an ENTRY-ONLY zero says nothing about later"
# A TERMINAL ZERO IS AUTHORITATIVE.
vcase OK        0 0 0   success  1 1 "post-command zero is evidence of no memory kill"
vcase OK        0 0 124 success  1 1 "a TIMEOUT stays its own outcome"
vcase OK        0 0 137 success  1 1 "terminal zero: the SIGKILL came from elsewhere"
[[ "$vfails" -eq 0 ]] && ok "verdict table: 14 cases -- only positive counters make LIMIT_HIT; rc/Result never do; entry-only and missing/malformed evidence are UNKNOWN"

# --- 6b. the MERGE must not invent evidence --------------------------------
mfails=0
mk_snap() { printf '%s\n' "$@"; }
merge_of() {  # merge_of <file-body>...  -> "<ok> <ogk> <peak> <have> <term> <srcs>"
  local d; d="$(mktemp -d "$TMP/merge.XXXXXX")"; local i=0 f files=()
  for body in "$@"; do i=$((i+1)); f="$d/s$i"; printf '%s\n' "$body" > "$f"; files+=("$f"); done
  [[ ${#files[@]} -gt 0 ]] || files=("$d/absent")
  scope_merge "${files[@]}"
}
mexpect() {  # mexpect <label> <want-have> <want-term> <want-ok> <body>...
  local label="$1" wh="$2" wt="$3" wk="$4"; shift 4
  read -r gk _gg _gp gh gt _gs < <(merge_of "$@")
  if [[ "$gh" != "$wh" || "$gt" != "$wt" || "$gk" != "$wk" ]]; then
    bad "merge($label): have=$gh term=$gt oom_kill=$gk, want have=$wh term=$wt oom_kill=$wk"
    mfails=$((mfails + 1))
  fi
}
mexpect "no files at all"       0 0 0
mexpect "malformed counters"    0 0 0 $'source=in-scope\nphase=exit\noom_kill=NaN\noom_group_kill=0'
mexpect "counters absent"       0 0 0 $'source=sampler\nphase=sample\ncgroup=/x'
mexpect "ENTRY-ONLY zero"       1 0 0 $'source=in-scope\nphase=entry\noom_kill=0\noom_group_kill=0'
mexpect "sampler-only zero"     1 0 0 $'source=in-scope\nphase=sample\noom_kill=0\noom_group_kill=0'
mexpect "terminal zero"         1 1 0 $'source=in-scope\nphase=exit\noom_kill=0\noom_group_kill=0'
mexpect "entry zero + sample kill" 1 0 1 \
  $'source=in-scope\nphase=entry\noom_kill=0\noom_group_kill=0' \
  $'source=in-scope\nphase=sample\noom_kill=1\noom_group_kill=0'
mexpect "kill then a later clean read" 1 1 1 \
  $'source=in-scope\nphase=sample\noom_kill=1\noom_group_kill=0' \
  $'source=in-scope\nphase=exit\noom_kill=0\noom_group_kill=0'
[[ "$mfails" -eq 0 ]] && ok "merge: malformed/absent counters never become zero; entry and sample readings never count as terminal; a kill survives a later clean read"

# --- 7. a certificate written BEFORE the kill must not be run --------------
# The fail-open branch this closes: resource-limit classification used to sit
# inside `if no certificate`, so a .dcert written moments before the OOM would
# have been handed to Lean and published as EMITTED / RAN.
if [[ ! -s "$TINY" ]]; then
  bad "could not build a tiny certificate; the ordering controls cannot run"
else
  printf 'selftest_ok\nselftest_oom\nselftest_slow\n' > "$TMP/self.list"
  # Writes a certificate and then DESTROYS the evidence channel: both snapshot
  # paths are replaced by DIRECTORIES, so the in-scope recorder and the sampler
  # can no longer publish a snapshot.  This is the condition the follow-up
  # review asked for -- a fresh certificate with no authoritative counters.
  cat > "$TMP/emit_cert_no_evidence.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert"
rm -rf "\$1/emit.cgroup.live" "\$1/emit.cgroup.final" "\$1/emit.cgroup.final.anchor"
mkdir -p "\$1/emit.cgroup.live" "\$1/emit.cgroup.final" "\$1/emit.cgroup.final.anchor"
echo "certificate written; evidence channel blocked"
sleep 1
EOF
  # Plants a certificate, destroys every snapshot channel, and kills ITSELF with
  # SIGKILL WITHOUT allocating against MemoryMax.  rc=137 and a stopped scope,
  # but the kernel never counted a memory kill.
  cat > "$TMP/emit_cert_sigkill.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert"
rm -rf "\$1/emit.cgroup.live" "\$1/emit.cgroup.final" "\$1/emit.cgroup.final.anchor"
mkdir -p "\$1/emit.cgroup.live" "\$1/emit.cgroup.final" "\$1/emit.cgroup.final.anchor"
echo "certificate written, channels blocked, SIGKILLing self without allocating"
kill -9 \$\$
EOF
  cat > "$TMP/emit_cert_then_oom.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert"
echo "certificate written; now exceeding the ceiling"
python3 -c "b=bytearray(3*1024*1024*1024)"
EOF
  cat > "$TMP/emit_slow.sh" <<'EOF'
#!/usr/bin/env bash
echo "sleeping past the emission timeout"
sleep 120
EOF
  # Writes a VALID certificate, leaves the evidence channels alone so the
  # terminal counters are clean, and then exits NONZERO -- the exact shape in
  # which `cva6_hpdcache_subsystem` was published as EMITTED/RAN despite its
  # emitter failing a static gate and returning 3.
  cat > "$TMP/emit_cert_rc3.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert"
echo "certificate written; emitter now failing with rc=3"
sleep 1
exit 3
EOF
  # Writes a VALID certificate and then runs past the emission timeout.
  cat > "$TMP/emit_cert_slow.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$TINY" "\$1/lean/\$2.dcert"
echo "certificate written; now sleeping past the emission timeout"
sleep 120
EOF
  chmod +x "$TMP"/emit_*.sh

  # 7a. POSITIVE: the selftest path can reach RAN, so 7b is a real discriminator.
  rc="$(run_self "$TMP/emit_ok.sh" selftest_ok "$TMP/self_ok" "EMIT_MAX_RSS_KB=1048576")"
  if [[ "$rc" -ne 0 ]]; then
    bad "selftest positive control failed: $(tail -1 "$TMP/self_ok.log")"
  elif [[ "$(col "$TMP/self_ok" 13)" != "RAN" ]]; then
    bad "selftest positive control did not reach RAN: $(col "$TMP/self_ok" 13) / $(col "$TMP/self_ok" 16)"
  elif [[ "$(col "$TMP/self_ok" 3)" != "EMITTED" ]]; then
    bad "selftest positive control emit_status=$(col "$TMP/self_ok" 3)"
  else
    ok "selftest path reaches EMITTED / RAN when emission succeeds"
  fi

  # 7b. THE BYPASS: certificate on disk, then killed by the ceiling.
  rc="$(run_self "$TMP/emit_cert_then_oom.sh" selftest_oom "$TMP/self_oom" "EMIT_MAX_RSS_KB=1048576")"
  diag="$(col "$TMP/self_oom" 16)"
  certs="$(find "$TMP/self_oom/blocks/selftest_oom" -name '*.dcert' -size +0c 2>/dev/null | wc -l)"
  if [[ "$rc" -ne 0 ]]; then
    bad "cert-then-OOM control: the sweep failed: $(tail -1 "$TMP/self_oom.log")"
  elif [[ "$certs" -lt 1 ]]; then
    bad "cert-then-OOM control planted no certificate, so it tests nothing"
  elif [[ "$(col "$TMP/self_oom" 13)" == "RAN" ]]; then
    bad "a certificate written before the kill was RUN and published"
  elif [[ "$(col "$TMP/self_oom" 3)" != "NO_CERT" ]]; then
    bad "cert-then-OOM row emit_status=$(col "$TMP/self_oom" 3), not NO_CERT"
  elif [[ "$diag" != resource-limit:* ]]; then
    bad "cert-then-OOM row is not tagged resource-limit: '$diag'"
  elif [[ "$(nf "$TMP/self_oom")" != "18" || "$(col "$TMP/self_oom" 13)" != "NOT_RUN" || "$(col "$TMP/self_oom" 15)" != "-" ]]; then
    bad "cert-then-OOM row shape: nf=$(nf "$TMP/self_oom") rt=$(col "$TMP/self_oom" 13) digest=$(col "$TMP/self_oom" 15)"
  elif [[ "$diag" != *"partial certificate written and NOT run"* ]]; then
    bad "cert-then-OOM diagnostic does not record the unused certificate: '$diag'"
  else
    ok "certificate written before the kill: NO_CERT / NOT_RUN, certificate kept as evidence and not run"
  fi

  # 7b2. A FRESH CERTIFICATE WITH NO AUTHORITATIVE EVIDENCE MUST NOT RUN.
  # Missing counters are not "no kill"; the block cannot be certified either way,
  # so it is reported as NO_VERDICT and excluded from runtime coverage.
  rc="$(run_self "$TMP/emit_cert_no_evidence.sh" selftest_noev "$TMP/self_noev" "EMIT_MAX_RSS_KB=1048576")"
  diag="$(col "$TMP/self_noev" 16)"
  certs="$(find "$TMP/self_noev/blocks/selftest_noev" -name '*.dcert' -size +0c 2>/dev/null | wc -l)"
  if [[ "$rc" -ne 0 ]]; then
    bad "no-evidence control: the sweep failed: $(tail -1 "$TMP/self_noev.log")"
  elif [[ "$certs" -lt 1 ]]; then
    bad "no-evidence control planted no certificate, so it tests nothing"
  elif [[ "$(col "$TMP/self_noev" 13)" == "RAN" ]]; then
    bad "a certificate was RUN although the kernel verdict was unavailable"
  elif [[ "$(col "$TMP/self_noev" 3)" != "NO_VERDICT" ]]; then
    bad "no-evidence row emit_status=$(col "$TMP/self_noev" 3), not NO_VERDICT"
  elif [[ "$diag" != resource-evidence-unavailable:* ]]; then
    bad "no-evidence row is not tagged resource-evidence-unavailable: '$diag'"
  elif [[ "$diag" == resource-limit:* ]]; then
    bad "missing evidence was reported as an OOM that was never observed"
  elif [[ "$(nf "$TMP/self_noev")" != "18" || "$(col "$TMP/self_noev" 13)" != "NOT_RUN" || "$(col "$TMP/self_noev" 15)" != "-" ]]; then
    bad "no-evidence row shape: nf=$(nf "$TMP/self_noev") rt=$(col "$TMP/self_noev" 13) digest=$(col "$TMP/self_noev" 15)"
  else
    ok "certificate with NO authoritative counters: NO_VERDICT / NOT_RUN, not RAN and not a fabricated OOM"
  fi

  # 7b3. SIGKILL WITHOUT A POSITIVE COUNTER IS NOT AN OOM.
  # The reported defect: rc=137 with no kernel counter produced a
  # `resource-limit:` row, fabricating an OOM AND contradicting the run's own
  # provenance, which names memory.events as the verdict source.
  rc="$(run_self "$TMP/emit_cert_sigkill.sh" selftest_sig "$TMP/self_sig" "EMIT_MAX_RSS_KB=1048576")"
  diag="$(col "$TMP/self_sig" 16)"
  certs="$(find "$TMP/self_sig/blocks/selftest_sig" -name '*.dcert' -size +0c 2>/dev/null | wc -l)"
  rec="$TMP/self_sig/blocks/selftest_sig/emit.cgroup"
  if [[ "$rc" -ne 0 ]]; then
    bad "SIGKILL control: the sweep failed: $(tail -1 "$TMP/self_sig.log")"
  elif [[ "$certs" -lt 1 ]]; then
    bad "SIGKILL control planted no certificate, so it tests nothing"
  elif [[ "$diag" == resource-limit:* ]]; then
    bad "a SIGKILL with no kernel counter was reported as a resource limit: '$diag'"
  elif [[ "$(col "$TMP/self_sig" 13)" == "RAN" ]]; then
    bad "a certificate was RUN although the kernel verdict was unavailable"
  elif [[ "$(col "$TMP/self_sig" 3)" != "NO_VERDICT" ]]; then
    bad "SIGKILL row emit_status=$(col "$TMP/self_sig" 3), not NO_VERDICT"
  elif [[ "$diag" != resource-evidence-unavailable:* ]]; then
    bad "SIGKILL row is not tagged resource-evidence-unavailable: '$diag'"
  elif [[ "$(nf "$TMP/self_sig")" != "18" || "$(col "$TMP/self_sig" 13)" != "NOT_RUN" || "$(col "$TMP/self_sig" 15)" != "-" ]]; then
    bad "SIGKILL row shape: nf=$(nf "$TMP/self_sig") rt=$(col "$TMP/self_sig" 13) digest=$(col "$TMP/self_sig" 15)"
  elif ! grep -qs '^oom_kill=0$' "$rec"; then
    bad "SIGKILL control did not produce a zero-counter shape: $(tr '\n' ' ' < "$rec" | cut -c1-150)"
  else
    ok "SIGKILL, rc=$(grep -s '^emit_rc=' "$rec" | cut -d= -f2), NO positive counter: NO_VERDICT / NOT_RUN, never resource-limit"
  fi

  # 7c. A TIMEOUT must stay a timeout, not become a resource limit.
  rc="$(run_self "$TMP/emit_slow.sh" selftest_slow "$TMP/self_slow" "EMIT_MAX_RSS_KB=1048576" "EMIT_TIMEOUT=5")"
  diag="$(col "$TMP/self_slow" 16)"
  if [[ "$rc" -ne 0 ]]; then
    bad "timeout control: the sweep failed: $(tail -1 "$TMP/self_slow.log")"
  elif [[ "$(col "$TMP/self_slow" 3)" != "EMIT_FAILED" ]]; then
    bad "timeout row emit_status=$(col "$TMP/self_slow" 3), not EMIT_FAILED"
  elif [[ "$diag" == resource-limit:* ]]; then
    bad "a TIMEOUT was folded into the resource-limit outcome: '$diag'"
  elif [[ "$diag" != *timeout* ]]; then
    bad "timeout row does not name the timeout: '$diag'"
  else
    ok "timeout stays its own outcome under a ceiling: '$(cut -c1-44 <<<"$diag")'"
  fi

  # 7e. A NONZERO EMITTER RC IS A FAILURE EVEN WITH A VALID CERTIFICATE AND
  #     CLEAN TERMINAL COUNTERS.
  rc="$(run_self "$TMP/emit_cert_rc3.sh" selftest_rc3 "$TMP/self_rc3" "EMIT_MAX_RSS_KB=1048576")"
  diag="$(col "$TMP/self_rc3" 16)"
  certs="$(find "$TMP/self_rc3/blocks/selftest_rc3" -name '*.dcert' -size +0c 2>/dev/null | wc -l)"
  rec="$TMP/self_rc3/blocks/selftest_rc3/emit.cgroup"
  if [[ "$rc" -ne 0 ]]; then
    bad "rc3 control: the sweep failed: $(tail -1 "$TMP/self_rc3.log")"
  elif [[ "$certs" -lt 1 ]]; then
    bad "rc3 control wrote no certificate, so it tests nothing"
  elif ! grep -qs '^verdict=OK' "$rec" || ! grep -qs '^terminal_snapshot=1' "$rec"; then
    bad "rc3 control did not produce clean terminal counters, so it tests the wrong thing: $(grep -s '^verdict=' "$rec")"
  elif [[ "$(col "$TMP/self_rc3" 13)" == "RAN" ]]; then
    bad "a certificate was RUN although the emitter returned nonzero"
  elif [[ "$(col "$TMP/self_rc3" 3)" != "EMIT_FAILED" ]]; then
    bad "rc3 row emit_status=$(col "$TMP/self_rc3" 3), not EMIT_FAILED"
  elif [[ "$diag" != emitter-failed:* ]]; then
    bad "rc3 row is not tagged emitter-failed: '$diag'"
  elif [[ "$(nf "$TMP/self_rc3")" != "18" || "$(col "$TMP/self_rc3" 13)" != "NOT_RUN" || "$(col "$TMP/self_rc3" 15)" != "-" ]]; then
    bad "rc3 row shape: nf=$(nf "$TMP/self_rc3") rt=$(col "$TMP/self_rc3" 13) digest=$(col "$TMP/self_rc3" 15)"
  elif [[ "$diag" != *"certificate written and NOT run"* ]]; then
    bad "rc3 diagnostic does not record the unused certificate: '$diag'"
  elif [[ "$("$ROOT/scripts/classify_d4_rows.sh" "$TMP/self_rc3/rows.tsv" 2>/dev/null | grep -c 'RUNTIME COVERAGE: 0 of 1')" -ne 1 ]]; then
    bad "the classifier counted an emitter failure as coverage"
  else
    ok "valid certificate + clean terminal counters + emitter rc=3: EMIT_FAILED / NOT_RUN, 0 coverage"
  fi

  # 7f. The same branch must keep a TIMEOUT distinct, with a certificate present.
  rc="$(run_self "$TMP/emit_cert_slow.sh" selftest_crto "$TMP/self_crto" "EMIT_MAX_RSS_KB=1048576" "EMIT_TIMEOUT=5")"
  diag="$(col "$TMP/self_crto" 16)"
  certs="$(find "$TMP/self_crto/blocks/selftest_crto" -name '*.dcert' -size +0c 2>/dev/null | wc -l)"
  if [[ "$rc" -ne 0 ]]; then
    bad "cert-before-timeout control: the sweep failed: $(tail -1 "$TMP/self_crto.log")"
  elif [[ "$certs" -lt 1 ]]; then
    bad "cert-before-timeout control wrote no certificate"
  elif [[ "$(col "$TMP/self_crto" 13)" == "RAN" ]]; then
    bad "a certificate written before a TIMEOUT was run and published"
  elif [[ "$diag" != *"emission timeout"* ]]; then
    bad "cert-before-timeout row does not name the timeout: '$diag'"
  elif [[ "$diag" == resource-limit:* ]]; then
    bad "a timeout was folded into the resource-limit outcome"
  else
    ok "certificate written before a TIMEOUT: not run, timeout kept distinct"
  fi

  # 7d. counters persisted, and the scope cleaned up.
  cg="$TMP/self_oom/blocks/selftest_oom/emit.cgroup"
  if [[ ! -s "$cg" ]]; then
    bad "no per-block cgroup record at $cg"
  elif ! grep -q '^verdict=LIMIT_HIT' "$cg"; then
    bad "per-block record does not carry the LIMIT_HIT verdict: $(grep '^verdict=' "$cg")"
  elif ! grep -qE '^oom_kill=[1-9]|^oom_group_kill=[1-9]' "$cg"; then
    # A LIMIT_HIT must rest on a POSITIVE KERNEL COUNTER in the record, and on
    # nothing else.  This used to accept `systemd_result=oom-kill` or
    # `emit_rc=137|143` as supporting evidence, which is the very rule the
    # verdict function rejects -- the control contradicted the policy it exists
    # to protect.  The in-scope anchor is what makes the counter reliable, so
    # demanding it is strict, not flaky.
    bad "LIMIT_HIT is recorded without a positive kernel counter: $(tr '\n' ' ' < "$cg" | cut -c1-160)"
  elif ! grep -q '^counters_readable=1' "$cg"; then
    bad "per-block record says the counters were unreadable"
  elif ! grep -q '^cleanup=clean' "$cg"; then
    bad "the scope was not cleaned up: $(grep '^cleanup=' "$cg")"
  elif [[ -n "$(systemctl --user list-units --all --no-legend 'd4emit-selftest*' 2>/dev/null)" ]]; then
    # Scoped to THIS control's units: an unrelated sweep may legitimately be
    # running its own d4emit-<module> scopes on the same machine.
    bad "selftest scopes were left behind: $(systemctl --user list-units --all --no-legend 'd4emit-selftest*' | head -2)"
  else
    ok "per-block counters persisted (oom_kill, peak, Result, rc), scope cleaned up, none left behind"
  fi
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

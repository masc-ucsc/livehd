#!/usr/bin/env bash
# The limit-hit verdict for one emission scope, as a pure, three-valued function.
#
# WHY THIS IS A SEPARATE, SOURCEABLE FILE.  The shapes that matter cannot be
# produced on demand, so they are injected in controls.
#
# WHY `Result` IS NOT THE VERDICT.  Measured on this host, five times out of
# five, a real cgroup OOM kill reports
#
#     rc=137  Result=success  ActiveState=active  Tasks=0
#     memory.events: oom=1 oom_kill=1       memory.peak=67108864
#
# while an earlier `cva6_tlb` run reported rc=143 with Result=oom-kill.  A
# harness that believes `Result` both MISSES real kills and REFUSES enforced
# caps, depending on which shape the kernel and systemd happen to produce.
# A POSITIVE KERNEL COUNTER IS THE ONLY THING THAT DECLARES A LIMIT HIT.
# `Result` and `rc` are recorded as diagnostics and decide nothing: `rc=137`
# says a process was killed, not that `MemoryMax` killed it.
#
# WHY THERE IS A THIRD VALUE.  Missing or malformed counters are NOT "no kill".
# Treating an unreadable `memory.events` as zero would silently convert an
# infrastructure failure into validated runtime coverage -- and the cgroup IS
# released before a post-hoc read on this host, so this is the common case, not
# a corner.  `UNKNOWN` is reported, never normalised away, and never fabricated
# into an OOM either.
#
#   d4_scope_verdict <oom_kill> <oom_group_kill> <rc> <result> <counters_ok> <terminal_ok>
#     counters_ok: 1 when at least one WELL-FORMED memory.events snapshot exists
#     terminal_ok: 1 when a well-formed POST-COMMAND snapshot exists
#   prints:  "LIMIT_HIT <evidence>" | "OK <evidence>" | "UNKNOWN <evidence>"
#
# ONLY A POSITIVE KERNEL COUNTER MAKES A LIMIT HIT.  `rc=137` says the process
# was killed; it does not say `MemoryMax` killed it.  `Result=oom-kill` is
# systemd's summary, and it is demonstrably not produced for every real kill on
# this host.  Letting either stand alone would manufacture resource-limit rows
# for an ordinary SIGKILL -- and would contradict this harness's own provenance,
# which names `memory.events` as the verdict source.  Both are kept in the
# evidence string as DIAGNOSTICS and neither can decide the verdict.
#
# A ZERO-COUNTER `OK` NEEDS TERMINAL EVIDENCE.  The in-scope recorder writes a
# snapshot on ENTRY as well as on exit, so a wrapper that is killed mid-run
# leaves a well-formed snapshot full of zeros that says nothing about what
# happened afterwards.  Positive counters are monotone and may be believed from
# any snapshot; "no kill" may only be believed from a post-command one.
# ---------------------------------------------------------------------------
# THE COUNTER DOMAIN.  One policy, shared by every consumer.
#
# A cgroup kill counter is an UNBOUNDED decimal written by the kernel, but
# Bash's `[[ -gt ]]` / `[[ -ne ]]` are fixed-width: `18446744073709551616`
# (2^64) is canonical, nonzero, and evaluates to EXACTLY ZERO in Bash
# arithmetic. A positivity test built on `-gt 0` therefore reads that value as
# "no kill" -- so a huge positive counter could be lost at collection,
# classified OK at source, and accepted by every downstream driver.
#
# So: validate the SYNTAX first, then decide zero/nonzero TEXTUALLY, and compare
# magnitudes by length-then-lexicographic order on the normalized decimal. No
# counter is ever handed to Bash arithmetic. (Ordinary loop and tally
# arithmetic is untouched; this is only for the unbounded kernel counters.)

# Canonical nonnegative decimal: no sign, no leading zeros, no whitespace, not
# empty, ASCII ONLY. Anything else is evidence failure, never zero.
#
# `LC_ALL=C` is load-bearing, not hygiene: under a UTF-8 locale a bracket
# expression `[0-9]` matches other Unicode decimal digits, so a FULLWIDTH ONE
# (U+FF11) passed this check and then went on to be compared textually against
# ASCII digits. A counter is a byte string written by the kernel; it is
# validated as one.
d4_counter_is_canonical() {
  local LC_ALL=C
  [[ "${1-}" =~ ^(0|[1-9][0-9]*)$ ]]
}

# Zero/nonzero WITHOUT arithmetic. A canonical decimal is zero iff it is "0".
d4_counter_is_zero()    { [[ "${1-}" == "0" ]]; }
d4_counter_is_nonzero() { d4_counter_is_canonical "${1-}" && [[ "${1-}" != "0" ]]; }

# Unbounded magnitude compare: longer canonical decimal wins, else lexicographic
# (digits collate in order under LC_ALL=C). Returns 2 if either side is not
# canonical, so a caller cannot mistake "not greater" for "well formed".
d4_counter_gt() {
  local LC_ALL=C a="${1-}" b="${2-}"
  d4_counter_is_canonical "$a" && d4_counter_is_canonical "$b" || return 2
  if [[ "${#a}" -ne "${#b}" ]]; then        # length arithmetic, not counter arithmetic
    [[ "${#a}" -gt "${#b}" ]]
    return
  fi
  [[ "$a" > "$b" ]]
}

d4_scope_verdict() {
  local ok_raw="${1-}" ogk_raw="${2-}" rc_raw="${3-}" result="${4-}" have="${5-}" term="${6-}"
  local ev="oom_kill=${ok_raw:-<missing>} oom_group_kill=${ogk_raw:-<missing>}"
  ev="$ev rc=${rc_raw:-<missing>} Result=${result:-unknown}"
  [[ "$have" == "1" ]] || ev="$ev counters=UNAVAILABLE"
  [[ "$term" == "1" ]] || ev="$ev terminal-snapshot=NONE"

  local counters_ok=0
  if [[ "$have" == "1" ]] && d4_counter_is_canonical "$ok_raw" && d4_counter_is_canonical "$ogk_raw"; then
    counters_ok=1
  elif [[ "$have" == "1" ]]; then
    ev="$ev counters=MALFORMED"
  fi

  # 1. THE ONLY WAY TO A LIMIT HIT.  Decided TEXTUALLY: `-gt 0` wraps 2^64 to
  #    zero and would report a huge kill count as a clean run.
  if [[ "$counters_ok" == "1" ]] \
     && { d4_counter_is_nonzero "$ok_raw" || d4_counter_is_nonzero "$ogk_raw"; }; then
    echo "LIMIT_HIT cgroup memory.events reports a kill ($ev)"; return 0
  fi

  # 2. No positive counter.  Anything short of a well-formed TERMINAL
  #    observation of zero is an evidence failure, not a clean run -- and a
  #    kill-like rc or Result without a counter is still an evidence failure,
  #    not an OOM.
  if [[ "$counters_ok" != "1" ]]; then
    echo "UNKNOWN no well-formed cgroup counters, so a memory kill can be neither shown nor ruled out ($ev)"
    return 0
  fi
  if [[ "$term" != "1" ]]; then
    echo "UNKNOWN counters were observed only before the command completed, so a later kill cannot be ruled out ($ev)"
    return 0
  fi

  # 3. A terminal, well-formed, zero reading IS evidence of no memory kill.
  #    `rc` is reported but does not change the verdict: an externally killed
  #    emission is not a resource-limit outcome, and a timeout stays a timeout.
  if [[ "$rc_raw" == "137" ]]; then
    echo "OK terminal counters show no memory kill; rc=137 came from elsewhere ($ev)"; return 0
  fi
  if [[ "$result" == "oom-kill" ]]; then
    echo "UNKNOWN systemd reported oom-kill but the terminal counters are zero; the two disagree ($ev)"
    return 0
  fi
  echo "OK $ev"
}

# ---------------------------------------------------------------------------
# CGROUP EVIDENCE CAPTURE.
#
# The transient scope's cgroup is RELEASED before the harness can read it.
# Measured on this host: an OOM-killed scope reports `ControlGroup=<EMPTY>`, and
# a cleanly-exited one reports a path whose `memory.events` is already gone.  A
# post-hoc read is therefore racy BY CONSTRUCTION, and a missed read must never
# be mistaken for "no kill".
#
# Evidence is captured TWICE, while the cgroup is alive:
#   * `d4_emit_in_scope.sh` runs inside the scope and records the counters on
#     entry and on exit -- covers any command that returns, however brief;
#   * `scope_sample` polls from outside for the whole run -- covers the case
#     where the in-scope wrapper is itself killed.
# `scope_merge` takes the MAXIMUM over every snapshot: an OOM that happened
# cannot be un-happened by a later or earlier reading.
# ---------------------------------------------------------------------------

# scope_sample <unit-without-suffix> <snapshot-out>   (run in the background)
scope_sample() {
  local u="$1.scope" out="$2" cg="" ev="" i
  for ((i = 0; i < 6000; i++)); do
    cg="$(systemctl --user show "$u" -p ControlGroup --value 2>/dev/null)"
    [[ -n "$cg" && -r "/sys/fs/cgroup${cg}/memory.events" ]] && break
    cg=""
    sleep 0.01
  done
  [[ -n "$cg" ]] || return 0
  ev="/sys/fs/cgroup${cg}/memory.events"
  while [[ -r "$ev" ]]; do
    {
      echo "source=sampler"
      # A sampler reading is never provably post-command: the loop ends when
      # the cgroup disappears, and the last write precedes that.
      echo "phase=sample"
      echo "cgroup=$cg"
      awk '/^oom /{print "oom="$2} /^oom_kill /{print "oom_kill="$2} /^oom_group_kill /{print "oom_group_kill="$2}' "$ev" 2>/dev/null
      echo "memory_peak_bytes=$(cat "/sys/fs/cgroup${cg}/memory.peak" 2>/dev/null || echo 0)"
      echo "sampled_at=$(date -Is)"
    } > "$out.tmp" 2>/dev/null && mv -f "$out.tmp" "$out" 2>/dev/null
    # Tight, because the window between the kill incrementing `oom_kill` and the
    # cgroup being released is short.  A miss here does NOT fall back to
    # Result/rc -- nothing does.  Without a positive counter the verdict is
    # UNKNOWN, so a miss costs a classifiable row, which is why the in-scope
    # anchor exists as well.
    sleep 0.02
  done
}

# scope_merge <snapshot...>
#   -> "<oom_kill> <oom_group_kill> <peak> <have> <terminal> <sources>"
#
# `have`     1 when some snapshot carried well-formed counters.
# `terminal` 1 when a well-formed POST-COMMAND (`phase=exit`) snapshot exists.
#
# Positive counters are monotone, so the MAXIMUM across every snapshot is taken
# -- an OOM that happened cannot be un-happened by an earlier or later reading.
# A zero, by contrast, is only as good as when it was read, which is why
# `terminal` is reported separately.
scope_merge() {
  local f k g pk ph have=0 term=0 mk=0 mg=0 mp=0 srcs=""
  for f in "$@"; do
    [[ -f "$f" && -s "$f" ]] || continue
    k="$(awk -F= '/^oom_kill=/{print $2}' "$f" | head -1)"
    g="$(awk -F= '/^oom_group_kill=/{print $2}' "$f" | head -1)"
    pk="$(awk -F= '/^memory_peak_bytes=/{print $2}' "$f" | head -1)"
    ph="$(awk -F= '/^phase=/{print $2}' "$f" | head -1)"
    srcs="$srcs$(awk -F= '/^source=/{print $2}' "$f" | head -1):${ph:-unmarked},"
    # A snapshot whose counters are not integers is NOT evidence of zero; it is
    # skipped, and `have` stays 0 unless some snapshot is well formed.
    d4_counter_is_canonical "$k" && d4_counter_is_canonical "$g" || continue
    have=1
    [[ "$ph" == "exit" ]] && term=1
    # Unbounded compare: a 2^64 snapshot must not be lost taking the maximum.
    d4_counter_gt "$k" "$mk" && mk="$k"
    d4_counter_gt "$g" "$mg" && mg="$g"
    d4_counter_gt "$pk" "$mp" && mp="$pk"
  done
  printf '%s %s %s %s %s %s\n' "$mk" "$mg" "$mp" "$have" "$term" "${srcs%,}"
}

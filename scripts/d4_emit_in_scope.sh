#!/usr/bin/env bash
# Run the emission command INSIDE the memory scope and capture the cgroup's
# counters from inside it, where they exist and where there is no discovery race.
#
# WHY INSIDE.  On this host the transient scope's cgroup is released almost
# immediately after a kill -- measured, an OOM-killed scope reports
# `ControlGroup=<EMPTY>`, and an outside sampler's final reading consistently
# lands BEFORE `oom_kill` is incremented.  A verdict that requires a positive
# kernel counter therefore cannot be reached from outside reliably.
#
# THREE WRITERS, THREE JOBS:
#   entry  snapshot   identifies the cgroup even if everything after it dies
#   anchor loop       samples continuously from within the cgroup, and takes a
#                     FINAL reading on SIGTERM -- this is what actually catches
#                     `oom_kill` in the instant before teardown
#   exit   snapshot   the only POST-COMMAND reading, and the only one that may
#                     be believed as "no kill"
#
# The anchor also keeps the cgroup alive a moment longer than its victim.  It
# allocates nothing and is killed as soon as the command returns.
#
#   d4_emit_in_scope.sh <snapshot-out> <command> [args...]
#     writes <snapshot-out>         entry, then exit
#     writes <snapshot-out>.anchor  continuous samples
# The command's exit status is preserved.
set -uo pipefail

# NOT `OUT`.  The emission scripts take their output directory from an EXPORTED
# `OUT`, so using that name here reassigned the caller's variable and the child
# wrote its whole tree into the snapshot path.
D4_SNAPSHOT_OUT="${1:?usage: d4_emit_in_scope.sh <snapshot-out> <command> [args...]}"
shift
[[ $# -ge 1 ]] || { echo "d4_emit_in_scope.sh: no command given" >&2; exit 2; }
D4_ANCHOR_OUT="$D4_SNAPSHOT_OUT.anchor"
D4_ANCHOR_SECONDS="${D4_ANCHOR_SECONDS:-7200}"

d4_cgroup_base() {
  local rel
  rel="$(awk -F: '$1=="0"{print $3}' /proc/self/cgroup 2>/dev/null)"
  [[ -n "$rel" ]] || return 1
  printf '/sys/fs/cgroup%s' "$rel"
}

# d4_snapshot <phase> <outfile>
d4_snapshot() {
  local phase="$1" out="$2" base ev rel
  rel="$(awk -F: '$1=="0"{print $3}' /proc/self/cgroup 2>/dev/null)" || return 1
  [[ -n "$rel" ]] || return 1
  base="/sys/fs/cgroup${rel}"
  ev="$base/memory.events"
  [[ -r "$ev" ]] || return 1
  {
    echo "source=in-scope"
    echo "phase=$phase"
    echo "cgroup=$rel"
    awk '/^oom /{print "oom="$2} /^oom_kill /{print "oom_kill="$2} /^oom_group_kill /{print "oom_group_kill="$2}' "$ev"
    echo "memory_peak_bytes=$(cat "$base/memory.peak" 2>/dev/null || echo 0)"
    echo "memory_max=$(cat "$base/memory.max" 2>/dev/null || echo unknown)"
    echo "sampled_at=$(date -Is)"
  } > "$out.tmp" 2>/dev/null && mv -f "$out.tmp" "$out" 2>/dev/null
}

d4_anchor_loop() {
  # `phase=sample`, never `exit`: a reading taken while the command is still
  # running -- or while the scope is being torn down -- catches a POSITIVE
  # counter, but it is not post-command and must not authorise a zero-counter OK.
  trap 'd4_snapshot sample "$D4_ANCHOR_OUT" || true; exit 0' TERM INT HUP
  local i
  for ((i = 0; i < D4_ANCHOR_SECONDS * 100; i++)); do
    d4_snapshot sample "$D4_ANCHOR_OUT" || true
    sleep 0.01
  done
}

d4_snapshot entry "$D4_SNAPSHOT_OUT" || true
d4_anchor_loop &
d4_anchor=$!

"$@"
rc=$?

d4_snapshot exit "$D4_SNAPSHOT_OUT" || true
kill -TERM "$d4_anchor" 2>/dev/null
wait "$d4_anchor" 2>/dev/null
exit "$rc"

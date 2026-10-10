#!/usr/bin/env bash
# Controls for CORPUS BINDING ADMISSION in scripts/verify_d4_run.sh.
#
# WHAT THIS GUARDS. Every CORE-ET identity the run records was checked for
# PRESENCE only, so each could carry any value and still pass: a wrong
# `coreet_rev`, a wrong `coreet_patch_script_sha256`, a wrong
# `coreet_derived_map_sha256` and `coreet_tech=xilinx` each exited 0 and
# printed VERIFICATION PASSED. A recorded identity that is never re-derived is
# a label, not a binding. And `emit_rc` was compared with `-ne 0`, which is
# Bash arithmetic and 64-bit: `18446744073709551616` is 2^64, wraps to zero,
# and was accepted as a clean emitter result.
#
# Each case below drives the ACTUAL CLI on a COPY of a real run -- the
# consumed run and the consumed derived sources are never written to -- and
# mutates exactly one field. The positive leg must exit 0; every tampered leg
# must exit nonzero AND name the field, because an exit code alone would not
# show that the right check fired.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
VERIFY="$ROOT/scripts/verify_d4_run.sh"
SRC="${D4_CONTROL_RUN:-$ROOT/generated/master_port/d4_pilot_coreet}"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

[[ -r "$VERIFY" ]] || { echo "FATAL: missing $VERIFY" >&2; exit 2; }
if [[ ! -s "$SRC/rows.tsv" || ! -s "$SRC/provenance.txt" ]]; then
  echo "SKIP: no verifiable run at $SRC -- unresolved prerequisite, not a pass" >&2
  exit 3
fi

W="$ROOT/generated/d4_verify_controls"
rm -rf "$W"; mkdir -p "$W"

# Snapshots taken BEFORE anything runs: verification must not write to what it
# verifies, nor to the derived sources it re-derives against.
SRC_SNAPSHOT="$(cat "$SRC/provenance.txt" "$SRC/rows.tsv" | sha256sum)"
DERIVED_SNAPSHOT_DIR="$(grep -m1 '^coreet_derived_dir=' "$SRC/provenance.txt" | cut -d= -f2-)"
DERIVED_SNAPSHOT="$(sha256sum "$DERIVED_SNAPSHOT_DIR"/* 2>/dev/null | sha256sum)"

# The positive baseline: the accepted modules only, so a clean run exits 0 and
# the tampered legs are measured against a PASS rather than against an already
# failing verification. Modules with a nonzero emitter rc are excluded here
# because they are correctly refused -- that refusal has its own control in
# exit_status_controls.sh and is not what is being measured.
mkbase() { # mkbase <dir>
  local d="$1"
  rm -rf "$d"; mkdir -p "$d/blocks"
  head -1 "$SRC/rows.tsv" > "$d/rows.tsv"
  cp "$SRC/provenance.txt" "$d/provenance.txt"
  : > "$d/modules.list"
  while IFS=$'\t' read -r _c m _e _cert _s _src _n _f _mm _p _pm _co rt _rest; do
    [[ "$rt" == "RAN" ]] || continue
    grep -q "^emit_rc=0$" "$SRC/blocks/$m/emit.cgroup" 2>/dev/null || continue
    grep -P "^[^\t]*\t$m\t" "$SRC/rows.tsv" >> "$d/rows.tsv"
    cp -a "$SRC/blocks/$m" "$d/blocks/$m"
    printf '%s\n' "$m" >> "$d/modules.list"
  done < <(tail -n +2 "$SRC/rows.tsv")
  # The baseline declares a SUBSET of the original run's modules, so its
  # recorded module-list hash must be the hash of THAT list. Leaving the
  # original's in place would make every leg fail on the list instead of on
  # the field under test -- which is how the patcher leg first "passed".
  sed -i "s|^module_list_sha256=.*|module_list_sha256=$(sha256sum < "$d/modules.list" | cut -d' ' -f1)|" \
    "$d/provenance.txt"
}

run_verify() { # run_verify <dir> <tag>
  nice -n19 ionice -c3 timeout 1800 bash "$VERIFY" "$1" "$1/modules.list" > "$W/$2.log" 2>&1
  echo $?
}

mkbase "$W/base"
n_base="$(( $(wc -l < "$W/base/modules.list") ))"
if [[ "$n_base" -lt 1 ]]; then
  echo "SKIP: $SRC has no accepted module to build a positive control from" >&2
  exit 3
fi

rc="$(run_verify "$W/base" base)"
if [[ "$rc" -eq 0 ]] && grep -q 'VERIFICATION PASSED' "$W/base.log"; then
  ok "positive: $n_base accepted module(s) verify clean (exit 0)"
else
  bad "positive control does not pass (exit $rc); every tampered leg below is then vacuous"
  grep -m4 '^  FAIL' "$W/base.log" | sed 's/^/       /'
fi

# tamper <tag> <sed-expression> <expected-substring> <description>
tamper() {
  local tag="$1" expr="$2" want="$3" desc="$4"
  rm -rf "$W/$tag"; cp -a "$W/base" "$W/$tag"
  sed -i "$expr" "$W/$tag/provenance.txt"
  local rc; rc="$(run_verify "$W/$tag" "$tag")"
  if [[ "$rc" -ne 0 ]] && grep -qF "$want" "$W/$tag.log"; then
    ok "$desc is refused, and named"
  elif [[ "$rc" -ne 0 ]]; then
    bad "$desc is refused but the message does not name it (looked for '$want')"
  else
    bad "$desc was ACCEPTED (exit 0)"
  fi
}

tamper rev   's|^coreet_rev=.*|coreet_rev=0000000000000000000000000000000000000000|' \
             "CORE-ET revision DIFFERS" "a wrong coreet_rev"
tamper patch 's|^coreet_patch_script_sha256=.*|coreet_patch_script_sha256=0000000000000000000000000000000000000000000000000000000000000000|' \
             "CORE-ET patcher DIFFERS" "a wrong coreet_patch_script_sha256"
tamper map   's|^coreet_derived_map_sha256=.*|coreet_derived_map_sha256=0000000000000000000000000000000000000000000000000000000000000000|' \
             "consumed map DIFFERS" "a wrong coreet_derived_map_sha256"
tamper tech  's|^coreet_tech=.*|coreet_tech=xilinx|' \
             "coreet_tech" "coreet_tech=xilinx"
tamper agg   's|^coreet_derived_aggregate_sha256=.*|coreet_derived_aggregate_sha256=0000000000000000000000000000000000000000000000000000000000000000|' \
             "derived CORE-ET sources DIFFER" "a wrong derived-source aggregate"
tamper clean 's|^coreet_relevant_inputs_clean=.*|coreet_relevant_inputs_clean=no|' \
             "coreet_relevant_inputs_clean" "a run that recorded its inputs as dirty"

# THE WRAPPING ZERO. Not a provenance field -- it lives in a block record.
for spec in "wrap:18446744073709551616:2^64, which Bash arithmetic wraps to zero" \
            "neg:-0:'-0', which is not the one character the writer emits" \
            "pad:00:'00', a zero with a leading zero"; do
  tag="${spec%%:*}"; rest="${spec#*:}"; val="${rest%%:*}"; desc="${rest#*:}"
  rm -rf "$W/$tag"; cp -a "$W/base" "$W/$tag"
  first="$(head -1 "$W/base/modules.list")"
  sed -i "s|^emit_rc=.*|emit_rc=$val|" "$W/$tag/blocks/$first/emit.cgroup"
  rc="$(run_verify "$W/$tag" "$tag")"
  if [[ "$rc" -ne 0 ]] && grep -qE "emit_rc|emitter returned" "$W/$tag.log"; then
    ok "emit_rc=$desc is refused"
  elif [[ "$rc" -ne 0 ]]; then
    bad "emit_rc=$val is refused but not for its own reason"
  else
    bad "emit_rc=$val was ACCEPTED as a clean emitter result"
  fi
done

# THE CONSUMED INPUTS ARE UNTOUCHED. A verifier that rewrote what it checks
# would make every later run agree with it. Compared against a snapshot taken
# BEFORE any control ran -- not against the baseline copy, which `mkbase`
# deliberately rewrites, and comparing to that was my own control's bug.
now_src="$(cat "$SRC/provenance.txt" "$SRC/rows.tsv" | sha256sum)"
if [[ "$now_src" == "$SRC_SNAPSHOT" ]]; then
  ok "the source run's provenance and rows are byte-identical after all controls"
else
  bad "the source run changed during verification"
fi
now_der="$(sha256sum "$DERIVED_SNAPSHOT_DIR"/* 2>/dev/null | sha256sum)"
if [[ "$now_der" == "$DERIVED_SNAPSHOT" ]]; then
  ok "the consumed derived sources are byte-identical after all controls"
else
  bad "the consumed derived sources changed during verification"
fi

if [[ $fails -eq 0 ]]; then echo "D4-VERIFY-BINDING CONTROLS OK"; else echo "D4-VERIFY-BINDING CONTROLS FAILED ($fails)"; fi
exit $fails

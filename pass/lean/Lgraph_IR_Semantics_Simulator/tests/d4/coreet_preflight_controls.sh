#!/usr/bin/env bash
# Controls for pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_preflight.sh.
#
# The sweep used to require only that the substitution map EXISTED.  That says
# nothing about which checkout the derived sources came from, whether it has
# moved since, or whether the derived files still match their sources.
#
# EVERYTHING HERE RUNS AGAINST COPIES under this worktree.  The real CORE-ET
# checkout and the real derived directory are never written to, and the
# checkout's untracked `core_et_blocks.txt` is never touched.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
PRE="$ROOT/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_preflight.sh"
PATCHER="$ROOT/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/coreet_patch_srcs.py"
REAL_ROOT="${COREET_ROOT:-/HOMEROOT/projects/core-et}"
REAL_DERIVED="$ROOT/generated/core-et/patched_src"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -x "$PRE" ]] || { echo "FATAL: no $PRE" >&2; exit 2; }
if [[ ! -s "$REAL_DERIVED/map.tsv" ]]; then
  echo "SKIP: no derived sources at $REAL_DERIVED"; exit 0
fi

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/coreet_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# ---------------------------------------------------------------- the mirror
# A real git checkout holding COPIES of exactly the four mapped sources, at
# their original relative paths.  Same bytes, so the hashes the generator
# embedded still match and the derived copies are legitimately current.
MIRROR="$TMP/mirror"
mkdir -p "$MIRROR"
( cd "$MIRROR" && git init -q . && git config user.email c@x && git config user.name c )
while IFS=$'\t' read -r src out; do
  [[ -n "$src" ]] || continue
  rel="${src#"$(readlink -f "$REAL_ROOT")/"}"
  [[ "$rel" != "$src" ]] || rel="${src#"$REAL_ROOT/"}"
  mkdir -p "$MIRROR/$(dirname "$rel")"
  cp "$src" "$MIRROR/$rel"
done < <(grep -v '^[[:space:]]*$' "$REAL_DERIVED/map.tsv")
# `mk/` is a RELEVANT input: every dv/rtlcosim Makefile includes
# mk/rtlcosim.mk, which includes mk/prim.mk, to derive NEW_RTL_FINAL.
mkdir -p "$MIRROR/mk" "$MIRROR/dv"
printf 'include $(REPO_ROOT)/mk/prim.mk\n' > "$MIRROR/mk/rtlcosim.mk"
printf '# prim\n' > "$MIRROR/mk/prim.mk"
printf '# placeholder dv tree\n' > "$MIRROR/dv/.keep"
( cd "$MIRROR" && git add -A && git commit -qm init )

mk_derived() {  # mk_derived <dest>: copy the derived dir, remap to the mirror
  local d="$1"
  mkdir -p "$d"
  cp "$REAL_DERIVED"/*.sv "$d/"
  : > "$d/map.tsv"
  while IFS=$'\t' read -r src out; do
    [[ -n "$src" ]] || continue
    local rel base
    rel="${src#"$(readlink -f "$REAL_ROOT")/"}"; [[ "$rel" != "$src" ]] || rel="${src#"$REAL_ROOT/"}"
    base="$(basename "$out")"
    printf '%s\t%s\n' "$MIRROR/$rel" "$d/$base" >> "$d/map.tsv"
  done < <(grep -v '^[[:space:]]*$' "$REAL_DERIVED/map.tsv")
}

run() { "$PRE" "$1" "$2" "$PATCHER" 2>&1; }
expect_fail() {  # expect_fail <label> <want-substring> <root> <derived>
  local label="$1" want="$2" root="$3" der="$4" out rc
  out="$(run "$root" "$der")"; rc=$?
  if [[ "$rc" -eq 0 ]]; then bad "$label: preflight PASSED"
  elif ! grep -qF "$want" <<<"$out"; then
    bad "$label: failed for the wrong reason: $(head -1 <<<"$out")"
  else ok "$label: refused -- $(grep -m1 FATAL <<<"$out" | cut -c8-82)"; fi
}

# --- 0. POSITIVE: the mirror and its derived copies are consistent ---------
D0="$TMP/d0"; mk_derived "$D0"
out="$(run "$MIRROR" "$D0")"; rc=$?
if [[ "$rc" -ne 0 ]]; then
  bad "positive control refused a consistent mirror: $(head -1 <<<"$out")"
elif ! grep -q '^coreet_rev=[0-9a-f]\{40\}$' <<<"$out"; then
  bad "positive control did not record a full revision"
elif ! grep -q '^coreet_derived_aggregate_sha256=[0-9a-f]\{64\}$' <<<"$out"; then
  bad "positive control did not record the derived aggregate hash"
elif ! grep -q '^coreet_patch_script_sha256=[0-9a-f]\{64\}$' <<<"$out"; then
  bad "positive control did not record the patch-script hash"
elif ! grep -q '^coreet_derived_map_sha256=[0-9a-f]\{64\}$' <<<"$out"; then
  bad "positive control did not record the map hash"
else
  ok "consistent mirror: accepted, revision + patch-script + map + aggregate hashes recorded"
fi

# --- 1. a MISSING derived output ------------------------------------------
D1="$TMP/d1"; mk_derived "$D1"; rm -f "$D1/vpu_defs_pkg.sv"
expect_fail "missing derived output" "does not hold exactly the four outputs" "$MIRROR" "$D1"

# --- 2. an EXTRA file in the derived dir ----------------------------------
D2="$TMP/d2"; mk_derived "$D2"; echo "// leftover" > "$D2/stale_from_an_older_run.sv"
expect_fail "extra/leftover derived file" "does not hold exactly the four outputs" "$MIRROR" "$D2"

# --- 3. a STALE derived output (source moved on) --------------------------
D3="$TMP/d3"; mk_derived "$D3"
S3="$(cut -f1 "$D3/map.tsv" | head -1)"
printf '\n// appended by the control: the source has moved on\n' >> "$S3"
( cd "$MIRROR" && git add -A && git commit -qm "source moved on" )
# A moved-on source makes a FRESH derivation differ from the published file, so
# the byte comparison catches it first.  The embedded-header STALE check remains
# as a second line of defence; it is no longer the thing that fires.
expect_fail "stale derived output (source moved on)" "NOT this generator's output" "$MIRROR" "$D3"
( cd "$MIRROR" && git revert --no-edit HEAD -q 2>/dev/null || git reset -q --hard HEAD~1 )

# --- 4. a MALFORMED map line ----------------------------------------------
D4="$TMP/d4"; mk_derived "$D4"
printf 'only-one-field\n' > "$D4/map.tsv.tmp"
tail -n +2 "$D4/map.tsv" >> "$D4/map.tsv.tmp"; mv "$D4/map.tsv.tmp" "$D4/map.tsv"
expect_fail "malformed map line" "not exactly two tab-separated fields" "$MIRROR" "$D4"

# --- 5. a DUPLICATE map entry ---------------------------------------------
D5="$TMP/d5"; mk_derived "$D5"
head -1 "$D5/map.tsv" > "$D5/dup"; head -1 "$D5/map.tsv" >> "$D5/dup"
tail -n +3 "$D5/map.tsv" >> "$D5/dup"; mv "$D5/dup" "$D5/map.tsv"
expect_fail "duplicate map entry" "not unique" "$MIRROR" "$D5"

# --- 6. a map SOURCE outside the resolved checkout ------------------------
D6="$TMP/d6"; mk_derived "$D6"
outside="$TMP/outside.sv"; cp "$(cut -f1 "$D6/map.tsv" | head -1)" "$outside"
awk -F'\t' -v o="$outside" 'NR==1{print o"\t"$2; next}{print}' OFS='\t' "$D6/map.tsv" > "$D6/m2"
mv "$D6/m2" "$D6/map.tsv"
expect_fail "map source outside the checkout" "not inside the resolved COREET_ROOT" "$MIRROR" "$D6"

# --- 7. a TRACKED modification under a relevant input path ----------------
D7="$TMP/d7"; mk_derived "$D7"
T7="$(cut -f1 "$D7/map.tsv" | head -1)"
printf '\n// tracked edit by the control\n' >> "$T7"
expect_fail "tracked modification under hw/" "are not clean at" "$MIRROR" "$D7"
( cd "$MIRROR" && git checkout -q -- . )

# --- 8. an UNTRACKED file under a relevant input path ---------------------
D8="$TMP/d8"; mk_derived "$D8"
touch "$MIRROR/hw/stray_untracked_input.sv"
expect_fail "untracked file under hw/" "are not clean at" "$MIRROR" "$D8"
rm -f "$MIRROR/hw/stray_untracked_input.sv"

# --- 9. an untracked file OUTSIDE the relevant paths is NOT a failure -----
# This is the `core_et_blocks.txt` case: unrelated retained evidence that must
# be counted and reported, never deleted, and never silently called clean.
D9="$TMP/d9"; mk_derived "$D9"
touch "$MIRROR/unrelated_retained_evidence.txt"
out="$(run "$MIRROR" "$D9")"; rc=$?
if [[ "$rc" -ne 0 ]]; then
  bad "an untracked file OUTSIDE hw//dv/ was treated as a failure: $(head -1 <<<"$out")"
elif [[ "$(grep -m1 '^coreet_outside_scope_untracked=' <<<"$out" | cut -d= -f2)" != "1" ]]; then
  bad "the outside-untracked file was not counted: $(grep -m1 'outside_scope' <<<"$out")"
elif ! grep -q 'NOT claimed porcelain-clean' <<<"$out"; then
  bad "provenance does not state the narrower cleanliness scope"
elif [[ ! -f "$MIRROR/unrelated_retained_evidence.txt" ]]; then
  bad "the preflight deleted an untracked file"
else
  ok "untracked file outside hw//dv/: accepted, counted as 1, scope stated, file untouched"
fi
rm -f "$MIRROR/unrelated_retained_evidence.txt"

# --- 10. a non-git root ----------------------------------------------------
D10="$TMP/d10"; mk_derived "$D10"
mkdir -p "$TMP/notgit"
# NOTE: $TMP/notgit sits INSIDE this worktree, which is itself a git repo, so
# `rev-parse HEAD` there answers with the D4 revision.  The preflight must
# therefore require the root to be its own work-tree TOP LEVEL, not merely to be
# inside some repository.  This control caught that gap.
expect_fail "directory inside another repo, not a checkout root" "is not a git checkout root" "$TMP/notgit" "$D10"

# --- 12. a TRACKED modification under mk/ --------------------------------
# The gap this pins: `mk` was not in the checked set, so a tracked edit there
# passed AND was then miscounted under a field whose name said "untracked".
D12="$TMP/d12"; mk_derived "$D12"
printf '\n# tracked edit by the control\n' >> "$MIRROR/mk/rtlcosim.mk"
expect_fail "tracked modification under mk/" "are not clean at" "$MIRROR" "$D12"
( cd "$MIRROR" && git checkout -q -- . )

# --- 13. an UNTRACKED file under mk/ -------------------------------------
D13="$TMP/d13"; mk_derived "$D13"
touch "$MIRROR/mk/stray_fragment.mk"
expect_fail "untracked file under mk/" "are not clean at" "$MIRROR" "$D13"
rm -f "$MIRROR/mk/stray_fragment.mk"

# --- 14. out-of-scope state is counted ACCURATELY -------------------------
# A TRACKED modification outside the relevant paths must be reported as
# tracked, never folded into an "untracked" count.
D14="$TMP/d14"; mk_derived "$D14"
# COMMIT the file first, THEN modify it, so this is genuinely a TRACKED
# modification -- appending to a non-existent path would merely have created
# another untracked file and the control would have proved nothing.
printf 'seed\n' > "$MIRROR/README_probe.txt"
( cd "$MIRROR" && git add -A && git commit -qm "seed an out-of-scope tracked file" )
printf 'changed\n' >> "$MIRROR/README_probe.txt"
touch "$MIRROR/unrelated_untracked.txt"
out="$(run "$MIRROR" "$D14")"; rc=$?
tr_n="$(grep -m1 '^coreet_outside_scope_tracked=' <<<"$out" | cut -d= -f2)"
un_n="$(grep -m1 '^coreet_outside_scope_untracked=' <<<"$out" | cut -d= -f2)"
if [[ "$rc" -ne 0 ]]; then
  bad "out-of-scope state was treated as a failure: $(head -1 <<<"$out")"
elif [[ "$tr_n" != "1" || "$un_n" != "1" ]]; then
  bad "out-of-scope counts wrong: tracked=$tr_n untracked=$un_n, expected 1 and 1"
else
  ok "out-of-scope state: 1 tracked and 1 untracked, counted separately and accurately"
fi
rm -f "$MIRROR/unrelated_untracked.txt"
( cd "$MIRROR" && git checkout -q -- . )

# --- 15. a derived BODY mutation with the source header INTACT -----------
# The header check alone cannot see this; only a byte comparison against a
# fresh derivation can.
D15="$TMP/d15"; mk_derived "$D15"
printf '\n// appended body bytes, the sha256(source) header left untouched\n' >> "$D15/vpu_defs_pkg.sv"
grep -q '^// sha256(source): ' "$D15/vpu_defs_pkg.sv" \
  || bad "the mutation control removed the header, so it tests the wrong thing"
expect_fail "derived body mutated, header intact" "NOT this generator's output" "$MIRROR" "$D15"

# --- 15b. A CLEAN, BYTE-IDENTICAL SOURCE ALIAS must be refused -----------
# The gap this pins: the staged comparison authenticated the four .sv BODIES but
# not `map.tsv`.  A committed, byte-identical COPY of a mapped source at another
# path inside the checkout satisfies every structural check -- four unique
# two-field rows, readable, contained, matching source-hash header -- and no
# generated body is touched.  But it is not the generator's association, and
# `coreet_filelist.sh::subst_of` looks up the EXACT original path, so the real
# derived source would silently be skipped.
D15b="$TMP/d15b"; mkdir -p "$D15b"
if ! "$PRE" "$MIRROR" "$D15b" "$PATCHER" --publish >/dev/null 2>&1; then
  bad "could not publish a clean set for the alias control"
else
  alias_dir="$MIRROR/aliases"            # deliberately OUTSIDE hw/ dv/ mk/
  mkdir -p "$alias_dir"
  victim_src="$(cut -f1 "$D15b/map.tsv" | head -1)"
  cp "$victim_src" "$alias_dir/$(basename "$victim_src")"
  ( cd "$MIRROR" && git add -A && git commit -qm "commit a byte-identical alias outside the relevant paths" )
  alias_src="$(readlink -f "$alias_dir/$(basename "$victim_src")")"
  cmp -s "$victim_src" "$alias_src" || bad "the alias is not byte-identical, so it tests the wrong thing"
  awk -F'\t' -v a="$alias_src" 'NR==1{print a"\t"$2; next}{print $1"\t"$2}' OFS='\t' "$D15b/map.tsv" > "$D15b/m2"
  mv "$D15b/m2" "$D15b/map.tsv"
  out="$(run "$MIRROR" "$D15b")"; rc=$?
  if [[ "$rc" -eq 0 ]]; then
    bad "a forged map key (clean byte-identical alias) was ACCEPTED"
  elif ! grep -q "NOT this generator's map" <<<"$out"; then
    bad "the alias was refused for the wrong reason: $(grep -m1 FATAL <<<"$out" | cut -c1-110)"
  else
    ok "clean byte-identical source alias in the map: refused as not the generator's map"
  fi
fi

# --- 16. --publish derives into the EXACT consumed directory -------------
D16="$TMP/d16"; mkdir -p "$D16"
out="$("$PRE" "$MIRROR" "$D16" "$PATCHER" --publish 2>&1)"; rc=$?
if [[ "$rc" -ne 0 ]]; then
  bad "--publish into an empty consumed dir failed: $(head -1 <<<"$out")"
elif [[ "$(grep -m1 '^coreet_derived_dir=' <<<"$out" | cut -d= -f2)" != "$(readlink -f "$D16")" ]]; then
  bad "--publish reported a different derived dir than the one consumed"
elif [[ "$(cut -f2 "$D16/map.tsv" | grep -c "^$D16/")" -ne 4 ]]; then
  bad "the map does not point at the consumed directory: $(head -1 "$D16/map.tsv")"
elif ! grep -q 'regenerated in place' <<<"$out"; then
  bad "--publish did not record that it regenerated in place"
else
  # and the published bytes survive a plain verify
  "$PRE" "$MIRROR" "$D16" "$PATCHER" >/dev/null 2>&1 \
    && ok "--publish derives the four files into the exact consumed dir; verify then passes" \
    || bad "the published set does not pass a plain verify"
fi

# --- 17. an ambient non-generic COREET_TECH is refused -------------------
D17="$TMP/d17"; mk_derived "$D17"
out="$(COREET_TECH=xilinx "$PRE" "$MIRROR" "$D17" "$PATCHER" 2>&1)"; rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "an ambient COREET_TECH=xilinx was accepted"
elif ! grep -q 'canonical CORE-ET flow is COREET_TECH=generic' <<<"$out"; then
  bad "non-generic tech refused for the wrong reason: $(head -1 <<<"$out")"
else
  ok "ambient COREET_TECH=xilinx: refused, never silently overridden"
fi
out="$(COREET_TECH=generic "$PRE" "$MIRROR" "$D17" "$PATCHER" 2>&1)"; rc=$?
if [[ "$rc" -ne 0 ]] || [[ "$(grep -m1 '^coreet_tech=' <<<"$out" | cut -d= -f2)" != "generic" ]]; then
  bad "an explicit COREET_TECH=generic was not accepted and recorded"
else
  ok "COREET_TECH=generic: accepted and recorded in provenance"
fi

# --- 10b. THE SWEEP ITSELF must refuse when the preflight refuses ---------
SWEEP="$ROOT/scripts/run_d4_runtime_sweep.sh"
if [[ ! -x "$SWEEP" ]]; then
  bad "no sweep at $SWEEP"
else
  printf 'debug_breakpoint\n' > "$TMP/one.list"
  out="$TMP/sweep_refuse"
  ( cd "$ROOT" && env "COREET_ROOT=$TMP/notgit" "$SWEEP" coreet "$TMP/one.list" "$out" ) >"$TMP/sweep.log" 2>&1
  rc=$?
  if [[ "$rc" -eq 0 ]]; then
    bad "the sweep ran with an unpinnable COREET_ROOT"
  elif ! grep -q 'CORE-ET preflight failed' "$TMP/sweep.log"; then
    bad "the sweep failed for the wrong reason: $(head -1 "$TMP/sweep.log")"
  elif [[ -s "$out/rows.tsv" ]]; then
    bad "the sweep still produced rows"
  else
    ok "sweep with an unpinnable COREET_ROOT: FATAL before any block, no rows"
  fi
fi

# --- 11. the real checkout and derived dir were not mutated ---------------
if [[ -n "$(git -C "$REAL_ROOT" status --porcelain --untracked-files=no -- hw dv 2>/dev/null)" ]]; then
  bad "the REAL CORE-ET checkout has tracked modifications after these controls"
elif [[ ! -f "$REAL_ROOT/core_et_blocks.txt" ]]; then
  bad "the real checkout's untracked core_et_blocks.txt is gone"
else
  ok "the real CORE-ET checkout is untouched, core_et_blocks.txt preserved"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

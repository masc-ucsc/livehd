#!/usr/bin/env bash
# Controls for the CVA6 foundation: fresh publication, the clean-named-source
# binding, the wrapper corpus, and the manifest.
#
# Each of these pins a defect that a weaker version of the harness let through:
#
#   1  unset CVA6_ROOT                 FATAL before any block
#   2  CVA6_ROOT without the config    FATAL naming the config package
#   3  fake root WITH the config       FATAL -- a non-git root holding only a
#                                      COPY of the config package once ran to
#                                      completion and wrote reconciled=YES with
#                                      cva6_root_rev=not-a-git-checkout
#   4  wrong revision in the manifest  FATAL before rows
#   5  swapped wrapper in the corpus   FATAL on the corpus hash
#   6  dirty tracked CVA6 source       the BUILDER refuses: a dirty tree at
#                                      revision R is not R
#   7  planted stale wrapper           reported by its CURRENT status, never
#                                      "GENERATED because a file exists", and
#                                      never published
#   8  wrapper corpus self-consistency no port names a type the wrapper renamed
#   9  manifest + module lists         positive control
#  10  the real foundation             positive control: binding passes, block runs
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
SWEEP="$ROOT/scripts/run_d4_runtime_sweep.sh"
BUILD="$ROOT/scripts/build_cva6_foundation.sh"
WRAPDIR="$ROOT/generated/cva6_wrappers"
MAN="$ROOT/generated/cva6_foundation/provenance.txt"
L78="$HERE/cva6_78.list"
L30="$HERE/cva6_30.list"
TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
REAL_ROOT="${CVA6_ROOT:-/HOMEROOT/projects/cva6-blockfinding}"
FL="$ROOT/generated/cva6_filelists/${TARGET}.flistplus.f"

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/cva6_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
printf 'csr_buffer\n' > "$TMP/one.list"

have_foundation=1
[[ -s "$FL" && -d "$WRAPDIR" && -s "$MAN" ]] || have_foundation=0
[[ "$have_foundation" -eq 1 ]] || bad "no foundation to test (run scripts/build_cva6_foundation.sh)"

# sweep_must_fail <label> <outdir> <expected-substring> <env...>
sweep_must_fail() {
  local label="$1" out="$2" want="$3"; shift 3
  ( cd "$ROOT" && env "$@" "$SWEEP" cva6 "$TMP/one.list" "$out" ) >"$out.log" 2>&1
  local rc=$?
  if [[ "$rc" -eq 0 ]]; then
    bad "$label exited 0"
  elif ! grep -q "$want" "$out.log"; then
    bad "$label failed for the wrong reason: $(head -2 "$out.log" | tr '\n' ' ')"
  elif [[ -s "$out/rows.tsv" ]]; then
    bad "$label still produced rows"
  elif grep -qs 'reconciled=YES' "$out/provenance.txt"; then
    bad "$label still claimed reconciled coverage"
  else
    ok "$label: FATAL before any block, no rows"
  fi
}

if [[ "$have_foundation" -eq 1 ]]; then
  # --- 1. unset CVA6_ROOT ---------------------------------------------------
  sweep_must_fail "unset CVA6_ROOT" "$TMP/o1" 'cva6 needs CVA6_ROOT' \
    -u CVA6_ROOT "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR"

  # --- 2. CVA6_ROOT with no config package ---------------------------------
  mkdir -p "$TMP/emptyroot/core/include"
  sweep_must_fail "CVA6_ROOT without the config package" "$TMP/o2" 'no config package at' \
    "CVA6_ROOT=$TMP/emptyroot" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR"

  # --- 3. fake root WITH the expected config, non-git ----------------------
  # The exact setup that previously ran to completion.
  mkdir -p "$TMP/fakeroot/core/include"
  cp "$REAL_ROOT/core/include/${TARGET}_config_pkg.sv" "$TMP/fakeroot/core/include/" 2>/dev/null
  if [[ ! -r "$TMP/fakeroot/core/include/${TARGET}_config_pkg.sv" ]]; then
    bad "could not stage the fake-root control (no real config package to copy)"
  else
    sweep_must_fail "fake root WITH the config package" "$TMP/o3" 'foundation mismatch: CVA6_ROOT canonical path' \
      "CVA6_ROOT=$TMP/fakeroot" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR"
  fi

  # --- 4. wrong revision ----------------------------------------------------
  sed 's/^cva6_rev=.*/cva6_rev=0123456789abcdef0123456789abcdef01234567/' "$MAN" > "$TMP/man_rev.txt"
  sweep_must_fail "manifest naming a different revision" "$TMP/o4" 'foundation mismatch: CVA6 revision' \
    "CVA6_ROOT=$REAL_ROOT" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR" "CVA6_FOUNDATION=$TMP/man_rev.txt"

  # --- 5. one wrapper swapped anywhere in the corpus ------------------------
  cp -r "$WRAPDIR" "$TMP/wraps"
  victim="$(ls "$TMP/wraps"/*_gate.sv 2>/dev/null | head -1)"
  if [[ -z "$victim" ]]; then
    bad "could not stage the swapped-wrapper control"
  else
    printf '\n// planted by cva6_foundation_controls.sh\n' >> "$victim"
    sed "s|^wrapper_dir=.*|wrapper_dir=$TMP/wraps|" "$MAN" > "$TMP/man_wrap.txt"
    sweep_must_fail "one wrapper changed in the corpus ($(basename "$victim"))" "$TMP/o5" \
      'foundation mismatch: wrapper corpus sha256' \
      "CVA6_ROOT=$REAL_ROOT" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$TMP/wraps" "CVA6_FOUNDATION=$TMP/man_wrap.txt"
  fi
fi

# --- 6. dirty TRACKED source, at the builder -------------------------------
# A real git repo, so the check under test is "dirty", not "not a checkout".
dr="$TMP/dirtyroot"
mkdir -p "$dr/core/include"
( cd "$dr" && git init -q . && git config user.email c@x && git config user.name c )
echo "// placeholder" > "$dr/core/include/${TARGET}_config_pkg.sv"
( cd "$dr" && git add -A && git commit -qm init )
echo "// dirtied by the control" >> "$dr/core/include/${TARGET}_config_pkg.sv"
( cd "$ROOT" && env "CVA6_ROOT=$dr" "FOUNDATION_OUT=$TMP/fo_dirty" "$BUILD" "$L78" ) >"$TMP/dirty.log" 2>&1
rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "the builder accepted a CVA6 checkout with tracked modifications"
elif ! grep -q 'has TRACKED modifications' "$TMP/dirty.log"; then
  bad "dirty-source control failed for the wrong reason: $(head -2 "$TMP/dirty.log" | tr '\n' ' ')"
elif [[ -d "$TMP/fo_dirty/cva6_wrappers" ]]; then
  bad "the builder published a corpus from a dirty source"
else
  ok "dirty tracked CVA6 source: the builder refuses, publishes nothing"
fi

# --- 7. planted stale wrapper ----------------------------------------------
# A declared module the generator will SKIP, with a file of that name already in
# place.  Status must come from THIS invocation, and the file must not survive.
if [[ "$have_foundation" -eq 1 ]]; then
  printf 'csr_buffer\nras\nghost_not_a_cva6_module\n' > "$TMP/ghost.list"
  mkdir -p "$TMP/fo_ghost/cva6_wrappers"
  printf '// PLANTED STALE WRAPPER -- must never be reported as generated\n' \
    > "$TMP/fo_ghost/cva6_wrappers/ghost_not_a_cva6_module_gate.sv"
  ( cd "$ROOT" && env "CVA6_ROOT=$REAL_ROOT" "FOUNDATION_OUT=$TMP/fo_ghost" "$BUILD" "$TMP/ghost.list" ) \
    >"$TMP/ghost.log" 2>&1
  rc=$?
  gstatus="$(awk -F'\t' '$1=="ghost_not_a_cva6_module"{print $2}' "$TMP/fo_ghost/cva6_foundation/wrappers.tsv" 2>/dev/null)"
  if [[ "$rc" -ne 0 ]]; then
    bad "planted-stale control: the builder failed outright: $(tail -2 "$TMP/ghost.log" | tr '\n' ' ')"
  elif [[ "$gstatus" == "GENERATED" ]]; then
    bad "a planted stale wrapper was reported GENERATED"
  elif [[ -e "$TMP/fo_ghost/cva6_wrappers/ghost_not_a_cva6_module_gate.sv" ]]; then
    bad "a planted stale wrapper survived publication"
  elif [[ -z "$gstatus" ]]; then
    bad "planted-stale control: no status row for the ghost module"
  elif [[ "$(grep -c '^GENERATED' <(awk -F'\t' 'NR>1{print $2}' "$TMP/fo_ghost/cva6_foundation/wrappers.tsv"))" -ne 2 ]]; then
    bad "planted-stale control: expected exactly 2 real wrappers in the scratch corpus"
  else
    ok "planted stale wrapper: reported $gstatus from this invocation, not published"
  fi
fi

# --- 8. wrapper corpus self-consistency ------------------------------------
if [[ ! -d "$WRAPDIR" ]]; then
  bad "no wrapper corpus at $WRAPDIR"
else
  n_w=0; n_bad=0; first=""
  for w in "$WRAPDIR"/*_gate.sv; do
    [[ -r "$w" ]] || continue
    n_w=$((n_w + 1))
    mapfile -t renamed < <(grep -oP '^\s*\}?\s*\K[A-Za-z_][A-Za-z0-9_]*__r(?=\s*;)' "$w" \
                           | sed 's/__r$//' | sort -u)
    for t in "${renamed[@]}"; do
      [[ -n "$t" ]] || continue
      if grep -qP "^\s*(input|output)\s+${t}\b(?!__r)" "$w"; then
        n_bad=$((n_bad + 1)); [[ -z "$first" ]] && first="$(basename "$w"): $t"
        break
      fi
    done
  done
  if [[ "$n_w" -eq 0 ]]; then
    bad "wrapper corpus is empty"
  elif [[ "$n_bad" -ne 0 ]]; then
    bad "$n_bad/$n_w wrappers declare <T>__r but declare a port as <T>  (first: $first)"
  else
    ok "wrapper corpus: $n_w wrappers, none names a type it renamed"
  fi
fi

# --- 9. manifest + module lists --------------------------------------------
n78="$(grep -cv '^[[:space:]]*\(#.*\)\?$' "$L78" 2>/dev/null)"
n30="$(grep -cv '^[[:space:]]*\(#.*\)\?$' "$L30" 2>/dev/null)"
not_in_78="$(comm -23 <(sort -u "$L30") <(sort -u "$L78") | wc -l)"
gen30=0
if [[ -s "$ROOT/generated/cva6_foundation/wrappers.tsv" ]]; then
  gen30="$(awk -F'\t' 'NR==FNR{want[$1];next} ($1 in want) && $2=="GENERATED"' \
            "$L30" "$ROOT/generated/cva6_foundation/wrappers.tsv" | wc -l)"
fi
if [[ "$n78" != "78" || "$n30" != "30" ]]; then
  bad "module lists are $n78 / $n30, not 78 / 30"
elif [[ "$not_in_78" != "0" ]]; then
  bad "$not_in_78 of the canonical 30 are not in the authoritative 78"
elif [[ ! -s "$MAN" ]]; then
  bad "no foundation manifest at $MAN"
elif ! grep -q '^cva6_rev=[0-9a-f]\{40\}$' "$MAN"; then
  bad "manifest does not pin a CVA6 revision"
elif ! grep -q '^cva6_tracked_clean=yes$' "$MAN"; then
  bad "manifest does not record the clean-source check"
elif ! grep -q '^wrappers_sha256=[0-9a-f]\{64\}$' "$MAN"; then
  bad "manifest does not pin the wrapper corpus"
elif grep -q 'livehd-new' "$MAN"; then
  bad "manifest references another checkout"
elif [[ "$gen30" != "30" ]]; then
  bad "only $gen30 of the canonical 30 have a generated wrapper"
else
  ok "78/30 lists consistent; manifest pins rev $(grep -m1 '^cva6_rev=' "$MAN" | cut -c10-17), clean source, corpus; 30/30 wrappers"
fi

# --- 10. positive control ---------------------------------------------------
if [[ "$have_foundation" -eq 1 ]]; then
  out="$TMP/pos"
  ( cd "$ROOT" && env "CVA6_ROOT=$REAL_ROOT" "CVA6_FILELIST=$FL" "CVA6_WRAP_DIR=$WRAPDIR" \
      "$SWEEP" cva6 "$TMP/one.list" "$out" ) >"$TMP/pos.log" 2>&1
  rc=$?
  nf="$(tail -n +2 "$out/rows.tsv" 2>/dev/null | awk -F'\t' '{print NF}' | sort -u | tr '\n' ' ')"
  st="$(tail -n +2 "$out/rows.tsv" 2>/dev/null | cut -f13)"
  if [[ "$rc" -ne 0 ]]; then
    bad "positive control failed: $(tail -2 "$TMP/pos.log" | tr '\n' ' ')"
  elif [[ "$nf" != "18 " || "$st" != "RAN" ]]; then
    bad "positive control row: fields='$nf' status='$st'"
  else
    ok "real foundation: binding passes, csr_buffer gives one 18-field RAN row"
  fi
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

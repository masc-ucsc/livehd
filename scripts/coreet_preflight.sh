#!/usr/bin/env bash
# Bind a CORE-ET run to the EXTERNAL corpus it is actually measuring.
#
# Three things this exists to stop, each found by review:
#
#   1. CLEANLINESS SCOPED TOO NARROWLY.  Every `dv/rtlcosim/*/Makefile` includes
#      `mk/rtlcosim.mk`, which includes `mk/prim.mk`, to derive NEW_RTL_FINAL.
#      With `mk` outside the checked set a tracked edit there passed, and was
#      then miscounted under a field whose name said "untracked".
#   2. NO PROOF THE DERIVED FILES CAME FROM THE RECORDED PATCHER.  Checking the
#      16-hex source-hash header the generator embeds is not enough: a derived
#      BODY can be edited with that header left intact.  The four files are now
#      regenerated into fresh staging and compared BYTE FOR BYTE.
#   3. AN AMBIENT, UNRECORDED CORPUS INPUT.  `COREET_TECH` selects which tech
#      cells the filelist picks (`generic` vs `xilinx` change the prim_clk_gate
#      sources).  Both succeed, and nothing recorded which was used.
#
# WHAT IS NOT CLAIMED.  The checkout is NOT asserted to be porcelain-clean:
# `core_et_blocks.txt` sits untracked at its root and is unrelated retained
# evidence, which this must neither delete nor misreport.  Out-of-scope state is
# reported with tracked and untracked counted SEPARATELY.
#
#   coreet_preflight.sh <coreet-root> <derived-dir> <patch-script> [--publish]
#
# --publish regenerates INTO the directory the filelist consumes.  Without it
# the consumed directory is only verified, and any byte that is not the
# generator's current output is fatal.
set -uo pipefail
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

RAW_ROOT="${1:?usage: coreet_preflight.sh <coreet-root> <derived-dir> <patch-script> [--publish]}"
DERIVED_IN="${2:?missing derived dir}"
PATCHER="${3:?missing patch script}"
MODE="${4:-}"

# RELEVANT INPUT PATHS -- every path the filelist flow reads.
#   hw  RTL and per-IP dv Makefiles
#   dv  dv/rtlcosim/<module>/Makefile
#   mk  rtlcosim.mk -> prim.mk -> minion_vpu_sources.mk, included by the above
RELEVANT=(hw dv mk)

# THE CANONICAL TECH.  An ambient setting must not silently change a canonical
# run, so a different value is REFUSED rather than quietly overridden.
CANON_TECH=generic
TECH="${COREET_TECH:-$CANON_TECH}"
[[ "$TECH" == "$CANON_TECH" ]] \
  || die "COREET_TECH=$TECH would change which tech cells the filelist selects; the canonical CORE-ET flow is COREET_TECH=$CANON_TECH. Unset it or set it to $CANON_TECH."

# --- the checkout, resolved ONCE -------------------------------------------
[[ -d "$RAW_ROOT" ]] || die "COREET_ROOT '$RAW_ROOT' is not a directory"
ROOT="$(readlink -f "$RAW_ROOT")" || die "cannot resolve COREET_ROOT '$RAW_ROOT'"
REV="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || true)"
[[ "$REV" =~ ^[0-9a-f]{40}$ ]] \
  || die "COREET_ROOT $ROOT is not a git checkout; the corpus revision must be nameable"
# A directory that merely SITS INSIDE another repository answers `rev-parse
# HEAD` with THAT repository's revision, so a run would record a revision
# belonging to a different project.
TOP="$(git -C "$ROOT" rev-parse --show-toplevel 2>/dev/null || true)"
[[ "$(readlink -f "${TOP:-/nonexistent}")" == "$ROOT" ]] \
  || die "COREET_ROOT $ROOT is not a git checkout root; it lies inside ${TOP:-<unknown>}, whose revision is not this corpus's"

DIRTY="$(git -C "$ROOT" status --porcelain --untracked-files=all -- "${RELEVANT[@]}" 2>/dev/null)"
[[ -z "$DIRTY" ]] || die "CORE-ET inputs under ${RELEVANT[*]} are not clean at $REV:
$(sed 's/^/       /' <<<"$DIRTY" | head -10)"

# Out-of-scope state, counted ACCURATELY.  A tracked modification is never
# reported as untracked.
OUT_ALL="$(git -C "$ROOT" status --porcelain --untracked-files=all 2>/dev/null \
            | grep -v -E "^.. ($(IFS='|'; echo "${RELEVANT[*]}"))/" || true)"
OUT_UNTRACKED="$(grep -c '^??' <<<"${OUT_ALL:-}" || true)"
OUT_TRACKED="$(grep -cv '^??' <<<"${OUT_ALL:-}" || true)"
[[ -n "${OUT_ALL:-}" ]] || { OUT_UNTRACKED=0; OUT_TRACKED=0; }

# --- derive FRESH, then compare byte for byte ------------------------------
[[ -r "$PATCHER" ]] || die "missing patch script $PATCHER"
DERIVED="$(readlink -f "$DERIVED_IN" 2>/dev/null || echo "$DERIVED_IN")"
EXPECTED=(intpipe_mul_div_ctl.sv txfmactl_top.sv txfmaexp_top.sv vpu_defs_pkg.sv)

if [[ "$MODE" == "--publish" ]]; then
  # The generator targets the EXACT directory the filelist consumes.
  mkdir -p "$DERIVED" || die "cannot create $DERIVED"
  COREET_ROOT="$ROOT" COREET_PATCHED_OUTDIR="$DERIVED" python3 -B "$PATCHER" >/dev/null 2>&1 \
    || die "coreet_patch_srcs.py failed against $ROOT"
fi
[[ -d "$DERIVED" ]] || die "no derived source directory at $DERIVED"

STAGE="$(mktemp -d "$(dirname "$DERIVED")/.coreet_stage.XXXXXX")" \
  || die "cannot create project-local staging beside $DERIVED"
trap 'rm -rf "$STAGE"' EXIT
COREET_ROOT="$ROOT" COREET_PATCHED_OUTDIR="$STAGE" python3 -B "$PATCHER" >/dev/null 2>&1 \
  || die "coreet_patch_srcs.py failed deriving into staging from $ROOT"

# EXACTLY the four outputs plus the map -- no more, no fewer.
mapfile -t PRESENT < <(cd "$DERIVED" && ls -1 | sort)
WANT="$(printf '%s\n' "${EXPECTED[@]}" map.tsv | sort)"
[[ "$(printf '%s\n' "${PRESENT[@]}")" == "$WANT" ]] \
  || die "derived dir $DERIVED does not hold exactly the four outputs plus map.tsv:
       present: $(printf '%s ' "${PRESENT[@]}")
       wanted:  $(tr '\n' ' ' <<<"$WANT")"

# BYTE FOR BYTE against a fresh derivation.  This is what the embedded header
# could not do: a body edit with the header left intact is caught here.
for f in "${EXPECTED[@]}"; do
  [[ -r "$DERIVED/$f" ]] || die "derived output unreadable: $DERIVED/$f"
  cmp -s "$DERIVED/$f" "$STAGE/$f" \
    || die "derived file $f is NOT this generator's output for $ROOT (differs from a fresh derivation). Re-run the preflight with --publish."
done

MAP="$DERIVED/map.tsv"
[[ -r "$MAP" ]] || die "map $MAP is unreadable"
n_map="$(grep -cv '^[[:space:]]*$' "$MAP")"
[[ "$n_map" -eq 4 ]] || die "map has $n_map entries, expected 4"
[[ "$(cut -f1 "$MAP" | sort -u | wc -l)" -eq 4 ]] || die "map sources are not unique"
[[ "$(cut -f2 "$MAP" | sort -u | wc -l)" -eq 4 ]] || die "map outputs are not unique"
[[ "$(awk -F'\t' 'NF!=2' "$MAP" | wc -l)" -eq 0 ]] || die "map has a line that is not exactly two tab-separated fields"
while IFS=$'\t' read -r src out; do
  [[ -n "$src" && -n "$out" ]] || die "map has an empty field"
  rsrc="$(readlink -f "$src" 2>/dev/null || true)"
  rout="$(readlink -f "$out" 2>/dev/null || true)"
  [[ -r "$rsrc" ]] || die "map source is unreadable: $src"
  [[ -r "$rout" ]] || die "map output is unreadable: $out"
  [[ "$rsrc" == "$ROOT"/* ]] || die "map source is not inside the resolved COREET_ROOT $ROOT: $rsrc"
  [[ "$rout" == "$DERIVED"/* ]] || die "map output is not inside the derived dir the filelist consumes ($DERIVED): $rout"
  emb="$(grep -m1 -oP '^// sha256\(source\): \K[0-9a-f]+' "$rout" || true)"
  [[ -n "$emb" ]] || die "derived file records no source hash: $rout"
  now="$(sha256sum "$rsrc" | cut -c1-${#emb})"
  [[ "$emb" == "$now" ]] \
    || die "derived file is STALE: $(basename "$rout") records source sha256 $emb, but $rsrc is now $now"
done < <(grep -v '^[[:space:]]*$' "$MAP")

STAGE_MAP="$STAGE/map.tsv"
[[ -r "$STAGE_MAP" ]] || die "fresh staging produced no map at $STAGE_MAP"

# THE MAP IS AUTHENTICATED AGAINST THE GENERATOR'S OWN MAP, not merely against
# structural predicates.
#
# Structure alone is not enough.  A clean, byte-identical COPY of a mapped
# source at another path inside the checkout satisfies every structural check --
# four unique two-field rows, readable paths, containment, a matching
# source-hash header -- while the association it records is not the generator's.
# That matters concretely: `coreet_filelist.sh::subst_of` looks up the EXACT
# original filelist path, so a forged key silently skips the substitution and
# the block compiles the unpatched source.
#
# The only legitimate difference between the two maps is the output DIRECTORY
# ($STAGE vs $DERIVED), so that prefix is stripped deliberately and the rest --
# the exact source paths, their order, and their source-to-output-basename
# associations -- must match exactly.
map_norm() {  # map_norm <output-dir-prefix> <map>
  awk -F'\t' -v p="$1/" '
    NF==2 { o=$2; if (index(o,p)==1) o=substr(o,length(p)+1); print $1 "\t" o }
  ' "$2"
}
if ! diff -q <(map_norm "$DERIVED" "$MAP") <(map_norm "$STAGE" "$STAGE_MAP") >/dev/null; then
  die "map.tsv is NOT this generator's map for $ROOT -- the source/output associations differ:
$(diff <(map_norm "$DERIVED" "$MAP") <(map_norm "$STAGE" "$STAGE_MAP") | head -8 | sed 's/^/       /')
       Re-run the preflight with --publish."
fi

AGG="$(for f in "${EXPECTED[@]}"; do printf '%s %s\n' "$f" "$(sha256sum < "$DERIVED/$f" | cut -d' ' -f1)"; done | sha256sum | cut -d' ' -f1)"

cat <<EOF
coreet_root=$RAW_ROOT
coreet_root_canonical=$ROOT
coreet_rev=$REV
coreet_tech=$TECH
coreet_relevant_paths=${RELEVANT[*]}
coreet_relevant_inputs_clean=yes
coreet_outside_scope_tracked=$OUT_TRACKED
coreet_outside_scope_untracked=$OUT_UNTRACKED
coreet_clean_scope=tracked AND untracked under ${RELEVANT[*]} only; the checkout as a whole is NOT claimed porcelain-clean
coreet_patch_script=$PATCHER
coreet_patch_script_sha256=$(sha256sum < "$PATCHER" | cut -d' ' -f1)
coreet_derived_dir=$DERIVED
coreet_derived_mode=$([[ "$MODE" == "--publish" ]] && echo "regenerated in place; four outputs AND map then authenticated against a second fresh derivation" || echo "four outputs AND map authenticated byte-for-byte against a fresh derivation")
coreet_derived_map_sha256=$(sha256sum < "$MAP" | cut -d' ' -f1)
coreet_derived_aggregate_sha256=$AGG
coreet_derived_count=${#EXPECTED[@]}
EOF

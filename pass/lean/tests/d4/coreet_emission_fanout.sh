#!/usr/bin/env bash
# PER-ROW FANOUT INVENTORY for the rows whose emission a correction moved.
#
# WHY. The front-end A/B table compares emission HASHES, so when a row changes
# it says THAT something changed and never WHAT. A previous unit answered the
# "what" by eye, followed one net one hop, and reported twenty rows as dead
# logic; three of them reached a memory CLOCK and seventeen were live. This
# computes the answer instead, from the emitted netlists, with authenticated
# binaries on both sides.
#
# Usage: coreet_emission_fanout.sh <A-commit> <before-commit> <after-commit> \
#                                  <modules.list> <out.tsv>
#
# For each module it emits the normalized Verilog under all three binaries,
# takes the registers whose next-state expression MOVED between A and each side,
# and asks `emission_fanout.py` what that register reaches. A module the reader
# cannot fully parse is reported UNPARSED, never DEAD.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
# shellcheck source=d4_evidence_provenance.sh
. "$HERE/d4_evidence_provenance.sh" || { echo "FATAL: cannot source the provenance library" >&2; exit 2; }

A_IN="${1:?usage: coreet_emission_fanout.sh <A-commit> <before-commit> <after-commit> <modules.list> <out.tsv>}"
B_IN="${2:?missing before-commit}"
N_IN="${3:?missing after-commit}"
LIST="${4:?missing modules.list}"
OUT="${5:?missing out.tsv}"
CORPUS="${CORPUS:-$ROOT/temp/coreet_122_auth/blocks}"
BAZEL_CONFIG="${BAZEL_CONFIG:--c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15}"
PY="${PYTHON:-python3}"

SELF="${BASH_SOURCE[0]}"

cd "$ROOT" || d4_die "cannot cd $ROOT"

# ---- THE OUTPUT CONTRACT, settled before anything that can fail ------------
# A FAILED RERUN MUST NOT LEAVE THE PREVIOUS RUN'S TABLE at the path the caller
# asked THIS invocation to produce. Writing only to `$OUT.tmp.$$` and never
# touching `$OUT` meant a failing rerun returned nonzero while an old table sat
# there looking current -- and every negative control masked it by removing the
# path first. So the destination is settled here: it must be a regular file or
# absent, its directory must be writable, and any previous table is removed
# BEFORE the first thing that can fail.
[[ ! -e "$OUT" || ( -f "$OUT" && ! -L "$OUT" ) ]] \
  || d4_die "$OUT exists and is not a regular file; this producer publishes one table and will not write there"
OUTDIR="$(dirname "$OUT")"
[[ -d "$OUTDIR" && -w "$OUTDIR" ]] || d4_die "cannot write a table into $OUTDIR"
rm -f "$OUT" || d4_die "cannot clear the previous table at $OUT"
[[ ! -e "$OUT" ]] || d4_die "$OUT is still present after clearing it"

[[ -r "$LIST"   ]] || d4_die "no module list at $LIST"
[[ -d "$CORPUS" ]] || d4_die "no retained corpus at $CORPUS"
# The analyzer is addressed through one variable so the control suite can put a
# STUB in its place and exercise THIS script's real result handling -- a
# duplicate of the handling in a test proves nothing about the handling here.
# Whatever is used is hashed into the table's own provenance.
FANOUT_TOOL="${D4_FANOUT_TOOL:-$HERE/emission_fanout.py}"
[[ -r "$FANOUT_TOOL" ]] || d4_die "no fanout analyzer at $FANOUT_TOOL"

BINDIR="${D4_BINDING_DIR:-$ROOT/temp/d4_evidence_bin}"
mkdir -p "$BINDIR" || d4_die "cannot create $BINDIR"
declare -A BIN COMMIT
for side in A B N; do
  case $side in A) want="$A_IN";; B) want="$B_IN";; N) want="$N_IN";; esac
  c="$(git rev-parse --verify --quiet "$want^{commit}")" || d4_die "$want is not a commit here"
  COMMIT[$side]="$c"
  att="$BINDIR/$c.binding"
  "$HERE/d4_build_binding.sh" build "$c" "$att" $BAZEL_CONFIG >/dev/null \
    || d4_die "could not build side $side from $c"
  d4_binding_validate "$att"
  BIN[$side]="$(sed -n 's/^retained *= *//p' "$att" | head -1)"
  d4_bin_check "${BIN[$side]}" "$side"
done
[[ "${COMMIT[B]}" != "${COMMIT[N]}" ]] || d4_die "before and after name the same commit; this table would compare nothing"

mapfile -t MODS < <(grep -vE '^\s*(#|$)' "$LIST")
[[ "${#MODS[@]}" -gt 0 ]] || d4_die "$LIST names no modules"

W="$ROOT/temp/emission_fanout.$$"
mkdir -p "$W" || d4_die "cannot create $W"
trap 'rm -rf "$W"' EXIT

emit_one() { # <side> <module> -> prints the netlist path, or nothing
  # ONE `local` PER NAME, deliberately. `local a="$1" b="$a"` expands `$a` from
  # the OUTER scope, not from the assignment beside it: written as one statement
  # this took `side` from the leftover binding-loop variable and `m` from
  # nothing, so side A's emission landed in side N's directory and was then
  # overwritten by it. The table that produced compared N against B and called
  # it "before".
  local side="$1"
  local m="$2"
  local d="$W/$m/$side"
  rm -rf "$d"; mkdir -p "$d"
  "${BIN[$side]}" pass single_edge --top "$m" "lg:$CORPUS/$m/lgdb_raw" \
      --emit-dir "lg:$d/lg" --workdir "$d/w" >"$d/p.log" 2>&1 || return 1
  "${BIN[$side]}" compile "lg:$d/lg" --top "$m" --recipe O0 \
      --emit-dir "verilog:$d/v" --workdir "$d/vw" -q >"$d/e.log" 2>&1 || return 1
  [[ -f "$d/v/$m.v" ]] || return 1
  echo "$d/v/$m.v"
}

TMP="$OUT.tmp.$$"
: > "$TMP" || d4_die "cannot write $TMP"
trap 'rm -rf "$W" "$TMP"' EXIT
{
  echo "# schema          = d4-emission-fanout/1"
  echo "# A_source_commit = ${COMMIT[A]}"
  echo "# A_sha256        = $(d4_sha "${BIN[A]}")"
  echo "# before_commit   = ${COMMIT[B]}"
  echo "# before_sha256   = $(d4_sha "${BIN[B]}")"
  echo "# after_commit    = ${COMMIT[N]}"
  echo "# after_sha256    = $(d4_sha "${BIN[N]}")"
  echo "# tool_sha256     = $(d4_sha "$FANOUT_TOOL")"
  # THE PRODUCER'S OWN BYTES. `generated_by` is a path, and a path says nothing
  # about which version of this script produced a row: the fail-closed
  # behaviour that makes these verdicts worth reading lives HERE, not in the
  # analyzer, so the artifact has to be able to tell the corrected chain from
  # the one that published whatever it had.
  echo "# producer_sha256 = $(d4_sha "$SELF")"
  echo "# module_list     = $LIST"
  echo "# modules         = ${#MODS[@]}"
  echo "# corpus_identity = $(d4_corpus_identity "$CORPUS" "${MODS[@]}")"
  echo "# generated_by    = pass/lean/tests/d4/coreet_emission_fanout.sh"
  printf 'module\tchanged_before\tfanout_before\tchanged_after\tfanout_after\n'
} >> "$TMP"

for m in "${MODS[@]}"; do
  # A FAILED EVIDENCE RUN PUBLISHES NOTHING. Every arm below is `d4_die`, not a
  # row with a marker in it and a flag to check later: the previous shape
  # printed `EMIT-FAILED` / `TOOL-FAILED` / `UNPARSED` into the table, carried
  # on, and -- because the embedded reader ignored the analyzer's exit status --
  # could reach the end with status 0 and a published table full of
  # non-verdicts.
  pa="$(emit_one A "$m")" || d4_die "$m: side A produced no netlist"
  # THE PATHS MUST BE THE ONES ASKED FOR. A silent directory collision is how
  # this producer first compared the wrong pair and reported it as a result.
  [[ "$pa" == "$W/$m/A/v/$m.v" ]] || d4_die "side A emitted to $pa, not to $W/$m/A/v/$m.v"
  row=("$m")
  for side in B N; do
    ps="$(emit_one "$side" "$m")" || d4_die "$m: side $side produced no netlist"
    [[ "$ps" == "$W/$m/$side/v/$m.v" ]] || d4_die "side $side emitted to $ps, not to $W/$m/$side/v/$m.v"
    [[ "$ps" != "$pa" ]] || d4_die "side $side and side A share a netlist path; this row would compare nothing"
    res="$("$PY" - "$pa" "$ps" "$FANOUT_TOOL" <<'PYEOF'
import json, re, subprocess, sys

# The classes `emission_fanout.py` is allowed to report. Anything else is a tool
# this reader does not understand, not a result.
CLASSES = {"OUTPUT", "STATE", "MEM-CLOCK", "MEM-DATA", "MEM-CTRL", "INSTANCE", "DEAD"}
NEXT = re.compile(r"^\s*(\\[^\s]+\s|[A-Za-z_][\w$.]*)\s*=\s*(.*);$")

def nexts(p):
    d = {}
    for l in open(p, errors="replace"):
        m = NEXT.match(l.rstrip("\n"))
        if m and "___next_" in m.group(1):
            d[m.group(1).strip()] = m.group(2)
    return d

def die(code, msg):
    sys.stderr.write(f"fanout: {msg}\n")
    raise SystemExit(code)

a, b, tool = sys.argv[1], sys.argv[2], sys.argv[3]
na, nb = nexts(a), nexts(b)
changed = sorted(k for k in set(na) | set(nb) if na.get(k) != nb.get(k))
seeds = sorted({k.replace("___next_", "", 1) if k.startswith("___next_") else k.replace("\\___next_", "\\", 1)
                for k in changed})
if not seeds:
    print("0\tNONE")
    raise SystemExit(0)
r = subprocess.run([sys.executable, tool, b] + seeds, capture_output=True, text=True)
if r.returncode != 0:
    die(3, f"the analyzer exited {r.returncode} on {b}: {r.stdout.strip()[:300]} {r.stderr.strip()[:200]}")
try:
    d = json.loads(r.stdout)
except Exception as e:
    die(4, f"the analyzer printed no readable JSON on {b}: {e}: {r.stdout.strip()[:300]}")
if d.get("verdict") == "UNPARSED" or d.get("unparsed"):
    die(5, f"the analyzer could not parse {b}; no verdict may be claimed for it")
got = d.get("seeds")
if not isinstance(got, dict):
    die(6, f"the analyzer reported no seed map for {b}")
if set(got) != set(seeds):
    die(6, f"the analyzer answered for {sorted(got)} but was asked about {seeds}")
cls = sorted({c for v in got.values() for c in v})
unknown = [c for c in cls if c not in CLASSES]
if unknown:
    die(7, f"the analyzer reported unknown class(es) {unknown} on {b}")
if not cls:
    die(8, f"the analyzer reported an empty class set on {b}")
print(f"{len(seeds)}\t{','.join(cls)}")
PYEOF
)" || d4_die "$m [$side]: the fanout analyzer produced no usable verdict"
    nf="$(cut -f1 <<<"$res")"
    cl="$(cut -f2 <<<"$res")"
    [[ "$nf" =~ ^[0-9]+$ ]] || d4_die "$m [$side]: changed-register count '$nf' is not a number"
    [[ -n "$cl" && "$cl" != *$'\t'* ]] || d4_die "$m [$side]: fanout class '$cl' is not one field"
    row+=("$nf" "$cl")
  done
  [[ "${#row[@]}" -eq 5 ]] || d4_die "$m: produced ${#row[@]} fields, not 5"
  printf '%s\t%s\t%s\t%s\t%s\n' "${row[@]}" >> "$TMP"
done

{
  echo "#"
  printf '# modules                       %d\n' "${#MODS[@]}"
  # `$1 != "module"` drops the HEADER, which otherwise counts itself: its own
  # `fanout_before` / `fanout_after` cells are neither DEAD nor NONE.
  printf '# live BEFORE (not DEAD/NONE)   %d\n' \
    "$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 && $3!="DEAD" && $3!="NONE"' "$TMP" | wc -l)"
  printf '# live AFTER                    %d\n' \
    "$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 && $5!="DEAD" && $5!="NONE"' "$TMP" | wc -l)"
  printf '# rows                          %d\n' \
    "$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5' "$TMP" | wc -l)"
  printf '# memory CLOCK reached, BEFORE  %d\n' "$(awk -F'\t' 'NF==5 && $3~/MEM-CLOCK/' "$TMP" | wc -l)"
  printf '# memory CLOCK reached, AFTER   %d\n' "$(awk -F'\t' 'NF==5 && $5~/MEM-CLOCK/' "$TMP" | wc -l)"
} >> "$TMP"

# ---- NOTHING IS PUBLISHED UNTIL ALL OF THIS HOLDS --------------------------
# The previous shape ran `mv` first and checked afterwards, so a run that
# returned nonzero still left the requested table on disk for someone to read.
rows_n="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5' "$TMP" | wc -l)"
[[ "$rows_n" -eq "${#MODS[@]}" ]] \
  || d4_die "the table holds $rows_n rows for ${#MODS[@]} declared modules"
ids_n="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 {print $1}' "$TMP" | sort -u | wc -l)"
[[ "$ids_n" -eq "${#MODS[@]}" ]] \
  || d4_die "the table holds $ids_n unique identities for ${#MODS[@]} declared modules"
missing="$(comm -23 <(printf '%s\n' "${MODS[@]}" | sort) \
                    <(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 {print $1}' "$TMP" | sort) | tr '\n' ' ')"
[[ -z "${missing// /}" ]] || d4_die "declared but absent from the table: $missing"
extra="$(comm -13 <(printf '%s\n' "${MODS[@]}" | sort) \
                  <(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 {print $1}' "$TMP" | sort) | tr '\n' ' ')"
[[ -z "${extra// /}" ]] || d4_die "in the table but not declared: $extra"
# NO FAILURE TOKEN ANYWHERE IN A DATA ROW. Every path that could write one now
# aborts instead, so finding one here means a path was missed.
bad="$(awk -F'\t' '$1!~/^#/ && $1!="module" {print}' "$TMP" \
        | grep -nE 'UNPARSED|TOOL-FAILED|EMIT-FAILED|NO-NETLIST' || true)"
[[ -z "$bad" ]] || { echo "$bad" >&2; d4_die "a data row carries a failure token"; }
# Every class cell is one of the names the reader is allowed to produce.
while IFS=$'\t' read -r _ _ cb _ ca; do
  for cell in "$cb" "$ca"; do
    [[ "$cell" == "NONE" ]] && continue
    IFS=',' read -r -a parts <<<"$cell"
    for pcl in "${parts[@]}"; do
      case "$pcl" in
        OUTPUT|STATE|MEM-CLOCK|MEM-DATA|MEM-CTRL|INSTANCE|DEAD) ;;
        *) d4_die "unknown fanout class '$pcl' in the generated table" ;;
      esac
    done
  done
done < <(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5' "$TMP")
grep -nE '[[:space:]]+$' "$TMP" && d4_die "the generated table carries trailing whitespace"

# ---- VALIDATE THE CANDIDATE, THEN PUBLISH ---------------------------------
# EVERY assertion is made on the CANDIDATE, and the checked rename is the LAST
# fallible operation of this script. The previous shape renamed first and
# validated after, so any later failure -- a short read, an unreadable file --
# exited nonzero with the requested path populated: the same prohibited state as
# a stale table, reached from the other direction.
#
# `set -e` is deliberately not in use, so none of these is implicit.
published="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5' "$TMP" | wc -l)" \
  || d4_die "cannot read the candidate table at $TMP"
[[ "$published" -eq "${#MODS[@]}" ]] \
  || d4_die "the candidate table holds $published rows, not ${#MODS[@]}"
grep -q '^# schema  *= d4-emission-fanout/1$' "$TMP" \
  || d4_die "the candidate table does not carry its schema line"
grep -q "^# producer_sha256 = $(d4_sha "$SELF")\$" "$TMP" \
  || d4_die "the candidate table does not carry this producer's hash"
# The summary is printed from the CANDIDATE, before publication, so that reading
# it is one of the things that must succeed BEFORE `$OUT` exists.
tail -7 "$TMP" || d4_die "cannot read the candidate table at $TMP"

# THE LAST FALLIBLE OPERATION. Nothing below may fail this invocation: the
# validated candidate IS the table once this rename returns, so the run has
# succeeded and the message that follows is not contractual.
mv -f "$TMP" "$OUT" || d4_die "could not publish $TMP to $OUT"
echo "wrote $OUT"
exit 0

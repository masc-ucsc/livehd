#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# scripts/packed_field_intervals.py decides whether a packed-aggregate cycle is
# FALSE (the fields never touch) or REAL (some bit depends on itself). That
# verdict is what licenses -- or forbids -- a split_selfref rule for
# `sra`/`sext`-shaped field reads, so a tool that can only ever say DISJOINT
# would be worse than none.
#
# Every case is a hand-built dump in the `lhd tool cat` shape: a positive, a
# genuine overlap that MUST be refused, and the three ways the walk gives up.
# The first version of this tool reported UNKNOWN on all seven real SCCs
# because it treated `get_mask(x,-1)` as a pass-through when it is a
# zero-extension, so the fill-bit cases are pinned too.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
TOOL=""
for c in "${TEST_SRCDIR:-}/_main/scripts/packed_field_intervals.py" \
         "$ROOT/scripts/packed_field_intervals.py" "scripts/packed_field_intervals.py"; do
  [ -r "$c" ] && { TOOL="$c"; break; }
done
[ -n "$TOOL" ] || { echo "FAIL: cannot find packed_field_intervals.py"; exit 1; }

if [ -n "${TEST_TMPDIR:-}" ]; then T="$TEST_TMPDIR/packed_field_intervals_runtime"
else T="$ROOT/generated/packed_field_intervals_test/runtime_tmp"; fi
rm -rf "$T"; mkdir -p "$T"
rc=0

PY_BUILD="$T/build.py"
cat > "$PY_BUILD" <<'PYB'
import json, sys
# A packed pair: or_A and or_B, each reading a field out of the other.
#   out[kind, nid, consts, width, [(driver_nid, bits)]]
def emit(path, nodes, edges):
    with open(path, "w") as f:
        for nid, kind, consts, w in nodes:
            f.write(json.dumps({"t": "node", "mod": "m", "nid": nid, "kind": kind,
                                "name": None, "color": None, "match": None, "src": None,
                                "partitionable": True, "consts": consts}) + "\n")
            f.write(json.dumps({"t": "pin", "mod": "m", "nid": nid, "name": None,
                                "bits": w, "signed": False, "match": None}) + "\n")
        for d, dk, s, sk, b in edges:
            f.write(json.dumps({"t": "edge", "mod": "m",
                                "from": f"{dk}_{d}.p0", "to": f"{sk}_{s}.p0",
                                "bits": b}) + "\n")

def pair(path, a_src_lo, b_src_lo, field=16, w=48, extra=None):
    """or_1 takes [a_src_lo, +field) of or_2; or_2 takes [b_src_lo, +field) of or_1."""
    nodes = [(1, "or", None, w), (2, "or", None, w),
             (10, "sra", f"p1={a_src_lo}", field), (11, "sext", "p1=%d" % field, field),
             (12, "shl", f"p1={a_src_lo}", w), (13, "get_mask", "p2=-1", w + 1),
             (20, "sra", f"p1={b_src_lo}", field), (21, "sext", "p1=%d" % field, field),
             (22, "shl", f"p1={b_src_lo}", w), (23, "get_mask", "p2=-1", w + 1)]
    edges = [(2, "or", 10, "sra", w), (10, "sra", 11, "sext", field),
             (11, "sext", 12, "shl", field), (12, "shl", 13, "get_mask", w),
             (13, "get_mask", 1, "or", w + 1),
             (1, "or", 20, "sra", w), (20, "sra", 21, "sext", field),
             (21, "sext", 22, "shl", field), (22, "shl", 23, "get_mask", w),
             (23, "get_mask", 2, "or", w + 1)]
    if extra:
        nodes, edges = extra(nodes, edges)
    emit(path, nodes, edges)

def zext_narrow(path, w=48, field=16):
    """THE REAL SHAPE: the field read ends in a get_mask whose operand is only
    `field` bits wide, zero-extended into a `w`-bit word. or_1 takes or_2's low
    field at offset 0; or_2 takes or_1's [field, 2*field) and places it there.

    Without the zero-extension clamp the walk asks the sext for bits far above
    its sign position, gets `field`..w of replicated sign, and invents
    dependencies that are not there -- which is what made all seven real SCCs
    report UNKNOWN before the model was fixed."""
    nodes = [(1, "or", None, w), (2, "or", None, w),
             (10, "sra", "p1=0", field), (11, "sext", f"p1={field}", field),
             (13, "get_mask", "p2=-1", field + 1),
             (20, "sra", f"p1={field}", field), (21, "sext", f"p1={field}", field),
             (22, "shl", f"p1={field}", w), (23, "get_mask", "p2=-1", w + 1)]
    edges = [(2, "or", 10, "sra", w), (10, "sra", 11, "sext", field),
             (11, "sext", 13, "get_mask", field), (13, "get_mask", 1, "or", field + 1),
             (1, "or", 20, "sra", w), (20, "sra", 21, "sext", field),
             (21, "sext", 22, "shl", field), (22, "shl", 23, "get_mask", w),
             (23, "get_mask", 2, "or", w + 1)]
    emit(path, nodes, edges)

out = sys.argv[1]
# 1. DISJOINT: or_1 takes B[0,16), or_2 takes A[16,32). No bit meets itself.
pair(f"{out}/disjoint.jsonl", 0, 16)
# 2. OVERLAPPING: both read the SAME field. or_1[16..32) <- or_2[16..32) and
#    back again, so bit 16 depends on bit 16.
pair(f"{out}/overlap.jsonl", 16, 16)
# 3. a NON-CONSTANT shift: drop the shift constant from one sra.
def nonconst(nodes, edges):
    return ([(n, k, (None if n == 10 else c), w) for n, k, c, w in nodes], edges)
pair(f"{out}/nonconst.jsonl", 0, 16, extra=nonconst)
# 4. a mask that is not all-ones: a partial get_mask is not a zero-extension.
def badmask(nodes, edges):
    return ([(n, k, ("p2=255" if n == 13 else c), w) for n, k, c, w in nodes], edges)
pair(f"{out}/badmask.jsonl", 0, 16, extra=badmask)
# 5. an op the walk does not model at all. The EDGE endpoints embed the kind
#    (`<kind>_<nid>.p0`), so renaming a node without renaming its edges makes
#    the dump self-inconsistent and the reporter refuses it as unsound -- a
#    different failure from the one being tested.
def unmodelled(nodes, edges):
    nodes = [(n, ("mult" if n == 11 else k), c, w) for n, k, c, w in nodes]
    edges = [(d, ("mult" if d == 11 else dk), s, ("mult" if s == 11 else sk), b)
             for d, dk, s, sk, b in edges]
    return nodes, edges
pair(f"{out}/unmodelled.jsonl", 0, 16, extra=unmodelled)
# 6. the real shape: a narrow field zero-extended straight into a wide word.
zext_narrow(f"{out}/zextnarrow.jsonl")
PYB
python3 "$PY_BUILD" "$T" || { echo "FAIL: could not build the fixtures"; exit 1; }

check() {  # <file> <want-verdict> <want-exit> <what>
  local out; out="$(python3 "$TOOL" "$T/$1" --stage t 2>&1)"; local got=$?
  local v; v="$(echo "$out" | grep -oP 'verdict=\K\S+' | head -1)"
  if [ "$v" != "$2" ] || [ "$got" -ne "$3" ]; then
    echo "FAIL: $4: verdict=$v exit=$got, wanted $2 / $3"
    echo "$out" | sed 's/^/      /' | head -8
    rc=1
  else
    echo "ok: $4 -> $2 (exit $got)"
  fi
}

check disjoint.jsonl   DISJOINT    0 "fields that never touch"
check overlap.jsonl    OVERLAPPING 1 "both words reading the SAME field is a real bit loop"
check nonconst.jsonl   UNKNOWN     1 "a non-constant shift"
check badmask.jsonl    UNKNOWN     1 "a get_mask that is not all-ones"
check unmodelled.jsonl UNKNOWN     1 "an op the walk does not model"
check zextnarrow.jsonl DISJOINT    0 "a narrow field zero-extended into a wide word"

# The positive must report the exact ranges, not a min..max hull: a packed
# word's dependency is usually SPARSE, and printing [0,48) for {[0,16),[32,48)}
# says the opposite of what the bits say.
out="$(python3 "$TOOL" "$T/disjoint.jsonl" --stage t 2>&1)"
if echo "$out" | grep -q 'or_1 bits \[0,16) <- or_2 bits \[0,16)' \
   && echo "$out" | grep -q 'or_2 bits \[16,32) <- or_1 bits \[16,32)'; then
  echo "ok: the exact bit ranges are reported"
else
  echo "FAIL: the reported ranges are not the expected disjoint pair"
  echo "$out" | sed 's/^/      /' | head -10
  rc=1
fi

# ...and the zero-extension must contribute NOTHING above the operand's width.
# Without that clamp the walk asks the sext for bits far above its sign
# position and invents a dependency on the replicated sign -- which does not
# always change the verdict, so only the RANGES catch it.
out="$(python3 "$TOOL" "$T/zextnarrow.jsonl" --stage t 2>&1)"
if ! echo "$out" | grep -q 'or_1 bits \[0,16) <- or_2 bits \[0,16)'; then
  echo "FAIL: the zero-extended field is not reported as or_1[0,16) <- or_2[0,16)"
  echo "$out" | sed 's/^/      /' | head -8
  rc=1
elif echo "$out" | grep -qE '^ +or_1 bits \[(1[6-9]|[2-9][0-9])'; then
  # ANCHORED to the destination. `or_1` also appears as the SOURCE in
  # "or_2 bits [16,32) <- or_1 bits [16,32)", which is the legitimate other
  # direction, and an unanchored match flagged it as the defect.
  echo "FAIL: the zero-extension contributed bits ABOVE its operand's width;"
  echo "      those are zeros, not a dependency on the replicated sign"
  echo "$out" | sed 's/^/      /' | head -8
  rc=1
else
  echo "ok: the zero-extension contributes nothing above its operand's width"
fi

[ "$rc" -eq 0 ] || { echo "FAIL: packed_field_intervals_test"; exit 1; }
echo "PASS: packed_field_intervals_test"

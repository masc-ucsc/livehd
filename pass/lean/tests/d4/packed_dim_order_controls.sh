#!/usr/bin/env bash
# Controls for packed-dimension reconstruction in gen_cva6_wrappers.py.
#
# THE DEFECT.  `render_field_type` recursed into a packed array's element and
# appended its OWN range AFTER the inner text, which reverses the order for
# every NESTED packed array.  SystemVerilog reads packed dimensions OUTER
# FIRST: `logic [0:0][63:0] x` makes `x[0]` a 64-bit element, while
# `logic [63:0][0:0] x` makes it ONE BIT.
#
# `hpdcache_req_data_t` -- an outer [0:0] of a 64-bit element -- was emitted as
# `logic [63:0] [0:0]`, so `rdata[0]` was one bit and slang correctly rejected
# `[63:32]` of it at cva6_hpdcache_if_adapter.sv:332.  The TOTAL WIDTH was right
# either way, which is why no width check ever saw it.
#
# These controls check BOTH properties that matter: the emitted declaration
# ORDER and the total WIDTH.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
GEN="$ROOT/scripts/gen_cva6_wrappers.py"
WRAPDIR="${D4_WRAPDIR:-$ROOT/generated/cva6_wrappers}"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -r "$GEN" ]] || { echo "FATAL: no $GEN" >&2; exit 2; }

# --- 1-5. the renderer, on synthetic AST shapes ---------------------------
probe="$(python3 -B - "$GEN" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("g", sys.argv[1])
g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)

S   = {"kind": "ScalarType"}
def pa(rng, elem):  return {"kind": "PackedArrayType", "range": rng, "elementType": elem}
L64 = pa("[63:0]", S)

cases = [
    # label,                      type,                              expected text,            expected width
    ("scalar",                    S,                                 "logic",                   1),
    ("single packed [63:0]",      L64,                               "logic [63:0]",           64),
    ("outer [0:0] of [63:0]",     pa("[0:0]", L64),                  "logic [0:0] [63:0]",     64),
    ("outer [1:0] of [63:0]",     pa("[1:0]", L64),                  "logic [1:0] [63:0]",    128),
    ("three levels [1:0][3:0][7:0]",
                                  pa("[1:0]", pa("[3:0]", pa("[7:0]", S))),
                                                                     "logic [1:0] [3:0] [7:0]", 64),
    ("struct, unchanged",
     {"kind": "PackedStructType",
      "members": [{"name": "hi", "bitOffset": 1, "type": S},
                  {"name": "lo", "bitOffset": 0, "type": S}]},
     "struct packed { logic hi; logic lo; }", 2),
]
bad = []
for label, t, want_txt, want_w in cases:
    got_txt = g.render_field_type(t)
    got_w   = g.type_width(t)
    if got_txt != want_txt:
        bad.append(f"{label}: text {got_txt!r}, want {want_txt!r}")
    if got_w != want_w:
        bad.append(f"{label}: width {got_w}, want {want_w}")

# TEETH: the exact reversed spelling the defect produced must not reappear.
rev = g.render_field_type(pa("[0:0]", L64))
if rev == "logic [63:0] [0:0]":
    bad.append("the reversed pre-fix spelling 'logic [63:0] [0:0]' is back")

print("ALLGOOD" if not bad else "BAD:" + " | ".join(bad))
PY
)"
if [[ "$probe" == "ALLGOOD" ]]; then
  ok "renderer: 6 shapes, declaration order outer-first and total width preserved"
else
  bad "renderer: ${probe#BAD:}"
fi

# --- 6. the published corpus is exactly what the generator produces -------
# NOT a width heuristic.  "outer range wider than inner means reversed" is
# WRONG -- `logic [63:0] [53:0]` is a legitimate 64-element array of 54-bit
# values, and that heuristic flagged eleven such declarations.  Order
# correctness is established by the renderer controls above, on known AST
# shapes.  What the corpus can show is that it IS the generator's output:
# regenerating from the same AST must reproduce it byte for byte, which also
# means no generated artifact was hand-edited.
AST="${D4_AST:-$ROOT/generated/cva6_ast/cva6_ast.json}"
LIST="${D4_MODULE_LIST:-$HERE/cva6_78.list}"
if [[ ! -s "$AST" || ! -d "$WRAPDIR" || ! -s "$LIST" ]]; then
  printf 'N/A   no AST or wrapper corpus to re-derive (%s)\n' "$AST"
else
  REGEN="$(mktemp -d "$ROOT/temp/pdo_regen.XXXXXX")"
  trap 'rm -rf "$REGEN"' EXIT
  mapfile -t MODS < <(grep -v '^[[:space:]]*#' "$LIST" | grep -v '^[[:space:]]*$')
  if ! python3 -B "$GEN" "$AST" --out "$REGEN" "${MODS[@]}" >"$REGEN/gen.log" 2>&1; then
    bad "could not regenerate the corpus: $(tail -1 "$REGEN/gen.log")"
  else
    n_cmp=0; n_diff=0; first=""
    for f in "$WRAPDIR"/*_gate.sv; do
      [[ -r "$f" ]] || continue
      n_cmp=$((n_cmp + 1))
      if ! cmp -s "$f" "$REGEN/$(basename "$f")"; then
        n_diff=$((n_diff + 1)); [[ -z "$first" ]] && first="$(basename "$f")"
      fi
    done
    if [[ "$n_cmp" -eq 0 ]]; then
      bad "no wrappers to compare in $WRAPDIR"
    elif [[ "$n_diff" -ne 0 ]]; then
      bad "$n_diff/$n_cmp published wrappers differ from a fresh generation (first: $first) -- hand-edited or stale"
    else
      ok "all $n_cmp published wrappers reproduce byte-for-byte from the AST; none hand-edited"
    fi
  fi
fi

# --- 7. the concrete shape that broke the build ---------------------------
w="$WRAPDIR/cva6_hpdcache_wrapper_gate.sv"
if [[ ! -r "$w" ]]; then
  printf 'N/A   no cva6_hpdcache_wrapper wrapper at %s\n' "$w"
elif grep -q 'typedef logic \[63:0\] \[0:0\] hpdcache_req_data_t__r;' "$w"; then
  bad "hpdcache_req_data_t__r is still the reversed 'logic [63:0] [0:0]'"
elif ! grep -q 'typedef logic \[0:0\] \[63:0\] hpdcache_req_data_t__r;' "$w"; then
  bad "hpdcache_req_data_t__r has an unexpected shape: $(grep -m1 'hpdcache_req_data_t__r;' "$w")"
else
  ok "hpdcache_req_data_t__r is 'logic [0:0] [63:0]' -- rdata[0] is 64 bits"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1

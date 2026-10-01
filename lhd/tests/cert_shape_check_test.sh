#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass/lean/scripts/cert_shape_check.py -- the static shape gate.
#
# It replaced a `nodes > 0` check that was UNSOUNDLY STRICT: `null_vpu` and
# `minion_dcache_texsend` are real CORE-ET modules with all-constant sources,
# `nodes := #[]` and every output resolving to a valid source slot. They are
# exactly what their names say, and the old gate refused them for having no
# combinational nodes -- a gate defect reported as two module failures.
#
# So a zero-node design whose refs resolve MUST pass, and anything whose refs
# do not resolve must still fail.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHK=""
for c in "${TEST_SRCDIR:-}/_main/pass/lean/scripts/cert_shape_check.py" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/pass/lean/scripts/cert_shape_check.py" \
         "pass/lean/scripts/cert_shape_check.py"; do
  [ -r "$c" ] && { CHK="$c"; break; }
done
[ -n "$CHK" ] || { echo "FAIL: cannot find cert_shape_check.py"; exit 1; }
T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/cert_shape"
rm -rf "$T"; mkdir -p "$T"; fails=0

want() {  # <desc> <file> <expected-rc>
  python3 "$CHK" "$2" > "$T/out.txt" 2>&1; local rc=$?
  if [ "$rc" -ne "$3" ]; then
    echo "FAIL: $1 -- rc=$rc, expected $3"; sed 's/^/      /' "$T/out.txt"; fails=$((fails+1))
  else echo "ok: $1"; fi
}

# A zero-node constant passthrough: the shape that was wrongly refused.
cat > "$T/zero_ok.lean" <<'EOF'
def z_designCert : DesignCert :=
  {
    sources  := #[
        SourceDesc.const 8 (0)
      , SourceDesc.const 4 ((Int.ofNat 3))
      ]
    nodes    := #[]
    outputs  := #[
        { slot := 0, width := 8 }
      , { slot := 1, width := 4 }
      ]
    flops    := #[]
    memories := #[]
    clocks   := #[ { name := "clock" } ]
  }
EOF
want "a zero-node constant passthrough passes" "$T/zero_ok.lean" 0
grep -q "zero-node design" "$T/out.txt" && echo "ok: it is identified as zero-node" \
  || { echo "FAIL: the zero-node note was not emitted"; fails=$((fails+1)); }

# Zero-node, but an output points past the last slot.
sed 's/{ slot := 1, width := 4 }/{ slot := 9, width := 4 }/' "$T/zero_ok.lean" > "$T/zero_oob.lean"
want "a zero-node design with an out-of-range output fails" "$T/zero_oob.lean" 1

# A flop whose din is out of range.
cat > "$T/flop_oob.lean" <<'EOF'
def f_designCert : DesignCert :=
  {
    sources  := #[
        SourceDesc.const 8 (0)
      ]
    nodes    := #[
        { op := LGraphOp.Op_Not, width := 8, deps := #[0], origin := 1 }
      ]
    outputs  := #[
        { slot := 1, width := 8 }
      ]
    flops    := #[
        { width := 8, din := 77, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false, clock := 0, asyncReset := false }
      ]
    memories := #[]
    clocks   := #[ { name := "clock" } ]
  }
EOF
want "an out-of-range flop din fails" "$T/flop_oob.lean" 1

# A node dep out of range.
sed 's/deps := #\[0\]/deps := #[42]/' "$T/flop_oob.lean" | sed 's/din := 77/din := 1/' > "$T/dep_oob.lean"
want "an out-of-range node dep fails" "$T/dep_oob.lean" 1

# Valid with nodes and a flop.
sed 's/din := 77/din := 1/' "$T/flop_oob.lean" > "$T/ok.lean"
want "a well-formed design with nodes and a flop passes" "$T/ok.lean" 0

# Nothing observable at all.
cat > "$T/empty.lean" <<'EOF'
def e_designCert : DesignCert :=
  {
    sources  := #[]
    nodes    := #[]
    outputs  := #[]
    flops    := #[]
    memories := #[]
    clocks   := #[ { name := "clock" } ]
  }
EOF
want "a certificate with no slots at all fails" "$T/empty.lean" 1

# A file the parser cannot read must FAIL LOUDLY as a parser problem, never be
# treated as an empty design (which could read as either a bogus shape failure
# or, worse, a pass).
printf 'def nothing := 1\n' > "$T/unparseable.lean"
want "an unparseable certificate fails as a parser error" "$T/unparseable.lean" 1
grep -q "PARSER failure" "$T/out.txt" && echo "ok: it is reported as a parser failure" \
  || { echo "FAIL: a parse failure was not reported as one"; fails=$((fails+1)); }

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: cert_shape_check_test"

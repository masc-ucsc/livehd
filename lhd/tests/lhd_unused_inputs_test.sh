#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd compile --unused-inputs FILE` — the Bazel `unused_inputs_list` report:
# declared source-file positionals whose contents did not reach the compiled
# closure (absent from every final unit's Source_locator table), one
# cwd-relative path per line. Positive case = a .sv whose module falls outside
# the --top hierarchy (inou.slang elaborates only the requested top). Pyrope
# positionals are all compiled as roots, so they are never listed. The file is
# always created when the flag is given, empty when everything was read.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_unused_inputs_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. Positive case: two .sv, --top a. Module b is parsed but not elaborated
#    (outside the top hierarchy), so b.sv is declared-but-never-read.
cat > "$W/a.sv" <<'EOF'
module a(input logic [7:0] x, output logic [7:0] y);
  assign y = x + 8'd1;
endmodule
EOF
cat > "$W/b.sv" <<'EOF'
module b(input logic [7:0] x, output logic [7:0] y);
  assign y = x ^ 8'hff;
endmodule
EOF
"$LHD" compile "$W/a.sv" "$W/b.sv" --top a --unused-inputs "$W/unused.txt" \
  --workdir "$W/w_sv" -q >/dev/null 2>&1 || fail "verilog --unused-inputs compile failed"
[ -f "$W/unused.txt" ] || fail "unused-inputs file was not created"
grep -q 'b\.sv' "$W/unused.txt" || fail "unused list should name b.sv: $(cat "$W/unused.txt")"
grep -q 'a\.sv' "$W/unused.txt" && fail "unused list must not name the --top source a.sv: $(cat "$W/unused.txt")"
[ "$(wc -l < "$W/unused.txt")" -eq 1 ] || fail "expected exactly one unused entry: $(cat "$W/unused.txt")"
head -1 "$W/unused.txt" | grep -q '^/' && fail "unused entries must be cwd-relative, got: $(cat "$W/unused.txt")"

# 2. Everything read: same two .sv without --top (slang auto-tops both) ->
#    empty list, file still created.
"$LHD" compile "$W/a.sv" "$W/b.sv" --unused-inputs "$W/unused_all.txt" \
  --workdir "$W/w_sv_all" -q >/dev/null 2>&1 || fail "auto-top --unused-inputs compile failed"
[ -f "$W/unused_all.txt" ] || fail "auto-top unused-inputs file was not created"
[ -s "$W/unused_all.txt" ] && fail "auto-top reads every source; expected an empty list: $(cat "$W/unused_all.txt")"

# 3. Pyrope: every .prp positional is compiled as a root (its module reaches
#    the emits), so nothing is ever prunable -> empty list even for a file
#    nothing imports.
cat > "$W/used.prp" <<'EOF'
comb used(a:u8) -> (z:u9) { z = a + 1 }
EOF
cat > "$W/extra.prp" <<'EOF'
comb extra(a:u8) -> (z:u8) { z = a ^ 3 }
EOF
"$LHD" compile "$W/used.prp" "$W/extra.prp" --unused-inputs "$W/unused_prp.txt" \
  --workdir "$W/w_prp" -q >/dev/null 2>&1 || fail "pyrope --unused-inputs compile failed"
[ -f "$W/unused_prp.txt" ] || fail "pyrope unused-inputs file was not created"
[ -s "$W/unused_prp.txt" ] && fail "pyrope roots are all compiled; expected an empty list: $(cat "$W/unused_prp.txt")"

# 4. The list is a declared output: it rides res.outputs (visible in the
#    result envelope) alongside the depfile, and coexists with --depfile.
"$LHD" compile "$W/a.sv" "$W/b.sv" --top a --unused-inputs "$W/unused2.txt" \
  --depfile "$W/dep.d" --result-json "$W/res.json" \
  --workdir "$W/w_both" -q >/dev/null 2>&1 || fail "--unused-inputs + --depfile compile failed"
[ -s "$W/dep.d" ] || fail "depfile was not written alongside --unused-inputs"
grep -q 'unused2\.txt' "$W/res.json" || fail "unused-inputs list is missing from result outputs: $(cat "$W/res.json")"

echo "PASS lhd_unused_inputs_test"

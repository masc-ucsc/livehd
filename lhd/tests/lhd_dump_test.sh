#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --dump parse|lnast|lg: the screen debug observables (future_cli.md "Shared
# arguments"). Dumps print to stderr at the named pipeline stage and force the
# stage that produces them; stdout stays protocol-clean (the result envelope).

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_dump_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# 1. elaborate --dump parse,lnast: both stages on stderr; --dump lnast forces
#    pass.upass even though elaborate declared no lg: emit.
"$LHD" elaborate "$PRP" --dump parse,lnast --workdir "$W/w1" -q --result-json "$W/r1.json" 2>"$W/e1" \
  || fail "elaborate --dump parse,lnast exited nonzero: $(cat "$W/r1.json" 2>/dev/null)"
grep -q '//---- lnast trivial_if (post-parse)' "$W/e1" || fail "missing post-parse dump: $(head -3 "$W/e1")"
grep -q '(post-upass)' "$W/e1" || fail "missing post-upass dump"
grep -q 'pass.upass' "$W/r1.json" || fail "--dump lnast did not force pass.upass: $(cat "$W/r1.json")"

# 2. compile --dump lg: textual LGraph node/edge dump, post-recipe.
"$LHD" compile "$PRP" --dump lg --workdir "$W/w2" -q --result-json "$W/r2.json" 2>"$W/e2" \
  || fail "compile --dump lg exited nonzero: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '//---- lg trivial_if.fun3 (post-recipe)' "$W/e2" || fail "missing lg dump header: $(head -3 "$W/e2")"
grep -q '^module trivial_if.fun3' "$W/e2" || fail "missing module line in lg dump"
grep -q 'input  \$a' "$W/e2" || fail "missing IO decl in lg dump"

# 3. Clean stdout: dumps never ride the protocol stream.
out=$("$LHD" elaborate "$PRP" --dump parse --workdir "$W/w3" -q 2>/dev/null)
case "$out" in
  '{"schema_version"'*) ;;
  *) fail "stdout is not the bare result envelope: $out" ;;
esac
echo "$out" | grep -q 'post-parse' && fail "dump leaked onto stdout"

# 4. Stage availability is validated, not silently no-op'd.
"$LHD" synth lg:/nonexistent --dump lnast -q --result-json "$W/r4.json" 2>/dev/null
grep -q '"class":"usage"' "$W/r4.json" || fail "synth lg: --dump lnast must be a usage error: $(cat "$W/r4.json")"
# (a malformed --dump fails in parse_args, before --result-json applies, so
# the envelope rides stdout)
"$LHD" elaborate x.prp --dump bogus -q 2>/dev/null >"$W/r5.json"
grep -q '"class":"usage"' "$W/r5.json" || fail "--dump bogus must be a usage error: $(cat "$W/r5.json")"

echo "PASS: --dump parse|lnast|lg"

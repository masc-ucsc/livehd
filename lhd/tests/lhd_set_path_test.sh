#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2h-set_path: the --set/--config option namespace matches the command path
# (`lhd pass color` -> pass.color.*), and a key may drop any leading segment
# the command words to its LEFT already supply. All forms below set the same
# canonical pass.color.continuous and must behave identically; a fully
# qualified key works anywhere, an abbreviated one only once the command
# context to its left supplies the dropped prefix.

set -u

LHD=lhd/lhd
V0=lhd/tests/part_hier.v
TOP=part_hier
W="${TEST_TMPDIR:-/tmp/lhd_set_path_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }

# A fresh, colorable lg (cprop-optimized) to copy before each coloring run.
"$LHD" compile verilog "$V0" --top "$TOP" --reader yosys-verilog --recipe O1 \
  --emit-dir lg:"$W/lg0" --workdir "$W/wc" -q --result-json "$W/rc.json" \
  || fail "compile setup: $(cat "$W/rc.json" 2>/dev/null)"

# ok <desc> <argv...> : run on a fresh copy of lg0, expect success.
ok() {
  local desc="$1"; shift
  rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg" || fail "cp lg ($desc)"
  "$LHD" "$@" -q --result-json "$W/r.json" >/dev/null 2>&1 \
    || fail "$desc: lhd $* -> $(cat "$W/r.json" 2>/dev/null)"
  echo "ok: $desc"
}

D="lg:$W/lg"

# 1. before the command word: must be fully qualified (no left context).
ok "before-cmd, full"        --set pass.color.continuous=true pass color acyclic --top "$TOP" "$D"
# 2. after `pass`: fully qualified still works.
ok "after-pass, full"        pass --set pass.color.continuous=true color acyclic --top "$TOP" "$D"
# 3. after `pass`: drop the `pass.` the command supplied.
ok "after-pass, drop pass."  pass --set color.continuous=true color acyclic --top "$TOP" "$D"
# 4. after `pass color`: fully qualified.
ok "after-sub, full"         pass color --set pass.color.continuous=true acyclic --top "$TOP" "$D"
# 5. after `pass color`: drop the whole `pass.color.` prefix.
ok "after-sub, drop all"     pass color acyclic --top "$TOP" --set continuous=true "$D"
# 6. after `pass color`: drop just `pass.`.
ok "after-sub, drop pass."   pass color acyclic --top "$TOP" --set color.continuous=true "$D"

# Negative: an abbreviated key before any command word cannot resolve.
rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg"
"$LHD" --set continuous=true pass color acyclic --top "$TOP" "$D" -q --result-json "$W/rn.json" >/dev/null 2>&1 \
  && fail "abbreviated key before the command word must error"
grep -qE "unknown pass|expects pass.flag" "$W/rn.json" || fail "neg: wrong error: $(cat "$W/rn.json")"
echo "ok: abbreviation before command word rejected"

# Negative: a real typo is still a usage error (not silently accepted).
rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg"
"$LHD" pass color acyclic --top "$TOP" --set bogusflag=1 "$D" -q --result-json "$W/rt.json" >/dev/null 2>&1 \
  && fail "unknown flag must error"
grep -q "unknown flag 'bogusflag' of pass 'pass.color'" "$W/rt.json" || fail "typo: wrong error: $(cat "$W/rt.json")"
echo "ok: unknown flag rejected with command-path pass name"

# run_id must not depend on how the key was abbreviated (same canonical key,
# same input path -> same content hash). Use the same path for both.
rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg"
"$LHD" pass color acyclic --top "$TOP" --set pass.color.continuous=true "$D" -q --result-json "$W/rA.json" >/dev/null 2>&1 \
  || fail "run_id A run failed: $(cat "$W/rA.json" 2>/dev/null)"
rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg"
"$LHD" pass color acyclic --top "$TOP" --set continuous=true "$D" -q --result-json "$W/rB.json" >/dev/null 2>&1 \
  || fail "run_id B run failed: $(cat "$W/rB.json" 2>/dev/null)"
idA=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/rA.json")
idB=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/rB.json")
[ -n "$idA" ] && [ "$idA" = "$idB" ] || fail "run_id must match across abbreviation: '$idA' vs '$idB'"
echo "ok: run_id stable across abbreviation"

# --config table uses the command-path namespace too ([pass.color]).
cat >"$W/c.toml" <<EOF
[pass.color]
continuous = true
EOF
rm -rf "$W/lg"; cp -r "$W/lg0" "$W/lg"
"$LHD" pass color acyclic --top "$TOP" --config "$W/c.toml" "$D" -q --result-json "$W/rcfg.json" >/dev/null 2>&1 \
  || fail "--config [pass.color] failed: $(cat "$W/rcfg.json" 2>/dev/null)"
echo "ok: --config [pass.color] table accepted"

echo "PASS: lhd --set command-path namespace + context-relative abbreviation"

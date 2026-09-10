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
"$LHD" compile verilog "$V0" --top "$TOP" --reader yosys-verilog \
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

# `lhd lec` IS `lhd formal lec` under its own command word, so both establish
# the same --set root (formal.lec): a bare `engine=bmc` resolves to the same
# canonical key after either spelling (same input -> same run_id). Regression:
# `lhd lec` used to root at `lec`, which names no pass, so the abbreviation
# only worked after `formal lec`.
LG0="lg:$W/lg0"
"$LHD" lec --impl "$LG0" --ref "$LG0" --top "$TOP" --set engine=bmc -q --result-json "$W/rl1.json" >/dev/null 2>&1 \
  || fail "lec --set engine=bmc (abbreviated) failed: $(cat "$W/rl1.json" 2>/dev/null)"
"$LHD" formal lec --impl "$LG0" --ref "$LG0" --top "$TOP" --set engine=bmc -q --result-json "$W/rl2.json" >/dev/null 2>&1 \
  || fail "formal lec --set engine=bmc (abbreviated) failed: $(cat "$W/rl2.json" 2>/dev/null)"
idL1=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/rl1.json")
idL2=$(sed 's/.*"run_id":"\([^"]*\)".*/\1/' "$W/rl2.json")
[ -n "$idL1" ] && [ "$idL1" = "$idL2" ] || fail "lec vs formal lec must resolve --set engine=bmc identically: '$idL1' vs '$idL2'"
echo "ok: lec / formal lec share the formal.lec --set root"

# ...and that root must NOT re-open the REMOVED lec.* namespace: `lec.solver`
# under either spelling keeps the directed "was removed" error instead of
# silently collecting the prefix into formal.lec.solver.
for cmd in "lec" "formal lec"; do
  # shellcheck disable=SC2086  # $cmd is one or two command words on purpose
  "$LHD" $cmd --impl "$LG0" --ref "$LG0" --top "$TOP" --set lec.solver=lgyosys -q --result-json "$W/rlr.json" >/dev/null 2>&1 \
    && fail "$cmd --set lec.solver must fail (namespace removed)"
  grep -q "use --set formal.solver=lgyosys instead" "$W/rlr.json" \
    || fail "$cmd --set lec.solver must keep the removed-namespace hint: $(cat "$W/rlr.json" 2>/dev/null)"
done
echo "ok: removed lec.* namespace still refused under lec / formal lec"

echo "PASS: lhd --set command-path namespace + context-relative abbreviation"

#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The sim.tune.* option surface (sim_profile.md §3, ruling 6), no simulation run:
#   - `lhd list options` / `describe` / `sim --help` show the dotted names;
#   - the four old spellings are directed, copy-pasteable rename errors, also
#     through the removed `compile.sim.*` namespace and through --config;
#   - every new knob's grammar rejects junk as a usage error;
#   - a `[sim.tune]` --config table works;
#   - P0.1: flipping a sim.* knob keeps the compile cache warm;
#   - the envelope carries `sim_tune` (disabled without a --workdir);
#   - `lhd compile --emit-dir sim:` lists every generated .cpp in its BUILD
#     (alwayslink, so the tune-id identity survives a static-archive link) and
#     bakes the explicit vector;
#   - sim.tune.profile_stride is range-checked at parse time like drv.bin does;
#   - a hand-written sim.tune.file may spell dirty as a JSON bool and fence /
#     live_words as JSON integers; any other JSON type is a config error naming
#     the knob; a file that was read is a depfile / envelope input.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_tune_options_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/acc.prp" <<'EOF'
mod acc(din:u8) -> (sum:u32@[0]) {
  reg total:u32 = 0
  sum = total
  wrap total = total + din
}

test acc.sum {
  mut a = acc
  mut s = nil
  tick 4 {
    a.din = 1
    step
    s = a.sum
  }
  assert(s == 4)
}
EOF
PRP="$W/acc.prp"

# ---- listing / describe / help --------------------------------------------------
out=$("$LHD" list options)
for kv in tune.profile:auto tune.dirty:auto tune.fence:auto tune.live_words:auto tune.backend:auto tune.file: tune.export: \
  tune.profile_dir: tune.profile_stride:0; do
  echo "$out" | grep -q "\"name\":\"sim.${kv%%:*}\",\"method\":\"sim\",\"default\":\"${kv#*:}\"" \
    || fail "sim.${kv%%:*} missing or wrong default in lhd list options"
done
for old in color_dirty fence_ratio live_words backend; do
  echo "$out" | grep -q "\"name\":\"sim.$old\"" && fail "old spelling sim.$old still listed"
done
"$LHD" describe sim.tune.dirty | grep -q '"name":"sim.tune.dirty","kind":"option","method":"sim"' \
  || fail "describe sim.tune.dirty must resolve to the sim namespace"
"$LHD" sim --help --diag-fmt pretty | grep -q "sim.tune" || fail "sim --help does not mention sim.tune"

# ---- ruling 6: directed renames -------------------------------------------------------
for pair in "color_dirty=true:sim.tune.dirty=on" "color_dirty=false:sim.tune.dirty=off" "fence_ratio=0:sim.tune.fence=0" \
  "fence_ratio=:sim.tune.fence=auto" "live_words=0:sim.tune.live_words=auto" "live_words=20:sim.tune.live_words=20" \
  "backend=llvm:sim.tune.backend=llvm"; do
  from="${pair%%:*}"
  to="${pair#*:}"
  "$LHD" sim "$PRP" --set "sim.$from" --setup-only --workdir "$W/w" -q >"$W/r.json" 2>/dev/null && fail "sim.$from must fail"
  grep -qF "use --set $to instead" "$W/r.json" || fail "sim.$from: no directed hint to $to: $(cat "$W/r.json")"
  "$LHD" compile "$PRP" --set "sim.$from" -q >"$W/r.json" 2>/dev/null && fail "compile --set sim.$from must fail"
  grep -qF "use --set $to instead" "$W/r.json" || fail "compile sim.$from: no directed hint to $to"
done
"$LHD" compile "$PRP" --set compile.sim.color_dirty=1 -q >"$W/r.json" 2>/dev/null && fail "compile.sim.color_dirty must fail"
grep -qF "use --set sim.tune.dirty=on instead" "$W/r.json" || fail "compile.sim.color_dirty must name sim.tune.dirty: $(cat "$W/r.json")"
"$LHD" compile "$PRP" --set compile.sim.tune.dirty=on -q >"$W/r.json" 2>/dev/null && fail "compile.sim.tune.dirty must fail"
grep -qF "use --set sim.tune.dirty=on instead" "$W/r.json" || fail "compile.sim.tune.dirty must name sim.tune.dirty"
printf '[sim]\ncolor_dirty = true\n' >"$W/old.toml"
"$LHD" sim "$PRP" --config "$W/old.toml" --setup-only --workdir "$W/w" -q >"$W/r.json" 2>/dev/null && fail "[sim] color_dirty must fail"
grep -qF "use --set sim.tune.dirty=on instead" "$W/r.json" || fail "--config color_dirty: no directed hint"

# ---- grammar ------------------------------------------------------------------------
for bad in tune.dirty=maybe tune.fence=-1 tune.fence=x tune.fence=2000000 tune.live_words=0 tune.live_words=abc \
  tune.profile=sometimes tune.profile=true tune.backend=gcc tune.profile_stride=1.5 tune.profile_stride=2000000 \
  tune.profile_stride=-1 tune.nope=1; do
  "$LHD" sim "$PRP" --set "sim.$bad" --setup-only --workdir "$W/w" -q >"$W/r.json" 2>/dev/null && fail "sim.$bad must fail"
  grep -q '"class":"usage"' "$W/r.json" || fail "sim.$bad: not a usage error: $(cat "$W/r.json")"
done

# ---- [sim.tune] --config table --------------------------------------------------------
printf '[sim.tune]\ndirty = "on"\nfence = 0\nprofile = "off"\n' >"$W/tune.toml"
"$LHD" sim "$PRP" --config "$W/tune.toml" --setup-only --workdir "$W/cfg" -q --result-json "$W/cfg.json" 2>/dev/null \
  || fail "[sim.tune] config rejected: $(cat "$W/cfg.json")"
python3 - "$W/cfg.json" <<'PY' || fail "[sim.tune] config not applied: $(cat "$W/cfg.json")"
import json, sys
t = json.load(open(sys.argv[1]))["sim_tune"]
assert t["applied"]["vector"] == "tv1:d=on;f=0;lw=256;be=slop", t
assert t["source"]["dirty"] == "explicit" and t["source"]["fence"] == "explicit", t
assert t["mode"] == "off" and t["enabled"] is False and t["reason"] == "mode-off", t
PY
[ -e "$W/cfg/incr/scopes/sim" ] && fail "profile=off created a tune store"

# ---- P0.1: a sim.* flip keeps the compile cache warm ---------------------------------
"$LHD" sim "$PRP" --setup-only --workdir "$W/p01" -q --result-json "$W/p01a.json" 2>/dev/null || fail "P0.1 base setup failed"
"$LHD" sim "$PRP" --setup-only --workdir "$W/p01" -q --result-json "$W/p01b.json" --set sim.tune.dirty=off \
  --set sim.slop_u=false 2>/dev/null || fail "P0.1 flipped setup failed"
python3 - "$W/p01b.json" <<'PY' || fail "a sim.* flip cold-started the compile cache: $(cat "$W/p01b.json")"
import json, sys
c = json.load(open(sys.argv[1]))["incremental"]["compile"]
assert c["enabled"] and c["misses"] == 0 and c["refused"] == 0 and c["hits"] > 0, c
PY

# ---- the envelope member without a workdir -------------------------------------------
"$LHD" sim "$PRP" -q --result-json "$W/nowd.json" 2>/dev/null || fail "sim without a workdir failed"
python3 - "$W/nowd.json" <<'PY' || fail "sim_tune without --workdir: $(cat "$W/nowd.json")"
import json, sys
t = json.load(open(sys.argv[1]))["sim_tune"]
assert t["enabled"] is False and t["reason"] == "no-workdir", t
assert t["applied"]["vector"] == "tv1:d=on;f=16;lw=256;be=slop", t
assert t["source"]["dirty"] == "default", t
PY
"$LHD" sim "$PRP" -q --set sim.tune.profile=on --result-json "$W/nowd_on.json" 2>"$W/nowd_on.err" || fail "profile=on without workdir failed"
grep -q '"warnings":[1-9]' "$W/nowd_on.json" || fail "profile=on without a --workdir must warn (tune-disabled)"

# ---- lhd compile --emit-dir sim: ------------------------------------------------------
"$LHD" compile "$PRP" --emit-dir "sim:$W/es" --set sim.tune.dirty=off -q --result-json "$W/es.json" 2>/dev/null \
  || fail "compile --emit-dir sim: failed: $(cat "$W/es.json")"
for f in "$W"/es/*.cpp; do
  b=$(basename "$f")
  grep -qF "\"$b\"" "$W/es/BUILD" || fail "--emit-dir sim: BUILD does not compile $b"
done
if ls "$W"/es/*.tune-id.cpp >/dev/null 2>&1; then
  grep -q 'tv1:d=off;f=none;lw=256;be=slop' "$W"/es/*.tune-id.cpp || fail "--emit-dir sim: did not bake the explicit vector"
fi

grep -q "alwayslink = True" "$W/es/BUILD" || fail "--emit-dir sim: BUILD must alwayslink the sim library (tune-id identity)"

# ---- sim.tune.file: JSON types, depfile and envelope inputs ----------------------------
echo '{"schema":"lhd-sim-tune-file-1","knobs":{"dirty":true,"fence":0}}' >"$W/tf_json.json"
"$LHD" compile "$PRP" --emit-dir "sim:$W/et" --set sim.tune.file="$W/tf_json.json" --depfile "$W/et.d" -q \
  --result-json "$W/et.json" 2>/dev/null || fail "a JSON-typed tune file was rejected: $(cat "$W/et.json")"
if ls "$W"/et/*.tune-id.cpp >/dev/null 2>&1; then
  grep -q 'tv1:d=on;f=0;lw=256;be=slop' "$W"/et/*.tune-id.cpp || fail "the JSON bool/int tune file was not applied"
fi
grep -q "tf_json.json" "$W/et.d" || fail "the tune file is not in the --depfile: $(cat "$W/et.d")"
grep -q "tf_json.json" "$W/et.json" || fail "the tune file is not an envelope input"
"$LHD" sim "$PRP" --workdir "$W/tfs" --setup-only --set sim.tune.profile=off --set sim.tune.file="$W/tf_json.json" -q \
  --result-json "$W/tfs.json" 2>/dev/null || fail "lhd sim with a JSON-typed tune file failed: $(cat "$W/tfs.json")"
grep -q "tf_json.json" "$W/tfs.json" || fail "lhd sim: the tune file is not an envelope input"
for bad in '"dirty":1' '"fence":1.5' '"backend":true' '"live_words":null'; do
  echo "{\"schema\":\"lhd-sim-tune-file-1\",\"knobs\":{$bad}}" >"$W/tf_bad.json"
  "$LHD" sim "$PRP" --workdir "$W/tfs" --setup-only --set sim.tune.file="$W/tf_bad.json" -q >"$W/r.json" 2>/dev/null \
    && fail "tune file knob $bad must fail"
  grep -q '"class":"config"' "$W/r.json" || fail "tune file knob $bad: not a config error: $(cat "$W/r.json")"
  knob=$(echo "$bad" | cut -d'"' -f2)
  grep -q "bad knob: $knob" "$W/r.json" || fail "tune file knob $bad: the error does not name $knob: $(cat "$W/r.json")"
done

echo "PASS: sim.tune options, renames, grammar, [sim.tune] config, P0.1, envelope, emit-dir BUILD, tune-file types + inputs"

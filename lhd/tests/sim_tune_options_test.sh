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
#     bakes the explicit vector; a rolled loop's every `*.llvm.o` (including
#     `<stem>.native-loop.llvm.o`) is linked and hashed, and `*.inc` is a header;
#   - a rolled loop with a gated or secondary clock among several clock ports is refused;
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
mod acc(din:U8) -> (sum:U32@[0]) {
  reg total:U32 = 0
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
for kv in tune.profile:auto tune.dirty:auto tune.fence:auto tune.live_words:auto tune.backend:llvm tune.file: tune.export: \
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
# backend = "auto" keeps its meaning (the tuned decision, else slop); unset is llvm.
printf '[sim.tune]\ndirty = "on"\nfence = 0\nprofile = "off"\nbackend = "auto"\n' >"$W/tune.toml"
"$LHD" sim "$PRP" --config "$W/tune.toml" --setup-only --workdir "$W/cfg" -q --result-json "$W/cfg.json" 2>/dev/null \
  || fail "[sim.tune] config rejected: $(cat "$W/cfg.json")"
python3 - "$W/cfg.json" <<'PY' || fail "[sim.tune] config not applied: $(cat "$W/cfg.json")"
import json, sys
t = json.load(open(sys.argv[1]))["sim_tune"]
assert t["applied"]["vector"] == "tv1:d=on;f=0;lw=256;be=slop", t
assert t["source"]["dirty"] == "explicit" and t["source"]["fence"] == "explicit", t
assert t["source"]["backend"] == "default", t
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
assert t["applied"]["vector"] == "tv1:d=on;f=16;lw=256;be=llvm", t
assert t["source"]["dirty"] == "default" and t["source"]["backend"] == "default", t
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
  grep -q 'tv1:d=off;f=none;lw=256;be=llvm' "$W"/es/*.tune-id.cpp || fail "--emit-dir sim: did not bake the explicit vector"
fi

grep -q "alwayslink = True" "$W/es/BUILD" || fail "--emit-dir sim: BUILD must alwayslink the sim library (tune-id identity)"

# Rolled loops under the default llvm backend (two, so they fuse) write a
# `<stem>.native-loop.llvm.o` (it defines the `__lhd_loop_<mod>` its adapter
# calls) and `#include` `.inc` adapters: every object must be a BUILD input and
# the headers must cover `*.inc`.
cat >"$W/rolled.prp" <<'EOF'
mod rolled(data:U32, select_a:U4, select_b:U4) -> (a:U8@[0], b:U8@[0]) {
  mut carry_a:U8 = 128
  mut carry_b:U8 = 64
  for i in 0..<4 {
    if select_a#[i] != 0 {
      carry_a |= data#[(i*8)..+8]
    }
  }
  for j in 0..<4 {
    if select_b#[j] != 0 {
      carry_b |= data#[(j*8)..+8]
    }
  }
  a = carry_a
  b = carry_b
}
EOF
"$LHD" compile "$W/rolled.prp" --emit-dir "sim:$W/el" -q --result-json "$W/el.json" 2>/dev/null \
  || fail "compile --emit-dir sim: of a rolled loop failed: $(cat "$W/el.json")"
ls "$W"/el/*.native-loop.llvm.o >/dev/null 2>&1 || fail "--emit-dir sim: the rolled loop emitted no native-loop object"
for f in "$W"/el/*.llvm.o; do
  b=$(basename "$f")
  grep -qF "\"$b\"" "$W/el/BUILD" || fail "--emit-dir sim: BUILD does not link $b"
  grep -qF "\"$b\"" "$W/el/manifest.json" || fail "--emit-dir sim: manifest.json does not hash $b"
done
grep -qF '"*.inc"' "$W/el/BUILD" || fail "--emit-dir sim: BUILD hdrs must declare the .inc adapters"

# ---- several clock ports on a rolled loop: refused on both backends, gated or not -----
# `pair`'s second port is a secondary (edge-detected) clock the walk never toggles,
# even when it is the same root clock, and a withheld period holds the whole loop body
# (every clock domain): fail closed, not mis-simulate. (A fused body's per-loop clock
# ports, each its lane child's reference, stay legal: tests/sim/loop_fused_state.)
cat >"$W/twoclk.prp" <<'EOF'
mod pair(clk:Clock, gclk:Clock, inc:Bool) -> (a:U4@[0], b:U4@[0]) {
  reg fa:U4:[clock_pin=clk] = 0
  reg fb:U4:[clock_pin=gclk] = 0
  a = fa
  b = fb
  if inc {
    wrap fa += 1
    wrap fb += 1
  }
}
mod lanes(clk:Clock, gclk:Clock, inc:Bool) -> (sa:U8@[0], sb:U8@[0]) {
  mut acc_a:U8 = 0
  mut acc_b:U8 = 0
  for j in 0..<2 {
    const p = pair(clk=clk, gclk=gclk, inc=inc)
    wrap acc_a = acc_a + p.a
    wrap acc_b = acc_b + p.b
  }
  sa = acc_a
  sb = acc_b
}
pub mod twoclk(clk:Clock, en:Bool, inc:Bool) -> (sa:U8@[], sb:U8@[]) {
  const g = Clock(clock_pin=clk, enable=en)
  const r = lanes(clk=clk, gclk=g, inc=inc)
  sa = r.sa
  sb = r.sb
}
test twoclk.t {
  mut x = twoclk
  x.en = true
  x.inc = true
  step
}
EOF
# Same loop, both ports on the ungated top clock: `b` would never count.
sed -e 's/pub mod twoclk(clk:Clock, en:Bool, inc:Bool)/pub mod twoclk(clk:Clock, inc:Bool)/' \
  -e '/const g = Clock(clock_pin=clk, enable=en)/d' -e 's/gclk=g,/gclk=clk,/' -e '/x.en = true/d' \
  "$W/twoclk.prp" >"$W/twoclk_ungated.prp"
grep -qF 'gclk=clk,' "$W/twoclk_ungated.prp" || fail "the ungated two-clock fixture was not derived"
for be in llvm slop; do
  "$LHD" sim "$W/twoclk.prp" --setup-only --set sim.tune.backend=$be --workdir "$W/tc_$be" -q >"$W/r.json" 2>/dev/null \
    && fail "$be: a gated clock among several clock ports of a rolled loop must be refused"
  grep -qF 'has 2 clock ports and state on the secondary clock `twoclk.pair.gclk`' "$W/r.json" \
    || fail "$be: not the multi-port loop refusal: $(cat "$W/r.json")"
  "$LHD" sim "$W/twoclk_ungated.prp" --setup-only --set sim.tune.backend=$be --workdir "$W/tcu_$be" -q >"$W/r.json" 2>/dev/null \
    && fail "$be: an ungated second clock port of a rolled loop must be refused"
  grep -qF 'has 2 clock ports and state on the secondary clock `twoclk_ungated.pair.gclk`' "$W/r.json" \
    || fail "$be: not the multi-port loop refusal (ungated): $(cat "$W/r.json")"
done

# Two loops fused into one body, one lane on a gated clock: no secondary-clock state,
# but a withheld period on the gated port would also hold the ungated lanes.
cat >"$W/fusedg.prp" <<'EOF'
mod lane1(clk:Clock, a:U8) -> (q:U16@[0]) {
  reg acc:U16:[clock_pin=clk] = 0
  wrap acc = acc + a
  q = acc
}
mod body(clk:Clock, gclk:Clock, a:U8) -> (first:U16@[0], second:U16@[0]) {
  mut x:U16 = 0
  for i in 0..<3 {
    const value = lane1(clk=clk, a=(a + i)#[0..<8])
    wrap x = x + value
  }
  first = x
  mut y:U16 = 0
  for i in 0..<3 {
    const value = lane1(clk=gclk, a=(a + i + 1)#[0..<8])
    wrap y = y + value
  }
  second = y
}
pub mod fusedg(clk:Clock, en:Bool, a:U8) -> (first:U16@[], second:U16@[]) {
  const g = Clock(clock_pin=clk, enable=en)
  const r = body(clk=clk, gclk=g, a=a)
  first = r.first
  second = r.second
}
test fusedg.t {
  mut x = fusedg
  x.en = false
  x.a = 1
  step
}
EOF
for be in llvm slop; do
  "$LHD" sim "$W/fusedg.prp" --setup-only --set sim.tune.backend=$be --workdir "$W/fg_$be" -q >"$W/r.json" 2>/dev/null \
    && fail "$be: a gated port among several clock ports of a fused loop body must be refused"
  grep -qF 'has 2 clock ports and `' "$W/r.json" && grep -qF '` is gated; a withheld period' "$W/r.json" \
    || fail "$be: not the gated multi-port loop refusal: $(cat "$W/r.json")"
done

# ---- sim.tune.file: JSON types, depfile and envelope inputs ----------------------------
echo '{"schema":"lhd-sim-tune-file-1","knobs":{"dirty":true,"fence":0}}' >"$W/tf_json.json"
"$LHD" compile "$PRP" --emit-dir "sim:$W/et" --set sim.tune.file="$W/tf_json.json" --depfile "$W/et.d" -q \
  --result-json "$W/et.json" 2>/dev/null || fail "a JSON-typed tune file was rejected: $(cat "$W/et.json")"
if ls "$W"/et/*.tune-id.cpp >/dev/null 2>&1; then
  grep -q 'tv1:d=on;f=0;lw=256;be=llvm' "$W"/et/*.tune-id.cpp || fail "the JSON bool/int tune file was not applied"
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

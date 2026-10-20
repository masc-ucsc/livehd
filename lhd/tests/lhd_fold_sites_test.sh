#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# compile.cprop.engine reaches EVERY site that folds a graph body, not only the two compile cprop slots
# (livehd::fold_body, pass/aegraph/fold_body.hpp). With LHD_FOLD_BODY_LOG=1 each call prints
# `fold_body engine=<engine> site=<site>` on stderr; for each engine this test drives one command per site
# family and asserts the line appears with exactly that engine and with no other engine's name:
#
#   specialize.refold + cgen.sim.refold   lhd sim (sim.unroll_sites splices a small loop; sim cost)
#   synth.loop_cleanup                    pass abc over a rolled loop
#   synth.memory_module                   pass abc --set pass.abc.memory=true over a register tile
#   synth.state_sweep                     lhd synth with synth.state_sweep=true
#   satopt.cleanup                        pass satopt over a design satopt rewrites
#   formal.lec.preflight                  lhd lec across two hierarchies
#
# The results stay sound under every engine (mapped netlists and the LEC are PROVEN), compile.sweep follows the
# engine too, and the synthesis region cache never replays one engine's regions for another.
set -u

LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_fold_sites_$$}"
mkdir -p "$W"
export LHD_FOLD_BODY_LOG=1

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# sites_of <log>: the distinct `engine=E site=S` pairs, sorted.
sites_of() { grep -a '^fold_body engine=' "$1" | sort -u; }

# expect <engine> <site> <log>: the site was folded by <engine>, and by no other engine.
expect() {
  local engine=$1 site=$2 log=$3
  grep -aq "^fold_body engine=$engine site=$site\$" "$log" || fail "engine=$engine: no 'fold_body engine=$engine site=$site' in $log: $(sites_of "$log" | tr '\n' ' ')"
  if grep -a "^fold_body engine=" "$log" | grep -aq -v "^fold_body engine=$engine site="; then
    fail "engine=$engine: another engine folded in $log: $(sites_of "$log" | tr '\n' ' ')"
  fi
}

SMALL_LOOP=inou/prp/tests/sim/loop_roll_carry.prp
LOOP_TOP=loop_roll_carry.loop_roll_carry
STATE_SRC=lhd/tests/state_sweep.v
STATE_LIB=lhd/tests/abc_arst_q.lib
TILE=lhd/tests/abc_memtile.sv
SATOPT_SRC=inou/prp/tests/equiv/instance_out_struct_ident.prp

# A register whose D input is a constant: the state sweep folds it away and refolds the body it leaves.
cat > "$W/cst.sv" <<'SV'
module cst(input logic clk, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] k, r;
  always_ff @(posedge clk) begin
    k <= 8'd5;
    r <= d;
  end
  assign y = r + k;
endmodule
SV
cat > "$W/hier.prp" <<'PRP'
comb child(a:U8, b:U8) -> (y:U10) {
  y = (a + b) + (b + a)
}
pub comb top(a:U8, b:U8) -> (o:U10) {
  const t = child(a=a, b=b)
  o = t.y + 0
}
PRP
cat > "$W/flat.prp" <<'PRP'
pub comb top(a:U8, b:U8) -> (o:U10) {
  o = (a + b) + (b + a)
}
PRP

"$LHD" pass liberty gensim "$LIB" --emit-dir verilog:"$W/models" --workdir "$W/model_work" -q >/dev/null 2>&1 || fail "gensim"
"$LHD" pass liberty gensim "$STATE_LIB" --emit-dir lg:"$W/state_models" --workdir "$W/state_model_work" -q >/dev/null 2>&1 || fail "gensim state lib"
"$LHD" compile "$SMALL_LOOP" --emit-dir lg:"$W/loop_src" --workdir "$W/loop_compile" -q >/dev/null 2>&1 || fail "compile loop"
"$LHD" compile "$SMALL_LOOP" --emit verilog:"$W/loop_ref.v" --top "$LOOP_TOP" --workdir "$W/loop_ref" -q >/dev/null 2>&1 || fail "compile loop ref"
"$LHD" pass color synth lg:"$W/loop_src" --top "$LOOP_TOP" --workdir "$W/loop_color" -q >/dev/null 2>&1 || fail "color loop"
"$LHD" compile "$TILE" --reader slang --top memtile --emit-dir lg:"$W/tile_src" --workdir "$W/tile_compile" -q -- -GN=8 >/dev/null 2>&1 || fail "compile tile"
"$LHD" pass color synth --top memtile.memtile lg:"$W/tile_src" --workdir "$W/tile_color" -q >/dev/null 2>&1 || fail "color tile"
"$LHD" compile "$SATOPT_SRC" --top top --emit-dir lg:"$W/satopt_src" --workdir "$W/satopt_compile" -q >/dev/null 2>&1 || fail "compile satopt"

for engine in classic aegraph aegraph+classic; do
  tag=${engine//+/_}
  sets=(--set "compile.cprop.engine=$engine")

  # -- sim: specialize refolds and the post-splice fold (cost=sim) -----------------------------------------------
  "$LHD" sim "$SMALL_LOOP" --setup-only --set sim.unroll_sites=100000 "${sets[@]}" --workdir "$W/sim_$tag" -q >"$W/sim_$tag.out" 2>"$W/sim_$tag.log" \
    || fail "sim engine=$engine failed: $(head -c 600 "$W/sim_$tag.out")"
  expect "$engine" cgen.sim.refold "$W/sim_$tag.log"
  expect "$engine" specialize.refold "$W/sim_$tag.log"

  # -- synthesis: loop cleanup, mapped netlist PROVEN against the compiled source ----------------------------------
  "$LHD" pass abc lg:"$W/loop_src" --top "$LOOP_TOP" --set synth.liberty="$LIB" "${sets[@]}" --emit verilog:"$W/loop_net_$tag.v" \
    --workdir "$W/loop_map_$tag" -q >"$W/loop_map_$tag.out" 2>"$W/loop_map_$tag.log" || fail "loop abc engine=$engine failed: $(head -c 600 "$W/loop_map_$tag.out")"
  expect "$engine" synth.loop_cleanup "$W/loop_map_$tag.log"
  cat "$W/loop_net_$tag.v" "$W/models/"*.v >"$W/loop_impl_$tag.v"
  "$LHD" lec --set formal.effort=proof --impl verilog:"$W/loop_impl_$tag.v" --ref verilog:"$W/loop_ref.v" --top "$LOOP_TOP" \
    --workdir "$W/loop_lec_$tag" --result-json "$W/loop_lec_$tag.json" -q >/dev/null 2>&1
  grep -q '"verdict":"proven-' "$W/loop_lec_$tag.json" || fail "loop engine=$engine: mapped netlist not PROVEN: $(head -c 600 "$W/loop_lec_$tag.json")"

  # -- synthesis: memory module lowering ----------------------------------------------------------------------------
  "$LHD" pass abc --top memtile.memtile lg:"$W/tile_src" --set synth.liberty="$LIB" --set pass.abc.memory=true "${sets[@]}" \
    --emit-dir lg:"$W/tile_net_$tag" --workdir "$W/tile_map_$tag" -q >"$W/tile_map_$tag.out" 2>"$W/tile_map_$tag.log" || fail "tile abc engine=$engine failed: $(head -c 600 "$W/tile_map_$tag.out")"
  expect "$engine" synth.memory_module "$W/tile_map_$tag.log"

  # -- synthesis: private-copy state sweep, PROVEN ------------------------------------------------------------------
  "$LHD" synth --reader slang --top sweep --workdir "$W/state_$tag" --emit-dir lg:"$W/state_net_$tag" --set synth.liberty="$STATE_LIB" \
    --set synth.state_sweep=true "${sets[@]}" --result-json "$W/state_$tag.json" -q -- "$STATE_SRC" >"$W/state_$tag.out" 2>"$W/state_$tag.log" \
    || fail "state sweep engine=$engine failed: $(head -c 600 "$W/state_$tag.json")"
  "$LHD" synth --reader slang --top cst --workdir "$W/cst_$tag" --set synth.liberty="$STATE_LIB" --set synth.state_sweep=true "${sets[@]}" \
    --result-json "$W/cst_$tag.json" -q -- "$W/cst.sv" >"$W/cst_$tag.out" 2>"$W/cst_$tag.log" \
    || fail "constant-register sweep engine=$engine failed: $(head -c 600 "$W/cst_$tag.json")"
  expect "$engine" synth.state_sweep "$W/cst_$tag.log"
  "$LHD" lec --set formal.effort=proof --impl lg:"$W/state_net_$tag" --ref lg:"$W/state_$tag/synth/lg" --lib lg:"$W/state_models" --top sweep \
    --workdir "$W/state_lec_$tag" --result-json "$W/state_lec_$tag.json" -q >/dev/null 2>&1
  grep -q '"verdict":"proven-full"' "$W/state_lec_$tag.json" || fail "state sweep engine=$engine: netlist not PROVEN: $(head -c 600 "$W/state_lec_$tag.json")"

  # -- satopt cleanup ------------------------------------------------------------------------------------------------
  "$LHD" pass satopt lg:"$W/satopt_src" --top top "${sets[@]}" --emit-dir lg:"$W/satopt_out_$tag" --workdir "$W/satopt_$tag" -q \
    >"$W/satopt_$tag.out" 2>"$W/satopt_$tag.log" || fail "satopt engine=$engine failed: $(head -c 600 "$W/satopt_$tag.out")"
  expect "$engine" satopt.cleanup "$W/satopt_$tag.log"

  # -- LEC preflight (no bitwidth follows): across two hierarchies, PROVEN -------------------------------------------
  "$LHD" lec --ref "$W/hier.prp" --impl "$W/flat.prp" --top top "${sets[@]}" --set formal.effort=proof --workdir "$W/lec_$tag" \
    --result-json "$W/lec_$tag.json" -q >"$W/lec_$tag.out" 2>"$W/lec_$tag.log" || fail "lec engine=$engine failed: $(head -c 600 "$W/lec_$tag.json")"
  expect "$engine" formal.lec.preflight "$W/lec_$tag.log"
  grep -q '"verdict":"proven-' "$W/lec_$tag.json" || fail "lec engine=$engine: not PROVEN: $(head -c 600 "$W/lec_$tag.json")"

  # -- compile.sweep: its per-round fold follows the engine --------------------------------------------------------
  "$LHD" compile "$STATE_SRC" --reader slang --top sweep --set compile.sweep=true "${sets[@]}" --workdir "$W/csweep_$tag" -q >/dev/null 2>&1 \
    || fail "compile.sweep engine=$engine failed"
  n_ae=$(ls "$W/csweep_$tag/logs/" | grep -c 'pass_aegraph')
  n_cp=$(ls "$W/csweep_$tag/logs/" | grep -c 'pass_cprop')
  case "$engine" in
    classic) [ "$n_ae" -eq 0 ] || fail "compile.sweep engine=classic ran pass.aegraph" ;;
    aegraph) [ "$n_cp" -eq 0 ] && [ "$n_ae" -ge 2 ] || fail "compile.sweep engine=aegraph: pass.aegraph x$n_ae pass.cprop x$n_cp" ;;
    *) [ "$n_cp" -ge 2 ] && [ "$n_ae" -ge 2 ] || fail "compile.sweep engine=$engine: pass.aegraph x$n_ae pass.cprop x$n_cp" ;;
  esac
done

# The default is classic: no setting, no e-graph fold anywhere.
"$LHD" lec --ref "$W/hier.prp" --impl "$W/flat.prp" --top top --workdir "$W/lec_default" -q >/dev/null 2>"$W/lec_default.log" || fail "default lec failed"
expect classic formal.lec.preflight "$W/lec_default.log"

# The synthesis region cache never mixes engines: one workdir, same source. A run under a different engine than the one
# that filled the cache replays nothing (it is salted by engine), and a repeat under the same engine hits every region.
cache_hits() {  # <json>: abc region cache hits
  grep -o '"hits":[0-9]*' "$1" | head -1 | cut -d: -f2
}
n=0
for step in classic:miss classic:hit aegraph:miss aegraph:hit aegraph+classic:miss classic:miss; do
  engine=${step%%:*}
  want=${step##*:}
  n=$((n + 1))
  "$LHD" pass abc lg:"$W/loop_src" --top "$LOOP_TOP" --set synth.liberty="$LIB" --set "compile.cprop.engine=$engine" \
    --emit-dir lg:"$W/cache_net_$n" --workdir "$W/cache_work" --result-json "$W/cache_$n.json" -q >/dev/null 2>&1 || fail "cache run $n engine=$engine failed"
  hits=$(cache_hits "$W/cache_$n.json")
  if [ "$want" = miss ]; then
    [ "$hits" = 0 ] || fail "cache: run $n engine=$engine replayed another engine's regions (hits=$hits)"
  else
    [ "$hits" != 0 ] || fail "cache: run $n engine=$engine (a repeat) found nothing (hits=$hits)"
  fi
done

echo "PASS: compile.cprop.engine reaches every fold site (sim, synth, satopt, LEC preflight, compile.sweep), stays sound, and keys the caches"

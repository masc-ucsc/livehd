#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# docs/opt_loop_incr.md L8 focused acceptance: cache gating/telemetry,
# exact comment-only Tier-B reuse, semantic invalidation, corruption recovery,
# structural warm==cold, and ghost-definition elimination.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_compile_cache_$$}"
mkdir -p "$W/src"

# The compile tenant must coexist with the already-landed flat cache/build
# tenants until the shared L7 substrate migrates them. Treat these as owned by
# abc, formal, and sim and prove compile never clears or rewrites them.
mkdir -p "$W/w/abc_cache" "$W/w/sim"
printf 'abc-owned\n' > "$W/w/abc_cache/owner"
printf 'formal-owned\n' > "$W/w/formal_cache.json"
printf 'sim-owned\n' > "$W/w/sim/owner"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$W/src/leaf.prp" <<'EOF'
pub comb add1(a:u8) -> (r:u9) { r = a + 1 }
EOF
cat > "$W/src/side.prp" <<'EOF'
pub comb twice(a:u8) -> (r:u9) { r = a + a }
EOF
cat > "$W/src/top.prp" <<'EOF'
const leaf = import("leaf")
const side = import("side")
mod top(x:u8) -> (y:u9@[]) { y = leaf.add1(a=x) }
EOF

compile() {
  local result=$1
  shift
  "$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/lg" --emit-dir "verilog:$W/v" --workdir "$W/w" \
    -q --result-json "$result" "$@" || fail "compile failed: $(cat "$result" 2>/dev/null)"
}

compile_lg_only() {
  local result=$1 out=$2
  "$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$out" --workdir "$W/w" \
    -q --result-json "$result" || fail "lg-only compile failed: $(cat "$result" 2>/dev/null)"
}

field() {
  python3 - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    value = json.load(f)
for key in sys.argv[2].split('.'):
    value = value[key]
print(str(value).lower() if isinstance(value, bool) else value)
PY
}

has_phase() {
  python3 - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
raise SystemExit(0 if any(p.get("name") == sys.argv[2] for p in data.get("phases", [])) else 1)
PY
}

inode() {
  python3 - "$1" <<'PY'
import os, sys
print(os.stat(sys.argv[1]).st_ino)
PY
}

# Cold: both closure files parse/lower and the exact shared-layout artifacts land.
compile "$W/cold.json"
[ "$(field "$W/cold.json" incremental.compile.enabled)" = true ] || fail "cache not enabled with named workdir"
[ "$(field "$W/cold.json" incremental.compile.misses)" = 3 ] || fail "cold closure did not report three misses"
has_phase "$W/cold.json" pass.upass || fail "cold compile skipped upass"
SCOPE="$W/w/incr/scopes/compile/top"
[ -f "$SCOPE/inventory.json" ] || fail "scope inventory missing"
[ -n "$(find "$SCOPE/ln" -name tree.bin -print -quit)" ] || fail "Tier-A per-unit LNAST cache missing"
[ -f "$SCOPE/lg/graph_inventory.json" ] || fail "Tier-B inventory missing"
cp -R "$W/lg" "$W/cold_lg" || fail "could not preserve the cold lg reference"
ls "$W/v/"*.v >/dev/null 2>&1 || fail "cold compile emitted no verilog"
grep -q 'input.*clock' "$W/v/"*.v && fail "combinational leaf unexpectedly clocked its parent"
# Capture-then-validate: a here-doc fed from a failed $(python3 ...) would set
# both names empty and every later inode-stability assertion would compare
# directory-inode==directory-inode (vacuously green).
SNAPSHOT_NAMES=$(python3 - "$SCOPE/inventory.json" <<'PY'
import json, sys
units = {u["name"]: u for u in json.load(open(sys.argv[1]))["units"]}
print(units["leaf"]["snapshot"], units["side"]["snapshot"])
PY
) || fail "could not read snapshot names from the inventory"
read -r LEAF_SNAPSHOT SIDE_SNAPSHOT <<<"$SNAPSHOT_NAMES"
[ -n "$LEAF_SNAPSHOT" ] && [ -n "$SIDE_SNAPSHOT" ] || fail "inventory reported empty snapshot names"
SIDE_SNAPSHOT_INODE=$(inode "$SCOPE/pyrope/$SIDE_SNAPSHOT") || fail "missing side snapshot"
LEAF_LNAST_INODE=$(inode "$SCOPE/ln/${LEAF_SNAPSHOT%.prp}/tree.bin") || fail "missing leaf compact LNAST"

# Byte-identical warm run: no parser redo and no lower/recipe/formal work.
compile "$W/warm.json"
[ "$(field "$W/warm.json" incremental.compile.misses)" = 0 ] || fail "warm compile reported a miss"
[ "$(field "$W/warm.json" incremental.compile.hits)" -ge 6 ] || fail "warm compile did not restore parse+graph units"
has_phase "$W/warm.json" pass.upass && fail "warm compile reran upass"
has_phase "$W/warm.json" lnast.tolg && fail "warm compile reran tolg"
[ "$("$LHD" tool diff "lg:$W/cold_lg" "lg:$W/lg" --structural -q)" = identical ] || fail "warm lg is not structurally identical to cold"

# A clean lg-only consumer reuses the immutable graph artifact as a filesystem
# generation. It must not deserialize either IR merely to materialize a second
# output directory, and the materialized result remains structurally exact.
compile_lg_only "$W/artifact.json" "$W/artifact_lg"
has_phase "$W/artifact.json" compile.cache.lg_artifact || fail "lg-only hit did not use artifact-level reuse"
has_phase "$W/artifact.json" compile.cache.lg_lookup && fail "lg-only artifact hit deserialized the graph library"
has_phase "$W/artifact.json" pass.lnastfmt && fail "lg-only artifact hit materialized cached LNASTs"
[ "$("$LHD" tool diff "lg:$W/cold_lg" "lg:$W/artifact_lg" --structural -q)" = identical ] \
  || fail "artifact-level lg reuse differs structurally from cold"

# Comment-only source change: exactly leaf reparses, canonical LNAST stays the
# same, and the post-formal graph closure still restores without lowering.
python3 - "$W/src/leaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text("// comment-only edit\n" + p.read_text())
PY
compile "$W/comment.json"
[ "$(field "$W/comment.json" incremental.compile.misses)" = 1 ] || fail "comment edit did not reparse exactly one file"
has_phase "$W/comment.json" pass.upass && fail "comment edit unnecessarily reran upass"
[ "$("$LHD" tool diff "lg:$W/cold_lg" "lg:$W/lg" --structural -q)" = identical ] || fail "comment-hit lg differs structurally from cold"
# The manifest is the Tier-A commit point. A one-file comment update replaces
# that source snapshot only; it must not rebuild the unchanged source object or
# the semantically-identical compact LNAST merely to publish a generation.
[ "$(inode "$SCOPE/pyrope/$SIDE_SNAPSHOT")" = "$SIDE_SNAPSHOT_INODE" ] \
  || fail "comment update republished an unchanged source snapshot"
[ "$(inode "$SCOPE/ln/${LEAF_SNAPSHOT%.prp}/tree.bin")" = "$LEAF_LNAST_INODE" ] \
  || fail "comment update republished a semantically-identical LNAST"
cmp "$W/src/leaf.prp" "$SCOPE/pyrope/$LEAF_SNAPSHOT" >/dev/null \
  || fail "comment update did not publish the exact hermetic source snapshot"

# Context identity is exact manifest data, not a 64-bit authorization hash.
# A mismatched descriptor refuses the entire proposal and rebuilds the scope.
python3 - "$SCOPE/inventory.json" <<'PY'
import json, sys
from pathlib import Path
p = Path(sys.argv[1])
d = json.loads(p.read_text())
d["context"] += "|manufactured-collision"
p.write_text(json.dumps(d, separators=(",", ":")) + "\n")
PY
compile "$W/context_mismatch.json"
[ "$(field "$W/context_mismatch.json" incremental.compile.refused)" -ge 1 ] \
  || fail "exact context mismatch was not refused"
[ "$(field "$W/context_mismatch.json" incremental.compile.misses)" = 3 ] \
  || fail "context mismatch did not rebuild the complete source closure"
[ "$("$LHD" tool diff "lg:$W/cold_lg" "lg:$W/lg" --structural -q)" = identical ] \
  || fail "context-mismatch rebuild differs structurally from cold"

# Semantic exporter edit invalidates the closure and performs a real rebuild.
# First manufacture the only collision that a deterministic unit test can:
# copy the new generation's semantic fingerprints into the old generation
# while leaving its old LNAST/graphs in place. The digest must only propose;
# exact type/name tree comparison must still reject the stale leaf and parent.
python3 - "$W/src/leaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text(p.read_text().replace("a + 1", "a + 2"))
PY
"$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/collision_probe_lg" --workdir "$W/collision_probe_w" \
  -q --result-json "$W/collision_probe.json" || fail "collision probe cold compile failed"
python3 - "$SCOPE" "$W/collision_probe_w/incr/scopes/compile/top" <<'PY'
import json, sys
from pathlib import Path

old = Path(sys.argv[1])
new = Path(sys.argv[2])

old_inv = json.loads((old / "inventory.json").read_text())
new_inv = json.loads((new / "inventory.json").read_text())
new_semantic = {u["name"]: u["semantic_hash"] for u in new_inv["units"]}
for unit in old_inv["units"]:
    unit["semantic_hash"] = new_semantic[unit["name"]]
(old / "inventory.json").write_text(json.dumps(old_inv, separators=(",", ":")) + "\n")

old_graph = json.loads((old / "lg/graph_inventory.json").read_text())
new_graph = json.loads((new / "lg/graph_inventory.json").read_text())
new_keys = {g["name"]: g.get("unit_key", "") for g in new_graph["graphs"]}
old_graph["closure_key"] = new_graph["closure_key"]
for graph in old_graph["graphs"]:
    graph["unit_key"] = new_keys[graph["name"]]
(old / "lg/graph_inventory.json").write_text(json.dumps(old_graph, separators=(",", ":")) + "\n")
PY
compile "$W/edit.json"
[ "$(field "$W/edit.json" incremental.compile.misses)" = 1 ] || fail "semantic edit did not miss the changed file"
has_phase "$W/edit.json" pass.upass || fail "semantic-hash collision incorrectly restored stale graphs"
[ "$(field "$W/edit.json" incremental.compile.hits)" -ge 2 ] || fail "semantic edit did not reuse the unrelated side unit"
[ "$("$LHD" tool diff "lg:$W/cold_lg" "lg:$W/lg" --structural -q)" != identical ] \
  || fail "structural H5 checker accepted a real semantic edit"

# Partial dirty-cone H5: compare the mixed restored+fresh result against an
# honestly cache-disabled compile of the edited source.
"$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/edit_cold_lg" --workdir "$W/edit_cold_w" \
  --set lhd.incremental=false -q --result-json "$W/edit_cold.json" || fail "semantic-edit cold reference failed"
[ "$("$LHD" tool diff "lg:$W/edit_cold_lg" "lg:$W/lg" --structural -q)" = identical ] \
  || fail "mixed restored+fresh semantic edit differs from cold"

# A damaged graph body is a refused cold miss, never an assert/abort. The fresh
# rebuild republishes a valid graph generation.
BODY=$(find "$SCOPE/lg" -name body.bin -print -quit)
[ -n "$BODY" ] || fail "no cached graph body to damage"
python3 - "$BODY" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_bytes(b"damaged")
PY
compile_lg_only "$W/damaged.json" "$W/lg"
[ "$(field "$W/damaged.json" incremental.compile.refused)" -ge 1 ] || fail "damaged cache was not attributed as refused"
has_phase "$W/damaged.json" pass.upass || fail "damaged cache did not recover through a cold rebuild"

# A leaf gaining state changes its implicit clock interface and therefore every
# importer/ancestor GraphIO. The unchanged importer must rebuild, not retain a
# stale no-clock boundary; the final mixed result must still equal cold.
cat > "$W/src/leaf.prp" <<'EOF'
pub mod add1(a:u8) -> (r:u9@[1]) {
  reg q:u9 = 0
  q = a + 2
  r = q
}
EOF
compile "$W/stateful.json"
has_phase "$W/stateful.json" pass.upass || fail "stateful exporter did not invalidate its importer"
grep -q 'input.*clock' "$W/v/"*.v || fail "leaf state did not re-port the parent clock"
"$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/stateful_cold_lg" --workdir "$W/stateful_cold_w" \
  --set lhd.incremental=false -q --result-json "$W/stateful_cold.json" || fail "stateful cold reference failed"
STATE_DIFF=$("$LHD" tool diff "lg:$W/stateful_cold_lg" "lg:$W/lg" --structural -q)
STATE_TEXT=$("$LHD" tool diff "lg:$W/stateful_cold_lg" "lg:$W/lg" --top leaf.add1 -q)
[ "$STATE_DIFF" = identical ] || fail "statefulness dirty cone differs from cold: $STATE_DIFF; $STATE_TEXT"

# The inverse mixed cone is equally load-bearing: keep the stateful child clean
# (its cached concrete mod has only a metadata stub) and edit the parent. The
# restored child must retain its declares-reg/reset facts so the freshly lowered
# parent still mints and forwards the implicit clock/reset ABI.
python3 - "$W/src/top.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text(p.read_text().replace(
    "mod top(x:u8) -> (y:u9@[]) { y = leaf.add1(a=x) }",
    "mod top(x:u8) -> (y:u9@[]) { const changed = x ^ 1; y = leaf.add1(a=changed) }"))
PY
compile "$W/stateful_parent_edit.json"
[ "$(field "$W/stateful_parent_edit.json" incremental.compile.misses)" = 1 ] \
  || fail "stateful-child parent edit did not miss exactly the parent"
[ "$(field "$W/stateful_parent_edit.json" incremental.compile.hits)" -ge 1 ] \
  || fail "stateful-child parent edit did not restore the clean child"
grep -q 'input.*clock' "$W/v/"*.v || fail "restored stateful child lost the parent clock ABI"
"$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/stateful_parent_edit_cold_lg" \
  --workdir "$W/stateful_parent_edit_cold_w" --set lhd.incremental=false -q \
  --result-json "$W/stateful_parent_edit_cold.json" || fail "stateful-child parent-edit cold reference failed"
[ "$("$LHD" tool diff "lg:$W/stateful_parent_edit_cold_lg" "lg:$W/lg" --structural -q)" = identical ] \
  || fail "restored stateful child with dirty parent differs from cold"

# Tier-A damage is also a refused cold miss. Remove the exact source snapshot
# for one unchanged unit; the compile must reparse and republish it cleanly.
SNAPSHOT=$(python3 - "$SCOPE/inventory.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    units = json.load(f)["units"]
print(next(u["snapshot"] for u in units if u["name"] == "top"))
PY
)
rm -f "$SCOPE/pyrope/$SNAPSHOT"
compile "$W/missing_snapshot.json"
[ "$(field "$W/missing_snapshot.json" incremental.compile.refused)" -ge 1 ] \
  || fail "missing Tier-A snapshot was not attributed as refused"
[ "$(field "$W/missing_snapshot.json" incremental.compile.misses)" = 1 ] \
  || fail "missing Tier-A snapshot did not reparse exactly its unit"

[ "$(cat "$W/w/abc_cache/owner")" = abc-owned ] || fail "compile collided with the abc workdir tenant"
[ "$(cat "$W/w/formal_cache.json")" = formal-owned ] || fail "compile collided with the formal workdir tenant"
[ "$(cat "$W/w/sim/owner")" = sim-owned ] || fail "compile collided with the sim workdir tenant"

# The off switch leaves no ambiguity in telemetry and always runs the full path.
"$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/off_lg" --workdir "$W/off_w" \
  --set lhd.incremental=false -q --result-json "$W/off.json" || fail "cache-off compile failed"
[ "$(field "$W/off.json" incremental.compile.enabled)" = false ] || fail "cache-off telemetry says enabled"
[ "$(field "$W/off.json" incremental.compile.hits)" = 0 ] || fail "cache-off reported hits"
has_phase "$W/off.json" pass.upass || fail "cache-off did not run the full path"
[ ! -e "$W/off_w/incr" ] || fail "cache-off created persistent incremental state"

# I5: publishing failure is never softened into an endlessly recomputed
# success. Make only the compile scope unwritable, require a clean nonzero exit,
# then restore permissions so the test sandbox can clean itself.
mkdir -p "$W/store_fail_w/incr/scopes/compile/top"
chmod 500 "$W/store_fail_w/incr/scopes/compile/top"
if "$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/store_fail_lg" \
  --workdir "$W/store_fail_w" -q --result-json "$W/store_fail.json"; then
  chmod 700 "$W/store_fail_w/incr/scopes/compile/top"
  fail "compile succeeded after a cache store failure"
fi
chmod 700 "$W/store_fail_w/incr/scopes/compile/top"
[ "$(field "$W/store_fail.json" incremental.compile.store_failed)" -ge 1 ] \
  || fail "cache store failure was not reported"

# Rename/delete: a semantic rebuild prunes the removed graph from both the
# emitted library and the cache generation (no ghost defs).
python3 - "$W/src/top.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text('mod top(x:u8) -> (y:u9@[0]) { y = x + 2 }\n')
PY
compile "$W/delete.json"
# Capture-then-grep: a pipe would take grep's exit status, so a crashing
# `tool cat` would vacuously pass the ghost checks.
EMITTED_CAT=$("$LHD" tool cat "lg:$W/lg" -q) || fail "tool cat on emitted lg failed"
CACHED_CAT=$("$LHD" tool cat "lg:$SCOPE/lg" -q) || fail "tool cat on cached lg failed"
if grep -q 'leaf.add1' <<<"$EMITTED_CAT"; then
  fail "deleted leaf graph survived in emitted lg"
fi
if grep -q 'leaf.add1' <<<"$CACHED_CAT"; then
  fail "deleted leaf graph survived in cached lg"
fi

# Real shared-workdir coexistence, not just path sentinels: exercise the three
# established sibling tenants in the same workdir, then prove this compile
# scope still hits. Each flow owns its own namespace and may replace the
# sentinel inside that namespace; none may damage incr/scopes/compile/top.
"$LHD" pass abc --top top.top "lg:$W/lg" --emit-dir "lg:$W/shared_net" \
  --set abc.library=inou/prp/tests/abc/test.lib --workdir "$W/w" -q \
  --result-json "$W/shared_abc.json" || fail "shared-workdir abc failed"

cat > "$W/src/shared_formal.prp" <<'EOF'
mod shared_formal(sel:u3, en:bool) -> (value:u16@[0]) {
  reg onehot:u16 = 0
  value = onehot
  assert((onehot & (onehot - 1)) == 0, "onehot invariant")
  if en { onehot = 1 << sel }
}
EOF
"$LHD" formal verify "$W/src/shared_formal.prp" --top shared_formal --workdir "$W/w" \
  --set formal.bound=3 --set formal.simfail_run=false -q \
  --result-json "$W/shared_formal.json" || fail "shared-workdir formal failed"

cat > "$W/src/shared_leaf.prp" <<'EOF'
pub comb shared_leaf(a:u8) -> (r:u8) { r = a }
EOF
cat > "$W/src/shared_tb.prp" <<'EOF'
const leaf = import("shared_leaf.shared_leaf")
test shared_tb.smoke(cycles:u20 = 1) {
  mut dut = leaf
  tick cycles clocks=(clock=1) {
    dut.a = 3
    step
  }
}
EOF
"$LHD" sim "$W/src/shared_tb.prp" --setup-only --workdir "$W/w" -q \
  --result-json "$W/shared_sim.json" || fail "shared-workdir sim failed"

compile "$W/post_shared.json"
[ "$(field "$W/post_shared.json" incremental.compile.misses)" = 0 ] \
  || fail "sibling workdir tenants evicted the compile scope"
[ -d "$W/w/abc_cache" ] || fail "shared-workdir abc tenant missing"
[ -s "$W/w/formal_cache.json" ] || fail "shared-workdir formal tenant missing"
[ -d "$W/w/sim" ] || fail "shared-workdir sim tenant missing"

# ---------------------------------------------------------------------------
# A `pass.formal` WARNING travels WITH the generation instead of poisoning it.
# A DEFERRED warning used to make compile_cache_store_graphs return before it
# ever wrote graph_inventory.json, so a design with one unprovable obligation
# cached no graph at all and every warm compile re-ran upass + tolg + cprop +
# pass.formal for the whole design (measured on minion: five `onehot-deferred`
# warnings, warm 4.5 s against cold 5.0 s). The generation now carries the
# warnings; a TOTAL restore replays them verbatim, and a partial restore -- which
# has no per-graph attribution to split live from stored -- is a counted refusal.
FW="$W/fw"
mkdir -p "$FW/src"
cat > "$FW/src/fleaf.prp" <<'EOF'
pub mod deferred_leaf(a:u4) -> (b:u4@[0]) {
  assert(a != 5)
  b = a + 1
}
EOF
# A third, UNRELATED unit: editing the leaf dirties the leaf and its importer,
# so without this every unit would be dirty and the restore would take the
# "nothing restorable" exit rather than the partial path under test.
cat > "$FW/src/fside.prp" <<'EOF'
pub comb fside_twice(a:u4) -> (r:u5) { r = a + a }
EOF
cat > "$FW/src/froot.prp" <<'EOF'
const fleaf = import("fleaf")
const fside = import("fside")
mod froot(c:u4) -> (d:u4@[0], e:u5@[0]) {
  const s = fleaf.deferred_leaf(a = 3)
  d = s + c
  e = fside.fside_twice(a = c)
}
EOF

fcompile() {  # RESULT_JSON DIAG_JSONL
  local result=$1 diag=$2
  "$LHD" compile "$FW/src/froot.prp" --top froot --emit-dir "lg:$FW/lg" \
    --workdir "$FW/w" -q --result-json "$result" --emit "diagnostics:$diag" \
    || fail "formal-warning compile failed: $(cat "$result" 2>/dev/null)"
}
fwarns() { grep -c '"pass":"pass.formal"' "$1" 2>/dev/null || true; }

# The whole diagnostic stream as a MULTISET of (pass, code, message, span),
# `seq` dropped. Counts and spans are both load-bearing: the sink dedups on
# (code, span, message) within a step, so a replay that carried the message but
# dropped the span silently collapses N distinct sites into one — measured on
# minion, 44 cold records replaying as 29.
fdiagset() {
  python3 - "$1" <<'PY'
import json, sys
rows = []
with open(sys.argv[1]) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        d = json.loads(line)
        sp = d.get("span") or {}
        rows.append((d.get("pass"), d.get("code"), d.get("message"), d.get("hint", ""),
                     sp.get("file", ""), sp.get("start_line"), sp.get("start_col")))
print(json.dumps(sorted(map(list, rows))))
PY
}

fcompile "$FW/cold.json" "$FW/cold.jsonl"
grep -q '"code":"assert-deferred"' "$FW/cold.jsonl" \
  || fail "fixture produced no pass.formal DEFERRED warning: $(cat "$FW/cold.jsonl")"
COLD_WARNS=$(fwarns "$FW/cold.jsonl")
[ "$COLD_WARNS" -ge 1 ] || fail "no pass.formal records to replay"
COLD_DIAGS=$(fdiagset "$FW/cold.jsonl")
FSCOPE="$FW/w/incr/scopes/compile/froot"
[ -f "$FSCOPE/lg/graph_inventory.json" ] \
  || fail "a pass.formal warning still blocks the graph generation from being stored"
cp -R "$FW/lg" "$FW/cold_lg" || fail "could not preserve the formal-warning cold lg"

# Warm, nothing changed: a total restore. No lowering, and the DEFERRED lines
# are reproduced rather than silently dropped.
fcompile "$FW/warm.json" "$FW/warm.jsonl"
has_phase "$FW/warm.json" pass.upass && fail "warm compile reran upass despite a stored generation"
has_phase "$FW/warm.json" pass.formal && fail "warm compile reran pass.formal"
[ "$(fwarns "$FW/warm.jsonl")" = "$COLD_WARNS" ] \
  || fail "warm restore did not replay the pass.formal warnings ($(cat "$FW/warm.jsonl"))"
grep -q '"code":"assert-deferred"' "$FW/warm.jsonl" || fail "replayed record lost its code"
[ "$(fdiagset "$FW/warm.jsonl")" = "$COLD_DIAGS" ] \
  || fail "warm diagnostics differ from cold: $(fdiagset "$FW/cold.jsonl") vs $(fdiagset "$FW/warm.jsonl")"
[ "$("$LHD" tool diff "lg:$FW/cold_lg" "lg:$FW/lg" --structural -q)" = identical ] \
  || fail "warm restore of a warning-carrying generation is not structurally cold-equal"

# Comment-only edit: still a total restore (the unit key is post-parse), so the
# replay path is what the incremental benchmark actually exercises.
python3 - "$FW/src/fleaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text("// comment-only edit\n" + p.read_text())
PY
fcompile "$FW/comment.json" "$FW/comment.jsonl"
has_phase "$FW/comment.json" pass.upass && fail "comment-only edit reran upass"
[ "$(fwarns "$FW/comment.jsonl")" = "$COLD_WARNS" ] \
  || fail "comment-only restore dropped the pass.formal warnings"
[ "$(fdiagset "$FW/comment.jsonl")" = "$COLD_DIAGS" ] \
  || fail "comment-only restore changed the diagnostic stream"
# The generation itself must carry every graph-pipeline record, spans included:
# a store-side drop would only surface on some LATER restore.
python3 - "$FSCOPE/lg/graph_inventory.json" "$COLD_WARNS" <<'PY'
import json, sys
inv = json.load(open(sys.argv[1]))
stored = inv.get("pipeline_diags")
if stored is None:
    raise SystemExit("FAIL: generation carries no pipeline_diags array")
if len(stored) < int(sys.argv[2]):
    raise SystemExit(f"FAIL: generation stored {len(stored)} records, cold emitted {sys.argv[2]}")
# The whole Diagnostic rides, not a hand-picked subset: a field dropped here is
# a field a warm restore silently loses.
for row in stored:
    for key in ("severity", "pass", "code", "category", "message", "hint", "span", "see", "notes",
                "deferred", "verdict", "engine", "duration_ms", "attrs"):
        if key not in row:
            raise SystemExit(f"FAIL: stored record is missing '{key}': {row}")
    if row["severity"] == "error":
        raise SystemExit(f"FAIL: an error-severity record was cached: {row}")
PY

# Semantic edit: the dirty cone re-runs pass.formal and emits its own warnings,
# so the stored set must NOT be replayed on top. That is a principled refusal
# (I5) and it is counted, not hidden.
python3 - "$FW/src/fleaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text(p.read_text().replace("a + 1", "a + 3"))
PY
fcompile "$FW/edit.json" "$FW/edit.jsonl"
has_phase "$FW/edit.json" pass.upass || fail "semantic edit did not re-lower the dirty cone"
[ "$(field "$FW/edit.json" incremental.compile.refused)" -ge 1 ] \
  || fail "a partial restore of a warning-carrying generation was not counted as refused"
[ "$(fwarns "$FW/edit.jsonl")" = "$COLD_WARNS" ] \
  || fail "semantic edit did not reproduce the pass.formal warnings live"
"$LHD" compile "$FW/src/froot.prp" --top froot --emit-dir "lg:$FW/edit_cold_lg" \
  --workdir "$FW/edit_cold_w" --set lhd.incremental=false -q --result-json "$FW/edit_cold.json" \
  || fail "formal-warning cold reference failed"
[ "$("$LHD" tool diff "lg:$FW/edit_cold_lg" "lg:$FW/lg" --structural -q)" = identical ] \
  || fail "refused partial restore diverged from cold"

# A damaged cache scope may not turn a COMPLETE live compile into a config
# error. Overlaying validated clean bodies is EXACTNESS only (H5-exact
# presentation of unchanged graphs); the live pipeline has already produced a
# checked result for every graph. So an unreadable cached body is a WARNING plus
# a cold-equal result -- only a HALF-FINISHED transplant (bodies deleted from
# the destination, merge then failed) may fail the build.
python3 - "$FW/src/fleaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text(p.read_text().replace("a + 3", "a + 4"))
PY
FBODY=$(find "$FSCOPE/lg" -name 'body.bin' -print 2>/dev/null | head -1)
[ -n "$FBODY" ] || fail "no cached graph body to damage"
printf 'damaged' > "$FBODY"
fcompile "$FW/ovdmg.json" "$FW/ovdmg.jsonl"
grep -q '"code":"cache-overlay-declined"' "$FW/ovdmg.jsonl" \
  || fail "a declined clean-body overlay was not reported: $(cat "$FW/ovdmg.jsonl")"
if grep '"code":"cache-overlay-declined"' "$FW/ovdmg.jsonl" | grep -q '"severity":"error"'; then
  fail "a cosmetic overlay refusal was reported at error severity"
fi
"$LHD" compile "$FW/src/froot.prp" --top froot --emit-dir "lg:$FW/ovdmg_cold_lg" \
  --workdir "$FW/ovdmg_cold_w" --set lhd.incremental=false -q --result-json "$FW/ovdmg_cold.json" \
  || fail "overlay-declined cold reference failed"
[ "$("$LHD" tool diff "lg:$FW/ovdmg_cold_lg" "lg:$FW/lg" --structural -q)" = identical ] \
  || fail "a declined overlay changed the compile result"
# This run republishes a healthy generation, so the NEXT compile is warm again
# and must NOT replay the overlay warning: it describes a SCOPE, not the design,
# so it has to sit OUTSIDE the stored [diag_mark, diag_end) window.
fcompile "$FW/ovheal.json" "$FW/ovheal.jsonl"
if grep -q '"code":"cache-overlay-declined"' "$FW/ovheal.jsonl"; then
  fail "the declined-overlay warning was cached into the generation and replayed"
fi

# A clean generic `mod` is NOT an ordinary restored Sub body. A dirty caller
# needs the template statements to materialize its concrete specialization.
# Storing every mod metadata-only used to leave `g.madd` registered but
# non-inlinable here, after which tolg tried to instantiate the nonexistent
# unspecialized GraphIO. Keep this as a genuinely mixed hit/miss run: editing
# leaf dirties leaf+top while the generic owner and its u8 specialization hit.
GW="$W/generic"
mkdir -p "$GW/src"
cat > "$GW/src/g.prp" <<'EOF'
pub mod madd<T>(a:T, b:T) -> (r:T@[0]) { r = a ^ b }
EOF
cat > "$GW/src/leaf.prp" <<'EOF'
pub comb bump(a:u8) -> (r:u8) { wrap r = a + 1 }
EOF
cat > "$GW/src/top.prp" <<'EOF'
const madd = import("g.madd")
const leaf = import("leaf")
mod top(x:u8) -> (y:u8@[0]) {
  const v = leaf.bump(a=x)
  y = madd<u8>(a=v, b=x)
}
EOF
gcompile() {  # RESULT_JSON OUTPUT_LG WORKDIR [extra options]
  local result=$1 out=$2 work=$3
  shift 3
  "$LHD" compile "$GW/src/top.prp" --top top --emit-dir "lg:$out" --workdir "$work" \
    -q --result-json "$result" "$@" || fail "generic-template compile failed: $(cat "$result" 2>/dev/null)"
}
gcompile "$GW/cold.json" "$GW/lg" "$GW/w"
python3 - "$GW/src/leaf.prp" <<'PY'
from pathlib import Path
p = Path(__import__('sys').argv[1])
p.write_text(p.read_text().replace("a + 1", "a + 2"))
PY
gcompile "$GW/mixed.json" "$GW/lg" "$GW/w"
[ "$(field "$GW/mixed.json" incremental.compile.hits)" -ge 2 ] \
  || fail "generic-template mixed run restored no clean units"
[ "$(field "$GW/mixed.json" incremental.compile.misses)" -ge 1 ] \
  || fail "generic-template semantic edit reported no dirty unit"
GCAT=$("$LHD" tool cat "lg:$GW/lg" -q) || fail "could not inspect generic-template mixed output"
grep -q 'g.madd__u8_u8' <<<"$GCAT" \
  || fail "generic-template mixed run did not retain the concrete specialization"
gcompile "$GW/edit_cold.json" "$GW/edit_cold_lg" "$GW/edit_cold_w" --set lhd.incremental=false
[ "$("$LHD" tool diff "lg:$GW/edit_cold_lg" "lg:$GW/lg" --structural -q)" = identical ] \
  || fail "generic-template mixed restored+fresh result differs from cold"

echo "PASS: incremental Pyrope compile cache"

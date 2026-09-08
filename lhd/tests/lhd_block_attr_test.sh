#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for block-scoped synthesis attributes (2opt-freq B):
#   `{ ::[abc='<flow>', color=N] stmts }` makes the block its own synthesis
#   partition region with a per-region ABC flow override.
#
#   1. LEC invariance: the annotation must change ZERO semantics — the
#      annotated source is PROVEN equivalent to the same source with the
#      attribute stripped.
#   2. compile -> pass abc (NO pass.color): the block becomes region __c2
#      (color 2) in qor.json and its abc= flow override is applied from the
#      graph-embedded coloring_info "region_opts".
#   3. pass color synth -> pass abc: SEEDED precedence — the block region
#      survives the algorithm (still color 2, override still applied) and the
#      algorithm's ids allocate above it.
#   4. The tech-mapped netlist LECs against its pass.partition twin (lgyosys +
#      gensim cell models).
#   5. Negative controls: an unknown scope attribute and a double-quoted
#      abc= flow carrying `{` (string interpolation would corrupt `{D}`) must
#      FAIL the compile — a mistyped hint never silently no-ops.
#
# Hermetic: the small vendored Liberty (inou/prp/tests/abc/test.lib).

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_block_attr.prp
TOP=abc_block_attr.abc_block_attr
W="${TEST_TMPDIR:-/tmp/lhd_block_attr_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# 1. the annotation is semantics-free: annotated vs stripped source PROVEN
sed 's/{::\[.*\]/{/' "$PRP" > "$W/plain.prp"
run lec --impl "$PRP" --ref "$W/plain.prp" --top "$TOP" --workdir "$W/wl"

# 2. compile + abc WITHOUT pass.color: the block is its own region
run compile "$PRP" --top "$TOP" --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net" --set synth.liberty="$LIB" --workdir "$W/w2"
grep -q "\"module\":\"${TOP}__c2\",\"color\":2" "$W/w2/qor.json" || fail "block region __c2 missing from qor.json"
grep -q "color 2 options override applied (coloring_info)" "$W/w2/logs/"*.log \
  || fail "block abc= flow override was not applied from coloring_info"

# 3. seeded precedence: pass.color must keep the block region + still override
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w3"
run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net2" --set synth.liberty="$LIB" --workdir "$W/w4"
grep -q "\"module\":\"${TOP}__c2\",\"color\":2" "$W/w4/qor.json" || fail "block region lost after pass color synth"
grep -q "color 2 options override applied (coloring_info)" "$W/w4/logs/"*.log \
  || fail "block abc= flow override lost after pass color synth"

# 4. the mapped netlist (post-color run) LECs against its partition twin
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w5"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w6"
run compile lg:"$W/net2" --top "$TOP" --emit-dir verilog:"$W/netv" --workdir "$W/w7"
run compile lg:"$W/models" --emit-dir verilog:"$W/modelsv" --workdir "$W/w8"
run compile lg:"$W/re" --top "$TOP" --emit-dir verilog:"$W/rev" --workdir "$W/w9"
cat "$W/netv/"*.v "$W/modelsv/"*.v > "$W/impl.v"
cat "$W/rev/"*.v > "$W/ref.v"
run lec --set formal.solver=lgyosys --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc"

# 5a. negative control: unknown scope attribute must fail the compile
sed "s/abc='[^']*'/colour=3/" "$PRP" > "$W/bad_key.prp"
if "$LHD" compile "$W/bad_key.prp" --top "$TOP" --emit-dir lg:"$W/lgbad" --workdir "$W/wn1" \
    -q --result-json "$W/rn1.json" 2>/dev/null; then
  fail "unknown scope attribute compiled clean; expected a hard error"
fi

# 5b. negative control: double-quoted abc= flow with `{` (interpolation trap)
sed "s/abc='\([^']*\)'/abc=\"\1\"/" "$PRP" > "$W/bad_quote.prp"
if "$LHD" compile "$W/bad_quote.prp" --top "$TOP" --emit-dir lg:"$W/lgbad2" --workdir "$W/wn2" \
    -q --result-json "$W/rn2.json" 2>/dev/null; then
  fail "double-quoted abc= flow with {D} compiled clean; expected a hard error"
fi

echo "PASS: block-scoped synthesis attributes (LEC-invariant, own region + flow override, seeded precedence, netlist LEC, negative controls)"

# Source ware/delay controls must match the corresponding ABC options. Keep
# the same explicit color in every variant, so this compares policy rather
# than partition changes. Use NLDM and prove every selected netlist equivalent.
python3 - <<'PY' || fail "section ware/delay policy"
import json, os, pathlib, subprocess
w = pathlib.Path(os.environ['TEST_TMPDIR']) / 'section-ware'
w.mkdir()
lhd = str(pathlib.Path('lhd/lhd').resolve())
lib = str(pathlib.Path('inou/prp/tests/abc/timing.lib').resolve())
fixture = pathlib.Path('inou/prp/tests/pyrope/abc_ware_attr.prp').read_text()
top = 'abc_ware_attr.abc_ware_attr'

def run(*args):
    result = w/'result.json'
    p = subprocess.run([lhd, *map(str,args), '-q', '--result-json',str(result)], capture_output=True, text=True, timeout=120)
    assert p.returncode == 0, (args, p.stdout, p.stderr, result.read_text())
    return json.loads(result.read_text())

def synth(name, attrs, *opts):
    d = w/name; d.mkdir(exist_ok=True)
    source = d/'abc_ware_attr.prp'
    source.write_text(fixture.replace('color=2, ware=true, delay=500',attrs))
    j = run('synth',source,'--top',top,'--set',f'synth.liberty={lib}','--set','synth.opentimer=false',
            '--workdir',d/'work','--emit-dir',f'lg:{d}/net', '--emit',f'verilog:{d}/mapped.v',*opts)
    rows = j['qor']['abc']['regions']
    assert len(rows)==1 and rows[0]['color']==2, rows
    logs = '\n'.join(p.read_text() for p in (d/'work/logs').glob('*.log'))
    assert 'QoR unavailable' not in logs, logs
    return rows[0], logs

cases = {}
for enabled in ('true','false'):
    cases['attr_'+enabled] = synth('attr_'+enabled, f'color=2, ware={enabled}, delay=500')
    cases['cli_'+enabled] = synth('cli_'+enabled,'color=2','--set',f'abc.ware={enabled}','--set','abc.delay=500')
    a, b = cases['attr_'+enabled][0], cases['cli_'+enabled][0]
    for key in ('ware_trials','ware_selected','gates','area','logic_depth','delay','budget'):
        assert a[key] == b[key], (key,a,b)
assert cases['attr_false'][0]['ware_trials']==0
assert cases['attr_true'][0]['ware_trials']>0
assert cases['attr_true'][0]['delay'] < cases['attr_false'][0]['delay']
assert cases['attr_true'][0]['delay'] <= 500 < cases['attr_false'][0]['delay']
assert 'objective=timing' in cases['attr_true'][1]
# Zero clears timing and chooses area. CLI and source remain interchangeable.
a, log = synth('attr_area','color=2, ware=true, delay=0')
b, _ = synth('cli_area','color=2','--set','abc.ware=true')
for key in ('ware_trials','ware_selected','gates','area','logic_depth','delay'):
    assert a[key]==b[key], (key,a,b)
assert 'objective=area' in log and a['area'] < cases['attr_true'][0]['area']
# Clearing an inherited target still uses area as its objective.
a, log = synth('clear_target','color=2, ware=true, delay=0','--set','abc.delay=500')
assert 'objective=area' in log and 'objective=timing' not in log, log
# Sibling sections retain independent switches. The disabled section's long
# path must not prevent improving a tied endpoint in the enabled section.
d = w/'mixed'; d.mkdir()
source = d/'mixed.prp'
source.write_text("""mod mixed(a:u64,b:u64,c:u64,d:u64) -> (y:u1@[0],z:u1@[0]) {
  {::[color=2, ware=false, delay=500] y = a < b }
  {::[color=3, ware=true, delay=500] z = c < d }
}
""")
j=run('synth',source,'--top','mixed.mixed','--set',f'synth.liberty={lib}',
      '--set','synth.opentimer=false','--workdir',d/'work','--emit-dir',f'lg:{d}/net')
rows={r['color']:r for r in j['qor']['abc']['regions']}
assert rows[2]['ware_trials']==0 and rows[3]['ware_trials']>0,rows
assert rows[3]['delay'] < rows[2]['delay'],rows
# Explicit region_opts overrides source attributes (global knobs are defaults).
a, _ = synth('region_override','color=2, ware=true, delay=500','--set','abc.region_opts={"2":{"ware":false}}')
assert a['ware_trials']==0
# Warm replay retains source policy even when baseline mapping hits the cache.
a, _ = synth('attr_true','color=2, ware=true, delay=500')
assert a['ware_trials']==cases['attr_true'][0]['ware_trials']
assert a['delay']==cases['attr_true'][0]['delay']
run('pass','liberty','gensim',lib,'--emit-dir',f'lg:{w}/models')
for name in ('attr_true','attr_false','attr_area'):
    run('lec','--impl',f'lg:{w}/{name}/net','--ref',w/'cli_true/abc_ware_attr.prp','--top',top,
        '--lib',f'lg:{w}/models','--workdir',w/('lec-'+name))
# Reject malformed source values, not a silently ignored option.
for i, attrs in enumerate(('ware=1','ware="true"','delay=-1','delay="500"','ware=true, ware=false')):
    source = w/f'bad{i}.prp'
    source.write_text(fixture.replace('color=2, ware=true, delay=500',attrs))
    p = subprocess.run([lhd,'compile',str(source),'--workdir',str(w/f'badwork{i}'),'-q'],capture_output=True)
    assert p.returncode!=0, attrs
print('PASS: section ware/delay matches CLI, changes timing, preserves equivalence, and validates inputs')
PY

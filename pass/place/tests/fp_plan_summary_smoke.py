"""A mapper can plan unequal chunks directly from a joined color-graph bundle."""
import json
import os
import subprocess
from pathlib import Path

tmp = Path(os.environ['TEST_TMPDIR'])
src = tmp/'joined.json'
out = tmp/'fp.json'
objects = [dict(id=f"u.{key}", instance="u", module="core", cells=count, pins=0, nets=0,
                macro=False, area=count, width=1, height=1) for key, count in (("a", 6000), ("b", 2000), ("c", 1500))]
graph = dict(id="top", design="dut", schema_version=1, core_dbu=[0, 0, 200, 200],
             site_dbu=1, row_dbu=1, density=.6, objects=objects, nets=[["u.a", "u.b"], ["u.b", "u.c"]])
graph['class'] = "livehd.placement.color_graph"
args = ['pass/place/fp_plan_summary', str(src), 'dut', '0.09', str(out)]
def run(records, want=True):
    src.write_text(json.dumps(records))
    result = subprocess.run(args, capture_output=True, text=True)
    assert (result.returncode == 0) == want, result.stderr
    return result

meta = [{"class": "unrelated.class", "id": "other"}, graph]
summary = json.loads(run(meta).stdout)
assert summary['chunks'] == 2 and summary['modeled_logic_cells'] == 9500
plan = json.loads(out.read_text())
chunks = plan[0]['chunks']
assert sorted(sum(m['cells'] for m in c['members']) for c in chunks) == [1500, 8000]
assert all(c['estimated_bytes'] < 90000000 for c in chunks)
run(meta + plan)
assert json.loads(out.read_text()) == plan
graph['memory_context'] = 'measured-tech-and-flow'
sample = dict(id="1", context=graph['memory_context'], counts=[9500, 0, 8, 0],
              scope="headless_single_chunk_placement_and_downstream", metric="sampled_process_tree_rss",
              start_bytes=20000000, peak_bytes=200000000)
sample['class'] = "livehd.placement.chunk_memory_sample"
run(meta + [sample])
mem = json.loads(out.read_text())[0]['memory_prediction']
assert mem['samples'] == 1 and mem['whole_design_bytes'] >= 250000000 and not mem['extrapolating']
# A saved floorplan is reproducible without its separately stored measurements.
trained_plan = json.loads(out.read_text())
run(meta + trained_plan)
assert json.loads(out.read_text()) == trained_plan
# Conversely an updated budget should discard that decision and use today's model.
args[3] = '0.2'
run(meta + trained_plan)
assert json.loads(out.read_text())[0]['memory_prediction']['samples'] == 0
graph['nets'][0][1] = 'unresolved'
assert 'unknown net object' in run(meta, False).stderr

# Real IO on a single-chunk floorplan lands at the named block-edge DBU.
graph['nets'][0][1] = 'u.b'
graph['terminals'] = [dict(name='external', object='u.b', xy_dbu=[200, 70])]
run(meta)
rec = json.loads(out.read_text())[0]
assert len(rec['chunks']) == 1 and rec['chunks'][0]['ports'] == [dict(net='external', net_index=2, xy_dbu=[200, 70])]
graph['terminals'][0]['xy_dbu'] = [0, 130]
run(meta + [rec])
changed = json.loads(out.read_text())[0]
assert changed['input_key'] != rec['input_key']
assert changed['chunks'][0]['ports'] == [dict(net='external', net_index=2, xy_dbu=[0, 130])]
graph['terminals'][0]['xy_dbu'] = [50, 50]
assert 'not on the block edge' in run(meta, False).stderr

# Three repeated copies and a real multi-home hypernet. A named pair can pin
# its common interface independently of the third copy; results must survive
# a fresh process reading only the graph and exported metadata.
graph.pop('terminals')
graph.pop('memory_context')
graph['objects'] = [dict(objects[0], id=f'{i}.pin', instance=i, module='cpu') for i in ('a','b','c')]
graph['nets'] = [['a.pin','b.pin','c.pin']]
args[3] = '0.09'
run([graph])
base = json.loads(out.read_text())[0]
assert len(base['chunks']) == 3 and base['communication']['crossing_nets'] == 1
family = base['chunks'][0]['partition']
ids = [c['id'] for c in base['chunks']]
assert sorted(c['instances'] for c in base['chunks']) == [['a'], ['b'], ['c']]
role = base['chunks'][0]['ports'][0]['role']
groups = [dict(name='A1', instances=ids[:2], pins=[dict(role=role, side='right', offset=.25)]),
          dict(name='A2', instances=ids[2:], aspect_ratio=1.5)]
rules = {'class':'livehd.placement.chunk_guidance','id':'variants','implementations':[
    dict(partition=family, mode='shared', groups=groups)]}
run([graph,rules])
exported = json.loads(out.read_text())
rec = next(r for r in exported if r['class'].endswith('chunk_plan'))
assert rules in exported
points = []
for i,c in enumerate(rec['chunks']):
    assert c['implementation'] == ('A1' if i < 2 else 'A2')
    l,b,r,t = c['box_dbu']
    x,y = c['ports'][0]['xy_dbu']
    if i < 2: assert x == r and abs(y-(b+.25*(t-b))) <= .5
    points.append((x,y))
assert sum(max(v)-min(v) for v in zip(*points)) == rec['communication']['pin_hpwl_dbu']
run([graph,*exported])
assert json.loads(out.read_text()) == exported
groups[1]['instances'] = ['missing']
assert 'does not belong' in run([graph,rules], False).stderr
groups[1]['instances'] = ids[:1]
assert 'listed twice' in run([graph,rules], False).stderr

# Split a real physical home, inspect the newly crossing hypernet and demand
# grid, then fuse back. Keep the neighboring homes' boxes on a local split.
graph['objects'] = objects
graph['nets'] = [['u.a','u.b'],['u.b','u.c']]
args[3] = '0.09'
run([graph]);base = json.loads(out.read_text())[0]
parent = next(c for c in base['chunks'] if len(c['members']) == 2)
other = next(c for c in base['chunks'] if c['id'] != parent['id'])
def frame(plan):
    return [dict(id=c['id'],box_dbu=c.get('reference_box_dbu',c['box_dbu'])) for c in plan['chunks']]
one = parent['members'][0]
split = dict(split=parent['id'],new_id='child',members=[dict(id=one['id'],macro=one['macro'])],before=frame(base))
guidance = {'class':'livehd.placement.chunk_guidance','id':'edit','topology':[split]}
def changed():
    run([graph,guidance]);return next(r for r in json.loads(out.read_text()) if r['class'].endswith('chunk_plan'))
divided = changed()
assert len(divided['chunks'])==3 and divided['communication']['crossing_nets']==2
assert next(c['box_dbu'] for c in divided['chunks'] if c['id']==other['id'])==other['box_dbu']
assert next(c['members'][0]['id'] for c in divided['chunks'] if c['id']=='child')==one['id']
grid=divided['route_demand'];x,y,r,t=grid['frame_dbu'];n=grid['bins']
wire=sum(h*(r-x)/n+v*(t-y)/n for h,v in grid['loads'])
assert abs(wire-divided['communication']['pin_hpwl_dbu'])<1e-6
assert grid['peak_horizontal']==max(h for h,v in grid['loads'])
assert grid['peak_vertical']==max(v for h,v in grid['loads'])
saved=json.loads(out.read_text());run([graph,*saved]);assert saved==json.loads(out.read_text())
guidance['topology'].append(dict(fuse=[parent['id'],'child'],before=frame(divided)))
fused=changed()
assert {c['id']:c['box_dbu'] for c in fused['chunks']}=={c['id']:c['box_dbu'] for c in base['chunks']}
assert fused['communication']['crossing_nets']==base['communication']['crossing_nets']
assert all(set(m['id'] for m in c['members'])==set(m['id'] for m in next(b for b in base['chunks'] if b['id']==c['id'])['members']) for c in fused['chunks'])
guidance['topology'].append(dict(fuse=[parent['id'],other['id']]))
assert 'above budget' in run([graph,guidance],False).stderr

# Rules naming a retired physical child stay in metadata for undo; invented
# IDs must still be rejected. A fusion must not silently rebind the child's pins.
guidance['topology'].pop()
child=next(c for c in divided['chunks'] if c['id']=='child')
guidance['implementations']=[dict(partition=child['partition'],groups=[dict(name='V',instances=['child'])])]
guidance['geometry']=[dict(lock_aspect=True,chunks=['child'])]
guidance['targets']=[dict(kind='chunk',selector='child',density=.48)]
joined=changed()
assert len(joined['chunks'])==2
guidance['topology'].pop()
separated=changed();kid=next(c for c in separated['chunks'] if c['id']=='child')
assert kid['implementation']=='V' and kid['aspect_locked'] and kid['density']==.48
guidance['targets'][0]['selector']='invented'
assert 'unknown chunk guidance selector' in run([graph,guidance],False).stderr

#!/usr/bin/env python3
"""Refresh the HTML guide; only idealized reference PNGs are embedded in Git."""
import argparse
import base64
from pathlib import Path
import tempfile

from generate import CASES, topology
import render


DESCRIPTIONS = {
    'mesh_2x2': 'Start here: four identical hard macros with a cycle of nearest-neighbor logic. '
                'Expect a square, short balanced corridors, and four similarly sized logic neighborhoods.',
    'mesh_4x4': 'Sixteen hard macros on a non-wrapping Manhattan mesh. Interior tiles have four neighbors; '
                'corners have two. Expect four rows and four columns with approximately equal pitch.',
    'ring_32': 'A closed serpentine: 32 macro points across four alternating horizontal runs. '
               'The four outer corners (0, 7, 24, 31) are fixed; the other 28 macros remain movable. '
               'Expect regular points along the runs and logic following the folded cycle. '
               'The reference draws the closing connection around the outside; this is not a routing guide. '
               'The four anchors reduce geometric freedom without forcing the interior folds, so pattern scores remain advisory.',
    'hierarchy_4x4': 'Four repeated quadrants, each containing a four-tile square. Corresponding tiles in '
                     'neighboring quadrants communicate as well as local neighbors. Expect four cohesive '
                     'groups with repeated internal shape; the longer inter-quadrant links are intentional.',
    'mesh_6x6': 'Thirty-six macros and roughly 90K mapped cells. This scales the same non-wrapping mesh '
                'to six rows and six columns, exposing cumulative pitch drift, channel collapse, and corner crowding.',
}


PLACEMENT_HINTS = '''<section id="placement-hints">
<h2>Hierarchical placement hints</h2>
<p><strong>Experimental implementation:</strong> pass <code>FILE.json</code>. Use the existing
<a href="#hierarchy_4x4">hierarchy_4x4</a> fixture and its invented technology with
optional metadata. Keep the source, mapped netlist, connectivity, technology,
seed and optimization settings identical with and without hints. The floorplanner
still chooses the legal placement; a human or agent supplies relative physical
intent or optional coordinate hints. Relative hints are preferred for portability.</p>
<p><strong>Every hint is advisory.</strong> Missing, contradictory or unsuitable
metadata must not change the circuit or override physical legality. The placer
may relax or ignore a hint, report the reason, and continue ordinary placement.
Bad hints may worsen QoR. This does not guarantee that an independently infeasible
floorplan can be legalized. No required-placement mode is proposed here.</p>
<h3>Current implementation</h3>
<p>Relative hints guide FP and macro GP through soft center pulls (default per-hint
<code>weight: 0.35</code>, valid range <code>(0,1]</code>), refreshed every 25 GP iterations.
They add no nets, pins or timing arcs. Coordinate hints seed movable hard macros;
normal placement, legal halo spacing, track alignment and legalization still apply.
Final cell placement inherits the resulting macro frame and does not reapply hints.</p>
<p><code>work/synth/place/metadata.json</code> preserves authored records and adds
<code>livehd.placement.hint_results</code> and <code>livehd.placement.state</code>.
Results report binding, rejected seeds, and geometric residuals after legalization.
Accepted means consumed; a relationship outside one placement pitch is reported as
relaxed. Clustering is an objective with no binary satisfaction threshold. Coordinate
seeds are allowed to move after initialization.</p>
<p>Generated state uses the same position payload inside module templates. It is
not automatically promoted over authored relative intent (<code>automatic_reuse:false</code>).
Existing dependency-checked caches still own incremental reuse. The completed-placement cache hashes selected placement hints, not the filename or
unrelated records, and merges generated records into the current input on restore.
The inner block cache is bypassed for hint runs; synthesis and timing caches remain independent. The experimental <code>macro=h2d</code> solver ignores placement hints with a warning.
Rigid macro magnet contraction does not yet support
relative hints: those hints are discarded with a warning. Source HDL attribute
lowering and migration of legacy JSON report/cache formats remain planned work;
this does not silently reinterpret those existing files.</p>
<h3>Use the existing repeated hierarchy</h3>
<pre>hierarchy_4x4.top
  g0 : quadrant                 g1 : quadrant
    t0/macro ... t3/macro          t0/macro ... t3/macro
  g2 : quadrant                 g3 : quadrant
    t0/macro ... t3/macro          t0/macro ... t3/macro

One suggested arrangement (north is up; no coordinates prescribed):
    g2                          g3
    [t2]  [t3]                  [t2]  [t3]
    [t0]  [t1]                  [t0]  [t1]

    g0                          g1
    [t2]  [t3]                  [t2]  [t3]
    [t0]  [t1]                  [t0]  [t1]

All squares are the existing 40 x 40 micrometre REG_MACRO.
Spaces depict legal routing channels, not fixed distances.</pre>
<p>The names below refer to logical source instances, before synthesis coloring
adds generated wrappers. A template attached to <code>quadrant</code> expands
independently in <code>g0</code>, <code>g1</code>, <code>g2</code> and <code>g3</code>.
For example, its <code>lower-pair</code> becomes four distinct hints;
<code>g0/t0/macro</code> is paired with <code>g0/t1/macro</code>, never
<code>g1/t1/macro</code>. Parent hints explicitly address child groups.</p>
<h3>Example metadata vocabulary</h3>
<p>The file is an array of typed records, extending the existing
<code>inou.attr</code> convention. The explicit
<code>"class": "livehd.placement.hints"</code> identifies this entry to the
placement reader, regardless of filename or neighboring records. Each record has
its own <code>schema_version</code>, <code>id</code> and design scope. A file can
contain placement hints, synthesis metadata, incremental state and reports
without mixing their fields. See the <a href="../todo/livehd/json-metadata.html">JSON audit and shared-format plan</a>.</p>
<p>The JSON placement consumer supports the examples below for hard macros.
Paths are relative to the template's instance; module selectors identify logical
source definitions in this example. Exact module names are preferred; a short module name is accepted only when it identifies one definition. Ambiguous or missing instance names produce warnings. Logical memories expanded into multiple physical SRAMs must name those physical children explicitly.</p>
<pre id="placement-hints-json">[
  {
    "class": "livehd.placement.hints",
    "schema_version": 1,
    "id": "floorplan-intent",
    "design": "hierarchy_4x4.top",
    "templates": [
      {
        "module": "quadrant",
        "groups": {
          "memories": ["t0/macro", "t1/macro", "t2/macro", "t3/macro"]
        },
        "hints": [
          {"id": "compact", "kind": "cluster", "group": "memories"},
          {"id": "lower-pair", "kind": "snap", "subject": "t1/macro", "relative_to": "t0/macro", "side": "east"},
          {"id": "upper-pair", "kind": "snap", "subject": "t3/macro", "relative_to": "t2/macro", "side": "east"},
          {"id": "rows", "kind": "direction", "subject": "t2/macro", "relative_to": "t0/macro", "side": "north"},
          {"id": "left-column", "kind": "align", "members": ["t0/macro", "t2/macro"], "axis": "x", "anchor": "center"}
        ]
      },
      {
        "module": "hierarchy_4x4.top",
        "hints": [
          {"id": "bottom-row", "kind": "direction", "subject": "g1/memories", "relative_to": "g0/memories", "side": "east"},
          {"id": "top-row", "kind": "direction", "subject": "g3/memories", "relative_to": "g2/memories", "side": "east"},
          {"id": "left-column", "kind": "direction", "subject": "g2/memories", "relative_to": "g0/memories", "side": "north"},
          {"id": "right-column", "kind": "direction", "subject": "g3/memories", "relative_to": "g1/memories", "side": "north"}
        ]
      }
    ]
  }
]</pre>
<table><tr><th>Hint</th><th>Proposed meaning</th></tr>
<tr><td>cluster</td><td>Prefer a compact group. Members remain movable;
its bounding box is not a physical obstacle or reserved empty rectangle.</td></tr>
<tr><td>direction</td><td>Prefer the subject's body or group envelope beyond the
specified side of the other body/envelope, including applicable clearance.
North/south/east/west use the global floorplan axes, including inside templates.</td></tr>
<tr><td>snap</td><td>Prefer neighboring facing edges with the legal halo/channel
spacing and overlapping orthogonal spans. If side is omitted, let placement choose
among the four sides. Snap alone does not require equal orthogonal centers or
force a permanent rigid merge; abandon an incompatible adjacency.</td></tr>
<tr><td>align</td><td>Prefer equal x or y center coordinates, or an explicitly
named edge. Equal x means a column; equal y means a row. A common two-dimensional
cluster centroid is an attraction target, never an instruction to overlap bodies.</td></tr>
</table>
<h3>Optional coordinates, including incremental warm starts</h3>
<p>Coordinate seeds are supported, although relative hints
are preferred. For example, add this hint to the top template:</p>
<pre id="placement-position-json">{
  "id": "g0-memory-start",
  "kind": "position",
  "subject": ["g0", "t0", "macro"],
  "frame": "floorplan",
  "anchor": "lower_left",
  "x_um": 120,
  "y_um": 80,
  "orientation": "N"
}</pre>
<p>The subject is an instance path relative to the containing template; segment
arrays avoid ambiguity in names containing separators. The string paths in the
relative examples are readable shorthand. Coordinates use microns in the same
global coordinate system as DEF, with its coordinate origin (not an implicit
core-origin offset). The anchor is the lower-left corner of the oriented physical
body, before halo padding. An omitted orientation retains the current legal
orientation. The first implementation should target individual physical objects;
a logical memory split into several SRAMs needs a group transform rather than
assigning the same coordinates to every member.</p>
<p>This is a preferred initial position, <strong>never FIXED</strong>. GP and
legalization may move the object. If the hint cannot be used because of overlap,
core bounds, row/site restrictions, illegal orientation, ambiguous identity or
incompatible metadata, discard the affected hint and continue normal placement.
Conflicting coordinate hints have no file-order priority. Check them together
against fixed geometry and other accepted hints; temporary ordinary GP overlap
is not itself a reason to reject all warm-start coordinates.</p>
<p>Emit a warning through the normal LiveHD diagnostic sink, respecting
<code>--diag-fmt</code>, rather than printing an ad-hoc message. Include the record
and hint IDs, resolved instance, requested coordinates and rejection reason.
Warn once per rejected hint/reason, not on every GP iteration. Normal movement
of a successfully accepted initial guess does not produce a warning. Never fail
an otherwise feasible placement solely because an advisory coordinate cannot
be honored.</p>
<h3>Source attributes and external metadata</h3>
<p>A memory declaration (including a Verilog register declaration that infers RAM)
can carry the same logical relationships as attributes. Both frontends should
lower to one hint representation. Syntax for source annotations remains to be
agreed; no new Pyrope syntax is asserted by this example. External metadata makes
A/B experiments possible without changing RTL. Use an explicit scope and stable
hint ID to replace or disable one inherited hint in a particular instance;
do not silently resolve ambiguous names or append conflicting duplicates.</p>
<p>Preserve declaration identity and instance scope across coloring, inlining and
mapping. If a memory becomes multiple SRAMs, its logical reference resolves to a
group of those SRAMs. If it disappears, report the unresolved hint and skip it.
Do not guess a physical target from a coincidentally matching flattened name.
The initial example concerns macros; standard-cell references need the same
identity and disappearance rules before being supported.</p>
<h3>Round-trip intent separately from incremental results</h3>
<p>The output bundle preserves the input <code>livehd.placement.hints</code>
record, including its relative references and stable IDs. Observed coordinates
belong to <code>livehd.placement.state</code>; achieved/relaxed relations belong
to <code>livehd.placement.hint_results</code>. Neither is implicitly promoted into
user intent. Incremental state may include <code>kind=position</code> hints
with exactly the same coordinate fields and validation as authored hints; it
remains generated state. Automatic import of this exported state is not enabled yet.
An exported relative or coordinate suggestion can reuse the hints payload shape
under <code>livehd.placement.suggestions</code>, requiring explicit selection
before it influences a future run.</p>
<p>The existing completed-placement cache records netlist, technology, relevant options and
consumed-hint identity; the exported coordinate record is an observation, not a self-admitting cache. Changed intent invalidates affected placement reuse; it
must not be overwritten by old saved coordinates. Unrelated report changes
must not invalidate placement. A single combined metadata file is the placement
external interface; separate compact JSON/JSONL workdir shards remain permitted.
Packing or splitting preserves record identity and meaning, not a last-writer-wins
merge of unrelated JSON objects.</p>
<h3>How it guides the stages</h3>
<ol>
<li><strong>FP:</strong> resolve the hint graph once and add normalized soft
proximity, order and alignment objectives to the relevant groups. Keep ordinary
connectivity and density objectives. Do not enable timing feedback in this stage.</li>
<li><strong>Macro planning:</strong> refine using physical macro extents, legal
orientations, halos and channel requirements. Existing snap/group machinery is a
possible implementation aid, but an advisory hint must remain relaxable.
Fixed imported positions and legality take precedence.</li>
<li><strong>Final cells:</strong> keep the accepted macro frame fixed and run the
ordinary final placer. Hints create no circuit edges, timing arcs, buffers, pins,
or additional occupied area. Report achieved versus requested relations after
macro legalization, not just their temporary GP positions.</li>
</ol>
<h3>Regression coverage and additional comparisons</h3>
<table><tr><th>Variant</th><th>Purpose</th><th>Required checks</th></tr>
<tr><td>No metadata</td><td>Existing baseline</td><td>Preserve the existing run and legality checks.</td></tr>
<tr><td>Local hints only</td><td>Test independent expansion in all four quadrants</td><td>No accidental cross-instance binding; all 16 macros retained.</td></tr>
<tr><td>Local + parent hints</td><td>Test hierarchical grouping</td><td>Same netlist and connectivity; report relation residuals and stage metrics.</td></tr>
<tr><td>Snap without side</td><td>Leave adjacency direction to placement</td><td>Selected side reported; all physical spacing rules preserved.</td></tr>
<tr><td>One-instance override</td><td>Change one g1 hint</td><td>Only g1's hint bindings change; other placements may respond naturally.</td></tr>
<tr><td>Coordinate hints</td><td>Valid initial positions; authored versus incremental export</td><td>Same hint representation and admission; objects remain movable, and authored relative hints survive the round trip.</td></tr>
<tr><td>Unusable coordinates</td><td>Overlapping requests, outside-core location, illegal orientation, missing or split target</td><td>Discard affected hints with diagnostic-format warnings; retain the feasible baseline flow and circuit identity.</td></tr>
<tr><td>Bad metadata</td><td>Unknown target, opposing directions, self-snap,
and a request conflicting with fixed geometry</td><td>Diagnose and relax/skip hints;
retain circuit identity and legal placement for a feasible baseline floorplan.</td></tr>
</table>
<p>For every variant, show FP, macro and final density images alongside HPWL,
WNS/TNS, RSMT-RUDY congestion and runtime, using identical measurement settings.
Count satisfied, relaxed and unresolved hints, with distances normalized to object
size. Report timing only as an observation in FP/macro; feedback belongs in final.
Geometric preference satisfaction is a QoR measurement, not a correctness test.
The short <code>//pass/place:placement_hints_smoke</code> test checks repeated instances,
invalid coordinates, unchanged cell/net inventory, metadata preservation and selective
cache replay. The unit test covers snap/direction/alignment behavior and coordinate conflicts.
One-instance override syntax, automatic warm-start import and source attributes remain planned.
The full synthetic comparison below was run with the same seed and settings.</p>
<h3>Measured initial validation (2026-10-27)</h3>
<table><tr><th>Design</th><th>Variant</th><th>Legal</th><th>HPWL (µm)</th><th>WNS</th><th>TNS</th></tr>
<tr><td>hierarchy_4x4</td><td>baseline</td><td>yes</td><td>2,102,133</td><td>+16.228321 ns</td><td>0 ns</td></tr>
<tr><td>hierarchy_4x4</td><td>24 hints</td><td>yes</td><td>1,999,788</td><td>+16.228056 ns</td><td>0 ns</td></tr>
<tr><td>CoralNPU / ASAP7</td><td>baseline</td><td>yes</td><td>2,121,603</td><td>−1,339.791 ps</td><td>−547,537.762 ps</td></tr>
<tr><td>CoralNPU / ASAP7</td><td>3 macro hints</td><td>yes</td><td>2,084,230</td><td>−1,478.592 ps</td><td>−614,355.406 ps</td></tr></table>
<p>The synthetic run has 52,245 cells and 16 macros; 7 hints were accepted and 17
relations relaxed. CoralNPU has 186,367 cells and 2 SRAMs; the cluster objective was
accepted, while row alignment and east/west ordering were relaxed. CoralNPU placement
took 125.6 s baseline and 131.8 s with hints (207.2/212.7 s whole invocation).
Both CoralNPU runs have identical component inventories and byte-identical DEF NETS
sections. The timing target is 3000 ps and reset is a false path. Timing feedback and
resizing were disabled. These capped runs did not converge; no post-route claim is made.</p>
<p>CoralNPU uses the same HighTide OpenROAD-mapped netlist, flattened with OpenROAD,
in both arms. The RTL attempt failed before placement on an unmapped flop; the
hierarchical mapped input was stopped during expensive loop handling. GP caps were
150 FP / 300 macro / 300 final, with FP snapping disabled. The synthetic comparison
used 300 iterations per stage and the same snap setting. The measured hints lower
HPWL but do not improve CoralNPU timing.</p>
<pre>bazel test -c opt --test_timeout=30 //core:metadata_test \
  //pass/place:placement_hints_test //pass/place:placement_hints_smoke
python3 pass/place/tests/regularity/run.py --case hierarchy_4x4 \
  --iterations 300 --set synth.place.fp.snap=false \
  "$PWD/pass/place/tests/regularity/hierarchy_hints.json"</pre>
</section>
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('docs/placement_regularity.html'))
    args = parser.parse_args()
    cards = []
    with tempfile.TemporaryDirectory() as temporary:
        for case in CASES:
            name = case['name']
            points, edges = topology(case)
            image = Path(temporary)/(name+'.png')
            render.expected(case, points, edges, image)
            encoded = base64.b64encode(image.read_bytes()).decode()
            result = '../repros/placement_regularity/'+name
            constraints = (f' · <a href="../pass/place/tests/regularity/{name}.def">Fixed corner DEF</a>'
                           if case.get('fixed_macros') else '')
            cards.append(f'''<section id="{name}"><h2>{name}</h2><p>{DESCRIPTIONS[name]}</p>
            <p>{len(points)} macros · target ≈{case['target']:,} standard cells (the actual mapped count is checked) ·
            {len(case.get('fixed_macros', []))} explicit fixed anchors{constraints} ·
            <a href="../pass/place/tests/regularity/{name}.prp">Pyrope source</a></p>
            <div class="pair"><figure><img src="data:image/png;base64,{encoded}" alt="Idealized {name} placement">
            <figcaption>Expected PNG regularity — manually designed topology, not measured placement.</figcaption></figure>
            <figure><a href="{result}/final.png"><img src="{result}/final.png" alt="Actual {name} final placement; run the benchmark to generate"
            onerror="this.style.display='none';this.parentElement.nextElementSibling.hidden=false"></a>
            <p hidden class="missing">No local PNG yet. Run this benchmark with the command below.</p>
            <figcaption>Actual final detailed placement PNG, generated by <code>bazel run</code>.</figcaption></figure></div>
            <iframe class="metrics" src="{result}/metrics.html" title="Measured {name} HPWL, WNS, TNS and regularity"></iframe>
            <p><a href="{result}/metrics.json">Measured metrics and provenance</a> ·
            <a href="../repros/placement_regularity/index.html#{name}">FP, macro and legalization PNGs</a></p>
            <pre>bazel run -c opt //pass/place/tests/regularity:run -- --case {name} --min-macros 0 --output "$PWD/repros/placement_regularity"</pre>
            </section>''')
    args.output.write_text('''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pyrope placement regularity</title><style>
body{font:16px/1.5 system-ui,sans-serif;max-width:1200px;margin:2em auto;padding:0 1em;background:#f5f7fb;color:#23344b}
section{background:white;padding:1.4em;margin:2em 0;border:1px solid #dbe2ec;border-radius:8px}
h1,h2{line-height:1.2}.pair{display:grid;grid-template-columns:1fr 1fr;gap:1.5em}figure{margin:0}img{width:100%}
pre{overflow:auto;padding:1em;background:#edf1f7}code{font-size:.9em}figcaption,.missing{font-size:.9em}
.metrics{width:100%;height:130px;border:0;margin-top:1em}a{color:#185caa}table{border-collapse:collapse}td,th{padding:.5em;border:1px solid #ccd5e2}
@media(max-width:700px){.pair{grid-template-columns:1fr}}</style></head><body>
<h1>Pyrope placement regularity</h1>
<p>Small source programs generate approximately 10K–100K standard cells around repeated fake hard macros.
The purpose is to make physical structure easy to inspect through the complete Pyrope → graph → ABC mapping →
<strong>fp_plan → macro_plan → final detailed placement</strong> → OpenTimer flow.</p>
<p>The expected PNGs below are topology sketches, with macro squares and standard-cell corridors.
Actual PNGs and timing numbers are generated locally under ignored <code>repros/placement_regularity/</code>.
No measured result is checked into this guide. Tile colors are consistent between the sketches and stage views;
outlines identify macro bodies, with red marking the four explicit corner anchors in the serpentine.
A sketch does not prescribe an exact gate location.</p>
<p><a href="../repros/placement_regularity/index.html">Open the complete generated report</a> ·
<a href="../pass/place/tests/regularity/BUILD">Bazel targets</a> ·
<a href="../pass/place/tests/regularity/generate.py">Fixture generator</a></p>
<h2>Run and inspect</h2>
<pre>bazel run -c opt //pass/place/tests/regularity:run -- --case all --min-macros 0 --output "$PWD/repros/placement_regularity"
# Every topology runs in the default suite with a bounded optimization budget:
bazel test -c opt //pass/place/tests/regularity:all
# Fixture/observer checks also run in the ordinary suite:
bazel test -c opt //pass/place/tests/regularity:regularity_checks</pre>
<p>Bazel test retains PNGs, HTML, metrics, DEFs and logs under
<code>bazel-testlogs/pass/place/tests/regularity/&lt;case&gt;/test.outputs/</code>
(or <code>outputs.zip</code> when undeclared-output zipping is enabled).
The run target accepts <code>--iterations N</code>, <code>--min-macros N</code> (default 0),
<code>--snap-mode alignment|symmetry</code>, and an optional <code>--max-pattern-rms PITCHES</code>. Use the same explicit settings and frozen binary for comparisons.
Every benchmark runs cold (<code>lhd.incremental=false</code>) so saved placements cannot mask regressions.</p>
<h2>What is measured</h2>
<table><tr><th>Metric</th><th>Interpretation</th></tr>
<tr><td>HPWL (µm)</td><td>Unweighted placed net half-perimeter sum reported by the flow, including its physical nets.</td></tr>
<tr><td>WNS / TNS (ns)</td><td>OpenTimer setup slack with a 20ns clock and the invented Liberty plus LEF RC. TNS sums negative slack;
positive WNS is possible. These are placement RC estimates, without routing or clock-tree synthesis.</td></tr>
<tr><td>Pattern RMS (pitches)</td><td>Macro centers fit to the expected template with translation, uniform scale, arbitrary rotation,
and reflection. Zero means the illustrated regular frame; lower is better. Macro centers suffice because all macros are equal squares.</td></tr>
<tr><td>Normalized edge length CV</td><td>For each neighbor edge, divide its measured length by its length in the template,
then take standard deviation divided by mean. Zero means equal pitch, including the longer hierarchical links.</td></tr>
<tr><td>Legal / GP converged</td><td>Reported separately. Legalization must succeed, even when GP exhausts its iteration budget.</td></tr></table>
<p>Flow completion, a legal final DEF, expected macro count, 10K–110K standard cells, and finite HPWL/WNS/TNS are required.
Pattern scores are reported for visual review; only an explicit <code>--max-pattern-rms</code> turns them into a pass/fail condition.
No exact coordinate golden is imposed on the heuristic placer.</p>
<h2>The deliberately fake technology</h2>
<p><a href="../pass/place/tests/regularity/fake.lib">Liberty</a> and
<a href="../pass/place/tests/regularity/fake.lef">LEF</a> contain INV, BUF, NAND2, NOR2, XOR2, DFF, TIE0 and TIE1,
one drive strength each. A 40×40µm BLOCK macro has 32 independent feed-through lanes with 0.4ns arcs.
Standard combinational arcs are 0.05ns, flop clock-to-Q is 0.05ns, and setup/hold are 0.02ns.
Rows are 2µm high with 1µm sites; DBU is 1000/µm. Two orthogonal routing layers supply explicit fake RC.
These values are invented; they are not Sky130 characterization.</p>
<p>Only the hard-macro interface is Verilog. The designs, connections, hierarchy and surrounding logic are Pyrope,
using <code>import("lg:REG_MACRO")</code>. Each tile has 32 resetless state bits and repeated nonlinear mixing,
and every tile is observable at a top output. Sequential feedback breaks the mesh/ring cycles.
The explicit mapping recipe <code>strash; &amp;get -n; &amp;nf; &amp;put</code> avoids expensive synthesis searches.
Resizing, placement-driven remapping and additional timing-driven placement rounds are explicitly disabled to keep
netlist identity and stage images comparable. Final STA remains enabled. Existing flow defaults are unchanged.</p>
<p>The run target sets <code>synth.place.fp.min_macros=0</code> (flag <code>--min-macros</code>, default 0 here; the flow
default is 2). A hierarchy candidate needs <em>more</em> than that many movable macros, so with the flow default these
one- and few-macro tiles would never become hierarchy candidates: they would fall back to unsnapped color centroids and the
floorplan snap would not be exercised. With 0, every repeated tile (four or more siblings under one parent,
<code>synth.place.fp.min_copies</code>) is a snap-eligible hierarchy candidate. Snapshots are required: missing stage data fails the run.
The optional <code>--snap-mode alignment|symmetry</code> sets <code>synth.place.macro.snap_mode</code> for the macro stage.</p>
<p>The mesh and hierarchy cases start with all macros movable. The serpentine imports a generated DEF containing
only its four corner macros as FIXED components; all 28 other macros and all standard cells remain movable.
The runner verifies each fixed corner survives unchanged and that the macro-stage handoff preserves every macro.
Pyrope fixed-coordinate attributes are not consumed by the current physical flow; this fixture uses the supported DEF interface.</p>
<p>The serpentine explicitly selects <code>pass.color.synth.mode=synth</code> to keep its root logic in one color
and retain the source instance names used by its DEF. The default cone coloring can wrap those instances in
generated hierarchy. The other cases retain their default synthesis coloring.</p>
<p>The serpentine follows the alternating runs in <a href="https://share.google/YXcon6ciunlGzwLwz">the supplied zigzag reference</a>,
with an outside connection closing its two ends into one cycle. The reference image is not copied into the repository.</p>
'''+PLACEMENT_HINTS+''.join(cards)+'''
<p>Refresh sources/technology with <code>python3 pass/place/tests/regularity/generate.py</code> and this guide with
<code>python3 pass/place/tests/regularity/documentation.py</code>. The fast check detects stale generated fixtures.</p>
</body></html>''')


if __name__ == '__main__':
    main()

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_page.hpp"

namespace livehd::placement::gui {
std::string_view page_html() {
  static constexpr std::string_view page = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LiveHD placement</title>
<style>
:root{--bg:#f6f7f9;--fg:#1d2330;--mut:#6b7385;--pan:#ffffffee;--line:#cfd5e0;--cell:#2f6fdb;--fixedc:#8b93a5;--macro:#e08a1e;--macrof:#a6671c;--io:#1f9d55;--reg:#9aa3b8;--sel:#e0245e;--pin:#111;--die:#ffffff;--acc:#2f6fdb;--bad:#d1242f;--seq:#a63fe0}
@media (prefers-color-scheme:dark){:root{--bg:#10131a;--fg:#e6e9f0;--mut:#8d95a8;--pan:#171b25ee;--line:#2c3446;--cell:#5b9bff;--fixedc:#6b7285;--macro:#f0a43c;--macrof:#b9782a;--io:#3fcf82;--reg:#4a536a;--sel:#ff5c8a;--pin:#fff;--die:#0b0e14;--acc:#5b9bff;--bad:#ff6b73;--seq:#d28cff}}
html,body{margin:0;height:100%;background:var(--bg);color:var(--fg);font:13px/1.4 system-ui,sans-serif;overflow:hidden}
#cv{position:absolute;inset:0;width:100%;height:100%;touch-action:none;cursor:default}
#bar{position:absolute;left:12px;top:12px;display:flex;gap:6px;align-items:center;background:var(--pan);border:1px solid var(--line);border-radius:8px;padding:6px 8px}
button{font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:6px;padding:3px 9px;cursor:pointer}
button:hover{border-color:var(--acc)} button.primary{background:var(--acc);color:#fff;border-color:var(--acc)}
button:disabled{opacity:.45;cursor:default}
#panel{position:absolute;right:12px;top:12px;bottom:12px;width:268px;overflow:auto;background:var(--pan);border:1px solid var(--line);border-radius:8px;padding:10px 12px}
#implEditor label, #chunks fieldset label{flex-wrap:wrap} #chunks fieldset select{max-width:100%;min-width:0} #panel h4{margin:12px 0 4px;font-size:11px;letter-spacing:.06em;text-transform:uppercase;color:var(--mut)} #panel h4:first-child{margin-top:0}
.row{display:flex;justify-content:space-between;gap:8px} .row span:first-child{color:var(--mut)}
label{display:flex;gap:6px;align-items:center;margin:2px 0} .na{opacity:.45}
#tl{position:absolute;left:12px;top:58px;max-width:calc(100% - 310px);display:flex;gap:4px;align-items:center;overflow-x:auto;background:var(--pan);border:1px solid var(--line);border-radius:8px;padding:4px 6px}
#tl:empty{display:none} #tl .cp{white-space:nowrap;padding:1px 7px;border:1px solid var(--line);border-radius:10px;cursor:pointer;font-size:11px;color:var(--mut)}
#tl .cp.cur{border-color:var(--io);color:var(--fg)} #tl .cp.sel{background:var(--acc);border-color:var(--acc);color:#fff} #tl .cp.nosnap{opacity:.6;cursor:default}
#scrub{position:absolute;left:12px;top:96px;width:min(760px,calc(100% - 330px));display:none;align-items:center;gap:6px;background:var(--pan);border:1px solid var(--line);border-radius:8px;padding:5px 8px}
#scrub.on{display:flex} #scrub button{padding:1px 8px} .sbox{position:relative;flex:1;height:32px} #sb_rng{width:100%;margin:0;position:absolute;left:0;top:0;height:20px}
#sb_marks{position:absolute;left:0;right:0;top:21px;height:11px} .mk{position:absolute;top:0;width:3px;height:11px;border-radius:1px;transform:translateX(-1px);background:var(--mut)}
.mk.gate{background:var(--acc)} .mk.gp_end{background:var(--io)} .mk.legalize,.mk.macro_legalize{background:var(--macro)} .mk.edit,.mk.finished{background:var(--sel)}
#sb_cnt{min-width:210px;text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
#tl .arrow{color:var(--mut);font-size:10px}
#flags{max-height:230px;overflow:auto;border:1px solid var(--line);border-radius:6px;padding:4px;margin:4px 0}
#flags .f{padding:2px 0;border-bottom:1px solid var(--line);font-size:12px} #flags .f.chg{background:#e0a02033}
#flags .f code{font-size:11px} #flags .f .h{color:var(--mut);font-size:11px} #flags input[type=text]{width:100%;box-sizing:border-box;font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:4px;padding:1px 4px}
#help{position:absolute;inset:0;display:none;align-items:center;justify-content:center;background:#0007;z-index:9}
#help.on{display:flex} #help .hb{background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:14px 18px;max-width:600px;font-size:13px;box-shadow:0 8px 30px #0006} #help td{padding:2px 12px 2px 0;vertical-align:top} #help td:first-child{white-space:nowrap;color:var(--mut)}
#opts{position:absolute;left:50%;top:56px;transform:translateX(-50%);width:min(860px,calc(100% - 340px));max-height:calc(100% - 80px);display:none;flex-direction:column;background:var(--bg);border:1px solid var(--line);border-radius:10px;box-shadow:0 8px 30px #0006;z-index:5}
#opts.on{display:flex} #opts .oh,#opts .of{padding:8px 12px;display:flex;gap:8px;align-items:center;flex-wrap:wrap} #opts .oh{border-bottom:1px solid var(--line)} #opts .of{border-top:1px solid var(--line)}
#o_list{overflow:auto;padding:4px 12px;flex:1} #o_list summary{cursor:pointer;padding:5px 0;font-weight:600;font-size:12px;color:var(--mut)}
.orow{display:grid;grid-template-columns:230px 200px 1fr;gap:8px;align-items:start;padding:3px 0;border-bottom:1px solid var(--line);font-size:12px} .orow.chg{background:#e0a02033}
.orow code{font-size:11px;word-break:break-all} .orow .h{color:var(--mut);font-size:11px} .orow input[type=text]{width:100%;box-sizing:border-box;font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:4px;padding:1px 4px}
.orow .rs{padding:0 5px;font-size:11px;margin-left:4px} #o_q{flex:1;min-width:180px;font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:4px;padding:2px 6px}
.tip{position:absolute;pointer-events:none;background:var(--pan);border:1px solid var(--line);border-radius:6px;padding:3px 7px;font-size:12px;display:none;max-width:320px;word-break:break-all}
#al{display:grid;grid-template-columns:repeat(3,1fr);gap:4px} #al button{padding:3px 2px;font-size:12px}
input[type=number]{width:44px;font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:4px;padding:2px 4px}
.mut{color:var(--mut)} .st-paused,.st-gate{color:var(--macro)} .st-running{color:var(--io)} .st-finished{color:var(--mut)}
</style></head><body>
<canvas id="cv"></canvas>
<div id="bar"><button id="run" class="primary">Run</button><button id="cont" style="display:none">Continue</button><button id="fit">Fit</button><button id="zin" title="zoom in (z or +)">+</button><button id="zout" title="zoom out (Z or -)">&minus;</button><button id="shot">Screenshot</button><button id="live" style="display:none">Live</button><button id="rp" disabled title="Re-run the flow with the recorded actions up to the selected checkpoint, using the flags edited in the panel">Replay from here</button><button id="otog" title="synth.place options: edit and rerun">Options</button><button id="ptog">Panel</button><strong id="viewName" hidden></strong><span id="msg" class="mut"></span></div>
<div id="tl"></div>
<div id="scrub"><button id="sb_start" title="first snapshot">|&lt;</button><button id="sb_back" title="one snapshot back (left arrow)">&lt;</button><button id="sb_play" title="play the recorded history like a video">&#9654;</button><button id="sb_fwd" title="one snapshot forward (right arrow)">&gt;</button><button id="sb_end" title="back to live">&gt;|</button><span class="sbox"><input type="range" id="sb_rng" min="0" max="0" value="0"><div id="sb_marks"></div></span><span id="sb_cnt" class="mut"></span></div>
<div id="err" style="display:none;position:absolute;left:12px;top:150px;max-width:calc(100% - 330px);white-space:pre-wrap;background:var(--pan);border:2px solid var(--bad);border-radius:8px;padding:10px 14px;color:var(--fg);font-size:13px"></div>
<div id="panel">
<details id="geom" hidden style="color:var(--bad);overflow-wrap:anywhere"><summary><strong id="geomSummary" role="alert"></strong></summary><ul id="geomList" style="padding-left:1.2em"></ul></details>
<details id="pinInspector" hidden><summary id="pinSummary">Pin anchors</summary>
<div id="pinAnchors" style="white-space:pre-wrap;overflow-wrap:anywhere"></div></details>
<h4>Status</h4>
<div class="row"><span>phase</span><span id="s_phase">-</span></div>
<div class="row"><span>state</span><span id="s_state">-</span></div>
<div class="row"><span>iteration</span><span id="s_iter">-</span></div>
<div class="row"><span>pending edits</span><span id="s_pend">0</span></div>
<h4>Stats</h4>
<div class="row"><span>HPWL (um)</span><span id="s_hpwl">-</span></div>
<div class="row"><span>Placement density overflow</span><span id="s_ovf">-</span></div>
<div class="row"><span>Fmax (MHz)</span><span id="s_freq">-</span></div>
<div id="clockCoverage" class="mut"></div>
<details id="frameTimingGroups" hidden><summary>Placement setup timing by clock and path family</summary><div></div></details>
<details id="sdcCoverage" hidden><summary id="sdcCount"></summary><ul id="sdcIssues"></ul></details>
<div class="row"><span>WNS (ps)</span><span id="s_wns">-</span></div>
<div class="row"><span>TNS (ps)</span><span id="s_tns">-</span></div>
<div id="repairAdvice" class="mut" hidden></div>
<div class="row"><span>Routing bins over capacity</span><span id="routing_share">–</span></div>
<div class="row"><span>Quality</span><span id="quality">Not configured</span></div>
<details id="scoreSettings"><summary>Configure quality score</summary>
<fieldset id="scoreFields"><legend>Score components</legend>
<label>Routing estimator <select id="scoreEstimator"><option value="rudy">RUDY</option><option value="rsmt">RSMT</option></select></label>
<table><thead><tr><th>Loss</th><th>Weight</th><th>Half-score loss</th></tr></thead><tbody id="scoreTerms"></tbody></table>
<button id="scoreApply">Apply score</button> <button id="scoreDisable">Disable score</button>
</fieldset><output id="scoreStatus" aria-live="polite"></output></details>
<div class="row"><span>timing from iter</span><span id="s_titer">-</span></div>
<div class="row"><span></span><span><button id="tim" style="padding:0 8px;font-size:12px">Refresh timing</button> <button id="evaluate">Full timing</button> <button id="legalize">Legalize</button></span></div>
<details><summary>Implementation controls</summary>
<label><input type="checkbox" id="no_resynthesis">No resynthesis</label>
<label><input type="checkbox" id="no_sizing">No sizing</label>
<label><input type="checkbox" id="no_buffer_edits">No buffer edits</label>
<button id="freeze">Freeze implementation</button>
<div id="slowwork" class="mut"></div></details>
<h4>Stop before stage</h4><div id="stages" class="mut">-</div>
<h4>Explorations</h4>
<div id="explist" aria-live="polite"></div>
<fieldset id="xeval"><legend>Evaluate this frame</legend>
<label>Evaluation <select id="xtier"><option value="T0">Geometry</option><option value="T1">Frozen timing</option><option value="T2">Placement relaxation</option><option value="T3">Implementation repair</option><option value="T4">Downstream routing</option></select></label>
<label id="xbudget" hidden><span id="xunit"></span> <input type="number" id="xcount" min="1" max="64" value="1" style="width:4em"></label>
<label id="xscope" hidden><input type="checkbox" id="xselected">Limit to selected cells</label>
<div id="xrepairs" hidden><label>Timing call limit <input type="number" id="xtiminglimit" min="1" step="1" style="width:5em"></label><label><input type="checkbox" id="xsize" checked>Sizing</label> <label><input type="checkbox" id="xbuffer">Buffer edits</label> <label><input type="checkbox" id="xresynth">Resynthesis</label></div>
<details><summary>Placement patch</summary><textarea id="xpatch" aria-label="Placement patch JSON" rows="3" style="width:100%" placeholder="Move commands or an exported relative-moves document"></textarea></details>
<button id="xsubmit">Evaluate candidate</button> <output id="xstatus" aria-live="polite"></output>
</fieldset>
<div class="row"><label>Reference <select id="xref"></select></label><label>Candidate <select id="xcandidate"></select></label><button id="xcompare">Compare</button></div>
<output id="xdiff" class="mut"></output>
<strong id="xgeometry" style="color:var(--bad)" hidden></strong>
<div id="xtimingdiff" hidden></div>
<div id="xplot" hidden><label>Map difference <select id="xlayer"><option value="rudy">RUDY</option><option value="rsmt">RSMT</option><option value="density">Cell density</option></select></label>
<canvas id="xmap" width="320" height="160" aria-label="Candidate minus reference utilization with macro displacements" style="width:100%;height:auto"></canvas><output id="xlegend" class="mut"></output></div>
<button id="xbegin">Begin exploration</button> <button id="xclose" hidden>Discard</button>
<div class="row"><label>Export <select id="exportKind"><option value="macros">Design metadata + exact macro coordinates</option><option value="relationships">Relative moves</option><option value="flags">Changed flags</option><option value="intent">Placement intent</option></select></label>
<button id="exportData">Download JSON</button></div>
<h4>Fork region</h4>
<div class="mut" style="font-size:12px">Take the cells of a region as a design of its own, in a new page: boundary pins fixed where the nets crossed, cells starting where they are. Change the options there and compare.</div>
<div class="row" style="margin-top:4px"><span></span><span><button id="f_sel" title="the box around the selected nodes">Fork selection</button> <button id="f_view" title="what is on screen">Fork view</button></span></div>
<div id="f_list" style="font-size:12px"></div>
<h4>Core area</h4>
<div class="row"><span>core (um)</span><span id="a_size">-</span></div>
<div class="row"><span>new core</span><span id="a_new">-</span></div>
<div class="row"><span>cells fill</span><span id="a_fill">-</span></div>
<div class="mut" style="font-size:12px">Pick an edge (L R B T, or drag it on the canvas), then Shrink / Grow or drag: the new outline is only a preview until you press Apply.</div>
<div id="a_edges" style="display:flex;gap:4px;margin:4px 0"><button data-e="left" title="the left edge">L</button><button data-e="right" title="the right edge">R</button><button data-e="bottom" title="the bottom edge">B</button><button data-e="top" title="the top edge">T</button></div>
<div class="row"><span>step (um)</span><input id="a_step" type="number" min="0" step="any" style="width:90px;font:inherit;color:var(--fg);background:transparent;border:1px solid var(--line);border-radius:4px;padding:1px 4px"></div>
<div class="row"><span></span><span><button id="a_shr" title="move the selected edge inward by the step (preview)">Shrink</button> <button id="a_grow" title="move the selected edge outward by the step (preview)">Grow</button></span></div>
<div class="row"><span></span><span><button id="a_apply" class="primary" title="re-place the design in the previewed area (restarts at the latest checkpoint)">Apply</button> <button id="a_cancel" title="drop the preview (Esc)">Cancel</button></span></div>
<div id="a_warn" style="font-size:12px;color:var(--bad)"></div>
<details id="stripInspector"><summary>Macro channels</summary>
<button id="stripRead">Measure channels</button>
<output id="stripStatus" aria-live="polite" style="display:block"></output>
<select id="stripSelect" aria-label="Measured macro channel" style="width:100%" hidden></select>
<div id="stripDetail"></div><div id="edgeClearance"></div>
<label id="faceSelector" hidden>Macro faces <select id="faceMacro"></select></label><div id="faceSummary"></div></details>
<h4>Layers</h4>
<label><input type="checkbox" id="l_comb" checked>comb</label>
<label><input type="checkbox" id="l_regs" checked><span style="color:var(--seq)">regs</span></label>
<label><input type="checkbox" id="l_macros" checked>macros</label>
<label><input type="checkbox" id="l_pins">macro signal pins</label>
<label><input type="checkbox" id="l_io" checked>IO</label>
<label><input type="checkbox" id="l_reg">regions <span class="mut" style="font-size:11px">(cuts and boundary pins)</span></label><div id="reg_note" class="mut" style="font-size:11px;margin-left:20px"></div>
<label><input type="checkbox" id="l_cen" checked>centroids <button id="cen_rel" style="padding:0 6px;font-size:11px">release</button></label>
<label><input type="checkbox" id="l_dens">density</label>
<label>congestion <select id="l_map"><option value="none">off</option><option value="rudy">RUDY</option><option value="rsmt">RSMT</option></select> <span id="s_map" class="mut"></span></label>
<label><input type="checkbox" id="l_disp">displacement</label>
<label>clock tree <select id="l_clk" title="the native clock tree (synth.place.pcrs.clock=true), once the flow has built it"><option value="off">off</option><option value="level">stage depth</option><option value="arrival">sink arrival</option><option value="slew">leaf slew</option><option value="skew">local skew map</option></select></label><div id="s_clk" class="mut" style="font-size:11px"></div>
<h4>Highlights</h4><div id="hls" class="mut" style="font-size:12px">none (an assistant can paint cells here)</div>
<h4>Selection (<span id="s_sel">0</span>)</h4>
<details><summary>Align selected objects</summary><div id="al">
<button data-a="left">Left</button><button data-a="hcenter">H center</button><button data-a="right">Right</button>
<button data-a="top">Top</button><button data-a="vcenter">V center</button><button data-a="bottom">Bottom</button>
<button data-a="disth">Distribute H</button><button data-a="distv">Distribute V</button><button data-a="unpin">Unpin</button>
</div>
<div class="row" style="margin-top:6px;align-items:center"><span>matrix</span><span><input type="number" id="m_r" value="2" min="1"> x <input type="number" id="m_c" value="2" min="1"> <button data-a="matrix">Go</button></span></div>
<label><input type="checkbox" id="ipreserve">Preserve as relationship</label></details>
<details><summary>Placement intent records</summary>
<button id="iread">Inspect</button> <button id="ipreview">Preview</button> <button id="iapply">Apply records</button>
<textarea id="irecords" aria-label="Authored placement records" rows="4" style="width:100%"></textarea>
<label id="ioverlayControl" hidden><input type="checkbox" id="ioverlay" checked> Show intent targets</label>
<output id="ilegend" class="mut" aria-live="polite"></output>
<pre id="ibind" style="white-space:pre-wrap;max-height:12em;overflow:auto"></pre></details>
<details id="chunks" hidden><summary>Floorplan chunks</summary>
<label><input type="checkbox" id="chunkLines" checked> Show boundaries</label>
<label>Target <select id="chunkSelect" style="max-width:100%"></select></label>
<fieldset id="peerLegend" style="min-width:0"><legend>Chunk relationships</legend>
<label><input type="checkbox" id="peerOverlay" checked> Highlight peers on the floorplan</label>
<div id="peerCounts" aria-live="polite"></div>
<div><b style="color:#d27713">▣</b> Selected · <b style="color:#778593">▧</b> Connected</div>
<div><b style="color:#078b74">┄ L</b> Same logical partition</div>
<div><b style="color:#9854cc">━ P</b> Same physical implementation</div>
<div><b style="color:#c94f83">·· I</b> Shared pin interface</div>
<details id="peerDetails"><summary>Find a related home</summary>
<select id="peerFilter" aria-label="Relationship"><option value="nets">Connected</option><option value="logical">Logical partition</option><option value="physical">Physical implementation</option><option value="pins">Pin interface</option></select>
<div id="peerList"></div><button id="peerPrev">Previous</button> <button id="peerNext">Next</button></details></fieldset>
<div id="chunkInfo" class="mut"></div>
<label>Density <input id="chunkDensity" type="number" min="0.001" max="1" step="any"></label>
<button id="chunkApply">Apply</button> <button id="chunkClear">Inherit</button>
<details><summary>Move a physical chunk</summary>
<label><input type="checkbox" id="chunkDrag" checked>Drag chunks (Alt-drag to pan)</label>
<label><input type="checkbox" id="reviewDrop">Preview drop before applying</label>
<span id="dropActions" hidden><button id="dropApply">Apply preview</button> <button id="dropDiscard">Discard preview</button></span>
<label>Allow overlap <input type="number" id="chunkOverlap" min="0" max="25" step="1" value="0"> % per chunk</label>
<label>Peer <select id="geometryPeer" style="max-width:100%"></select></label><output id="geometryPair" style="display:block" class="mut"></output>
<label>Width / height <input id="homeAspect" type="number" min="0" step="any" style="width:6em"></label>
<button id="aspectPreview">Preview shape</button> <button id="aspectApply">Apply shape</button>
<label>Step depth (µm) <input id="boundaryDepth" type="number" step="any" style="width:6em"></label>
<button id="stepPreview">Preview stepped edge</button> <button id="stepApply">Apply stepped edge</button>
<button id="geometryLock">Lock aspect</button> <button id="geometryOthers">Lock other aspects</button>
<button id="geometryUnlock">Unlock all</button> <button id="geometryUndo">Undo last geometry edit</button>
<output id="geometryInfo" aria-live="polite" style="display:block">Drag to estimate wire spans with peers held still. Drop to refine boxes and pins.</output>
</details>
<details><summary>Physical homes</summary>
<label><input id="homeLinks" type="checkbox" checked>Show selected interconnects</label>
<label><input id="homeHeat" type="checkbox">Routing estimate</label><select id="homeMap" aria-label="Routing endpoints"><option value="boundary">Boundary contacts</option><option value="coarse">Color centers</option></select><select id="homeRouting" aria-label="Floorplan routing overlay"><option value="0">Crossing demand</option><option value="2">Internal demand</option><option value="4">Available capacity</option><option value="6">Estimated overflow</option></select><select id="homeDirection" aria-label="Routing direction"><option value="0">Horizontal</option><option value="1">Vertical</option></select>
<div id="homeMetrics" class="mut"></div><small>Wire and route proxies count crossing nets only. Split or fuse changes that set. Final placement checks row cutouts and fixed-cell obstacles.</small>
<label>Edit <select id="homeAction"><option value="split">Split this home</option><option value="transfer">Move members to another home</option><option value="fuse">Fuse with another home</option></select></label>
<div id="homeSplit"><label>Members <select id="homeMembers" multiple size="4" style="max-width:100%"></select></label>
<label id="homeNameField">New home ID <input id="homeName" style="min-width:0;width:9em"></label></div>
<div id="homeFuse" hidden><label>Other home <select id="homePeer" style="max-width:100%"></select></label></div>
<button id="suggestTransfer" hidden>Suggest a color</button> <button id="homePreview">Preview</button> <button id="homeApply">Apply</button> <button id="homeUndo">Undo home edit</button>
<output id="homeStatus" aria-live="polite" style="display:block"></output></details>
<details><summary>Copy a boundary interface</summary>
<div id="copyTarget" class="mut"></div>
<label>Source home <select id="copySource" style="max-width:100%"></select></label>
<label>Orientation <select id="copyOrientation"><option value="auto">Lowest crossing HPWL</option><option value="N">Unchanged (N)</option><option value="FN">Mirror left/right (FN)</option><option value="FS">Mirror top/bottom (FS)</option><option value="S">Rotate 180° (S)</option></select></label>
<div class="mut">Only matching pins move. Unmatched contacts keep their own locations.</div>
<button id="copyPreview">Preview pins</button> <button id="copyApply">Apply pins</button> <button id="copyClear">Remove copy</button>
<output id="copyStatus" aria-live="polite" style="display:block"></output>
<details id="copyContacts"><summary>Inspect boundary contacts</summary>
<label>Show <select id="copyFilter"><option value="local">Kept local</option><option value="copied">Copied from source</option></select></label>
<button id="copyPrev">Previous</button> <button id="copyNext">Next</button>
<div id="copyRange" aria-live="polite"></div><ol id="copyPins" style="padding-left:2em;overflow-wrap:anywhere"></ol></details></details>
<details><summary>Chunk implementations</summary>
<label>Logical partition <select id="implFamily" style="max-width:100%"></select></label>
<div id="implSummary" class="mut"></div>
<label>Instances <select id="implCopies" aria-label="Instances of this logical partition" multiple size="5" style="width:100%"></select></label>
<label>Change <select id="implAction">
<option value="group">Group selected copies</option>
<option value="shared">Share all copies</option><option value="separate">Separate every copy</option>
<option value="auto">Automatically group all copies</option></select></label>
<div id="implEditor">
<label>Existing group <select id="implExisting"></select></label>
<label>Group name <input id="implName" placeholder="e.g. A1" style="width:7em"></label>
<label>Target width / height <input id="implAspect" type="number" step="any" min="0" placeholder="Automatic" style="width:6em"></label>
<label>Interface pin <select id="implRole" style="max-width:100%"></select></label>
<label>Side <select id="implSide"><option value="">Automatic</option><option>left</option><option>right</option><option>bottom</option><option>top</option></select>
 Offset <input id="implOffset" type="number" min="0" max="1" step="any" value=".5" style="width:5em"></label>
</div>
<button id="implPreview">Preview change</button> <button id="implApply">Apply change</button>
</details>
<button id="chunkSave">Save design metadata</button><output id="chunkStatus" aria-live="polite"></output>
</details>
<h4>Options <span id="f_n" class="mut"></span></h4>
<button id="o_open" style="width:100%">synth.place options&hellip;</button>
<div class="mut" style="font-size:11px;margin-top:3px">Every <code>lhd list options synth.place</code> entry, editable; rerun from the start or from the selected checkpoint.</div>
<h4>Help</h4>
<div class="mut">? for help</div>
</div>
<div id="help"><div class="hb"><b>Keys and mouse</b><table>
<tr><td>drag an object</td><td>move and pin it (a fixed macro or memory restarts the flow with it there)</td></tr>
<tr><td>click, shift+click</td><td>select, add to / remove from the selection</td></tr>
<tr><td>shift+drag</td><td>box select</td></tr>
<tr><td>drag empty space</td><td>pan</td></tr>
<tr><td>wheel, z / +, Z / -</td><td>zoom in / out around the pointer</td></tr>
<tr><td>f</td><td>fit the design</td></tr>
<tr><td>space</td><td>run / pause</td></tr>
<tr><td>u</td><td>unpin the selection</td></tr>
<tr><td>left / right arrow</td><td>step the recorded history</td></tr>
<tr><td>Esc</td><td>clear the selection, close a panel</td></tr>
<tr><td>?</td><td>this help</td></tr>
<tr><td>Align buttons</td><td>line up with the first selected object, which stays put</td></tr>
</table><div class="mut" style="margin-top:8px">click anywhere or press ? / Esc to close</div></div></div>
<div id="opts"><div class="oh"><b>synth.place options</b><span id="o_n" class="mut"></span><input type="text" id="o_q" placeholder="filter by name or help"><label><input type="checkbox" id="o_chg">edited only</label><button id="o_close">Close</button></div>
<div id="o_list" class="mut">-</div>
<div class="of"><span id="o_info" class="mut" style="flex:1"></span><button id="o_reset">Reset edits</button><button id="o_cp" disabled>Rerun from checkpoint</button><button id="o_start" class="primary">Rerun from start</button></div></div>
<div id="tip" class="tip"></div>
<script>
"use strict";
const T=new URLSearchParams(location.search).get('t')||'';
const $=id=>document.getElementById(id);
const cv=$('cv'),ctx=cv.getContext('2d');
let D=null,X=null,Y=null,PX=null,PY=null,S={},names={},pinned=new Set(),sel=new Set();
let view={s:1,ox:0,oy:0},dpr=1,dirty=true,frameId=-1,sceneId=-1,ver=0,drag=null,hover=-1,grid=null;
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const exploration = new URLSearchParams(location.search).get('x');
function api(p,o){const q=new URLSearchParams({t:T});if(exploration&&p!='/agent')q.set('x',exploration);return fetch(p+(p.includes('?')?'&':'?')+q,o)}
async function xcommand(o){const r=await(await api('/agent',{method:'POST',body:JSON.stringify(o)})).json();if(r.ok===false)throw Error(r.error);return r}
function documentUrl(path,target,params={}){
 const u=new URL(path,location.href);u.searchParams.set('t',T);
 if(target && target!=='main')u.searchParams.set('x',target);
 for(const [k,v] of Object.entries(params))u.searchParams.set(k,v);
 return u;
}
async function readGrid(target,kind,frame){
 const res=await fetch(documentUrl('/map',target,frame==null?{kind}:{kind,frame}));if(!res.ok)return null;
 const bytes=await res.arrayBuffer();if(bytes.byteLength<24)return null;
 const view=new DataView(bytes),bx=view.getUint32(4,true),by=view.getUint32(8,true);
 if(!bx||!by||bytes.byteLength!==24+4*bx*by)return null;
 return {bx,by,peak:view.getFloat32(12,true),ovf:view.getFloat32(16,true),v:new Float32Array(bytes,24)};
}
function gotoExploration(id){const u=new URL(location.href);if(id)u.searchParams.set('x',id);else u.searchParams.delete('x');location.href=u}
async function discardCandidate(id,button){
 button.disabled=true;
 try{
  await xcommand({cmd:'explore_discard',id});
  if(id===exploration){gotoExploration(null);return}
  await explorations();
 }catch(e){$('xstatus').textContent=e.message;button.disabled=false}
}
async function downloadExport(target,what){
 try{
  const text=what==='macros'||what==='intent'?
    (await xcommand({cmd:'export',target,what,format:'metadata',encoding:'json_text'})).records_json:
    JSON.stringify(what==='record'?(await xcommand({cmd:'explorations',ids:[target]})).explorations[0]:await xcommand({cmd:'export',target,what}),null,2);
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([text+'\n'],{type:'application/json'}));
  a.download=target+'-'+what+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);
 }catch(e){$('xstatus').textContent=e.message}
}
$('exportData').onclick=()=>downloadExport(exploration||'main',$('exportKind').value);
let openingForeground=null,ledgerEpoch='',ledgerRequest=0,ledgerApplied=0,ledgerPolling=false;
const ledgerRows=new Map();
// Show absolute measurements as well as pairwise deltas. Build the tables
// only when requested; large multi-clock runs do not slow down ledger polling.
function fillTimingBreakdown(panel,endpoints){
   panel.replaceChildren();
  for(const [kind,groups] of [['Capture clock',endpoints.capture_clocks],['Path family',endpoints.path_families]]){
   if(!groups||!Object.keys(groups).length)continue;
   const table=document.createElement('table'),header=table.createTHead().insertRow();
   for(const text of [kind,'WNS (ps)','TNS (ps)','Failing / constrained']){
    const th=document.createElement('th');th.textContent=text;header.append(th);
   }
   const body=table.createTBody();
   for(const [name,g] of Object.entries(groups)){
    if(!g)continue;
    const tr=body.insertRow();
    for(const text of [name||'(no clock)',fmt(g.wns_ps,2),fmt(g.tns_ps,2),(g.failing??'–')+' / '+(g.endpoints??'–')])tr.insertCell().textContent=text;
   }
   panel.append(table);
  }
}
function addTimingBreakdown(row,endpoints,label){
 if(!endpoints?.total?.endpoints)return;
 const details=document.createElement('details'),title=document.createElement('summary'),panel=document.createElement('div');
 title.textContent=label+' by clock and path family';details.append(title,panel);row.append(details);
 details.ontoggle=()=>{if(details.open&&!panel.childElementCount)fillTimingBreakdown(panel,endpoints)};
}
async function explorations(){
 const known=Object.create(null),request=++ledgerRequest;
 for(const [id,entry] of ledgerRows)if(entry.cache)known[id]=entry.cache;
 const r=await xcommand({cmd:'explorations',epoch:ledgerEpoch,known});
 if(request<ledgerApplied)return;
 ledgerApplied=request;
 const box=$('explist');
 if(ledgerEpoch!==r.epoch){ledgerRows.clear();box.replaceChildren();ledgerEpoch=r.epoch}
 const visible=new Set(r.unchanged);
 for(const x of r.explorations){const row=document.createElement('div');row.textContent=x.id+' '+x.state+(x.reason?' · '+x.reason:'');
 if(x.label)row.append(' · '+x.label);
 if(x.queue_s!=null)row.append(' · queue '+x.queue_s.toFixed(2)+' s');
 if(x.startup_s!=null)row.append(' · '+(x.worker==='replay'?'replay ':'start ')+x.startup_s.toFixed(2)+' s');
 if(x.run_s!=null)row.append(' · run '+x.run_s.toFixed(2)+' s');
 if(x.cpu_s!=null)row.append(' · CPU '+x.cpu_s.toFixed(2)+' s');
 if(x.work)row.append(' · '+x.work.full_calls+' full / '+x.work.ranking_calls+' ranking calls · '+x.work.gp_iterations+' GP iterations');
 if(x.sampled_session_rss_bytes>0){
   const mib=bytes=>Math.ceil(bytes/1048576);
   row.append(' · sampled session peak '+mib(x.sampled_session_rss_bytes)+' / '+mib(r.limits.session_memory_bytes)+' MiB');
 }
 if(!x.closed && x.state==='done' && x.tier!=='region'){
 const img=document.createElement('img');img.src=documentUrl('/screenshot.png',x.id,{w:96,h:64,layers:'congestion,pin_sides'});img.alt='Placement '+x.id;img.loading='lazy';img.width=48;row.prepend(img,' ');
 const download=document.createElement('button');download.textContent='Export';download.onclick=()=>downloadExport(x.id,'macros');row.append(' ',download);
}
const evidence=x.last_measurement||x.result;
if(x.last_measurement)row.append(' · last full timing at '+(evidence.gate||'iteration '+evidence.iteration)+', frame '+evidence.frame);
if(evidence?.geometry_status==='illegal'){const warning=document.createElement('strong');warning.textContent=' · ILLEGAL GEOMETRY'+(x.tier==='T4'?' at route input':'');row.append(warning)}
if(evidence?.summary){const t=evidence.summary.timing,r=evidence.summary.routing,q=evidence.summary.quality;
 const routed=t.model==='final_downstream_sta';
 row.append(' · '+(routed?'Routed setup WNS ':'WNS ')+(t.wns_ps??'–')+' ps · TNS '+(t.tns_ps??'–')+' ps · '+r.estimator+' '+(r.overflow_bin_share==null?'unavailable':(100*r.overflow_bin_share).toFixed(1)+'% over capacity')+' · quality '+(q.score==null?(q.status==='not_configured'?'not configured':'unavailable'):fmt(q.score,2)));
 if(t.repair_advice?.recommended)row.append(' · '+repairAdviceText(t));
 if(!routed)addTimingBreakdown(row,t.endpoints,'Placement setup timing')}
if(x.result?.validation){const q=x.result.validation.row, t=x.result.validation.setup_timing;
 row.append(' · '+q.status+' · DRC '+(q.drc??'–'));
 if(t?.status!=='measured'&&q.status==='routed')row.append(' · Setup timing unavailable: '+(t?.reason||'no final setup measurement'));
 if(t?.status==='measured'&&t.endpoints?.status==='measured'&&t.endpoints.total){
  row.append(' · '+t.endpoints.total.failing+' failing / '+t.endpoints.total.endpoints+' constrained endpoints');
  addTimingBreakdown(row,t.endpoints,'Routed setup timing');
 }else if(t?.status==='measured')row.append(' · Endpoint breakdown unavailable'+(t.endpoints?.reason?': '+t.endpoints.reason:''));
 const checks=x.result.validation.placement_checks;
 if(checks){const failures=Array.isArray(checks.legalization_failure_counts)?checks.legalization_failure_counts.filter(n=>n>0):[];
 row.append(' · final placement: '+String(checks.final_legality??'not reported').replace('_',' ')+' · macro poses (applied/final): '+(checks.applied_macro_poses??'not reported')+'/'+(checks.routed_macro_poses??'not reported'));
 if(failures.length)row.append(' · earlier legalization failures per pass: '+failures.join(', '));}
 if(!x.closed){const a=document.createElement('a');a.href=documentUrl('/validation/receipt.json',x.id);a.textContent='Receipt';row.append(' ',a)}}
if(x.page_url){const a=document.createElement('a');a.href=x.page_url;a.textContent='Region page';row.append(' ',a)}
 if(x.closed){const b=document.createElement('button');b.textContent='Save record';b.onclick=()=>downloadExport(x.id,'record');row.append(' ',b)}
 if(x.tier!=='region'&&!x.closed&&(x.state==='ready'||x.state==='done')&&x.id!==exploration){const b=document.createElement('button');b.textContent='View';b.onclick=()=>gotoExploration(x.id);row.append(' ',b)}
 if(!x.closed && x.id!==exploration && x.mode!=='foreground'){
   const button=document.createElement('button');
   button.textContent=['queued','starting','configuring','running'].includes(x.state)?'Cancel':'Discard';
   button.setAttribute('aria-label',button.textContent+' '+x.id);button.onclick=()=>discardCandidate(x.id,button);
   row.append(' ',button);
 }
 const previous=ledgerRows.get(x.id);
 if(previous)previous.row.replaceWith(row);else box.append(row);
 const terminal=!['running','queued','starting','configuring','ready'].includes(x.state);
 const cache=x.details_evicted?'evicted':x.closed?'closed':terminal&&x.mode!=='foreground'?'terminal':'';
 ledgerRows.set(x.id,{id:x.id,state:x.state,mode:x.mode,closed:x.closed,reason:x.reason,
   measured:!!(x.result?.summary||x.result?.validation),row,cache});
 visible.add(x.id)}
 for(const [id,entry] of ledgerRows)if(!visible.has(id)){entry.row.remove();ledgerRows.delete(id)}
 const entries=Array.from(ledgerRows.values());
 if(openingForeground){
   const row=ledgerRows.get(openingForeground);
   if(row?.state==='ready'&&!row.closed){openingForeground=null;gotoExploration(row.id);return}
   if(row&&(row.closed||['failed','stale','cancelled','timed_out','budget_exhausted'].includes(row.state))){
     openingForeground=null;$('xstatus').textContent=row.reason||row.state;
   }
 }
 const closed=new Set(entries.filter(x=>x.closed).map(x=>x.id));
 for(const name of ['xref','xcandidate']){
   const select=$(name),value=select.value;
   const ids=entries.filter(x=>x.mode!=='foreground'&&x.state==='done'&&x.measured).map(x=>x.id);
   if(r.main.timing.wns_ps!=null&&r.main.timing.model==='placement_rc')ids.unshift('main');
   if(Array.from(select.options,o=>o.value).join()!==ids.join()){
     select.replaceChildren(...ids.map(id=>new Option(id,id)));
     if(ids.includes(value))select.value=value;else if(name==='xcandidate')select.selectedIndex=ids.length-1;
   }
   for(const option of select.options)option.dataset.closed=String(closed.has(option.value));
 }
 $('xcompare').disabled=!$('xref').value||!$('xcandidate').value;
 if(!exploration){const open=entries.find(x=>!x.closed&&x.mode==='foreground');$('xbegin').disabled=!!open;$('xclose').hidden=!open;if(open){$('xclose').disabled=false;$('xclose').onclick=()=>discardCandidate(open.id,$('xclose'))}}
}
let xcap={},submitting=false;
const scoreLosses={negative_wns_ps:'Negative WNS (ps)',negative_tns_ps:'Negative TNS (ps)',overflow_bin_share:'Over-capacity bin share',routing_overflow:'Routing overflow',hpwl_um:'HPWL (µm)'};
function loadScore(){
 const q=S.summary?.quality;if(!q)return;
 $('scoreEstimator').value=q.estimator;$('scoreTerms').replaceChildren();
 for(const [loss,label] of Object.entries(scoreLosses)){
  const term=q.components.find(t=>t.loss===loss),row=$('scoreTerms').insertRow();
  row.dataset.loss=loss;row.insertCell().textContent=label;
  for(const field of ['weight','half']){
   const input=document.createElement('input');input.type='number';input.min='0';input.step='any';
   input.setAttribute('aria-label',label+' '+(field==='half'?'half-score loss':field));
   input.value=term?.[field]??'';row.insertCell().append(input);
  }
 }
}
$('scoreSettings').ontoggle=()=>{if($('scoreSettings').open){loadScore();$('scoreStatus').textContent=''}};
async function setScore(disable){
 const terms=[];
 if(!disable)for(const row of $('scoreTerms').rows){
  const [w,h]=row.querySelectorAll('input');
  if(w.value===''&&h.value==='')continue;
  if(w.value===''||!w.validity.valid||!(h.valueAsNumber>0)){
   $('scoreStatus').textContent='Enter a nonnegative weight and positive half-score loss for '+scoreLosses[row.dataset.loss]+'.';return;
  }
  terms.push({loss:row.dataset.loss,weight:w.valueAsNumber,half:h.valueAsNumber});
 }
 try{
  const reply=await xcommand({cmd:'score_config',target:exploration||'main',estimator:$('scoreEstimator').value,components:terms});
  S.summary=reply.summary;loadScore();$('scoreStatus').textContent=terms.length?'Score applied.':'Score disabled.';
 }catch(e){$('scoreStatus').textContent=e.message}
}
$('scoreApply').onclick=()=>setScore(false);$('scoreDisable').onclick=()=>setScore(true);
function candidateControls(){
 $('exportData').disabled=$('stripRead').disabled=rev!==null||!S.frame;
 const tier=$('xtier').value,relax=tier==='T2',repair=tier==='T3',scoped=(relax||repair)&&$('xselected').checked;
 $('xbudget').hidden=$('xscope').hidden=!(relax||repair);$('xrepairs').hidden=!repair;
 $('xunit').textContent=relax?'GP iterations':'Trial limit';
 $('xcount').max=relax?10000:64;
 $('xbuffer').disabled=scoped;$('xresynth').disabled=scoped||!xcap.repair_operations?.includes('resynthesis');
 for(const o of $('xtier').options)o.disabled=o.value==='T3'&&!xcap.terminal_repair || o.value==='T4'&&!xcap.external_validation || o.value==='T2'&&!S.legalize_available;
 const gate=S.state==='gate'||S.state==='paused',live=rev===null&&(!exploration||gate);
 $('xeval').disabled=!live;
 $('scoreFields').disabled=!live||(!exploration&&!!S.foreground);
 $('xsubmit').disabled=!live||submitting||!S.frame||$('xtier').selectedOptions[0].disabled || tier!=='T0'&&!gate || scoped&&!sel.size;
}
$('xtier').onchange=$('xselected').onchange=candidateControls;
$('xsubmit').onclick=async()=>{
 const tier=$('xtier').value,request={cmd:'explore',tier,patch:[],target:exploration||'main',client:'browser',expect:{frame:S.frame,baseline:S.baseline_identity}};
 const count=Number($('xcount').value);
 if(tier==='T2')request.budget={iterations:count};
 if(tier==='T3'){
   request.budget={repair_trials:count};
   const limit=$('xtiminglimit');
   if(!limit.validity.valid){limit.reportValidity();return}
   if(limit.value!=='')request.budget.timing_calls=limit.valueAsNumber;
   request.policy={sizing:$('xsize').checked,buffering:!$('xbuffer').disabled&&$('xbuffer').checked,resynthesis:!$('xresynth').disabled&&$('xresynth').checked};
 }
 if((tier==='T2'||tier==='T3')&&$('xselected').checked)request.scope={nodes:[...sel].map(i=>'#'+i)};
 submitting=true;candidateControls();
 try{
  if($('xpatch').value.trim()){
   const patch=JSON.parse($('xpatch').value);
   if(patch?.units==='dbu'&&patch.dbu_per_um!==D.dbu)throw Error('Patch and design use different database units per micron');
   request.patch=Array.isArray(patch?.moves)&&!patch.cmd?patch.moves:patch;
  }
  const row=await xcommand(request);$('xstatus').textContent=row.id+' · '+row.state;await explorations()
 }
 catch(e){$('xstatus').textContent=e.message}
 finally{submitting=false;candidateControls()}
};
let comparisonEpoch=0;
function repairAdviceText(t){
 const reasons={negative_setup_slack:'setup slack '+fmt(t.wns_ps,1)+' ps',negative_hold_slack:'hold slack '+fmt(t.hold_wns_ps,1)+' ps',electrical_violations:t.electrical_violations+' electrical violations'};
 return 'Consider a repair trial: '+t.repair_advice.reasons.map(r=>reasons[r]).join(', ')+'.'+(t.repair_advice.mutations_frozen?' Implementation edits are frozen.':'');
}
function showTimingDeltas(delta,fmt){
 const panel=$('xtimingdiff');panel.replaceChildren();
 for(const [label,groups] of [['Path family',delta.path_families],['Capture clock',delta.capture_clocks]]){
  if(!groups||!Object.keys(groups).length)continue;
  const table=document.createElement('table'),header=table.createTHead().insertRow();
  for(const name of [label,'Δ WNS (ps)','Δ TNS (ps)','Δ failing']){
   const th=document.createElement('th');th.textContent=name;header.append(th);
  }
  const body=table.createTBody();
  for(const [name,group] of Object.entries(groups)){
   const row=body.insertRow();row.insertCell().textContent=name;
   if(group.coverage==='matched'){
    for(const value of [group.wns_ps,group.tns_ps,group.failing])row.insertCell().textContent=fmt(value,'');
   }else{const cell=row.insertCell();cell.colSpan=3;cell.textContent='Endpoint set changed ('+group.reference_endpoints+' → '+group.candidate_endpoints+')'}
  }
  panel.append(table);
 }
 panel.hidden=!panel.childElementCount;
}
async function comparePlacement(){
 const epoch=++comparisonEpoch,left=$('xref').value,right=$('xcandidate').value;
 $('xplot').hidden=true;$('xtimingdiff').hidden=true;$('xgeometry').hidden=true;
 try{
  const result=await xcommand({cmd:'compare',reference:left,candidate:right});
  if(epoch!==comparisonEpoch)return;
  const fmt=(x,unit)=>x==null?'unavailable':x.toLocaleString(undefined,{maximumFractionDigits:4})+unit;
  const d=result.delta;
  const illegal=Object.entries(result.geometry||{}).filter(([,status])=>status==='illegal').map(([side])=>side);
  if(illegal.length){
   $('xgeometry').textContent='ILLEGAL GEOMETRY · '+illegal.join(' and ')+(result.tier==='T4'?' at route input':'');
   $('xgeometry').hidden=false;
  }
  const layer=$('xlayer');
  for(const option of layer.options)option.disabled=result.tier==='T0'&&option.value!=='density';
  if(result.tier==='T0'){
   layer.value='density';
   $('xdiff').textContent='Geometry · reference '+result.geometry.reference+' · candidate '+result.geometry.candidate;
  }else if(result.tier==='T4'){
   $('xdiff').textContent='Δ WNS '+fmt(d.wns_ns,' ns')+' · Δ TNS '+fmt(d.tns_ns,' ns')+' · Δ DRC '+fmt(d.drc,'')+' · Δ wirelength '+fmt(d.wl_um,' µm')+' · Δ quality '+fmt(d.quality,'');
   if(result.timing_coverage!=='matched')$('xdiff').append(' · Endpoint coverage '+result.timing_coverage);
   showTimingDeltas(d,fmt);
   return;
  }else{
   $('xdiff').textContent='Δ WNS '+fmt(d.wns_ps,' ps')+' · Δ TNS '+fmt(d.tns_ps,' ps')+' · Δ over-capacity share '+fmt(d.overflow_bin_share,'')+' · Δ quality '+fmt(d.quality,'');
   if(result.timing_coverage!=='matched')$('xdiff').append(' · Endpoint coverage '+result.timing_coverage);
   showTimingDeltas(d,fmt);
  }
  // Closed candidates still have measured timing; their maps and physical scenes have been released.
  if($('xref').selectedOptions[0]?.dataset.closed==='true'||$('xcandidate').selectedOptions[0]?.dataset.closed==='true')return;
  const [a,b,pa,pb]=await Promise.all([readGrid(left,layer.value,result.reference_frame),readGrid(right,layer.value,result.candidate_frame),xcommand({cmd:'export',target:left,what:'macros'}),xcommand({cmd:'export',target:right,what:'macros'})]);
  if(epoch!==comparisonEpoch||!pa.macros||!pb.macros)return;
  if((result.reference_frame!=null&&pa.frame!==result.reference_frame)||(result.candidate_frame!=null&&pb.frame!==result.candidate_frame)){
   $('xdiff').append(' · Placement changed since comparison.');return;
  }
  if(JSON.stringify(pa.core_dbu)!==JSON.stringify(pb.core_dbu)||JSON.stringify(pa.outline)!==JSON.stringify(pb.outline)){
   $('xdiff').append(' · Map difference unavailable for different floorplans.');return;
  }
  $('xplot').hidden=false;
  const canvas=$('xmap'),g=canvas.getContext('2d'),[xl,yl,xh,yh]=pa.core_dbu;
  canvas.height=Math.max(48,Math.min(400,Math.round(canvas.width*(yh-yl)/(xh-xl))));
  const w=canvas.width,h=canvas.height,px=x=>(x-xl)*w/(xh-xl),py=y=>h-(y-yl)*h/(yh-yl);
  g.clearRect(0,0,w,h);const outline=floorplanPath(px,py,pa.outline);g.fillStyle='#ececec';g.fill(outline,'evenodd');
  let scale=0;
  const available=a&&b&&a.bx===b.bx&&a.by===b.by;
  if(available){
   g.save();g.clip(outline,'evenodd');
   for(let i=0;i<a.v.length;i++)scale=Math.max(scale,Math.abs(b.v[i]-a.v[i]));
   for(let x=0;x<a.bx;x++)for(let y=0;y<a.by;y++){
    const k=x*a.by+y,diff=b.v[k]-a.v[k];
    g.fillStyle=diff>=0?'#bd4b34':'#287f9b';g.globalAlpha=scale?Math.abs(diff)/scale:0;
    g.fillRect(x*w/a.bx,h-(y+1)*h/a.by,Math.ceil(w/a.bx),Math.ceil(h/a.by));
   }
   g.restore();
  }
  g.globalAlpha=1;g.strokeStyle='#272727';g.fillStyle='#272727';
  const ref=new Map(pa.macros.map(m=>[m.name,m]));let moved=0,reoriented=0;
  for(const m of pb.macros){const old=ref.get(m.name);if(!old)continue;
   if(old.x!==m.x||old.y!==m.y){
    moved++;g.beginPath();g.moveTo(px(old.x),py(old.y));g.lineTo(px(m.x),py(m.y));g.stroke();g.beginPath();g.arc(px(m.x),py(m.y),2.5,0,2*Math.PI);g.fill();
   }
   if(old.orient!==m.orient){reoriented++;g.strokeRect(px(m.x)-4,py(m.y)-4,8,8)}
  }
  $('xlegend').textContent=(available?'Red increases, blue decreases. Peak |Δ| '+fmt(scale,'')+'.':'Map unavailable.')+
    ' '+moved+' displaced (dots), '+reoriented+' reoriented (squares).';
 }catch(e){if(epoch===comparisonEpoch)$('xdiff').textContent=e.message}
}
$('xcompare').onclick=comparePlacement;$('xlayer').onchange=comparePlacement;
let chunkPlan=null,homeLabels=new Map();
function homeLabel(c,index){
 const paths=c.instances||[],first=(paths[0]||'').split('.');
 let n=first.length;
 for(const path of paths){const parts=path.split('.');let k=0;while(k<n&&k<parts.length&&parts[k]===first[k])++k;n=k}
 const common=first.slice(0,n).slice(-2).reverse().join(' / ');
 return 'H'+(index+1)+(common?' · '+common:'');
}
function showChunks(plan) {
 const select=$('chunkSelect'),previous=select.value; chunkPlan=plan;
 homeLabels=new Map((plan?.chunks||[]).map((c,i)=>[c.id,homeLabel(c,i)]));
 $('chunks').hidden=!plan;select.replaceChildren();if(!plan){dirty=true;return}
 const added=new Set();
 function add(kind,key,label,density,info){
   const value=JSON.stringify([kind,key]);if(added.has(value))return;added.add(value);
   const opt=new Option(label,value);opt.title=key;opt.dataset.density=density;opt.dataset.info=info;select.add(opt);
 }
 const local=new Map((plan.local_placement||[]).map(r=>[r.id,r]));
 const actual=new Map((D?.chunk_results?.chunks||[]).map(c=>[c.id,c]));
 const colors=new Map((D?.chunk_results?.colors||[]).map(c=>[c.id,c]));
 for(const c of plan.chunks){
   const measured=actual.get(c.id)?.fill;
   const fill=c.planned_fill===undefined?'':'; planned fill '+(100*c.planned_fill).toFixed(1)+'%';
   const mem=plan.memory_prediction;
   const basis=!mem?'':(mem.samples?'; based on '+mem.samples+' measured run(s)'+(mem.extrapolating?' (design outside measured sizes)':''):'; cold estimate, no matching measured runs');
   const msg='Target '+(100*c.density).toFixed(1)+'% ('+c.density_source+')'+fill+'; estimated '+(c.estimated_bytes/1e6).toFixed(2)+' MB'+basis+(measured===undefined?'':'; measured fill '+(100*measured).toFixed(1)+'%')+
     (c.allowance_bytes>plan.budget_bytes?' (single-object allowance)':'')+
     (c.reserved_dbu2>(c.box_dbu[2]-c.box_dbu[0])*(c.box_dbu[3]-c.box_dbu[1])?' · requested area exceeds available space':'');
   const r=local.get(c.id),result=!r?'; no local placement measured for this plan':r.status==='placed'?
     '; local placement legal · HPWL '+num(r.hpwl_dbu/r.dbu_per_um)+' µm · peak '+(r.peak_rss_bytes/1e6).toFixed(1)+' MB · '+r.boundary_contacts+' fixed boundary contacts':
     '; local placement: '+r.status+(r.reason?' · '+r.reason:'');
   add('chunk',c.id,homeLabels.get(c.id)+(c.fixed_macros?' (fixed)':''),c.density,msg+result+(c.fixed_macros?'; position fixed by '+c.fixed_macros+' macro(s)':''));
   if(c.partition!==c.id)add('partition',c.partition,'Partition '+c.partition,c.density,'Shared by matching instances');
   for(const m of c.members)if(!m.macro){
     const msg='Home '+c.id+'; '+m.cells+' cells; effective density '+(100*m.density).toFixed(1)+'% ('+m.density_source+')'+(colors.has(m.id)?'; measured slot fill '+(100*colors.get(m.id).slot_fill).toFixed(1)+'%':'');
     add('color',m.id,'Color '+m.id,m.density,msg);
     if(m.template_color)add('template_color',m.template_color,'Template color '+m.template_color,m.density,'Shared by matching instances');
   }
 }
 if(added.has(previous))select.value=previous;
 const families=[...new Set(plan.chunks.map(c=>c.partition))], picker=$('implFamily'), saved=picker.value;
 picker.replaceChildren(...families.map(f=>new Option(f,f)));if(families.includes(saved))picker.value=saved;
 select.onchange();
 if(saved&&families.includes(saved)&&picker.value!==saved){picker.value=saved;showImplementations()}
 dirty=true;
}
$('chunkSelect').onchange=()=>{const opt=$('chunkSelect').selectedOptions[0];if(opt){$('chunkDensity').value=opt.dataset.density;$('chunkInfo').textContent=opt.dataset.info;
 const [kind,id]=JSON.parse(opt.value);
 const c=chunkPlan.chunks.find(c=>kind==='chunk'?c.id===id:kind==='partition'?c.partition===id:c.members.some(m=>kind==='color'?m.id===id:m.template_color===id));
 if(c){$('implFamily').value=c.partition;geomFocus=c.id;$('geometryLock').textContent=c.aspect_locked?'Unlock aspect':'Lock aspect';geometryPeers(c);homeControls(c)}
 showImplementations(c?.id);dirty=true}};
$('chunkLines').onchange=()=>{dirty=true};
// The pin identity is per net and the physical implementation name is scoped
// to its logical partition. An interface copy alone never implies cell reuse.
let relationCache=null,legendCache=null,peerStart=0;
function relatedHomes(plan,id){
 if(relationCache?.plan===plan&&relationCache.id===id)return relationCache;
 const c=plan.chunks.find(h=>h.id===id),rows=new Map(),totals={nets:0,logical:0,physical:0,pins:0};
 if(c){
   const counts=new Map(),root=c.interface_copy?.source||c.id;
   if(peerCache?.plan===plan&&peerCache.id===id){
     for(const [key,pairs] of peerCache.map)counts.set(key,pairs.length);
   }else{
     const {nets}=geometryIndex(plan);
     for(const p of c.ports)for(const [other] of nets.get(p.net_index??p.net)||[])
       if(other!==c)counts.set(other.id,(counts.get(other.id)||0)+1);
   }
   for(const h of plan.chunks){if(h===c)continue;
     const logical=h.partition===c.partition,physical=logical&&h.implementation===c.implementation;
     const pins=(h.interface_copy?.source||h.id)===root,nets=counts.get(h.id)||0;
     if(!nets&&!logical&&!pins)continue;
     const rec={id:h.id,nets,logical,physical,pins};rows.set(h.id,rec);
     for(const key of Object.keys(totals))if(rec[key])++totals[key];
   }
 }
 return relationCache={plan,id,rows,totals,present:!!c};
}
function peerListing(r){
 const list=$('peerList'),key=$('peerFilter').value;list.replaceChildren();
 const rows=[...r.rows.values()].filter(p=>p[key]);
 if(key==='nets')rows.sort((a,b)=>b.nets-a.nets);
 for(const p of rows.slice(peerStart,peerStart+8)){
   const btn=document.createElement('button');btn.textContent=(homeLabels.get(p.id)||p.id)+(key==='nets'?' · '+p.nets+' nets':'');btn.title=p.id;
   const c=chunkPlan.chunks.find(c=>c.id===p.id);btn.disabled=!c;if(!c)btn.title='New in this preview: '+p.id;btn.onclick=()=>focusGeometry(c);list.append(btn);
 }
 if(rows.length===0)list.textContent='None.';
 $('peerPrev').disabled=peerStart===0;$('peerNext').disabled=peerStart+8>=rows.length;
}
function peerLegend(plan,id){
 const r=relatedHomes(plan,id);
 if(legendCache!==r){
   legendCache=r;peerStart=0;const t=r.totals;
   const preview=homePreview?.base===chunkPlan&&plan===homePreview.plan;
   $('peerCounts').textContent=!r.present?(preview?'Selected home is absent from this preview.':'Choose a home in this frame.'):(preview?'Preview · ':'')+(homeLabels.get(id)||id||'No home')+' · '+t.nets+' connected · '+t.logical+' logical · '+t.physical+' physical · '+t.pins+' interface';
   if($('peerDetails').open)peerListing(r);
 }
 return r;
}
$('peerOverlay').onchange=()=>{dirty=true};
$('peerFilter').onchange=$('peerDetails').ontoggle=()=>{peerStart=0;if(legendCache&&$('peerDetails').open)peerListing(legendCache)};
$('peerPrev').onclick=()=>{peerStart=Math.max(0,peerStart-8);if(legendCache)peerListing(legendCache)};
$('peerNext').onclick=()=>{peerStart+=8;if(legendCache)peerListing(legendCache)};
function drawChunks(){
 if(!D?.chunk_plan)return;
 const trial=rev===null&&homePreview?.base===chunkPlan?homePreview.plan:null;
 const plan=trial||(rev===null&&chunkPlan?chunkPlan:D.chunk_plan);
 const [kind,key]=JSON.parse($('chunkSelect').value||'["",""]');
 const focus=kind==='chunk'?key:geomFocus,relations=peerLegend(plan,focus),overlay=$('peerOverlay').checked;
 const boundaries=$('chunkLines').checked;if(!boundaries&&!overlay)return;
 ctx.save();
 const selected=new Set(Array.from($('implCopies').selectedOptions,o=>o.value));
 function outline(x,y,w,h,inset,color,thick,dash){
   const d=Math.min(inset,w/5,h/5);ctx.lineWidth=thick;ctx.strokeStyle=color;ctx.setLineDash(dash);ctx.strokeRect(x+d,y+d,w-2*d,h-2*d);
 }
 for(let i=0;i<plan.chunks.length;++i){const c=plan.chunks[i],b=c.box_dbu,marked=selected.has(c.id);
   const x=sx(b[0]),y=sy(b[3]),w=(b[2]-b[0])*view.s,h=(b[3]-b[1])*view.s;
   const r=overlay&&relations.rows.get(c.id),active=c.id===focus;
   if(r?.nets){ctx.fillStyle='rgba(83,99,117,.29)';ctx.fillRect(x,y,w,h)}
   if(c.perimeter_dbu&&(boundaries||active)){
     ctx.setLineDash([]);ctx.strokeStyle=active?'#d27713':css('--acc');ctx.lineWidth=active?3:1.5;
     for(const [k,line,lo,hi] of c.perimeter_dbu){ctx.beginPath();ctx.moveTo(sx(k?line:lo),sy(k?lo:line));ctx.lineTo(sx(k?line:hi),sy(k?hi:line));ctx.stroke()}
   }else if(boundaries)outline(x,y,w,h,1,css('--acc'),1.5,[6,3]);
   if(r?.logical)outline(x,y,w,h,3,'#078b74',3,[7,4]);
   if(r?.physical)outline(x,y,w,h,7,'#9854cc',3,[]);
   if(r?.pins)outline(x,y,w,h,10,'#c94f83',3,[2,3]);
   if(active&&!c.perimeter_dbu)outline(x,y,w,h,2,'#d27713',4,[]);
   if((boundaries||active||r)&&w>23&&h>18){
     const id=(homeLabels.get(c.id)||('H'+(i+1))).split(' · ')[0],roles=(!r?'':[r.logical?'L':'',r.physical?'P':'',r.pins?'I':''].join(''));
     ctx.font='bold 12px sans-serif';let label=id+(r?.nets?' ↔'+r.nets:'')+(roles?' · '+roles:'');
     if(ctx.measureText(label).width+12>w)label=roles?id+' '+roles:id;
     if(ctx.measureText(label).width+12>w)label=id;
     const px=x+2,py=y+2;ctx.save();ctx.beginPath();ctx.rect(x,y,w,h);ctx.clip();
     ctx.fillStyle=active?'#915008':'#182735';ctx.fillRect(px,py,ctx.measureText(label).width+8,17);ctx.fillStyle='white';ctx.textAlign='left';ctx.textBaseline='top';ctx.fillText(label,px+4,py+2);ctx.restore()
   }
   if(marked){ctx.fillStyle=css('--macro');for(const port of c.ports)ctx.fillRect(sx(port.xy_dbu[0])-2,sy(port.xy_dbu[1])-2,4,4)}
   if(kind==='color'||kind==='template_color')for(const m of c.members)if(!m.macro&&(m.id===key||m.template_color===key)){
     const a=m.box_dbu;outline(sx(a[0]),sy(a[3]),(a[2]-a[0])*view.s,(a[3]-a[1])*view.s,0,css('--macro'),2,[]);
   }
 }ctx.restore();
}
// Keep the drag loop local: fixed bounds and all remote contacts of an incident
// hypernet are constant until release. A drag need not re-read every design net.
let geomFocus=null,geomBusy=false;const wireIndexes=new WeakMap();
function geometryChunk(){
 const [kind,id]=JSON.parse($('chunkSelect').value||'["",""]');
 return chunkPlan?.chunks.find(c=>c.id===(kind==='chunk'?id:geomFocus));
}
function geometryIndex(plan=chunkPlan){
 if(wireIndexes.has(plan))return wireIndexes.get(plan);
 const nets=new Map(),anchors=new Map((plan.communication?.fixed_contacts||[]).map(t=>[t.net_index,t.box_dbu]));
 const bounds=[Infinity,Infinity,-Infinity,-Infinity];
 for(const c of plan.chunks){const b=c.box_dbu;for(let k=0;k<2;++k){bounds[k]=Math.min(bounds[k],b[k]);bounds[k+2]=Math.max(bounds[k+2],b[k+2])}
   for(const p of c.ports){const key=p.net_index??p.net;if(!nets.has(key))nets.set(key,[]);nets.get(key).push([c,p])}}
 const index={nets,anchors,bounds};wireIndexes.set(plan,index);return index;
}
let peerCache=null;
function geometryPeers(c){
 const select=$('geometryPeer'),saved=select.value;
 if(peerCache?.plan===chunkPlan&&peerCache.id===c.id)return;
 const map=new Map(),{nets}=geometryIndex();
 for(const pin of c.ports)for(const [other,p] of nets.get(pin.net_index??pin.net)||[]){
   if(other.id===c.id)continue;
   if(!map.has(other.id))map.set(other.id,[]);map.get(other.id).push([pin.xy_dbu,p.xy_dbu]);
 }
 peerCache={plan:chunkPlan,id:c.id,map};
 select.replaceChildren(new Option('All incident nets',''),...[...map].sort((a,b)=>b[1].length-a[1].length).map(([id,pairs])=>new Option((homeLabels.get(id)||id)+' · '+pairs.length+' nets',id)));
 if(map.has(saved))select.value=saved;
 geometryPair(0,0);
}
function geometryPair(dx,dy){
 const pairs=peerCache?.map.get($('geometryPeer').value);let text='';
 if(pairs){
   let span=0,worst=0;
   for(const [a,b] of pairs){const d=Math.abs(a[0]+dx-b[0])+Math.abs(a[1]+dy-b[1]);span+=d;worst=Math.max(worst,d)}
   text=pairs.length+' shared nets · pin-to-pin sum '+num(span)+' DBU; longest '+num(worst)+' DBU.';
 }
 $('geometryPair').textContent=text;return pairs;
}
$('geometryPeer').onchange=()=>{geometryPair(drag?.dx||0,drag?.dy||0);dirty=true};
function focusGeometry(c){
 geomFocus=c.id;$('chunkSelect').value=JSON.stringify(['chunk',c.id]);$('chunkSelect').onchange();
}
function beginGeometry(e){
 if(rev!==null||D?.phase!=='fp'||!chunkPlan||!$('chunkDrag').checked||geomBusy||e.altKey||e.shiftKey||S.state!=='gate')return false;
 const x=wx(e.offsetX),y=wy(e.offsetY),hits=chunkPlan.chunks.filter(c=>{const b=c.box_dbu;return x>=b[0]&&x<b[2]&&y>=b[1]&&y<b[3]});
 if(!hits.length)return false;
 const c=hits.find(c=>c.id===geometryChunk()?.id)||hits[0];focusGeometry(c);
 if(c.fixed_macros){$('chunks').open=true;$('geometryInfo').textContent='Position fixed by '+c.fixed_macros+' macro(s). Select a different chunk or start from an unfixed macro plan.';dirty=true;return true}
 const {nets,anchors,bounds}=geometryIndex(),incident=[];
 for(const p of c.ports){
   const key=p.net_index??p.net,remote=(anchors.get(key)||[Infinity,Infinity,-Infinity,-Infinity]).slice();
   for(const [other,pin] of nets.get(key))if(other!==c){for(let k=0;k<2;++k){remote[k]=Math.min(remote[k],pin.xy_dbu[k]);remote[k+2]=Math.max(remote[k+2],pin.xy_dbu[k])}}
   if(Number.isFinite(remote[0]))incident.push([p.xy_dbu,remote]);
 }
 drag={chunk:c,x,y,dx:0,dy:0,incident,bounds,moved:false,frame:S.frame,intent:S.intent_revision};dirty=true;return true;
}
function geometryMotion(g,x,y){
 const b=g.chunk.box_dbu,f=g.bounds;
 g.dx=Math.max(f[0]-b[0],Math.min(f[2]-b[2],Math.round(x-g.x)));g.dy=Math.max(f[1]-b[1],Math.min(f[3]-b[3],Math.round(y-g.y)));
 if(!g.moved && Math.hypot(g.dx,g.dy)*view.s<3)return;
 g.moved=true;geometryPair(g.dx,g.dy);let sum=0,worst=0;
 for(const [p,r] of g.incident){const x=p[0]+g.dx,y=p[1]+g.dy;
   const span=Math.max(r[2],x)-Math.min(r[0],x)+Math.max(r[3],y)-Math.min(r[1],y);
   sum+=span;worst=Math.max(worst,span)}
 const delta=sum-g.chunk.incident_pin_hpwl_dbu;
 $('geometryInfo').textContent='Drag estimate · '+g.incident.length+' incident nets, peers held still · span sum '+num(sum)+' DBU ('+signed(delta)+'); longest '+num(worst)+' DBU. Release to refine.';
 dirty=true;
}
function num(v){return Math.round(v).toLocaleString()}
function signed(v){return (v>=0?'+':'')+num(v)}
function geometryOverlay(){
 if(rev!==null||!chunkPlan)return;
 ctx.save();ctx.fillStyle='rgba(148,74,204,.34)';
 if($('chunkLines').checked&&D?.phase==='fp')for(const o of chunkPlan.geometry?.overlaps||[]){const b=o.box_dbu;ctx.fillRect(sx(b[0]),sy(b[3]),(b[2]-b[0])*view.s,(b[3]-b[1])*view.s)}
 if(drag?.chunk&&drag.moved){
   const {chunk:c,dx,dy,incident}=drag;
   const rect=b=>[sx(b[0]+dx),sy(b[3]+dy),(b[2]-b[0])*view.s,(b[3]-b[1])*view.s];
   ctx.fillStyle='rgba(17,143,171,.25)';ctx.fillRect(...rect(c.box_dbu));ctx.strokeStyle=css('--io');ctx.lineWidth=3;ctx.strokeRect(...rect(c.box_dbu));
   ctx.lineWidth=1;for(const m of c.members)ctx.strokeRect(...rect(m.box_dbu));
   const peer=peerCache?.map.get($('geometryPeer').value);
   const lines=peer?peer.slice(0,32).map(([p,q])=>[p,[q[0],q[1],q[0],q[1]]]):incident.slice(0,32);
   ctx.setLineDash([4,4]);for(const [p,r] of lines){ctx.beginPath();ctx.moveTo(sx(p[0]+dx),sy(p[1]+dy));
     ctx.lineTo(sx((r[0]+r[2])/2),sy((r[1]+r[3])/2));ctx.stroke()}
 }
 ctx.restore();
}
let homePreview=null,homeControlsCache=null,homeLinkCache=null,dropPreview=null;
function discardDrop(){
 if(dropPreview&&homePreview===dropPreview.overlay){homePreview=null;dirty=true}
 dropPreview=null;$('dropActions').hidden=true;
}
$('dropDiscard').onclick=()=>{discardDrop();$('geometryInfo').textContent='Preview discarded. Live boxes and costs are unchanged.'};
$('dropApply').onclick=()=>{
 const d=dropPreview;
 if(!d)return;
 if(d.overlay!==homePreview||d.overlay.base!==chunkPlan||d.target!==(exploration||'main')){
   discardDrop();$('geometryInfo').textContent='The floorplan changed. Preview the drop again.';return;
 }
 geometryEdit(d.op,d.who,d.expect);
};
function planSummary(plan){
 const chunks=plan.chunks,route=plan.route_demand;
 let largest=0;for(const c of chunks)largest=Math.max(largest,c.estimated_bytes);
 const unique=new Set(chunks.map(c=>JSON.stringify([c.partition,c.implementation]))).size;
 const memory=largest>=1e9?(largest/1e9).toFixed(2)+' GB':(largest/1e6).toFixed(2)+' MB';
 return chunks.length+(chunks.length===1?' home · ':' homes · ')+unique+(unique===1?' implementation':' implementations')+
   ' · peak predicted '+memory+' · '+plan.communication.crossing_nets.toLocaleString()+
   ' crossing nets · pin HPWL '+num(plan.communication.pin_hpwl_dbu)+' DBU'+(route?
   ' · peak route demand H '+route.peak_horizontal.toFixed(1)+' / V '+route.peak_vertical.toFixed(1):'');
}
function homeControls(c){
 if(homeControlsCache?.plan===chunkPlan&&homeControlsCache.id===c.id)return;
 homeControlsCache={plan:chunkPlan,id:c.id};homePreview=null;
 const copy=c.interface_copy,choices=chunkPlan.chunks.filter(r=>r.id!==c.id&&!r.interface_copy);
 choices.sort((a,b)=>Number(b.partition===c.partition)-Number(a.partition===c.partition)||Math.abs(a.members.length-c.members.length)-Math.abs(b.members.length-c.members.length));
 $('copyTarget').textContent='Target: '+(homeLabels.get(c.id)||c.id);
 $('copySource').replaceChildren(...choices.map(r=>new Option(homeLabels.get(r.id)||r.id,r.id)));
 if(copy)$('copySource').value=copy.source;
 $('copyOrientation').value=copy?.requested||'auto';
 $('copyPreview').disabled=$('copyApply').disabled=!choices.length;$('copyClear').disabled=!copy;
 $('copyStatus').textContent=copy?copyResult(copy):'Local interface';
 resetCopyPins();
 const peers=$('homePeer'), members=$('homeMembers'), old=peers.value;
 let list=chunkPlan.chunks.filter(x=>x.id!==c.id);
 const b=c.box_dbu;
 $('homeAspect').value=(b[2]-b[0])/(b[3]-b[1]);
 function gap(x){const a=x.box_dbu;return Math.max(0,b[0]-a[2],a[0]-b[2])+Math.max(0,b[1]-a[3],a[1]-b[3])}
 list.sort((a,b)=>gap(a)-gap(b));
 peers.replaceChildren(...list.map(x=>new Option((homeLabels.get(x.id)||x.id)+(gap(x)===0?' (touches)':''),x.id)));
 if(list.some(x=>x.id===old))peers.value=old;
 members.replaceChildren(...c.members.map(m=>new Option((m.macro?'Macro ':'Color ')+m.id,JSON.stringify({id:m.id,macro:m.macro}))));
 let name=c.id+'.2',n=2;const ids=new Set(chunkPlan.chunks.map(c=>c.id));while(ids.has(name))name=c.id+'.'+(++n);$('homeName').value=name;
 $('homeMetrics').textContent='Live: '+planSummary(chunkPlan);
}
function clearHomePreview(){homePreview=null;dirty=true}
function homeAction(apply,undo=false){
 const c=geometryChunk();if(!c)return;
 let op='undo';
 if(!undo){
   const action=$('homeAction').value,other=$('homePeer').value;
   if(action!=='split'&&!other){$('homeStatus').textContent='Choose another home.';return}
   if(action==='fuse')op={fuse:[c.id,other]};
   else{
     const selected=Array.from($('homeMembers').selectedOptions,o=>JSON.parse(o.value));
     if(!selected.length||selected.length===c.members.length){$('homeStatus').textContent='Select some, but not all, of the members.';return}
     if(action==='transfer')op={transfer:c.id,to:other,members:selected};
     else{
       const name=$('homeName').value.trim();if(!name){$('homeStatus').textContent='Enter a new home ID.';return}
       op={split:c.id,new_id:name,members:selected};
     }
   }
 }
 geometryEdit(op,c.id,null,'topology',apply);
}
function drawHomePreview(){
 if(rev!==null||!chunkPlan||D?.phase!=='fp')return;
 const trial=homePreview?.base===chunkPlan?homePreview.plan:null,plan=trial||chunkPlan;
 const grid=plan.route_demand;
 ctx.save();
 const coarse=($('homeMap').value==='boundary'&&plan.boundary_routing)||plan.coarse_routing;
 if(coarse&&$('homeHeat').checked){
   const [l,b,r,t]=coarse.frame_dbu,n=coarse.columns,w=(r-l)/n,h=(t-b)/coarse.rows;
   const k=Number($('homeRouting').value)+Number($('homeDirection').value),peak=Math.max(...coarse.tiles.map(v=>v[k]));
   if(peak>0)coarse.tiles.forEach((v,i)=>{if(!v[k])return;ctx.fillStyle='rgba(216,85,25,'+(.7*Math.sqrt(v[k]/peak))+')';
     const x=i%n,y=Math.floor(i/n),xl=coarse.x_edges?.[x]??l+x*w,xh=coarse.x_edges?.[x+1]??xl+w;
     const yl=coarse.y_edges?.[y]??b+y*h,yh=coarse.y_edges?.[y+1]??yl+h;
     ctx.fillRect(sx(xl),sy(yh),(xh-xl)*view.s,(yh-yl)*view.s)});
 }else if(grid&&$('homeHeat').checked){
   const [l,b,r,t]=grid.frame_dbu,n=grid.bins,w=(r-l)/n,h=(t-b)/n,peak=Math.max(grid.peak_horizontal,grid.peak_vertical,chunkPlan.route_demand?.peak_horizontal||0,chunkPlan.route_demand?.peak_vertical||0);
   if(peak>0)grid.loads.forEach(([a,v],i)=>{const q=Math.max(a,v)/peak;if(!q)return;
     ctx.fillStyle='rgba(216,85,25,'+(.45*q)+')';ctx.fillRect(sx(l+i%n*w),sy(b+(Math.floor(i/n)+1)*h),w*view.s,h*view.s)});
 }
 if(trial){
   ctx.strokeStyle=css('--io');ctx.lineWidth=3;ctx.setLineDash([8,3]);const old=new Map(chunkPlan.chunks.map(c=>[c.id,c.box_dbu]));
   for(const c of trial.chunks){if(old.get(c.id)?.every((x,i)=>x===c.box_dbu[i]))continue;
     const [x,y,r,t]=c.box_dbu;ctx.strokeRect(sx(x),sy(t),(r-x)*view.s,(t-y)*view.s)}
 }
 if($('homeLinks').checked&&!drag?.chunk){
   const c=plan.chunks.find(c=>c.id===geomFocus);
   if(c){
     // Preview connectivity comes from the trial homes; a fused internal net disappears immediately.
     const peer=$('geometryPeer').value;
     if(homeLinkCache?.plan!==plan||homeLinkCache.id!==c.id||homeLinkCache.peer!==peer){
       const nets=new Map(c.ports.map(p=>[p.net_index,p.xy_dbu])),lines=[];
       for(const other of plan.chunks){if(other===c||(peer&&other.id!==peer))continue;
         for(const p of other.ports){const a=nets.get(p.net_index);if(a)lines.push([a,p.xy_dbu]);if(lines.length===96)break}
         if(lines.length===96)break}
       homeLinkCache={plan,id:c.id,peer,lines};
     }
     ctx.lineWidth=1;ctx.setLineDash([]);ctx.strokeStyle='rgba(34,103,163,.7)';
     for(const [a,b] of homeLinkCache.lines){ctx.beginPath();ctx.moveTo(sx(a[0]),sy(a[1]));ctx.lineTo(sx(b[0]),sy(b[1]));ctx.stroke()}
   }
 }
 ctx.restore();
}
$('homeAction').onchange=()=>{const action=$('homeAction').value;$('suggestTransfer').hidden=action!=='transfer';$('homeSplit').hidden=action==='fuse';$('homeNameField').hidden=action!=='split';$('homeFuse').hidden=action==='split';clearHomePreview()};
$('suggestTransfer').onclick=async()=>{
 const source=geometryChunk()?.id,peer=$('homePeer').value,status=$('homeStatus');
 if(!source||!peer)return;
 try{
   const result=await xcommand({cmd:'export',target:exploration||'main',what:'chunk_transfers',chunk:source,peer,limit:1});
   if(source!==geometryChunk()?.id||peer!==$('homePeer').value)return;
   const move=result.transfers?.[0];
   if(!move){status.textContent='No movable colors in this home.';return}
   clearHomePreview();
   $('homeMembers').value=JSON.stringify({id:move.id,macro:false});
   status.textContent=move.id+' · shared net change '+signed(move.pair_net_delta)+' · inter-color cut change '+signed(move.cut_net_delta);
 }catch(e){status.textContent=e.message}
};
for(const id of ['homeMembers','homePeer','homeName'])$(id).oninput=clearHomePreview;
$('homeMap').onchange=$('homeDirection').onchange=$('homeRouting').onchange=$('homeHeat').onchange=$('homeLinks').onchange=()=>{dirty=true};
$('homePreview').onclick=()=>homeAction(false);$('homeApply').onclick=()=>homeAction(true);
$('homeUndo').onclick=()=>homeAction(true,true);
async function geometryEdit(op,who,expect,field='geometry',apply=true){
 const topology=field==='topology',status=$(topology?'homeStatus':'geometryInfo');if(geomBusy)return;
 try{
   discardDrop();
   if(rev!==null||D?.phase!=='fp'||!chunkPlan||S.state!=='gate')throw Error('Edit at the live floorplan gate.');
   geomBusy=true;const target=exploration||'main',frame=expect?.frame??S.frame,before=chunkPlan.communication.pin_hpwl_dbu;
   const revision=expect?.intent??S.intent_revision;
   status.textContent='Refining chunk boxes, colors and pins…';
   const a=await xcommand({cmd:'chunk_edit',target,field,op,apply,expect:{frame,intent:revision}});
   for(;;){const r=await xcommand({cmd:'command_status',target,sequence:a.sequence});
     if(r.status!=='queued'){if(r.status!=='applied')throw Error(r.reason||r.status);break}await new Promise(resolve=>setTimeout(resolve,60))}
   const r=await xcommand({cmd:'export',target,what:'intent'});
   if(r.sequence!==a.sequence)throw Error('A newer edit replaced this report.');
   if(!r.bindings?.chunks)throw Error(r.status||'Move rejected.');
   const plan=r.bindings;
   if(topology){
     const delta=plan.communication.pin_hpwl_dbu-before;
     if(apply){homePreview=null;showChunks(plan);const c=plan.chunks.find(c=>c.id===who);if(c)focusGeometry(c)}
     else homePreview={base:chunkPlan,plan};
     status.textContent=(apply?'Applied: ':'Preview: ')+planSummary(plan)+'; wire change '+signed(delta)+' DBU.';return;
   }
   const c=plan.chunks.find(c=>c.id===who);
   if(apply){showChunks(plan);if(c)focusGeometry(c)}
   else{
     homePreview={base:chunkPlan,plan};
     dropPreview={op,who,expect:{frame,intent:revision},target,overlay:homePreview};
     $('dropActions').hidden=false;dirty=true;
   }
   const landed=c?.box_dbu;
   const at=v=>'('+v.slice(0,2).map(num).join(', ')+')';
   const msg=c&&op.reshape?'Width / height '+((landed[2]-landed[0])/(landed[3]-landed[1])).toFixed(4)+'. ':c&&op.move?(apply?'Placed at ':'Preview only: would land at ')+at(landed)+' DBU'+(landed[0]!==op.to_dbu[0]||landed[1]!==op.to_dbu[1]?'; requested '+at(op.to_dbu)+' DBU':'')+'; longest incident span '+num(c.longest_net_span_dbu)+' DBU. ':'';
   const delta=plan.communication.pin_hpwl_dbu-before;
   const change=before&&delta?' · '+(Math.abs(delta)/before<.0001?'<0.01':(100*Math.abs(delta)/before).toFixed(2))+'% '+(delta>0?'longer':'shorter'):'';
   status.textContent=msg+'Refined design HPWL '+num(plan.communication.pin_hpwl_dbu)+' DBU ('+signed(delta)+change+'). Shared area '+num(plan.geometry?.shared_dbu2||0)+' DBU²; cycles '+(plan.geometry?.independent_cycles||0)+'.';
 }catch(e){status.textContent=e.message}finally{geomBusy=false;geometryPair(0,0);dirty=true}
}
function editAspect(apply){
 const c=geometryChunk(),aspect=$('homeAspect').valueAsNumber;
 if(!c||!(aspect>0)){$('geometryInfo').textContent='Select a chunk and enter a positive width / height.';return}
 geometryEdit({reshape:c.id,aspect_ratio:aspect},c.id,null,'geometry',apply);
}
$('aspectPreview').onclick=()=>editAspect(false);$('aspectApply').onclick=()=>editAspect(true);
function editStep(apply){
 const c=geometryChunk(),peer=$('geometryPeer').value,depth=$('boundaryDepth').valueAsNumber;
 if(!c||!peer||!Number.isFinite(depth)){$('geometryInfo').textContent='Select adjacent chunks and enter a signed depth.';return}
 geometryEdit({step:c.id,peer,depth_dbu:depth*D.dbu},c.id,null,'geometry',apply);
}
$('stepPreview').onclick=()=>editStep(false);$('stepApply').onclick=()=>editStep(true);
function geometryLock(what){
 const c=geometryChunk();if(!c){$('geometryInfo').textContent='Select a physical chunk first.';return}
 if(what==='others'){geometryEdit({lock_others:c.id},c.id);return}
 const ids=what==='one'?[c.id]:chunkPlan.chunks.filter(x=>what==='unlock'||x.id!==c.id).map(x=>x.id);
 if(!ids.length)return;geometryEdit({lock_aspect:what==='one'?!c.aspect_locked:what==='others',chunks:ids},c.id);
}
$('geometryLock').onclick=()=>geometryLock('one');$('geometryOthers').onclick=()=>geometryLock('others');
$('geometryUnlock').onclick=()=>geometryLock('unlock');$('geometryUndo').onclick=()=>geometryEdit('undo',geometryChunk()?.id);
async function editChunk(clear){
 const status=$('chunkStatus');try{
   if(rev!==null||D?.phase!=='fp'||!D?.chunk_plan)throw Error('Edit at the live floorplan gate.');
   const [kind,selector]=JSON.parse($('chunkSelect').value),density=$('chunkDensity').valueAsNumber;
   if(!clear&&!(density>0&&density<=1))throw Error('Density must be greater than 0 and at most 1.');
   const target=exploration||'main',frame=S.frame;
   const old=await xcommand({cmd:'export',target,what:'intent',encoding:'json_text'});
   // Only typed density records cross the browser number parser. The server retains all other records losslessly.
   const records=JSON.parse(old.records_json).filter(r=>r.class==='livehd.placement.chunk_guidance'&&(!r.design||r.design===chunkPlan.design));
   for(const r of records)r.targets=(r.targets||[]).filter(t=>!(t.kind===kind&&t.selector===selector));
   if(!clear){let r=records.find(r=>r.id==='gui'&&r.design===chunkPlan.design);
     if(!r){r={class:'livehd.placement.chunk_guidance',design:chunkPlan.design,id:'gui',targets:[]};records.push(r)}
     (r.targets??=[]).push({kind,selector,density});}
   const a=await xcommand({cmd:'intent_apply',target,expect:{frame,intent:old.revision},records_json:JSON.stringify(records)});
   for(;;){const receipt=await xcommand({cmd:'command_status',target,sequence:a.sequence});
     if(receipt.status!=='queued'){if(receipt.status!=='applied')throw Error(receipt.reason||receipt.status);break}
     await new Promise(resolve=>setTimeout(resolve,80));}
   const result=await xcommand({cmd:'export',target,what:'intent'});
   if(result.sequence!==a.sequence)throw Error('A newer edit replaced this report. Inspect again.');
   if(!result.bindings?.chunks)throw Error(result.status||'The planner rejected the edit.');
   showChunks(result.bindings);
   status.textContent='Floorplan updated.';
 }catch(e){status.textContent=e.message}
}
function copyResult(r){
 return r.orientation+' from '+(homeLabels.get(r.source)||r.source)+' · '+r.matched+'/'+r.target_ports+' target contacts matched · '+(r.target_ports-r.matched)+' kept local';
}
let copyOffset=0,copyPinPage=0;
async function showCopyPins(){
 const epoch=++copyPinPage,details=$('copyContacts');
 if(!details.open)return;
 const target=geometryChunk()?.id,ctx=chunkPlan,trial=homePreview,view=trial?.base===ctx?'preview':'live';
 const list=$('copyPins');list.replaceChildren();$('copyRange').textContent='Reading contacts…';$('copyPrev').disabled=$('copyNext').disabled=true;
 try{
   if(!target)throw Error('Select a physical home.');
   const r=await xcommand({cmd:'export',target:exploration||'main',what:'chunk_pins',chunk:target,view,match:$('copyFilter').value,offset:copyOffset,limit:8});
   if(epoch!==copyPinPage||ctx!==chunkPlan||trial!==homePreview||target!==geometryChunk()?.id)return;
   list.start=copyOffset+1;
   for(const p of r.pins){const li=document.createElement('li');li.textContent=p.net+' at ('+p.xy_dbu.join(', ')+') DBU'+
      (p.source_net===undefined?' · local':' ← '+p.source_net+' at ('+p.source_xy_dbu.join(', ')+') DBU');list.append(li)}
   $('copyRange').textContent=(view==='preview'?'Preview':'Live')+' · '+(r.total===0?'no contacts':(copyOffset+1)+'–'+(copyOffset+r.pins.length)+' of '+r.total);
   $('copyPrev').disabled=copyOffset===0;$('copyNext').disabled=copyOffset+r.pins.length>=r.total;
 }catch(e){if(epoch===copyPinPage)$('copyRange').textContent=e.message}
}
function resetCopyPins(){copyOffset=0;showCopyPins()}
$('copyContacts').ontoggle=resetCopyPins;$('copyFilter').onchange=resetCopyPins;
$('copyPrev').onclick=()=>{copyOffset=Math.max(0,copyOffset-8);showCopyPins()};
$('copyNext').onclick=()=>{copyOffset+=8;showCopyPins()};
async function editCopy(apply,clear=false){
 const status=$('copyStatus');
 try{
   const c=geometryChunk(),target=exploration||'main',frame=S.frame;
   if(!c||rev!==null||D?.phase!=='fp'||S.state!=='gate')throw Error('Select a home at the live floorplan gate.');
   const source=$('copySource').value,orientation=clear?'clear':$('copyOrientation').value,before=chunkPlan.communication.pin_hpwl_dbu;
   const old=await xcommand({cmd:'export',what:'chunks',target,chunk:c.id});
   const receipt=await xcommand({cmd:'chunk_interface',target,chunk:c.id,source,orientation,apply,expect:{frame,intent:old.intent_revision}});
   status.textContent='Matching pins…';
   for(;;){const s=await xcommand({cmd:'command_status',target,sequence:receipt.sequence});if(s.status!=='queued'){if(s.status!=='applied')throw Error(s.reason||s.status);break}await new Promise(r=>setTimeout(r,80))}
   const r=await xcommand({cmd:'export',target,what:'intent'});
   if(r.sequence!==receipt.sequence||!r.bindings?.chunks)throw Error('The displayed floorplan changed; inspect again.');
   const plan=r.bindings,changed=plan.chunks.find(x=>x.id===c.id);
   if(apply){showChunks(plan);focusGeometry(changed)}else{homePreview={base:chunkPlan,plan};dirty=true}
   resetCopyPins();
   status.textContent=(apply?'Applied: ':'Preview: ')+(changed.interface_copy?copyResult(changed.interface_copy):'local interface')+
     '; crossing HPWL '+num(plan.communication.pin_hpwl_dbu)+' DBU ('+signed(plan.communication.pin_hpwl_dbu-before)+').';
 }catch(e){status.textContent=e.message}
}
$('copyPreview').onclick=()=>editCopy(false);$('copyApply').onclick=()=>editCopy(true);$('copyClear').onclick=()=>editCopy(true,true);
const implSelected=()=>new Set(Array.from($('implCopies').selectedOptions,o=>o.value));
let implGroups=[],implEpoch=0;
function formPins(){
 const group=implGroups.find(g=>g.name===$('implName').value), pin=group?.pins?.find(p=>p.role===$('implRole').value);
 $('implSide').value=pin?.side||'';$('implOffset').value=pin?.offset??.5;
}
function commonRoles(){
 const selected=implSelected(), rows=chunkPlan.chunks.filter(c=>selected.has(c.id));
 const sets=rows.map(c=>new Set(c.ports.map(p=>p.role).filter(Boolean)));
 const roles=sets.length?[...sets[0]].filter(r=>sets.every(set=>set.has(r))):[];
 const old=$('implRole').value;
 $('implRole').replaceChildren(new Option('Choose a common pin',''),...roles.sort().map(r=>new Option(r,r)));
 if(roles.includes(old))$('implRole').value=old;formPins();dirty=true;
}
async function showImplementations(focus){
 const family=$('implFamily').value, rows=chunkPlan.chunks.filter(c=>c.partition===family);
 const sub=$('implCopies'), old=implSelected();
 sub.replaceChildren(...rows.map(c=>{const o=new Option((c.instances?.join(', ')||c.id)+' → '+(c.implementation||'shared')+' · '+((c.box_dbu[2]-c.box_dbu[0])/(c.box_dbu[3]-c.box_dbu[1])).toFixed(2)+':1',c.id);o.title=c.id;o.selected=focus?focus===c.id:old.has(c.id);return o}));
 $('implSummary').textContent=rows.length+' copies · '+new Set(rows.map(c=>c.implementation)).size+' implementations'+(chunkPlan.communication?
    ' · design pin HPWL '+chunkPlan.communication.pin_hpwl_dbu.toLocaleString()+' DBU':'');
 $('implName').value='';$('implAspect').value='';implGroups=[];commonRoles();
 const seq=++implEpoch, existing=$('implExisting');
 existing.disabled=false;const effective=[...new Set(rows.map(c=>c.implementation).filter(Boolean))];
 existing.replaceChildren(new Option('Select copies…',''),...effective.map(n=>new Option(n,n)));
 if(rev!==null||!family||D?.phase!=='fp')return;
 existing.disabled=true;try{const r=await xcommand({cmd:'export',target:exploration||'main',what:'intent',encoding:'json_text'});
   if(seq===implEpoch)implGroups=JSON.parse(r.records_json).filter(r=>r.class==='livehd.placement.chunk_guidance'&&(!r.design||r.design===chunkPlan.design))
     .flatMap(r=>r.implementations||[]).find(r=>r.partition===family)?.groups||[];
 }catch(e){if(seq===implEpoch)$('chunkStatus').textContent=e.message}
 finally{if(seq===implEpoch)existing.disabled=false}
}
$('implFamily').onchange=()=>showImplementations();
$('implCopies').onchange=commonRoles;$('implRole').onchange=formPins;
$('implExisting').onchange=()=>{
 const n=$('implExisting').value, g=implGroups.find(g=>g.name===n);
 const ids=new Set(chunkPlan.chunks.filter(c=>c.partition===$('implFamily').value&&c.implementation===n).map(c=>c.id));
 for(const o of $('implCopies').options)o.selected=ids.has(o.value);
 $('implName').value=g?.name||'';$('implAspect').value=g?.aspect_ratio??'';commonRoles();
};
$('implAction').onchange=()=>{$('implEditor').hidden=$('implAction').value!=='group'};
async function editImplementation(apply){
 const status=$('chunkStatus');try{
   if(rev!==null||D?.phase!=='fp')throw Error('Edit at the live floorplan gate.');
   const target=exploration||'main',frame=S.frame,family=$('implFamily').value,action=$('implAction').value,before=chunkPlan.communication?.pin_hpwl_dbu;
   const old=await xcommand({cmd:'export',target,what:'intent',encoding:'json_text'});
   const records=JSON.parse(old.records_json).filter(r=>r.class==='livehd.placement.chunk_guidance'&&(!r.design||r.design===chunkPlan.design));
   let record=records.find(r=>r.implementations?.some(i=>i.partition===family));
   if(!record){record=records.find(r=>r.id==='gui'&&r.design===chunkPlan.design);
     if(!record){record={class:'livehd.placement.chunk_guidance',id:'gui',design:chunkPlan.design};records.push(record)}}
   const policies=record.implementations??=[];let rule=policies.find(r=>r.partition===family);
   if(!rule){rule={partition:family,mode:'auto',groups:[]};policies.push(rule)}
   if(action==='group'){
     const ids=implSelected(),name=$('implName').value.trim(), aspect=$('implAspect').valueAsNumber;
     if(!ids.size||!name)throw Error('Select the copies and enter a group name.');
     if($('implAspect').value&&!(aspect>0))throw Error('Target aspect ratio must be positive.');
     const groups=rule.groups??=[];let g=groups.find(g=>g.name===name);
     if(!g){g={name,instances:[]};groups.push(g)}
     for(const other of groups)if(other!==g)other.instances=other.instances.filter(id=>!ids.has(id));
     g.instances=[...new Set([...g.instances,...ids])].sort();rule.groups=groups.filter(g=>g.instances.length);
     if(aspect>0)g.aspect_ratio=aspect;else if($('implExisting').value===name)delete g.aspect_ratio;
     const role=$('implRole').value,side=$('implSide').value,offset=$('implOffset').valueAsNumber;
     if(role){g.pins=(g.pins||[]).filter(p=>p.role!==role);if(side){if(!(offset>=0&&offset<=1))throw Error('Pin offset must be between 0 and 1.');g.pins.push({role,side,offset})}}
   }else{rule.mode=action;rule.groups=[]}
   status.textContent=apply?'Applying…':'Comparing…';
   const accepted=await xcommand({cmd:apply?'intent_apply':'intent_preview',target,expect:{frame,intent:old.revision},records_json:JSON.stringify(records)});
   for(;;){const r=await xcommand({cmd:'command_status',target,sequence:accepted.sequence});
     if(r.status!=='queued'){if(r.status!=='applied')throw Error(r.reason||r.status);break}await new Promise(resolve=>setTimeout(resolve,80))}
   const r=await xcommand({cmd:'export',target,what:'intent'});
   if(r.sequence!==accepted.sequence)throw Error('A newer edit replaced this report. Inspect again.');
   if(!r.bindings?.chunks)throw Error(r.status||'The planner rejected the edit.');
   const next=r.bindings, after=next.communication.pin_hpwl_dbu;
   const count=new Set(next.chunks.filter(c=>c.partition===family).map(c=>c.implementation)).size;
   if(apply){showChunks(next);$('implFamily').value=family;showImplementations()}else{homePreview={base:chunkPlan,plan:next};dirty=true}
   status.textContent=(apply?'Applied':'Preview')+': '+count+' implementations; design pin HPWL '+after.toLocaleString()+' DBU'+
      (before===undefined?'':' ('+(after-before>=0?'+':'')+(after-before).toLocaleString()+' DBU)')+(next.route_demand?'; peak route demand H '+next.route_demand.peak_horizontal.toFixed(1)+' / V '+next.route_demand.peak_vertical.toFixed(1):'')+'.';
 }catch(e){status.textContent=e.message}
}
$('implPreview').onclick=()=>editImplementation(false);$('implApply').onclick=()=>editImplementation(true);
$('chunkApply').onclick=()=>editChunk(false);$('chunkClear').onclick=()=>editChunk(true);
$('chunkSave').onclick=async()=>{try{
 const what=D?.phase==='fp'?'floorplan':'macros';
 const r=await xcommand({cmd:'export',target:exploration||'main',what,format:'metadata',encoding:'json_text'});
 const url=URL.createObjectURL(new Blob([r.records_json+'\n'],{type:'application/json'}));
 const a=document.createElement('a');a.href=url;a.download='placement-guidance.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}catch(e){$('chunkStatus').textContent=e.message}};
let intentOverlay=null,intentRequest=0;
function clearIntentOverlay(){
 intentOverlay=null;$('ioverlayControl').hidden=true;$('ilegend').textContent='';dirty=true;
}
function displayIntent(r){
 clearIntentOverlay();
 const hints=r.bindings?.hints||[],targets=r.bindings?.targets_dbu||[];
 const summary=h=>h.id+' · '+h.kind+' · '+h.status+' · weight '+h.weight+
   (h.status==='discarded'?'':' · residual '+h.residual_um.toFixed(3)+' µm')+
   (h.reason?' · '+h.reason:'')+'\n'+h.subjects.join(', ');
 $('ibind').textContent=[r.status,...hints.map(summary)].filter(Boolean).join('\n');
 if(r.frame && r.frame!==S.frame){$('ilegend').textContent='Measured at frame '+r.frame;return}
 const edges=hints.filter(h=>h.status!=='discarded'&&h.pin_facing_edge_dbu).map(h=>h.pin_facing_edge_dbu);
 if(!targets.length&&!edges.length)return;
 intentOverlay={frame:r.frame,epoch:S.epoch,targets,edges};
 $('ioverlayControl').hidden=false;
 $('ilegend').textContent=[targets.length?targets.length+' dashed target outlines':'',edges.length?edges.length+' requested pin-facing edges':''].filter(Boolean).join(' · ');
}
function drawIntentOverlay(){
 if(!intentOverlay||!$('ioverlay').checked||rev!==null||intentOverlay.frame!==haveFrame||intentOverlay.epoch!==S.epoch)return;
 ctx.save();ctx.strokeStyle=css('--acc');ctx.lineWidth=2;
 for(const t of intentOverlay.targets){
   const [x,y,w,h]=t.target_center_size,[cx,cy]=t.from_center;
   ctx.setLineDash([]);ctx.beginPath();ctx.moveTo(sx(cx),sy(cy));ctx.lineTo(sx(x),sy(y));ctx.stroke();
   ctx.setLineDash([6,4]);ctx.strokeRect(sx(x-w/2),sy(y+h/2),w*view.s,h*view.s);
 }
 ctx.strokeStyle=css('--io');ctx.lineWidth=4;ctx.setLineDash([]);ctx.beginPath();
 for(const [x,y,u,v] of intentOverlay.edges){ctx.moveTo(sx(x),sy(y));ctx.lineTo(sx(u),sy(v))}
 ctx.stroke();ctx.restore();
}
$('ioverlay').onchange=()=>{dirty=true};
$('irecords').oninput=()=>{++intentRequest;clearIntentOverlay();$('ibind').textContent=''};
$('iread').onclick=async()=>{
 const request=++intentRequest;
 try{
   const r=await xcommand({cmd:'export',target:exploration||'main',what:'intent',encoding:'json_text'});
   if(request===intentRequest){$('irecords').value=r.records_json;displayIntent(r)}
 }catch(e){if(request===intentRequest)$('ibind').textContent=e.message}
};
for(const [button,action] of [['ipreview','intent_preview'],['iapply','intent_apply']]){
 $(button).onclick=async()=>{
   const request=++intentRequest,target=exploration||'main',epoch=S.epoch;
   clearIntentOverlay();
   try{
     const accepted=await xcommand({cmd:action,target,expect:{frame:S.frame,intent:S.intent_revision},records_json:$('irecords').value});
     if(request!==intentRequest)return;
     $('ibind').textContent='Queued';
     for(;;){
       const receipt=await xcommand({cmd:'command_status',target,sequence:accepted.sequence});
       if(request!==intentRequest||S.epoch!==epoch)return;
       if(receipt.status!=='queued'){
         if(receipt.status!=='applied')throw Error(receipt.reason||receipt.status);
         break;
       }
       await new Promise(resolve=>setTimeout(resolve,100));
     }
     const r=await xcommand({cmd:'export',target,what:'intent'});
     if(request===intentRequest && S.epoch===epoch){
       if(r.sequence===accepted.sequence)displayIntent(r);
       else $('ibind').textContent='A newer intent report is available. Use Inspect to view it.';
     }
   }catch(e){if(request===intentRequest)$('ibind').textContent=e.message}
 }
}
$('legalize').onclick=()=>cmd({cmd:'legalize'});
$('xbegin').onclick=async()=>{
 const button=$('xbegin');button.disabled=true;
 try{
  const x=await xcommand({cmd:'explore_begin',expect:{frame:S.frame}});
  openingForeground=x.id;$('xstatus').textContent='Opening '+x.id+'…';
  await explorations();
 }catch(e){$('xstatus').textContent=e.message;button.disabled=false}
};
if(exploration){
 $('xbegin').hidden=true;$('xclose').hidden=false;$('xclose').onclick=()=>discardCandidate(exploration,$('xclose'));
}
explorations().catch(e=>{$('xstatus').textContent=e.message});
setInterval(async()=>{if(ledgerPolling)return;ledgerPolling=true;try{await explorations()}catch{}finally{ledgerPolling=false}},2000);
let aEdge='right',aStepSet=false,aPend=null,aBase='';
const areaBox=()=>aPend||D.die;
function areaCancel(){if(aPend){aPend=null;dirty=true;areaInfo()}}
function areaSet(edge,v){if(!aPend){aPend=D.die.slice();aBase=D.die.join()}const m=2*D.dbu;if(edge==='left')aPend[0]=Math.min(v,aPend[2]-m);else if(edge==='right')aPend[2]=Math.max(v,aPend[0]+m);else if(edge==='bottom')aPend[1]=Math.min(v,aPend[3]-m);else aPend[3]=Math.max(v,aPend[1]+m);aEdge=edge;dirty=true;areaInfo()}
function areaNudge(sign){const st=parseFloat($('a_step').value);if(!(st>0)){$('msg').textContent='area: give a step in um';return}const b=areaBox(),d=sign*st*D.dbu;
 areaSet(aEdge,aEdge==='left'?b[0]-d:aEdge==='right'?b[2]+d:aEdge==='bottom'?b[1]-d:b[3]+d)}
function usableArea(box){
 if(box===D.die)return D.usable_area;
 const [xl,yl,xh,yh]=box;
 if(!D.exclusions?.length)return (xh-xl)*(yh-yl);
 const rects=D.exclusions.map(([a,b,c,d])=>[Math.max(a,xl),Math.max(b,yl),Math.min(c,xh),Math.min(d,yh)]).filter(([a,b,c,d])=>a<c&&b<d);
 if(!rects.length)return (xh-xl)*(yh-yl);
 const xs=[...new Set([xl,xh,...rects.flatMap(r=>[r[0],r[2]])])].sort((a,b)=>a-b);let area=0;
 for(let i=1;i<xs.length;i++){let y=yl;const spans=rects.filter(r=>r[0]<xs[i]&&r[2]>xs[i-1]).sort((a,b)=>a[1]-b[1]);
  for(const r of spans){area+=(xs[i]-xs[i-1])*Math.max(0,r[1]-y);y=Math.max(y,r[3])}area+=(xs[i]-xs[i-1])*(yh-y)}
 return area}
function areaProblem(){if(!aPend)return'';const[xl,yl,xh,yh]=aPend,area=usableArea(aPend);if(area<=0)return'no usable placement area';if(D.cell_area>0&&D.cell_area/area>0.98)return'the cells would fill '+fmt(100*D.cell_area/area,0)+' % of it (limit 98 %)';if(D.macro_area>0&&D.macro_limit>0&&D.macro_area/area>D.macro_limit)return'the macros and memories would need '+fmt(100*D.macro_area/area,0)+' % of it with their spacing (limit '+fmt(100*D.macro_limit,0)+' %)';
 if(X)for(const i of pinned){if(i<X.length&&(X[i]<xl-0.5||Y[i]<yl-0.5||X[i]+D.w[i]>xh+0.5||Y[i]+D.h[i]>yh+0.5))return'a pinned node would lie outside (move or unpin it)'}return''}
function areaApply(){if(!aPend)return;const bad=areaProblem();if(bad){$('msg').textContent='area: '+bad;return}const box=aPend.map(v=>v/D.dbu);aPend=null;dirty=true;cmd({cmd:'area',box});$('msg').textContent='re-placing with the new area...';areaInfo()}
function areaInfo(){if(!D||!D.die||!D.dbu)return;if(aPend&&aBase!==D.die.join())aPend=null;const[xl,yl,xh,yh]=D.die,w=(xh-xl)/D.dbu,h=(yh-yl)/D.dbu,fill=b=>D.cell_area>0&&usableArea(b)>0?fmt(100*D.cell_area/usableArea(b),1)+' %':'–';
 $('a_size').textContent=fmt(w,1)+' x '+fmt(h,1);
 if(aPend){const nw=(aPend[2]-aPend[0])/D.dbu,nh=(aPend[3]-aPend[1])/D.dbu;$('a_new').textContent=fmt(nw,1)+' x '+fmt(nh,1)+' ('+fmt(100*nw*nh/(w*h)-100,0)+' %)';$('a_fill').textContent=fill(D.die)+' -> '+fill(aPend)}
 else{$('a_new').textContent='-';$('a_fill').textContent=fill(D.die)}
 const bad=areaProblem();$('a_warn').textContent=bad?'Cannot apply: '+bad:'';$('a_apply').disabled=!aPend||!!bad;$('a_cancel').disabled=!aPend;
 if(!aStepSet&&document.activeElement!==$('a_step'))$('a_step').value=fmt((aEdge==='left'||aEdge==='right'?w:h)*0.05,1).replace(/,/g,'');
 for(const b of document.querySelectorAll('#a_edges button'))b.className=b.dataset.e===aEdge?'primary':''}
// the previewed area on the canvas: what leaves (red), what is added (green), the new outline (dashed) with a handle per edge
function areaOverlay(){if(!D||!D.die||rev!==null)return;const b=areaBox(),[dx0,dy0,dx1,dy1]=D.die;
 if(aPend){const[nx0,ny0,nx1,ny1]=aPend;ctx.save();
  const rect=(x0,y0,x1,y1,c)=>{if(x1>x0&&y1>y0){ctx.fillStyle=c;ctx.fillRect(sx(x0),sy(y1),(x1-x0)*view.s,(y1-y0)*view.s)}};
  const ix0=Math.max(dx0,nx0),iy0=Math.max(dy0,ny0),ix1=Math.min(dx1,nx1),iy1=Math.min(dy1,ny1);
  rect(dx0,dy0,ix0,dy1,'#d1242f44');rect(ix1,dy0,dx1,dy1,'#d1242f44');rect(ix0,dy0,ix1,iy0,'#d1242f44');rect(ix0,iy1,ix1,dy1,'#d1242f44');
  rect(nx0,ny0,dx0,ny1,'#1f9d5544');rect(dx1,ny0,nx1,ny1,'#1f9d5544');rect(Math.max(nx0,dx0),ny0,Math.min(nx1,dx1),dy0,'#1f9d5544');rect(Math.max(nx0,dx0),dy1,Math.min(nx1,dx1),ny1,'#1f9d5544');
  ctx.strokeStyle=css('--acc');ctx.lineWidth=2;ctx.setLineDash([7,4]);ctx.strokeRect(sx(nx0),sy(ny1),(nx1-nx0)*view.s,(ny1-ny0)*view.s);ctx.setLineDash([]);ctx.restore()}
 ctx.save();ctx.fillStyle=css('--acc');const hw=5,mx0=(b[0]+b[2])/2,my0=(b[1]+b[3])/2;
 for(const[e,x,y,wd,ht]of[['left',sx(b[0]),sy(my0),hw,18],['right',sx(b[2]),sy(my0),hw,18],['bottom',sx(mx0),sy(b[1]),18,hw],['top',sx(mx0),sy(b[3]),18,hw]]){ctx.globalAlpha=e===aEdge?1:.55;ctx.fillRect(x-wd/2,y-ht/2,wd,ht)}ctx.restore()}
function hitEdge(px,py){if(!D||!D.die||rev!==null)return null;const b=areaBox(),tol=6,l=sx(b[0]),r=sx(b[2]),t=sy(b[3]),u=sy(b[1]);let best=null,bd=tol+1;
 const test=(e,d,inside)=>{if(inside&&d<bd){bd=d;best=e}};test('left',Math.abs(px-l),py>=t-tol&&py<=u+tol);test('right',Math.abs(px-r),py>=t-tol&&py<=u+tol);test('bottom',Math.abs(py-u),px>=l-tol&&px<=r+tol);test('top',Math.abs(py-t),px>=l-tol&&px<=r+tol);return best}
function forkBox(box){if(!D||!D.dbu)return;cmd({cmd:'fork',box:box.map(v=>v/D.dbu)});$('msg').textContent='forking the region...'}
function forkSelection(){if(!sel.size||!X){$('msg').textContent='select cells first (shift+drag selects a box)';return}let a=1e300,b=1e300,c=-1e300,d=-1e300;for(const i of sel){const k=D.kinds.charCodeAt(i)-48;if(k===3||k===4||k===8||k===9)continue;a=Math.min(a,X[i]);b=Math.min(b,Y[i]);c=Math.max(c,X[i]+D.w[i]);d=Math.max(d,Y[i]+D.h[i])}if(a>c){$('msg').textContent='the selection holds no cells';return}forkBox([a,b,c,d])}
function forkView(){if(!D)return;const W=cv.clientWidth,H=cv.clientHeight,[xl,yl,xh,yh]=D.die;forkBox([Math.max(xl,wx(0)),Math.max(yl,wy(H)),Math.min(xh,wx(W)),Math.min(yh,wy(0))])}
let forkSeen=-1;
function forkList(){const f=S.forks||[],el=$('f_list');if(el.dataset.n==String(f.length))return;el.dataset.n=String(f.length);el.textContent='';f.forEach((k,i)=>{const a=document.createElement('a');a.href=k.url;a.target='_blank';a.rel='noopener';a.textContent=k.top+': '+k.cells+' cells, '+k.inputs+' in / '+k.outputs+' out - open';a.style.display='block';el.appendChild(a)});if(forkSeen>=0&&f.length>forkSeen){const k=f[f.length-1];try{window.open(k.url,'_blank','noopener')}catch(e){}}forkSeen=f.length}
const resultView=()=>!!exploration&&S.state==='done';
async function cmd(o){
 if(resultView())return;
 try{return await xcommand({cmd:'cmd',target:exploration||'main',command:o})}
 catch(e){$('msg').textContent=e.message}
}
const lComb=$('l_comb'),lRegs=$('l_regs'),cellOn=i=>isSeq(i)?lRegs.checked:lComb.checked,isSeq=i=>!!D.seq&&D.seq.charCodeAt(i)===49,kind=i=>D.kinds.charCodeAt(i)-48,isMovable=i=>kind(i)<5,isFixedMacro=i=>kind(i)===7,canMove=i=>isMovable(i)||isFixedMacro(i),kd=i=>kind(i)%5;
const nm=i=>names[i]??('#'+i);
function resize(){dpr=window.devicePixelRatio||1;cv.width=cv.clientWidth*dpr;cv.height=cv.clientHeight*dpr;dirty=true}
window.addEventListener('resize',resize);resize();
function fit(){if(!D)return;const[xl,yl,xh,yh]=D.die,w=cv.clientWidth,h=cv.clientHeight,pad=40;
 const pw=$('panel').style.display==='none'?0:290,aw=w-pw,s=Math.min((aw-2*pad)/Math.max(1,xh-xl),(h-2*pad)/Math.max(1,yh-yl));view.s=s;view.ox=(aw-(xh-xl)*s)/2-xl*s;view.oy=(h+(yh-yl)*s)/2+yl*s;dirty=true}
const sx=x=>x*view.s+view.ox,sy=y=>view.oy-y*view.s,wx=x=>(x-view.ox)/view.s,wy=y=>(view.oy-y)/view.s;
let R=null,regV=-1,M=null,mapV=-1,C=null,epoch=null,CP=[],selCp=-1,rev=null,flagDefs=null,flagEdits={},cpKey="";
let stripReport=null,stripEpoch=0;
function showMacroFaces(){
 const out=$('faceSummary');out.replaceChildren();
 const m=stripReport?.macros[$('faceMacro').selectedIndex];
 if(!m)return;
 for(const [side,f] of Object.entries(m.pin_access_proxy)){
  const line=document.createElement('div'),unit=side==='bottom'||side==='top'?'row heights':'site widths';
  line.textContent=side+': '+f.pins+' signal pins · '+(f.edge_pitch_capacity==null?'no full outline edge':('at most '+f.edge_pitch_capacity+' '+unit));
  out.append(line);
 }
}
$('faceMacro').onchange=showMacroFaces;
function selectStrip(){
 const edge=$('edgeClearance');edge.textContent='';
 if(stripReport&&D){
  const macro=stripReport.macros.reduce((best,m)=>m.edge_halo_clearance_dbu!=null&&(!best||m.edge_halo_clearance_dbu<best.edge_halo_clearance_dbu)?m:best,null);
  if(macro)edge.textContent='Smallest halo-to-edge gap: '+macro.name+' · '+macro.halo_boundary_side+' · '+fmt(macro.edge_halo_clearance_dbu/D.dbu,3)+' µm'+(macro.edge_pitch_capacity==null?'':' · at most '+macro.edge_pitch_capacity+(['bottom','top'].includes(macro.halo_boundary_side)?' row heights':' site widths'));
 }
 const strip=stripReport?.strips[$('stripSelect').selectedIndex];
 const detail=$('stripDetail');detail.replaceChildren();dirty=true;
 if(!strip||!D)return;
 const area=strip.standard_cell_area_dbu2/(D.dbu*D.dbu);
 detail.textContent=strip.standard_cells+' cells · '+fmt(area,2)+' µm² · '+fmt(100*strip.standard_cell_utilization,1)+'% occupied · '+strip.entrances.length+' open entrances';
}
$('stripRead').onclick=async()=>{
 const generation=++stripEpoch;
 try{
  const report=await xcommand({cmd:'geometry',target:exploration||'main'});
  if(generation!==stripEpoch)return;
  const oldName=stripReport?.macros[$('faceMacro').selectedIndex]?.name;
  stripReport=report;
  const faceSelect=$('faceMacro');
  faceSelect.replaceChildren(...report.macros.map(m=>new Option(m.name,m.name)));
  const chosen=report.macros.findIndex(m=>m.name===oldName);
  if(chosen>=0)faceSelect.selectedIndex=chosen;
  $('faceSelector').hidden=!report.macros.length;showMacroFaces();
  const select=$('stripSelect');
  select.replaceChildren(...report.strips.map((s,i)=>new Option(s.members.join(' ↔ '),i)));
  select.hidden=!report.strips.length;
  $('stripStatus').textContent='Frame '+report.frame+' · '+report.strips.length+' channels';
  selectStrip();
 }catch(e){if(generation===stripEpoch)$('stripStatus').textContent=e.message}
};
$('stripSelect').onchange=selectStrip;
$('stripInspector').ontoggle=()=>{dirty=true};
function drawSelectedStrip(){
 if(!$('stripInspector').open||!stripReport||rev!==null||stripReport.frame!==haveFrame)return;
 const strip=stripReport.strips[$('stripSelect').selectedIndex];if(!strip)return;
 const [l,b,r,t]=strip.box;
 ctx.save();ctx.strokeStyle=css('--acc');ctx.setLineDash([4,3]);ctx.strokeRect(sx(l),sy(t),(r-l)*view.s,(t-b)*view.s);
 ctx.setLineDash([]);ctx.lineWidth=3;ctx.strokeStyle=css('--io');ctx.beginPath();
 for(const [x0,y0,x1,y1] of strip.entrances){ctx.moveTo(sx(x0),sy(y0));ctx.lineTo(sx(x1),sy(y1))}
 ctx.stroke();ctx.restore();
}
let dens={b:48,v:null,max:1},densFrame=-2;
function densMap(){const B=dens.b,v=new Float64Array(B*B),[xl,yl,xh,yh]=D.die,bw=(xh-xl)/B,bh=(yh-yl)/B;
 for(let i=0;i<X.length;i++){const k=kd(i);if(k===3||k===4||D.w[i]<=0||D.h[i]<=0)continue;const x0=X[i],x1=x0+D.w[i],y0=Y[i],y1=y0+D.h[i];
  const i0=Math.max(0,Math.floor((x0-xl)/bw)),i1=Math.min(B-1,Math.floor((x1-xl)/bw)),j0=Math.max(0,Math.floor((y0-yl)/bh)),j1=Math.min(B-1,Math.floor((y1-yl)/bh));
  for(let j=j0;j<=j1;j++)for(let a=i0;a<=i1;a++){
   const l=Math.max(x0,xl+a*bw),r=Math.min(x1,xl+(a+1)*bw),b=Math.max(y0,yl+j*bh),t=Math.min(y1,yl+(j+1)*bh);
   if(l<r&&b<t)v[a+j*B]+=D.exclusions?.length?usableArea([l,b,r,t]):(r-l)*(t-b);
  }
 }
 let m=0;for(let j=0;j<B;j++)for(let a=0;a<B;a++){
  const i=a+j*B,area=D.exclusions?.length?usableArea([xl+a*bw,yl+j*bh,xl+(a+1)*bw,yl+(j+1)*bh]):bw*bh;
  v[i]=area>0?v[i]/area:0;m=Math.max(m,v[i]);
 }
 dens.v=v;dens.max=Math.max(1,m);densFrame=frameId}
function buildGrid(){grid=null;if(!X)return;const n=X.length;let xl=1e300,yl=1e300,xh=-1e300,yh=-1e300;
 for(let i=0;i<n;i++){if(kd(i)!==0)continue;if(X[i]<xl)xl=X[i];if(X[i]>xh)xh=X[i];if(Y[i]<yl)yl=Y[i];if(Y[i]>yh)yh=Y[i]}
 if(xl>xh)return;const m=Math.max(1,Math.ceil(Math.sqrt(n)/2)),gw=(xh-xl)/m||1,gh=(yh-yl)/m||1,b=new Map();
 for(let i=0;i<n;i++){if(kd(i)!==0)continue;const k=Math.min(m-1,((X[i]-xl)/gw)|0)+Math.min(m-1,((Y[i]-yl)/gh)|0)*m;let a=b.get(k);if(!a)b.set(k,a=[]);a.push(i)}
 grid={xl,yl,gw,gh,m,b}}
function hit(px,py){if(!D||!X)return -1;const n=X.length,x=wx(px),y=wy(py);
 for(let i=n-1;i>=0;i--){const k=kd(i);if(k===0||k===4)continue;if($('l_macros').checked===false&&k!==3)continue;
  if(x>=X[i]&&x<=X[i]+D.w[i]&&y>=Y[i]&&y<=Y[i]+D.h[i])return i}
 if($('l_io').checked){for(let i=n-1;i>=0;i--){if(kd(i)!==4)continue;if(Math.abs(sx(X[i])-px)<6&&Math.abs(sy(Y[i])-py)<6)return i}}
 if(grid&&(lComb.checked||lRegs.checked)){const r=5/view.s,{xl,yl,gw,gh,m,b}=grid;let best=-1,bd=1e300;
  const x0=Math.max(0,((x-r-xl)/gw)|0),x1=Math.min(m-1,((x+r-xl)/gw)|0),y0=Math.max(0,((y-r-yl)/gh)|0),y1=Math.min(m-1,((y+r-yl)/gh)|0);
  for(let gy=y0;gy<=y1;gy++)for(let gx=x0;gx<=x1;gx++){const a=b.get(gx+gy*m);if(!a)continue;
   for(const i of a){if(!cellOn(i))continue;const cx=X[i]+D.w[i]/2,cy=Y[i]+D.h[i]/2,d=(cx-x)**2+(cy-y)**2;if(d<bd&&Math.sqrt(d)<=r+Math.max(D.w[i],D.h[i])/2){bd=d;best=i}}}
  if(best>=0)return best}
 return -1}
function floorplanPath(px,py,rings=D?.outline){
 const outline=new Path2D();
 for(const ring of rings||[]){
  ring.forEach(([x,y],i)=>i?outline.lineTo(px(x),py(y)):outline.moveTo(px(x),py(y)));
  outline.closePath();
 }
 return outline;
}
function draw(){dirty=false;const W=cv.clientWidth,H=cv.clientHeight;ctx.setTransform(dpr,0,0,dpr,0,0);ctx.fillStyle=css('--bg');ctx.fillRect(0,0,W,H);
 if(!D||!X){ctx.fillStyle=css('--mut');ctx.fillText('waiting for the placer...',20,60);return}
 const[xl,yl,xh,yh]=D.die,outline=floorplanPath(sx,sy);
 ctx.fillStyle=css('--die');ctx.fill(outline,'evenodd');ctx.strokeStyle=css('--line');ctx.stroke(outline);
 ctx.strokeStyle=css('--line');ctx.strokeRect(sx(xl),sy(yh),(xh-xl)*view.s,(yh-yl)*view.s);
 {const e=aEdge;ctx.save();ctx.strokeStyle=css('--acc');ctx.lineWidth=4;ctx.beginPath();if(e==='left'){ctx.moveTo(sx(xl),sy(yl));ctx.lineTo(sx(xl),sy(yh))}else if(e==='right'){ctx.moveTo(sx(xh),sy(yl));ctx.lineTo(sx(xh),sy(yh))}else if(e==='bottom'){ctx.moveTo(sx(xl),sy(yl));ctx.lineTo(sx(xh),sy(yl))}else{ctx.moveTo(sx(xl),sy(yh));ctx.lineTo(sx(xh),sy(yh))}ctx.stroke();ctx.restore()}
 if($('l_reg').checked){ctx.save();
  if(D.regions.length){ctx.strokeStyle=css('--reg');ctx.setLineDash([4,3]);for(const[a,b,c,d]of D.regions)ctx.strokeRect(sx(a),sy(d),(c-a)*view.s,(d-b)*view.s);ctx.setLineDash([])}
  if(R&&R.rects.length>1){ctx.strokeStyle=css('--macro');ctx.lineWidth=1.5;ctx.setLineDash([8,4]);for(const[a,b,c,d]of R.rects)ctx.strokeRect(sx(a),sy(d),(c-a)*view.s,(d-b)*view.s);ctx.setLineDash([]);
   if(view.s*Math.max(xh-xl,1)>200){ctx.fillStyle=css('--macro');ctx.font='11px system-ui';for(let k=0;k<R.rects.length;k++){const[a,b,c,d,n]=R.rects[k];ctx.fillText('R'+k+' ('+n+')',sx(a)+4,sy(d)+13)}}
   ctx.fillStyle=css('--sel');const q=Math.max(2,Math.min(4,view.s*(xh-xl)/300));for(const[x,y,ax]of R.pins){const px=sx(x),py=sy(y);if(ax===0)ctx.fillRect(px-q,py-q/2,2*q,q);else ctx.fillRect(px-q/2,py-q,q,2*q)}}
  ctx.restore()}
 const n=X.length,cell=css('--cell'),fx=css('--fixedc');
 ctx.save();ctx.clip(outline,'evenodd');
 if(M&&(S.map||resultView())){const bw=(xh-xl)/M.bx,bh=(yh-yl)/M.by;for(let i=0;i<M.bx;i++)for(let j=0;j<M.by;j++){const u=M.v[i*M.by+j];if(u<=.05)continue;const t=Math.min(1,u/Math.max(1,M.peak));ctx.fillStyle='rgba('+(u>1?'255,60,60':'255,170,40')+','+(.1+.55*t).toFixed(3)+')';ctx.fillRect(sx(xl+i*bw),sy(yl+(j+1)*bh),bw*view.s+.5,bh*view.s+.5)}}
 if($('l_dens').checked){if(densFrame!==frameId)densMap();const B=dens.b,bw=(xh-xl)/B,bh=(yh-yl)/B;for(let j=0;j<B;j++)for(let i=0;i<B;i++){const v=Math.min(1,dens.v[i+j*B]/dens.max);if(v<=.02)continue;ctx.fillStyle='rgba(224,36,94,'+(.08+.6*v).toFixed(3)+')';ctx.fillRect(sx(xl+i*bw),sy(yl+(j+1)*bh),bw*view.s+.5,bh*view.s+.5)}}
 ctx.restore();
 if(lComb.checked||lRegs.checked){const sz=Math.max(1.4,Math.min(4,view.s*3));
  const vx0=wx(0),vx1=wx(W),vy1=wy(0),vy0=wy(H),seqc=css('--seq');for(let i=0;i<n;i++){if(kd(i)!==0||!cellOn(i))continue;if(X[i]>vx1||Y[i]>vy1||X[i]+D.w[i]<vx0||Y[i]+D.h[i]<vy0)continue;ctx.fillStyle=isSeq(i)?seqc:isMovable(i)?cell:fx;const w=D.w[i]*view.s,h=D.h[i]*view.s;
   if(w>3||h>3)ctx.fillRect(sx(X[i]),sy(Y[i])-h,w,h);else ctx.fillRect(sx(X[i]+D.w[i]/2)-sz/2,sy(Y[i]+D.h[i]/2)-sz/2,sz,sz)}}
 if($('l_disp').checked&&PX&&PX.length===n){ctx.strokeStyle=css('--bad');ctx.lineWidth=1;ctx.beginPath();
  for(let i=0;i<n;i++){if(kd(i)!==0)continue;const dx=X[i]-PX[i],dy=Y[i]-PY[i];if(dx*dx+dy*dy>0){ctx.moveTo(sx(PX[i]),sy(PY[i]));ctx.lineTo(sx(X[i]),sy(Y[i]))}}ctx.stroke()}
 for(let i=0;i<n;i++){const k=kd(i);if(k===0||k===4||(k!==3&&!$('l_macros').checked))continue;const mv=isMovable(i),w=D.w[i]*view.s,h=D.h[i]*view.s;
  ctx.globalAlpha=k===3?.25:.35;ctx.fillStyle=k===3?css('--reg'):mv?css('--macro'):css('--macrof');ctx.fillRect(sx(X[i]),sy(Y[i])-h,w,h);ctx.globalAlpha=1;
  ctx.strokeStyle=mv?css('--macro'):css('--macrof');ctx.lineWidth=1;ctx.strokeRect(sx(X[i]),sy(Y[i])-h,w,h);
  if(k!==3&&w>46&&h>14){ctx.fillStyle=css('--fg');ctx.font='11px system-ui';ctx.fillText(nm(i),sx(X[i])+4,sy(Y[i])-h+13,w-8)}}
 if($('l_pins').checked){ctx.fillStyle=css('--io');for(const [i,ox,oy] of D.macro_pins)ctx.fillRect(sx(X[i]+ox)-1,sy(Y[i]+oy)-1,3,3)}
 drawSelectedStrip();
 if($('l_io').checked){ctx.fillStyle=css('--io');for(let i=0;i<n;i++){if(kd(i)!==4)continue;ctx.fillRect(sx(X[i])-2.5,sy(Y[i])-2.5,5,5)}}
 if($('l_cen').checked&&D.centroids.length){ctx.strokeStyle=css('--sel');ctx.fillStyle=css('--sel');D.centroids.forEach((c0,ci)=>{const c=C&&C.x.length>ci?{x:C.x[ci],y:C.y[ci],r:C.r[ci]}:c0;const x=sx(c.x),y=sy(c.y),r=Math.max(4,c.r*view.s);
  ctx.beginPath();ctx.arc(x,y,r,0,7);ctx.globalAlpha=.12;ctx.fill();ctx.globalAlpha=1;ctx.stroke();ctx.beginPath();ctx.moveTo(x-5,y);ctx.lineTo(x+5,y);ctx.moveTo(x,y-5);ctx.lineTo(x,y+5);ctx.stroke()})}
 if(rev===null&&HL.length){for(const h of HL){ctx.strokeStyle=h.color;ctx.fillStyle=h.color;for(const i of h.idx){if(i>=n)continue;const w=D.w[i]*view.s,hh=D.h[i]*view.s,x=sx(X[i]),y=sy(Y[i]);if(x<-20||y<-20||x>W+20||y>H+20)continue;
  if(w<5&&hh<5){ctx.globalAlpha=.9;ctx.fillRect(sx(X[i]+D.w[i]/2)-3,sy(Y[i]+D.h[i]/2)-3,6,6);ctx.globalAlpha=1;ctx.lineWidth=1;ctx.strokeRect(sx(X[i]+D.w[i]/2)-5,sy(Y[i]+D.h[i]/2)-5,10,10)}
  else{ctx.globalAlpha=.45;ctx.fillRect(x,y-hh,w,hh);ctx.globalAlpha=1;ctx.lineWidth=2;ctx.strokeRect(x,y-hh,w,hh)}}}}
 ctx.lineWidth=1.5;ctx.strokeStyle=css('--pin');for(const i of pinned){if(i>=n)continue;const w=Math.max(6,D.w[i]*view.s),h=Math.max(6,D.h[i]*view.s);ctx.strokeRect(sx(X[i])-1,sy(Y[i])-h-1,w+2,h+2)}
 ctx.strokeStyle=css('--sel');ctx.lineWidth=2;for(const i of sel){if(i>=n)continue;const w=Math.max(8,D.w[i]*view.s),h=Math.max(8,D.h[i]*view.s);ctx.strokeRect(sx(X[i])-2,sy(Y[i])-h-2,w+4,h+4)}
 drawClock();drawChunks();drawHomePreview();geometryOverlay();drawIntentOverlay();
 if(drag&&drag.box){ctx.strokeStyle=css('--acc');ctx.setLineDash([5,3]);ctx.strokeRect(drag.bx0,drag.by0,drag.bx1-drag.bx0,drag.by1-drag.by0);ctx.setLineDash([])}areaOverlay()}
// The native clock tree: driver-to-load fans by stage depth, endpoints by an estimate, or the local skew grid.
let CK=null,CKM=null,ckKey='',ckScene='',ckBusy=false;
const ckPal=['#ffd60a','#ff8c1e','#f05078','#aa6eff','#5aaaff'],ckHeat=t=>'rgb('+Math.round(40+215*t)+','+Math.round(90+60*(1-Math.abs(2*t-1)))+','+Math.round(255-215*t)+')';
async function loadClock(){const mode=$('l_clk').value,note=$('s_clk');if(mode==='off'){note.textContent='';return}
 if(!S.clock||!S.clock.nets){CK=CKM=null;ckKey=ckScene='';note.textContent='no clock tree in this phase yet';dirty=true;return}
 const sc=S.epoch+'|'+S.scene,key=sc+'|'+mode+'|'+(mode==='skew'?S.frame:'')+'|'+S.clock.staged;if(key===ckKey||ckBusy)return;ckBusy=true;
 try{if(!CK||ckScene!==sc){CK=await xcommand({cmd:'clock',what:'view'});ckScene=sc}
  CKM=mode==='skew'?await xcommand({cmd:'clock',what:'map',metric:'skew'}):null;ckKey=key;
  const a=CK.arrival,f=v=>v==null?'-':v.toFixed(1);
  note.textContent=CK.nets.length+' nets, '+CK.levels+' stages'+(a.known?'; insertion '+f(a.min_ps)+' to '+f(a.max_ps)+' ps, skew '+f(a.skew_ps)+' ps (estimates)':'; no estimates')+(CKM&&CKM.max!=null?'; worst local skew '+f(CKM.max)+' ps':'')+(S.clock.staged?'; '+S.clock.staged+' staged edits':'')}
 catch(e){CK=CKM=null;ckScene='';note.textContent=String(e.message||e)}
 ckBusy=false;dirty=true}
function drawClock(){const mode=$('l_clk').value;if(mode==='off'||!CK||!X||rev!==null||CK.nodes!==X.length)return;const n=X.length,cx=i=>sx(X[i]+D.w[i]/2),cy=i=>sy(Y[i]+D.h[i]/2);
 ctx.save();
 if(mode==='skew'&&CKM&&CKM.max>0){const[xl,yl,xh,yh]=CKM.die_um.map(v=>v*D.dbu),bw=(xh-xl)/CKM.bx,bh=(yh-yl)/CKM.by;
  for(let i=0;i<CKM.bx;i++)for(let j=0;j<CKM.by;j++){const v=CKM.v[i*CKM.by+j];if(v==null)continue;ctx.globalAlpha=.55;ctx.fillStyle=ckHeat(v/CKM.max);ctx.fillRect(sx(xl+i*bw),sy(yl+(j+1)*bh),bw*view.s+.5,bh*view.s+.5)}ctx.globalAlpha=1}
 for(let pass=0;pass<2;pass++)for(const k of CK.nets){if(k.d<0||k.d>=n)continue;ctx.strokeStyle=ckPal[Math.min(k.l,ckPal.length-1)];ctx.lineWidth=pass?2:1;ctx.globalAlpha=pass?1:(mode==='skew'?.25:.5);ctx.beginPath();
  for(let j=0;j<k.s.length;j++){const s=k.s[j];if(s<0||s>=n||(k.e[j]?0:1)!==pass)continue;ctx.moveTo(cx(k.d),cy(k.d));ctx.lineTo(cx(s),cy(s))}ctx.stroke()}
 ctx.globalAlpha=1;
 if(mode==='arrival'||mode==='slew'){const a0=CK.arrival.min_ps,a1=CK.arrival.max_ps,q=Math.max(3,Math.min(7,view.s*400));
  for(const k of CK.nets)for(let j=0;j<k.s.length;j++){const s=k.s[j];if(!k.e[j]||s<0||s>=n)continue;let t=null;
   if(mode==='arrival'){if(k.a[j]!=null&&a1>a0)t=(k.a[j]-a0)/(a1-a0)}else if(k.sl!=null&&CK.slew_max_ps>0)t=k.sl/CK.slew_max_ps;
   if(t===null)continue;ctx.fillStyle=ckHeat(t);ctx.fillRect(cx(s)-q/2,cy(s)-q/2,q,q)}}
 for(const k of CK.nets){if(k.d<0||k.d>=n)continue;ctx.fillStyle=ckPal[Math.min(k.l,ckPal.length-1)];ctx.fillRect(cx(k.d)-3,cy(k.d)-3,6,6)}
 ctx.restore()}
$('l_clk').onchange=()=>{dirty=true;loadClock()};
function loop(){if(dirty)draw();requestAnimationFrame(loop)}requestAnimationFrame(loop);
cv.addEventListener('wheel',e=>{e.preventDefault();const f=Math.exp(-e.deltaY*.0015),x=e.offsetX,y=e.offsetY,ox=wx(x),oy=wy(y);view.s*=f;view.ox=x-ox*view.s;view.oy=y+oy*view.s;dirty=true},{passive:false});
function selMove(dx,dy,final){const ns=[],xs=[],ys=[],fn=[],fx=[],fy=[];for(const i of drag.items){let x=drag.x0.get(i)+dx,y=drag.y0.get(i)+dy;const[a,b,c,d]=D.die;x=Math.min(Math.max(x,a),c-D.w[i]);y=Math.min(Math.max(y,b),d-D.h[i]);X[i]=x;Y[i]=y;drag.cur.set(i,[x,y]);if(isMovable(i)){ns.push(nm(i));xs.push(x);ys.push(y)}else{fn.push(nm(i));fx.push(x);fy.push(y)}}
 dirty=true;return{nodes:ns,x:xs,y:ys,final,fixed:{nodes:fn,x:fx,y:fy}}}
// Movable nodes are pinned in the running solver; a FIXED macro (a memory) is an obstacle the solver cannot move, so its
// final position restarts the flow with the macro there (the placer starts it at that spot).
function sendMove(m){if(m.nodes.length)cmd({cmd:'move',nodes:m.nodes,x:m.x,y:m.y,final:m.final});if(m.final&&m.fixed.nodes.length)cmd({cmd:'macro_move',nodes:m.fixed.nodes,x:m.fixed.x,y:m.fixed.y,final:true})}
function hitCen(px,py){if(!D||!$('l_cen').checked||!D.centroids.length)return -1;for(let i=D.centroids.length-1;i>=0;i--){const c=C&&C.x.length>i?{x:C.x[i],y:C.y[i]}:D.centroids[i];if(Math.hypot(sx(c.x)-px,sy(c.y)-py)<=10)return i}return -1}
cv.addEventListener('mousedown',e=>{if(!D||e.button!==0)return;if(beginGeometry(e))return;if(e.altKey&&D.phase==='fp'){drag={pan:true,px:e.offsetX,py:e.offsetY,ox:view.ox,oy:view.oy};return}if(rev!==null){drag={pan:true,px:e.offsetX,py:e.offsetY,ox:view.ox,oy:view.oy};return}{const eg=e.shiftKey||resultView()?null:hitEdge(e.offsetX,e.offsetY);if(eg){aEdge=eg;aStepSet=false;if(!aPend){aPend=D.die.slice();aBase=D.die.join()}drag={edge:eg};areaInfo();dirty=true;return}}const ci=hitCen(e.offsetX,e.offsetY);
 if(ci>=0&&(!D.centroids[ci].name.startsWith('rec:')||D.centroids[ci].name.startsWith('rec:gui-centroids::')||D.centroids[ci].name.startsWith('rec:pcrs:'))&&!e.shiftKey&&!resultView()){if(!C||C.x.length<=ci)C={x:Float32Array.from(D.centroids,c=>c.x),y:Float32Array.from(D.centroids,c=>c.y),r:Float32Array.from(D.centroids,c=>c.r)};drag={cen:ci,px:e.offsetX,py:e.offsetY,x0:C.x[ci],y0:C.y[ci],t:0,moved:false};dirty=true;return}
 const i=hit(e.offsetX,e.offsetY);
 if(i>=0){if(e.shiftKey){sel.has(i)?sel.delete(i):sel.add(i);selUi();dirty=true;return}
  if(!sel.has(i)){sel.clear();sel.add(i)}selUi();const items=resultView()?[]:[...sel].filter(canMove);
  if(!items.length){dirty=true;return}
  drag={items,px:e.offsetX,py:e.offsetY,x0:new Map(items.map(k=>[k,X[k]])),y0:new Map(items.map(k=>[k,Y[k]])),t:0,moved:false,cur:new Map()}}
 else if(e.shiftKey){drag={box:true,bx0:e.offsetX,by0:e.offsetY,bx1:e.offsetX,by1:e.offsetY}}
 else{if(sel.size){sel.clear();selUi()}drag={pan:true,px:e.offsetX,py:e.offsetY,ox:view.ox,oy:view.oy}}dirty=true});
cv.addEventListener('dblclick',e=>{
 if(rev!==null||D?.phase!=='fp'||!chunkPlan)return;
 const x=wx(e.offsetX),y=wy(e.offsetY),c=chunkPlan.chunks.find(c=>{return (c.region_dbu||[c.box_dbu]).some(b=>x>=b[0]&&x<=b[2]&&y>=b[1]&&y<=b[3])});
 if(!c)return;
 $('chunks').open=true;$('chunkSelect').value=JSON.stringify(['chunk',c.id]);$('chunkSelect').onchange();
 $('implFamily').scrollIntoView({block:'nearest'});
});
let mx=-1,my=-1;
function zoomBy(f){const inside=mx>=0&&my>=0&&mx<=cv.clientWidth&&my<=cv.clientHeight;zoomAt(f,inside?mx:cv.clientWidth/2,inside?my:cv.clientHeight/2)}
function zoomAt(f,x,y){const ox=wx(x),oy=wy(y);view.s*=f;view.ox=x-ox*view.s;view.oy=y+oy*view.s;dirty=true}
window.addEventListener('mousemove',e=>{const r=cv.getBoundingClientRect(),px=e.clientX-r.left,py=e.clientY-r.top;mx=px;my=py;
 if(!drag&&D){const eg=resultView()||rev!==null?null:hitEdge(px,py);cv.style.cursor=eg?(eg==='left'||eg==='right'?'ew-resize':'ns-resize'):''}else if(drag&&drag.edge){cv.style.cursor=drag.edge==='left'||drag.edge==='right'?'ew-resize':'ns-resize'}
 if(!drag){if(!D||!X)return;const i=hit(px,py);if(i!==hover){hover=i;const t=$('tip');if(i<0)t.style.display='none';else{showTip(i);t.style.display='block';if(names[i]===undefined&&rev===null)fetchName(i)}}
  if(hover>=0){const t=$('tip');t.style.left=px+14+'px';t.style.top=py+14+'px'}return}
 if(drag.chunk){geometryMotion(drag,wx(px),wy(py));return}
 if(drag.cen!==undefined){if(!drag.moved&&Math.hypot(px-drag.px,py-drag.py)<3)return;drag.moved=true;const x=drag.x0+(px-drag.px)/view.s,y=drag.y0-(py-drag.py)/view.s;C.x[drag.cen]=x;C.y[drag.cen]=y;dirty=true;const now=performance.now();if(now-drag.t>40){drag.t=now;if(!D.centroids[drag.cen].name.startsWith('rec:'))cmd({cmd:'centroid',name:D.centroids[drag.cen].name,x,y,final:false})}}
 else if(drag.pan){view.ox=drag.ox+px-drag.px;view.oy=drag.oy+py-drag.py;dirty=true}
 else if(drag.box){drag.bx1=px;drag.by1=py;dirty=true}
 else if(drag.edge){const e2=drag.edge;areaSet(e2,Math.round(e2==='left'||e2==='right'?wx(px):wy(py)))}
 else{const dx=(px-drag.px)/view.s,dy=-(py-drag.py)/view.s;if(!drag.moved&&Math.hypot(px-drag.px,py-drag.py)<3)return;drag.moved=true;const now=performance.now();
  if(now-drag.t>40){drag.t=now;sendMove(selMove(dx,dy,false))}else selMove(dx,dy,false)}});
window.addEventListener('mouseup',e=>{if(!drag)return;const r=cv.getBoundingClientRect(),px=e.clientX-r.left,py=e.clientY-r.top;
 if(drag.chunk){const g=drag;geometryMotion(g,wx(px),wy(py));drag=null;dirty=true;
   if(g.moved){const cap=$('chunkOverlap').valueAsNumber/100;
     if(!(cap>=0&&cap<=.25)){$('geometryInfo').textContent='Overlap must be from 0 to 25%.';return}
     geometryEdit({move:g.chunk.id,to_dbu:[g.chunk.box_dbu[0]+g.dx,g.chunk.box_dbu[1]+g.dy],max_overlap:cap},g.chunk.id,g,'geometry',!$('reviewDrop').checked)}
   return}
 if(drag.box){const x0=Math.min(drag.bx0,drag.bx1),x1=Math.max(drag.bx0,drag.bx1),y0=Math.min(drag.by0,drag.by1),y1=Math.max(drag.by0,drag.by1);
  for(let i=0;i<X.length;i++){const k=kd(i);if(k===4||k===3)continue;const cx=sx(X[i]+D.w[i]/2),cy=sy(Y[i]+D.h[i]/2);if(cx>=x0&&cx<=x1&&cy>=y0&&cy<=y1)sel.add(i)}selUi()}
 else if(drag.cen!==undefined&&drag.moved){
   const name=D.centroids[drag.cen].name,x=C.x[drag.cen],y=C.y[drag.cen];
   if(name.startsWith('rec:')){
     const generated=name.startsWith('rec:pcrs:'),dragIndex=drag.cen,originalX=drag.x0,originalY=drag.y0,frame=C;
     xcommand({cmd:'centroid_edit',target:exploration||'main',expect:{frame:S.frame,intent:S.intent_revision},
       op:generated?'adopt':'set',id:name.slice(generated?4:'rec:gui-centroids::'.length),position:[x/D.dbu,y/D.dbu],apply:true})
       .catch(e=>{frame.x[dragIndex]=originalX;frame.y[dragIndex]=originalY;dirty=true;$('msg').textContent=e.message});
   }else cmd({cmd:'centroid',name,x,y,final:true});
 }
 else if(drag.items&&drag.moved){sendMove(selMove((px-drag.px)/view.s,-(py-drag.py)/view.s,true))}
 drag=null;dirty=true});
let HL=[],hlV=-1,selV=-1,selExpect=0,applyingSel=false,selTimer=null;
function selUi(){candidateControls();$('s_sel').textContent=sel.size;if(!applyingSel&&!resultView()&&rev===null){clearTimeout(selTimer);selTimer=setTimeout(()=>{selExpect++;cmd({cmd:'select',names:[...sel].slice(0,5000).map(i=>'#'+i)})},150)}}
async function loadHighlights(){const r=await(await api('/highlights')).json();HL=r.h;hlV=r.v;renderHl();dirty=true}
function renderHl(){const box=$('hls');box.innerHTML='';if(!HL.length){box.textContent='none (an assistant can paint cells here)';return}
 for(const h of HL){const row=document.createElement('div');row.style.cssText='display:flex;gap:6px;align-items:center;margin:2px 0';const sw=document.createElement('span');sw.style.cssText='width:12px;height:12px;border-radius:2px;flex:none;background:'+h.color;
  const t=document.createElement('span');t.style.flex='1';t.textContent=h.label+' ('+h.count+(h.truncated?'+':'')+')';t.title=(h.regex?'regex '+h.regex+' ':'')+(h.scope?'scope '+h.scope:'');const x=document.createElement('button');x.textContent='x';x.style.padding='0 6px';x.onclick=()=>cmd({cmd:'clear_highlight',id:h.id});row.append(sw,t,x);box.appendChild(row)}}
async function loadSelection(){const r=await(await api('/selection')).json();applyingSel=true;sel=new Set(r.idx);selUi();applyingSel=false;dirty=true}
$('al').addEventListener('click',e=>{const a=e.target.dataset?.a;if(a)align(a)});$('al').nextElementSibling.addEventListener('click',e=>{if(e.target.dataset?.a==='matrix')align('matrix')});
async function align(a){if(!D||resultView()||rev!==null)return;const it=[...sel].filter(canMove);if(a==='unpin'){const ns=it.filter(i=>isMovable(i)&&pinned.has(i)).map(nm);if(ns.length)cmd({cmd:'unpin',nodes:ns});return}
 if(it.length<2)return;const f=it[0];let L=1e300,R=-1e300,B=1e300,Tp=-1e300;for(const i of it){L=Math.min(L,X[i]);R=Math.max(R,X[i]+D.w[i]);B=Math.min(B,Y[i]);Tp=Math.max(Tp,Y[i]+D.h[i])}
 const nx=new Map(it.map(i=>[i,X[i]])),ny=new Map(it.map(i=>[i,Y[i]]));
 // Like Inkscape: everything lines up with the FIRST selected object, which stays where it is (change the selection to change the reference).
 if(a==='left')it.forEach(i=>nx.set(i,X[f]));else if(a==='right')it.forEach(i=>nx.set(i,X[f]+D.w[f]-D.w[i]));else if(a==='hcenter')it.forEach(i=>nx.set(i,X[f]+D.w[f]/2-D.w[i]/2));
 else if(a==='bottom')it.forEach(i=>ny.set(i,Y[f]));else if(a==='top')it.forEach(i=>ny.set(i,Y[f]+D.h[f]-D.h[i]));else if(a==='vcenter')it.forEach(i=>ny.set(i,Y[f]+D.h[f]/2-D.h[i]/2));
 else if(a==='disth'){const s=[...it].sort((p,q)=>X[p]-X[q]);const sum=s.reduce((t,i)=>t+D.w[i],0),gap=(R-L-sum)/(s.length-1);let x=L;for(const i of s){nx.set(i,x);x+=D.w[i]+gap}}
 else if(a==='distv'){const s=[...it].sort((p,q)=>Y[p]-Y[q]);const sum=s.reduce((t,i)=>t+D.h[i],0),gap=(Tp-B-sum)/(s.length-1);let y=B;for(const i of s){ny.set(i,y);y+=D.h[i]+gap}}
 else if(a==='matrix'){const rows=Math.max(1,+$('m_r').value|0),cols=Math.max(1,+$('m_c').value|0),s=[...it].sort((p,q)=>Y[q]-Y[p]||X[p]-X[q]),mw=Math.max(...it.map(i=>D.w[i])),mh=Math.max(...it.map(i=>D.h[i]));
  const px=cols>1?(R-L-mw)/(cols-1):0,py=rows>1?(Tp-B-mh)/(rows-1):0;s.forEach((i,k)=>{const r=Math.min(rows-1,Math.floor(k/cols)),c=k%cols;nx.set(i,L+c*px);ny.set(i,Tp-mh-r*py)})}
 if($('ipreserve').checked){
   try{
     const target=exploration||'main';
     if(!S.intent_available || it.some(i=>kind(i)!==1))throw Error('Select movable hard macros at a placement intent gate');
     const hello=await xcommand({cmd:'hello',target});
     const current=await xcommand({cmd:'export',what:'intent',target,encoding:'json_text'});
     const module=hello.session.top||current.bindings.design;
     if(!module)throw Error('No bound top module');
     const horizontal=['left','hcenter','right','disth'].includes(a);
     let order=[...it],hint={kind:'align',axis:horizontal?'x':'y',anchor:['left','bottom'].includes(a)?'min':['right','top'].includes(a)?'max':'center'};
     if(a==='disth'||a==='distv'){
       order.sort((p,q)=>horizontal?X[p]-X[q]:Y[p]-Y[q]);hint={kind:'distribute',axis:horizontal?'x':'y'};
     }else if(a==='matrix'){
       order.sort((p,q)=>ny.get(p)-ny.get(q)||nx.get(p)-nx.get(q));
       const cols=Math.min(order.length,Math.max(1,+$('m_c').value|0));
       if(order.length%cols!==0)throw Error('Preserving the displayed matrix requires complete rows');
       const rows=order.length/cols;
       if(rows!==Math.max(1,+$('m_r').value|0))throw Error('Set rows and columns to fit the selected macros');
       const sx=c=>Math.max(...order.filter((_,k)=>k%cols===c).map(i=>D.w[i]+2*(D.halo?.[0]||0)));
       const sy=r=>Math.max(...order.slice(r*cols,(r+1)*cols).map(i=>D.h[i]+2*(D.halo?.[1]||0)));
       const w=Array.from({length:cols},(_,c)=>sx(c)).reduce((x,y)=>x+y,0),h=Array.from({length:rows},(_,r)=>sy(r)).reduce((x,y)=>x+y,0);
       const gx=cols>1?Math.max(0,(R-L-w)/(cols-1)):0,gy=rows>1?Math.max(0,(Tp-B-h)/(rows-1)):0;
       const [xl,yl,xh,yh]=D.die;
       const normalized=(val,span)=>span>0?Math.min(1,Math.max(0,val/span)):.5;
       hint={kind:'matrix',columns:cols,gap_x_um:gx/D.dbu,gap_y_um:gy/D.dbu,
          anchor_x:normalized(L-xl,xh-xl-w-gx*(cols-1)),anchor_y:normalized(B-yl,yh-yl-h-gy*(rows-1))};
     }
     hint.members=order.map(i=>[nm(i)]);hint.id='relationship-'+crypto.randomUUID();
     const record={class:'livehd.placement.hints',schema_version:2,id:hint.id,design:module,templates:[{module,hints:[hint]}]};
     const array=current.records_json.trim().slice(0,-1).trimEnd();
     await xcommand({cmd:'intent_apply',target,records_json:array+(array==='['?'':',')+JSON.stringify(record)+']',expect:{frame:S.frame,intent:current.revision}});
   }catch(e){$('ibind').textContent=e.message}
   return;
 }
 const axes={left:'left',right:'right',hcenter:'center_x',top:'top',bottom:'bottom',vcenter:'center_y'};
 if(axes[a]&&it.every(i=>isMovable(i)&&nx.get(i)>=D.die[0]&&ny.get(i)>=D.die[1]&&nx.get(i)+D.w[i]<=D.die[2]&&ny.get(i)+D.h[i]<=D.die[3])){
   await cmd({cmd:'move',nodes:it.map(nm),reference:nm(f),align:axes[a],expect:{frame:S.frame}});
   return;
 }
 const[a0,b0,c0,d0]=D.die,ns=[],xs=[],ys=[],fn=[],fx=[],fy=[];for(const i of it){const x=Math.min(Math.max(nx.get(i),a0),c0-D.w[i]),y=Math.min(Math.max(ny.get(i),b0),d0-D.h[i]);X[i]=x;Y[i]=y;if(isMovable(i)){ns.push(nm(i));xs.push(x);ys.push(y)}else{fn.push(nm(i));fx.push(x);fy.push(y)}}
 // The reference goes LAST among the fixed macros: the placer settles the newest move first, so the reference keeps its spot
 // and the others find the nearest legal place around it.
 const fi=fn.indexOf(nm(f));if(fi>=0&&fi<fn.length-1){for(const arr of[fn,fx,fy])arr.push(arr.splice(fi,1)[0])}
 dirty=true;sendMove({nodes:ns,x:xs,y:ys,final:true,fixed:{nodes:fn,x:fx,y:fy}})}
$('l_map').onchange=async()=>{
 const kind=$('l_map').value;
 if(exploration&&S.state==='done'){M=kind==='none'?null:await readGrid(exploration,kind);$('s_map').textContent=M?'peak '+M.peak.toFixed(2):kind==='none'?'':'unavailable';dirty=true;return}
 cmd({cmd:'map',kind});if(kind==='none'){M=null;dirty=true}
};
$('cen_rel').onclick=e=>{e.preventDefault();cmd({cmd:'centroid_release'})};
$('evaluate').onclick=()=>cmd({cmd:'evaluate'});
for(const gate of ['no_resynthesis','no_sizing','no_buffer_edits']) $(gate).onchange=()=>cmd({cmd:'policy',[gate]:$(gate).checked});
$('freeze').onclick=()=>cmd({cmd:'policy',fast:true});
$('tim').onclick=()=>cmd({cmd:'timing'});
$('rp').onclick=sendRestart;$('f_sel').onclick=forkSelection;$('f_view').onclick=forkView;for(const b of document.querySelectorAll('#a_edges button'))b.onclick=()=>{aEdge=b.dataset.e;aStepSet=false;areaInfo();dirty=true};$('a_step').oninput=()=>{aStepSet=true};$('a_shr').onclick=()=>areaNudge(-1);$('a_grow').onclick=()=>areaNudge(1);$('a_apply').onclick=areaApply;$('a_cancel').onclick=areaCancel;$('sb_start').onclick=()=>{stopPlay();scrubTo(0)};$('sb_back').onclick=()=>stepTl(-1);$('sb_fwd').onclick=()=>stepTl(1);$('sb_end').onclick=live;$('sb_play').onclick=togglePlay;$('sb_rng').onpointerdown=()=>{scrubbing=true};window.addEventListener('pointerup',()=>{scrubbing=false});$('sb_rng').oninput=()=>{stopPlay();scrubTo(+$('sb_rng').value)};$('live').onclick=live;$('o_open').onclick=openOptions;$('otog').onclick=()=>{$('opts').className==='on'?$('opts').className='':openOptions()};$('o_close').onclick=()=>{$('opts').className=''};$('o_q').oninput=renderOptions;$('o_chg').onchange=renderOptions;$('o_reset').onclick=()=>{flagEdits={};renderOptions()};$('o_start').onclick=()=>rerun(-1);$('o_cp').onclick=()=>rerun(selCp);
$('cont').onclick=()=>cmd({cmd:'continue'});
$('run').onclick=()=>cmd({cmd:S.finished?'finish':S.paused||S.state==='gate'||S.state==='ready'||S.state==='starting'?'run':'pause'});
$('help').onclick=()=>$('help').classList.remove('on');$('fit').onclick=fit;$('zin').onclick=()=>zoomBy(1.25);$('zout').onclick=()=>zoomBy(1/1.25);$('ptog').onclick=()=>{const p=$('panel');p.style.display=p.style.display==='none'?'':'none'};
$('shot').onclick=()=>{draw();cv.toBlob(b=>{const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='placement_'+(S.phase||'x')+'_'+(S.iter||0)+'.png';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),2000)})};
for(const id of['l_comb','l_regs','l_macros','l_pins','l_io','l_reg','l_cen','l_disp','l_dens'])$(id).onchange=()=>{dirty=true};$('l_reg').addEventListener('change',()=>{if(resultView()){dirty=true;return}cmd({cmd:'regions',on:$('l_reg').checked});if(!$('l_reg').checked){R=null;regV=-1;$('reg_note').textContent=''}else $('reg_note').textContent='computing...'});
window.addEventListener('keydown',e=>{if(['INPUT','TEXTAREA','SELECT'].includes(e.target.tagName))return;if(e.key==='?'){e.preventDefault();$('help').classList.toggle('on');return}if(e.key==='Escape'&&$('help').classList.contains('on')){$('help').classList.remove('on');return}if(e.key==='Escape'&&aPend){areaCancel();return}if(e.key===' '){e.preventDefault();$('run').click()}else if(e.key==='u'){align('unpin')}else if(e.key==='f'){fit()}else if(e.key==='ArrowLeft'){e.preventDefault();stepTl(-1)}else if(e.key==='ArrowRight'){e.preventDefault();stepTl(1)}else if(e.code==='KeyZ'||e.key==='z'||e.key==='Z'||e.key==='+'||e.key==='='||e.key==='-'||e.key==='_'){const out=e.key==='Z'||(e.code==='KeyZ'&&e.shiftKey)||e.key==='-'||e.key==='_';zoomBy(out?1/1.25:1.25)}else if(e.key==='Escape'){if($('opts').className==='on'){$('opts').className=''}else{sel.clear();selUi();dirty=true}}});
function fmt(v,d){return v==null?'-':(+v).toFixed(d)}
let pinView='', priorSuspended=new Set(), restoredPins=[];
function pinInfo(){
 const key=(S.epoch||'')+'/'+(S.target||'main'), pins=S.pin_intent||[], suspended=pins.filter(p=>p.suspended);
 if(key!==pinView){pinView=key;priorSuspended.clear();restoredPins=[]}
 const restored=pins.filter(p=>!p.suspended&&priorSuspended.has(p.name)).map(p=>p.name);
 if(restored.length)restoredPins=restored;
 const active=new Set(pins.filter(p=>!p.suspended).map(p=>p.name));
 restoredPins=restoredPins.filter(n=>active.has(n));priorSuspended=new Set(suspended.map(p=>p.name));
 const box=$('pinInspector');box.hidden=!pins.length;
 const summary='Pin anchors: '+active.size+' active, '+suspended.length+' suspended'+(restoredPins.length?' · restored: '+restoredPins.join(', '):'');
 if($('pinSummary').textContent!==summary)$('pinSummary').textContent=summary;
 if(box.open){const lines=pins.map(p=>p.name+' @ ('+p.x+', '+p.y+') DBU · '+(p.suspended?'suspended: '+(S.geometry_conflicts?.[p.name]||[]).join(', '):'active')).join('\n');
  if($('pinAnchors').textContent!==lines)$('pinAnchors').textContent=lines}
}
$('pinInspector').ontoggle=()=>{if(S)pinInfo()};
function geometryWarning(){
 const box=$('geom'),list=$('geomList'),summary=$('geomSummary');
 box.hidden=S.geometry_status!=='illegal';
 if(box.hidden){
  if(summary.textContent){summary.textContent='';list.replaceChildren();box.open=false;delete list.dataset.frame}
  return;
 }
 const conflicts=S.geometry_conflicts,names=Object.keys(conflicts),key=[S.epoch,S.scene,S.frame].join(':');
 const label='ILLEGAL GEOMETRY · '+names.length+' affected '+(names.length===1?'instance':'instances');
 if(summary.textContent!==label)summary.textContent=label;
 if(list.dataset.frame===key)return;
 if(list.childElementCount)list.replaceChildren();
 delete list.dataset.frame;
 if(!box.open&&names.length>2)return;
 for(const n of names){const li=document.createElement('li');li.textContent=n+': '+conflicts[n].join('; ');list.append(li)}
 list.dataset.frame=key;
}
$('geom').ontoggle=()=>{if(S)geometryWarning()};
function frameTimingBreakdown(){
 const details=$('frameTimingGroups'),timing=S.summary?.timing,e=timing?.endpoints;
 details.hidden=!e?.total?.endpoints;
 if(details.hidden){delete details.dataset.frame;return}
 const label=(timing.model==='final_downstream_sta'?'Routed':'Placement')+' setup timing by clock and path family';
 if(details.firstElementChild.textContent!==label)details.firstElementChild.textContent=label;
 if(!details.open)return;
 const key=[S.epoch,timing.frame,S.evaluation_seq].join(':');
 if(key!==details.dataset.frame){fillTimingBreakdown(details.lastElementChild,e);details.dataset.frame=key}
}
$('frameTimingGroups').ontoggle=()=>{if(S)frameTimingBreakdown()};
function ui(){areaInfo();forkList();loadClock();$('s_phase').textContent=S.phase||'-';const st=$('s_state');st.textContent=S.state+(S.gate?' before '+S.gate:'');st.className='st-'+S.state;
 pinInfo();frameTimingBreakdown();geometryWarning();
 const routed=S.summary?.timing.model==='final_downstream_sta';
 $('s_iter').textContent=S.iter;$('s_pend').textContent=S.pending;$('s_hpwl').textContent=S.hpwl==null?'-':fmt(S.hpwl,1);$('s_ovf').textContent=S.overflow==null||S.overflow<0?'-':fmt(S.overflow,4);
 {const e=S.summary?.timing.endpoints,f=$('s_freq'),periods=Object.entries((routed?e?.clock_periods_ns:e?.clock_periods_ps)||{});
  f.textContent=fmt(e?.frequency_mhz,0);f.style.color='';
  const reasons={multiple_clocks:'Multiple clock domains have no single reported frequency.',unresolved_clock:'Capture clock unresolved.',
   unverified_period_dependence:'The timer cannot verify full-cycle coverage.',nonpositive_period:'The derived period is nonpositive.',
   no_constrained_endpoints:'No constrained setup endpoints.'};
  f.title=e?.frequency_mhz!=null?'From the period and worst setup slack of the measured endpoints.':routed?'No frequency derived from the routed setup report.':reasons[e?.frequency_status]||'Requires full timing on this frame.';
  $('clockCoverage').textContent=e?[e.engine,...(e.setup_corners||[]),...periods.map(([clock,period])=>clock+' '+fmt(period,2)+(routed?' ns':' ps')),e.frequency_mhz==null?f.title:''].filter(Boolean).join(' · '):'';
  const issues=e?.constraint_issues||[],list=$('sdcIssues');
  $('sdcCoverage').hidden=!issues.length;
  if(list.dataset.hash!==(e?.constraint_issues_hash||'')){
   list.replaceChildren(...issues.map(issue=>{const li=document.createElement('li');li.textContent=issue;return li}));
   list.dataset.hash=e?.constraint_issues_hash||'';
   $('sdcCount').textContent=issues.length+' SDC '+(issues.length===1?'issue':'issues');
  }
 }
 $('s_wns').textContent=routed?fmt(S.summary.timing.wns_ps,1):S.has_timing?(S.timing_est?'~':'')+fmt(S.wns,1):'-';$('s_tns').textContent=routed?fmt(S.summary.timing.tns_ps,1):S.has_timing?fmt(S.tns,1):'-';$('s_titer').textContent=routed?'Final downstream STA':S.has_timing?S.timing_iter:'-';
 const measured=S.summary?.timing,advice=$('repairAdvice');advice.hidden=!measured?.repair_advice?.recommended;advice.textContent=advice.hidden?'':repairAdviceText(measured);
 if(S.summary){const r=S.summary.routing,q=S.summary.quality;$('routing_share').textContent=r.overflow_bin_share==null?'–':(100*r.overflow_bin_share).toFixed(1)+'% ('+r.estimator+')';$('quality').textContent=q.score==null?q.status.replace('_',' '):q.score.toFixed(2)}
 candidateControls();
 if(S.slow_work){
   const steps=S.slow_work.operations.filter(op=>op.configured);
   $('slowwork').textContent=steps.map(op=>op.operation+' · '+(op.deferred?'deferred by policy':op.trigger.replaceAll('_',' '))).join(' | ');
   if(S.slow_work.timing!=='idle')$('slowwork').textContent+=' '+S.slow_work.timing.replaceAll('_',' ');
 }
 $('legalize').disabled=!S.legalize_available;
 $('ipreview').disabled=$('iapply').disabled=!S.intent_available;
 if(intentOverlay&&(S.epoch!==intentOverlay.epoch||S.frame!==intentOverlay.frame)){const frame=intentOverlay.frame;clearIntentOverlay();$('ilegend').textContent='Measured at frame '+frame}
 if(S.policy)for(const gate of ['no_resynthesis','no_sizing','no_buffer_edits'])$(gate).checked=S.policy[gate];
 const b=$('run');b.textContent=S.finished?'Exit lhd':(S.paused||S.state==='gate'||S.state==='ready'||S.state==='starting')?'Run':'Pause';b.disabled=false;const ge=S.state==='gate'&&S.gate==='gp_end',ee=S.state==='gate'&&S.gate==='edit';if(ge)b.textContent='Run (+'+S.min_iters+' iter)';if(ee)b.textContent='Apply edits';$('cont').style.display=ge||ee?'':'none';$('msg').textContent=S.note||'';
 const box=$('stages');if(S.stages&&S.stages.length&&box.dataset.k!==JSON.stringify(S.stages)){box.dataset.k=JSON.stringify(S.stages);box.innerHTML='';
  for(const s of S.stages){const l=document.createElement('label');if(!s.available)l.className='na';const c=document.createElement('input');c.type='checkbox';c.checked=!s.on;c.disabled=resultView()||!s.available;
   c.onchange=()=>cmd({cmd:'stage',name:s.name,on:!c.checked});l.appendChild(c);l.appendChild(document.createTextNode(s.name));box.appendChild(l)}}
 const label=$('viewName');label.hidden=!exploration;
 if(exploration)label.textContent=routed?'Routed result '+exploration+' · showing route input geometry':resultView()?'Result '+exploration+' · read only':'Exploring '+exploration;
 if(resultView()){
  for(const id of ['run','rp','tim','evaluate','legalize','freeze','no_sizing','no_buffer_edits','no_resynthesis',
    'ipreview','iapply','ipreserve','m_r','m_c','a_step','a_shr','a_grow','a_apply','a_cancel','cen_rel',
    'f_sel','f_view','o_cp','o_start'])$(id).disabled=true;
  for(const button of document.querySelectorAll('[data-a],[data-e]'))button.disabled=true;
 }
 pinned=new Set(S.pins||[]);dirty=true}
let haveFrame=-1;
function applyFrame(buf,q16){const dv=new DataView(buf),mg=String.fromCharCode(dv.getUint8(0),dv.getUint8(1),dv.getUint8(2),dv.getUint8(3)),n=dv.getUint32(4,true);let off;
 if(n!==D.n)return false;
 if(mg==='LHD1'){if(!X)return false;const m=dv.getUint32(24,true),idx=new Uint32Array(buf,32,m),px=new Float32Array(buf,32+m*4,m),py=new Float32Array(buf,32+m*8,m);PX=Float32Array.from(X);PY=Float32Array.from(Y);for(let k=0;k<m;k++){X[idx[k]]=px[k];Y[idx[k]]=py[k]}off=32+m*12}
 else{PX=X?Float32Array.from(X):null;PY=Y?Float32Array.from(Y):null;
  if(mg==='LHQ1'){const bx=new Float32Array(buf,16,4),qx=new Uint16Array(buf,32,n),qy=new Uint16Array(buf,32+n*2,n),kx=(bx[2]-bx[0])/65535,ky=(bx[3]-bx[1])/65535;X=new Float32Array(n);Y=new Float32Array(n);for(let i=0;i<n;i++){X[i]=bx[0]+qx[i]*kx;Y[i]=bx[1]+qy[i]*ky}off=32+n*4}
  else{X=new Float32Array(buf,16,n).slice();Y=new Float32Array(buf,16+n*4,n).slice();off=16+n*8}}
 if(drag&&drag.items&&drag.moved)for(const[i,p]of drag.cur){X[i]=p[0];Y[i]=p[1]}
 buildGrid();densFrame=-2;haveFrame=Number(dv.getBigUint64(8,true));
 if(buf.byteLength>=off+4){const m=dv.getUint32(off,true);if(m>0&&buf.byteLength>=off+4+m*12&&!(drag&&drag.cen!==undefined&&drag.moved)){C={x:new Float32Array(buf,off+4,m).slice(),y:new Float32Array(buf,off+4+m*4,m).slice(),r:new Float32Array(buf,off+4+m*8,m).slice()}}}
 return true}
function showTip(i){const t=$('tip');if(hover!==i)return;t.textContent=(names[i]??('cell #'+i))+(isSeq(i)?' (register)':'')+(pinned.has(i)?' (pinned)':'')}
async function fetchName(i){try{const r=await(await api('/node?i='+i)).json();if(r.name&&D&&rev===null){names[i]=r.name;showTip(i)}}catch(e){}}
function sendRestart(){if(selCp<0&&CP.length===0&&S.state!=='failed')return;const set={};for(const[k,v]of Object.entries(flagEdits))set[k]=v;cmd({cmd:'restart',checkpoint:selCp,set});$('msg').textContent='restarting...'}
function renderCp(){const key=JSON.stringify(CP.map(c=>[c.label,c.nth,c.snap]))+'|'+selCp+'|'+rev;if(key===cpKey)return;cpKey=key;const tl=$('tl');tl.innerHTML='';
 CP.forEach((c,i)=>{const e=document.createElement('span');e.className='cp'+(i===CP.length-1?' cur':'')+(i===selCp?' sel':'')+(c.snap?'':' nosnap');e.textContent=c.label+(c.nth>1?'#'+c.nth:'');e.title=c.label+' #'+c.nth+' (phase '+c.phase+', iteration '+c.iter+')'+(c.snap?'':' - snapshot released, still a restart point');
  e.onclick=()=>{selCp=i;cpKey='';renderCp();if(c.snap)review(i)};tl.appendChild(e);if(i<CP.length-1){const a=document.createElement('span');a.className='arrow';a.textContent='>';tl.appendChild(a)}});
 $('rp').disabled=selCp<0&&S.state!=='failed';$('live').style.display=rev!==null?'':'none'}
let mkKey='',tlTimer=null,TL=null,tlLast=0,tlIdx=null,tlWant=null,tlBusy=false,designCache={},shownScene=null,playT=null;
async function loadTimeline(){const r=await(await api('/timeline')).json();TL=r;tlLast=performance.now();renderScrub()}
function renderScrub(){const sc=$('scrub');if(!TL||!TL.e.length){sc.className='';return}sc.className='on';const n=TL.e.length,rng=$('sb_rng');rng.max=Math.max(0,n-1);const cur=tlIdx===null?n-1:tlIdx;if(!scrubbing)rng.value=cur;
 const m=$('sb_marks'),mk=TL.v+'|'+n+'|'+CP.length;if(mk!==mkKey){mkKey=mk;m.innerHTML='';TL.e.forEach((e,i)=>{const c=e[1]>=0?CP[e[1]]:null;if(!c)return;const k=document.createElement('span');k.className='mk '+(c.label==='gp_end'||c.label==='legalize'||c.label==='macro_legalize'||c.label==='edit'||c.label==='finished'?c.label:'gate');k.style.left=(n>1?100*i/(n-1):0)+'%';k.title=c.label+(c.nth>1?' #'+c.nth:'')+' (iteration '+e[0]+')';k.onclick=()=>scrubTo(i);m.appendChild(k)})}
 const e=TL.e[cur];$('sb_cnt').textContent=(TL.phases[e[3]]||'')+' iter '+e[0]+'   '+(cur+1)+'/'+n+(tlIdx===null?'  live':'')}
// Snapshots are fetched as the stored 16-bit frames (4 bytes per node), cached by identity (scene, phase, iteration,
// checkpoint) so thinning or new entries never confuse them, and the neighbours are prefetched: stepping and sliding
// are served from memory.
let tlCache=new Map(),tlBytes=0,tlPre=false,scrubbing=false;
const tlKey=e=>e[2]+':'+(TL.phases[e[3]]||'')+':'+e[0]+':'+e[1];
async function tlFrame(k){const e=TL.e[k],key=tlKey(e);let b=tlCache.get(key);if(b){tlCache.delete(key);tlCache.set(key,b);return b}
 b=await(await api('/frame?tl='+k+'&q=16')).arrayBuffer();tlCache.set(key,b);tlBytes+=b.byteLength;while(tlBytes>400e6&&tlCache.size>2){const old=tlCache.keys().next().value;tlBytes-=tlCache.get(old).byteLength;tlCache.delete(old)}return b}
async function tlDesign(k){const e=TL.e[k];let dd=designCache[e[2]];if(!dd){dd=await(await api('/design?tl='+k)).json();designCache[e[2]]=dd}return dd}
async function prefetchTl(){if(tlPre||!TL)return;tlPre=true;try{for(let r=1;r<=TL.e.length&&TL;r++){for(const k of[(tlIdx??TL.e.length-1)+r,(tlIdx??TL.e.length-1)-r]){if(k<0||k>=TL.e.length-1)continue;if(rev!==-2&&tlIdx===null&&r>12)continue;await tlDesign(k);await tlFrame(k);await new Promise(r=>setTimeout(r,5))}if(tlBytes>300e6)break}}catch(e){}tlPre=false}
async function scrubTo(i){if(!TL||!TL.e.length)return;const n=TL.e.length;i=Math.max(0,Math.min(n-1,i));if(i>=n-1){live();return}
 tlWant=i;if(tlBusy)return;tlBusy=true;
 try{while(tlWant!==null){const k=tlWant;tlWant=null;const e=TL.e[k];const dd=await tlDesign(k),buf=await tlFrame(k);D=dd;names=D.names;X=null;
   if(applyFrame(buf,true)){rev=-2;tlIdx=k;haveFrame=-1;if(shownScene!==e[2]){shownScene=e[2];sel.clear();selUi();fit()}cpKey='';$('msg').textContent='timeline: snapshot '+(k+1)+'/'+n+' (read-only)';renderScrub()}}}
 finally{tlBusy=false}renderCp();renderScrub();prefetchTl()}
function stopPlay(){if(playT){clearInterval(playT);playT=null;$('sb_play').innerHTML='&#9654;'}}
function stepTl(d){stopPlay();if(!TL)return;const n=TL.e.length;scrubTo((tlIdx===null?n-1:tlIdx)+d)}
function togglePlay(){if(playT){stopPlay();return}if(!TL||TL.e.length<2)return;$('sb_play').innerHTML='&#10074;&#10074;';if(tlIdx===null)tlIdx=0;playT=setInterval(()=>{const n=TL.e.length;if(tlIdx>=n-2){stopPlay();live();return}scrubTo(tlIdx+1)},120)}
async function review(i){const dd=await(await api('/design?cp='+i)).json();if(!dd.n){return}D=dd;names=D.names;const buf=await(await api('/frame?cp='+i)).arrayBuffer();X=null;if(applyFrame(buf,false)){rev=i;haveFrame=-1;sel.clear();selUi();fit();$('msg').textContent='reviewing checkpoint '+CP[i].label+' #'+CP[i].nth+' (read-only)';cpKey='';if(TL){const k=TL.e.findIndex(e=>e[1]===i);tlIdx=k>=0?k:null}renderCp();renderScrub()}}
function live(){stopPlay();mkKey='';hlV=-1;rev=null;tlIdx=null;shownScene=null;sceneId=-1;frameId=-1;haveFrame=-1;X=null;selCp=-1;cpKey='';$('msg').textContent='';renderCp();renderScrub()}
function optVal(f){return f.name in flagEdits?flagEdits[f.name]:f.value}
function optCounts(){const n=Object.keys(flagEdits).length;$('f_n').textContent=n?'('+n+' edited)':'';$('o_n').textContent=flagDefs?'('+flagDefs.length+' options'+(n?', '+n+' edited':'')+')':'';
 $('o_cp').disabled=selCp<0||!CP[selCp];$('o_cp').textContent=selCp>=0&&CP[selCp]?'Rerun from '+CP[selCp].label+(CP[selCp].nth>1?' #'+CP[selCp].nth:''):'Rerun from checkpoint';
 $('o_info').textContent=selCp>=0&&CP[selCp]?'checkpoint selected on the timeline: '+CP[selCp].label+' #'+CP[selCp].nth:'select a checkpoint on the timeline to rerun from it'}
function optRow(f,short){const row=document.createElement('div');row.className='orow'+(f.name in flagEdits?' chg':'');
 const nm=document.createElement('div');const code=document.createElement('code');code.textContent=short+(f.value!==f.default?' *':'');code.title=f.name+(f.ro?' (read only)':'');nm.appendChild(code);row.appendChild(nm);
 const cell=document.createElement('div'),h=document.createElement('div');h.className='h';h.textContent=f.help+' (default '+(f.default===''?'empty':f.default)+')';
 let inp;const set=v=>{if(v!==f.value)flagEdits[f.name]=v;else delete flagEdits[f.name];row.className='orow'+(f.name in flagEdits?' chg':'');optCounts()};
 if(f.kind==='boolean'){inp=document.createElement('input');inp.type='checkbox';inp.checked=optVal(f)==='true';inp.onchange=()=>set(inp.checked?'true':'false');const l=document.createElement('label');l.appendChild(inp);l.appendChild(document.createTextNode(inp.checked?' true':' false'));inp.addEventListener('change',()=>{l.lastChild.textContent=inp.checked?' true':' false'});cell.appendChild(l)}
 else{inp=document.createElement('input');inp.type='text';if(f.kind==='integer')inp.inputMode='numeric';inp.value=optVal(f);inp.onchange=()=>set(inp.value);cell.appendChild(inp)}
 if(f.ro)inp.disabled=true;
 if(!f.ro){const rs=document.createElement('button');rs.className='rs';rs.textContent='default';rs.title='back to '+(f.default===''?'empty':f.default);rs.onclick=()=>{set(f.default);if(f.kind==='boolean'){inp.checked=f.default==='true';inp.dispatchEvent(new Event('change'))}else inp.value=f.default};cell.appendChild(rs)}
 row.appendChild(cell);row.appendChild(h);return row}
function renderOptions(){const list=$('o_list');if(!flagDefs){list.textContent='-';return}const q=$('o_q').value.toLowerCase(),only=$('o_chg').checked;list.innerHTML='';const groups=new Map();
 for(const f of flagDefs){const short=f.name.replace(/^synth\.place\.?/,'')||'place';if(only&&!(f.name in flagEdits))continue;if(q&&!f.name.toLowerCase().includes(q)&&!f.help.toLowerCase().includes(q))continue;const g=short.includes('.')?short.split('.')[0]:'general';if(!groups.has(g))groups.set(g,[]);groups.get(g).push([f,short])}
 for(const[g,items]of[...groups].sort((a,b)=>a[0]==='general'?-1:b[0]==='general'?1:a[0]<b[0]?-1:1)){const d=document.createElement('details');d.open=!!q||only||g==='general';const sm=document.createElement('summary');sm.textContent=g+' ('+items.length+')';d.appendChild(sm);for(const[f,short]of items)d.appendChild(optRow(f,short));list.appendChild(d)}
 if(!groups.size)list.textContent='no match';optCounts()}
function openOptions(){$('opts').className='on';renderOptions()}
function rerun(cp){const set={};for(const[k,v]of Object.entries(flagEdits))set[k]=v;cmd({cmd:'restart',checkpoint:cp,set});$('opts').className='';$('msg').textContent='restarting...'}
async function sync(){for(;;){try{
 const r=await api('/state?since='+ver);S=await r.json();ver=S.v;if(S.hpwl<0)S.hpwl=null;
 if(S.epoch!==epoch){epoch=S.epoch;stripEpoch++;stripReport=null;$('faceSelector').hidden=true;$('faceMacro').replaceChildren();showMacroFaces();$('stripSelect').hidden=true;$('stripStatus').textContent='';selectStrip();D=null;X=null;Y=null;sceneId=-1;frameId=-1;haveFrame=-1;rev=null;selCp=-1;cpKey='';flagEdits={};flagDefs=null;TL=null;tlIdx=null;designCache={};tlCache=new Map();tlBytes=0;mkKey='';shownScene=null;try{xcap=(await xcommand({cmd:'hello',target:'main'})).capabilities;flagDefs=await(await api('/options')).json();renderOptions()}catch(e){}}
 if(rev===null&&S.scene!==sceneId){sceneId=S.scene;hlV=-1;if(S.scene>0){const prev=D;D=await(await api('/design')).json();showChunks(D.chunk_plan);names=D.names;PX=null;
   // A chunk edit replaces the scene in the same phase and die: keep the user's zoom and selection.
   if(!(prev&&prev.phase===D.phase&&String(prev.die)===String(D.die)&&prev.names?.length===D.names?.length)){sel.clear();selUi();fit()}}}
 if(rev===null&&S.frame!==frameId&&D&&D.n){frameId=S.frame;const q16=D.n>50000,buf=await(await api('/frame?'+(q16?'q=16&':'')+(haveFrame>0&&X?'since='+haveFrame:''))).arrayBuffer();if(!applyFrame(buf,q16))haveFrame=-1}
 if(S.regions_on){if(S.regions_v!==regV){regV=S.regions_v;R=await(await api('/regions')).json();$('reg_note').textContent=R.note||'';dirty=true}}else if(R){R=null;regV=-1;$('reg_note').textContent='';dirty=true}
 if(S.map&&S.map_v!==mapV){mapV=S.map_v;const mb=await(await api('/map')).arrayBuffer(),h=new DataView(mb);if(mb.byteLength>=24&&h.getUint32(4,true)>0){const bx=h.getUint32(4,true),by=h.getUint32(8,true);M={bx,by,peak:h.getFloat32(12,true),ovf:h.getFloat32(16,true),v:new Float32Array(mb.slice(24))};$('s_map').textContent='peak '+M.peak.toFixed(2)+' ovf '+M.ovf.toFixed(3)}else{M=null;$('s_map').textContent=S.map_note||'no data'}dirty=true}else if(!S.map&&M&&!(exploration&&S.state==='done')){M=null;mapV=S.map_v;$('s_map').textContent='';dirty=true}
 if(S.hl_v!==undefined&&S.hl_v!==hlV&&rev===null)await loadHighlights();if(S.sel_v!==undefined&&S.sel_v!==selV){if(selExpect>0){selExpect--;selV=S.sel_v}else if(D&&rev===null){selV=S.sel_v;await loadSelection()}else selV=S.sel_v}
 CP=S.cps||[];if(S.tl_n>0&&(!TL||TL.e.length!==S.tl_n)){if(!TL||performance.now()-tlLast>500)await loadTimeline();else if(!tlTimer)tlTimer=setTimeout(async()=>{tlTimer=null;try{await loadTimeline()}catch(e){}},600)}else if(TL)renderScrub();renderCp();{const e=$('err'),f=S.state==='failed';e.style.display=f?'':'none';if(f){e.textContent=S.error;$('rp').disabled=false;$('rp').textContent='Restart (edited flags)';$('run').disabled=true}else if($('rp').textContent!=='Replay from here'){$('rp').textContent='Replay from here'}}ui();if(exploration&&S.state==='done')return}catch(e){console.error(e);window.__lastError=String(e&&e.stack||e);ver=0;$('msg').textContent='disconnected';await new Promise(r=>setTimeout(r,1000))}}}
sync();
</script></body></html>)HTML";
  return page;
}
}  // namespace livehd::placement::gui

// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "magnet_report.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
namespace livehd::placement {
void write_magnet_report(const DreamPlace::native::MagnetReport& report, const std::vector<std::string>& names, int64_t dbu,
                         const std::string& directory) {
  namespace dp = DreamPlace::native;
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  const auto                                 sources = [&](const std::vector<int>& members) {
    w.StartArray();
    for (auto i : members) {
      w.String(names.at(i).c_str());
    }
    w.EndArray();
  };
  w.StartObject();
  w.Key("schema_version");
  w.Int(1);
  w.Key("scope");
  w.String(report.options.enabled ? "global rigid macro magnetization; colors and hierarchy do not restrict selection"
                                  : "macro movement and regional cell density; magnetization disabled");
  w.Key("enabled");
  w.Bool(report.options.enabled);
  w.Key("dbu_per_um");
  w.Int64(dbu);
  w.Key("target_density");
  w.Double(report.target_density);
  w.Key("density_fraction");
  w.Double(report.options.density_fraction);
  w.Key("settle_window");
  w.Int(report.options.settle_window);
  w.Key("settle_displacement");
  w.Double(report.options.settle_displacement);
  w.Key("before_snap");
  w.Bool(report.options.before_snap);
  w.Key("start_displacement");
  w.Double(report.options.start_displacement);
  w.Key("start_iteration");
  if (report.start_iteration < 0) {
    w.Null();
  } else {
    w.Int(report.start_iteration);
  }
  w.Key("start_observed_displacement");
  if (report.start_iteration < 0 || report.options.before_snap) {
    w.Null();
  } else {
    w.Double(report.start_observed_displacement);
  }
  double stability_reference = 0;
  for (const auto& sample : report.motion_history) {
    if (sample.center_step_valid && sample.iteration >= 1 && sample.iteration <= 50) {
      stability_reference = std::max(stability_reference, sample.center_step_sum);
    }
  }
  w.Key("macro_stability");
  w.StartObject();
  w.Key("definition");
  w.String("sum of consecutive physical SRAM center distances, normalized to the maximum in GP iterations 1..50");
  w.Key("reference_iterations");
  w.Int(50);
  w.Key("reference_peak_dbu");
  w.Double(stability_reference);
  w.EndObject();
  w.Key("warp_stability");
  w.StartArray();
  std::ifstream warp_trace(directory + "/macro_warp_stability.jsonl");
  for (std::string line; std::getline(warp_trace, line);) {
    rapidjson::Document sample;
    sample.Parse<rapidjson::kParseFullPrecisionFlag>(line.c_str());
    if (sample.HasParseError() || !sample.IsObject()) {
      throw std::runtime_error("invalid warpspace stability trace");
    }
    sample.Accept(w);
  }
  w.EndArray();
  w.Key("motion_history");
  w.StartArray();
  for (const auto& sample : report.motion_history) {
    w.StartObject();
    w.Key("iteration");
    w.Int(sample.iteration);
    w.Key("maximum");
    w.Double(sample.movement.maximum);
    w.Key("rms");
    w.Double(sample.movement.rms);
    w.Key("window_complete");
    w.Bool(sample.movement.window_complete);
    w.Key("center_step_sum_dbu");
    w.Double(sample.center_step_sum);
    w.Key("center_step_valid");
    w.Bool(sample.center_step_valid);
    w.Key("macro_stability_percent");
    if (sample.center_step_valid && stability_reference > 0) {
      w.Double(100 * sample.center_step_sum / stability_reference);
    } else {
      w.Null();
    }
    w.Key("physical_macro_count");
    w.Int(sample.physical_macros);
    w.Key("gp_macro_count");
    w.Int(sample.gp_macros);
    w.EndObject();
  }
  w.EndArray();
  w.Key("overflow_history");
  w.StartArray();
  for (const auto& [iteration, overflow] : report.overflow_history) {
    w.StartArray();
    w.Int(iteration);
    w.Double(overflow);
    w.EndArray();
  }
  w.EndArray();
  w.Key("layout");
  w.StartArray();
  for (auto x : {report.layout.xl, report.layout.yl, report.layout.xh, report.layout.yh}) {
    w.Double(x);
  }
  w.EndArray();
  w.Key("stopped_settled");
  w.Bool(report.stopped_settled);
  w.Key("seconds");
  w.Double(report.seconds);
  w.Key("events");
  w.StartArray();
  for (const auto& event : report.events) {
    w.StartObject();
    w.Key("iteration");
    w.Int(event.iteration);
    w.Key("event");
    w.String(dp::magnet_event_name(event.kind));
    w.EndObject();
  }
  w.EndArray();
  w.Key("rounds");
  w.StartArray();
  for (const auto& round : report.rounds) {
    w.StartObject();
    w.Key("iteration");
    w.Int(round.iteration);
    w.Key("overflow");
    w.Double(round.overflow);
    w.Key("macro_move_max");
    w.Double(round.movement.maximum);
    w.Key("macro_move_rms");
    w.Double(round.movement.rms);
    w.Key("status");
    w.String(round.reason.c_str());
    w.Key("search_seconds");
    w.Double(round.search.seconds);
    w.Key("exact_density");
    w.Bool(round.search.exact_density);
    w.Key("global_cell_density");
    w.Double(round.search.global_cell_density);
    w.Key("density_limit");
    w.Double(round.search.density_limit);
    w.Key("group_ids");
    w.StartArray();
    for (auto id : round.group_ids) {
      w.Uint64(id);
    }
    w.EndArray();
    w.Key("candidates");
    w.StartArray();
    for (const auto& candidate : round.search.candidates) {
      w.StartObject();
      w.Key("a");
      sources(candidate.source_a);
      w.Key("b");
      sources(candidate.source_b);
      w.Key("axis");
      w.String(candidate.axis == dp::Axis::x ? "x" : "y");
      w.Key("gap");
      w.Double(candidate.gap);
      w.Key("gap_density");
      w.Double(candidate.gap_density);
      w.Key("macro_move_a");
      w.Double(candidate.move_a);
      w.Key("macro_move_b");
      w.Double(candidate.move_b);
      w.Key("gap_region");
      w.StartArray();
      for (const auto& [x, y] : candidate.gap_region) {
        w.StartArray();
        w.Double(x);
        w.Double(y);
        w.EndArray();
      }
      w.EndArray();
      w.Key("displacement");
      w.Double(candidate.displacement);
      w.Key("center_shift");
      w.StartArray();
      w.Double(candidate.center_shift_x);
      w.Double(candidate.center_shift_y);
      w.EndArray();
      w.Key("merged_box");
      w.StartArray();
      for (double value : {candidate.merged_box.xl, candidate.merged_box.yl, candidate.merged_box.xh, candidate.merged_box.yh}) {
        w.Double(value);
      }
      w.EndArray();
      w.Key("selected");
      w.Bool(candidate.selected);
      w.Key("reason");
      w.String(candidate.reason.c_str());
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.Key("groups");
  w.StartArray();
  for (const auto& group : report.mapping.groups) {
    w.StartObject();
    w.Key("id");
    w.Uint64(group.id);
    w.Key("members");
    w.StartArray();
    for (const auto& member : group.members) {
      w.StartObject();
      w.Key("name");
      w.String(names.at(member.source).c_str());
      w.Key("x_offset");
      w.Double(member.x);
      w.Key("y_offset");
      w.Double(member.y);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.Key("frames");
  w.StartArray();
  for (const auto& frame : report.frames) {
    w.StartObject();
    w.Key("iteration");
    w.Int(frame.iteration);
    w.Key("event");
    w.String(frame.event.c_str());
    w.Key("macros");
    w.StartArray();
    for (const auto& macro : frame.macros) {
      w.StartObject();
      w.Key("name");
      w.String(names.at(macro.source).c_str());
      w.Key("x");
      w.Double(macro.x);
      w.Key("y");
      w.Double(macro.y);
      w.Key("width");
      w.Double(macro.width);
      w.Key("height");
      w.Double(macro.height);
      constexpr const char* orientations[] = {"N", "FN", "FS", "S"};
      w.Key("orientation");
      w.String(orientations[static_cast<unsigned>(macro.orientation)]);
      w.Key("group");
      w.Uint64(macro.group);
      w.Key("fixed");
      w.Bool(macro.fixed);
      w.EndObject();
    }
    w.EndArray();
    w.Key("bins_x");
    w.Int(frame.bins_x);
    w.Key("bins_y");
    w.Int(frame.bins_y);
    w.Key("density");
    w.StartArray();
    for (auto rho : frame.density) {
      w.Double(rho);
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  const std::string data = buffer.GetString();
  std::ofstream     json(directory + "/magnet.json");
  if (!(json << data << '\n')) {
    throw std::runtime_error("cannot write macro magnet report");
  }
  // Embed JSON safely even when a user instance name contains HTML text.
  std::string embedded;
  for (char c : data) {
    embedded += c == '<' ? "\\u003c" : std::string(1, c);
  }
  std::ofstream html(directory + "/macro_animation.html");
  html
      << R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Macro movement and regional cell density</title><style>
body{font:15px system-ui;margin:24px;background:#101925;color:#e5edf5}button,input,select{font:inherit;margin:4px}canvas{display:block;background:#142236;max-width:100%;border:1px solid #567}#status{white-space:pre-wrap}label{margin-right:16px}.legend{background:linear-gradient(90deg,#142236,#24969c,#f3d35d,#ff4536);width:300px;height:12px}details{margin:12px 0}pre{max-height:240px;overflow:auto}</style>
<h1>Macro movement and cell density</h1><p>Only macros are drawn. The background is movable standard-cell footprint density; macros, halos and fillers are excluded. Dashed outlines identify rigid groups.</p>
<button id="prev">Previous</button><button id="play">Play</button><button id="next">Next</button><input id="seek" type="range" min="0" value="0"><label>Speed <select id="speed"><option>2</option><option selected>5</option><option>10</option><option>20</option></select> frames/s</label><label><input id="names" type="checkbox">Names</label>
<p><label>Inspect recorded decision <select id="candidate"><option value="">None</option></select></label></p><p id="candidate_status"></p>
<p><label>Jump to feedback <select id="feedback"><option value="">None</option></select></label></p><p id="gate_status"></p>
<p id="status"></p><canvas id="view" width="1100" height="800"></canvas><p>Cell density: 0 → target → 2× target</p><div class="legend"></div><details><summary>Feedback events and candidate decisions</summary><pre id="decisions"></pre></details>
<script id="data" type="application/json">)HTML"
      << embedded << R"HTML(</script><script>
const d=JSON.parse(document.getElementById('data').textContent),frames=d.frames;
const $=id=>document.getElementById(id),c=$('view'),ctx=c.getContext('2d');let index=0,timer=null;
const stabilityReference=d.macro_stability?.reference_peak_dbu??(d.motion_history||[]).reduce((peak,s)=>s.center_step_valid&&s.iteration>=1&&s.iteration<=50?Math.max(peak,s.center_step_sum_dbu):peak,0);
const choices=d.rounds.flatMap(r=>r.candidates.map(k=>({r,k})));let chosen=null;
for(let i=0;i<choices.length;i++){const {r,k}=choices[i],o=document.createElement('option');o.value=String(i);o.textContent=`${r.iteration}: ${k.a.join(' + ')} / ${k.b.join(' + ')} · ${k.axis} · ${k.reason}`;$('candidate').appendChild(o);}$('candidate').value='';
// Index recorded geometry once: Eyeriss can have thousands of decisions.
// Several before-snap rounds share one iteration but test different poses.
const byIteration=new Map();for(let i=0;i<frames.length;i++){const f=frames[i];if(!byIteration.has(f.iteration))byIteration.set(f.iteration,{before:[],merged:[],snap:[],gp:-1});const a=byIteration.get(f.iteration);if(f.event==='before_magnet')a.before.push(i);if(f.event==='magnet')a.merged.push(i);if(f.event==='snap'||f.event==='snap_disabled')a.snap.push(i);if(f.event==='global placement'&&a.gp<0)a.gp=i;}
const roundFrames=new Map(),roundsAtFrame=new Map(),mergedCounts=new Map();
for(const r of d.rounds){const ordinal=mergedCounts.get(r.iteration)||0,a=byIteration.get(r.iteration);let j=a?.before[ordinal]??-1;if(j<0&&d.before_snap&&ordinal)j=a?.merged[ordinal-1]??-1;if(j<0)j=a?.gp??-1;roundFrames.set(r,j);if(!roundsAtFrame.has(j))roundsAtFrame.set(j,[]);roundsAtFrame.get(j).push(r);if(r.status==='merged')mergedCounts.set(r.iteration,ordinal+1);}
const decisionFrame=round=>roundFrames.get(round)??-1;
const warpStateAt=i=>(d.warp_stability||[]).filter(s=>s.iteration<=i).at(-1);
const eventName=e=>e.event==='pull'?'Warpspace target update'+(warpStateAt(e.iteration)?' · strength '+warpStateAt(e.iteration).weight.toFixed(2)+' · '+warpStateAt(e.iteration).decision.replaceAll('_',' '):''):e.event==='ready'?(d.before_snap?'Magnet enabled before snap':'Global magnet gate opens'):e.event==='magnet'?'Rigid merge':e.event==='snap'?'Macro snap/LP':e.event;
const eventFrames=[],eventsAtFrame=new Map(),eventCounts=new Map();
for(let j=0;j<d.events.length;j++){const e=d.events[j],key=e.iteration+':'+e.event,ordinal=eventCounts.get(key)||0,a=byIteration.get(e.iteration);eventCounts.set(key,ordinal+1);let i=e.event==='magnet'?a?.merged[ordinal]??-1:e.event==='snap'?a?.snap[ordinal]??-1:-1;
if(i<0){let lo=0,hi=frames.length;while(lo<hi){const mid=(lo+hi)>>>1;if(frames[mid].iteration<e.iteration)lo=mid+1;else hi=mid;}i=lo<frames.length?lo:-1;}eventFrames.push(i);if(!eventsAtFrame.has(i))eventsAtFrame.set(i,[]);eventsAtFrame.get(i).push(e);
const o=document.createElement('option');o.value=String(j);o.textContent=`${e.iteration}: ${eventName(e)}`;$('feedback').appendChild(o);}$('feedback').value='';
const eventFrame=eventIndex=>eventFrames[eventIndex]??-1;
$('seek').max=Math.max(0,frames.length-1);$('seek').style.width='min(400px,60vw)';
function color(v){let t=Math.max(0,Math.min(1,v/(2*d.target_density)));const stops=[[20,34,54],[36,150,156],[243,211,93],[255,69,54]],z=t*3,k=Math.min(2,Math.floor(z)),u=z-k;return `rgb(${stops[k].map((v,i)=>Math.round(v+(stops[k+1][i]-v)*u)).join(',')})`;}
function draw(){if(!frames.length)return;const f=frames[index],[xl,yl,xh,yh]=d.layout,s=Math.min((c.width-40)/(xh-xl),(c.height-40)/(yh-yl)),ox=(c.width-(xh-xl)*s)/2,oy=(c.height-(yh-yl)*s)/2;
const X=x=>ox+(x-xl)*s,Y=y=>c.height-oy-(y-yl)*s;ctx.clearRect(0,0,c.width,c.height);
const bw=(xh-xl)/f.bins_x,bh=(yh-yl)/f.bins_y;
for(let x=0;x<f.bins_x;x++)for(let y=0;y<f.bins_y;y++){ctx.fillStyle=color(f.density[x*f.bins_y+y]);ctx.fillRect(X(xl+x*bw),Y(yl+(y+1)*bh),bw*s+.5,bh*s+.5);}
const groups=new Map();ctx.font='11px system-ui';
for(const m of f.macros){ctx.fillStyle=m.fixed?'#96a4b7':'#94caff';ctx.globalAlpha=.83;ctx.fillRect(X(m.x),Y(m.y+m.height),m.width*s,m.height*s);ctx.globalAlpha=1;ctx.strokeStyle='#061019';ctx.setLineDash([]);ctx.strokeRect(X(m.x),Y(m.y+m.height),m.width*s,m.height*s);if($('names').checked){ctx.fillStyle='#fff';ctx.fillText(m.name,X(m.x)+2,Y(m.y+m.height)+12);}
if(m.group){let g=groups.get(m.group)||[Infinity,Infinity,-Infinity,-Infinity];g=[Math.min(g[0],m.x),Math.min(g[1],m.y),Math.max(g[2],m.x+m.width),Math.max(g[3],m.y+m.height)];groups.set(m.group,g);}}
ctx.setLineDash([6,3]);ctx.lineWidth=2;ctx.strokeStyle='#fff';for(const [id,g]of groups){ctx.strokeRect(X(g[0])-2,Y(g[3])-2,(g[2]-g[0])*s+4,(g[3]-g[1])*s+4);ctx.fillStyle='#fff';ctx.fillText('G'+id,X(g[0]),Y(g[3])-5);}ctx.lineWidth=1;ctx.setLineDash([]);
if(chosen){const {r,k}=chosen,q=k.gap_region,atDecision=index===decisionFrame(r);let info=`Decision @ ${r.iteration}: ${k.a.join(' + ')} / ${k.b.join(' + ')} · ${k.axis} · ${k.reason.replaceAll('_',' ')} · gap density ${k.gap_density.toFixed(4)}${r.density_limit===undefined?'':` / limit ${r.density_limit.toFixed(4)} (global ${r.global_cell_density.toFixed(4)} × ${d.density_fraction})`}`;
if(k.macro_move_a!==undefined)info+=` · macro motion ${(100*k.macro_move_a).toFixed(2)}%, ${(100*k.macro_move_b).toFixed(2)}% (each limit ${(100*d.settle_displacement).toFixed(1)}%)`;
if(q&&q.length===4){const orth=k.axis==='x'?1:0,a=k.axis==='x'?[0,3]:[0,1],b=k.axis==='x'?[1,2]:[3,2],offset=Math.abs((q[a[0]][orth]+q[a[1]][orth]-q[b[0]][orth]-q[b[1]][orth])/2),limit=Math.abs(q[a[1]][orth]-q[a[0]][orth])/2;info+=` · orthogonal offset ${(offset/d.dbu_per_um).toFixed(2)} µm (limit ${(limit/d.dbu_per_um).toFixed(2)} µm)`;
if(atDecision){ctx.beginPath();q.forEach((p,i)=>i?ctx.lineTo(X(p[0]),Y(p[1])):ctx.moveTo(X(p[0]),Y(p[1])));ctx.closePath();ctx.fillStyle=k.selected?'#55ff99':'#ff7788';ctx.strokeStyle=ctx.fillStyle;ctx.globalAlpha=.3;ctx.fill();ctx.globalAlpha=1;ctx.lineWidth=3;ctx.stroke();ctx.lineWidth=1;}}
if(k.center_shift)info+=` · core translation ${k.center_shift.map(v=>(v/d.dbu_per_um).toFixed(3)).join(', ')} µm`;
if(atDecision&&k.merged_box){const [a,b,e,g]=k.merged_box;ctx.strokeStyle='#47e4ff';ctx.setLineDash([3,4]);ctx.strokeRect(X(a),Y(g),(e-a)*s,(g-b)*s);ctx.setLineDash([]);}
$('candidate_status').textContent=info+(atDecision?' · Corridor: green/red; proposed padded union: cyan.':decisionFrame(r)<0?' · Exact decision pose was not recorded; overlay unavailable.':' · Select this decision again to return to its recorded geometry.');}else $('candidate_status').textContent='Group interiors keep both member halos. The channel between member bodies belongs to the rigid group.';
const events=eventsAtFrame.get(index)||[],rounds=roundsAtFrame.get(index)||[];
const motion=(d.motion_history||[]).filter(v=>v.iteration<=f.iteration).at(-1),step=(d.motion_history||[]).filter(v=>v.center_step_valid&&v.iteration<=f.iteration).at(-1),reference=stabilityReference,warp=d.events.filter(e=>e.event==='pull'&&e.iteration<=f.iteration).at(-1);
const stability=`Macro stability (last GP step): ${step&&reference>0?(100*step.center_step_sum_dbu/reference).toFixed(2)+'%':'not available'} · Total center movement: ${step?(step.center_step_sum_dbu/d.dbu_per_um).toFixed(3)+' µm':'not available'}`;
const gate=d.enabled===false?'Magnet disabled':d.before_snap?'Magnet before snap; movement gates unused':`Global max ${motion&&motion.window_complete?(100*motion.maximum).toFixed(2)+'%':'window incomplete'} · RMS ${motion&&motion.window_complete?(100*motion.rms).toFixed(2)+'%':'window incomplete'} · ${(100*d.start_displacement).toFixed(1)}% start gate ${d.start_iteration===null?'not reached':f.iteration<d.start_iteration?'waiting (opens @ '+d.start_iteration+')':'open since '+d.start_iteration} · Local limit ${(100*d.settle_displacement).toFixed(1)}%`;
const warpState=warpStateAt(f.iteration);$('gate_status').textContent=stability+(warpState?` · Warpspace strength ${warpState.weight.toFixed(2)} · ${warpState.decision.replaceAll('_',' ')}${warpState.macro_stability_fraction===null?'':` (window ${(100*warpState.macro_stability_fraction).toFixed(2)}%)`}`:'')+' · '+gate+` · Last warpspace target update: ${warp?warp.iteration:'not applied yet'}`;
$('status').textContent=`Frame ${index+1}/${frames.length} · iteration ${f.iteration} · ${f.event} · ${groups.size} rigid groups\n`+events.map(e=>`${eventName(e)} @ ${e.iteration}`).join(' · ');
$('decisions').textContent=JSON.stringify({events,rounds},null,2);$('seek').value=index;}
function stop(){clearInterval(timer);timer=null;$('play').textContent='Play';}function play(){stop();$('play').textContent='Pause';timer=setInterval(()=>{if(index===frames.length-1){stop();return;}index++;draw();},1000/+$('speed').value);}
$('play').onclick=()=>timer?stop():play();$('prev').onclick=()=>{stop();index=Math.max(0,index-1);draw();};$('next').onclick=()=>{stop();index=Math.min(frames.length-1,index+1);draw();};$('seek').oninput=e=>{stop();index=+e.target.value;draw();};$('speed').onchange=()=>{if(timer)play();};$('names').onchange=draw;draw();
$('candidate').onchange=()=>{stop();$('feedback').value='';chosen=$('candidate').value===''?null:choices[+$('candidate').value];if(chosen){const j=decisionFrame(chosen.r);if(j>=0)index=j;$('names').checked=true;}draw();};
$('feedback').onchange=()=>{if($('feedback').value==='')return;stop();chosen=null;$('candidate').value='';const j=eventFrame(+$('feedback').value);if(j>=0)index=j;draw();};
</script></html>)HTML";
  if (!html) {
    throw std::runtime_error("cannot write macro movement animation");
  }
}
}  // namespace livehd::placement

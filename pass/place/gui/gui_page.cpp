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
#panel h4{margin:12px 0 4px;font-size:11px;letter-spacing:.06em;text-transform:uppercase;color:var(--mut)} #panel h4:first-child{margin-top:0}
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
<div id="bar"><button id="run" class="primary">Run</button><button id="cont" style="display:none">Continue</button><button id="fit">Fit</button><button id="zin" title="zoom in (z or +)">+</button><button id="zout" title="zoom out (Z or -)">&minus;</button><button id="shot">Screenshot</button><button id="live" style="display:none">Live</button><button id="rp" disabled title="Re-run the flow with the recorded actions up to the selected checkpoint, using the flags edited in the panel">Replay from here</button><button id="otog" title="synth.place options: edit and rerun">Options</button><button id="ptog">Panel</button><span id="msg" class="mut"></span></div>
<div id="tl"></div>
<div id="scrub"><button id="sb_start" title="first snapshot">|&lt;</button><button id="sb_back" title="one snapshot back (left arrow)">&lt;</button><button id="sb_play" title="play the recorded history like a video">&#9654;</button><button id="sb_fwd" title="one snapshot forward (right arrow)">&gt;</button><button id="sb_end" title="back to live">&gt;|</button><span class="sbox"><input type="range" id="sb_rng" min="0" max="0" value="0"><div id="sb_marks"></div></span><span id="sb_cnt" class="mut"></span></div>
<div id="err" style="display:none;position:absolute;left:12px;top:150px;max-width:calc(100% - 330px);white-space:pre-wrap;background:var(--pan);border:2px solid var(--bad);border-radius:8px;padding:10px 14px;color:var(--fg);font-size:13px"></div>
<div id="panel">
<h4>Status</h4>
<div class="row"><span>phase</span><span id="s_phase">-</span></div>
<div class="row"><span>state</span><span id="s_state">-</span></div>
<div class="row"><span>iteration</span><span id="s_iter">-</span></div>
<div class="row"><span>pending edits</span><span id="s_pend">0</span></div>
<h4>Stats</h4>
<div class="row"><span>HPWL (um)</span><span id="s_hpwl">-</span></div>
<div class="row"><span>overflow</span><span id="s_ovf">-</span></div>
<div class="row"><span>Freq (MHz)</span><span id="s_freq">-</span></div>
<div class="row"><span>WNS (ps)</span><span id="s_wns">-</span></div>
<div class="row"><span>TNS (ps)</span><span id="s_tns">-</span></div>
<div class="row"><span>timing from iter</span><span id="s_titer">-</span></div>
<div class="row"><span></span><span><button id="tim" style="padding:0 8px;font-size:12px">Refresh timing</button></span></div>
<h4>Stop before stage</h4><div id="stages" class="mut">-</div>
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
<h4>Layers</h4>
<label><input type="checkbox" id="l_comb" checked>comb</label>
<label><input type="checkbox" id="l_regs" checked><span style="color:var(--seq)">regs</span></label>
<label><input type="checkbox" id="l_macros" checked>macros</label>
<label><input type="checkbox" id="l_io" checked>IO</label>
<label><input type="checkbox" id="l_reg">regions <span class="mut" style="font-size:11px">(cuts and boundary pins)</span></label><div id="reg_note" class="mut" style="font-size:11px;margin-left:20px"></div>
<label><input type="checkbox" id="l_cen" checked>centroids <button id="cen_rel" style="padding:0 6px;font-size:11px">release</button></label>
<label><input type="checkbox" id="l_dens">density</label>
<label>congestion <select id="l_map"><option value="none">off</option><option value="rudy">RUDY</option><option value="rsmt">RSMT</option><option value="uroute">URoute</option></select> <span id="s_map" class="mut"></span></label>
<label><input type="checkbox" id="l_disp">displacement</label>
<h4>Highlights</h4><div id="hls" class="mut" style="font-size:12px">none (an assistant can paint cells here)</div>
<h4>Selection (<span id="s_sel">0</span>)</h4>
<div id="al">
<button data-a="left">Left</button><button data-a="hcenter">H center</button><button data-a="right">Right</button>
<button data-a="top">Top</button><button data-a="vcenter">V center</button><button data-a="bottom">Bottom</button>
<button data-a="disth">Distribute H</button><button data-a="distv">Distribute V</button><button data-a="unpin">Unpin</button>
</div>
<div class="row" style="margin-top:6px;align-items:center"><span>matrix</span><span><input type="number" id="m_r" value="2" min="1"> x <input type="number" id="m_c" value="2" min="1"> <button data-a="matrix">Go</button></span></div>
<h4>Options <span id="f_n" class="mut"></span></h4>
<button id="o_open" style="width:100%">synth.place options&hellip;</button>
<div class="mut" style="font-size:11px;margin-top:3px">Every <code>lhd list options synth.place</code> entry, editable; rerun from the start or from the selected checkpoint.</div>
<h4>Help</h4>
<div class="mut">Drag an object to move and pin it. Shift+click adds to the selection, shift+drag draws a box. Drag empty space to pan, wheel to zoom. z / + zoom in, shift+Z / - zoom out (around the pointer; the + and - buttons too). The slider under the toolbar replays the recorded history (left/right arrows step, the play button runs it like a video, markers are the phases). Space runs/pauses. U unpins, F fits, Esc clears.</div>
</div>
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
function api(p,o){return fetch(p+(p.includes('?')?'&':'?')+'t='+T,o)}
let aEdge='right',aStepSet=false,aPend=null,aBase='';
const areaBox=()=>aPend||D.die;
function areaCancel(){if(aPend){aPend=null;dirty=true;areaInfo()}}
function areaSet(edge,v){if(!aPend){aPend=D.die.slice();aBase=D.die.join()}const m=2*D.dbu;if(edge==='left')aPend[0]=Math.min(v,aPend[2]-m);else if(edge==='right')aPend[2]=Math.max(v,aPend[0]+m);else if(edge==='bottom')aPend[1]=Math.min(v,aPend[3]-m);else aPend[3]=Math.max(v,aPend[1]+m);aEdge=edge;dirty=true;areaInfo()}
function areaNudge(sign){const st=parseFloat($('a_step').value);if(!(st>0)){$('msg').textContent='area: give a step in um';return}const b=areaBox(),d=sign*st*D.dbu;
 areaSet(aEdge,aEdge==='left'?b[0]-d:aEdge==='right'?b[2]+d:aEdge==='bottom'?b[1]-d:b[3]+d)}
function areaProblem(){if(!aPend)return'';const[xl,yl,xh,yh]=aPend,w=xh-xl,h=yh-yl;if(D.cell_area>0&&D.cell_area/(w*h)>0.98)return'the cells would fill '+fmt(100*D.cell_area/(w*h),0)+' % of it (limit 98 %)';
 if(X)for(const i of pinned){if(i<X.length&&(X[i]<xl-0.5||Y[i]<yl-0.5||X[i]+D.w[i]>xh+0.5||Y[i]+D.h[i]>yh+0.5))return'a pinned node would lie outside (move or unpin it)'}return''}
function areaApply(){if(!aPend)return;const bad=areaProblem();if(bad){$('msg').textContent='area: '+bad;return}const box=aPend.map(v=>v/D.dbu);aPend=null;dirty=true;cmd({cmd:'area',box});$('msg').textContent='re-placing with the new area...';areaInfo()}
function areaInfo(){if(!D||!D.die||!D.dbu)return;if(aPend&&aBase!==D.die.join())aPend=null;const[xl,yl,xh,yh]=D.die,w=(xh-xl)/D.dbu,h=(yh-yl)/D.dbu,fill=(a,b)=>D.cell_area>0?fmt(100*D.cell_area/(a*b),1)+' %':'-';
 $('a_size').textContent=fmt(w,1)+' x '+fmt(h,1);
 if(aPend){const nw=(aPend[2]-aPend[0])/D.dbu,nh=(aPend[3]-aPend[1])/D.dbu;$('a_new').textContent=fmt(nw,1)+' x '+fmt(nh,1)+' ('+fmt(100*nw*nh/(w*h)-100,0)+' %)';$('a_fill').textContent=fill(xh-xl,yh-yl)+' -> '+fill(aPend[2]-aPend[0],aPend[3]-aPend[1])}
 else{$('a_new').textContent='-';$('a_fill').textContent=fill(xh-xl,yh-yl)}
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
async function cmd(o){try{const r=await api('/cmd',{method:'POST',body:JSON.stringify(o)});if(!r.ok){$('msg').textContent=(await r.json()).error||'error'}}catch(e){$('msg').textContent='disconnected'}}
const lComb=$('l_comb'),lRegs=$('l_regs'),cellOn=i=>isSeq(i)?lRegs.checked:lComb.checked,isSeq=i=>!!D.seq&&D.seq.charCodeAt(i)===49,kind=i=>D.kinds.charCodeAt(i)-48,isMovable=i=>kind(i)<5,kd=i=>kind(i)%5;
const nm=i=>names[i]??('#'+i);
function resize(){dpr=window.devicePixelRatio||1;cv.width=cv.clientWidth*dpr;cv.height=cv.clientHeight*dpr;dirty=true}
window.addEventListener('resize',resize);resize();
function fit(){if(!D)return;const[xl,yl,xh,yh]=D.die,w=cv.clientWidth,h=cv.clientHeight,pad=40;
 const pw=$('panel').style.display==='none'?0:290,aw=w-pw,s=Math.min((aw-2*pad)/Math.max(1,xh-xl),(h-2*pad)/Math.max(1,yh-yl));view.s=s;view.ox=(aw-(xh-xl)*s)/2-xl*s;view.oy=(h+(yh-yl)*s)/2+yl*s;dirty=true}
const sx=x=>x*view.s+view.ox,sy=y=>view.oy-y*view.s,wx=x=>(x-view.ox)/view.s,wy=y=>(view.oy-y)/view.s;
let R=null,regV=-1,M=null,mapV=-1,C=null,epoch=null,CP=[],selCp=-1,rev=null,flagDefs=null,flagEdits={},cpKey="";
let dens={b:48,v:null,max:1},densFrame=-2;
function densMap(){const B=dens.b,v=new Float32Array(B*B),[xl,yl,xh,yh]=D.die,bw=(xh-xl)/B,bh=(yh-yl)/B;
 for(let i=0;i<X.length;i++){const k=kd(i);if(k===4||D.w[i]<=0)continue;const x0=X[i],x1=x0+D.w[i],y0=Y[i],y1=y0+D.h[i];
  const i0=Math.max(0,Math.floor((x0-xl)/bw)),i1=Math.min(B-1,Math.floor((x1-xl)/bw)),j0=Math.max(0,Math.floor((y0-yl)/bh)),j1=Math.min(B-1,Math.floor((y1-yl)/bh));
  for(let j=j0;j<=j1;j++)for(let a=i0;a<=i1;a++){const ox=Math.min(x1,xl+(a+1)*bw)-Math.max(x0,xl+a*bw),oy=Math.min(y1,yl+(j+1)*bh)-Math.max(y0,yl+j*bh);if(ox>0&&oy>0)v[a+j*B]+=ox*oy}}
 let m=0;for(let i=0;i<v.length;i++){v[i]/=bw*bh;if(v[i]>m)m=v[i]}dens.v=v;dens.max=Math.max(1,m);densFrame=frameId}
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
function draw(){dirty=false;const W=cv.clientWidth,H=cv.clientHeight;ctx.setTransform(dpr,0,0,dpr,0,0);ctx.fillStyle=css('--bg');ctx.fillRect(0,0,W,H);
 if(!D||!X){ctx.fillStyle=css('--mut');ctx.fillText('waiting for the placer...',20,60);return}
 const[xl,yl,xh,yh]=D.die;ctx.fillStyle=css('--die');ctx.fillRect(sx(xl),sy(yh),(xh-xl)*view.s,(yh-yl)*view.s);
 ctx.strokeStyle=css('--line');ctx.strokeRect(sx(xl),sy(yh),(xh-xl)*view.s,(yh-yl)*view.s);
 {const e=aEdge;ctx.save();ctx.strokeStyle=css('--acc');ctx.lineWidth=4;ctx.beginPath();if(e==='left'){ctx.moveTo(sx(xl),sy(yl));ctx.lineTo(sx(xl),sy(yh))}else if(e==='right'){ctx.moveTo(sx(xh),sy(yl));ctx.lineTo(sx(xh),sy(yh))}else if(e==='bottom'){ctx.moveTo(sx(xl),sy(yl));ctx.lineTo(sx(xh),sy(yl))}else{ctx.moveTo(sx(xl),sy(yh));ctx.lineTo(sx(xh),sy(yh))}ctx.stroke();ctx.restore()}
 if($('l_reg').checked){ctx.save();
  if(D.regions.length){ctx.strokeStyle=css('--reg');ctx.setLineDash([4,3]);for(const[a,b,c,d]of D.regions)ctx.strokeRect(sx(a),sy(d),(c-a)*view.s,(d-b)*view.s);ctx.setLineDash([])}
  if(R&&R.rects.length>1){ctx.strokeStyle=css('--macro');ctx.lineWidth=1.5;ctx.setLineDash([8,4]);for(const[a,b,c,d]of R.rects)ctx.strokeRect(sx(a),sy(d),(c-a)*view.s,(d-b)*view.s);ctx.setLineDash([]);
   if(view.s*Math.max(xh-xl,1)>200){ctx.fillStyle=css('--macro');ctx.font='11px system-ui';for(let k=0;k<R.rects.length;k++){const[a,b,c,d,n]=R.rects[k];ctx.fillText('R'+k+' ('+n+')',sx(a)+4,sy(d)+13)}}
   ctx.fillStyle=css('--sel');const q=Math.max(2,Math.min(4,view.s*(xh-xl)/300));for(const[x,y,ax]of R.pins){const px=sx(x),py=sy(y);if(ax===0)ctx.fillRect(px-q,py-q/2,2*q,q);else ctx.fillRect(px-q/2,py-q,q,2*q)}}
  ctx.restore()}
 const n=X.length,cell=css('--cell'),fx=css('--fixedc');
 if(M&&S.map){const bw=(xh-xl)/M.bx,bh=(yh-yl)/M.by;for(let i=0;i<M.bx;i++)for(let j=0;j<M.by;j++){const u=M.v[i*M.by+j];if(u<=.05)continue;const t=Math.min(1,u/Math.max(1,M.peak));ctx.fillStyle='rgba('+(u>1?'255,60,60':'255,170,40')+','+(.1+.55*t).toFixed(3)+')';ctx.fillRect(sx(xl+i*bw),sy(yl+(j+1)*bh),bw*view.s+.5,bh*view.s+.5)}}
 if($('l_dens').checked){if(densFrame!==frameId)densMap();const B=dens.b,bw=(xh-xl)/B,bh=(yh-yl)/B;for(let j=0;j<B;j++)for(let i=0;i<B;i++){const v=Math.min(1,dens.v[i+j*B]/dens.max);if(v<=.02)continue;ctx.fillStyle='rgba(224,36,94,'+(.08+.6*v).toFixed(3)+')';ctx.fillRect(sx(xl+i*bw),sy(yl+(j+1)*bh),bw*view.s+.5,bh*view.s+.5)}}
 if(lComb.checked||lRegs.checked){const sz=Math.max(1.4,Math.min(4,view.s*3));
  const vx0=wx(0),vx1=wx(W),vy1=wy(0),vy0=wy(H),seqc=css('--seq');for(let i=0;i<n;i++){if(kd(i)!==0||!cellOn(i))continue;if(X[i]>vx1||Y[i]>vy1||X[i]+D.w[i]<vx0||Y[i]+D.h[i]<vy0)continue;ctx.fillStyle=isSeq(i)?seqc:isMovable(i)?cell:fx;const w=D.w[i]*view.s,h=D.h[i]*view.s;
   if(w>3||h>3)ctx.fillRect(sx(X[i]),sy(Y[i])-h,w,h);else ctx.fillRect(sx(X[i]+D.w[i]/2)-sz/2,sy(Y[i]+D.h[i]/2)-sz/2,sz,sz)}}
 if($('l_disp').checked&&PX&&PX.length===n){ctx.strokeStyle=css('--bad');ctx.lineWidth=1;ctx.beginPath();
  for(let i=0;i<n;i++){if(kd(i)!==0)continue;const dx=X[i]-PX[i],dy=Y[i]-PY[i];if(dx*dx+dy*dy>0){ctx.moveTo(sx(PX[i]),sy(PY[i]));ctx.lineTo(sx(X[i]),sy(Y[i]))}}ctx.stroke()}
 for(let i=0;i<n;i++){const k=kd(i);if(k===0||k===4||(k!==3&&!$('l_macros').checked))continue;const mv=isMovable(i),w=D.w[i]*view.s,h=D.h[i]*view.s;
  ctx.globalAlpha=k===3?.25:.35;ctx.fillStyle=k===3?css('--reg'):mv?css('--macro'):css('--macrof');ctx.fillRect(sx(X[i]),sy(Y[i])-h,w,h);ctx.globalAlpha=1;
  ctx.strokeStyle=mv?css('--macro'):css('--macrof');ctx.lineWidth=1;ctx.strokeRect(sx(X[i]),sy(Y[i])-h,w,h);
  if(k!==3&&w>46&&h>14){ctx.fillStyle=css('--fg');ctx.font='11px system-ui';ctx.fillText(nm(i),sx(X[i])+4,sy(Y[i])-h+13,w-8)}}
 if($('l_io').checked){ctx.fillStyle=css('--io');for(let i=0;i<n;i++){if(kd(i)!==4)continue;ctx.fillRect(sx(X[i])-2.5,sy(Y[i])-2.5,5,5)}}
 if($('l_cen').checked&&D.centroids.length){ctx.strokeStyle=css('--sel');ctx.fillStyle=css('--sel');D.centroids.forEach((c0,ci)=>{const c=C&&C.x.length>ci?{x:C.x[ci],y:C.y[ci],r:C.r[ci]}:c0;const x=sx(c.x),y=sy(c.y),r=Math.max(4,c.r*view.s);
  ctx.beginPath();ctx.arc(x,y,r,0,7);ctx.globalAlpha=.12;ctx.fill();ctx.globalAlpha=1;ctx.stroke();ctx.beginPath();ctx.moveTo(x-5,y);ctx.lineTo(x+5,y);ctx.moveTo(x,y-5);ctx.lineTo(x,y+5);ctx.stroke()})}
 if(rev===null&&HL.length){for(const h of HL){ctx.strokeStyle=h.color;ctx.fillStyle=h.color;for(const i of h.idx){if(i>=n)continue;const w=D.w[i]*view.s,hh=D.h[i]*view.s,x=sx(X[i]),y=sy(Y[i]);if(x<-20||y<-20||x>W+20||y>H+20)continue;
  if(w<5&&hh<5){ctx.globalAlpha=.9;ctx.fillRect(sx(X[i]+D.w[i]/2)-3,sy(Y[i]+D.h[i]/2)-3,6,6);ctx.globalAlpha=1;ctx.lineWidth=1;ctx.strokeRect(sx(X[i]+D.w[i]/2)-5,sy(Y[i]+D.h[i]/2)-5,10,10)}
  else{ctx.globalAlpha=.45;ctx.fillRect(x,y-hh,w,hh);ctx.globalAlpha=1;ctx.lineWidth=2;ctx.strokeRect(x,y-hh,w,hh)}}}}
 ctx.lineWidth=1.5;ctx.strokeStyle=css('--pin');for(const i of pinned){if(i>=n)continue;const w=Math.max(6,D.w[i]*view.s),h=Math.max(6,D.h[i]*view.s);ctx.strokeRect(sx(X[i])-1,sy(Y[i])-h-1,w+2,h+2)}
 ctx.strokeStyle=css('--sel');ctx.lineWidth=2;for(const i of sel){if(i>=n)continue;const w=Math.max(8,D.w[i]*view.s),h=Math.max(8,D.h[i]*view.s);ctx.strokeRect(sx(X[i])-2,sy(Y[i])-h-2,w+4,h+4)}
 if(drag&&drag.box){ctx.strokeStyle=css('--acc');ctx.setLineDash([5,3]);ctx.strokeRect(drag.bx0,drag.by0,drag.bx1-drag.bx0,drag.by1-drag.by0);ctx.setLineDash([])}areaOverlay()}
function loop(){if(dirty)draw();requestAnimationFrame(loop)}requestAnimationFrame(loop);
cv.addEventListener('wheel',e=>{e.preventDefault();const f=Math.exp(-e.deltaY*.0015),x=e.offsetX,y=e.offsetY,ox=wx(x),oy=wy(y);view.s*=f;view.ox=x-ox*view.s;view.oy=y+oy*view.s;dirty=true},{passive:false});
function selMove(dx,dy,final){const ns=[],xs=[],ys=[];for(const i of drag.items){let x=drag.x0.get(i)+dx,y=drag.y0.get(i)+dy;const[a,b,c,d]=D.die;x=Math.min(Math.max(x,a),c-D.w[i]);y=Math.min(Math.max(y,b),d-D.h[i]);X[i]=x;Y[i]=y;drag.cur.set(i,[x,y]);ns.push(nm(i));xs.push(x);ys.push(y)}
 dirty=true;return{cmd:'move',nodes:ns,x:xs,y:ys,final}}
function hitCen(px,py){if(!D||!$('l_cen').checked||!D.centroids.length)return -1;for(let i=D.centroids.length-1;i>=0;i--){const c=C&&C.x.length>i?{x:C.x[i],y:C.y[i]}:D.centroids[i];if(Math.hypot(sx(c.x)-px,sy(c.y)-py)<=10)return i}return -1}
cv.addEventListener('mousedown',e=>{if(!D||e.button!==0)return;if(rev!==null){drag={pan:true,px:e.offsetX,py:e.offsetY,ox:view.ox,oy:view.oy};return}{const eg=e.shiftKey?null:hitEdge(e.offsetX,e.offsetY);if(eg){aEdge=eg;aStepSet=false;if(!aPend){aPend=D.die.slice();aBase=D.die.join()}drag={edge:eg};areaInfo();dirty=true;return}}const ci=hitCen(e.offsetX,e.offsetY);
 if(ci>=0&&!e.shiftKey){if(!C||C.x.length<=ci)C={x:Float32Array.from(D.centroids,c=>c.x),y:Float32Array.from(D.centroids,c=>c.y),r:Float32Array.from(D.centroids,c=>c.r)};drag={cen:ci,px:e.offsetX,py:e.offsetY,x0:C.x[ci],y0:C.y[ci],t:0,moved:false};dirty=true;return}
 const i=hit(e.offsetX,e.offsetY);
 if(i>=0){if(e.shiftKey){sel.has(i)?sel.delete(i):sel.add(i);selUi();dirty=true;return}
  if(!sel.has(i)){sel.clear();sel.add(i)}selUi();const items=[...sel].filter(isMovable);
  if(!items.length){dirty=true;return}
  drag={items,px:e.offsetX,py:e.offsetY,x0:new Map(items.map(k=>[k,X[k]])),y0:new Map(items.map(k=>[k,Y[k]])),t:0,moved:false,cur:new Map()}}
 else if(e.shiftKey){drag={box:true,bx0:e.offsetX,by0:e.offsetY,bx1:e.offsetX,by1:e.offsetY}}
 else{if(sel.size){sel.clear();selUi()}drag={pan:true,px:e.offsetX,py:e.offsetY,ox:view.ox,oy:view.oy}}dirty=true});
let mx=-1,my=-1;
function zoomBy(f){const inside=mx>=0&&my>=0&&mx<=cv.clientWidth&&my<=cv.clientHeight;zoomAt(f,inside?mx:cv.clientWidth/2,inside?my:cv.clientHeight/2)}
function zoomAt(f,x,y){const ox=wx(x),oy=wy(y);view.s*=f;view.ox=x-ox*view.s;view.oy=y+oy*view.s;dirty=true}
window.addEventListener('mousemove',e=>{const r=cv.getBoundingClientRect(),px=e.clientX-r.left,py=e.clientY-r.top;mx=px;my=py;
 if(!drag&&D){const eg=hitEdge(px,py);cv.style.cursor=eg?(eg==='left'||eg==='right'?'ew-resize':'ns-resize'):''}else if(drag&&drag.edge){cv.style.cursor=drag.edge==='left'||drag.edge==='right'?'ew-resize':'ns-resize'}
 if(!drag){if(!D||!X)return;const i=hit(px,py);if(i!==hover){hover=i;const t=$('tip');if(i<0)t.style.display='none';else{showTip(i);t.style.display='block';if(names[i]===undefined&&rev===null)fetchName(i)}}
  if(hover>=0){const t=$('tip');t.style.left=px+14+'px';t.style.top=py+14+'px'}return}
 if(drag.cen!==undefined){if(!drag.moved&&Math.hypot(px-drag.px,py-drag.py)<3)return;drag.moved=true;const x=drag.x0+(px-drag.px)/view.s,y=drag.y0-(py-drag.py)/view.s;C.x[drag.cen]=x;C.y[drag.cen]=y;dirty=true;const now=performance.now();if(now-drag.t>40){drag.t=now;cmd({cmd:'centroid',name:D.centroids[drag.cen].name,x,y,final:false})}}
 else if(drag.pan){view.ox=drag.ox+px-drag.px;view.oy=drag.oy+py-drag.py;dirty=true}
 else if(drag.box){drag.bx1=px;drag.by1=py;dirty=true}
 else if(drag.edge){const e2=drag.edge;areaSet(e2,Math.round(e2==='left'||e2==='right'?wx(px):wy(py)))}
 else{const dx=(px-drag.px)/view.s,dy=-(py-drag.py)/view.s;if(!drag.moved&&Math.hypot(px-drag.px,py-drag.py)<3)return;drag.moved=true;const now=performance.now();
  if(now-drag.t>40){drag.t=now;cmd(selMove(dx,dy,false))}else selMove(dx,dy,false)}});
window.addEventListener('mouseup',e=>{if(!drag)return;const r=cv.getBoundingClientRect(),px=e.clientX-r.left,py=e.clientY-r.top;
 if(drag.box){const x0=Math.min(drag.bx0,drag.bx1),x1=Math.max(drag.bx0,drag.bx1),y0=Math.min(drag.by0,drag.by1),y1=Math.max(drag.by0,drag.by1);
  for(let i=0;i<X.length;i++){const k=kd(i);if(k===4||k===3)continue;const cx=sx(X[i]+D.w[i]/2),cy=sy(Y[i]+D.h[i]/2);if(cx>=x0&&cx<=x1&&cy>=y0&&cy<=y1)sel.add(i)}selUi()}
 else if(drag.cen!==undefined){if(drag.moved)cmd({cmd:'centroid',name:D.centroids[drag.cen].name,x:C.x[drag.cen],y:C.y[drag.cen],final:true})}
 else if(drag.items&&drag.moved){cmd(selMove((px-drag.px)/view.s,-(py-drag.py)/view.s,true))}
 drag=null;dirty=true});
let HL=[],hlV=-1,selV=-1,selExpect=0,applyingSel=false,selTimer=null;
function selUi(){$('s_sel').textContent=sel.size;if(!applyingSel){clearTimeout(selTimer);selTimer=setTimeout(()=>{selExpect++;cmd({cmd:'select',names:[...sel].slice(0,5000).map(i=>'#'+i)})},150)}}
async function loadHighlights(){const r=await(await api('/highlights')).json();HL=r.h;hlV=r.v;renderHl();dirty=true}
function renderHl(){const box=$('hls');box.innerHTML='';if(!HL.length){box.textContent='none (an assistant can paint cells here)';return}
 for(const h of HL){const row=document.createElement('div');row.style.cssText='display:flex;gap:6px;align-items:center;margin:2px 0';const sw=document.createElement('span');sw.style.cssText='width:12px;height:12px;border-radius:2px;flex:none;background:'+h.color;
  const t=document.createElement('span');t.style.flex='1';t.textContent=h.label+' ('+h.count+(h.truncated?'+':'')+')';t.title=(h.regex?'regex '+h.regex+' ':'')+(h.scope?'scope '+h.scope:'');const x=document.createElement('button');x.textContent='x';x.style.padding='0 6px';x.onclick=()=>cmd({cmd:'clear_highlight',id:h.id});row.append(sw,t,x);box.appendChild(row)}}
async function loadSelection(){const r=await(await api('/selection')).json();applyingSel=true;sel=new Set(r.idx);selUi();applyingSel=false;dirty=true}
$('al').addEventListener('click',e=>{const a=e.target.dataset?.a;if(a)align(a)});$('al').nextElementSibling.addEventListener('click',e=>{if(e.target.dataset?.a==='matrix')align('matrix')});
function align(a){if(!D)return;const it=[...sel].filter(isMovable);if(a==='unpin'){const ns=it.filter(i=>pinned.has(i)).map(nm);if(ns.length)cmd({cmd:'unpin',nodes:ns});return}
 if(it.length<2)return;let L=1e300,R=-1e300,B=1e300,Tp=-1e300;for(const i of it){L=Math.min(L,X[i]);R=Math.max(R,X[i]+D.w[i]);B=Math.min(B,Y[i]);Tp=Math.max(Tp,Y[i]+D.h[i])}
 const nx=new Map(it.map(i=>[i,X[i]])),ny=new Map(it.map(i=>[i,Y[i]]));
 if(a==='left')it.forEach(i=>nx.set(i,L));else if(a==='right')it.forEach(i=>nx.set(i,R-D.w[i]));else if(a==='hcenter')it.forEach(i=>nx.set(i,(L+R)/2-D.w[i]/2));
 else if(a==='bottom')it.forEach(i=>ny.set(i,B));else if(a==='top')it.forEach(i=>ny.set(i,Tp-D.h[i]));else if(a==='vcenter')it.forEach(i=>ny.set(i,(B+Tp)/2-D.h[i]/2));
 else if(a==='disth'){const s=[...it].sort((p,q)=>X[p]-X[q]);const sum=s.reduce((t,i)=>t+D.w[i],0),gap=(R-L-sum)/(s.length-1);let x=L;for(const i of s){nx.set(i,x);x+=D.w[i]+gap}}
 else if(a==='distv'){const s=[...it].sort((p,q)=>Y[p]-Y[q]);const sum=s.reduce((t,i)=>t+D.h[i],0),gap=(Tp-B-sum)/(s.length-1);let y=B;for(const i of s){ny.set(i,y);y+=D.h[i]+gap}}
 else if(a==='matrix'){const rows=Math.max(1,+$('m_r').value|0),cols=Math.max(1,+$('m_c').value|0),s=[...it].sort((p,q)=>Y[q]-Y[p]||X[p]-X[q]),mw=Math.max(...it.map(i=>D.w[i])),mh=Math.max(...it.map(i=>D.h[i]));
  const px=cols>1?(R-L-mw)/(cols-1):0,py=rows>1?(Tp-B-mh)/(rows-1):0;s.forEach((i,k)=>{const r=Math.min(rows-1,Math.floor(k/cols)),c=k%cols;nx.set(i,L+c*px);ny.set(i,Tp-mh-r*py)})}
 const[a0,b0,c0,d0]=D.die,ns=[],xs=[],ys=[];for(const i of it){const x=Math.min(Math.max(nx.get(i),a0),c0-D.w[i]),y=Math.min(Math.max(ny.get(i),b0),d0-D.h[i]);X[i]=x;Y[i]=y;ns.push(nm(i));xs.push(x);ys.push(y)}
 dirty=true;cmd({cmd:'move',nodes:ns,x:xs,y:ys,final:true})}
$('l_map').onchange=()=>{cmd({cmd:'map',kind:$('l_map').value});if($('l_map').value==='none'){M=null;dirty=true}};
$('cen_rel').onclick=e=>{e.preventDefault();cmd({cmd:'centroid_release'})};
$('tim').onclick=()=>cmd({cmd:'timing'});
$('rp').onclick=sendRestart;$('f_sel').onclick=forkSelection;$('f_view').onclick=forkView;for(const b of document.querySelectorAll('#a_edges button'))b.onclick=()=>{aEdge=b.dataset.e;aStepSet=false;areaInfo();dirty=true};$('a_step').oninput=()=>{aStepSet=true};$('a_shr').onclick=()=>areaNudge(-1);$('a_grow').onclick=()=>areaNudge(1);$('a_apply').onclick=areaApply;$('a_cancel').onclick=areaCancel;$('sb_start').onclick=()=>{stopPlay();scrubTo(0)};$('sb_back').onclick=()=>stepTl(-1);$('sb_fwd').onclick=()=>stepTl(1);$('sb_end').onclick=live;$('sb_play').onclick=togglePlay;$('sb_rng').onpointerdown=()=>{scrubbing=true};window.addEventListener('pointerup',()=>{scrubbing=false});$('sb_rng').oninput=()=>{stopPlay();scrubTo(+$('sb_rng').value)};$('live').onclick=live;$('o_open').onclick=openOptions;$('otog').onclick=()=>{$('opts').className==='on'?$('opts').className='':openOptions()};$('o_close').onclick=()=>{$('opts').className=''};$('o_q').oninput=renderOptions;$('o_chg').onchange=renderOptions;$('o_reset').onclick=()=>{flagEdits={};renderOptions()};$('o_start').onclick=()=>rerun(-1);$('o_cp').onclick=()=>rerun(selCp);
$('cont').onclick=()=>cmd({cmd:'continue'});
$('run').onclick=()=>cmd({cmd:S.finished?'finish':S.paused||S.state==='gate'||S.state==='ready'||S.state==='starting'?'run':'pause'});
$('fit').onclick=fit;$('zin').onclick=()=>zoomBy(1.25);$('zout').onclick=()=>zoomBy(1/1.25);$('ptog').onclick=()=>{const p=$('panel');p.style.display=p.style.display==='none'?'':'none'};
$('shot').onclick=()=>{draw();cv.toBlob(b=>{const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='placement_'+(S.phase||'x')+'_'+(S.iter||0)+'.png';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),2000)})};
for(const id of['l_comb','l_regs','l_macros','l_io','l_reg','l_cen','l_disp','l_dens'])$(id).onchange=()=>{dirty=true};$('l_reg').addEventListener('change',()=>{cmd({cmd:'regions',on:$('l_reg').checked});if(!$('l_reg').checked){R=null;regV=-1;$('reg_note').textContent=''}else $('reg_note').textContent='computing...'});
window.addEventListener('keydown',e=>{if(e.target.tagName==='INPUT')return;if(e.key==='Escape'&&aPend){areaCancel();return}if(e.key===' '){e.preventDefault();$('run').click()}else if(e.key==='u'){align('unpin')}else if(e.key==='f'){fit()}else if(e.key==='ArrowLeft'){e.preventDefault();stepTl(-1)}else if(e.key==='ArrowRight'){e.preventDefault();stepTl(1)}else if(e.code==='KeyZ'||e.key==='z'||e.key==='Z'||e.key==='+'||e.key==='='||e.key==='-'||e.key==='_'){const out=e.key==='Z'||(e.code==='KeyZ'&&e.shiftKey)||e.key==='-'||e.key==='_';zoomBy(out?1/1.25:1.25)}else if(e.key==='Escape'){if($('opts').className==='on'){$('opts').className=''}else{sel.clear();selUi();dirty=true}}});
function fmt(v,d){return v==null?'-':(+v).toFixed(d)}
function ui(){areaInfo();forkList();$('s_phase').textContent=S.phase||'-';const st=$('s_state');st.textContent=S.state+(S.gate?' before '+S.gate:'');st.className='st-'+S.state;
 $('s_iter').textContent=S.iter;$('s_pend').textContent=S.pending;$('s_hpwl').textContent=S.hpwl==null?'-':fmt(S.hpwl,1);$('s_ovf').textContent=S.overflow==null||S.overflow<0?'-':fmt(S.overflow,4);
 {const ok=S.has_timing&&S.period>0&&S.period-S.wns>0,f=$('s_freq');f.textContent=ok?(S.timing_est?'~':'')+fmt(1e6/(S.period-S.wns),0):'-';f.title=S.period>0?'achieved 1/(period - WNS); the SDC target is '+fmt(1e6/S.period,0)+' MHz ('+fmt(S.period,0)+' ps)':'no SDC clock period'}
 $('s_wns').textContent=S.has_timing?(S.timing_est?'~':'')+fmt(S.wns,1):'-';$('s_tns').textContent=S.has_timing?fmt(S.tns,1):'-';$('s_titer').textContent=S.has_timing?S.timing_iter:'-';
 const b=$('run');b.textContent=S.finished?'Exit lhd':(S.paused||S.state==='gate'||S.state==='ready'||S.state==='starting')?'Run':'Pause';b.disabled=false;const ge=S.state==='gate'&&S.gate==='gp_end',ee=S.state==='gate'&&S.gate==='edit';if(ge)b.textContent='Run (+'+S.min_iters+' iter)';if(ee)b.textContent='Apply edits';$('cont').style.display=ge||ee?'':'none';$('msg').textContent=S.note||'';
 const box=$('stages');if(S.stages&&S.stages.length&&box.dataset.k!==JSON.stringify(S.stages)){box.dataset.k=JSON.stringify(S.stages);box.innerHTML='';
  for(const s of S.stages){const l=document.createElement('label');if(!s.available)l.className='na';const c=document.createElement('input');c.type='checkbox';c.checked=!s.on;c.disabled=!s.available;
   c.onchange=()=>cmd({cmd:'stage',name:s.name,on:!c.checked});l.appendChild(c);l.appendChild(document.createTextNode(s.name));box.appendChild(l)}}
 pinned=new Set(S.pins||[]);dirty=true}
let haveFrame=-1;
function applyFrame(buf,q16){const dv=new DataView(buf),mg=String.fromCharCode(dv.getUint8(0),dv.getUint8(1),dv.getUint8(2),dv.getUint8(3)),n=dv.getUint32(4,true);let off;
 if(n!==D.n)return false;
 if(mg==='LHD1'){if(!X)return false;const m=dv.getUint32(24,true),idx=new Uint32Array(buf,32,m),px=new Float32Array(buf,32+m*4,m),py=new Float32Array(buf,32+m*8,m);PX=Float32Array.from(X);PY=Float32Array.from(Y);for(let k=0;k<m;k++){X[idx[k]]=px[k];Y[idx[k]]=py[k]}off=32+m*12}
 else{PX=X?Float32Array.from(X):null;PY=Y?Float32Array.from(Y):null;
  if(mg==='LHQ1'){const bx=new Float32Array(buf,16,4),qx=new Uint16Array(buf,32,n),qy=new Uint16Array(buf,32+n*2,n),kx=(bx[2]-bx[0])/65535,ky=(bx[3]-bx[1])/65535;X=new Float32Array(n);Y=new Float32Array(n);for(let i=0;i<n;i++){X[i]=bx[0]+qx[i]*kx;Y[i]=bx[1]+qy[i]*ky}off=32+n*4}
  else{X=new Float32Array(buf,16,n).slice();Y=new Float32Array(buf,16+n*4,n).slice();off=16+n*8}}
 if(drag&&drag.items&&drag.moved)for(const[i,p]of drag.cur){X[i]=p[0];Y[i]=p[1]}
 buildGrid();haveFrame=Number(dv.getBigUint64(8,true));
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
 if(S.epoch!==epoch){epoch=S.epoch;D=null;X=null;Y=null;sceneId=-1;frameId=-1;haveFrame=-1;rev=null;selCp=-1;cpKey='';flagEdits={};flagDefs=null;TL=null;tlIdx=null;designCache={};tlCache=new Map();tlBytes=0;mkKey='';shownScene=null;try{flagDefs=await(await api('/options')).json();renderOptions()}catch(e){}}
 if(rev===null&&S.scene!==sceneId){sceneId=S.scene;hlV=-1;if(S.scene>0){D=await(await api('/design')).json();names=D.names;sel.clear();selUi();PX=null;fit()}}
 if(rev===null&&S.frame!==frameId&&D&&D.n){frameId=S.frame;const q16=D.n>50000,buf=await(await api('/frame?'+(q16?'q=16&':'')+(haveFrame>0&&X?'since='+haveFrame:''))).arrayBuffer();if(!applyFrame(buf,q16))haveFrame=-1}
 if(S.regions_on){if(S.regions_v!==regV){regV=S.regions_v;R=await(await api('/regions')).json();$('reg_note').textContent=R.note||'';dirty=true}}else if(R){R=null;regV=-1;$('reg_note').textContent='';dirty=true}
 if(S.map&&S.map_v!==mapV){mapV=S.map_v;const mb=await(await api('/map')).arrayBuffer(),h=new DataView(mb);if(mb.byteLength>=24&&h.getUint32(4,true)>0){const bx=h.getUint32(4,true),by=h.getUint32(8,true);M={bx,by,peak:h.getFloat32(12,true),ovf:h.getFloat32(16,true),v:new Float32Array(mb.slice(24))};$('s_map').textContent='peak '+M.peak.toFixed(2)+' ovf '+M.ovf.toFixed(3)}else{M=null;$('s_map').textContent=S.map_note||'no data'}dirty=true}else if(!S.map&&M){M=null;mapV=S.map_v;$('s_map').textContent='';dirty=true}
 if(S.hl_v!==undefined&&S.hl_v!==hlV&&rev===null)await loadHighlights();if(S.sel_v!==undefined&&S.sel_v!==selV){if(selExpect>0){selExpect--;selV=S.sel_v}else if(D&&rev===null){selV=S.sel_v;await loadSelection()}else selV=S.sel_v}
 CP=S.cps||[];if(S.tl_n>0&&(!TL||TL.e.length!==S.tl_n)){if(!TL||performance.now()-tlLast>500)await loadTimeline();else if(!tlTimer)tlTimer=setTimeout(async()=>{tlTimer=null;try{await loadTimeline()}catch(e){}},600)}else if(TL)renderScrub();renderCp();{const e=$('err'),f=S.state==='failed';e.style.display=f?'':'none';if(f){e.textContent=S.error;$('rp').disabled=false;$('rp').textContent='Restart (edited flags)';$('run').disabled=true}else if($('rp').textContent!=='Replay from here'){$('rp').textContent='Replay from here'}}ui()}catch(e){console.error(e);window.__lastError=String(e&&e.stack||e);ver=0;$('msg').textContent='disconnected';await new Promise(r=>setTimeout(r,1000))}}}
sync();
</script></body></html>)HTML";
  return page;
}
}  // namespace livehd::placement::gui

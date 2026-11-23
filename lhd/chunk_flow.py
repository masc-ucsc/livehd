# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Coordinate pre-CTS stages on the immutable packets exported by fp_plan."""
import argparse
import copy
import contextlib
import csv
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
import re
from pathlib import Path
import secrets
import shutil
import subprocess
import threading
import tempfile
import time
from socketserver import ThreadingUnixStreamServer
from chunk_resources import monitor
import chunk_dispatch
import chunk_interfaces
import chunk_explore
import chunk_validate
from chunk_live import LivePlacement, revision
from chunk_journal import Journal
from chunk_score import Exterior, contribution, priorities, refresh_neighbors
import sys
import tempfile
import webbrowser

PREFIX = 'livehd.placement.'


def one(records, kind, design=None):
    matches = [r for r in records if r['class'] == PREFIX + kind and (design is None or r['design'] == design)]
    if len(matches) != 1:
        raise ValueError(f'expected one {kind} record, found {len(matches)}')
    return matches[0]


def adjacent(a, b):
    dx = min(a[2], b[2]) - max(a[0], b[0])
    dy = min(a[3], b[3]) - max(a[1], b[1])
    return dx >= 0 and dy >= 0 and (dx > 0 or dy > 0)


def boundary(box, point):
    x, y = point
    u = min(max(x, box[0]), box[2])
    v = min(max(y, box[1]), box[3])
    return min(([box[0], v], [u, box[1]], [box[2], v], [u, box[3]]), key=lambda p: math.dist(point, p))


PROJECT = 'chunk-project.json'


def remember_contact(state, net, xy):
    history = state.setdefault('contact_history', {}).setdefault(str(net), [])
    if not history or history[-1] != xy:
        history.append(list(xy))
        del history[:-4]


def atomic_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as out:
        json.dump(value, out, allow_nan=False)
    os.replace(out.name, path)


def place_dir(workdir=None):
    if workdir:
        return workdir.resolve() / 'synth/place'
    for parent in (Path.cwd(), *Path.cwd().parents):
        for path in (parent, parent / 'synth/place'):
            if (path / PROJECT).is_file() or (path / 'chunks.json').is_file():
                return path
    raise ValueError('no chunk workspace here; use init or --workdir DIR')


@contextlib.contextmanager
def project_lock(directory, exclusive):
    with (directory / '.chunk-project.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
        yield lock.fileno()


def current_manifest(directory):
    project = directory / PROJECT
    name = json.loads(project.read_text())['manifest'] if project.is_file() else 'chunks.json'
    return directory / name


class Workspace:
    def __init__(self, manifest, exe):
        self.exe = exe
        self.manifest = manifest.resolve()
        raw = self.manifest.read_bytes()
        records = json.loads(raw)
        self.index = one(records, 'chunk_jobs')
        self.design = self.index['design']
        self.guidance = [r for r in records if r['class'] == PREFIX + 'chunk_guidance' and r.get('design') == self.design]
        plan = self.plan = one(records, 'chunk_plan', self.design)
        self.chunks = {c['id']: {'box_dbu': c.get('worker_box_dbu', c['box_dbu']), 'ports': c['ports'],
                                 **{k:c[k] for k in ('perimeter_dbu','region_dbu') if k in c}} for c in plan['chunks']}
        self.anchored = {p['net_index'] for p in plan.get('communication', {}).get('fixed_contacts', [])}
        self.incident = interfaces(self)
        self.timed_interfaces = {r[0] for r in self.index.get("assembly", {}).get("timing", [])}
        self.members = {c['id']: frozenset((m['macro'], m['id']) for m in c['members']) for c in plan['chunks']}
        self.jobs = {j['id']: j for j in self.index['jobs']}
        # Bind state to the exact export, including worker options and admission policy.
        self.manifest_digest = hashlib.sha256(raw).hexdigest()
        self.root = self.manifest.parent / '.chunk-work' / self.manifest_digest
        self.root.mkdir(parents=True, exist_ok=True)
        self.path = self.root / 'state.json'

    def read_state(self):
        return json.loads(self.path.read_text()) if self.path.exists() else {'results': {}, 'contacts': {}}

    @staticmethod
    def recover(state):
        # Holding the workspace lock proves that no earlier coordinator owns a running stage.
        for result in state['results'].values():
            for stage in result.values():
                if stage['status'] == 'running':
                    stage.update(status='interrupted', reason='previous coordinator exited before publishing the result')
        return state

    def state(self):
        with (self.root / 'lock').open('a') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
            except BlockingIOError:
                return self.read_state()
            return self.recover(self.read_state())

    def save(self, state):
        atomic_json(self.path, state)

    @contextlib.contextmanager
    def writer(self):
        with project_lock(self.manifest.parent, False) as project_fd, (self.root / 'lock').open('a') as lock:
            project = self.manifest.parent / PROJECT
            if project.exists() and json.loads(project.read_text())['manifest'] != self.manifest.name:
                raise ValueError('the project has been updated; reload the active floorplan')
            fcntl.flock(lock, fcntl.LOCK_EX)
            yield self.recover(self.read_state()), (project_fd, lock.fileno())

    def select(self, ids, neighbors=False):
        if set(ids) - self.chunks.keys():
            raise ValueError(f'unknown chunk(s): {sorted(set(ids) - self.chunks.keys())}')
        if neighbors and not ids:
            raise ValueError('--neighbors needs a starting chunk')
        selected = set(ids or self.chunks)
        if neighbors:
            for ident in ids:
                home = self.chunks[ident]
                selected.update(k for k, c in self.chunks.items()
                                if (chunk_interfaces.common_segments(home,c) if "region_dbu" in home or "region_dbu" in c
                                    else adjacent(home["box_dbu"],c["box_dbu"])))
        return [k for k in self.chunks if k in selected]

    def rows(self, state, ids):
        rows = []
        for k in ids:
            job = self.jobs.get(k, {})
            result = {}
            for stage, saved in state['results'].get(k, {}).items():
                result[stage] = saved.copy()
                for field in ('directory', 'report', 'previous_report', 'positions', 'preview', 'handoff'):
                    if field in saved:
                        result[stage][field] = os.path.normpath(self.root / saved[field])
            rows.append(dict(id=k, box_dbu=self.chunks[k]['box_dbu'], exported=bool(job),
                             **{field:self.chunks[k][field] for field in ('region_dbu','perimeter_dbu') if field in self.chunks[k]},
                             estimated_bytes=job.get('estimated_bytes'), budget_bytes=self.index['budget_bytes'],
                             macro=result.get('macro', {'status': 'pending'}), final=result.get('final', {'status': 'pending'})))
        return rows

    def display_rows(self, rows):
        pending = chunk_dispatch.active_jobs(self.root)
        for row in rows:
            job=pending.get(row['id'])
            if job:
                stage=job['stage']
                row[stage]=dict(row[stage],status='running',directory=job['directory'])
        return rows

    def show(self, state, ident):
        row, = self.rows(state, self.select([ident]))
        edits = state['contacts'].get(ident, {})
        row['ports'] = [dict(p, xy_dbu=edits.get(str(p['net_index']), p['xy_dbu'])) for p in self.chunks[ident]['ports']]
        for port in row['ports']:
            layer=state.get('contact_layers',{}).get(ident,{}).get(str(port['net_index']))
            if layer:port['layer']=layer
        row['neighbors'] = [k for k in self.select([ident], True) if k != ident]
        return row

    def analysis(self, cmd, req, state=None):
        state = state if state is not None else self.state()
        if cmd == 'shapes':
            if req.get('screen'):
                return chunk_interfaces.screen_shapes(self, state, req.get('pitch'))
            return {
                'rank': self.plan.get('shape_rank'),
                'home_order': list(self.chunks),
                'candidates': self.plan.get('shape_candidates', []),
            }
        if cmd == 'resources':
            project = self.manifest.parent / PROJECT
            settings = json.loads(project.read_text()) if project.exists() else {}
            waves = sorted((self.root / 'waves').glob('*.json'), key=lambda path: path.stat().st_mtime, reverse=True)
            return {
                'budget_bytes': self.index['budget_bytes'],
                'producer': settings.get('producer'),
                'latest_wave': json.loads(waves[0].read_text()) if waves else None,
            }
        if cmd == 'interfaces':
            proposal = chunk_interfaces.proposal(self, state, req.get('pitch'), req.get('nets'),spread=req.get('spread'),bus_order=req.get('bus_order',False))
            if req.get('apply') and proposal['updates']:
                with self.writer() as (current, _):
                    if revision(current) != revision(state):
                        raise ValueError('placement changed during slot allocation')
                    chunk_interfaces.install(current, proposal)
                    journal = Journal(self.root)
                    journal.checkpoint(revision(current), state)
                    journal.epoch(advance=True)
                    self.save(current)
            elif req.get('apply') and not proposal['feasible']:
                raise ValueError('insufficient interface slots at this pitch')
            if req.get('summary'):
                return {key: proposal[key] for key in ('pitch_dbu', 'feasible', 'edges','spread','bus_order')} | {
                    'unassigned': len(proposal['conflicts']),
                    'affected_homes': {h: len(nets) for h, nets in proposal['updates'].items()},
                }
            return proposal
        maps = {}
        for h in self.jobs:
            rows = state['results'].get(h, {})
            for stage in ('final', 'macro'):
                row = rows.get(stage, {})
                if row.get('status') == 'done':
                    measured = json.loads((self.root / row['report']).read_text())[0]
                    if 'coarse_routing' in measured:
                        maps[h] = {'stage': stage, 'grid': measured['coarse_routing']}
                    break
        floorplan = self.plan.get('coarse_routing')
        boundary_grid = self.plan.get('boundary_routing')
        fields = [
            'crossing_h',
            'crossing_v',
            'internal_h',
            'internal_v',
            'capacity_h',
            'capacity_v',
            'excess_h',
            'excess_v',
        ]
        if req.get('summary'):

            def summary(grid):
                if not grid:
                    return None
                totals = list(map(sum, zip(*grid['tiles'])))
                return {
                    'track_pitches': grid['track_pitches'],
                    'pitch_dbu': grid['pitch_dbu'],
                    'track_totals': dict(zip(fields, totals)),
                    'tiles_with_excess': sum(t[6] + t[7] > 0 for t in grid['tiles']),
                    'displayed_tiles': len(grid['tiles']),
                }

            floorplan = summary(floorplan)
            boundary_grid = summary(boundary_grid)
            for result in maps.values():
                result['grid'] = summary(result['grid'])
        return {'tile_fields': fields, 'floorplan': floorplan, 'boundaries':boundary_grid, 'chunks': maps}

    def macro_prerequisites(self, ids, state):
        pending = []
        for home in ids:
            if home not in self.jobs:
                continue
            results = state['results'].get(home, {})
            if any(results.get(stage, {}).get('status') == 'done' for stage in ('macro', 'final')):
                continue
            macros = sum(is_macro for is_macro, _ in self.members[home])
            if macros >= 2:
                pending.append(home)
        return pending

    def move_io(self, ident, net, xy, measured=False, undo=None):
        with self.writer() as (state, _):
            row = self.show(state, ident)
            port = next((p for p in row['ports'] if p['net_index'] == net), None)
            if port is None:
                raise ValueError(f'no planned contact {net} on {ident}')
            if measured:
                stage = 'macro' if measured is True else measured
                if stage not in ('macro', 'final') or row[stage]['status'] != 'done':
                    raise ValueError('a current macro or final placement is required for measured feedback')
                report = one(json.loads(Path(row[stage]['report']).read_text()), 'chunk_feedback', self.design)
                sample = next((p for p in report.get('boundary', []) if p['net_index'] == net), None)
                if sample is None:
                    raise ValueError(f'{stage} stage has no measurement for this net')
                xy = [a + b for a, b in zip(sample['contact_dbu'], sample['mean_pin_offset_dbu'])]
                xy = chunk_interfaces.project(row, xy)
            if len(xy) != 2 or not all(math.isfinite(v) for v in xy) or chunk_interfaces.project(row, xy) != list(xy):
                raise ValueError('use a point on the selected chunk perimeter in die DBU')
            if list(xy) == port['xy_dbu']:
                return {'changed': []}
            previous = copy.deepcopy(state) if undo is not None else None
            remember_contact(state, net, port["xy_dbu"])
            remember_contact(state, net, list(xy))
            affected = []
            for k, c in self.chunks.items():
                if any(p['net_index'] == net for p in c['ports']):
                    affected.append(k)
                    revisions = state.setdefault("interface_revision", {})
                    revisions[k] = revisions.get(k, 0) + 1
                    state.get('contact_layers',{}).get(k,{}).pop(str(net),None)
                    state['contacts'].setdefault(k, {})[str(net)] = chunk_interfaces.project(c, xy)
                    for result in state['results'].get(k, {}).values():
                        result.update(status='stale', reason=f'{ident} moved net {net}')
            self.save(state)
            if undo is not None:
                undo.append((state,previous))
            return {'changed': affected}

    def shared_edges(self, ident, state):
        """Slide a coincident contact along the edge common to all its homes."""
        edges = []
        for port in self.chunks[ident]['ports']:
            net = port['net_index']
            homes = self.incident[net]
            if net in self.anchored or len(homes) < 2:
                continue
            if any(str(net) in state['contacts'].get(k, {}) for k in homes):
                continue  # A manual edit or an earlier negotiated contact.
            if any(k != ident and any(r['status'] == 'done' for r in state['results'].get(k, {}).values())
                   for k in homes):
                continue  # Keep the interface of a current neighbor solution.
            xy = port['xy_dbu']
            if any(contact != xy for _, contact, _ in homes.values()):
                continue
            candidates = chunk_interfaces.perimeter(self.chunks[ident])
            for axis,line,start,end in candidates:
                if xy[1-axis]!=line or not start<=xy[axis]<=end: continue
                lo,hi=start,end
                for peer in homes:
                    match=next((e for e in chunk_interfaces.perimeter(self.chunks[peer])
                                if e[0]==axis and e[1]==line and e[2]<=xy[axis]<=e[3]),None)
                    if not match: break
                    lo,hi=max(lo,match[2]),min(hi,match[3])
                else:
                    if lo<hi:
                        edges.append([net,axis,lo,hi])
                        break
        return edges

    def prepare_job(
        self,
        state,
        k,
        stage,
        run,
        *,
        seed=None,
        poses=(),
        iterations=None,
        live_frames=False,
        density=None,
        freeze_macros=None,
        negotiate=False,
    ):
        result = state["results"].get(k, {})
        job = self.jobs[k].copy()
        job["input"] = str(self.manifest.parent / job["input"])
        handoff = stage == "final" and result.get("macro", {}).get("status") == "done"
        if seed:
            job.update(input=seed["path"], checksum=seed["checksum"])
        elif handoff:
            job.update(
                input=str(self.root / result["macro"]["handoff"]),
                checksum=result["macro"]["handoff_checksum"],
            )
        elif stage == "macro" and result.get("macro", {}).get("status") == "done":
            current = result["macro"]
            job.update(
                input=str(self.root / current["handoff"]),
                checksum=current["handoff_checksum"],
            )
        contacts = sorted(
            [int(net), *xy] for net, xy in state["contacts"].get(k, {}).items()
        )
        if freeze_macros is not None:
            handoff = freeze_macros
        edges = (
            self.shared_edges(k, state)
            if negotiate and not handoff and self.index.get("adjust_contacts", False)
            else []
        )
        job["request"] = {
            "class": PREFIX + "chunk_request",
            "stage": stage,
            "id": k,
            "design": self.design,
            "contacts": contacts,
            "freeze_macros": handoff,
            "shared_edges": edges,
        }
        if centroids := state.get('centroids', {}).get(k):
            job['request']['centroids'] = centroids
        if layers := state.get("contact_layers", {}).get(k):
            job["request"]["contact_layers"] = layers
        if grid := self.plan.get("coarse_routing"):
            members = next(c["members"] for c in self.plan["chunks"] if c["id"] == k)
            job["request"]["routing"] = dict(
                pitch=grid["track_pitch_dbu"],
                rates=grid["tracks_per_dbu"],
                blockage={
                    m["id"]: m["blocked_fraction"]
                    for m in members
                    if "blocked_fraction" in m
                },
            )
        if "score_nets" in job:
            job["request"]["score_nets"] = job.pop("score_nets")
        job.pop("seed_score", None)
        if density is not None:
            job["request"]["density"] = density
        if live_frames:
            job["request"]["live_frames"] = True
        if poses:
            job["request"]["poses"] = poses
        if iterations is not None:
            job["request"]["iterations"] = iterations
        if not poses:
            job["request"]["poses"] = [
                [n, x, y, x is not None, y is not None]
                for n, (x, y) in state.get("holds", {}).get(k, {}).items()
            ]
        # An edited frame must never appear current when the base fp_plan is reopened.
        base = self.index["plan_revision"]
        inputs = json.dumps([base, stage, job["checksum"], job["request"]]).encode()
        revision = "chunk-" + hashlib.sha256(inputs).hexdigest()
        job["input"] = os.path.relpath(job["input"], run)
        manifest = run / "job.json"
        packet_index = {
            field: value
            for field, value in self.index.items()
            if field not in ("jobs", "assembly")
        }
        packet_index.update(jobs=[job], base_plan_revision=base, plan_revision=revision)
        manifest.write_text(json.dumps([packet_index]))
        return manifest, edges

    def run(self, stage, ids, neighbors=False, *, seed=None, poses=(), iterations=None, undo=None, live_frames=False, density=None, freeze_macros=None, negotiate=True):
        selected = self.select(ids, neighbors)
        with self.writer() as (state, lock_fds), open(self.exe, 'rb') as binary:
            # A long placement queue can outlive a rebuild of lhd. Keep using the
            # same executable inode for each worker, even during an atomic replacement.
            fd_path = Path(f'/proc/self/fd/{binary.fileno()}')
            worker, worker_fds = self.exe, lock_fds
            if fd_path.exists():
                worker, worker_fds = fd_path, (*lock_fds, binary.fileno())
            previous = copy.deepcopy(state) if undo is not None else None
            if not ids:
                selected = [k for k in self.jobs if state['results'].get(k, {}).get(stage, {}).get('status') != 'done']
            for k in selected:
                if k not in self.jobs:
                    raise ValueError(f'{k} has no exported packet')
            for k in selected:
                _, previous_score = contribution(self,state,k)
                result = state['results'].setdefault(k, {})
                run = Path(tempfile.mkdtemp(prefix=stage + "-", dir=self.root))
                manifest, edges = self.prepare_job(
                    state,
                    k,
                    stage,
                    run,
                    seed=seed,
                    poses=poses,
                    iterations=iterations,
                    live_frames=live_frames,
                    density=density,
                    freeze_macros=freeze_macros,
                    negotiate=negotiate,
                )
                if stage == 'macro' and 'final' in result:
                    result['final'].update(status='stale', reason='macro placement rerun')
                prior_result = result.get(stage,{})
                # Keep the previous immutable measurement visible until the new one is published.
                previous_report = prior_result.get('report') or prior_result.get('previous_report')
                result[stage] = {'status': 'running', 'directory': run.name}
                if previous_report:
                    result[stage]['previous_report']=previous_report
                self.save(state)
                try:
                    out = run / 'result'
                    with (run / 'worker.log').open('w') as log:
                        # The child keeps the lock if the browser server or CLI is interrupted.
                        rc = subprocess.run([worker, 'place-chunks', manifest, '--out', out],
                                            stdout=log, stderr=log, pass_fds=worker_fds).returncode
                    if not (out / 'metadata.json').exists():
                        raise ValueError(f'worker produced no report (exit {rc}), see {run}/worker.log')
                    report = one(json.loads((out / 'metadata.json').read_text()), 'chunk_feedback', self.design)
                    result[stage]['report'] = str((out / report['report']).relative_to(self.root))
                    for key in ('iterations', 'hpwl_dbu', 'estimated_wns_ps', 'estimated_worst_slack_ps', 'timed_nets', 'peak_rss_bytes', 'worker_seconds', 'over_budget', 'solver_movable_nodes'):
                        if key in report:
                            result[stage][key] = report[key]
                    if rc or report['status'] not in ('placed', 'fixed_only'):
                        raise ValueError(report.get('reason', f'worker exit {rc}'))
                    for artifact in ('positions', 'preview'):
                        if artifact in report:
                            result[stage][artifact] = str((out / report[artifact]).relative_to(self.root))
                    if 'handoff_checksum' in report:
                        result[stage].update(handoff=str((out / '0/handoff.bin').relative_to(self.root)), handoff_checksum=report['handoff_checksum'])
                    if 'preview' in report:
                        image = json.loads((out / report['preview']).read_text())[0]
                        for key in ('overflow_fraction', 'peak_density', 'target_density'):
                            if key in image:
                                result[stage][key] = image[key]
                    for key in ('centroid_groups', 'gp_moved', 'gp_max_displacement_dbu','gp_mean_displacement_dbu'):
                        if key in report:
                            result[stage][key] = report[key]
                    permitted = {net for net, *_ in edges}
                    feedback = one(json.loads((out / report['report']).read_text()), 'chunk_feedback', self.design)
                    refresh_neighbors(self,state,k,previous_score,feedback.get('score'))
                    for sample in feedback.get('boundary', []):
                        net, xy = sample['net_index'], sample['contact_dbu']
                        if net in permitted and xy != self.incident[net][k][1]:
                            remember_contact(state, net, self.incident[net][k][1])
                            remember_contact(state, net, xy)
                            for home in self.incident[net]:
                                state['contacts'].setdefault(home, {})[str(net)] = xy
                    result[stage].pop('previous_report',None)
                    result[stage]['status'] = 'done'
                except (ValueError, OSError, KeyError) as error:
                    result[stage].update(status='failed', reason=str(error))
                    raise
                finally:
                    self.save(state)
            if undo is not None:
                undo.append((state, previous))
        return self.rows(state, selected)


def serve(work, page, no_open, viewer=None, workers=None, compare_dirs=()):
    token = secrets.token_urlsafe(32)
    primary = LivePlacement(work, workers)
    directories = list(dict.fromkeys([work.manifest.parent, *(place_dir(p) for p in compare_dirs)]))
    projects = {str(i): path for i, path in enumerate(directories)}
    sessions = {'0': (primary, {})}
    session_lock = threading.Lock()

    # Several tabs can watch the same study. Cache only small activity records,
    # and avoid loading every project's cell database on each poll.
    activity_lock = threading.Lock()
    activity_cache = {}
    legacy_roots = {}
    def activity():
        from chunk_live import project_activity

        with activity_lock:
            now = time.monotonic()
            if now < activity_cache.get('until', 0):
                return activity_cache['rows']
            rows = []
            for key, directory in projects.items():
                manifest = current_manifest(directory)
                if not manifest.is_file():
                    continue
                digest = manifest.stem.removeprefix('chunks-')
                if len(digest) != 64:
                    version = (manifest, manifest.stat().st_mtime_ns)
                    if version not in legacy_roots:
                        legacy_roots[version] = hashlib.sha256(manifest.read_bytes()).hexdigest()
                    digest = legacy_roots[version]
                event = project_activity(directory / '.chunk-work' / digest)
                if event:
                    rows.append(dict(project=key, name=directory.parent.parent.name, **event))
            rows.sort(key=lambda e: (e.get('running', False), e['time']), reverse=True)
            activity_cache.update(until=now+1, rows=rows)
            return rows

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def reply(self, content, mime, code=200):
            self.send_response(code)
            self.send_header('Content-Type', mime)
            self.send_header('Cache-Control', 'no-store')
            if hasattr(self, 'generation'):
                self.send_header('X-Chunk-Generation', self.generation)
            self.end_headers()
            self.wfile.write(content)

        def do_GET(self):
            if self.path == '/':
                self.reply(page.read_bytes(), 'text/html; charset=utf-8')
            else:
                self.send_error(404)

        def do_POST(self):
            local = isinstance(self.server, ThreadingUnixStreamServer)
            if (local and self.path != '/agent') or (not local and (self.path != '/chunk' or self.headers.get('Authorization') != 'Bearer ' + token)):
                self.send_error(403)
                return
            try:
                size = int(self.headers['Content-Length'])
                if not 0 < size <= 65536:
                    raise ValueError('invalid request size')
                req = json.loads(self.rfile.read(size))
                if not isinstance(req, dict) or not isinstance(req.get('cmd'), str):
                    raise ValueError('send an object with a command name')
                project = str(req.get('project', '0'))
                if project not in projects:
                    raise ValueError('unknown project')
                if req['cmd'] == 'activity':
                    self.reply(json.dumps({'events': activity()}).encode(), 'application/json')
                    return
                if req['cmd'] == 'projects':
                    options = [{'id': key, 'name': path.parent.parent.name,
                                'ready': current_manifest(path).is_file()} for key, path in projects.items()]
                    self.reply(json.dumps({'projects': options, 'selected': project}).encode(), 'application/json')
                    return
                with session_lock:
                    if project not in sessions:
                        peer = Workspace(current_manifest(projects[project]), work.exe)
                        sessions[project] = LivePlacement(peer, primary.slots, admission=primary.admission), {}
                    live, searches = sessions[project]
                with live.lock:
                    active = live.work
                    if (active.manifest.parent / PROJECT).is_file():
                        path = current_manifest(active.manifest.parent)
                        if path != active.manifest:
                            active = Workspace(path, active.exe)
                            live.bind(active)
                self.generation = active.root.name
                cmd = req['cmd']
                if cmd != 'list' and req.get('generation', self.generation) != self.generation:
                    raise ValueError('floorplan changed; refresh the selection')
                undo = []
                if cmd == 'list':
                    answer = active.display_rows(active.rows(active.state(), active.chunks))
                elif cmd == 'show':
                    answer = active.display_rows([active.show(active.state(), req['id'])])[0]
                elif cmd == 'preview':
                    row, = active.display_rows(active.rows(active.state(), active.select([req['id']])))
                    result = row[req['stage']]
                    if result['status'] == 'running':
                        frame = live.worker_progress(active, active.state(), req['id'], req['stage'])
                        answer = frame['image'] if frame else None
                    elif result['status'] == 'done' and 'preview' in result:
                        answer = one(json.loads(Path(result['preview']).read_text()), 'chunk_preview', active.design)
                    else:
                        raise ValueError('no current placement preview')
                elif cmd == 'restore-best':
                    answer = chunk_explore.restore_best(active, req.get('objective', 'hpwl'))
                elif local and cmd == 'verify-wns':
                    answer = chunk_validate.verify_wns(active, req['trial'], req['branch'], req.get('openroad', 'openroad'),
                                                       req.get('timing_setup'), req.get('keep', False))
                elif local and cmd == 'replan':
                    # The request replaces editing intent. Keep the active generation on disk
                    # until export and reconciliation succeed, then the next call reloads it.
                    guidance = req['guidance']
                    if not isinstance(guidance, dict) or set(guidance) - {'topology', 'geometry'}:
                        raise ValueError('guidance accepts topology and geometry edit arrays')
                    if any(not isinstance(v, list) for v in guidance.values()):
                        raise ValueError('floorplan edits must be arrays')
                    directory = active.manifest.parent
                    with tempfile.TemporaryDirectory(dir=active.root) as tmp:
                        path = Path(tmp) / 'guidance.json'
                        bundle = [dict(guidance, **{'class': PREFIX + 'chunk_guidance', 'design': active.design, 'id': 'agent'})] if guidance else []
                        atomic_json(path, bundle)
                        command = [active.exe, 'synth', 'chunk', 'update', '--workdir', str(directory.parent.parent), '--guidance', str(path)]
                        if 'budget' in req:
                            command.extend(['--budget', str(req['budget'])])
                        run = subprocess.run(command, capture_output=True, text=True)
                        if run.returncode:
                            raise ValueError(run.stderr.strip().splitlines()[-1] if run.stderr.strip() else 'incremental replan failed')
                        answer = json.loads(run.stdout)
                elif local and cmd == 'pcrs':
                    answer = chunk_validate.pcrs(active, req)
                elif local and cmd == 'validate':
                    answer = chunk_validate.validate(
                        active,
                        req.get('openroad', 'openroad'),
                        req.get('route'),
                        req.get('capacity', 0.75),
                        req.get('timing_setup'),
                        req.get('segments', False),
                        req.get('trial'),
                        req.get('branch'),
                    )
                elif local and cmd == 'assemble' and req.get('def'):
                    state = chunk_validate.placement_state(active, req.get('trial'), req.get('branch'))
                    answer = chunk_validate.stitch(active, state, req['def'])
                elif local and cmd == 'dispatch':
                    answer = {
                        'request': chunk_dispatch.issue(
                            active,
                            req['home'],
                            req['stage'],
                            req['bundle'],
                            iterations=req.get('iterations'),
                        )
                    }
                elif local and cmd == 'collect':
                    answer = chunk_dispatch.collect(active, req['bundle'])
                elif cmd == 'explore':
                    kind = req['kind']
                    if kind not in ('macro', 'density', 'interfaces'):
                        raise ValueError('explore accepts macro, density or interfaces')
                    ident = secrets.token_hex(6)
                    journal = Journal(active.root)
                    start = active.state()
                    homes = active.select(req.get('ids') or [])
                    pending = dict(
                        id=ident,
                        kind=kind,
                        home=homes[0],
                        stage='final',
                        selection=[],
                        checkpoint=revision(start),
                        epoch=journal.epoch(),
                        started=time.time(),
                        hypothesis=req.get('hypothesis') or f'Compare {kind} placement',
                        requested_iterations=req.get('iterations', 120),
                        candidates={kind: {'status': 'queued'}},
                    )
                    journal.save(pending)

                    def search():
                        try:
                            return chunk_explore.explore(
                                active,
                                kind,
                                live.slots,
                                req.get('iterations', 120),
                                req.get('final_iterations', 300),
                                homes,
                                req.get('pitch'),
                                req.get('nets'),
                                req.get('density', 0.4),
                                req.get('keep', False),
                                req.get('objective', 'hpwl'),
                                admit=live.admit,
                                ident=ident,
                                base_state=start, spread=req.get('spread'),bus_order=req.get('bus_order',False),hypothesis=req.get('hypothesis'),
                            )
                        except Exception as exc:
                            record = journal.get(ident)
                            for row in record['candidates'].values():
                                if row['status'] in ('queued', 'running'):
                                    row.update(status='failed', error=str(exc))
                            record['error'] = str(exc)
                            journal.save(record)
                            raise
                        finally:
                            with live.lock:
                                searches.pop(ident, None)

                    with live.lock:
                        task = live.pool.submit(search)
                        searches[ident] = task
                    answer = task.result() if req.get('wait') else {'ok': True, 'trial': ident, 'status': 'queued'}
                elif cmd == 'compare' and req.get('wait') and req.get('trial') in searches:
                    with live.lock:
                        task = searches.get(req['trial'])
                    answer = task.result() if task else Journal(active.root).get(req['trial'])
                elif cmd in ('interfaces', 'shapes', 'resources'):
                    answer=active.analysis(cmd,req)
                elif cmd == 'move-io':
                    with live.lock:
                        home, net = req.get('id', live.home), req.get('net', live.port)
                    answer = active.move_io(home, net, req.get('xy', []), req.get('measured', False), undo=undo)
                elif cmd in ('macro', 'final','place-design'):
                    finish = cmd == 'place-design'
                    if finish:
                        cmd = 'final'
                        req = dict(req, ids=[])
                    with live.lock:
                        ids = req.get('ids', live.homes or [live.home])
                        if len(ids)==1 and ids[0] in active.jobs:
                            if live.home != ids[0]:
                                live.selected = []
                            live.home, live.stage, live.view = ids[0], cmd, None
                            live.update()
                    selected = active.select(ids, req.get('neighbors', False))
                    if cmd == 'final':
                        required = active.macro_prerequisites(selected, active.state())
                        if required:
                            chunk_dispatch.wave(active, 'macro', required, live.slots, live_frames=True, admit=live.admit)
                    answer = chunk_dispatch.wave(
                        active,
                        cmd,
                        [h for h in selected if h in active.jobs] if ids else None,
                        live.slots,
                        iterations=req.get('iterations'),
                        live_frames=True,
                        admit=live.admit,
                    )
                    if len(answer) == 1:
                        with live.lock:
                            if live.home != answer[0]['id']:
                                live.selected = []
                            live.home, live.stage, live.view = answer[0]['id'], cmd, None
                            live.update()
                    if finish:
                        with live.lock:
                            live.stage, live.view = 'final', None
                            live.update()
                        answer = live.assemble(active, active.state())
                else:
                    answer = live.command(active, cmd, req)
                if undo:
                    live.record(active, *undo[0])
                if local and not isinstance(answer, dict):
                    answer = {'ok': True, 'rows': answer}
                self.reply(json.dumps(answer).encode(), 'application/json')
            except (ValueError, TypeError, OSError, KeyError) as e:
                self.reply(json.dumps({'ok': False, 'error': str(e)}).encode(), 'application/json', 400)

    with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as http:
        url = f'http://127.0.0.1:{http.server_port}/#{token}'
        with tempfile.TemporaryDirectory(prefix='lhd-live-') as directory:
            sock = str(Path(directory) / 'agent.sock')
            with ThreadingUnixStreamServer(sock, Handler) as server:
                os.chmod(sock, 0o600)
                pointer = work.manifest.parent / 'chunk_agent.json'
                atomic_json(pointer, dict(socket=sock, pid=os.getpid()))
                threading.Thread(target=server.serve_forever, daemon=True).start()
                print(url, flush=True)
                try:
                    if viewer:
                        viewer.target = url
                    elif not no_open:
                        webbrowser.open(url)
                    http.serve_forever()
                finally:
                    server.shutdown()
                    if pointer.exists() and json.loads(pointer.read_text()).get('socket') == sock:
                        pointer.unlink()


def interfaces(work):
    nets = {}
    for ident, chunk in work.chunks.items():
        for port in chunk['ports']:
            nets.setdefault(port['net_index'], {})[ident] = (port['net'], port['xy_dbu'], chunk['box_dbu'])
    return nets


def reconcile(old, new, same_engine):
    if same_engine and old.root == new.root:
        return dict(unchanged=list(new.jobs), changed=[], membership_changed=[], added=[], removed=[])
    prior = old.state()
    state = {'results': {}, 'contacts': {}}
    before, after = old.incident, new.incident
    changed_io = set()
    stable_nets = set()
    old_grid = old.plan.get('coarse_routing', {})
    new_grid = new.plan.get('coarse_routing', {})
    same_tracks = all(old_grid.get(key) == new_grid.get(key) for key in ('track_patterns', 'track_pitch_dbu', 'io_spacing'))
    # Compare the actual interface, including every previously negotiated tap.
    # A bounding frame can expand while the shared contacts on another edge stay put.
    overrides = {}
    for ident, edits in prior['contacts'].items():
        for key, xy in edits.items():
            overrides.setdefault(int(key), {})[ident] = xy
    for net in before.keys() | after.keys():
        previous, current = before.get(net, {}), after.get(net, {})
        taps = overrides.get(net, {})
        matching = previous.keys() == current.keys() and all(previous[h][:2] == current[h][:2] for h in current)
        if taps:
            matching = (matching and same_tracks and (net in old.anchored) == (net in new.anchored)
                        and all(h in current and chunk_interfaces.project(new.chunks[h], xy) == xy for h, xy in taps.items()))
        if matching:
            stable_nets.add(net)
        else:
            changed_io.update(previous)
            changed_io.update(current)
    for net, taps in overrides.items():
        if net not in stable_nets:
            continue
        key = str(net)
        for ident, xy in taps.items():
            state['contacts'].setdefault(ident, {})[key] = xy
            layer = prior.get('contact_layers', {}).get(ident, {}).get(key)
            if layer:
                state.setdefault('contact_layers', {}).setdefault(ident, {})[key] = layer
    for ident, counter in prior.get('interface_revision', {}).items():
        if ident in new.chunks:
            state.setdefault('interface_revision', {})[ident] = counter + int(ident in changed_io)
    changed, reused, repartitioned = [], [], []
    for ident in new.jobs.keys() & old.jobs.keys():
        reasons = []
        if new.members[ident] != old.members[ident]:
            repartitioned.append(ident)
            reasons.append('chunk membership')
        elif groups := prior.get('centroids', {}).get(ident):
            state.setdefault('centroids', {})[ident] = groups
        if old.jobs[ident].get('score_nets') != new.jobs[ident].get('score_nets'):
            reasons.append('crossing net identities')
        if old.jobs[ident]['checksum'] != new.jobs[ident]['checksum']:
            reasons.append('worker packet')
        if ident in changed_io:
            reasons.append('shared IO')
        if old.index.get('adjust_contacts', False) != new.index.get('adjust_contacts', False):
            reasons.append('shared IO adjustment policy')
        if not same_engine or old.index['require_fit'] != new.index['require_fit']:
            reasons.append('worker version or admission policy')
        (changed if reasons else reused).append(ident)
        if not reasons and ident in prior.get('holds', {}):
            state.setdefault('holds', {})[ident] = prior['holds'][ident]
        stages = prior['results'].get(ident)
        if not stages:
            continue
        for stage, saved in stages.items():
            row = saved.copy()
            for field in ('directory', 'report', 'previous_report', 'positions', 'preview', 'handoff'):
                if field in row:
                    row[field] = os.path.relpath(old.root / row[field], new.root)
            if reasons:
                row.update(status='stale', reason='update changed ' + ', '.join(reasons))
            state['results'].setdefault(ident, {})[stage] = row
    new.save(state)
    return dict(unchanged=sorted(reused), changed=sorted(changed), membership_changed=sorted(repartitioned),
                added=sorted(new.jobs.keys() - old.jobs.keys()), removed=sorted(old.jobs.keys() - new.jobs.keys()))


def without_settings(argv, keys):
    """Remove inherited --set pairs for explicitly overridden native options."""
    result = []
    i = 0
    while i < len(argv):
        if argv[i] == '--set' and i + 1 < len(argv) and argv[i + 1].partition('=')[0] in keys:
            i += 2
        else:
            result.append(argv[i])
            i += 1
    return result


def export_args(argv, managed):
    # The native driver passes everything after -- directly to the HDL reader.
    args = list(argv)
    index = args.index('--') if '--' in args else len(args)
    flags = ['--set', 'synth.place=true', '--set', 'synth.place.fp=chunks', '--set', 'synth.place.chunk.run=export']
    if '--gui' in args[:index] and not any(a.startswith('synth.place.gui.pause=') for a in args[:index]):
        flags += ['--set', 'synth.place.gui.pause=true']
    if managed:
        flags += ['--set', 'synth.place.chunk.pack_scopes=true']
    args[index:index] = flags
    return args


class FloorplanWindow:
    """Keep one browser tab across the native fp_plan gate and packet workers."""
    target = ''

    def __init__(self, no_open):
        token = secrets.token_urlsafe(32)
        window = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == '/':
                    body = b"""<!doctype html><html lang="en"><meta charset="utf-8"><title>LiveHD floorplan</title>
<style>body{margin:0;font:16px system-ui}p{padding:1rem}iframe{position:fixed;inset:0;width:100%;height:100%;border:0}</style>
<p role="status">Preparing floorplan...</p><iframe hidden title="Live floorplan"></iframe>
<script>const pane=document.querySelector('iframe');let target='';
async function follow(){try{const response=await fetch('/target',{headers:{Authorization:'Bearer '+location.hash.slice(1)}});
if(response.ok){const next=await response.text();if(next && next!==target){target=next;pane.src=next;pane.hidden=false;}}}
finally{setTimeout(follow,500);}}follow();</script></html>"""
                    mime = 'text/html'
                elif self.path == '/target' and self.headers.get('Authorization') == 'Bearer ' + token:
                    body, mime = window.target.encode(), 'text/plain'
                else:
                    self.send_error(403)
                    return
                self.send_response(200)
                self.send_header('Content-Type', mime)
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Referrer-Policy', 'no-referrer')
                self.end_headers()
                self.wfile.write(body)

        self.http = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self.http.serve_forever, daemon=True).start()
        url = f'http://127.0.0.1:{self.http.server_port}/#{token}'
        print('Live floorplan: '+url, file=sys.stderr, flush=True)
        if not no_open:
            webbrowser.open(url)

    def run(self, command, budget, **kwargs):
        # Relay native diagnostics and discover its authenticated browser URL.
        command = list(command)
        if '--gui-no-open' not in command:
            command.insert(command.index('--gui')+1, '--gui-no-open')
        with subprocess.Popen(command, stderr=subprocess.PIPE, **kwargs) as proc, monitor(proc,budget) as usage:
            for line in proc.stderr:
                sys.stderr.buffer.write(line)
                sys.stderr.buffer.flush()
                if line.startswith(b'lhd synth --gui: http://127.0.0.1:'):
                    self.target = line.decode().partition('--gui: ')[2].strip()
            return proc.wait(),usage

def setup_project(exe, page, argv, updating):
    index = argv.index('--') if '--' in argv else len(argv)
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument('--workdir', type=Path)
    parser.add_argument('--budget', metavar='SIZE')
    parser.add_argument('--producer-budget', metavar='SIZE')
    if updating:
        parser.add_argument('--guidance', type=Path, help='replace saved floorplan editing intent with a JSON bundle')
    else:
        parser.add_argument('--from-workdir', type=Path, help='reuse this synthesis recipe and mapping caches in a new project')
    parser.add_argument('--workers', type=int)
    opts, args = parser.parse_known_args(argv[:index])
    if opts.workers is not None and opts.workers < 1:
        raise ValueError('--workers must be positive')
    if updating and (any(a not in ('--gui', '--gui-no-open') for a in args) or argv[index:]):
        raise ValueError('update accepts --workdir DIR, --budget SIZE, --gui and --gui-no-open')
    gui_args = [a for a in args if a in ('--gui', '--gui-no-open')]
    if updating and '--gui-no-open' in gui_args and '--gui' not in gui_args:
        raise ValueError('--gui-no-open requires --gui')
    if updating:
        directory = place_dir(opts.workdir)
    else:
        directory = (opts.workdir or Path.cwd()).resolve() / 'synth/place'
        directory.mkdir(parents=True, exist_ok=True)
    path = directory / PROJECT
    with project_lock(directory, True) as lock_fd:
        if updating:
            if not path.exists():
                raise ValueError('update requires a workspace created by init')
            project = json.loads(path.read_text())
            old = Workspace(directory / project['manifest'], exe)
        else:
            if path.exists():
                raise ValueError('workspace already initialized; use update')
            if opts.from_workdir:
                source = place_dir(opts.from_workdir)
                with project_lock(source, False):
                    saved = json.loads((source / PROJECT).read_text())
                    previous = saved['args']
                    sep = previous.index('--') if '--' in previous else len(previous)
                    native = [a for a in previous[:sep] if a not in ('--gui', '--gui-no-open')]
                    overrides = {args[i + 1].partition('=')[0] for i, arg in enumerate(args[:-1]) if arg == '--set'}
                    native = without_settings(native, overrides)
                    reader = previous[sep + 1:] + argv[index + 1:]
                    project = {k: saved[k] for k in ('cwd', 'workers', 'budget_mib', 'producer_budget_mib') if k in saved}
                    project['args'] = native + args + (['--'] + reader if reader else [])
                    project['source_project'] = str(source / saved['manifest'])
                    # Only mapping caches cross the boundary. The new owner gets private
                    # inodes and can remap or update its index independently.
                    for name in ('abc_cache', 'abc_cache_pre', 'usyn_cache'):
                        origin, destination = source.parent.parent / name, directory.parent.parent / name
                        if origin.is_dir() and not destination.exists():
                            with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
                                copied = Path(temporary) / name
                                shutil.copytree(origin, copied)
                                copied.rename(destination)
            else:
                args += argv[index:]
                project = dict(cwd=str(Path.cwd()), args=export_args(args, True))
        if opts.workers is not None:
            project['workers'] = opts.workers
        for size, key in (
            (opts.budget, "budget_mib"),
            (opts.producer_budget, "producer_budget_mib"),
        ):
            if size is None:
                continue
            match = re.fullmatch(r"(\d+(?:\.\d+)?|\.\d+)(MiB|GiB|MB|GB)?", size, re.I)
            if not match or not 0 < float(match[1]) < math.inf:
                raise ValueError(
                    "use a positive memory size, for example 2GB or 1907MiB"
                )
            scale = {"mib": 2**20, "gib": 2**30, "mb": 10**6, "gb": 10**9}[
                (match[2] or "MiB").lower()
            ]
            project[key]=float(match[1])*scale/2**20
        digest = hashlib.sha256()
        with open(exe, 'rb') as binary:
            for block in iter(lambda: binary.read(1 << 20), b''):
                digest.update(block)
        engine = digest.hexdigest()
        args = list(project['args'])
        sep = args.index('--') if '--' in args else len(args)
        native, reader = args[:sep], args[sep:]
        # Older recipes stored an absolute workdir. Always use the active project directory.
        if '--workdir' in native:
            native[native.index('--workdir') + 1] = str(directory.parent.parent)
        else:
            native += ['--workdir', str(directory.parent.parent)]
        if 'budget_mib' in project:
            native = without_settings(native, {'lhd.ram_budget_mb'})
            native += ['--set', f"lhd.ram_budget_mb={project['budget_mib']}"]
        if updating:
            native = [a for a in native if a not in ('--gui', '--gui-no-open')]
            native += gui_args
            if '--gui' in gui_args and not any(a.startswith('synth.place.gui.pause=') for a in native):
                native += ['--set', 'synth.place.gui.pause=true']
            if opts.guidance is not None:
                native.append(str(opts.guidance.resolve(strict=True)))
            elif project.get('guidance'):
                native.append(str(directory / project['guidance']))
        args = native + reader
        log = directory / 'chunk-build.log'
        # The native synth driver reuses its normal compile and mapping caches.
        # Publish the snapshot only after the export and state reconciliation both succeed.
        viewer = FloorplanWindow('--gui-no-open' in native) if '--gui' in native else None
        with log.open('w') as out:
            command = [exe, 'synth', *args]
            invocation = dict(cwd=project['cwd'], stdout=out, pass_fds=(lock_fd,))
            budget = round(project.get("producer_budget_mib", 0) * 2**20)
            invocation["start_new_session"] = True
            if viewer:
                rc, usage = viewer.run(command, budget, **invocation)
            else:
                with (
                    subprocess.Popen(command, **invocation) as proc,
                    monitor(proc, budget) as usage,
                ):
                    rc = proc.wait()
            project['producer']=usage
        if rc:
            raise ValueError(f'floorplan export failed (exit {rc}); see {log}')
        work = Workspace(directory / 'chunks.json', exe)
        snapshot = directory / ('chunks-' + work.root.name + '.json')
        seed_def = directory / "chunk-plan.def"
        if seed_def.is_file() and not (work.root / "seed.def").exists():

            shutil.copyfile(seed_def,work.root/'seed.def')
        if not snapshot.exists():
            os.link(work.manifest, snapshot)
        replace_guidance = '--gui' in native or (updating and opts.guidance is not None)
        if replace_guidance and work.guidance:
            # Replay the accepted editing intent. A full saved plan would pin the
            # old source revision and prevent the planner from adapting to an ECO.
            guidance = work.root / 'accepted-guidance.json'
            atomic_json(guidance, work.guidance)
            project['guidance'] = str(guidance.relative_to(directory))
        elif replace_guidance:
            project.pop('guidance', None)
        summary = reconcile(old, work, engine == project['engine']) if updating else {'added': list(work.jobs)}
        project.update(manifest=snapshot.name, engine=engine)
        atomic_json(path, project)
        print(json.dumps(dict(workdir=str(directory.parent.parent), chunks=len(work.jobs), **summary), indent=2))
    if '--gui' in native:
        serve(Workspace(snapshot, exe), Path(page), '--gui-no-open' in native, viewer, workers=project.get('workers'))


def main(exe, page, argv):
    native_args = argv[:argv.index('--')] if '--' in argv else argv
    if argv[:1] in (['init'], ['update']) and not any(a in ('--help', '-h') for a in native_args[1:]):
        return setup_project(exe, page, argv[1:], argv[0] == 'update')
    if argv[:1] == ['plan'] and argv[1:] not in (['--help'], ['-h']):
        command = [exe, 'synth', *export_args(argv[1:], False)]
        if '--gui' not in argv:
            os.execv(exe, command)
        viewer = FloorplanWindow('--gui-no-open' in argv)
        rc, _ = viewer.run(command, 0, start_new_session=True)
        if rc:
            raise SystemExit(rc)
        # Continue from the accepted interactive plan after synthesis releases its flat view.
        opts = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
        opts.add_argument('--workdir', type=Path, required=True)
        workdir = opts.parse_known_args(argv[1:])[0].workdir
        return serve(Workspace(workdir / 'synth/place/chunks.json', exe), Path(page), '--gui-no-open' in argv, viewer)
    parser = argparse.ArgumentParser(prog='lhd synth chunk')
    cmds = parser.add_subparsers(dest='cmd', required=True)
    clocks = cmds.add_parser('clocks', help='inspect the chunk entries in a saved PCRS clock tree')
    clocks.add_argument('homes', nargs='*', metavar='CHUNK', help='chunk IDs to inspect (default: all)')
    clocks.add_argument('--routed', type=Path, help='include extracted edge arrivals from the matching 6_clock_chunk_arrivals.tsv')
    location = clocks.add_mutually_exclusive_group()
    location.add_argument('--report', type=Path, help='saved pcrs_clock.json from placement or DEF export')
    location.add_argument('--workdir', type=Path, help='synthesis workspace containing the saved clock tree')
    init = cmds.add_parser('init', usage='%(prog)s SOURCES [--workdir DIR] [--budget SIZE] [synth options]',
                           help='create a floorplan and remember the source recipe for update',
                           description='Pass the same HDL, top, Liberty, LEF and reader options as lhd synth.')
    init.add_argument('--from-workdir', type=Path, help='reuse a managed project recipe and mapping caches')
    init.add_argument('--budget', metavar='SIZE', help='per-worker memory budget, for example 1024MiB or 1GiB')
    init.add_argument('--workdir', metavar='DIR', help='new project directory (default: current directory)')
    update = cmds.add_parser('update', help='rebuild from the saved source recipe and reconcile chunks')
    for sub in (init, update):
        sub.add_argument('--producer-budget',metavar='SIZE',help='separate process-tree limit for source mapping and export')
        sub.add_argument('--workers',type=int,help='concurrent GUI placement workers for this project')
        sub.add_argument('--gui', action='store_true', help='pause in the interactive floorplan editor')
        sub.add_argument('--gui-no-open', action='store_true', help='print GUI access details without opening a browser')
    update.add_argument('--budget', metavar='SIZE', help='resize the floorplan using a new worker budget')
    update.add_argument('--guidance', metavar='JSON', help='replace previous floorplan edits, using [] to clear them')
    update.add_argument('--workdir', metavar='DIR', help='project to update (default: current project)')
    cmds.add_parser('plan', usage='%(prog)s SOURCES --workdir DIR [synth options]',
                    help='export fp_plan with the usual synth inputs',
                    description='Supply the synthesis sources and technology. Set --set lhd.ram_budget_mb=N to size the chunks. '
                                'Use --gui to edit the initial plan and continue into chunk placement.')
    for cmd, help_text in [('list', 'list chunks and their stage status'), ('show', 'inspect a chunk and its IO contacts'),
                           ('macro', 'place macros and save the fixed frame'), ('final', 'place cells in the accepted macro frame'),
                           ('shapes','list screened frames in coarse wirelength order'), ('resources','report producer and worker memory and concurrent solves'), ('validate','stitch and check a final DEF using OpenROAD'), ('restore-best','restore the best complete comparable archived placement'), ('verify-wns','compare independent WNS on an archived trial, control and checkpoint'), ('explore','test a placement hypothesis against a matched control'), ('interfaces','preview or apply a shared IO slot contract'), ('routing','get directional floorplan and worker routing estimates'), ('move-io', 'move a contact and invalidate all homes on its net'), ('assemble','verify final placements and emit the assembled position manifest'), ('next','rank the next chunk to optimize'), ('score','score the assembled coordinates'), ('gui', 'open chunk placement controls')]:
        sub = cmds.add_parser(cmd, help=help_text)
        target = sub.add_mutually_exclusive_group()
        target.add_argument('--manifest', type=Path, help='exported chunk manifest')
        target.add_argument('--workdir', type=Path, help='synthesis workspace (auto-detected inside a project)')
        if cmd in ('list', 'macro', 'final'):
            sub.add_argument('ids', nargs='*')
            sub.add_argument('--neighbors', action='store_true', help='include homes touching or overlapping the selected boxes')
        if cmd in ('list', 'macro', 'final'):
            sub.add_argument('--json', action='store_true', help='include machine-readable feedback and artifact paths')
        if cmd in ("macro", "final"):
            sub.add_argument(
                "--workers",
                type=int,
                help="concurrent workers using frozen shared contacts",
            )
            sub.add_argument("--iterations", type=int)
            sub.add_argument('--cgroup-root',type=Path,help='delegated Linux cgroup v2 directory for hard worker limits')
        if cmd=='shapes':
            sub.add_argument('--screen',action='store_true',help='rank candidate frames by slot feasibility and projected physical pin span')
            sub.add_argument('--pitch',type=float)
        if cmd in ('interfaces','routing'):
            sub.add_argument('--summary',action='store_true',help='summarize demand or infeasible contacts without all coordinates')
        if cmd == 'list':
            sub.add_argument('--stage', choices=['macro', 'final'], default='macro', help='stage used by --status')
            sub.add_argument('--status', choices=['pending', 'running', 'done', 'stale', 'failed', 'interrupted'])
        if cmd in ('show', 'move-io'):
            sub.add_argument('id')
        if cmd == "restore-best":
            sub.add_argument("--objective", choices=["hpwl","wns"], default="hpwl")
        if cmd == "verify-wns":
            sub.add_argument("--trial", required=True)
            sub.add_argument("--branch", required=True)
            sub.add_argument("--openroad", default="openroad")
            sub.add_argument("--timing-setup", type=Path)
            sub.add_argument("--keep", action="store_true")
        if cmd in ("assemble", "validate"):
            sub.add_argument("--trial", help="use an archived experiment")
            sub.add_argument("--branch", help="use this completed candidate from the experiment")
        if cmd == "assemble":
            sub.add_argument(
                "--def",
                dest="def_out",
                type=Path,
                help="write stitched final coordinates into a DEF",
            )
        if cmd == "validate":
            sub.add_argument(
                "--segments",
                action="store_true",
                help="retain global route segments for spatial calibration",
            )
            sub.add_argument("--openroad", default="openroad")
            sub.add_argument("--route", nargs=2, metavar=("FIRST_LAYER", "LAST_LAYER"))
            sub.add_argument("--capacity", type=float, default=0.75)
            sub.add_argument(
                "--timing-setup",
                type=Path,
                help="Tcl constraints setup to source before the design SDC",
            )
        if cmd in ("explore","interfaces"):
            sub.add_argument("--spread",action=argparse.BooleanOptionalAction,default=None,help="try top-level track spacing with a capacity fallback")
            sub.add_argument("--bus-order",action="store_true",help="order logical bits along shared boundaries")
        if cmd == "explore":
            sub.add_argument("kind", choices=["macro", "density", "interfaces"])
            sub.add_argument("--ids", nargs="+")
            sub.add_argument("--workers", type=int, default=1)
            sub.add_argument("--iterations", type=int, default=120)
            sub.add_argument("--final-iterations", type=int, default=300)
            sub.add_argument("--hypothesis",help="record what this fork is testing in the GUI experiment log")
            sub.add_argument("--pitch", type=float)
            sub.add_argument("--nets", type=int, nargs="+")
            sub.add_argument("--density", type=float, default=0.4)
            sub.add_argument("--keep", action="store_true")
            sub.add_argument("--objective", choices=["hpwl", "wns"], default="hpwl")
        if cmd == "interfaces":
            sub.add_argument("--pitch", type=float, help="logical slot spacing in DBU")
            sub.add_argument("--nets", type=int, nargs="+")
            sub.add_argument('--apply',action='store_true')
        if cmd == 'move-io':
            sub.add_argument('net', type=int, help='net_index from show')
            source = sub.add_mutually_exclusive_group(required=True)
            source.add_argument('--xy', type=float, nargs=2, metavar=('X', 'Y'))
            source.add_argument('--from-final', action='store_true', help='use the final placement boundary measurement')
            source.add_argument('--from-macro', action='store_true', help='accept the measured local pin centroid projected to the perimeter')
        if cmd=='next':
            sub.add_argument('--objective',choices=['hpwl','wns','density'],default='hpwl')
        if cmd == 'gui':
            sub.add_argument('--workers',type=int,help='maximum concurrent placement workers including trials')
            sub.add_argument('--compare-workdir', action='append', type=Path, default=[], help='add a design or floorplan alternative to this viewer (repeatable)')
            sub.add_argument('--no-open', action='store_true')
    for cmd in ("dispatch", "collect", "worker"):
        sub = cmds.add_parser(
            cmd,
            help={
                "dispatch": "issue a frozen portable placement request",
                "worker": "execute a request with the matching native build",
                "collect": "import a completed request if its inputs are current",
            }[cmd],
        )
        sub.add_argument("bundle", type=Path)
        if cmd == "worker":
            sub.add_argument("--cgroup-root", type=Path)
        if cmd != "worker":
            target = sub.add_mutually_exclusive_group()
            target.add_argument("--workdir", type=Path)
            target.add_argument("--manifest", type=Path)
        if cmd == "dispatch":
            sub.add_argument("stage", choices=["macro", "final"])
            sub.add_argument("home")
            sub.add_argument('--iterations',type=int)
    args = parser.parse_args(argv or ['--help'])
    if args.cmd == 'clocks':
        report = args.report
        if not report:
            directory = args.workdir.resolve() / 'synth/place' if args.workdir else next(
                (p for base in (Path.cwd(), *Path.cwd().parents) for p in (base, base / 'synth/place')
                 if (p / 'pcrs_clock.json').is_file()), None)
            if directory is None:
                raise ValueError('no saved clock tree here; use --workdir DIR or --report FILE')
            report = directory / 'pcrs_clock.json'
        raw = report.read_bytes()
        tree = json.loads(raw)
        entries = [dict(source_net=net['source_net'], **entry) for net in tree['networks']
                   for entry in net.get('chunk_entries', [])]
        if args.routed:
            digest = hashlib.sha256(raw).hexdigest()
            expected = {(e['source_net'], e['chunk'], e['instance'], e.get('input_pin', e.get('output_pin'))): e for e in entries}
            with args.routed.open() as stream:
                for row in csv.DictReader(stream, delimiter='\t'):
                    if row.get('report_sha256') != digest:
                        raise ValueError('routed arrivals belong to a different clock tree')
                    key = tuple(row[k] for k in ('source_net', 'chunk', 'instance', 'pin'))
                    if key not in expected:
                        raise ValueError('unexpected or repeated routed clock entry: ' + '/'.join(key))
                    expected.pop(key)['routed'] = {k: float(row[k]) if row[k] else None for k in
                                                  ('early_rise_ps', 'early_fall_ps', 'late_rise_ps', 'late_fall_ps')}
            if expected:
                raise ValueError('routed report is missing clock entries')
        homes = set(args.homes)
        unknown = homes - {e['chunk'] for e in entries} - set(tree.get('floorplan_chunks', []))
        if unknown:
            raise ValueError('unknown chunk in saved clock tree: ' + ', '.join(sorted(unknown)))
        print(json.dumps(dict(report=str(report.resolve()), plan_revision=tree.get('chunk_plan_revision'),
                              entries=[e for e in entries if not homes or e['chunk'] in homes]), indent=2))
        return
    if args.cmd == 'worker':
        print(
            json.dumps(
                chunk_dispatch.execute(exe, args.bundle, cgroup_root=args.cgroup_root),
                indent=2,
            )
        )
        return
    if args.manifest and args.manifest.name != 'chunks.json':
        work = Workspace(args.manifest, exe)
    else:
        directory = args.manifest.resolve().parent if args.manifest else place_dir(args.workdir)
        work = Workspace(current_manifest(directory), exe)
    if args.cmd == 'restore-best':
        print(json.dumps(chunk_explore.restore_best(work, args.objective), indent=2))
        return
    if args.cmd == 'verify-wns':
        print(json.dumps(chunk_validate.verify_wns(work, args.trial, args.branch, args.openroad, args.timing_setup, args.keep), indent=2))
        return
    if args.cmd == 'validate':
        print(
            json.dumps(
                chunk_validate.validate(
                    work,
                    args.openroad,
                    args.route,
                    args.capacity,
                    args.timing_setup,
                    args.segments,
                    args.trial,
                    args.branch,
                ),
                indent=2,
            )
        )
        return
    if args.cmd == 'explore':
        print(
            json.dumps(
                chunk_explore.explore(
                    work,
                    args.kind,
                    args.workers,
                    args.iterations,
                    args.final_iterations,
                    args.ids,
                    args.pitch,
                    args.nets,
                    args.density,
                    args.keep,
                    args.objective,spread=args.spread,bus_order=args.bus_order,hypothesis=args.hypothesis,
                ),
                indent=2,
            )
        )
        return
    if args.cmd in ('routing', 'interfaces', 'shapes', 'resources'):
        print(json.dumps(work.analysis(args.cmd, vars(args)), indent=2))
        return
    if args.cmd == 'dispatch':
        print(
            json.dumps(
                {
                    'request': chunk_dispatch.issue(
                        work,
                        args.home,
                        args.stage,
                        args.bundle,
                        iterations=args.iterations,
                    )
                }
            )
        )
        return
    if args.cmd == "collect":
        print(json.dumps(chunk_dispatch.collect(work, args.bundle), indent=2))
        return
    if args.cmd == 'gui':
        if args.workers is not None and args.workers<1:
            raise ValueError('--workers must be positive')
        workers = args.workers
        project = work.manifest.parent / PROJECT
        if workers is None and project.is_file():
            workers = json.loads(project.read_text()).get('workers')
        return serve(work, Path(page), args.no_open, workers=workers, compare_dirs=args.compare_workdir)
    if args.cmd in ('assemble', 'score', 'next'):
        state = work.state()
        if args.cmd == 'assemble':
            state = chunk_validate.placement_state(work, args.trial, args.branch)
            value = (
                chunk_validate.stitch(work, state, args.def_out)
                if args.def_out
                else LivePlacement.assemble(work, state)
            )
        elif args.cmd == 'score':
            value = Exterior(work, state).measure()
        else:
            value = priorities(work, state, args.objective)
        print(json.dumps(value, indent=2))
        return
    if args.cmd == 'list':
        value = work.display_rows(work.rows(work.state(), work.select(args.ids, args.neighbors)))
        if args.status:
            value = [r for r in value if r[args.stage]['status'] == args.status]
        if not args.json:
            width = max((len(r['id']) for r in value), default=2)
            print(f'{"ID":<{width}}  {"Macro":<12} {"Final":<12} {"Est. MiB":>10} {"Budget MiB":>12}')
            for r in value:
                estimate = f"{r['estimated_bytes'] / 1048576:.2f}" if r['exported'] else 'unexported'
                print(f"{r['id']:<{width}}  {r['macro']['status']:<12} {r['final']['status']:<12} "
                      f"{estimate:>10} {r['budget_bytes'] / 1048576:12.2f}")
            return
    elif args.cmd == 'show':
        value = work.show(work.state(), args.id)
    elif args.cmd == 'move-io':
        value = work.move_io(args.id, args.net, args.xy or [], 'final' if args.from_final else args.from_macro)
    else:
        project = work.manifest.parent / PROJECT
        workers = (
            args.workers
            if args.workers is not None
            else (
                json.loads(project.read_text()).get("workers")
                if project.exists()
                else None
            )
        )
        if args.cgroup_root and workers is None:
            workers = 1
        if args.cmd == 'final':
            required = work.macro_prerequisites(work.select(args.ids, args.neighbors), work.state())
            if required:
                if workers is None:
                    work.run('macro', required)
                else:
                    chunk_dispatch.wave(work, 'macro', required, workers, cgroup_root=args.cgroup_root)
        if workers is None:
            value = work.run(
                args.cmd, args.ids, args.neighbors, iterations=args.iterations
            )
        else:
            ids = (
                work.select(args.ids, args.neighbors)
                if args.ids or args.neighbors
                else None
            )
            value=chunk_dispatch.wave(work,args.cmd,ids,workers,iterations=args.iterations,cgroup_root=args.cgroup_root)
        if sys.stdout.isatty() and not args.json:
            if not value:
                print(f'All exported chunks have current {args.cmd} placement.')
            for row in value:
                stage = row[args.cmd]
                print(f"{row['id']}  {args.cmd} {stage['status']}  {stage['worker_seconds']:.2f} s  "
                      f"{stage['peak_rss_bytes'] / 1048576:.1f} MiB")
            return
    print(json.dumps(value, indent=2))


if __name__ == '__main__':
    try:
        main(sys.argv[1], sys.argv[2], sys.argv[3:])
    except (ValueError, OSError, KeyError) as error:
        sys.exit(f'lhd synth chunk: {error}')

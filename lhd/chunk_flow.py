# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Coordinate pre-CTS stages on the immutable packets exported by fp_plan."""
import argparse
import contextlib
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
import re
from pathlib import Path
import secrets
import subprocess
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
        plan = one(records, 'chunk_plan', self.design)
        self.chunks = {c['id']: {'box_dbu': c['box_dbu'], 'ports': c['ports']} for c in plan['chunks']}
        self.members = {c['id']: frozenset((m['macro'], m['id']) for m in c['members']) for c in plan['chunks']}
        self.jobs = {j['id']: j for j in self.index['jobs']}
        # Bind state to the exact export, including worker options and admission policy.
        self.root = self.manifest.parent / '.chunk-work' / hashlib.sha256(raw).hexdigest()
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
                box = self.chunks[ident]['box_dbu']
                selected.update(k for k, c in self.chunks.items() if adjacent(box, c['box_dbu']))
        return [k for k in self.chunks if k in selected]

    def rows(self, state, ids):
        rows = []
        for k in ids:
            job = self.jobs.get(k, {})
            result = {}
            for stage, saved in state['results'].get(k, {}).items():
                result[stage] = saved.copy()
                for field in ('directory', 'report', 'positions', 'preview', 'handoff'):
                    if field in saved:
                        result[stage][field] = os.path.normpath(self.root / saved[field])
            rows.append(dict(id=k, box_dbu=self.chunks[k]['box_dbu'], exported=bool(job),
                             estimated_bytes=job.get('estimated_bytes'), budget_bytes=self.index['budget_bytes'],
                             macro=result.get('macro', {'status': 'pending'}), final=result.get('final', {'status': 'pending'})))
        return rows

    def show(self, state, ident):
        row, = self.rows(state, self.select([ident]))
        edits = state['contacts'].get(ident, {})
        row['ports'] = [dict(p, xy_dbu=edits.get(str(p['net_index']), p['xy_dbu'])) for p in self.chunks[ident]['ports']]
        row['neighbors'] = [k for k in self.select([ident], True) if k != ident]
        return row

    def move_io(self, ident, net, xy, measured=False):
        with self.writer() as (state, _):
            row = self.show(state, ident)
            port = next((p for p in row['ports'] if p['net_index'] == net), None)
            if port is None:
                raise ValueError(f'no planned contact {net} on {ident}')
            if measured:
                macro = row['macro']
                if macro['status'] != 'done':
                    raise ValueError('place this chunk at the macro stage first')
                report = one(json.loads(Path(macro['report']).read_text()), 'chunk_feedback', self.design)
                sample = next((p for p in report.get('boundary', []) if p['net_index'] == net), None)
                if sample is None:
                    raise ValueError('macro stage has no measurement for this net')
                xy = [a + b for a, b in zip(sample['contact_dbu'], sample['mean_pin_offset_dbu'])]
                xy = boundary(row['box_dbu'], xy)
            if len(xy) != 2 or not all(math.isfinite(v) for v in xy) or boundary(row['box_dbu'], xy) != list(xy):
                raise ValueError('use a point on the selected chunk perimeter in die DBU')
            if list(xy) == port['xy_dbu']:
                return {'changed': []}
            affected = []
            for k, c in self.chunks.items():
                if any(p['net_index'] == net for p in c['ports']):
                    affected.append(k)
                    state['contacts'].setdefault(k, {})[str(net)] = boundary(c['box_dbu'], xy)
                    for result in state['results'].get(k, {}).values():
                        result.update(status='stale', reason=f'{ident} moved net {net}')
            self.save(state)
            return {'changed': affected}

    def run(self, stage, ids, neighbors=False):
        selected = self.select(ids, neighbors)
        with self.writer() as (state, lock_fds), open(self.exe, 'rb') as binary:
            # A long placement queue can outlive a rebuild of lhd. Keep using the
            # same executable inode for each worker, even during an atomic replacement.
            fd_path = Path(f'/proc/self/fd/{binary.fileno()}')
            worker, worker_fds = self.exe, lock_fds
            if fd_path.exists():
                worker, worker_fds = fd_path, (*lock_fds, binary.fileno())
            if not ids:
                selected = [k for k in self.jobs if state['results'].get(k, {}).get(stage, {}).get('status') != 'done']
            for k in selected:
                if k not in self.jobs:
                    raise ValueError(f'{k} has no exported packet')
                if stage == 'final' and state['results'].get(k, {}).get('macro', {}).get('status') != 'done':
                    raise ValueError(f'{k} requires current macro placement')
            for k in selected:
                result = state['results'].setdefault(k, {})
                job = self.jobs[k].copy()
                job['input'] = str(self.manifest.parent / job['input'])
                if stage == 'final':
                    job.update(input=str(self.root / result['macro']['handoff']), checksum=result['macro']['handoff_checksum'])
                elif 'final' in result:
                    result['final'].update(status='stale', reason='macro placement rerun')
                contacts = sorted([int(net), *xy] for net, xy in state['contacts'].get(k, {}).items())
                job['request'] = {'class': PREFIX + 'chunk_request', 'stage': stage, 'id': k,
                                  'design': self.design, 'contacts': contacts}
                # An edited frame must never appear current when the base fp_plan is reopened.
                base = self.index['plan_revision']
                inputs = json.dumps([base, stage, job['checksum'], contacts]).encode()
                revision = 'chunk-' + hashlib.sha256(inputs).hexdigest()
                run = Path(tempfile.mkdtemp(prefix=stage + '-', dir=self.root))
                job['input'] = os.path.relpath(job['input'], run)
                manifest = run / 'job.json'
                manifest.write_text(json.dumps([dict(self.index, jobs=[job], base_plan_revision=base, plan_revision=revision)]))
                result[stage] = {'status': 'running', 'directory': run.name}
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
                    for key in ('hpwl_dbu', 'peak_rss_bytes', 'worker_seconds', 'over_budget', 'solver_movable_nodes'):
                        if key in report:
                            result[stage][key] = report[key]
                    if rc or report['status'] not in ('placed', 'fixed_only'):
                        raise ValueError(report.get('reason', f'worker exit {rc}'))
                    for artifact in ('positions', 'preview'):
                        if artifact in report:
                            result[stage][artifact] = str((out / report[artifact]).relative_to(self.root))
                    if stage == 'macro':
                        result[stage].update(handoff=str((out / '0/handoff.bin').relative_to(self.root)), handoff_checksum=report['handoff_checksum'])
                    result[stage]['status'] = 'done'
                except (ValueError, OSError, KeyError) as error:
                    result[stage].update(status='failed', reason=str(error))
                    raise
                finally:
                    self.save(state)
        return self.rows(state, selected)


def serve(work, page, no_open):
    token = secrets.token_urlsafe(32)
    managed = (work.manifest.parent / PROJECT).exists()

    class Handler(BaseHTTPRequestHandler):
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
            nonlocal work
            if self.headers.get('Authorization') != 'Bearer ' + token or self.path != '/chunk':
                self.send_error(403)
                return
            try:
                size = int(self.headers['Content-Length'])
                if not 0 < size <= 65536:
                    raise ValueError('invalid request size')
                req = json.loads(self.rfile.read(size))
                active = work
                if managed:
                    path = current_manifest(active.manifest.parent)
                    if path != active.manifest:
                        active = Workspace(path, active.exe)
                        work = active
                self.generation = active.root.name
                cmd = req['cmd']
                if cmd != 'list' and req.get('generation', self.generation) != self.generation:
                    raise ValueError('floorplan changed; refresh the selection')
                if cmd == 'list':
                    answer = active.rows(active.state(), active.chunks)
                elif cmd == 'show':
                    answer = active.show(active.state(), req['id'])
                elif cmd == 'preview':
                    row, = active.rows(active.state(), active.select([req['id']]))
                    result = row[req['stage']]
                    if result['status'] != 'done' or 'preview' not in result:
                        raise ValueError('no current placement preview')
                    answer = one(json.loads(Path(result['preview']).read_text()), 'chunk_preview', active.design)
                elif cmd == 'move-io':
                    answer = active.move_io(req['id'], req['net'], req.get('xy', []), req.get('measured', False))
                elif cmd in ('macro', 'final'):
                    answer = active.run(cmd, req['ids'], req.get('neighbors', False))
                else:
                    raise ValueError('unknown chunk command')
                self.reply(json.dumps(answer).encode(), 'application/json')
            except (ValueError, TypeError, OSError, KeyError) as e:
                self.reply(json.dumps({'error': str(e)}).encode(), 'application/json', 400)

    with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as http:
        url = f'http://127.0.0.1:{http.server_port}/#{token}'
        print(url, flush=True)
        if not no_open:
            webbrowser.open(url)
        http.serve_forever()


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
    before, after = interfaces(old), interfaces(new)
    changed_io = set()
    stable_nets = set()
    for net in before.keys() | after.keys():
        if before.get(net) != after.get(net):
            changed_io.update(before.get(net, {}))
            changed_io.update(after.get(net, {}))
        else:
            stable_nets.add(net)
    for ident, edits in prior['contacts'].items():
        for key, xy in edits.items():
            net = int(key)
            if net in stable_nets and ident in after[net]:
                state['contacts'].setdefault(ident, {})[key] = xy
    changed, reused, repartitioned = [], [], []
    for ident in new.jobs.keys() & old.jobs.keys():
        reasons = []
        if new.members[ident] != old.members[ident]:
            repartitioned.append(ident)
            reasons.append('chunk membership')
        if old.jobs[ident]['checksum'] != new.jobs[ident]['checksum']:
            reasons.append('worker packet')
        if ident in changed_io:
            reasons.append('shared IO')
        if not same_engine or old.index['require_fit'] != new.index['require_fit']:
            reasons.append('worker version or admission policy')
        (changed if reasons else reused).append(ident)
        stages = prior['results'].get(ident)
        if not stages:
            continue
        for stage, saved in stages.items():
            row = saved.copy()
            for field in ('directory', 'report', 'positions', 'preview', 'handoff'):
                if field in row:
                    row[field] = os.path.relpath(old.root / row[field], new.root)
            if reasons:
                row.update(status='stale', reason='update changed ' + ', '.join(reasons))
            state['results'].setdefault(ident, {})[stage] = row
    new.save(state)
    return dict(unchanged=sorted(reused), changed=sorted(changed), membership_changed=sorted(repartitioned),
                added=sorted(new.jobs.keys() - old.jobs.keys()), removed=sorted(old.jobs.keys() - new.jobs.keys()))


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


def setup_project(exe, page, argv, updating):
    index = argv.index('--') if '--' in argv else len(argv)
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument('--workdir', type=Path)
    parser.add_argument('--budget', metavar='SIZE')
    opts, args = parser.parse_known_args(argv[:index])
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
            args += argv[index:]
            project = dict(cwd=str(Path.cwd()), args=export_args(args, True))
        if opts.budget:
            match = re.fullmatch(r'(\d+(?:\.\d+)?|\.\d+)(MiB|GiB)?', opts.budget, re.I)
            if not match or not 0 < float(match[1]) < math.inf:
                raise ValueError('--budget expects a positive size such as 1024MiB or 1GiB')
            project['budget_mib'] = float(match[1]) * (1024 if (match[2] or '').lower() == 'gib' else 1)
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
            native += ['--set', f"lhd.ram_budget_mb={project['budget_mib']}"]
        if updating:
            native = [a for a in native if a not in ('--gui', '--gui-no-open')]
            native += gui_args
            if '--gui' in gui_args and not any(a.startswith('synth.place.gui.pause=') for a in native):
                native += ['--set', 'synth.place.gui.pause=true']
            if project.get('guidance'):
                native.append(str(directory / project['guidance']))
        args = native + reader
        log = directory / 'chunk-build.log'
        # The native synth driver reuses its normal compile and mapping caches.
        # Publish the snapshot only after the export and state reconciliation both succeed.
        with log.open('w') as out:
            rc = subprocess.run([exe, 'synth', *args], cwd=project['cwd'], stdout=out, pass_fds=(lock_fd,)).returncode
        if rc:
            raise ValueError(f'floorplan export failed (exit {rc}); see {log}')
        work = Workspace(directory / 'chunks.json', exe)
        snapshot = directory / ('chunks-' + work.root.name + '.json')
        if not snapshot.exists():
            os.link(work.manifest, snapshot)
        if '--gui' in native and work.guidance:
            # Replay the accepted editing intent. A full saved plan would pin the
            # old source revision and prevent the planner from adapting to an ECO.
            guidance = work.root / 'accepted-guidance.json'
            atomic_json(guidance, work.guidance)
            project['guidance'] = str(guidance.relative_to(directory))
        elif '--gui' in native:
            project.pop('guidance', None)
        summary = reconcile(old, work, engine == project['engine']) if updating else {'added': list(work.jobs)}
        project.update(manifest=snapshot.name, engine=engine)
        atomic_json(path, project)
        print(json.dumps(dict(workdir=str(directory.parent.parent), chunks=len(work.jobs), **summary), indent=2))
    if '--gui' in native:
        serve(Workspace(snapshot, exe), Path(page), '--gui-no-open' in native)


def main(exe, page, argv):
    native_args = argv[:argv.index('--')] if '--' in argv else argv
    if argv[:1] in (['init'], ['update']) and not any(a in ('--help', '-h') for a in native_args[1:]):
        return setup_project(exe, page, argv[1:], argv[0] == 'update')
    if argv[:1] == ['plan'] and argv[1:] not in (['--help'], ['-h']):
        command = [exe, 'synth', *export_args(argv[1:], False)]
        if '--gui' not in argv:
            os.execv(exe, command)
        rc = subprocess.run(command).returncode
        if rc:
            raise SystemExit(rc)
        # Continue from the accepted interactive plan after synthesis releases its flat view.
        opts = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
        opts.add_argument('--workdir', type=Path, required=True)
        workdir = opts.parse_known_args(argv[1:])[0].workdir
        return serve(Workspace(workdir / 'synth/place/chunks.json', exe), Path(page), '--gui-no-open' in argv)
    parser = argparse.ArgumentParser(prog='lhd synth chunk')
    cmds = parser.add_subparsers(dest='cmd', required=True)
    init = cmds.add_parser('init', usage='%(prog)s SOURCES [--workdir DIR] [--budget SIZE] [synth options]',
                           help='create a floorplan and remember the source recipe for update',
                           description='Pass the same HDL, top, Liberty, LEF and reader options as lhd synth.')
    init.add_argument('--budget', metavar='SIZE', help='per-worker memory budget, for example 1024MiB or 1GiB')
    init.add_argument('--workdir', metavar='DIR', help='new project directory (default: current directory)')
    update = cmds.add_parser('update', help='rebuild from the saved source recipe and reconcile chunks')
    for sub in (init, update):
        sub.add_argument('--gui', action='store_true', help='pause in the interactive floorplan editor')
        sub.add_argument('--gui-no-open', action='store_true', help='print GUI access details without opening a browser')
    update.add_argument('--budget', metavar='SIZE', help='resize the floorplan using a new worker budget')
    update.add_argument('--workdir', metavar='DIR', help='project to update (default: current project)')
    cmds.add_parser('plan', usage='%(prog)s SOURCES --workdir DIR [synth options]',
                    help='export fp_plan with the usual synth inputs',
                    description='Supply the synthesis sources and technology. Set --set lhd.ram_budget_mb=N to size the chunks. '
                                'Use --gui to edit the initial plan and continue into chunk placement.')
    for cmd, help_text in [('list', 'list chunks and their stage status'), ('show', 'inspect a chunk and its IO contacts'),
                           ('macro', 'place macros and save the fixed frame'), ('final', 'place cells in the accepted macro frame'),
                           ('move-io', 'move a contact and invalidate all homes on its net'), ('gui', 'open chunk placement controls')]:
        sub = cmds.add_parser(cmd, help=help_text)
        target = sub.add_mutually_exclusive_group()
        target.add_argument('--manifest', type=Path, help='exported chunk manifest')
        target.add_argument('--workdir', type=Path, help='synthesis workspace (auto-detected inside a project)')
        if cmd in ('list', 'macro', 'final'):
            sub.add_argument('ids', nargs='*')
            sub.add_argument('--neighbors', action='store_true', help='include homes touching or overlapping the selected boxes')
        if cmd in ('list', 'macro', 'final'):
            sub.add_argument('--json', action='store_true', help='include machine-readable feedback and artifact paths')
        if cmd == 'list':
            sub.add_argument('--stage', choices=['macro', 'final'], default='macro', help='stage used by --status')
            sub.add_argument('--status', choices=['pending', 'running', 'done', 'stale', 'failed', 'interrupted'])
        if cmd in ('show', 'move-io'):
            sub.add_argument('id')
        if cmd == 'move-io':
            sub.add_argument('net', type=int, help='net_index from show')
            source = sub.add_mutually_exclusive_group(required=True)
            source.add_argument('--xy', type=float, nargs=2, metavar=('X', 'Y'))
            source.add_argument('--from-macro', action='store_true', help='accept the measured local pin centroid projected to the perimeter')
        if cmd == 'gui':
            sub.add_argument('--no-open', action='store_true')
    args = parser.parse_args(argv or ['--help'])
    if args.manifest and args.manifest.name != 'chunks.json':
        work = Workspace(args.manifest, exe)
    else:
        directory = args.manifest.resolve().parent if args.manifest else place_dir(args.workdir)
        work = Workspace(current_manifest(directory), exe)
    if args.cmd == 'gui':
        return serve(work, Path(page), args.no_open)
    if args.cmd == 'list':
        value = work.rows(work.state(), work.select(args.ids, args.neighbors))
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
        value = work.move_io(args.id, args.net, args.xy or [], args.from_macro)
    else:
        value = work.run(args.cmd, args.ids, args.neighbors)
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

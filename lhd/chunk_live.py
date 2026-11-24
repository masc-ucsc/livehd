# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Interactive chunk placements. Trials consume immutable handoffs and publish by revision."""

import concurrent.futures
import contextlib
import copy
import hashlib
import json
import math
import os
import secrets
import sqlite3
import tempfile
import threading
import time
from functools import lru_cache
from pathlib import Path

from chunk_dispatch import active_jobs
from chunk_journal import Journal
from chunk_score import Exterior, priorities, report_score


def project_activity(root):
    """Read small artifacts shared by GUI, CLI and dispatched workers."""
    def stamp(path):
        try:
            return path.stat().st_mtime
        except FileNotFoundError:
            return 0

    events = []
    def note(stamp, stage, **fields):
        if stamp:
            events.append(dict(time=stamp, stage=stage, **fields))

    note(stamp(root / "state.json"), "Placement saved")
    last_wave = max((root / "waves").glob("*.json"), key=stamp, default=None)
    if last_wave:
        wave = json.loads(last_wave.read_text())
        note(wave["completed"], wave["stage"] + " placement complete")
    for logfile in (root / "validation").glob("*/*/openroad.log"):
        result = logfile.parent / "result.json"
        request = logfile.parent / "request.json"
        context = json.loads(request.read_text()) if request.is_file() else {}
        stage = "OpenROAD global route" if context.get("route") else "OpenROAD validation"
        if result.is_file():
            stage += " complete"
        note(stamp(result) or stamp(logfile), stage, trial=context.get("trial"), branch=context.get("branch"))
    # Leases are held by the actual worker. Reading them also follows a remote
    # request collected by a different coordinator in this workspace.
    def workers(directory, **fields):
        for h, job in active_jobs(directory).items():
            attempt = Path(job["directory"])
            image = attempt / "result/0/progress.json"
            note(stamp(image) or stamp(attempt), job["stage"], home=h, running=True, **fields)

    workers(root)
    db = root / "experiments.sqlite3"
    if db.is_file():
        with contextlib.closing(sqlite3.connect(db.as_uri() + "?mode=ro", uri=True)) as conn:
            for (body,) in conn.execute("SELECT body FROM experiments ORDER BY started DESC LIMIT 12"):
                trial = json.loads(body)
                for branch, row in trial.get("candidates", {}).items():
                    if row.get("status") == "running" and row.get("directory"):
                        workers(Path(row["directory"]), trial=trial["id"], branch=branch)
                note(trial.get("finished") or trial.get("started"), trial.get("kind","Trial"),
                     trial=trial["id"])
    return max(events, key=lambda e: (e.get("running", False), e["time"]), default=None)


def revision(state):
    return hashlib.sha256(json.dumps(state, sort_keys=True).encode()).hexdigest()


@lru_cache(maxsize=128)
def archived_revision(state_path):
    return revision(json.loads(Path(state_path).read_text()))


def validation_file(work, row):
    if row.get('state_path'):
        return work.root / 'validation' / archived_revision(row['state_path']) / 'latest.json'
    return None


def read_validation(path):
    if not path or not path.is_file():
        return None
    result = json.loads(path.read_text())
    routed = path.with_name('global-routing.json')
    if 'routed_um' not in result and routed.is_file():
        result['previous_global_route'] = json.loads(routed.read_text())
    return result


def finite(value):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise ValueError('coordinates must be finite numbers in design DBU')
    return value


def engine_identity(path):
    info = os.stat(path)
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def publish_trial(work, journal, trial, branch, checkpoint, epoch, engine, after):
    """Conditionally publish a frozen branch and preserve the prior main for undo."""
    with work.writer() as (current, _):
        if revision(current) != checkpoint or journal.epoch() != epoch or engine_identity(work.exe) != tuple(engine):
            raise ValueError('main changed since the fork; keep refused')
        journal.checkpoint(revision(after), current, trial, branch)
        journal.epoch(advance=True)
        work.save(after)


class WorkerAdmission:
    def __init__(self, slots):
        self.slots = slots
        self.active_workers = 0
        self.admission = threading.Condition()
        self.reserved = 0
        self.capacity = os.sysconf('SC_PHYS_PAGES') * os.sysconf('SC_PAGE_SIZE') // 2

    @contextlib.contextmanager
    def admit(self, required):
        required = min(self.capacity, required)
        with self.admission:
            self.admission.wait_for(
                lambda: self.active_workers < self.slots and self.reserved + required <= self.capacity
            )
            self.reserved += required
            self.active_workers += 1
        try:
            yield
        finally:
            with self.admission:
                self.reserved -= required
                self.active_workers -= 1
                self.admission.notify_all()


class LivePlacement:
    def __init__(self, work, workers=None, *, admission=None):
        self.lock = threading.RLock()
        self.slots = workers or max(1, min(4, os.cpu_count() or 1))
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=self.slots)
        self.admission = admission or WorkerAdmission(self.slots)
        self.admit = self.admission.admit
        self.bind(work)

    def bind(self, work):
        with self.lock:
            self.work = work
            results = work.state()['results']
            self.home = next(
                (k for k in work.jobs if results.get(k, {}).get('macro', {}).get('status') == 'done'),
                next(iter(work.jobs), next(iter(work.chunks), '')),
            )
            self.selected = []
            self.stage = 'final' if results.get(self.home,{}).get('final',{}).get('status')=='done' else 'macro'
            self.homes = []
            self.port = None
            self.floorplan_selection = []
            focus = work.manifest.parent / 'chunk-selection.json'
            if focus.is_file():
                saved = json.loads(focus.read_text())
                if saved.get('revision') == work.index['plan_revision']:
                    self.floorplan_selection = saved['members']
                    self.homes = list(
                        dict.fromkeys(m['home'] for m in self.floorplan_selection if m['home'] in work.jobs)
                    )
                    if self.homes:
                        self.home = self.homes[0]
                        self.selected = [
                            m['physical_name']
                            for m in self.floorplan_selection
                            if m.get('physical_name') and m['home'] == self.home
                        ]
            self.journal = Journal(work.root)
            self.trials = {}
            self.accepted_cache = None
            self.objective = 'hpwl'
            self.map_cache = None
            self.view = None
            self.version = 0

    def update(self):
        self.version += 1

    def record(self, work, after, before):
        with self.lock:
            if work is self.work and revision(work.state()) == revision(after):
                if revision(after) != revision(before):
                    self.journal.checkpoint(revision(after), before)
                    self.accepted(work, before)
                    self.accepted(work, after)
                self.journal.epoch(advance=True)
                self.view = None
                self.update()

    def result(self, work, state, home, stage='macro'):
        row = work.rows(state, [home])[0][stage]
        if row['status'] != 'done':
            raise ValueError(f'{home} needs a completed {stage} placement')
        return row

    def geometry(self, work, state, home, row=None, full=False, preview=None):
        row = row or self.result(work, state, home)
        preview = preview or json.loads(Path(row['preview']).read_text())[0]
        if preview.get('owned') == 0:
            return {}
        boxes = preview['boxes_dbu']
        names = preview.get('names')
        if names is None or full:
            positions = json.loads(Path(row['positions']).read_text())[0]
            macros = {i: [w, h] for i, w, h in positions['macro_sizes_dbu']}
        if names is None:
            n = len(positions['nodes'])
            strides = [max(1, math.ceil(count / 1024)) for count in (n - len(macros), len(macros))]
            seen = [0, 0]
            names = []
            for i, node in enumerate(positions['nodes']):
                kind = int(i in macros)
                if seen[kind] % strides[kind] == 0:
                    names.append(node[0])
                seen[kind] += 1
        if len(names) != len(boxes):
            raise ValueError('placement preview names and boxes differ')
        result = dict(zip(names, boxes))
        if full:
            sizes = positions.get('sizes_dbu')
            for i, (name, x, y) in enumerate(positions['nodes']):
                if name not in result:
                    size = sizes[i] if sizes else macros.get(i, [None, None])
                    result[name] = [x, y, *size, int(i in macros)]
        return result

    def layout(self, work, home, geometry, selection, edit, holds, image):
        if not isinstance(edit, dict) or set(edit) - {
            'swap',
            'align',
            'side',
            'gap',
            'x',
            'y',
            'dx',
            'dy',
            'fix',
            'release',
        }:
            raise ValueError('unknown edit field')
        if edit and not selection:
            raise ValueError('select objects before editing')
        if any(n not in geometry for n in selection):
            raise ValueError('selection is no longer in this placement')
        if (edit.get('align') or edit.get('side')) and any(None in geometry[n][2:4] for n in selection):
            raise ValueError('regenerate this older placement to use the cell dimensions for alignment')
        x0, y0, x1, y1 = work.chunks[home]['box_dbu']
        align, side = edit.get('align'), edit.get('side')
        if align not in (None, 'row', 'column') or side not in (
            None,
            'left',
            'right',
            'top',
            'bottom',
        ):
            raise ValueError('unknown alignment or side')
        axis = int(align == 'column')
        order = sorted(selection, key=lambda n: (geometry[n][axis], n))
        gap = finite(edit.get('gap', 0))
        if gap < 0:
            raise ValueError('gap must be nonnegative')
        xy = {n: geometry[n][:2] for n in order}
        if 'swap' in edit:
            if edit['swap'] is not True or set(edit) - {'swap', 'fix', 'release'} or len(order) != 2:
                raise ValueError('swap needs exactly two selected macros and optional axis holds')
            first, second = order
            if not all(geometry[n][4] and None not in geometry[n][2:4] for n in order) or geometry[first][2:4] != geometry[second][2:4]:
                raise ValueError('swap needs two macros with matching physical dimensions')
            xy[first], xy[second] = xy[second], xy[first]
        if align and order:
            cursor = min(xy[n][axis] for n in order)
            level = round(sum(xy[n][1 - axis] for n in order) / len(order))
            for n in order:
                xy[n][axis] = cursor
                xy[n][1 - axis] = level
                cursor += geometry[n][2 + axis] + gap
        if side and order:
            k = int(side in ('top', 'bottom'))
            lower = side in ('left', 'bottom')
            shift = (
                (x0, y0)[k] - min(p[k] for p in xy.values())
                if lower
                else (x1, y1)[k] - max(xy[n][k] + geometry[n][2 + k] for n in order)
            )
            for point in xy.values():
                point[k] += shift
        for action in ('fix', 'release'):
            if action in edit and edit[action] not in ('x', 'y', 'xy'):
                raise ValueError(f'{action} must specify x, y or xy')
        if (side or align) and edit.get('fix') and image.get('grid_dbu'):
            halos = image.get('macro_halos_dbu', {})
            for k, a in enumerate('xy'):
                if a not in edit['fix'] or a in edit or 'd' + a in edit:
                    continue  # explicit DBU coordinates stay exact
                origin, end = work.chunks[home]['box_dbu'][k::2]
                step = image['grid_dbu'][k]
                reverse = side in ('right', 'top') and k == int(side == 'top')
                ordered = sorted(order, key=lambda n: xy[n][k], reverse=reverse)
                previous = None
                for n in ordered:
                    halo = halos.get(n, [0, 0])[k]
                    low = math.ceil(halo / step)
                    high = math.floor((end - origin - halo - geometry[n][2 + k]) / step)
                    if align and k == axis and previous is not None:
                        margin = max(gap, halo + halos.get(previous, [0, 0])[k])
                        if reverse:
                            high = min(
                                high,
                                math.floor((xy[previous][k] - origin - margin - geometry[n][2 + k]) / step),
                            )
                        else:
                            low = max(
                                low,
                                math.ceil((xy[previous][k] - origin + margin + geometry[previous][2 + k]) / step),
                            )
                    if low > high:
                        raise ValueError('aligned objects and their halos do not fit in the chunk')
                    xy[n][k] = origin + step * min(high, max(low, round((xy[n][k] - origin) / step)))
                    previous = n
        poses = {}
        for name in order:
            locked = holds.setdefault(name, [None, None])
            for k, a in enumerate('xy'):
                if a in edit:
                    xy[name][k] = finite(edit[a])
                xy[name][k] += finite(edit.get('d' + a, 0))
                if a in edit.get('release', ''):
                    locked[k] = None
                if a in edit.get('fix', ''):
                    locked[k] = xy[name][k]
                elif locked[k] is not None and xy[name][k] != locked[k]:
                    raise ValueError(f'{name} has a fixed {a} coordinate; release it or set a new fix')
            poses[name] = xy[name]
        holds_copy = {n: p for n, p in holds.items() if any(v is not None for v in p)}
        rows = []
        for n in sorted(set(poses) | set(holds_copy)):
            locks = holds_copy.get(n, [None, None])
            rows.append([n, *poses.get(n, locks), *(v is not None for v in locks)])
        return rows, holds_copy

    def solve(self, work, branch, state, home, rows, poses, holds, steps, engine, density, phase, io, coordinate_io, centroids):
        seed_state = copy.deepcopy(state)
        if centroids is not None:
            seed_state.setdefault('centroids', {})[home] = centroids
        # All inherited paths refer to the parent's immutable artifacts.
        for stages in seed_state['results'].values():
            for result in stages.values():
                for key in (
                    'directory',
                    'report',
                    'previous_report',
                    'preview',
                    'handoff',
                    'positions',
                ):
                    if key in result:
                        result[key] = str(work.root / result[key])
        branch.save(seed_state)
        if io:
            branch.move_io(home, io['net'], io['xy'])
        measured = {}
        for ident, row in rows.items():
            seed = {'path': row['handoff'], 'checksum': row['handoff_checksum']}
            frozen = phase == 'final' and state['results'][ident].get('macro', {}).get('status') == 'done'
            with self.admit(max(work.jobs[ident]['estimated_bytes'], row.get('peak_rss_bytes', 0))):
                if engine_identity(work.exe) != engine:
                    raise ValueError('placement executable changed since the fork')
                (result,) = branch.run(
                    phase,
                    [ident],
                    seed=seed,
                    poses=poses if ident == home else (),
                    iterations=steps,
                    density=density if ident == home else None,
                    freeze_macros=frozen,
                    negotiate=not coordinate_io,
                )
            measured[ident] = result[phase]
            actual = result[phase]['iterations']
            if actual < steps and result[phase]['solver_movable_nodes']:
                raise ValueError(f'{ident} stopped after {actual} of {steps} iterations')
        if engine_identity(work.exe) != engine:
            raise ValueError('placement executable changed during the trial')
        stage = measured[home]
        requested = {p[0]: p[1:] for p in poses}
        resolved = {}
        if requested:
            geometry = self.geometry(work, state, home, stage, full=True)
            resolved = {
                n: {'requested_dbu': pose[:2], 'fixed_axes': pose[2:], 'box_dbu': geometry[n][:4]}
                for n, pose in requested.items()
            }
        saved = branch.state()
        for ident, worker in measured.items():
            saved['results'][ident][phase] = worker.copy()
        exterior = work.jobs.keys() - measured.keys()
        # Dereferenced paths identify immutable worker outputs. Validate every
        # exterior stage without loading any of its cells.
        for old, new in zip(work.rows(state, sorted(exterior)), branch.rows(saved, sorted(exterior))):
            if any(old[s] != new[s] for s in ('macro', 'final')):
                raise ValueError(f'trial changed exterior placement {old["id"]}')
        saved.setdefault('holds', {})[home] = holds
        branch.save(saved)
        return {
            'result': stage,
            'resolved': resolved,
            'state': saved,
            'directory': branch.root,
            'iterations': stage['iterations'],
            'hpwl_dbu': stage['hpwl_dbu'],
            'measured': measured,
            'frozen_homes': len(exterior),
        }

    def progress(self, trial, label):
        branch = trial['branches'][label]
        state = branch.read_state()
        for home in trial['affected_homes']:
            frame = self.worker_progress(branch, state, home, trial['stage'])
            if frame:
                return dict(frame, home=home)
        return None

    def worker_progress(self, branch, state, home, stage):
        row = state['results'].get(home, {}).get(stage, {})
        pending = active_jobs(branch.root).get(home, {})
        if pending.get('stage') == stage:
            row = dict(pending, status='running')
        if row.get('status') != 'running':
            return None
        path = branch.root / row['directory'] / 'result/0/progress.json'
        if not path.exists():
            return None
        image = json.loads(path.read_text())[0]
        return dict(
            preview=str(path),
            image=image,
            **{k: image[k] for k in ('iterations', 'hpwl_dbu', 'estimated_wns_ps', 'timed_nets') if k in image},
        )

    def accepted(self, work, state):
        rev = revision(state)
        if not self.accepted_cache or self.accepted_cache[0] != rev:
            self.accepted_cache = (rev, Exterior(work, state).measure())
            self.journal.snapshot(rev, self.accepted_cache[1], state)
        return dict(self.accepted_cache[1], revision=rev)

    def receipt(self, trial):
        info = self.info(trial)
        info.update(
            started=trial['started'],
            epoch=trial['epoch'],
            engine=trial['engine'],
            hypothesis=trial['hypothesis'],
            selection=trial['selection'],
            edits=trial['edits'],
            requested_iterations=trial['steps'],
            baseline=trial['baseline']['preview'],
        )
        for label, future in trial['jobs'].items():
            if future.done() and not future.exception():
                row = future.result()['result']
                info['candidates'][label].update(
                    state_path=str(trial['branches'][label].path),
                    artifacts={
                        h: {k: v[k] for k in ('report', 'preview', 'positions') if k in v}
                        for h, v in future.result()['measured'].items()
                    },
                    preview=row['preview'],
                    report=row['report'],
                    timed_nets=row.get('timed_nets', 0),
                    gp_moved=row.get('gp_moved'),
                    gp_max_displacement_dbu=row.get('gp_max_displacement_dbu'),
                )
        return info

    def completed(self, journal, trial):
        with self.lock:
            journal.save(self.receipt(trial))
            self.update()

    def info(self, trial):
        result = {}
        reference = None
        for label, future in trial['jobs'].items():
            if not future.done():
                frame = self.progress(trial, label) or {}
                parts = trial['seed_scores'].copy()
                branch = trial['branches'][label]
                saved = branch.read_state()
                for home in trial['affected_homes']:
                    row = saved['results'].get(home, {}).get(trial['stage'], {})
                    if row.get('status') == 'done' and not Path(row['report']).is_absolute():
                        parts[home] = report_score(str(branch.root / row['report']))
                if 'score' in frame.get('image', {}):
                    parts[frame['home']] = frame['image']['score']
                score = trial['exterior'].measure(parts.values(), trial['stage'])
                result[label] = {
                    'status': 'running',
                    'assembled': score,
                    'frame_kind': 'GP estimate' if frame else 'checkpoint',
                    'iterations': frame.get('iterations'),
                    'running_home': frame.get('home'),
                    'hpwl_dbu': frame.get('hpwl_dbu'),
                }
            elif future.exception():
                result[label] = {'status': 'failed', 'error': str(future.exception())}
            else:
                r = future.result()
                result[label] = {
                    'status': 'done',
                    'iterations': r['iterations'],
                    'hpwl_dbu': r['hpwl_dbu'],
                    'delta_checkpoint_dbu': r['hpwl_dbu'] - trial['hpwl'],
                }
                wns = r['result'].get('estimated_wns_ps')
                result[label]['estimated_wns_ps'] = wns
                if wns is not None and trial['wns'] is not None:
                    result[label]['delta_checkpoint_wns_ps'] = wns - trial['wns']
                stage = r['result']
                if 'assembled' not in r:
                    r['assembled'] = trial['exterior'].measure(
                        [report_score(row['report']) for row in r['measured'].values()], trial['stage']
                    )
                    r['assembled']['refresh'] = sorted(
                        set(r['assembled']['refresh']) | r['state'].get('timing_refresh', {}).keys()
                    )
                assembled = r['assembled']
                result[label].update(
                    assembled=assembled,
                    frozen_homes=r['frozen_homes'],
                    per_home_iterations={h: v['iterations'] for h, v in r['measured'].items()},
                    worker_seconds=sum(v['worker_seconds'] for v in r['measured'].values()),
                    peak_rss_bytes=max(v['peak_rss_bytes'] for v in r['measured'].values()),
                    target_density=stage.get('target_density'),
                    centroid_groups=stage.get('centroid_groups', 0),
                    overflow_fraction=stage.get('overflow_fraction'),
                    peak_density=stage.get('peak_density'),
                )
                if label == 'control':
                    reference = r['hpwl_dbu']
        if reference is not None:
            for r in result.values():
                if r['status'] == 'done':
                    r['delta_control_dbu'] = r['hpwl_dbu'] - reference
                    for key in ('hpwl_um', 'estimated_wns_ps'):
                        a, b = r['assembled'][key], result['control']['assembled'][key]
                        if a is not None and b is not None:
                            r.setdefault('delta_assembled', {})[key] = a - b
                    if r['estimated_wns_ps'] is not None and result['control']['estimated_wns_ps'] is not None:
                        r['delta_control_wns_ps'] = r['estimated_wns_ps'] - result['control']['estimated_wns_ps']
        return {
            'id': trial['id'],
            'stage': trial['stage'],
            'affected_homes': trial['affected_homes'],
            'home': trial['home'],
            'checkpoint': trial['rev'],
            'baseline_hpwl_dbu': trial['hpwl'],
            'baseline_assembled': trial['base_score'],
            'score_scope': trial['scope'],
            'metric': trial['metric'],
            'baseline_estimated_wns_ps': trial['wns'],
            'timing_model': 'exported STA slack corrected for physical pin spans with distributed wire RC',
            'candidates': result,
        }

    def start(self, work, state, home, req, single):
        steps = req.get('iterations', 0 if single else 100)
        if type(steps) is not int or steps < 0:
            raise ValueError('iterations must be a nonnegative integer')
        edits = {'edit': req['edit']} if single else req['candidates']
        if not isinstance(edits, dict) or not edits or any(n in ('', 'control', 'best', 'main') for n in edits):
            raise ValueError('supply named candidate edits; control is added automatically')
        if not single:
            edits = {'control': {}, **edits}
        if len(edits) > 16:
            raise ValueError('up to 16 branches including the control can share an experiment')
        if len(self.trials) >= 32:
            closed = next((t for t in self.trials.values() if all(f.done() for f in t['jobs'].values())), None)
            if closed is None:
                raise ValueError('32 experiments are still running')
            self.journal.save(self.receipt(closed))
            del self.trials[closed['id']]
        phase = req.get('stage', self.stage)
        if phase not in ('macro', 'final'):
            raise ValueError('stage must be macro or final')
        io_edits = {}
        affected = {home}
        for label, edit in edits.items():
            if not isinstance(edit, dict):
                raise TypeError('candidate must be an edit')
            if 'io' in edit:
                io = edit['io']
                if not isinstance(io, dict) or set(io) != {'net', 'xy'} or type(io['net']) is not int:
                    raise ValueError('io needs an integer net and a perimeter xy in DBU')
                if home not in work.incident.get(io['net'], {}):
                    raise ValueError('shared net has no contact on the selected home')
                if not isinstance(io['xy'], list) or len(io['xy']) != 2:
                    raise ValueError('io needs two perimeter coordinates')
                for value in io['xy']:
                    finite(value)
                io_edits[label] = io
                affected.update(work.incident[io['net']])
        selection = req.get('names', self.selected if self.home == home else [])
        if ('stage' not in req and phase == 'final' and selection
                and state['results'][home].get('macro', {}).get('status') == 'done'
                and any(set(edit) - {'density', 'io'} for edit in edits.values())):
            geometry = self.geometry(work, state, home, self.result(work, state, home, phase), full=True)
            if any(name in geometry and geometry[name][4] for name in selection):
                phase = 'macro'
        rows = {h: self.result(work, state, h, phase) for h in [home, *sorted(affected - {home})]}
        row = rows[home]
        if any('handoff' not in r for r in rows.values()):
            raise ValueError('rerun the affected stages to export forkable handoffs')
        if steps and all(r.get('solver_movable_nodes') == 0 for r in rows.values()):
            raise ValueError('this stage has no movable nodes; select the macro stage')
        metric = req.get('metric', 'hpwl')
        if metric not in ('hpwl', 'wns'):
            raise ValueError('metric must be hpwl or wns')
        exterior = Exterior(work, state, rows)
        seed_scores = {h: report_score(r['report']) for h, r in rows.items()}
        base_score = exterior.measure(seed_scores.values(), phase)
        scope = req.get('scope', 'assembled' if base_score['hpwl_um'] is not None else 'local')
        if len(rows) > 1 and scope != 'assembled':
            raise ValueError('coordinated IO trials require assembled scores')
        if scope not in ('local', 'assembled') or (scope == 'assembled' and base_score['hpwl_um'] is None):
            raise ValueError('assembled scoring requires a complete export with pin bounds')
        if (
            metric == 'wns'
            and (base_score['estimated_wns_ps'] if scope == 'assembled' else row.get('estimated_wns_ps')) is None
        ):
            raise ValueError('no timed nets at this checkpoint; export with an SDC and timing enabled')
        geometry = self.geometry(work, state, home, row, full=True)
        selection = req.get('names', self.selected if self.home == home else [])
        if not isinstance(selection, list) or any(not isinstance(n, str) or n not in geometry for n in selection):
            raise ValueError('unknown trial selection')
        selection = list(dict.fromkeys(selection))
        if (
            phase == 'final'
            and state['results'][home].get('macro', {}).get('status') == 'done'
            and any(geometry[n][4] for n in selection)
            and any(set(edit) - {'density', 'io'} for edit in edits.values())
        ):
            raise ValueError('use the macro stage to move a memory fixed by its handoff')
        hypothesis = req.get('hypothesis', '')
        if not isinstance(hypothesis, str):
            raise TypeError('hypothesis must be text')
        densities = {}
        for label, edit in edits.items():
            if not isinstance(edit, dict):
                raise TypeError('candidate must contain an edit')
            if 'centroids' in edit and not isinstance(edit['centroids'], list):
                raise TypeError('centroids must be an array of records for this home')
            if 'density' in edit:
                densities[label] = finite(edit['density'])
                if not 0 < densities[label] <= 1:
                    raise ValueError('density must be in (0, 1]')
        image = json.loads(Path(row['preview']).read_text())[0]
        prepared = {
            label: self.layout(
                work,
                home,
                geometry,
                selection,
                {k: v for k, v in edit.items() if k not in ('density', 'io', 'centroids')},
                copy.deepcopy(state.get('holds', {}).get(home, {})),
                image,
            )
            for label, edit in edits.items()
        }
        ident = secrets.token_hex(6)
        jobs, branches = {}, {}
        engine = engine_identity(work.exe)
        for label, (poses, locks) in prepared.items():
            branch = copy.copy(work)
            branch.root = Path(tempfile.mkdtemp(dir=work.root, prefix='fork-'))
            branch.path = branch.root / 'state.json'
            branches[label] = branch
            jobs[label] = self.pool.submit(
                self.solve,
                work,
                branch,
                state,
                home,
                rows,
                poses,
                locks,
                steps,
                engine,
                densities.get(label),
                phase,
                io_edits.get(label),
                bool(io_edits),
                edits[label].get('centroids'),
            )
        trial = {
            'id': ident,
            'rev': revision(state),
            'exterior': exterior,
            'stage': phase,
            'affected_homes': list(rows),
            'base_score': base_score,
            'seed_scores': seed_scores,
            'scope': scope,
            'started': time.time(),
            'edits': edits,
            'steps': steps,
            'hypothesis': hypothesis,
            'selection': selection,
            'epoch': self.journal.epoch(),
            'engine': engine,
            'baseline': row,
            'metric': metric,
            'wns': row.get('estimated_wns_ps'),
            'hpwl': row['hpwl_dbu'],
            'home': home,
            'jobs': jobs,
            'branches': branches,
        }
        self.accepted(work, state)
        self.objective = metric
        self.trials[ident] = trial
        journal = self.journal
        journal.save(self.receipt(trial))
        for future in jobs.values():
            future.add_done_callback(lambda _, j=journal, t=trial: self.completed(j, t))
        self.update()
        return trial

    @staticmethod
    def assemble(work, state, positions_db=None):
        """A manifest of immutable final position files. Validate one home at a time."""
        parts = []
        with (
            contextlib.nullcontext(positions_db)
            if positions_db is not None
            else contextlib.closing(sqlite3.connect(''))
        ) as owners:
            columns = ', x INTEGER, y INTEGER, macro INTEGER, orientation TEXT' if positions_db is not None else ''
            owners.execute(f'CREATE TABLE owners (name TEXT PRIMARY KEY{columns}) WITHOUT ROWID')
            for row in work.rows(state, work.chunks):
                stage = row['final']
                if stage['status'] != 'done':
                    raise ValueError(f'{row["id"]} requires a current final placement')
                positions = stage.get('positions')
                count = 0
                if positions:
                    image = json.loads(Path(positions).read_text())[0]
                    if image['id'] != row['id'] or image['design'] != work.design:
                        raise ValueError('placement owner differs from the assembly')
                    x0, y0, x1, y1 = row['box_dbu']
                    if len(image['nodes']) != len(image['sizes_dbu']):
                        raise ValueError('placement lacks instance dimensions')
                    macros = {m[0] for m in image.get('macro_sizes_dbu', ())} if positions_db is not None else set()
                    for index, ((name, x, y), (width, height)) in enumerate(zip(image['nodes'], image['sizes_dbu'])):
                        if x < x0 - 0.01 or y < y0 - 0.01 or x + width > x1 + 0.01 or y + height > y1 + 0.01:
                            raise ValueError(f'{name} outside {row["id"]}')
                        if slabs := row.get('region_dbu'):
                            covered=sum(max(0,min(x+width,r)-max(x,l))*max(0,min(y+height,t)-max(y,b)) for l,b,r,t in slabs)
                            if covered < width*height - 0.01:
                                raise ValueError(f'{name} crosses the stepped perimeter of {row["id"]}')
                        try:
                            if positions_db is None:
                                owners.execute('INSERT INTO owners VALUES(?)', (name,))
                            else:
                                owners.execute(
                                    'INSERT INTO owners VALUES(?,?,?,?,?)',
                                    (
                                        name,
                                        round(x),
                                        round(y),
                                        index in macros,
                                        image.get('macro_orientations', {}).get(name),
                                    ),
                                )
                        except sqlite3.IntegrityError:
                            raise ValueError(f'{name} has multiple placement owners') from None
                        count += 1
                parts.append(
                    {
                        'home': row['id'],
                        'box': row['box_dbu'],
                        'positions': positions,
                        'owned': count,
                        'preview': stage['preview'],
                    }
                )
        return {
            'ok': True,
            'design': work.design,
            'units': 'design_dbu',
            'revision': revision(state),
            'chunks': parts,
            'metrics': Exterior(work, state).measure(),
        }

    def overview(self, work, state, objective):
        if objective not in ('hpwl', 'wns'):
            raise ValueError('objective must be hpwl or wns')
        accepted = self.accepted(work, state)
        chosen = None
        if self.view:
            ident, label = self.view
            info = self.info(self.trials[ident]) if ident in self.trials else self.journal.get(ident)
            chosen = info['candidates'][label].get('assembled')
        candidate = validation_file(work, info['candidates'][label]) if self.view else None
        basis = chosen or accepted
        best = self.journal.best(basis.get('coverage', ''), objective)
        key = 'hpwl_um' if objective == 'hpwl' else 'estimated_wns_ps'
        if (
            accepted.get('coverage') == basis.get('coverage')
            and accepted[key] is not None
            and (best is None or (accepted[key] <= best[key] if objective == 'hpwl' else accepted[key] >= best[key]))
        ):
            best = dict(trial=None, branch='main', **accepted)
        delta = info['candidates'][label].get('delta_assembled', {}) if self.view else {}
        return {
            'ok': True,
            'accepted': accepted,
            'repeated_scope_candidate': work.plan.get('repeated_scope_candidate'),
            'validation': read_validation(work.root / 'validation' / accepted['revision'] / 'latest.json'),
            'selected_validation': read_validation(candidate),
            'selected': chosen,
            'best': best,
            'objective': objective,
            'delta_control': delta,
            'selected_frame': info['candidates'][label].get('frame_kind', 'legal') if self.view else None,
        }

    def promote(self, work, ident, label, home, stage, checkpoint, epoch, engine, after, finding):
        publish_trial(work, self.journal, ident, label, checkpoint, epoch, engine, after)
        self.home, self.stage, self.view = home, stage, None
        self.accepted(work, after)
        self.journal.decide(ident, label, 'kept', finding)
        self.update()

    def command(self, work, cmd, req):
        with self.lock:
            if work is not self.work:
                raise ValueError('floorplan changed')
            state = work.state()
            home = req.get('home', self.home)
            if home not in work.chunks:
                raise ValueError('unknown home')
            if cmd == 'overview':
                return self.overview(work, state, req.get('objective', self.objective))
            if cmd in ('floorplan', 'routing'):
                if self.view:
                    ident = self.view[0]
                    record = self.trials.get(ident) or self.journal.get(ident)
                    state = self.journal.saved_main(record.get('rev') or record['checkpoint'], required=False) or {
                        'results': {},
                        'contacts': {},
                    }
                    label = self.view[1]
                    if ident in self.trials:
                        f = self.trials[ident]['jobs'][label]
                        if f.done() and not f.exception():
                            state = f.result()['state']
                    elif record['candidates'][label].get('state_path'):
                        state = json.loads(Path(record['candidates'][label]['state_path']).read_text())
                    else:
                        for h, artifacts in record['candidates'][label].get('artifacts', {}).items():
                            state['results'][h] = {record['stage']: dict(artifacts, status='done')}
                rev = revision(state)
                if cmd == 'routing':
                    return dict(work.analysis(cmd, req, state=state), view=self.view, revision=rev)
                if not self.map_cache or self.map_cache[0] != rev:
                    tiles = {}
                    quota = max(1, 6000 // max(1, len(work.jobs)))
                    for r in work.rows(state, work.jobs):
                        placed = next(
                            (r[s] for s in ('final', 'macro') if r[s]['status'] == 'done'),
                            None,
                        )
                        if placed and 'preview' in placed:
                            frame = json.loads(Path(placed['preview']).read_text())[0]
                            boxes = frame['boxes_dbu']
                            stride = max(1, math.ceil(len(boxes) / quota))
                            tiles[r['id']] = {
                                'boxes': boxes[::stride],
                                'density': frame.get('cell_density'),
                                'bins': frame['bins'],
                            }
                    self.map_cache = (rev, tiles)
                return {'ok': True, 'tiles': self.map_cache[1], 'revision': rev}
            if cmd == 'next':
                return {
                    'ok': True,
                    'chunks': priorities(work, state, req.get('objective', 'hpwl')),
                }
            if cmd == 'history':
                return {
                    'ok': True,
                    'experiments': self.journal.search(
                        req.get('query', ''), req.get('limit', 25), req.get('offset', 0)
                    ),
                }
            if cmd == 'finding':
                self.journal.get(req['trial'])
                if not isinstance(req.get('text'), str) or not req['text'].strip():
                    raise ValueError('supply finding text')
                self.journal.decide(req['trial'], req.get('branch'), 'finding', req['text'])
                self.update()
                return {'ok': True}
            if cmd == 'assemble':
                if req.get('revision'):
                    state = self.journal.saved_main(req['revision'])
                return self.assemble(work, state)
            if cmd == 'hello':
                return {'ok': True, 'mode': 'chunk', 'design': work.design, 'revision': revision(state)}
            if cmd == 'export':
                what, ident = req.get('what'), req.get('chunk', home)
                if what == 'chunks':
                    return {'ok': True, 'chunks': work.rows(state, work.chunks)}
                detail = work.show(state, ident)
                if what == 'chunk_members':
                    return {
                        'ok': True,
                        'members': [{'macro': m, 'id': n} for m, n in sorted(work.members[ident])],
                    }
                if what == 'chunk_pins':
                    return {'ok': True, 'pins': detail['ports']}
                if what == 'chunk_peers':
                    return {'ok': True, 'peers': detail['neighbors']}
                raise ValueError('available exports: chunks, chunk_members, chunk_pins, chunk_peers')
            if cmd == 'help':
                return {
                    'ok': True,
                    'usage': {
                        'select': 'home=CHUNK kind=macro match=NAME_FRAGMENT, or names=[...]',
                        'edit': 'edit={swap:true} exchanges two selected equal-footprint macros; edit={x,y,dx,dy,align:row|column,side:left|right|top|bottom,gap,fix:x|y|xy,release:x|y|xy}; DBU',
                        'trial': 'candidates={left:{dx:DBU},sparse:{density:0.72},pin:{io:{net:N,xy:[X,Y]}}} stage=macro|final iterations=100 metric=hpwl|wns scope=assembled|local keep_if=checkpoint|control',
                        'compare': 'trial=ID wait=true',
                        'projects': 'list registered workspaces; project=ID selects one for this request',
                        'restore-best': 'objective=hpwl|wns recalls the entire best comparable archived placement; undo restores the prior state',
                        'verify-wns': 'local socket: trial=ID branch=NAME openroad=PATH keep=true compares independent placement timing',
                        'resources': 'producer and worker process-group peaks, coordinator memory and concurrency',
                        'shapes': 'screen=true projects measured pin spans and boundary slots onto alternative frames',
                        'validate': 'local socket: openroad=PATH, optional trial=ID branch=NAME, route=[FIRST,LAST] and segments=true',
                        'routing': 'summary=true for totals; omit for directional demand, capacity and estimated overflow tiles',
                        'interfaces': 'pitch=DBU nets=[N,...] bus_order=true spread=true apply=false previews shared slots, ordered buses and spacing fallback',
                        'dispatch': 'local socket: stage=macro|final home=ID bundle=NEW_DIRECTORY',
                        'collect': 'local socket: bundle=RETURNED_DIRECTORY',
                        'activity': 'latest worker, trial and independent validation across registered projects',
                        'explore': 'kind=macro|density|interfaces ids=[...] iterations=N objective=hpwl|wns keep=false; interfaces accepts spread, bus_order and nets; wait=true blocks until measured',
                        'overview': 'accepted, selected, best matched-stage assembled metrics and control delta',
                        'replan': 'local socket: replace floorplan editing intent with guidance={topology:[...],geometry:[...]}; optional budget, empty guidance clears edits',
                        'pcrs': 'local socket: build the clock tree on the current assembled finals, with optional clock_port, fanout, leaf_steps, layers, max_slew_ps and balance',
                        'assemble': 'verified manifest of all current local finals, optional revision=HASH',
                        'next': 'objective=hpwl|wns|density',
                        'history': 'query=TEXT limit=N offset=N',
                        'finding': 'trial=ID branch=NAME text=OBSERVATION',
                        'replay': 'trial=ID branch=NAME restores an archived view',
                        'view': 'trial=ID branch=NAME, or branch=main',
                        'keep': 'trial=ID branch=NAME|best',
                        'measure': 'names=[A,B] or the two selected objects; center-to-center Manhattan distance',
                    },
                    'commands': [
                        'routing', 'interfaces','dispatch','collect','explore','validate','shapes','resources',
                        'floorplan',
                        'place-design', 'pcrs', 'replan',
                        'overview',
                        'assemble',
                        'next',
                        'history',
                        'finding',
                        'replay',
                        'focus',
                        'list',
                        'show',
                        'selection',
                        'find',
                        'select',
                        'scene',
                        'measure',
                        'metrics',
                        'watch',
                        'edit',
                        'trial',
                        'compare',
                        'view',
                        'keep',
                        'undo',
                        'discard',
                        'macro',
                        'final',
                        'move-io',
                    ],
                }
            if cmd == 'focus':
                stage = req.get('stage', self.stage)
                if stage not in ('macro', 'final'):
                    raise ValueError('unknown placement stage')
                homes = req.get('homes', self.homes)
                if not isinstance(homes, list) or any(h not in work.chunks for h in homes):
                    raise ValueError('unknown selected chunk')
                net = req.get('net')
                if net is not None and (
                    type(net) is not int or net not in work.incident or home not in work.incident[net]
                ):
                    raise ValueError('no such contact in this chunk')
                if home != self.home or net is not None:
                    self.selected = []
                self.home, self.homes, self.port, self.view, self.stage = (
                    home,
                    homes,
                    net,
                    None,
                    stage,
                )
                self.update()
            if cmd in ('focus', 'selection'):
                return {
                    'ok': True,
                    'home': self.home,
                    'homes': self.homes,
                    'floorplan_members': self.floorplan_selection,
                    'net': self.port,
                    'names': self.selected,
                    'revision': revision(state),
                }
            if cmd in ('find', 'select', 'measure'):
                if req.get('kind') not in (None, 'cell', 'macro'):
                    raise ValueError('kind must be cell or macro')
                stage = req.get('stage', self.stage)
                row = self.result(work, state, home, stage)
                geometry = self.geometry(work, state, home, row, full=True)
                names = req.get('names')
                if cmd == 'measure' and names is None:
                    names = self.selected if self.home == home else []
                if names is None:
                    names = [
                        n
                        for n, b in geometry.items()
                        if req.get('match', '') in n
                        and (not req.get('kind') or req['kind'] == ('macro' if b[4] else 'cell'))
                    ]
                if not isinstance(names, list) or any(not isinstance(n, str) or n not in geometry for n in names):
                    raise ValueError('unknown selection')
                if cmd == 'measure':
                    if len(names) != 2 or any(None in geometry[n][:4] for n in names):
                        raise ValueError('select two objects with known dimensions')
                    a, b = (geometry[n] for n in names)
                    dist = sum(abs((a[k] + a[k + 2] / 2) - (b[k] + b[k + 2] / 2)) for k in (0, 1))
                    return {
                        'ok': True,
                        'home': home,
                        'names': names,
                        'center_manhattan_dbu': dist,
                        'distance_um': dist / work.index['dbu_per_um'],
                    }
                offset, limit = req.get('offset', 0), req.get('limit', 64)
                if type(offset) is not int or offset < 0 or type(limit) is not int or not 1 <= limit <= 5000:
                    raise ValueError('offset must be nonnegative and limit must be between 1 and 5000')
                if cmd == 'select':
                    self.home, self.selected, self.port, self.view = (
                        home,
                        list(dict.fromkeys(names)),
                        None,
                        None,
                    )
                    self.floorplan_selection = []
                    self.stage = stage
                    if ('stage' not in req and names and all(geometry[n][4] for n in names)
                            and state['results'][home].get('macro', {}).get('status') == 'done'):
                        self.stage = 'macro'
                    self.update()
                return {
                    'ok': True,
                    'home': home,
                    'count': len(names),
                    'nodes': [
                        {
                            'name': n,
                            'kind': 'macro' if geometry[n][4] else 'cell',
                            'box': geometry[n][:4],
                        }
                        for n in names[offset : offset + limit]
                    ],
                }
            if cmd == 'replay' or (cmd=='view' and req.get('branch')!='main' and req['trial'] not in self.trials):
                record = self.journal.get(req['trial'])
                label = req['branch']
                if label not in record['candidates'] or ('preview' not in record['candidates'][label] and not record.get('kind')):
                    raise ValueError('branch has no saved placement')
                self.view = (record['id'], label)
                self.update()
                return {'ok': True}
            if cmd in ('scene', 'metrics'):
                stage = req.get('stage', self.stage)
                if stage not in ('macro', 'final'):
                    raise ValueError('unknown stage')
                if req.get('revision'):
                    state = self.journal.saved_main(req['revision'])
                row = None
                if self.view and not req.get('revision'):
                    t, label = self.view
                    stage = 'macro'
                    if t not in self.trials:
                        record = self.journal.get(t)
                        home = record['home']
                        stage = record.get('stage', 'macro')
                        row = record['candidates'][label]
                        if record.get('kind'):
                            if row.get('state_path'):
                                state = json.loads(Path(row['state_path']).read_text())
                                home = req.get('home', self.home)
                                row = self.result(work,state,home,stage)
                            else:
                                jobs=active_jobs(Path(row['directory'])) if row.get('directory') else {}
                                h=next(iter(jobs),None)
                                row=None
                                if h:
                                    path=Path(jobs[h]['directory'])/'result/0/progress.json'
                                    if path.is_file():
                                        home=h
                                        image=json.loads(path.read_text())[0]
                                        row=dict(image,preview=str(path),image=image)
                    else:
                        trial = self.trials[t]
                        home = trial['home']
                        stage = trial['stage']
                        future = trial['jobs'][label]
                        if future.done() and not future.exception():
                            row, state = (
                                future.result()['result'],
                                future.result()['state'],
                            )
                        elif not future.done():
                            row = self.progress(trial, label)
                            if row:
                                home = row['home']
                        if row is None:
                            row = trial['baseline']
                row = row or self.worker_progress(work, state, home, stage) or self.result(work, state, home, stage)
                image = row.get('image') or json.loads(Path(row['preview']).read_text())[0]
                gp = Path(row['preview']).with_name('gp.json')
                gp_image = json.loads(gp.read_text())[0] if req.get('overlay') and gp.is_file() else None
                density = {
                    k: image.get(k)
                    for k in (
                        'cell_density',
                        'target_density',
                        'peak_density',
                        'overflow_fraction',
                    )
                }
                if cmd == 'metrics':
                    bins = image.get('occupancy', [])
                    return dict(
                        ok=True,
                        home=home,
                        view=self.view,
                        iterations=row.get('iterations'),
                        hpwl_um=row['hpwl_dbu'] / work.index['dbu_per_um'],
                        estimated_wns_ps=row.get('estimated_wns_ps'),
                        timed_nets=row.get('timed_nets', 0),
                        **density,
                        occupied_fraction=sum(bins) / len(bins) if bins else None,
                        peak_bin_fraction=max(bins) if bins else None,
                        revision=revision(state),
                    )
                nodes = self.geometry(work, state, home, row, preview=image)
                if self.home == home and 'positions' in row and any(n not in nodes for n in self.selected):
                    full = self.geometry(work, state, home, row, full=True)
                    nodes.update((n, full[n]) for n in self.selected if n in full)
                ports = work.show(state, home)['ports']
                if self.view and 'report' in row:
                    feedback = json.loads(Path(row['report']).read_text())[0]
                    contacts = {p['net_index']: p['contact_dbu'] for p in feedback.get('boundary', [])}
                    ports = [dict(p, xy_dbu=contacts.get(p['net_index'], p['xy_dbu'])) for p in ports]
                return dict(
                    ok=True,
                    home=home,
                    box=work.chunks[home]['box_dbu'],
                    nodes=nodes,
                    iterations=row.get('iterations'),
                    estimated_wns_ps=row.get('estimated_wns_ps'),
                    timed_nets=row.get('timed_nets', 0),
                    ports=ports,
                    **density,
                    stage=stage,
                    gp_nodes=dict(zip(gp_image['names'], gp_image['boxes_dbu'])) if gp_image else {},
                    gp_moved=row.get('gp_moved'),
                    gp_max_displacement_dbu=row.get('gp_max_displacement_dbu'),
                    occupancy=image.get('occupancy'),
                    bins=image.get('bins'),
                    selected=self.selected if self.home == home else [],
                    view=None if req.get('revision') else self.view,
                    hpwl_dbu=row['hpwl_dbu'],
                    dbu_per_um=work.index['dbu_per_um'],
                    revision=(trial['rev'] if self.view and self.view[0] in self.trials else record['checkpoint'])
                    if self.view and not req.get('revision')
                    else revision(state),
                    version=self.version,
                )
            if cmd == 'watch':
                running = {}
                for key, stages in state['results'].items():
                    for stage, item in stages.items():
                        if item['status'] == 'running':
                            progress = self.worker_progress(work, state, key, stage) or {}
                            running[key] = {'stage': stage, 'iterations': progress.get('iterations')}
                for key, job in active_jobs(work.root).items():
                    progress = self.worker_progress(work, state, key, job['stage']) or {}
                    running[key] = dict(stage=job['stage'], iterations=progress.get('iterations'))
                shown = (self.trials.get(self.view[0]) or self.journal.get(self.view[0])) if self.view else {}
                external = [
                    r for r in reversed(self.journal.search(limit=12)) if r.get('kind') and r['id'] not in self.trials
                ]
                for record in external:
                    for item in record['candidates'].values():
                        if item.get('status') == 'running' and item.get('directory'):
                            jobs = active_jobs(Path(item['directory']))
                            item['workers'] = {h: j['stage'] for h, j in jobs.items()}
                            item['running_home'] = next(iter(jobs), None)
                            for j in jobs.values():
                                frame = Path(j['directory']) / 'result/0/progress.json'
                                if frame.exists():
                                    item['iterations'] = json.loads(frame.read_text())[0].get('iterations')
                                break
                verified = work.root / 'validation' / revision(state) / 'latest.json'
                candidates = shown.get('candidates', {})
                archived = validation_file(work, candidates.get(self.view[1], {})) if self.view else None
                return {
                    'validation_updated': verified.stat().st_mtime_ns if verified.is_file() else None,
                    'selected_validation_updated': archived.stat().st_mtime_ns if archived and archived.is_file() else None,
                    'running': running,
                    'stage': shown.get('stage', self.stage),
                    'homes': self.homes,
                    'dbu_per_um': work.index['dbu_per_um'],
                    'ok': True,
                    'version': self.version,
                    'revision': revision(state),
                    'home': shown.get('home', self.home),
                    'view': self.view,
                    'can_keep': bool(
                        shown
                        and (shown.get('rev') or shown.get('checkpoint')) == revision(state)
                        and shown.get('epoch') == self.journal.epoch()
                    ),
                    'trials': sorted(
                        [self.info(t) for t in self.trials.values()] + external, key=lambda r: r.get('started', 0)
                    ),
                    'objective': self.objective,
                }
            if cmd == 'undo':
                with work.writer() as (current, _):
                    parent = self.journal.parent(revision(current))
                    if parent is None:
                        raise ValueError('no accepted edit to undo at this revision')
                    _, previous, ident, label = parent
                    work.save(previous)
                    if ident:
                        self.journal.decide(ident, label, 'undone', 'Restored parent placement')
                    self.journal.epoch(advance=True)
                self.view = None
                self.update()
                return {'ok': True, 'revision': revision(previous)}
            if cmd in ('trial', 'edit'):
                if 'keep_if' in req and (cmd != 'trial' or req['keep_if'] not in ('checkpoint', 'control')):
                    raise ValueError('keep_if must name checkpoint or control on a trial')
                trial = self.start(work, state, home, req, cmd == 'edit')
            elif cmd == 'view' and req.get('branch') == 'main':
                self.view = None
                self.update()
                return {'ok': True}
            elif cmd in ('keep', 'compare') and req['trial'] not in self.trials:
                record = self.journal.get(req['trial'])
                if cmd == 'compare':
                    return dict(ok=True, **record)
                label = req.get('branch')
                if label not in record['candidates']:
                    raise ValueError('name the archived candidate to keep')
                row = record['candidates'][label]
                if row['status'] != 'done' or 'state_path' not in row:
                    raise ValueError('archived branch has no promotable checkpoint')
                self.promote(
                    work,
                    record['id'],
                    label,
                    record['home'],
                    record['stage'],
                    record['checkpoint'],
                    record['epoch'],
                    record['engine'],
                    json.loads(Path(row['state_path']).read_text()),
                    req.get('finding', 'Promoted archived placement'),
                )
                return dict(ok=True, kept=label, **record)
            elif cmd in ('compare', 'keep', 'discard', 'view'):
                trial = self.trials[req['trial']]
            else:
                raise ValueError('unknown live placement command')
            jobs = trial['jobs']
        if cmd == 'edit' or req.get('wait') or req.get('keep_if'):
            concurrent.futures.wait(jobs.values())
        with self.lock:
            if work is not self.work or self.trials.get(trial['id']) is not trial:
                raise ValueError('trial is no longer active')
            info = self.info(trial)
            if cmd in ('compare', 'edit', 'trial', 'keep'):
                for label, f in jobs.items():
                    if f.done() and not f.exception():
                        info['candidates'][label]['poses'] = f.result()['resolved']

            def cost(label):
                row = info['candidates'][label]
                if trial['scope'] == 'assembled':
                    return (
                        row['assembled']['hpwl_um']
                        if trial['metric'] == 'hpwl'
                        else -row['assembled']['estimated_wns_ps']
                    )
                return row['hpwl_dbu'] if trial['metric'] == 'hpwl' else -row['estimated_wns_ps']

            if cmd == 'trial' and req.get('keep_if'):
                if any(r['status'] != 'done' for r in info['candidates'].values()):
                    return dict(ok=False, error='incomplete comparison', **info)
                label = min((k for k in jobs if k != 'control'), key=cost)
                reference = (
                    cost('control')
                    if req['keep_if'] == 'control'
                    else (trial['hpwl'] if trial['metric'] == 'hpwl' else -trial['wns'])
                )
                if req['keep_if'] == 'checkpoint' and trial['scope'] == 'assembled':
                    base = trial['base_score']
                    reference = base['hpwl_um'] if trial['metric'] == 'hpwl' else -base['estimated_wns_ps']
                if cost(label) >= reference:
                    self.journal.decide(
                        trial['id'],
                        label,
                        'rejected',
                        f'No improvement against {req["keep_if"]} on {trial["scope"]} {trial["metric"]}',
                    )
                    self.update()
                    return dict(ok=True, kept=None, **info)
                cmd, req = 'keep', {'branch': label}
            if cmd == 'discard':
                if any(not f.done() for f in jobs.values()):
                    raise ValueError('trial is running')
                if self.view and self.view[0] == trial['id']:
                    self.view = None
                self.journal.save(self.receipt(trial))
                self.journal.decide(trial['id'], None, 'closed', 'Archived measured frames')
                del self.trials[trial['id']]
                self.update()
            if cmd in ('view', 'keep', 'edit'):
                label = 'edit' if cmd == 'edit' else req.get('branch', 'best')
                if label == 'best':
                    if any(r['status'] != 'done' for r in info['candidates'].values()):
                        raise ValueError('all candidates must complete before choosing best')
                    label = min(jobs, key=cost)
                future = jobs[label]
                if (not future.done() and cmd != 'view') or (future.done() and future.exception()):
                    raise ValueError('branch is unfinished or failed: ' + str(info['candidates'][label]))
                if cmd == 'view':
                    self.view = (trial['id'], label)
                else:
                    self.promote(
                        work,
                        trial['id'],
                        label,
                        trial['home'],
                        trial['stage'],
                        trial['rev'],
                        trial['epoch'],
                        trial['engine'],
                        future.result()['state'],
                        req.get('finding', 'Promoted measured placement'),
                    )
                    info['kept'] = label
                    if cmd == 'edit':
                        del self.trials[trial['id']]
                self.update()
            return dict(ok=True, **info)

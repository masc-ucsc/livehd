# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Compose physical pin bounds without opening another chunk's placement."""

import hashlib
import json
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=512)
def report_score(report):
    return json.loads(Path(report).read_text())[0].get('score')


def union(a, b):
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])] if a else b


def span(box):
    return box[2] - box[0] + box[3] - box[1]


def delay(box, rc, dbu):
    x, y = (box[2] - box[0]) / dbu, (box[3] - box[1]) / dbu
    rh, ch, rv, cv = rc
    return 0.0005 * (rh * x + rv * y) * (ch * x + cv * y)


def contribution(work, state, home):
    stages = state['results'].get(home, {})
    for stage in ('final', 'macro'):
        row = stages.get(stage, {})
        if row.get('status') == 'done':
            return stage, report_score(str(work.root / row['report']))
    for stage in ('final', 'macro'):
        row = stages.get(stage, {})
        path = row.get('previous_report') or (row.get('report') if row.get('status') == 'stale' else None)
        if path:
            return 'stale', report_score(str(work.root / path))
    return 'seed', work.jobs[home].get('seed_score')


def inputs(work, state):
    for home in work.jobs:
        yield home, *contribution(work, state, home)


def refresh_neighbors(work, state, home, before, after):
    if not before or not after:
        return
    refresh = state.setdefault('timing_refresh', {})
    refresh.pop(home, None)
    prior = dict(before['bounds'])
    for net, box in after['bounds']:
        if net in work.timed_interfaces and prior.get(net) != box:
            for peer in work.incident.get(net, {}):
                if peer != home:
                    refresh.setdefault(peer, {})[str(net)] = home


class Exterior:
    """Freeze all sibling contributions once. A fork replaces only its own pin bounds."""

    def __init__(self, work, state, active=()):
        model = work.index.get('assembly', {})
        self.active = set(active)
        self.dbu = work.index['dbu_per_um']
        self.rc = model.get('wire_rc')
        self.complete = bool(model) and len(work.jobs) == len(work.chunks)
        self.internal = model.get('fixed_dbu', 0)
        self.stages = {}
        self.bounds = dict(model.get('anchors', []))
        self.timing = {net: (slack, box) for net, slack, box, *window in model.get('timing', [])}
        self.worst = model.get('fixed_slack_ps')
        self.refresh = set(state.get('timing_refresh', {}))
        for home, stage, part in inputs(work, state):
            if home in self.active:
                continue
            self.stages[home] = stage
            if any(r['status'] == 'stale' for r in state['results'].get(home, {}).values()):
                self.refresh.add(home)
            if part is None:
                self.complete = False
                continue
            self.internal += part['internal_dbu']
            value = part.get('internal_slack_ps')
            if value is not None:
                self.worst = min(self.worst, value) if self.worst is not None else value
            for net, box in part['bounds']:
                self.bounds[net] = union(self.bounds.get(net), box)
        # Fold nets independent of the active worker, including their timing.
        changed = {n for h in self.active for _, n in work.jobs[h].get('score_nets', [])}
        for net in list(self.bounds):
            if net in changed:
                continue
            box = self.bounds.pop(net)
            self.internal += span(box)
            value = self.slack(net, box)
            if value is not None:
                self.worst = min(self.worst, value) if self.worst is not None else value

        self.timing = {n: self.timing[n] for n in changed if n in self.timing}

    def slack(self, net, box):
        if net in self.timing:
            slack, seed = self.timing[net]
            return slack + delay(seed, self.rc, self.dbu) - delay(box, self.rc, self.dbu)

    def measure(self, parts=(), stage=None):
        homes = dict(self.stages)
        if stage:
            homes.update(dict.fromkeys(self.active, stage))
        stages = list(homes.values())
        fidelity = hashlib.sha256(json.dumps(homes, sort_keys=True).encode()).hexdigest()
        result = {
            'stages': {s: stages.count(s) for s in ('seed', 'macro', 'final', 'stale')},
            'coverage': fidelity,
            'refresh': sorted(self.refresh),
            'hpwl_um': None,
            'estimated_wns_ps': None,
        }
        if not self.complete or any(p is None for p in parts):
            return result
        bounds = self.bounds.copy()
        length, worst = self.internal, self.worst
        for part in parts:
            length += part['internal_dbu']
            value = part.get('internal_slack_ps')
            if value is not None:
                worst = min(value, worst) if worst is not None else value
            for net, box in part['bounds']:
                bounds[net] = union(bounds.get(net), box)
        for net, box in bounds.items():
            length += span(box)
            value = self.slack(net, box)
            if value is not None:
                worst = min(value, worst) if worst is not None else value
        result.update(
            hpwl_um=length / self.dbu,
            estimated_wns_ps=min(0.0, worst) if worst is not None else None,
        )
        return result


def priorities(work, state, objective):
    if objective not in ('hpwl', 'wns', 'density'):
        raise ValueError('objective must be hpwl, wns or density')
    model = work.index.get('assembly', {})
    bounds = dict(model.get('anchors', []))
    contributors = {}
    cost, worst, critical = {}, {}, {}
    for home, _, part in inputs(work, state):
        if not part:
            continue
        cost[home] = part['internal_dbu']
        worst[home] = part.get('internal_slack_ps')
        for net, box in part['bounds']:
            bounds[net] = union(bounds.get(net), box)
            contributors.setdefault(net, []).append(home)
    for net, homes in contributors.items():
        share = span(bounds[net]) / len(homes)
        for home in homes:
            cost[home] += share
    for net, slack, seed, *window in model.get('timing', []):
        if net not in bounds:
            continue
        rc, dbu = model['wire_rc'], work.index['dbu_per_um']
        change = delay(bounds[net], rc, dbu) - delay(seed, rc, dbu)
        value = slack - change
        for home in contributors.get(net, []):
            if worst[home] is None or value < worst[home]:
                worst[home] = value
                critical[home] = {
                    'net': net,
                    'estimated_slack_ps': value,
                    'arrival_ps': window[0] + change if window else None,
                    'required_ps': window[1] if window else None,
                }
    ranked = []
    for row in work.rows(state, work.jobs):
        home = row['id']
        macro, final = row['macro'], row['final']
        stage = 'final' if final['status'] in ('done', 'stale') and macro['status'] != 'stale' else 'macro'
        r = row[stage]
        stale = r['status'] == 'stale'
        missing = r['status'] != 'done'
        reason = r.get('reason') or (f'{stage} placement {r["status"]}' if missing else '')
        score = None
        if objective == 'hpwl':
            if home in cost:
                score = cost[home] / work.index['dbu_per_um']
                reason = reason or f'{score:.2f} µm including its share of crossing nets'
        elif objective == 'wns':
            slack = worst.get(home)
            if slack is not None:
                score = -slack
                reason = reason or f'estimated worst slack {slack:.2f} ps'
        else:
            score = r.get('overflow_fraction')
            if score is not None:
                reason = reason or f'{100 * score:.2f}% cell area over target'
        dirty = state.get('timing_refresh', {}).get(home, {})
        if objective == 'wns' and dirty and not stale:
            net = next(iter(dirty))
            reason = f'{dirty[net]} changed timed net {net}; refresh the boundary estimate'
        rss = r.get('peak_rss_bytes', row['estimated_bytes'])
        seconds = r.get('worker_seconds')
        # An alternating boundary position should be negotiated before repeating either worker.
        oscillating = [
            int(n)
            for n, history in state.get('contact_history', {}).items()
            if len(history) >= 3 and history[-1] == history[-3] and home in work.incident.get(int(n), {})
        ]
        ranked.append(
            (
                (
                    -stale,
                    -missing,
                    -(objective == 'wns' and bool(dirty)),
                    score is None,
                    -(score or 0),
                    seconds or 0,
                    home,
                ),
                {
                    'home': home,
                    'stage': stage,
                    'reason': reason or 'no measurement yet',
                    'score': score,
                    'objective': objective,
                    'worker_seconds': seconds,
                    'estimated_peak_bytes': rss,
                    'critical_boundary': critical.get(home),
                    'timing_refresh_nets': list(map(int, dirty)),
                    'action': 'negotiate_io' if oscillating else stage,
                    'oscillating_nets': oscillating,
                },
            )
        )
    return [entry for _, entry in sorted(ranked)]

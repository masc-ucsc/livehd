#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Inventory frozen cellboxes and diagnostic probes; never fit from attachment labels.

Results are explicit wrappers: {box: absolute_path, box_sha256: digest,
result: probe_output, process: {returncode, elapsed_seconds, timed_out}}.
Current probe schema 1 has no complete box certificate. All such labels remain
ineligible; a new completion schema needs a separate reviewed acceptance rule.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    data = path.read_bytes()
    return json.loads(data), digest(data)


def member_identity(source_id, box):
    """Pose, menu, box index and result cannot change a member set's fold."""
    members = sorted(cell['name'] for cell in box['cells'])
    if not members or len(set(members)) != len(members):
        raise ValueError('cellbox members must be nonempty and unique')
    key = digest(json.dumps([source_id, members], separators=(',', ':')).encode())
    return key, 'heldout' if int(key[:16], 16) % 5 == 0 else 'train'


def bucket(value):
    for ceiling in (0, 2, 4, 8):
        if value <= ceiling:
            return str(ceiling)
    return '9+'


def box_features(box):
    cells = box['cells']
    rows = {cell['name']: cell['y'] for cell in cells}
    counts = Counter()
    unresolved = []
    unresolved_all = []
    for net in box['nets']:
        pins = net['pins']
        inside = [pin for pin in pins if pin.get('in_box')]
        if net.get('power') or not inside:
            continue
        if not net.get('routed'):
            unresolved_all.append(net['name'])
        if net.get('clock'):
            counts['clock_nets'] += 1
            counts['clock_pins'] += len(inside)
            continue
        external = len(inside) != len(pins)
        kind = 'external' if external else 'internal'
        counts[kind + '_nets'] += 1
        counts[kind + '_pins'] += len(inside)
        if not external:
            counts['internal_singleton_nets'] += len(inside) == 1
            member_rows = {rows[p['owner']] for p in inside if p['owner'] in rows}
            counts['current_multirow_internal_nets'] += len(member_rows) > 1
        if not net.get('routed'):
            unresolved.append(net['name'])
    fields = ('internal_nets', 'external_nets', 'internal_pins', 'external_pins',
              'internal_singleton_nets', 'current_multirow_internal_nets',
              'clock_nets', 'clock_pins')
    features = {field: counts[field] for field in fields}
    site = box.get('site_dbu', 0)
    features.update(member_count=len(cells), current_member_rows=len(set(rows.values())),
                    member_width_sites=(sum(c['width_dbu'] for c in cells) / site if site > 0 else None),
                    free_segments=box.get('free_segments', []),
                    unresolved_ordinary_scope_nets=sorted(unresolved),
                    unresolved_scope_nets=sorted(unresolved_all))
    return features


def label_status(result, process):
    """Separate successful diagnostics, censored searches and unsupported labels."""
    process = process if isinstance(process, dict) else {}
    nets = result.get('nets')
    valid_nets = isinstance(nets, list) and all(isinstance(net, dict) for net in nets)
    reasons = [result.get('first_failure', '')]
    reasons += [net.get('reason', '') for net in nets if net.get('ok') is not True] if valid_nets else []
    text = ' '.join(str(reason) for reason in reasons).lower()
    censored = process.get('timed_out') is True or any(
        word in text for word in ('timeout', 'timed out', 'deadline', 'budget'))
    if censored:
        outcome = 'censored_search'
    elif process.get('returncode') is not None and (type(process['returncode']) is not int
                                                  or process['returncode'] not in (0, 1)):
        outcome = 'process_error'
    elif process.get('returncode') is None:
        outcome = 'missing_process_record'
    elif 'exhaust' in text:
        outcome = 'candidate_graph_exhausted'
    elif nets is None or nets == []:
        outcome = 'no_routing_jobs'
    elif not valid_nets:
        outcome = 'invalid_routing_jobs'
    elif result.get('signal_jobs_ok') is True and all(net.get('ok') is True for net in nets):
        outcome = 'diagnostic_jobs_connected_or_attached'
    else:
        outcome = 'diagnostic_incomplete'
    blockers = ['execution_context_unverified']
    if outcome in ('missing_process_record', 'no_routing_jobs', 'invalid_routing_jobs', 'process_error'):
        blockers.append(outcome)
    if result.get('measurement_schema') != 1:
        blockers.append('missing_or_unsupported_measurement_schema')
    if result.get('calibration_eligible') is not True:
        blockers.append('producer_did_not_certify_calibration')
    obligations = result.get('unverified_obligations')
    if not isinstance(obligations, list):
        blockers.append('missing_obligation_inventory')
    elif obligations:
        blockers.extend(str(item) for item in obligations)
    # Even a manually edited all_ok/calibration_eligible flag cannot turn schema 1
    # into a certificate of terminal scope, placement, ports and physical unions.
    blockers.append('no_supported_complete_box_certificate')
    if censored:
        blockers.append('censored_search')
    return {'eligible': False, 'outcome': outcome, 'blockers': sorted(set(blockers))}


def inventory(design, source_id, dump_dir, results_dir=None):
    if not design or not source_id:
        raise ValueError('design and source-id must be nonempty')
    boxes = []
    lookup = {}
    classes = Counter()
    for path in sorted(Path(dump_dir).glob('box_*.json')):
        path = path.resolve()
        box, sha = read_json(path)
        identity, fold = member_identity(source_id, box)
        features = box_features(box)
        key = '/'.join(bucket(features[field]) for field in ('member_count', 'internal_nets', 'external_nets'))
        entry = {'path': str(path), 'sha256': sha, 'member_identity': identity,
                 'fold': fold, 'class': key, 'features': features, 'measurements': []}
        boxes.append(entry)
        lookup[str(path)] = entry
        classes[key] += 1
    if not boxes:
        raise ValueError('dump directory contains no box_*.json files')
    unmatched = []
    for path in sorted(Path(results_dir).rglob('*.json')) if results_dir else []:
        raw = path.read_bytes()
        reason = None
        try:
            wrapper = json.loads(raw)
        except (ValueError, UnicodeError):
            wrapper = {}
            reason = 'invalid_json'
        if not isinstance(wrapper, dict):
            wrapper, reason = {}, 'not_a_result_wrapper'
        box_path = wrapper.get('box')
        if reason is None and (not isinstance(box_path, str) or not Path(box_path).is_absolute()):
            reason = 'missing_absolute_box_identity'
        entry = lookup.get(str(Path(box_path).resolve())) if reason is None else None
        if reason is None and entry is None:
            reason = 'box_not_in_cohort'
        if reason is None and wrapper.get('box_sha256') != entry['sha256']:
            reason = 'box_hash_mismatch'
        if reason is None and 'source_id' in wrapper and wrapper['source_id'] != source_id:
            reason = 'source_id_mismatch'
        result, process = wrapper.get('result'), wrapper.get('process')
        if reason is None and not isinstance(result, dict):
            reason = 'missing_result_record'
        record = {'path': str(path.resolve()), 'sha256': digest(raw)}
        if reason:
            unmatched.append(dict(record, reason=reason))
            continue
        context = {key: wrapper[key] for key in ('binary_sha256', 'config', 'config_sha256',
                                                'frame_sha256', 'source_id') if key in wrapper}
        context['verified'] = False
        entry['measurements'].append(dict(record, label=label_status(result, process), process=process,
                                         execution_context=context,
                                         certificate_status=result.get('certificate_status'),
                                         internal_connectivity_connected=result.get('internal_connectivity_connected'),
                                         external_attachment_jobs=result.get('external_attachment_jobs')))
    measurements = [measurement for box in boxes for measurement in box['measurements']]
    return {'schema': 1, 'design': design, 'source_id': source_id, 'boxes': boxes,
            'class_key': ['member_count', 'internal_nets', 'external_nets'],
            'bucket_upper_bounds': [0, 2, 4, 8], 'class_counts': dict(sorted(classes.items())),
            'fold_counts': dict(Counter(box['fold'] for box in boxes)),
            'measurement_outcomes': dict(Counter(m['label']['outcome'] for m in measurements)),
            'measurement_blockers': dict(Counter(b for m in measurements for b in m['label']['blockers'])),
            'unmatched_results': unmatched,
            'calibration': {'status': 'withheld_no_eligible_completion_labels', 'eligible_labels': 0,
                            'reason': 'Current probe schema measures fixed-pose connectivity/attachments only; no table is fitted.'},
            'limitations': ['Row features describe current member origins, not minimum routing rows.',
                            'Clock flags are inventory only; no shared terminal-level reset/scope manifest exists.',
                            'Candidate exhaustion and censored search do not prove physical unroutability.',
                            'Only exact member sets share a fold; regrouped overlapping boxes or shared cells can cross folds.',
                            'This is not a held-out-design split; small classes do not establish 99% coverage.',
                            'Box hashes are checked; binary, configuration, frame and execution context are unverified.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('design', 'source-id', 'dump-dir', 'out'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--results-dir')
    args = parser.parse_args()
    try:
        report = inventory(args.design, args.source_id, args.dump_dir, args.results_dir)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    Path(args.out).write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()

# SPDX-License-Identifier: BSD-3-Clause
"""Fit directional scale on paired same-pose coarse/OpenROAD observations.

Input JSON: {"pairs":[{"coarse":"export-directory", "detailed":"sample-directory"}]}.
Detailed sample must retain independently established pose_sha256/netlist_sha256
and explicit openroad_detailed provenance. It must have layer_groups.bin from
our detailed extractor (H/V for M2-3, M4-5, M6+). Unknown or mismatched identity
is rejected; matching top names or feature maps alone are not proof of pairing.
"""
import argparse
import json
import math
from pathlib import Path

from .coarse import DOMAIN, validate_metadata
from .maps import decode
from .pool import canonical, sha


def _read_map(directory, name, digest):
    data = (directory / name).read_bytes()
    if sha(data) != digest:
        raise ValueError(f'calibration map digest mismatch: {name}')
    result = decode(data)
    if result.normalized or min(result.values) < 0:
        raise ValueError('calibration requires raw nonnegative maps')
    return result, sha(data)


def read_pair(pair):
    coarse, detailed = Path(pair['coarse']), Path(pair['detailed'])
    c = json.loads((coarse / 'metadata.json').read_text())
    d = json.loads((detailed / ('sample.json' if (detailed / 'sample.json').exists() else 'metadata.json')).read_text())
    validate_metadata(c)
    global_reference = d.get('label_source') == 'openroad_global' and d.get('status') == 'global' and d.get('complete') is True
    detailed_reference = d.get('label_source') == 'openroad_detailed' and d.get('status') == 'routed'
    if not c['complete'] or not (global_reference or detailed_reference):
        raise ValueError('calibration requires complete coarse and explicitly detailed routed labels')
    for key in ('pose_sha256', 'netlist_sha256', 'technology', 'top', 'core', 'dbu_per_micron'):
        if key not in d or c.get(key) != d[key]:
            raise ValueError(f'unmatched physical pair: {key}')
    # Capacity definitions intentionally differ; scales are bound to both.
    if not d.get('directional_definition') or not d.get('capacity_sha256'):
        raise ValueError('missing detailed capacity/directional provenance')
    cm, ch = _read_map(coarse, 'directional_labels.bin', c['directional_labels_sha256'])
    dm, dh = (_read_map(detailed, 'directional_labels.bin', d['directional_labels_sha256']) if global_reference
              else _read_map(detailed, 'layer_groups.bin', d['files']['layer_groups.bin']))
    if cm.channels != 2 or dm.channels != (2 if global_reference else 6) or (cm.bins_x, cm.bins_y) != (dm.bins_x, dm.bins_y):
        raise ValueError('directional calibration grid/channel mismatch')
    n = cm.bins_x * cm.bins_y
    x = [cm.values[:n], cm.values[n:]]
    y = ([dm.values[:n], dm.values[n:]] if global_reference else
         [[max(dm.values[(2 * group + direction) * n + i] for group in range(3)) for i in range(n)] for direction in range(2)])
    evidence = {key: c[key] for key in ('top', 'pose_sha256', 'netlist_sha256')}
    evidence.update(coarse_map_sha256=ch, detailed_map_sha256=dh, detailed_capacity_sha256=d['capacity_sha256'])
    return c, d, x, y, evidence


def calibrate(pairs, heldout_designs, max_relative_rmse):
    if not heldout_designs or not math.isfinite(max_relative_rmse) or max_relative_rmse < 0:
        raise ValueError('explicit held-out designs and nonnegative error threshold required')
    observations = [read_pair(pair) for pair in pairs]
    if not observations:
        raise ValueError('no paired observations')
    domain = {key: observations[0][0][key] for key in DOMAIN}
    definition = observations[0][1]['directional_definition']
    reference_source = observations[0][1]['label_source']
    for c, d, _, _, _ in observations:
        if {key: c[key] for key in DOMAIN} != domain or d['directional_definition'] != definition or d['label_source'] != reference_source:
            raise ValueError('mixed calibration domains/definitions')
    hold = set(heldout_designs)
    fit = [o for o in observations if o[0]['top'] not in hold]
    test = [o for o in observations if o[0]['top'] in hold]
    if not fit or not test or {o[0]['top'] for o in test} != hold:
        raise ValueError('calibration requires fit and all held-out designs')
    if len({(o[0]['pose_sha256'], o[0]['netlist_sha256']) for o in observations}) != len(observations):
        raise ValueError('duplicate physical pair')
    scales = []
    for direction in range(2):
        xx = sum(a * a for o in fit for a in o[2][direction])
        xy = sum(a * b for o in fit for a, b in zip(o[2][direction], o[3][direction]))
        if xx <= 0 or xy <= 0:
            raise ValueError('direction lacks nonzero calibration evidence')
        scales.append(xy / xx)
    reports = []
    for c, _, x, y, evidence in observations:
        metrics = []
        for direction in range(2):
            residual = sum((scales[direction] * a - b) ** 2 for a, b in zip(x[direction], y[direction]))
            energy = sum(b * b for b in y[direction])
            if energy <= 0:
                raise ValueError('zero detailed energy cannot validate directional calibration')
            metrics.append(math.sqrt(residual / energy))
        reports.append(dict(evidence, heldout=c['top'] in hold, relative_rmse_hv=metrics))
    worst = max(v for r in reports if r['heldout'] for v in r['relative_rmse_hv'])
    return dict(calibration_version=1, method='positive_directional_scale_through_origin', domain=domain,
                detailed_directional_definition=definition, reference_label_source=reference_source, scale_hv=scales,
                fit_designs=sorted({o[0]['top'] for o in fit}), heldout_designs=sorted(hold), pairs=reports,
                max_relative_rmse=max_relative_rmse, heldout_worst_relative_rmse=worst,
                accepted=worst <= max_relative_rmse, physical_routing_claim=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pairs', required=True, type=Path)
    parser.add_argument('--holdout', required=True, action='append')
    parser.add_argument('--max-relative-rmse', required=True, type=float)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = calibrate(json.loads(args.pairs.read_text())['pairs'], args.holdout, args.max_relative_rmse)
    args.output.write_bytes(canonical(result))
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()

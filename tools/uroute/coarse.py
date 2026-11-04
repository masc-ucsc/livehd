# SPDX-License-Identifier: BSD-3-Clause
"""Ingest unpenalized legroute observations; never manufacture detailed labels.

python -m tools.uroute.coarse --export DIR --pool POOL [--calibration JSON]
"""
import argparse
import json
import math
from pathlib import Path
import re

from .maps import decode, raw
from .pool import VERSIONS, canonical, publish_sample, sha

POLICIES = ('detailed', 'coarse-only', 'calibrated-mixed')
DOMAIN = ('technology', 'capacity_sha256', 'endpoint_basis', 'admission_share')


def validate_metadata(m):
    if (m.get('status') != 'coarse' or m.get('label_source') != 'legroute_coarse'
            or m.get('map_semantics') != 'raw_unpenalized' or m.get('physically_routed') is not False):
        raise ValueError('expected explicitly non-physical raw legroute_coarse observation')
    for name in ('pose_sha256', 'netlist_sha256', 'capacity_sha256'):
        if not re.fullmatch('[0-9a-f]{64}', m.get(name, '')):
            raise ValueError(f'invalid coarse {name}')
    if not isinstance(m.get('endpoint_basis'), str) or not m['endpoint_basis']:
        raise ValueError('missing coarse endpoint basis')
    if type(m.get('complete')) is not bool:
        raise ValueError('missing complete flag')
    for key in ('unrouted', 'search_limited'):
        if type(m.get(key)) is not int or m[key] < 0:
            raise ValueError('invalid unresolved count')
    if m['complete'] and (m['unrouted'] or m['search_limited']):
        raise ValueError('complete coarse sample contains unresolved routes')
    expected = ['utilization'] if m['complete'] else []
    if m.get('eligible_tasks') != expected:
        raise ValueError('coarse samples are utilization-only, and incomplete samples are ineligible')
    share = m.get('admission_share')
    if not isinstance(share, (float, int)) or not math.isfinite(share) or not 0 < share <= .95:
        raise ValueError('invalid coarse admission share')


def eligible_metrics(record, policy='detailed'):
    if policy not in POLICIES:
        raise ValueError('unknown label source policy')
    if record.get('status') == 'routed' and record.get('label_source', 'openroad_detailed') == 'openroad_detailed':
        return () if policy == 'coarse-only' else (0, 1)
    if record.get('status') != 'coarse':
        return ()
    validate_metadata(record)
    if not record['complete'] or policy == 'detailed':
        return ()
    calibrated = 'calibration_sha256' in record
    return (0,) if calibrated == (policy == 'calibrated-mixed') else ()


def validate_calibration(c, metadata):
    if c.get('calibration_version') != 1 or c.get('accepted') is not True:
        raise ValueError('calibration has not passed its held-out acceptance criterion')
    if c.get('domain') != {key: metadata[key] for key in DOMAIN}:
        raise ValueError('calibration domain mismatch')
    if c.get('reference_label_source') != 'openroad_detailed':
        raise ValueError('OpenROAD global calibration does not authorize mixing with detailed labels')
    scales = c.get('scale_hv')
    if not isinstance(scales, list) or len(scales) != 2 or any(not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0 for v in scales):
        raise ValueError('invalid directional calibration scales')
    fit, held = c.get('fit_designs'), c.get('heldout_designs')
    if not fit or not held or set(fit) & set(held):
        raise ValueError('calibration requires disjoint fit/held-out designs')
    return scales


def ingest(export, pool, calibration=None):
    export = Path(export)
    metadata = json.loads((export / 'metadata.json').read_text())
    validate_metadata(metadata)
    if any(metadata.get(k) != v for k, v in VERSIONS.items()):
        raise ValueError('incompatible coarse export versions')
    files = {}
    for name, key in (('features.bin', 'features_sha256'), ('directional_labels.bin', 'directional_labels_sha256')):
        files[name] = (export / name).read_bytes()
        if sha(files[name]) != metadata[key]:
            raise ValueError('coarse export digest mismatch')
    features, directional = decode(files['features.bin']), decode(files['directional_labels.bin'])
    if (directional.channels, directional.bins_x, directional.bins_y, directional.normalized) != (2, 256, 256, False):
        raise ValueError('expected raw two-channel directional map')
    if min(directional.values) < 0:
        raise ValueError('negative coarse utilization')
    scales = [1., 1.]
    extras = {'directional_labels.bin': files['directional_labels.bin'], 'export_metadata.json': canonical(metadata)}
    if calibration is not None:
        if not metadata['complete']:
            raise ValueError('cannot calibrate unresolved route observations')
        data = Path(calibration).read_bytes()
        scales = validate_calibration(json.loads(data), metadata)
        extras['calibration.json'] = data
        metadata['calibration_sha256'] = sha(data)
    area = directional.bins_x * directional.bins_y
    utilization = [max(scales[0] * directional.values[i], scales[1] * directional.values[area + i]) for i in range(area)]
    # One actual label channel, not three with invented zero DRC/detour.
    return publish_sample(pool, metadata, features, raw([utilization], 256, 256), extra_files=extras)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', required=True, type=Path)
    parser.add_argument('--pool', required=True, type=Path)
    parser.add_argument('--calibration', type=Path)
    args = parser.parse_args()
    print(json.dumps(ingest(args.export, args.pool, args.calibration), sort_keys=True))


if __name__ == '__main__':
    main()

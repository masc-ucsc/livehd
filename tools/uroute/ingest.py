# SPDX-License-Identifier: BSD-3-Clause
"""Recompute physical routed features, extract wires/DRC and atomically ingest.

No routing or training is launched. Both native readers operate on a private
copy of the supplied DEF, so its placement, net identities and wires agree.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile

from .extract import extract, text_file, verify_lengths
from .features import NETWORK_BINS, features
from .maps import encode, read
from .pool import VERSIONS, canonical, publish_sample, sha


def feature_gap(before, after):
    if (before.channels, before.bins_x, before.bins_y, before.normalized) != (4, after.bins_x, after.bins_y, True):
        raise ValueError('incompatible LiveHD export for feature-gap measurement')
    size = after.bins_x * after.bins_y
    result = []
    for c in range(4):
        difference = [a - b for a, b in zip(before.values[c * size:(c + 1) * size], after.values[c * size:(c + 1) * size])]
        result.append({'normalized_rmse': math.sqrt(math.fsum(d * d for d in difference) / size),
                       'normalized_mean_absolute': math.fsum(map(abs, difference)) / size,
                       'normalized_max_absolute': max(map(abs, difference)),
                       'livehd_raw_range': before.ranges[c], 'routed_raw_range': after.ranges[c]})
    return result


# Optional controls with their defaults; hand-built namespaces may omit them.
OPTIONAL = {'odb': None, 'openroad': None, 'odb_timeout': 300, 'overflow_unavailable': None}


def ingest_options(args=None, **fields):
    """A namespace for ingest(): args' attributes and fields over OPTIONAL defaults."""
    return argparse.Namespace(**{**OPTIONAL, **(vars(args) if args is not None else {}), **fields})


def ingest(args, odb_provenance=None):
    """odb_provenance is set only by the internal ODB-to-DEF conversion."""
    args = ingest_options(args)
    unavailable = args.overflow_unavailable
    if unavailable is not None and (not isinstance(unavailable, str) or not unavailable.strip() or len(unavailable) > 4096 or '\0' in unavailable):
        raise ValueError('--overflow-unavailable requires a nonempty reason')
    if unavailable is not None and (args.status != 'unrouted' or args.last_global_overflow is not None):
        raise ValueError('--overflow-unavailable requires --status unrouted and no supplied overflow map')
    if args.odb is not None:
        from .odb import export_odb
        if args.openroad is None:
            raise ValueError('--odb requires an explicit --openroad executable')
        with tempfile.TemporaryDirectory(prefix='uroute_odb_') as temporary:
            definition, lengths, overflow, provenance = export_odb(
                args.odb, args.openroad, temporary, routed=args.status == 'routed',
                overflow=args.status == 'unrouted' and args.last_global_overflow is None and unavailable is None,
                timeout=args.odb_timeout)
            converted = ingest_options(args, odb=None, definition=definition,
                                       wirelength_report=args.wirelength_report or lengths,
                                       last_global_overflow=args.last_global_overflow or overflow)
            return ingest(converted, provenance)
    definition = text_file(args.definition)
    definition_hash = sha(definition.encode())
    with tempfile.TemporaryDirectory(prefix='uroute_ingest_') as scratch:
        scratch = Path(scratch)
        definition_path = scratch / 'routed.def'
        definition_path.write_text(definition)
        geometry_path, metric_path = scratch / 'geometry.json', scratch / 'physical.json'
        command = [str(args.placement_metrics.resolve()), '--def', str(definition_path), '--liberty', str(args.liberty.resolve()),
                   '--controls', str(args.controls.resolve()), '--output', str(metric_path), '--uroute-geometry', str(geometry_path)]
        for lef in args.lef:
            command += ['--lef', str(lef.resolve())]
        subprocess.run(command, check=True, timeout=120)
        geometry = json.loads(geometry_path.read_text())
        if odb_provenance and odb_provenance.get('global_overflow'):
            frame = odb_provenance['global_overflow']
            if frame['core'] != geometry['core'] or frame['dbu_per_micron'] != geometry['dbu']:
                raise ValueError('ODB overflow and physical feature geometry have different core/DBU frames')
        bins = args.bins
        livehd = None
        if args.livehd_export:
            livehd = json.loads((args.livehd_export / 'metadata.json').read_text())
            if any(type(livehd.get(k)) is not int or livehd[k] != v for k, v in VERSIONS.items()):
                raise ValueError('unsupported LiveHD export version')
            if sha((args.livehd_export / 'features.bin').read_bytes()) != livehd['features_sha256']:
                raise ValueError('LiveHD feature digest mismatch')
            if (livehd['top'] != geometry['top'] or livehd['dbu_per_micron'] != geometry['dbu']
                    or livehd['core'] != geometry['core']):
                raise ValueError('LiveHD export top/DBU/core differs from routed geometry')
            bins = livehd['route_grid'][0] if bins is None else bins
            route_y = args.bins if args.bins is not None else livehd['route_grid'][1]
        else:
            route_y = bins or 64
        bins = bins or 64
        computed = features(geometry, bins, route_y)
        # Native reference also validates actual routed feature interchange.
        native_features = scratch / 'features.bin'
        subprocess.run([str(args.native.resolve()), 'features', str(geometry_path), str(native_features),
                        str(bins), str(route_y), str(NETWORK_BINS), str(NETWORK_BINS)], check=True, timeout=120)
        if encode(computed) != native_features.read_bytes():
            raise ValueError('routed Python/native feature mismatch')
        lefs = sorted(sha(path.read_bytes()) for path in args.lef)
        technology = 'lef:' + sha(':'.join(lefs).encode())
        macros = ''.join(f'{n["name"]}:{n["master"]}:{n["width"]:.0f}:{n["height"]:.0f};'
                         for n in geometry['nodes'] if n.get('macro') and not n.get('mask'))
        die = geometry['die']
        core = geometry['core']
        utilization = args.utilization
        if utilization is None:
            if livehd:
                utilization = livehd['floorplan']['utilization']
            else:
                raise ValueError('--utilization is required without a LiveHD export (the pool stores target utilization)')
        metadata = {'top': geometry['top'], 'technology': technology, 'source': args.source, 'status': args.status,
                    'floorplan': {'die_width': (die[2] - die[0]) / geometry['dbu'],
                                  'die_height': (die[3] - die[1]) / geometry['dbu'],
                                  'utilization': utilization, 'macro_signature': sha(macros.encode())},
                    'routed_def_sha256': definition_hash, 'dbu_per_micron': geometry['dbu'],
                    'core': core, 'route_grid': [bins, route_y], 'network_grid': [NETWORK_BINS, NETWORK_BINS],
                    'liberty_sha256': sha(args.liberty.read_bytes()), 'controls_sha256': sha(args.controls.read_bytes()),
                    'extractor': 'uroute-def-centerlines-v1'}
        files = {'geometry.json': geometry_path.read_bytes(), 'physical.json': metric_path.read_bytes()}
        if odb_provenance is not None:
            metadata['source_odb_sha256'] = odb_provenance['source_odb_sha256']
            files['odb_export.json'] = canonical(odb_provenance)
        if livehd:
            before = read(args.livehd_export / 'features.bin')
            files['livehd_features.bin'] = encode(before)
            files['livehd_metadata.json'] = canonical(livehd)
            metadata['feature_gap'] = feature_gap(before, read(native_features))
        if args.status == 'unrouted':
            if not args.last_global_overflow and unavailable is None:
                raise ValueError('unrouted result requires --last-global-overflow or an explicit --overflow-unavailable reason')
            return publish_sample(args.pool, metadata, read(native_features),
                                  last_global_overflow=read(args.last_global_overflow) if args.last_global_overflow else None,
                                  overflow_unavailable=unavailable, extra_files=files)
        if not args.drc:
            raise ValueError('routed result requires --drc (an empty report explicitly represents zero markers)')
        lengths = scratch / 'flute.json'
        subprocess.run([str(args.native.resolve()), 'trees', str(geometry_path), str(lengths)], check=True, timeout=120)
        drc = text_file(args.drc)
        labels, groups, report = extract(geometry, definition, drc, json.loads(lengths.read_text()), bins, route_y)
        metadata['drc_sha256'] = sha(drc.encode())
        if args.wirelength_report:
            reference = text_file(args.wirelength_report)
            metadata['wirelength_verification'] = verify_lengths(report, reference, geometry['dbu'])
            files['openroad_wirelength.txt'] = reference.encode()
        files.update({'layer_groups.bin': encode(groups), 'extraction.json': canonical(report), 'flute.json': lengths.read_bytes()})
        return publish_sample(args.pool, metadata, read(native_features), labels, extra_files=files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--def', dest='definition', type=Path)
    source.add_argument('--odb', type=Path)
    parser.add_argument('--openroad', type=Path, help='explicit external OpenROAD executable for ODB input')
    parser.add_argument('--odb-timeout', type=float, default=300)
    parser.add_argument('--lef', action='append', type=Path, required=True)
    parser.add_argument('--liberty', type=Path, required=True)
    parser.add_argument('--controls', type=Path, required=True, help='JSON with prects_controls clock/reset roots and provenance')
    parser.add_argument('--pool', type=Path, required=True)
    parser.add_argument('--placement-metrics', type=Path, default=Path('bazel-bin/tools/placement_metrics'))
    parser.add_argument('--native', type=Path, default=Path('bazel-bin/tools/uroute/native'))
    parser.add_argument('--drc', type=Path)
    parser.add_argument('--wirelength-report', type=Path, help='optional OpenROAD report_wire_length -detailed_route -file output')
    parser.add_argument('--livehd-export', type=Path)
    parser.add_argument('--last-global-overflow', type=Path)
    parser.add_argument('--overflow-unavailable', help='explicit reason no global-route map exists, e.g. failure before global routing')
    parser.add_argument('--status', choices=('routed', 'unrouted'), default='routed')
    parser.add_argument('--source', choices=('livehd', 'external'), required=True)
    parser.add_argument('--utilization', type=float)
    parser.add_argument('--bins', type=int)
    args = parser.parse_args()
    print(json.dumps(ingest(args), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
